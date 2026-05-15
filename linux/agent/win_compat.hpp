#pragma once

// Linux-side adapter for MSVC-isms in common/. Force-included by the
// rawaccel_common CMake INTERFACE target so the shared math headers
// compile under clang/gcc without modifying common/.

#include <math.h>

// MSVC intrinsic used in rawaccel.hpp; map to POSIX copysign.
inline double _copysign(double x, double y) { return ::copysign(x, y); }

// MSVC keyword used in accel-lookup.hpp (and rawaccel.hpp under _KERNEL_MODE).
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif
