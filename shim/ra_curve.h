// Cross-OS C ABI for the userspace curve preview. Wraps the full common/
// modifier pipeline behind an opaque handle so .NET callers (Linux and
// Windows) can evaluate the live preview via P/Invoke without depending on
// C++/CLI or the kernel driver. The header is plain C; the .cpp side does the
// C++ dispatch.
//
// The handle is built from a driver-config JSON (the same cross-OS settings
// shape the agent and wrapper consume) and evaluated with ra_curve_modify,
// which runs rawaccel::modifier::modify -- the exact code path the agent's
// LUT builder and the Windows driver use. This keeps the Linux preview in
// lockstep with what the HID-BPF program actually applies, instead of
// reimplementing the per-profile math (range weight, DPI, anisotropy) in C#.

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

typedef struct ra_curve ra_curve_t;

// Build a curve handle from a driver-config JSON string (UTF-8). The shape is
// RawAccel.Contracts.RawAccelConfig / the agent's rajson::driver_config: a
// top-level object with "defaultDeviceConfig", "profiles", and "devices". The
// handle is built from the first entry in "profiles"; the rest is ignored.
//
// The smoother halflives are zeroed internally so the preview reflects the
// steady-state curve (matching the curve-only LUT the BPF program loads),
// not any EMA warmup. Returns null on parse failure, an empty profile list,
// or allocation failure. Caller owns the handle and must release it with
// ra_curve_destroy.
RA_API ra_curve_t* ra_curve_create_from_config_json(const char* config_json);

// Free a curve previously returned by ra_curve_create_from_config_json. Null
// is a no-op.
RA_API void ra_curve_destroy(ra_curve_t* curve);

// Evaluate the modifier at one input sample. Mirrors ManagedAccel.Accelerate
// and rawaccel::modifier::modify: (x, y) are raw counts for this sample,
// dpi_factor is the device DPI normalized against NORMALIZED_DPI (1.0 for the
// device-independent chart), time_ms is the time slice (1.0 in the preview).
// The post-acceleration components are written to out_x / out_y. If curve is
// null the input is passed through unchanged. out_x and out_y must be
// non-null.
RA_API void ra_curve_modify(const ra_curve_t* curve,
                            double x, double y,
                            double dpi_factor, double time_ms,
                            double* out_x, double* out_y);

// ABI version. Bump if the struct layout or function signatures change in
// a way that breaks existing callers.
RA_API uint32_t ra_curve_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif
