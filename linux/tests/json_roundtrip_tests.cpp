// Roundtrip tests for linux/agent/json_io.
//
// Windows JsonProperty names are the cross-OS contract. Renaming a field
// on either side breaks one of: the schema-name assertion here, or the
// matching constant in wrapper/wrapper.cpp.

#include "test_harness.hpp"

#include "json_io.hpp"

#include <string>

namespace ra = rawaccel;

using rajson::driver_config;
using rajson::from_jobject;
using rajson::from_string;
using rajson::to_jobject;
using rajson::to_string;

namespace {

driver_config make_default()
{
    driver_config cfg{};
    // mirrors DriverConfig::FromProfile(new Profile()) on Windows
    ra::modifier_settings mod{};
    ra::init_data(mod);
    cfg.profiles.push_back(std::move(mod));
    return cfg;
}

} // namespace

RA_TEST("JSON: serialized default emits every Windows JsonProperty name")
{
    const std::string s = to_string(make_default());

    // top-level banners and fields
    RA_CHECK(s.find("### Accel modes ###") != std::string::npos);
    RA_CHECK(s.find("### Cap modes ###") != std::string::npos);
    RA_CHECK(s.find("\"version\"") != std::string::npos);
    RA_CHECK(s.find("\"defaultDeviceConfig\"") != std::string::npos);
    RA_CHECK(s.find("\"profiles\"") != std::string::npos);
    RA_CHECK(s.find("\"devices\"") != std::string::npos);

    // Profile field names
    RA_CHECK(s.find("\"Stretches domain for horizontal vs vertical inputs\"") != std::string::npos);
    RA_CHECK(s.find("\"Stretches accel range for horizontal vs vertical inputs\"") != std::string::npos);
    RA_CHECK(s.find("\"Whole or horizontal accel parameters\"") != std::string::npos);
    RA_CHECK(s.find("\"Vertical accel parameters\"") != std::string::npos);
    RA_CHECK(s.find("\"Input speed calculation parameters\"") != std::string::npos);
    RA_CHECK(s.find("\"Output DPI\"") != std::string::npos);
    RA_CHECK(s.find("\"Y/X output DPI ratio (vertical sens multiplier)\"") != std::string::npos);
    RA_CHECK(s.find("\"L/R output DPI ratio (left sens multiplier)\"") != std::string::npos);
    RA_CHECK(s.find("\"U/D output DPI ratio (up sens multiplier)\"") != std::string::npos);
    RA_CHECK(s.find("\"Degrees of rotation\"") != std::string::npos);
    RA_CHECK(s.find("\"Degrees of angle snapping\"") != std::string::npos);
    RA_CHECK(s.find("\"Input Speed Cap\"") != std::string::npos);

    // AccelArgs field names
    RA_CHECK(s.find("\"Gain / Velocity\"") != std::string::npos);
    RA_CHECK(s.find("\"Cap / Jump\"") != std::string::npos);
    RA_CHECK(s.find("\"Cap mode\"") != std::string::npos);
    RA_CHECK(s.find("\"exponentClassic\"") != std::string::npos);
    RA_CHECK(s.find("\"exponentPower\"") != std::string::npos);
    RA_CHECK(s.find("\"syncSpeed\"") != std::string::npos);

    // SpeedArgs field names
    RA_CHECK(s.find("\"Whole/combined accel (set false for 'by component' mode)\"") != std::string::npos);
    RA_CHECK(s.find("\"lpNorm\"") != std::string::npos);
    RA_CHECK(s.find("Time in ms after which an input is weighted") != std::string::npos);
    RA_CHECK(s.find("Time in ms after which scale is weighted") != std::string::npos);
    RA_CHECK(s.find("Time in ms after which an output is weighted") != std::string::npos);

    // DeviceConfig field names
    RA_CHECK(s.find("\"disable\"") != std::string::npos);
    RA_CHECK(s.find("\"Use constant time interval based on polling rate\"") != std::string::npos);
    RA_CHECK(s.find("\"DPI (normalizes input speed unit: counts/ms -> in/s)\"") != std::string::npos);
    RA_CHECK(s.find("\"Polling rate Hz (keep at 0 for automatic adjustment)\"") != std::string::npos);
}

RA_TEST("JSON: default driver_config roundtrips structurally")
{
    const driver_config a = make_default();
    const std::string s = to_string(a);
    const driver_config b = from_string(s);

    RA_CHECK_EQ(b.profiles.size(), a.profiles.size());
    RA_CHECK_EQ(b.devices.size(), a.devices.size());
    RA_CHECK(b.version == a.version);

    const auto& pa = a.profiles[0].prof;
    const auto& pb = b.profiles[0].prof;
    RA_CHECK_EQ(pb.output_dpi, pa.output_dpi);
    RA_CHECK_EQ(pb.yx_output_dpi_ratio, pa.yx_output_dpi_ratio);
    RA_CHECK_EQ(pb.degrees_rotation, pa.degrees_rotation);
    RA_CHECK_EQ(pb.degrees_snap, pa.degrees_snap);
    RA_CHECK_EQ(pb.speed_max, pa.speed_max);
    RA_CHECK_EQ((int)pb.accel_x.mode, (int)pa.accel_x.mode);
    RA_CHECK_EQ(pb.accel_x.gain, pa.accel_x.gain);
    RA_CHECK_EQ((int)pb.accel_x.cap_mode, (int)pa.accel_x.cap_mode);
}

RA_TEST("JSON: non-default profile values roundtrip exactly")
{
    driver_config a = make_default();
    auto& p = a.profiles[0].prof;

    // non-default values across the surface
    p.output_dpi          = 1600;
    p.yx_output_dpi_ratio = 0.8;
    p.lr_output_dpi_ratio = 1.1;
    p.ud_output_dpi_ratio = 0.95;
    p.degrees_rotation    = 12.5;
    p.degrees_snap        = 3.0;
    p.speed_max           = 250.0;
    p.domain_weights      = {1.25, 0.75};
    p.range_weights       = {1.5,  0.5};

    p.accel_x.mode             = ra::accel_mode::synchronous;
    p.accel_x.gain             = false;
    p.accel_x.sync_speed       = 20.0;
    p.accel_x.gamma            = 0.5;
    p.accel_x.motivity         = 1.3;
    p.accel_x.smooth           = 0.5;
    p.accel_x.cap              = {12.0, 1.4};
    p.accel_x.cap_mode         = ra::cap_mode::in;

    p.accel_y.mode             = ra::accel_mode::classic;
    p.accel_y.cap_mode         = ra::cap_mode::out;

    p.speed_processor_args.whole = false;
    p.speed_processor_args.lp_norm = 3.0;
    p.speed_processor_args.input_speed_smooth_halflife = 25.0;

    const std::string s = to_string(a);
    const driver_config b = from_string(s);
    const auto& q = b.profiles[0].prof;

    RA_CHECK_EQ(q.output_dpi, p.output_dpi);
    RA_CHECK_EQ(q.yx_output_dpi_ratio, p.yx_output_dpi_ratio);
    RA_CHECK_EQ(q.lr_output_dpi_ratio, p.lr_output_dpi_ratio);
    RA_CHECK_EQ(q.ud_output_dpi_ratio, p.ud_output_dpi_ratio);
    RA_CHECK_EQ(q.degrees_rotation, p.degrees_rotation);
    RA_CHECK_EQ(q.degrees_snap, p.degrees_snap);
    RA_CHECK_EQ(q.speed_max, p.speed_max);
    RA_CHECK_EQ(q.domain_weights.x, p.domain_weights.x);
    RA_CHECK_EQ(q.domain_weights.y, p.domain_weights.y);
    RA_CHECK_EQ(q.range_weights.x, p.range_weights.x);
    RA_CHECK_EQ(q.range_weights.y, p.range_weights.y);

    RA_CHECK_EQ((int)q.accel_x.mode, (int)p.accel_x.mode);
    RA_CHECK_EQ(q.accel_x.gain, p.accel_x.gain);
    RA_CHECK_EQ(q.accel_x.sync_speed, p.accel_x.sync_speed);
    RA_CHECK_EQ(q.accel_x.gamma, p.accel_x.gamma);
    RA_CHECK_EQ(q.accel_x.motivity, p.accel_x.motivity);
    RA_CHECK_EQ(q.accel_x.smooth, p.accel_x.smooth);
    RA_CHECK_EQ(q.accel_x.cap.x, p.accel_x.cap.x);
    RA_CHECK_EQ(q.accel_x.cap.y, p.accel_x.cap.y);
    RA_CHECK_EQ((int)q.accel_x.cap_mode, (int)p.accel_x.cap_mode);

    RA_CHECK_EQ((int)q.accel_y.mode, (int)p.accel_y.mode);
    RA_CHECK_EQ((int)q.accel_y.cap_mode, (int)p.accel_y.cap_mode);

    RA_CHECK_EQ(q.speed_processor_args.whole, p.speed_processor_args.whole);
    RA_CHECK_EQ(q.speed_processor_args.lp_norm, p.speed_processor_args.lp_norm);
    RA_CHECK_EQ(q.speed_processor_args.input_speed_smooth_halflife,
                p.speed_processor_args.input_speed_smooth_halflife);
}

RA_TEST("JSON: LUT data array round-trips with implicit length")
{
    driver_config a = make_default();
    auto& ax = a.profiles[0].prof.accel_x;
    ax.mode = ra::accel_mode::lookup;
    ax.length = 5;
    ax.data[0] = 1.0f; ax.data[1] = 2.5f; ax.data[2] = 3.0f;
    ax.data[3] = 4.25f; ax.data[4] = 5.75f;

    const std::string s = to_string(a);
    const driver_config b = from_string(s);
    const auto& bx = b.profiles[0].prof.accel_x;

    RA_CHECK_EQ(bx.length, 5);
    RA_CHECK_EQ(bx.data[0], 1.0f);
    RA_CHECK_EQ(bx.data[1], 2.5f);
    RA_CHECK_EQ(bx.data[2], 3.0f);
    RA_CHECK_EQ(bx.data[3], 4.25f);
    RA_CHECK_EQ(bx.data[4], 5.75f);
    // trailing entries padded to zero
    RA_CHECK_EQ(bx.data[5], 0.0f);
}

RA_TEST("JSON: non-LUT profile emits empty data array")
{
    const std::string s = to_string(make_default());
    // accel_x defaults to noaccel -> empty data array
    RA_CHECK(s.find("\"data\": []") != std::string::npos
          || s.find("\"data\":[]") != std::string::npos);
}

RA_TEST("JSON: AccelMode string mapping (lookup <-> lut)")
{
    RA_CHECK(std::string(rajson::accel_mode_to_string(ra::accel_mode::lookup)) == "lut");
    RA_CHECK((int)rajson::accel_mode_from_string("lut") == (int)ra::accel_mode::lookup);
}

RA_TEST("JSON: CapMode string mapping (io <-> in_out)")
{
    RA_CHECK(std::string(rajson::cap_mode_to_string(ra::cap_mode::io)) == "in_out");
    RA_CHECK((int)rajson::cap_mode_from_string("in_out") == (int)ra::cap_mode::io);
    RA_CHECK(std::string(rajson::cap_mode_to_string(ra::cap_mode::in)) == "input");
    RA_CHECK(std::string(rajson::cap_mode_to_string(ra::cap_mode::out)) == "output");
}

RA_TEST("JSON: profile name roundtrips through UTF-8")
{
    driver_config a = make_default();
    auto& p = a.profiles[0].prof;
    const std::string ascii = "MyProfile";
    rajson::utf8_to_wchar(ascii, p.name, ra::MAX_NAME_LEN);
    RA_CHECK(rajson::wchar_to_utf8(p.name, ra::MAX_NAME_LEN) == ascii);

    const std::string s = to_string(a);
    const driver_config b = from_string(s);
    RA_CHECK(rajson::wchar_to_utf8(b.profiles[0].prof.name, ra::MAX_NAME_LEN) == ascii);
}

RA_TEST("JSON: device_settings roundtrip")
{
    driver_config a = make_default();
    ra::device_settings dev{};
    rajson::utf8_to_wchar("Logitech G Pro", dev.name, ra::MAX_NAME_LEN);
    rajson::utf8_to_wchar("MyProfile", dev.profile, ra::MAX_NAME_LEN);
    rajson::utf8_to_wchar("USB\\VID_046D&PID_C088", dev.id, ra::MAX_DEV_ID_LEN);
    dev.config.dpi = 1600;
    dev.config.polling_rate = 1000;
    dev.config.disable = false;
    dev.config.set_extra_info = true;
    dev.config.clamp.min = 0.5;
    dev.config.clamp.max = 50;
    a.devices.push_back(dev);

    const std::string s = to_string(a);
    const driver_config b = from_string(s);

    RA_CHECK_EQ(b.devices.size(), std::size_t(1));
    const auto& d = b.devices[0];
    RA_CHECK(rajson::wchar_to_utf8(d.name, ra::MAX_NAME_LEN) == "Logitech G Pro");
    RA_CHECK(rajson::wchar_to_utf8(d.profile, ra::MAX_NAME_LEN) == "MyProfile");
    RA_CHECK(rajson::wchar_to_utf8(d.id, ra::MAX_DEV_ID_LEN) == "USB\\VID_046D&PID_C088");
    RA_CHECK_EQ(d.config.dpi, 1600);
    RA_CHECK_EQ(d.config.polling_rate, 1000);
    RA_CHECK_EQ(d.config.set_extra_info, true);
    RA_CHECK_EQ(d.config.clamp.min, 0.5);
    RA_CHECK_EQ(d.config.clamp.max, 50.0);
}

RA_TEST("JSON: minimumTime / maximumTime omitted when default, present when not")
{
    driver_config a = make_default();
    const std::string s_default = to_string(a);
    RA_CHECK(s_default.find("\"minimumTime\"") == std::string::npos);
    RA_CHECK(s_default.find("\"maximumTime\"") == std::string::npos);

    a.default_device_config.clamp.min = 0.5;
    a.default_device_config.clamp.max = 50;
    const std::string s_set = to_string(a);
    RA_CHECK(s_set.find("\"minimumTime\"") != std::string::npos);
    RA_CHECK(s_set.find("\"maximumTime\"") != std::string::npos);
}
