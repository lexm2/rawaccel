// Tests for the cross-OS curve shim (shim/ra_curve.cpp).
//
// The shim is what the Linux GUI preview P/Invokes: parse driver-config JSON
// into a modifier, run rawaccel::modifier::modify. Asserts ABI parity against
// a direct modify call, that profile scaling takes effect, and that bad input
// degrades to a null handle / pass-through instead of crashing.

#include "ra_curve.h"
#include "test_harness.hpp"

#include "json_io.hpp"
#include "rawaccel.hpp"

namespace ra = rawaccel;

namespace {

// Direct modifier on the same settings the shim builds: parse JSON, zero
// smoother halflives, init_data, construct. Mirrors
// ra_curve_create_from_config_json for comparison.
ra::modifier reference_modifier(const std::string& json,
                                ra::modifier_settings& out_settings)
{
    auto cfg = rajson::from_string(json);
    out_settings = cfg.profiles.front();
    out_settings.prof.speed_processor_args.input_speed_smooth_halflife = 0;
    out_settings.prof.speed_processor_args.scale_smooth_halflife = 0;
    out_settings.prof.speed_processor_args.output_speed_smooth_halflife = 0;
    ra::init_data(out_settings);
    return ra::modifier(out_settings);
}

std::string config_json_with(const ra::modifier_settings& s)
{
    rajson::driver_config cfg{};
    cfg.profiles.push_back(s);
    return rajson::to_string(cfg);
}

} // namespace

RA_TEST("Shim: abi version is 2")
{
    RA_CHECK_EQ(ra_curve_abi_version(), 2u);
}

RA_TEST("Shim: modify matches a direct modifier::modify on a classic curve")
{
    ra::modifier_settings s{};
    auto& ax = s.prof.accel_x;
    ax.mode = ra::accel_mode::classic;
    ax.gain = true;
    ax.acceleration = 0.005;
    ax.exponent_classic = 2.0;
    s.prof.accel_y = ax;

    std::string json = config_json_with(s);

    ra_curve_t* c = ra_curve_create_from_config_json(json.c_str());
    RA_CHECK(c != nullptr);
    if (c == nullptr) return;

    ra::modifier_settings ref{};
    ra::modifier mod = reference_modifier(json, ref);

    // skip v=0 (LUT builder samples the limit separately); representative speeds
    for (double v : {1.0, 5.0, 20.0, 100.0}) {
        double ox = 0, oy = 0;
        ra_curve_modify(c, v, 0.0, 1.0, 1.0, &ox, &oy);

        vec2d in{v, 0.0};
        ra::speed_processor sp{};
        sp.init(ref.prof.speed_processor_args);
        mod.modify(in, sp, ref, 1.0, 1.0);

        RA_CHECK_NEAR(ox, in.x, 1e-9);
        RA_CHECK_NEAR(oy, in.y, 1e-9);
    }

    ra_curve_destroy(c);
}

RA_TEST("Shim: output_dpi 2000 scales a noaccel profile by 2x")
{
    ra::modifier_settings s{};
    s.prof.output_dpi = 2000;  // 2x NORMALIZED_DPI (=1000)

    std::string json = config_json_with(s);
    ra_curve_t* c = ra_curve_create_from_config_json(json.c_str());
    RA_CHECK(c != nullptr);
    if (c == nullptr) return;

    double ox = 0, oy = 0;
    ra_curve_modify(c, 10.0, 0.0, 1.0, 1.0, &ox, &oy);
    RA_CHECK_NEAR(ox, 20.0, 1e-6);

    ra_curve_destroy(c);
}

RA_TEST("Shim: default profile is identity")
{
    std::string json = config_json_with(ra::modifier_settings{});
    ra_curve_t* c = ra_curve_create_from_config_json(json.c_str());
    RA_CHECK(c != nullptr);
    if (c == nullptr) return;

    double ox = 0, oy = 0;
    ra_curve_modify(c, 7.0, -3.0, 1.0, 1.0, &ox, &oy);
    RA_CHECK_NEAR(ox, 7.0, 1e-9);
    RA_CHECK_NEAR(oy, -3.0, 1e-9);

    ra_curve_destroy(c);
}

RA_TEST("Shim: malformed and empty input degrade gracefully")
{
    RA_CHECK(ra_curve_create_from_config_json(nullptr) == nullptr);
    RA_CHECK(ra_curve_create_from_config_json("not json") == nullptr);

    // valid JSON, no profiles -> null handle
    std::string empty = rajson::to_string(rajson::driver_config{});
    RA_CHECK(ra_curve_create_from_config_json(empty.c_str()) == nullptr);

    // null handle passes input through unchanged
    double ox = 1, oy = 1;
    ra_curve_modify(nullptr, 4.0, 9.0, 1.0, 1.0, &ox, &oy);
    RA_CHECK_NEAR(ox, 4.0, 0.0);
    RA_CHECK_NEAR(oy, 9.0, 0.0);

    ra_curve_destroy(nullptr);  // no-op
}
