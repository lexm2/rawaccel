// Cross-OS C ABI for the userspace curve preview. Wraps common/accel-*.hpp
// in an opaque handle so .NET callers (Linux and Windows) can evaluate a
// single curve via P/Invoke without depending on C++/CLI or the kernel
// driver. The header is plain C; the .cpp side does the C++ dispatch.

#ifndef RAWACCEL_SHIM_RA_CURVE_H
#define RAWACCEL_SHIM_RA_CURVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  ifdef RA_SHIM_BUILD
#    define RA_API __declspec(dllexport)
#  else
#    define RA_API __declspec(dllimport)
#  endif
#else
#  define RA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Integer encodings mirror RawAccel.Contracts.AccelMode and the underlying
// rawaccel::accel_mode (positional, declaration order preserved).
//   0 classic, 1 jump, 2 natural, 3 synchronous, 4 power, 5 lookup, 6 noaccel
//
// cap_mode: 0 io (in_out), 1 in (input), 2 out (output).

struct ra_accel_args {
    int32_t mode;
    int32_t gain;
    double input_offset;
    double output_offset;
    double acceleration;
    double decay_rate;
    double gamma;
    double motivity;
    double exponent_classic;
    double scale;
    double exponent_power;
    double limit;
    double sync_speed;
    double smooth;
    double cap_x;
    double cap_y;
    int32_t cap_mode;
    int32_t length;
    // Length-prefixed LUT for accel_mode::lookup; ignored otherwise. May be
    // null when length is 0. The shim copies length entries internally.
    const float* data;
};

typedef struct ra_curve ra_curve_t;

// Build a curve from args. Returns null on allocation failure or invalid
// arguments. Caller owns the handle and must release with ra_curve_destroy.
RA_API ra_curve_t* ra_curve_create(const struct ra_accel_args* args);

// Free a curve previously returned by ra_curve_create. Null is a no-op.
RA_API void ra_curve_destroy(ra_curve_t* curve);

// Evaluate the curve at speed and return the raw sensitivity scale (no
// range-weight applied; that is the caller's job per profile config).
// Returns 1.0 if curve is null.
RA_API double ra_curve_evaluate(const ra_curve_t* curve, double speed);

// ABI version. Bump if the struct layout or function signatures change in
// a way that breaks existing callers.
RA_API uint32_t ra_curve_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif
