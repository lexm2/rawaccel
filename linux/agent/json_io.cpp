#include "json_io.hpp"

#include <cstdint>
#include <stdexcept>

namespace rajson {

using nlohmann::json;

std::string wchar_to_utf8(const wchar_t* s, std::size_t cap)
{
    std::string out;
    out.reserve(cap);
    for (std::size_t i = 0; i < cap && s[i] != 0; ++i) {
        std::uint32_t cp = static_cast<std::uint32_t>(s[i]);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0x10FFFF) {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        // Above U+10FFFF is not valid Unicode; skip rather than emit garbage.
    }
    return out;
}

void utf8_to_wchar(const std::string& s, wchar_t* out, std::size_t cap)
{
    if (cap == 0) return;
    std::size_t w = 0;
    std::size_t i = 0;
    while (i < s.size() && w + 1 < cap) {
        std::uint8_t b = static_cast<std::uint8_t>(s[i]);
        std::uint32_t cp = 0;
        if (b < 0x80) {
            cp = b;
            i += 1;
        }
        else if ((b & 0xE0) == 0xC0 && i + 1 < s.size()) {
            cp = (static_cast<std::uint32_t>(b & 0x1F) << 6)
               |  (static_cast<std::uint32_t>(s[i + 1]) & 0x3F);
            i += 2;
        }
        else if ((b & 0xF0) == 0xE0 && i + 2 < s.size()) {
            cp = (static_cast<std::uint32_t>(b & 0x0F) << 12)
               | ((static_cast<std::uint32_t>(s[i + 1]) & 0x3F) << 6)
               |  (static_cast<std::uint32_t>(s[i + 2]) & 0x3F);
            i += 3;
        }
        else if ((b & 0xF8) == 0xF0 && i + 3 < s.size()) {
            cp = (static_cast<std::uint32_t>(b & 0x07) << 18)
               | ((static_cast<std::uint32_t>(s[i + 1]) & 0x3F) << 12)
               | ((static_cast<std::uint32_t>(s[i + 2]) & 0x3F) << 6)
               |  (static_cast<std::uint32_t>(s[i + 3]) & 0x3F);
            i += 4;
        }
        else {
            ++i; // skip invalid byte
            continue;
        }
        out[w++] = static_cast<wchar_t>(cp);
    }
    out[w] = 0;
}

const char* accel_mode_to_string(ra::accel_mode m)
{
    // C# names "lookup" as "lut"
    switch (m) {
        case ra::accel_mode::classic:     return "classic";
        case ra::accel_mode::jump:        return "jump";
        case ra::accel_mode::natural:     return "natural";
        case ra::accel_mode::synchronous: return "synchronous";
        case ra::accel_mode::power:       return "power";
        case ra::accel_mode::lookup:      return "lut";
        case ra::accel_mode::noaccel:     return "noaccel";
    }
    return "noaccel";
}

ra::accel_mode accel_mode_from_string(const std::string& s)
{
    if (s == "classic")     return ra::accel_mode::classic;
    if (s == "jump")        return ra::accel_mode::jump;
    if (s == "natural")     return ra::accel_mode::natural;
    if (s == "synchronous") return ra::accel_mode::synchronous;
    if (s == "power")       return ra::accel_mode::power;
    if (s == "lut")         return ra::accel_mode::lookup;
    if (s == "noaccel")     return ra::accel_mode::noaccel;
    throw std::runtime_error("unknown accel mode: " + s);
}

const char* cap_mode_to_string(ra::cap_mode m)
{
    switch (m) {
        case ra::cap_mode::io:  return "in_out";
        case ra::cap_mode::in:  return "input";
        case ra::cap_mode::out: return "output";
    }
    return "in_out";
}

ra::cap_mode cap_mode_from_string(const std::string& s)
{
    if (s == "in_out") return ra::cap_mode::io;
    if (s == "input")  return ra::cap_mode::in;
    if (s == "output") return ra::cap_mode::out;
    throw std::runtime_error("unknown cap mode: " + s);
}

namespace {

json vec2_to(const vec2d& v)
{
    return json{{key::X, v.x}, {key::Y, v.y}};
}

vec2d vec2_from(const json& j)
{
    return {j.at(key::X).get<double>(), j.at(key::Y).get<double>()};
}

json accel_args_to(const ra::accel_args& a)
{
    json j;
    j[key::MODE]              = accel_mode_to_string(a.mode);
    j[key::GAIN]              = a.gain;
    j[key::INPUT_OFFSET]      = a.input_offset;
    j[key::OUTPUT_OFFSET]     = a.output_offset;
    j[key::ACCELERATION]      = a.acceleration;
    j[key::DECAY_RATE]        = a.decay_rate;
    j[key::GAMMA]             = a.gamma;
    j[key::MOTIVITY]          = a.motivity;
    j[key::EXPONENT_CLASSIC]  = a.exponent_classic;
    j[key::SCALE]             = a.scale;
    j[key::EXPONENT_POWER]    = a.exponent_power;
    j[key::LIMIT]             = a.limit;
    j[key::SYNC_SPEED]        = a.sync_speed;
    j[key::SMOOTH]            = a.smooth;
    j[key::CAP]               = vec2_to(a.cap);
    j[key::CAP_MODE]          = cap_mode_to_string(a.cap_mode);

    // LUT data only for lookup mode, first `length` entries (clamped like accel_args_from)
    json data_arr = json::array();
    if (a.mode == ra::accel_mode::lookup) {
        int n = a.length < 0 ? 0
              : (a.length > static_cast<int>(ra::LUT_RAW_DATA_CAPACITY)
                     ? static_cast<int>(ra::LUT_RAW_DATA_CAPACITY) : a.length);
        for (int i = 0; i < n; ++i) {
            data_arr.push_back(a.data[i]);
        }
    }
    j[key::DATA] = std::move(data_arr);
    return j;
}

void accel_args_from(const json& j, ra::accel_args& out)
{
    out.mode             = accel_mode_from_string(j.at(key::MODE).get<std::string>());
    out.gain             = j.at(key::GAIN).get<bool>();
    out.input_offset     = j.at(key::INPUT_OFFSET).get<double>();
    out.output_offset    = j.at(key::OUTPUT_OFFSET).get<double>();
    out.acceleration     = j.at(key::ACCELERATION).get<double>();
    out.decay_rate       = j.at(key::DECAY_RATE).get<double>();
    out.gamma            = j.at(key::GAMMA).get<double>();
    out.motivity         = j.at(key::MOTIVITY).get<double>();
    out.exponent_classic = j.at(key::EXPONENT_CLASSIC).get<double>();
    out.scale            = j.at(key::SCALE).get<double>();
    out.exponent_power   = j.at(key::EXPONENT_POWER).get<double>();
    out.limit            = j.at(key::LIMIT).get<double>();
    out.sync_speed       = j.at(key::SYNC_SPEED).get<double>();
    out.smooth           = j.at(key::SMOOTH).get<double>();
    out.cap              = vec2_from(j.at(key::CAP));
    out.cap_mode         = cap_mode_from_string(j.at(key::CAP_MODE).get<std::string>());

    // array size sets `length`; zero-pad tail for constant binary layout
    const auto& data_arr = j.at(key::DATA);
    const int n = static_cast<int>(data_arr.size());
    if (n > static_cast<int>(ra::LUT_RAW_DATA_CAPACITY)) {
        throw std::runtime_error("LUT data exceeds capacity");
    }
    out.length = n;
    int i = 0;
    for (; i < n; ++i) out.data[i] = data_arr[i].get<float>();
    for (; i < static_cast<int>(ra::LUT_RAW_DATA_CAPACITY); ++i) out.data[i] = 0.0f;
}

json speed_args_to(const ra::speed_args& s)
{
    json j;
    j[key::COMBINE_MAGNITUDES]      = s.whole;
    j[key::LP_NORM]                 = s.lp_norm;
    j[key::INPUT_SMOOTH_HALFLIFE]   = s.input_speed_smooth_halflife;
    j[key::SCALE_SMOOTH_HALFLIFE]   = s.scale_smooth_halflife;
    j[key::OUTPUT_SMOOTH_HALFLIFE]  = s.output_speed_smooth_halflife;
    return j;
}

void speed_args_from(const json& j, ra::speed_args& out)
{
    out.whole                        = j.at(key::COMBINE_MAGNITUDES).get<bool>();
    out.lp_norm                      = j.at(key::LP_NORM).get<double>();
    out.input_speed_smooth_halflife  = j.at(key::INPUT_SMOOTH_HALFLIFE).get<double>();
    out.scale_smooth_halflife        = j.at(key::SCALE_SMOOTH_HALFLIFE).get<double>();
    out.output_speed_smooth_halflife = j.at(key::OUTPUT_SMOOTH_HALFLIFE).get<double>();
}

json profile_to(const ra::profile& p)
{
    json j;
    j[key::NAME]            = wchar_to_utf8(p.name, ra::MAX_NAME_LEN);
    j[key::DOMAIN_XY]       = vec2_to(p.domain_weights);
    j[key::RANGE_XY]        = vec2_to(p.range_weights);
    j[key::ARGS_X]          = accel_args_to(p.accel_x);
    j[key::ARGS_Y]          = accel_args_to(p.accel_y);
    j[key::INPUT_SPEED_ARGS] = speed_args_to(p.speed_processor_args);
    j[key::OUTPUT_DPI]      = p.output_dpi;
    j[key::YX_RATIO]        = p.yx_output_dpi_ratio;
    j[key::LR_RATIO]        = p.lr_output_dpi_ratio;
    j[key::UD_RATIO]        = p.ud_output_dpi_ratio;
    j[key::ROTATION]        = p.degrees_rotation;
    j[key::SNAP]            = p.degrees_snap;
    j[key::MAXIMUM_SPEED]   = p.speed_max;
    return j;
}

void profile_from(const json& j, ra::profile& out)
{
    utf8_to_wchar(j.at(key::NAME).get<std::string>(), out.name, ra::MAX_NAME_LEN);
    out.domain_weights = vec2_from(j.at(key::DOMAIN_XY));
    out.range_weights  = vec2_from(j.at(key::RANGE_XY));
    accel_args_from(j.at(key::ARGS_X), out.accel_x);
    accel_args_from(j.at(key::ARGS_Y), out.accel_y);
    speed_args_from(j.at(key::INPUT_SPEED_ARGS), out.speed_processor_args);
    out.output_dpi          = j.at(key::OUTPUT_DPI).get<double>();
    out.yx_output_dpi_ratio = j.at(key::YX_RATIO).get<double>();
    out.lr_output_dpi_ratio = j.at(key::LR_RATIO).get<double>();
    out.ud_output_dpi_ratio = j.at(key::UD_RATIO).get<double>();
    out.degrees_rotation    = j.at(key::ROTATION).get<double>();
    out.degrees_snap        = j.at(key::SNAP).get<double>();
    out.speed_min           = 0; // JsonIgnore on Windows side
    out.speed_max           = j.at(key::MAXIMUM_SPEED).get<double>();
}

json device_config_to(const ra::device_config& c)
{
    json j; // optional fields only when non-default (matches C# ShouldSerialize)
    j[key::DISABLE]        = c.disable;
    if (c.set_extra_info) j[key::SET_EXTRA_INFO] = c.set_extra_info;
    j[key::POLL_TIME_LOCK] = c.poll_time_lock;
    j[key::DPI]            = c.dpi;
    j[key::POLLING_RATE]   = c.polling_rate;
    if (c.clamp.min != ra::DEFAULT_TIME_MIN) j[key::MINIMUM_TIME] = c.clamp.min;
    if (c.clamp.max != ra::DEFAULT_TIME_MAX) j[key::MAXIMUM_TIME] = c.clamp.max;
    return j;
}

void device_config_from(const json& j, ra::device_config& out)
{
    out.disable        = j.at(key::DISABLE).get<bool>();
    out.set_extra_info = j.value(key::SET_EXTRA_INFO, false);
    out.poll_time_lock = j.at(key::POLL_TIME_LOCK).get<bool>();
    out.dpi            = j.at(key::DPI).get<int>();
    out.polling_rate   = j.at(key::POLLING_RATE).get<int>();
    out.clamp.min      = j.value(key::MINIMUM_TIME, static_cast<double>(ra::DEFAULT_TIME_MIN));
    out.clamp.max      = j.value(key::MAXIMUM_TIME, static_cast<double>(ra::DEFAULT_TIME_MAX));
}

json device_settings_to(const ra::device_settings& d)
{
    json j;
    j[key::DEVICE_NAME]    = wchar_to_utf8(d.name, ra::MAX_NAME_LEN);
    j[key::DEVICE_PROFILE] = wchar_to_utf8(d.profile, ra::MAX_NAME_LEN);
    j[key::DEVICE_ID]      = wchar_to_utf8(d.id, ra::MAX_DEV_ID_LEN);
    j[key::DEVICE_CONFIG]  = device_config_to(d.config);
    return j;
}

void device_settings_from(const json& j, ra::device_settings& out)
{
    utf8_to_wchar(j.at(key::DEVICE_NAME).get<std::string>(),    out.name,    ra::MAX_NAME_LEN);
    utf8_to_wchar(j.at(key::DEVICE_PROFILE).get<std::string>(), out.profile, ra::MAX_NAME_LEN);
    utf8_to_wchar(j.at(key::DEVICE_ID).get<std::string>(),      out.id,      ra::MAX_DEV_ID_LEN);
    device_config_from(j.at(key::DEVICE_CONFIG), out.config);
}

} // anonymous

json modifier_settings_to_jobject(const ra::modifier_settings& m)
{
    return profile_to(m.prof);
}

ra::modifier_settings modifier_settings_from_jobject(const json& j)
{
    ra::modifier_settings mod{};
    profile_from(j, mod.prof);
    ra::init_data(mod);
    return mod;
}

json device_config_to_jobject(const ra::device_config& c)
{
    return device_config_to(c);
}

ra::device_config device_config_from_jobject(const json& j)
{
    ra::device_config c{};
    device_config_from(j, c);
    return c;
}

json to_jobject(const driver_config& cfg)
{
    json j; // banners first to match Windows AddFirst order
    j[key::ACCEL_MODES_BANNER] = ACCEL_MODES_JOINED;
    j[key::CAP_MODES_BANNER]   = CAP_MODES_JOINED;
    j[key::VERSION]            = cfg.version;
    j[key::DEFAULT_DEVICE_CONFIG] = device_config_to(cfg.default_device_config);

    json profiles_arr = json::array();
    for (const auto& mod : cfg.profiles) {
        profiles_arr.push_back(profile_to(mod.prof));
    }
    j[key::PROFILES] = std::move(profiles_arr);

    json devices_arr = json::array();
    for (const auto& dev : cfg.devices) {
        devices_arr.push_back(device_settings_to(dev));
    }
    j[key::DEVICES] = std::move(devices_arr);

    return j;
}

driver_config from_jobject(const json& j)
{
    driver_config cfg{};
    cfg.version = j.value(key::VERSION, std::string(RA_VER_STRING));
    device_config_from(j.at(key::DEFAULT_DEVICE_CONFIG), cfg.default_device_config);

    for (const auto& pj : j.at(key::PROFILES)) {
        ra::modifier_settings mod{};
        profile_from(pj, mod.prof);
        ra::init_data(mod);
        cfg.profiles.push_back(std::move(mod));
    }

    for (const auto& dj : j.at(key::DEVICES)) {
        ra::device_settings dev{};
        device_settings_from(dj, dev);
        cfg.devices.push_back(std::move(dev));
    }

    return cfg;
}

std::string to_string(const driver_config& cfg, int indent)
{
    return to_jobject(cfg).dump(indent);
}

driver_config from_string(const std::string& s)
{
    return from_jobject(json::parse(s));
}

} // namespace rajson
