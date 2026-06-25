cmake_minimum_required(VERSION 3.16)
project(rawaccel-linux CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

# Instrumented test builds (global, so libs are covered). run-tests.sh uses a
# per-mode dir (build-asan/, build-cov/); default build/ is uninstrumented.
option(RA_SANITIZE "Build with AddressSanitizer + UBSan + LeakSanitizer" OFF)
option(RA_COVERAGE "Build with clang source-based coverage instrumentation" OFF)

if(RA_SANITIZE)
    add_compile_options(-fsanitize=address,undefined
                        -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

if(RA_COVERAGE)
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate -fcoverage-mapping)
endif()

# Header-only view of common/ for Linux. win_compat.hpp is force-included so
# common/'s MSVC-isms (_copysign, __forceinline) resolve on clang/gcc.
add_library(rawaccel_common INTERFACE)
target_include_directories(rawaccel_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/agent
)
target_compile_options(rawaccel_common INTERFACE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/agent/win_compat.hpp
    # g++14+ rejects common/'s `cap_mode cap_mode = ...` under -Wchanges-meaning;
    # tolerate it rather than restructure shared code.
    -Wno-changes-meaning
)

include(FetchContent)

# Prefer system nlohmann_json; else fetch v3.11.3 at configure time (one-time
# download, then cached in build/_deps).
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    # TODO: pin URL_HASH once verified against an authoritative source.
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/include.zip
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# JSON layer mirroring wrapper/wrapper.cpp's JsonProperty schema.
add_library(rawaccel_json_io STATIC agent/json_io.cpp)
target_include_directories(rawaccel_json_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/agent)
target_link_libraries(rawaccel_json_io PUBLIC rawaccel_common nlohmann_json::nlohmann_json)
# Linked into a shared lib, so must be PIC.
set_target_properties(rawaccel_json_io PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Cross-OS C-ABI shim over common/: builds a modifier from driver-config JSON and
# runs modifier::modify behind an opaque handle, giving .NET a stable P/Invoke
# surface whose preview matches the agent/kernel math.
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

# Portability check: compiles the math headers and instantiates a ra::modifier.
add_executable(compile_check tests/compile_check.cpp)
target_link_libraries(compile_check PRIVATE rawaccel_common)
add_test(NAME compile_check COMMAND compile_check)

# Parity tests: a subset of wrapper-tests/ ported to native C++. Smoothers and the
# synchronous-curve simulator are reimplemented so parity is real, not circular.
add_executable(parity_tests
    tests/test_main.cpp
    tests/end_to_end_tests.cpp
    tests/speed_tests.cpp
    tests/synchronous_accel_tests.cpp
)
target_link_libraries(parity_tests PRIVATE rawaccel_common)
add_test(NAME parity_tests COMMAND parity_tests)

# JSON roundtrip tests against the Windows JsonProperty schema.
add_executable(json_tests
    tests/test_main.cpp
    tests/json_roundtrip_tests.cpp
)
target_link_libraries(json_tests PRIVATE rawaccel_json_io)
target_compile_definitions(json_tests PRIVATE
    RA_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default_config.json")
add_test(NAME json_tests COMMAND json_tests)

# Curve shim tests: exercise the C ABI the GUI preview P/Invokes, asserting parity
# against a direct modifier::modify call.
add_executable(shim_tests
    tests/test_main.cpp
    tests/shim_tests.cpp
)
target_include_directories(shim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shim)
target_link_libraries(shim_tests PRIVATE rawaccel_common_shim rawaccel_json_io)
target_compile_definitions(shim_tests PRIVATE
    RA_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default_config.json")
add_test(NAME shim_tests COMMAND shim_tests)

# C++ data-plane + math library: LUT precomputation, the BPF attach/bind lifecycle,
# and the kernel-capability probe. The Rust daemon drives it over the C ABI; libbpf
# and curve math stay in C++. Feeds ra_backend (hence PIC).
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBBPF REQUIRED IMPORTED_TARGET libbpf)

add_library(rawaccel_agent STATIC
    agent/lut_builder.cpp
    agent/bpf_capability.cpp
    agent/bpf_backend.cpp
)
target_include_directories(rawaccel_agent PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/agent
    ${CMAKE_CURRENT_SOURCE_DIR}/bpf
)
target_link_libraries(rawaccel_agent PUBLIC rawaccel_json_io PkgConfig::LIBBPF)
set_target_properties(rawaccel_agent PROPERTIES POSITION_INDEPENDENT_CODE ON)

# C ABI over the data plane + curve math for the Rust daemon (the only C++ artifact
# shipped at runtime); math and libbpf glue come from rawaccel_agent.
add_library(ra_backend SHARED agent/ra_backend.cpp)
target_include_directories(ra_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/agent
    ${CMAKE_CURRENT_SOURCE_DIR}/bpf
)
target_link_libraries(ra_backend PUBLIC rawaccel_agent rawaccel_json_io PkgConfig::LIBBPF)
install(TARGETS ra_backend LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

# `cmake --install build` deploys the C++ artifacts (libra_backend.so, curve shim,
# BPF object) and the systemd unit; cargo installs the daemon binary separately.
install(TARGETS rawaccel_common_shim LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

pkg_check_modules(SYSTEMD systemd)
if(SYSTEMD_FOUND)
    pkg_get_variable(RA_SYSTEMD_UNIT_DIR systemd systemdsystemunitdir)
endif()
if(NOT RA_SYSTEMD_UNIT_DIR)
    set(RA_SYSTEMD_UNIT_DIR /usr/lib/systemd/system)
endif()

# The unit points at the cargo-installed daemon and the installed BPF object.
set(RA_AGENT_BIN ${CMAKE_INSTALL_FULL_BINDIR}/rawaccel-agentd)
set(RA_BPF_OBJECT_PATH ${CMAKE_INSTALL_FULL_LIBDIR}/rawaccel/rawaccel.bpf.o)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/rawaccel-agentd.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/rawaccel-agentd.service
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rawaccel-agentd.service
        DESTINATION ${RA_SYSTEMD_UNIT_DIR})

# Diagnostic probe (`rawaccel-agentd --probe`) lives in the Rust daemon now.

# HID-BPF kernel program: rawaccel.bpf.c via clang -target bpf. clang + bpftool
# required; rawaccel-agentd cannot run without rawaccel.bpf.o.
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

# __TARGET_ARCH_x86 gates CO-RE relocations to the host arch (arm64 on ARM hosts).
add_custom_command(
    OUTPUT ${RA_BPF_OBJ}
    DEPENDS ${RA_BPF_DIR}/rawaccel.bpf.c
            ${RA_BPF_DIR}/rawaccel_bpf_layout.h
            ${RA_BPF_DIR}/rawaccel_fixedpoint.h
            ${RA_VMLINUX_H}
    COMMAND ${CLANG_EXE}
            -O2 -g -Wall
            # newer clang flags empty struct decls in vmlinux.h; kernel-side noise.
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

# Install where the systemd unit's --bpf-object (RA_BPF_OBJECT_PATH) points.
install(FILES ${RA_BPF_OBJ} DESTINATION ${CMAKE_INSTALL_LIBDIR}/rawaccel)

add_executable(rawaccel-bpf-verify tools/rawaccel-bpf-verify.cpp)
target_link_libraries(rawaccel-bpf-verify PRIVATE PkgConfig::LIBBPF)
add_dependencies(rawaccel-bpf-verify rawaccel_bpf_object)
add_test(NAME bpf_verify
    COMMAND rawaccel-bpf-verify --object ${RA_BPF_OBJ})
# Exit 77 means skipped (no CAP_BPF / RLIMIT_MEMLOCK), so unprivileged boxes don't
# fail CI while `sudo ctest` still runs the verifier.
set_tests_properties(bpf_verify PROPERTIES SKIP_RETURN_CODE 77)

# Data-plane tests: LUT precomputation, the BPF backend, and the capability probe.
find_package(Threads REQUIRED)
add_executable(dataplane_tests
    tests/test_main.cpp
    tests/lut_builder_tests.cpp
    tests/bpf_backend_tests.cpp
)
target_link_libraries(dataplane_tests PRIVATE rawaccel_agent Threads::Threads)
add_test(NAME dataplane_tests COMMAND dataplane_tests)

# Fixed-point parity tests: drive rawaccel_fixedpoint.h (the exact in-kernel math)
# and assert it matches the double-precision common/ modifier.
add_executable(fixedpoint_tests
    tests/test_main.cpp
    tests/fixedpoint_tests.cpp
)
target_link_libraries(fixedpoint_tests PRIVATE rawaccel_agent)
add_test(NAME fixedpoint_tests COMMAND fixedpoint_tests)

# BPF scenario tests: push fake packet sequences through the fixed-point pipeline +
# ra_emit_q16, checking carry, speed clamp, directional DPI, smoothing warmup, and
# idle pass-through against the common/ oracle.
add_executable(sequence_tests
    tests/test_main.cpp
    tests/sequence_tests.cpp
)
target_link_libraries(sequence_tests PRIVATE rawaccel_agent)
add_test(NAME sequence_tests COMMAND sequence_tests)

# Per-packet latency gate: time the fixed-point pipeline over the full
# curve/distance/flag matrix, assert p99 < 125 us (the 8 kHz budget). Skipped on
# sanitized runs (instrumentation skews timing) via `ctest -E latency_bench`.
add_executable(latency_bench
    tests/test_main.cpp
    tests/latency_bench.cpp
)
target_link_libraries(latency_bench PRIVATE rawaccel_agent)
add_test(NAME latency_bench COMMAND latency_bench)

# FFI lifecycle leak test: loop the C ABI create/bind/health/speed/detach/destroy
# cycle under RA_SANITIZE so LSan proves the libbpf/Slot/JSON paths leak nothing.
# Links ra_backend directly (pure C++ -> one ASan runtime, no Rust mismatch).
add_executable(lifecycle_tests
    tests/test_main.cpp
    tests/lifecycle_tests.cpp
)
target_link_libraries(lifecycle_tests PRIVATE ra_backend)
target_compile_definitions(lifecycle_tests PRIVATE
    RA_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default_config.json")
add_test(NAME lifecycle_tests COMMAND lifecycle_tests)
