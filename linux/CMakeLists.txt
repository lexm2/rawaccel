cmake_minimum_required(VERSION 3.16)
project(rawaccel-linux CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Step 5 agent skeleton: control plane state + UDS server. The agent owns
# the active modifier_settings and applies the 1s WriteDelay debounce that
# mirrors driver/driver.cpp:446-451.
add_library(rawaccel_agent STATIC
    agent/agent.cpp
    agent/control_server.cpp
)
target_include_directories(rawaccel_agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/agent)
target_link_libraries(rawaccel_agent PUBLIC rawaccel_json_io)

add_executable(rawaccel-agentd agent/main.cpp)
target_link_libraries(rawaccel-agentd PRIVATE rawaccel_agent)

find_package(Threads REQUIRED)
add_executable(agent_tests
    tests/test_main.cpp
    tests/agent_tests.cpp
    tests/control_server_tests.cpp
    tests/control_server_socket_tests.cpp
)
target_link_libraries(agent_tests PRIVATE rawaccel_agent Threads::Threads)
add_test(NAME agent_tests COMMAND agent_tests)
