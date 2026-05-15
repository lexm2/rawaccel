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

enable_testing()

# Step 2 portability check: compiles only the math headers and instantiates
# a ra::modifier. If this builds, common/ is consumable from Linux.
add_executable(compile_check tests/compile_check.cpp)
target_link_libraries(compile_check PRIVATE rawaccel_common)
add_test(NAME compile_check COMMAND compile_check)
