#pragma once

// Native C++ port of wrapper/wrapper.cpp's JSON layer. Key names are the cross-OS
// settings.json contract: do NOT rename a key without changing the Windows JsonProperty.

#include "rawaccel.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace rajson {

namespace ra = rawaccel;

namespace key {

// AccelArgs (mode-specific knobs).
inline constexpr const char* MODE              = "mode";
inline constexpr const char* GAIN              = "Gain / Velocity";
inline constexpr const char* INPUT_OFFSET      = "inputOffset";
inline constexpr const char* OUTPUT_OFFSET     = "outputOffset";
inline constexpr const char* ACCELERATION      = "acceleration";
inline constexpr const char* DECAY_RATE        = "decayRate";
inline constexpr const char* GAMMA             = "gamma";
inline constexpr const char* MOTIVITY          = "motivity";
inline constexpr const char* EXPONENT_CLASSIC  = "exponentClassic";
inline constexpr const char* SCALE             = "scale";
inline constexpr const char* EXPONENT_POWER    = "exponentPower";
inline constexpr const char* LIMIT             = "limit";
inline constexpr const char* SYNC_SPEED        = "syncSpeed";
inline constexpr const char* SMOOTH            = "smooth";
inline constexpr const char* CAP               = "Cap / Jump";
inline constexpr const char* CAP_MODE          = "Cap mode";
inline constexpr const char* DATA              = "data";

// SpeedArgs.
inline constexpr const char* COMBINE_MAGNITUDES =
    "Whole/combined accel (set false for 'by component' mode)";
inline constexpr const char* LP_NORM            = "lpNorm";
inline constexpr const char* INPUT_SMOOTH_HALFLIFE =
    "Time in ms after which an input is weighted at half its original value.";
inline constexpr const char* SCALE_SMOOTH_HALFLIFE =
    "Time in ms after which scale is weighted at half its original value.";
inline constexpr const char* OUTPUT_SMOOTH_HALFLIFE =
    "Time in ms after which an output is weighted at half its original value.";

// Profile.
inline constexpr const char* NAME            = "name";
inline constexpr const char* DOMAIN_XY       = "Stretches domain for horizontal vs vertical inputs";
inline constexpr const char* RANGE_XY        = "Stretches accel range for horizontal vs vertical inputs";
inline constexpr const char* ARGS_X          = "Whole or horizontal accel parameters";
inline constexpr const char* ARGS_Y          = "Vertical accel parameters";
inline constexpr const char* INPUT_SPEED_ARGS = "Input speed calculation parameters";
inline constexpr const char* OUTPUT_DPI      = "Output DPI";
inline constexpr const char* YX_RATIO        = "Y/X output DPI ratio (vertical sens multiplier)";
inline constexpr const char* LR_RATIO        = "L/R output DPI ratio (left sens multiplier)";
inline constexpr const char* UD_RATIO        = "U/D output DPI ratio (up sens multiplier)";
inline constexpr const char* ROTATION        = "Degrees of rotation";
inline constexpr const char* SNAP            = "Degrees of angle snapping";
inline constexpr const char* MAXIMUM_SPEED   = "Input Speed Cap";

// DeviceConfig.
inline constexpr const char* DISABLE         = "disable";
inline constexpr const char* SET_EXTRA_INFO  = "setExtraInfo";
inline constexpr const char* POLL_TIME_LOCK  = "Use constant time interval based on polling rate";
inline constexpr const char* DPI             = "DPI (normalizes input speed unit: counts/ms -> in/s)";
inline constexpr const char* POLLING_RATE    = "Polling rate Hz (keep at 0 for automatic adjustment)";
inline constexpr const char* MINIMUM_TIME    = "minimumTime";
inline constexpr const char* MAXIMUM_TIME    = "maximumTime";

// Vec2 component names.
inline constexpr const char* X = "x";
inline constexpr const char* Y = "y";

} // namespace key

// Per-type parsers for the backend's resolved-config FFI and the curve shim. `j`
// is a single profile / device_config object. modifier_settings_from runs init_data.
ra::modifier_settings modifier_settings_from_jobject(const nlohmann::json& j);
ra::device_config device_config_from_jobject(const nlohmann::json& j);

ra::accel_mode accel_mode_from_string(const std::string& s);
ra::cap_mode cap_mode_from_string(const std::string& s);

// Linux wchar_t is 32-bit (UTF-32); caller buffer needs room for the null.
void utf8_to_wchar(const std::string& s, wchar_t* out, std::size_t cap);

} // namespace rajson
