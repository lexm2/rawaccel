#pragma once

// Linux adapter for MSVC-isms in common/. Force-included by rawaccel_common so shared headers build under clang/gcc.

#include <math.h>

// MSVC intrinsic in rawaccel.hpp -> POSIX copysign
inline double _copysign(double x, double y) { return ::copysign(x, y); }

// MSVC keyword in accel-lookup.hpp / rawaccel.hpp (_KERNEL_MODE)
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif
