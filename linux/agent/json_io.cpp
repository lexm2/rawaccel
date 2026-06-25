#include "json_io.hpp"

#include <cstdint>
#include <stdexcept>

namespace rajson {

using nlohmann::json;

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

ra::cap_mode cap_mode_from_string(const std::string& s)
{
    if (s == "in_out") return ra::cap_mode::io;
    if (s == "input")  return ra::cap_mode::in;
    if (s == "output") return ra::cap_mode::out;
    throw std::runtime_error("unknown cap mode: " + s);
}

namespace {

vec2d vec2_from(const json& j)
{
    return {j.at(key::X).get<double>(), j.at(key::Y).get<double>()};
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

    // array size sets `length`
    // zero-pad tail for constant binary layout
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

void speed_args_from(const json& j, ra::speed_args& out)
{
    out.whole                        = j.at(key::COMBINE_MAGNITUDES).get<bool>();
    out.lp_norm                      = j.at(key::LP_NORM).get<double>();
    out.input_speed_smooth_halflife  = j.at(key::INPUT_SMOOTH_HALFLIFE).get<double>();
    out.scale_smooth_halflife        = j.at(key::SCALE_SMOOTH_HALFLIFE).get<double>();
    out.output_speed_smooth_halflife = j.at(key::OUTPUT_SMOOTH_HALFLIFE).get<double>();
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

} // anonymous

// Per-type parsers for the backend's resolved-config FFI and the curve shim. `j`
// is a single profile / device_config object. modifier_settings_from runs init_data.
ra::modifier_settings modifier_settings_from_jobject(const json& j)
{
    ra::modifier_settings mod{};
    profile_from(j, mod.prof);
    ra::init_data(mod);
    return mod;
}

ra::device_config device_config_from_jobject(const json& j)
{
    ra::device_config c{};
    device_config_from(j, c);
    return c;
}

} // namespace rajson
