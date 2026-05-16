cmake_minimum_required(VERSION 3.16)
project(rawaccel-linux CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

# Header-only view of common/ for the Linux side. Anything that consumes
# the shared math links to this target instead of poking at common/ directly.
# The win_compat.hpp shim is force-included so the MSVC-isms in common/
# (_copysign, __forceinline) resolve on clang/gcc with zero edits to common/.
add_library(rawaccel_common INTERFACE)
target_include_directories(rawaccel_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/agent
)
target_compile_options(rawaccel_common INTERFACE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/agent/win_compat.hpp
    # common/rawaccel-base.hpp has `cap_mode cap_mode = cap_mode::out;` which
    # MSVC accepts but g++14+ rejects under -Wchanges-meaning. Tolerate it on
    # the Linux side rather than restructuring shared code.
    -Wno-changes-meaning
)

# Cross-OS C-ABI shim over common/. Exposes the curve dispatch from
# accel_union via an opaque handle so .NET callers P/Invoke a stable C
# surface instead of pinning to common/'s C++ layout.
add_library(rawaccel_common_shim SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/../shim/ra_curve.cpp
)
target_include_directories(rawaccel_common_shim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../shim
)
target_link_libraries(rawaccel_common_shim PRIVATE rawaccel_common)
set_target_properties(rawaccel_common_shim PROPERTIES
    OUTPUT_NAME rawaccel_common
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

include(FetchContent)

# Prefer a system-installed nlohmann_json (e.g. pacman -S nlohmann-json).
# Fall back to fetching the v3.11.3 single-header release from upstream at
# configure time. Requires internet on the fresh configure; subsequent
# builds reuse the cached copy in build/_deps.
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    # TODO: pin URL_HASH once verified against an authoritative source.
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/include.zip
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# Step 4 JSON layer mirroring wrapper/wrapper.cpp's JsonProperty schema.
add_library(rawaccel_json_io STATIC agent/json_io.cpp)
target_include_directories(rawaccel_json_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/agent)
target_link_libraries(rawaccel_json_io PUBLIC rawaccel_common nlohmann_json::nlohmann_json)

enable_testing()

# Step 2 portability check: compiles only the math headers and instantiates
# a ra::modifier. If this builds, common/ is consumable from Linux.
add_executable(compile_check tests/compile_check.cpp)
target_link_libraries(compile_check PRIVATE rawaccel_common)
add_test(NAME compile_check COMMAND compile_check)

# Step 3 parity tests: representative subset of wrapper-tests/, ported to
# native C++. Smoothers and the synchronous-curve simulator are reimplemented
# independently inside the test files so parity is checked, not tautological.
add_executable(parity_tests
    tests/test_main.cpp
    tests/end_to_end_tests.cpp
    tests/speed_tests.cpp
    tests/synchronous_accel_tests.cpp
)
target_link_libraries(parity_tests PRIVATE rawaccel_common)
add_test(NAME parity_tests COMMAND parity_tests)

# Step 4 JSON roundtrip tests against the Windows JsonProperty schema.
add_executable(json_tests
    tests/test_main.cpp
    tests/json_roundtrip_tests.cpp
)
target_link_libraries(json_tests PRIVATE rawaccel_json_io)
add_test(NAME json_tests COMMAND json_tests)

# Step 5/6 agent: control plane + per-device evdev/uinput backend. The agent
# owns active modifier_settings and the 1s WriteDelay debounce; the evdev
# backend owns udev hotplug, EVIOCGRAB, uinput mirroring, and the per-packet
# read/transform/write loop (driver/driver.cpp:84-131 mirror).
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)

add_library(rawaccel_agent STATIC
    agent/agent.cpp
    agent/control_server.cpp
    agent/evdev_processor.cpp
    agent/evdev_io.cpp
    agent/evdev_device.cpp
    agent/udev_watcher.cpp
    agent/evdev_backend.cpp
    agent/trace_format.cpp
    agent/trace_replay.cpp
    agent/hid_descriptor.cpp
    agent/lut_builder.cpp
)
target_include_directories(rawaccel_agent PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/agent
    ${CMAKE_CURRENT_SOURCE_DIR}/bpf
)
target_link_libraries(rawaccel_agent PUBLIC rawaccel_json_io PkgConfig::UDEV)

# Step 12: BPF backend + capability probe. Compiled when libbpf is present
# (clang/bpftool are only required for the .bpf.c build above). The agent
# library always links the capability probe so --backend=auto can run on
# any host; only BpfBackend itself depends on libbpf.
if(LIBBPF_FOUND)
    target_sources(rawaccel_agent PRIVATE
        agent/bpf_capability.cpp
        agent/bpf_backend.cpp
    )
    target_link_libraries(rawaccel_agent PUBLIC PkgConfig::LIBBPF)
    target_compile_definitions(rawaccel_agent PUBLIC RA_HAVE_LIBBPF=1)
endif()

add_executable(rawaccel-agentd agent/main.cpp)
target_link_libraries(rawaccel-agentd PRIVATE rawaccel_agent)

# Install rules. `cmake --install build` deploys the daemon, the curve
# shim, and a systemd unit pointing at the installed binary path. The
# systemd unit dir is queried via pkg-config when present, otherwise
# falls back to /usr/lib/systemd/system.
install(TARGETS rawaccel-agentd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS rawaccel_common_shim LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

pkg_check_modules(SYSTEMD systemd)
if(SYSTEMD_FOUND)
    pkg_get_variable(RA_SYSTEMD_UNIT_DIR systemd systemdsystemunitdir)
endif()
if(NOT RA_SYSTEMD_UNIT_DIR)
    set(RA_SYSTEMD_UNIT_DIR /usr/lib/systemd/system)
endif()

set(RA_AGENT_BIN ${CMAKE_INSTALL_FULL_BINDIR}/rawaccel-agentd)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/rawaccel-agentd.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/rawaccel-agentd.service
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rawaccel-agentd.service
        DESTINATION ${RA_SYSTEMD_UNIT_DIR})

# Step 8 calibration tools. rawaccel-replay drives a trace through the
# processor for cross-OS parity diffs; rawaccel-latency pairs raw vs
# uinput SYN_REPORTs to characterize tail latency on real hardware.
add_executable(rawaccel-replay tools/rawaccel-replay.cpp)
target_link_libraries(rawaccel-replay PRIVATE rawaccel_agent)

add_executable(rawaccel-latency tools/rawaccel-latency.cpp)
target_link_libraries(rawaccel-latency PRIVATE rawaccel_agent)

# Step 9 diagnostic: probe every hidraw node and report whether the parser
# accepts it for BPF rewriting. Run on a target host before enabling BPF.
add_executable(rawaccel-hid-probe tools/rawaccel-hid-probe.cpp)
target_link_libraries(rawaccel-hid-probe PRIVATE rawaccel_agent)

# Step 10 HID-BPF kernel program: compile rawaccel.bpf.c via clang -target bpf
# and verifier-check it via a libbpf loader. Gated on clang + bpftool being
# present so a host without them can still build the rest of the agent.
find_program(CLANG_EXE NAMES clang)
find_program(BPFTOOL_EXE NAMES bpftool)
pkg_check_modules(LIBBPF IMPORTED_TARGET libbpf)

if(CLANG_EXE AND BPFTOOL_EXE AND LIBBPF_FOUND)
    set(RA_BPF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bpf)
    set(RA_VMLINUX_H ${CMAKE_CURRENT_BINARY_DIR}/vmlinux.h)
    set(RA_BPF_OBJ ${CMAKE_CURRENT_BINARY_DIR}/rawaccel.bpf.o)

    add_custom_command(
        OUTPUT ${RA_VMLINUX_H}
        COMMAND ${BPFTOOL_EXE} btf dump file /sys/kernel/btf/vmlinux format c
                > ${RA_VMLINUX_H}
        COMMENT "Generating vmlinux.h from /sys/kernel/btf/vmlinux"
        VERBATIM
    )

    # __TARGET_ARCH_x86 is the convention libbpf-bootstrap uses to gate
    # CO-RE relocations to the host arch. ARM hosts would set arm64.
    add_custom_command(
        OUTPUT ${RA_BPF_OBJ}
        DEPENDS ${RA_BPF_DIR}/rawaccel.bpf.c
                ${RA_BPF_DIR}/rawaccel_bpf_layout.h
                ${RA_VMLINUX_H}
        COMMAND ${CLANG_EXE}
                -O2 -g -Wall
                # vmlinux.h has a handful of empty struct decls in nested
                # scopes that newer clang flags; they are kernel-side noise.
                -Wno-missing-declarations
                -target bpf
                -D__TARGET_ARCH_x86
                -I${CMAKE_CURRENT_BINARY_DIR}
                -I${RA_BPF_DIR}
                -c ${RA_BPF_DIR}/rawaccel.bpf.c
                -o ${RA_BPF_OBJ}
        COMMENT "clang -target bpf -> rawaccel.bpf.o"
        VERBATIM
    )
    add_custom_target(rawaccel_bpf_object ALL DEPENDS ${RA_BPF_OBJ})

    add_executable(rawaccel-bpf-verify tools/rawaccel-bpf-verify.cpp)
    target_link_libraries(rawaccel-bpf-verify PRIVATE PkgConfig::LIBBPF)
    add_dependencies(rawaccel-bpf-verify rawaccel_bpf_object)
    add_test(NAME bpf_verify
        COMMAND rawaccel-bpf-verify --object ${RA_BPF_OBJ})
    # Exit 77 from rawaccel-bpf-verify when CAP_BPF / RLIMIT_MEMLOCK is
    # absent means "skipped, not failed" -- avoids a noisy CI failure on
    # unprivileged developer boxes while still letting `sudo ctest` exercise
    # the verifier end to end.
    set_tests_properties(bpf_verify PROPERTIES SKIP_RETURN_CODE 77)
else()
    message(STATUS
        "BPF backend disabled (clang=${CLANG_EXE} bpftool=${BPFTOOL_EXE} "
        "libbpf_found=${LIBBPF_FOUND}). Install all three to enable.")
endif()

find_package(Threads REQUIRED)
add_executable(agent_tests
    tests/test_main.cpp
    tests/agent_tests.cpp
    tests/control_server_tests.cpp
    tests/control_server_socket_tests.cpp
    tests/evdev_processor_tests.cpp
    tests/evdev_device_tests.cpp
    tests/grab_registry_tests.cpp
    tests/per_device_state_tests.cpp
    tests/trace_replay_tests.cpp
    tests/hid_descriptor_tests.cpp
    tests/lut_builder_tests.cpp
    tests/bpf_backend_tests.cpp
)
target_link_libraries(agent_tests PRIVATE rawaccel_agent Threads::Threads)
add_test(NAME agent_tests COMMAND agent_tests)
