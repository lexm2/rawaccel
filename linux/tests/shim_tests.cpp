// Tests for the cross-OS curve shim (shim/ra_curve.cpp).
//
// The shim is what the Linux GUI preview P/Invokes: parse a single profile
// (modifier_settings) JSON into a modifier, run rawaccel::modifier::modify.
// Asserts ABI parity against a direct modify call, that profile scaling takes
// effect, and that bad input degrades to a null handle / pass-through.

#include "ra_curve.h"
#include "test_harness.hpp"

#include "json_io.hpp"
#include "rawaccel.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace ra = rawaccel;
using nlohmann::json;

namespace {

#ifndef RA_FIXTURE_PATH
#error "RA_FIXTURE_PATH must be defined (path to default_config.json)"
#endif

// The shim input is a single profile object: the frozen fixture's profiles[0].
json base_profile()
{
    std::ifstream f(RA_FIXTURE_PATH);
    std::stringstream ss;
    ss << f.rdbuf();
    return json::parse(ss.str()).at("profiles").at(0);
}

// Direct modifier on the same JSON the shim parses: zero smoother halflives,
// init_data, construct. Mirrors ra_curve_create_from_config_json for comparison.
ra::modifier reference_modifier(const std::string& profile_json,
                                ra::modifier_settings& out_settings)
{
    out_settings = rajson::modifier_settings_from_jobject(json::parse(profile_json));
    out_settings.prof.speed_processor_args.input_speed_smooth_halflife = 0;
    out_settings.prof.speed_processor_args.scale_smooth_halflife = 0;
    out_settings.prof.speed_processor_args.output_speed_smooth_halflife = 0;
    ra::init_data(out_settings);
    return ra::modifier(out_settings);
}

} // namespace

RA_TEST("Shim: abi version is 3")
{
    RA_CHECK_EQ(ra_curve_abi_version(), 3u);
}

RA_TEST("Shim: modify matches a direct modifier::modify on a classic curve")
{
    json p = base_profile();
    json args = p.at(rajson::key::ARGS_X);
    args[rajson::key::MODE] = "classic";
    args[rajson::key::GAIN] = true;
    args[rajson::key::ACCELERATION] = 0.005;
    args[rajson::key::EXPONENT_CLASSIC] = 2.0;
    p[rajson::key::ARGS_X] = args;
    p[rajson::key::ARGS_Y] = args;
    const std::string json_str = p.dump();

    ra_curve_t* c = ra_curve_create_from_config_json(json_str.c_str());
    RA_CHECK(c != nullptr);
    if (c == nullptr) return;

    ra::modifier_settings ref{};
    ra::modifier mod = reference_modifier(json_str, ref);

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
    json p = base_profile();
    p[rajson::key::OUTPUT_DPI] = 2000.0;  // 2x NORMALIZED_DPI (=1000)
    const std::string json_str = p.dump();

    ra_curve_t* c = ra_curve_create_from_config_json(json_str.c_str());
    RA_CHECK(c != nullptr);
    if (c == nullptr) return;

    double ox = 0, oy = 0;
    ra_curve_modify(c, 10.0, 0.0, 1.0, 1.0, &ox, &oy);
    RA_CHECK_NEAR(ox, 20.0, 1e-6);

    ra_curve_destroy(c);
}

RA_TEST("Shim: default profile is identity")
{
    const std::string json_str = base_profile().dump();
    ra_curve_t* c = ra_curve_create_from_config_json(json_str.c_str());
    RA_CHECK(c != nullptr);
    if (c == nullptr) return;

    double ox = 0, oy = 0;
    ra_curve_modify(c, 7.0, -3.0, 1.0, 1.0, &ox, &oy);
    RA_CHECK_NEAR(ox, 7.0, 1e-9);
    RA_CHECK_NEAR(oy, -3.0, 1e-9);

    ra_curve_destroy(c);
}

RA_TEST("Shim: malformed and incomplete input degrade gracefully")
{
    RA_CHECK(ra_curve_create_from_config_json(nullptr) == nullptr);
    RA_CHECK(ra_curve_create_from_config_json("not json") == nullptr);

    // valid JSON missing required profile keys -> .at() throws -> null handle
    RA_CHECK(ra_curve_create_from_config_json("{}") == nullptr);

    // null handle passes input through unchanged
    double ox = 1, oy = 1;
    ra_curve_modify(nullptr, 4.0, 9.0, 1.0, 1.0, &ox, &oy);
    RA_CHECK_NEAR(ox, 4.0, 0.0);
    RA_CHECK_NEAR(oy, 9.0, 0.0);

    ra_curve_destroy(nullptr);  // no-op
}
