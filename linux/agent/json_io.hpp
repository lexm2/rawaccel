#pragma once

// Native C++ port of wrapper/wrapper.cpp's JSON layer. Key names are the cross-OS
// settings.json contract: do NOT rename a key without changing the Windows JsonProperty.

#include "rawaccel.hpp"
#include "rawaccel-json-keys.h"

#include <nlohmann/json.hpp>

#include <string>

namespace rajson {

namespace ra = rawaccel;

// Key names come from common/rawaccel-json-keys.h (shared with wrapper.cpp); these
// constants just give the C++ math layer typed handles. Don't inline a literal here.
namespace key {

// AccelArgs (mode-specific knobs).
inline constexpr const char* MODE              = RA_JK_MODE;
inline constexpr const char* GAIN              = RA_JK_GAIN;
inline constexpr const char* INPUT_OFFSET      = RA_JK_INPUT_OFFSET;
inline constexpr const char* OUTPUT_OFFSET     = RA_JK_OUTPUT_OFFSET;
inline constexpr const char* ACCELERATION      = RA_JK_ACCELERATION;
inline constexpr const char* DECAY_RATE        = RA_JK_DECAY_RATE;
inline constexpr const char* GAMMA             = RA_JK_GAMMA;
inline constexpr const char* MOTIVITY          = RA_JK_MOTIVITY;
inline constexpr const char* EXPONENT_CLASSIC  = RA_JK_EXPONENT_CLASSIC;
inline constexpr const char* SCALE             = RA_JK_SCALE;
inline constexpr const char* EXPONENT_POWER    = RA_JK_EXPONENT_POWER;
inline constexpr const char* LIMIT             = RA_JK_LIMIT;
inline constexpr const char* SYNC_SPEED        = RA_JK_SYNC_SPEED;
inline constexpr const char* SMOOTH            = RA_JK_SMOOTH;
inline constexpr const char* CAP               = RA_JK_CAP;
inline constexpr const char* CAP_MODE          = RA_JK_CAP_MODE;
inline constexpr const char* DATA              = RA_JK_DATA;

// SpeedArgs.
inline constexpr const char* COMBINE_MAGNITUDES    = RA_JK_COMBINE_MAGNITUDES;
inline constexpr const char* LP_NORM               = RA_JK_LP_NORM;
inline constexpr const char* INPUT_SMOOTH_HALFLIFE  = RA_JK_INPUT_SMOOTH_HALFLIFE;
inline constexpr const char* SCALE_SMOOTH_HALFLIFE  = RA_JK_SCALE_SMOOTH_HALFLIFE;
inline constexpr const char* OUTPUT_SMOOTH_HALFLIFE = RA_JK_OUTPUT_SMOOTH_HALFLIFE;

// Profile.
inline constexpr const char* NAME            = RA_JK_NAME;
inline constexpr const char* DOMAIN_XY       = RA_JK_DOMAIN_XY;
inline constexpr const char* RANGE_XY        = RA_JK_RANGE_XY;
inline constexpr const char* ARGS_X          = RA_JK_ARGS_X;
inline constexpr const char* ARGS_Y          = RA_JK_ARGS_Y;
inline constexpr const char* INPUT_SPEED_ARGS = RA_JK_INPUT_SPEED_ARGS;
inline constexpr const char* OUTPUT_DPI      = RA_JK_OUTPUT_DPI;
inline constexpr const char* YX_RATIO        = RA_JK_YX_RATIO;
inline constexpr const char* LR_RATIO        = RA_JK_LR_RATIO;
inline constexpr const char* UD_RATIO        = RA_JK_UD_RATIO;
inline constexpr const char* ROTATION        = RA_JK_ROTATION;
inline constexpr const char* SNAP            = RA_JK_SNAP;
inline constexpr const char* MAXIMUM_SPEED   = RA_JK_MAXIMUM_SPEED;

// DeviceConfig.
inline constexpr const char* DISABLE         = RA_JK_DISABLE;
inline constexpr const char* SET_EXTRA_INFO  = RA_JK_SET_EXTRA_INFO;
inline constexpr const char* POLL_TIME_LOCK  = RA_JK_POLL_TIME_LOCK;
inline constexpr const char* DPI             = RA_JK_DPI;
inline constexpr const char* POLLING_RATE    = RA_JK_POLLING_RATE;
inline constexpr const char* MINIMUM_TIME    = RA_JK_MINIMUM_TIME;
inline constexpr const char* MAXIMUM_TIME    = RA_JK_MAXIMUM_TIME;

// Vec2 component names.
inline constexpr const char* X = RA_JK_X;
inline constexpr const char* Y = RA_JK_Y;

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
