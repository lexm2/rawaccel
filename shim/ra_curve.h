// Cross-OS C ABI for the userspace curve preview. Wraps the common/ modifier
// pipeline behind an opaque handle so .NET callers (Linux/Windows) can preview
// via P/Invoke without C++/CLI or the driver. Plain C header; C++ dispatch in .cpp.
//
// Built from a driver-config JSON (the cross-OS settings shape agent and
// wrapper consume) and evaluated via ra_curve_modify -> rawaccel::modifier::modify,
// the exact path the agent LUT builder and Windows driver use. Keeps the
// preview in lockstep with the HID-BPF program instead of reimplementing the
// per-profile math (range weight, DPI, anisotropy) in C#.

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

// Build a curve handle from a driver-config JSON string (UTF-8). Shape is
// RawAccel.Contracts.RawAccelConfig / rajson::driver_config: top-level object
// with "defaultDeviceConfig", "profiles", "devices". Uses profiles[0]; rest ignored.
//
// Smoother halflives are zeroed so the preview is the steady-state curve
// (matching the BPF LUT), not EMA warmup. Returns null on parse failure, empty
// profiles, or OOM. Caller owns the handle; release with ra_curve_destroy.
RA_API ra_curve_t* ra_curve_create_from_config_json(const char* config_json);

// Free a handle from ra_curve_create_from_config_json; null is a no-op.
RA_API void ra_curve_destroy(ra_curve_t* curve);

// Evaluate the modifier at one sample. Mirrors ManagedAccel.Accelerate /
// rawaccel::modifier::modify: (x, y) raw counts, dpi_factor = NORMALIZED_DPI /
// device DPI (1.0 for the device-independent chart), time_ms the time slice
// (1.0 in preview). Result written to out_x/out_y (both must be non-null);
// null curve passes input through.
RA_API void ra_curve_modify(const ra_curve_t* curve,
                            double x, double y,
                            double dpi_factor, double time_ms,
                            double* out_x, double* out_y);

// ABI version; bump on any layout/signature change that breaks callers.
RA_API uint32_t ra_curve_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif
