#define RA_SHIM_BUILD 1
#include "ra_curve.h"

#include <rawaccel.hpp>
#include <json_io.hpp>

#include <new>

namespace ra = rawaccel;

// Parsed settings + a modifier built from them. The modifier's ctor captures
// output_dpi / NORMALIZED_DPI, so it must come from the same (halflife-zeroed,
// init_data'd) settings the handle stores. settings precedes mod so the ctor
// init list can hand it over.
struct ra_curve {
    ra::modifier_settings settings;
    ra::modifier mod;

    explicit ra_curve(const ra::modifier_settings& s)
        : settings(s), mod(settings) {}
};

extern "C" {

uint32_t ra_curve_abi_version(void)
{
    return 3u;
}

ra_curve_t* ra_curve_create_from_config_json(const char* config_json)
{
    if (config_json == nullptr) return nullptr;

    try {
        // Input is a single profile (modifier_settings) JSON object.
        ra::modifier_settings settings =
            rajson::modifier_settings_from_jobject(nlohmann::json::parse(config_json));

        // Curve-only preview: the kernel runs its own input-speed EMA, so the
        // LUT (the steady-state curve charted) zeroes the smoother halflives.
        // Mirrors lut_builder::build_lut so the preview matches what loads.
        settings.prof.speed_processor_args.input_speed_smooth_halflife = 0;
        settings.prof.speed_processor_args.scale_smooth_halflife = 0;
        settings.prof.speed_processor_args.output_speed_smooth_halflife = 0;
        ra::init_data(settings);

        return new ra_curve_t(settings);
    }
    catch (...) {
        // parse throws on bad JSON / missing keys; report as null handle
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
    // fresh speed_processor per call -> stateless; modify only mutates `in`
    // and this local
    ra::speed_processor sp{};
    sp.init(curve->settings.prof.speed_processor_args);
    curve->mod.modify(in, sp, curve->settings, dpi_factor, time_ms);

    *out_x = in.x;
    *out_y = in.y;
}

} // extern "C"
