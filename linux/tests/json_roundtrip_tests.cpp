// Parser tests for linux/agent/json_io (the live *_from_jobject parsers).
//
// The C++ side no longer serializes; the cross-OS contract is the frozen fixture
// tests/fixtures/default_config.json (byte-stability guarded by the Rust
// config::reserialize_is_byte_stable test). These tests assert the C++ parsers
// consume that exact shape, so a key rename on either side fails loudly here.

#include "test_harness.hpp"

#include "json_io.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace ra = rawaccel;
using nlohmann::json;

namespace {

#ifndef RA_FIXTURE_PATH
#error "RA_FIXTURE_PATH must be defined (path to default_config.json)"
#endif

json load_fixture()
{
    std::ifstream f(RA_FIXTURE_PATH);
    std::stringstream ss;
    ss << f.rdbuf();
    return json::parse(ss.str());
}

} // namespace

RA_TEST("JSON: frozen fixture parses into a profile via modifier_settings_from_jobject")
{
    const json fixture = load_fixture();
    const ra::modifier_settings mod =
        rajson::modifier_settings_from_jobject(fixture.at("profiles").at(0));
    const auto& p = mod.prof;

    RA_CHECK_EQ(p.output_dpi, 1000.0);
    RA_CHECK_EQ(p.yx_output_dpi_ratio, 1.0);
    RA_CHECK_EQ((int)p.accel_x.mode, (int)ra::accel_mode::noaccel);
    RA_CHECK_EQ(p.accel_x.gain, true);
    RA_CHECK_EQ((int)p.accel_x.cap_mode, (int)ra::cap_mode::out);
    RA_CHECK_EQ(p.accel_x.cap.x, 15.0);
    RA_CHECK_EQ(p.accel_x.cap.y, 1.5);
    RA_CHECK_EQ(p.speed_processor_args.lp_norm, 2.0);
    RA_CHECK_EQ(p.speed_processor_args.whole, true);
}

RA_TEST("JSON: frozen fixture default device config parses; absent times take defaults")
{
    const json fixture = load_fixture();
    const ra::device_config c =
        rajson::device_config_from_jobject(fixture.at("defaultDeviceConfig"));

    RA_CHECK_EQ(c.dpi, 0);
    RA_CHECK_EQ(c.polling_rate, 0);
    RA_CHECK_EQ(c.disable, false);
    RA_CHECK_EQ(c.poll_time_lock, false);
    // minimumTime / maximumTime absent -> contract defaults
    RA_CHECK_EQ(c.clamp.min, static_cast<double>(ra::DEFAULT_TIME_MIN));
    RA_CHECK_EQ(c.clamp.max, static_cast<double>(ra::DEFAULT_TIME_MAX));
}

RA_TEST("JSON: LUT data array parses with implicit length and zero-pad")
{
    json profile = load_fixture().at("profiles").at(0);
    profile.at("Whole or horizontal accel parameters")["mode"] = "lut";
    profile.at("Whole or horizontal accel parameters")["data"] =
        json::array({1.0, 2.5, 3.0, 4.25, 5.75});

    const ra::modifier_settings mod = rajson::modifier_settings_from_jobject(profile);
    const auto& ax = mod.prof.accel_x;

    RA_CHECK_EQ((int)ax.mode, (int)ra::accel_mode::lookup);
    RA_CHECK_EQ(ax.length, 5);
    RA_CHECK_EQ(ax.data[0], 1.0f);
    RA_CHECK_EQ(ax.data[1], 2.5f);
    RA_CHECK_EQ(ax.data[2], 3.0f);
    RA_CHECK_EQ(ax.data[3], 4.25f);
    RA_CHECK_EQ(ax.data[4], 5.75f);
    RA_CHECK_EQ(ax.data[5], 0.0f); // trailing entries padded to zero
}

RA_TEST("JSON: AccelMode string mapping (lut -> lookup)")
{
    RA_CHECK((int)rajson::accel_mode_from_string("lut") == (int)ra::accel_mode::lookup);
    RA_CHECK((int)rajson::accel_mode_from_string("noaccel") == (int)ra::accel_mode::noaccel);
}

RA_TEST("JSON: CapMode string mapping (in_out/input/output)")
{
    RA_CHECK((int)rajson::cap_mode_from_string("in_out") == (int)ra::cap_mode::io);
    RA_CHECK((int)rajson::cap_mode_from_string("input")  == (int)ra::cap_mode::in);
    RA_CHECK((int)rajson::cap_mode_from_string("output") == (int)ra::cap_mode::out);
}

RA_TEST("JSON: missing required key is rejected (fail-loud .at())")
{
    bool threw = false;
    try {
        rajson::modifier_settings_from_jobject(json::object());
    } catch (...) {
        threw = true;
    }
    RA_CHECK(threw);
}
