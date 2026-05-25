#define RA_SHIM_BUILD 1
#include "ra_curve.h"

#include <rawaccel.hpp>
#include <json_io.hpp>

#include <new>

namespace ra = rawaccel;

// Holds the parsed settings plus a modifier built from them. The modifier's
// ctor captures output_dpi / NORMALIZED_DPI, so it must be constructed from
// the same (halflife-zeroed, init_data'd) settings the handle stores. settings
// is declared before mod so the ctor init list can hand it to mod.
struct ra_curve {
    ra::modifier_settings settings;
    ra::modifier mod;

    explicit ra_curve(const ra::modifier_settings& s)
        : settings(s), mod(settings) {}
};

extern "C" {

uint32_t ra_curve_abi_version(void)
{
    return 2u;
}

ra_curve_t* ra_curve_create_from_config_json(const char* config_json)
{
    if (config_json == nullptr) return nullptr;

    try {
        rajson::driver_config cfg = rajson::from_string(config_json);
        if (cfg.profiles.empty()) return nullptr;

        ra::modifier_settings settings = cfg.profiles.front();

        // Curve-only preview: the BPF program runs its own EMA on the input
        // speed, so the kernel LUT (and therefore the steady-state curve the
        // chart shows) is built with the smoother halflives zeroed. Mirror
        // lut_builder::build_lut so the preview matches what is loaded.
        settings.prof.speed_processor_args.input_speed_smooth_halflife = 0;
        settings.prof.speed_processor_args.scale_smooth_halflife = 0;
        settings.prof.speed_processor_args.output_speed_smooth_halflife = 0;
        ra::init_data(settings);

        return new ra_curve_t(settings);
    }
    catch (...) {
        // from_string throws on malformed JSON or missing required keys; the
        // C ABI swallows it and reports failure as a null handle.
        return nullptr;
    }
}

void ra_curve_destroy(ra_curve_t* curve)
{
    delete curve;
}

void ra_curve_modify(const ra_curve_t* curve,
                     double x, double y,
                     double dpi_factor, double time_ms,
                     double* out_x, double* out_y)
{
    if (out_x == nullptr || out_y == nullptr) return;
    if (curve == nullptr) {
        *out_x = x;
        *out_y = y;
        return;
    }

    vec2d in{ x, y };
    // A fresh speed_processor per call keeps evaluation stateless; modify is
    // const and only mutates `in` and the local speed_processor.
    ra::speed_processor sp{};
    sp.init(curve->settings.prof.speed_processor_args);
    curve->mod.modify(in, sp, curve->settings, dpi_factor, time_ms);

    *out_x = in.x;
    *out_y = in.y;
}

} // extern "C"
