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
# Linked into rawaccel_common_shim (a shared library), so the static objects
# must be position-independent. Harmless for the executables that also link it.
set_target_properties(rawaccel_json_io PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Cross-OS C-ABI shim over common/. Builds a modifier from a driver-config
# JSON (via rawaccel_json_io) and evaluates the full modifier::modify pipeline
# behind an opaque handle, so .NET callers P/Invoke a stable C surface and the
# preview matches the math the agent/kernel run instead of reimplementing it.
add_library(rawaccel_common_shim SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/../shim/ra_curve.cpp
)
target_include_directories(rawaccel_common_shim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../shim
)
target_link_libraries(rawaccel_common_shim PRIVATE rawaccel_common rawaccel_json_io)
set_target_properties(rawaccel_common_shim PROPERTIES
    OUTPUT_NAME rawaccel_common
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

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

# Curve shim tests: exercise the C ABI the GUI preview P/Invokes, asserting it
# faithfully runs modifier::modify (parity against a direct call).
add_executable(shim_tests
    tests/test_main.cpp
    tests/shim_tests.cpp
)
target_include_directories(shim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shim)
target_link_libraries(shim_tests PRIVATE rawaccel_common_shim rawaccel_json_io)
add_test(NAME shim_tests COMMAND shim_tests)

# Step 5/6 agent: control plane + BPF backend. The agent owns active
# modifier_settings and the 1s WriteDelay debounce; the BPF backend owns
# hidraw enumeration, HID descriptor parsing, LUT precomputation, and the
# fmod_ret/hid_bpf_device_event program attach lifecycle.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBBPF REQUIRED IMPORTED_TARGET libbpf)

add_library(rawaccel_agent STATIC
    agent/agent.cpp
    agent/control_server.cpp
    agent/hid_descriptor.cpp
    agent/lut_builder.cpp
    agent/bpf_capability.cpp
    agent/bpf_backend.cpp
)
target_include_directories(rawaccel_agent PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/agent
    ${CMAKE_CURRENT_SOURCE_DIR}/bpf
)
target_link_libraries(rawaccel_agent PUBLIC rawaccel_json_io PkgConfig::LIBBPF)

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

# Step 9 diagnostic: probe every hidraw node and report whether the parser
# accepts it for BPF rewriting. Run on a target host before enabling BPF.
add_executable(rawaccel-hid-probe tools/rawaccel-hid-probe.cpp)
target_link_libraries(rawaccel-hid-probe PRIVATE rawaccel_agent)

# Step 10 HID-BPF kernel program: compile rawaccel.bpf.c via clang -target bpf
# and verifier-check it via a libbpf loader. clang + bpftool are required;
# rawaccel-agentd cannot run without rawaccel.bpf.o.
find_program(CLANG_EXE NAMES clang REQUIRED)
find_program(BPFTOOL_EXE NAMES bpftool REQUIRED)

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

find_package(Threads REQUIRED)
add_executable(agent_tests
    tests/test_main.cpp
    tests/agent_tests.cpp
    tests/control_server_tests.cpp
    tests/control_server_socket_tests.cpp
    tests/hid_descriptor_tests.cpp
    tests/lut_builder_tests.cpp
    tests/bpf_backend_tests.cpp
)
target_link_libraries(agent_tests PRIVATE rawaccel_agent Threads::Threads)
add_test(NAME agent_tests COMMAND agent_tests)
