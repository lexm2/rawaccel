#pragma once

// Single source of truth for the settings.json key names (the cross-OS contract).
// Plain string-literal macros so every C/C++ consumer resolves the same value:
//   wrapper/wrapper.cpp      [JsonProperty(RA_JK_GAIN)]              (Windows C++/CLI)
//   linux/agent/json_io.hpp  constexpr ... GAIN = RA_JK_GAIN;        (Linux native)
//   linux/tests/shim_tests   args[rajson::key::GAIN] = ...;          (via the header)
// Rename a key here and all of them move together. The C# RawAccel.Contracts
// assembly carries its own parallel [JsonProperty] strings (different language,
// cannot include this header) -- keep it in sync by hand if a key changes.

// AccelArgs (mode-specific knobs).
#define RA_JK_MODE              "mode"
#define RA_JK_GAIN              "Gain / Velocity"
#define RA_JK_INPUT_OFFSET      "inputOffset"
#define RA_JK_OUTPUT_OFFSET     "outputOffset"
#define RA_JK_ACCELERATION      "acceleration"
#define RA_JK_DECAY_RATE        "decayRate"
#define RA_JK_GAMMA             "gamma"
#define RA_JK_MOTIVITY          "motivity"
#define RA_JK_EXPONENT_CLASSIC  "exponentClassic"
#define RA_JK_SCALE             "scale"
#define RA_JK_EXPONENT_POWER    "exponentPower"
#define RA_JK_LIMIT             "limit"
#define RA_JK_SYNC_SPEED        "syncSpeed"
#define RA_JK_SMOOTH            "smooth"
#define RA_JK_CAP               "Cap / Jump"
#define RA_JK_CAP_MODE          "Cap mode"
#define RA_JK_DATA              "data"

// SpeedArgs.
#define RA_JK_COMBINE_MAGNITUDES \
    "Whole/combined accel (set false for 'by component' mode)"
#define RA_JK_LP_NORM           "lpNorm"
#define RA_JK_INPUT_SMOOTH_HALFLIFE \
    "Time in ms after which an input is weighted at half its original value."
#define RA_JK_SCALE_SMOOTH_HALFLIFE \
    "Time in ms after which scale is weighted at half its original value."
#define RA_JK_OUTPUT_SMOOTH_HALFLIFE \
    "Time in ms after which an output is weighted at half its original value."

// Profile.
#define RA_JK_NAME              "name"
#define RA_JK_DOMAIN_XY         "Stretches domain for horizontal vs vertical inputs"
#define RA_JK_RANGE_XY          "Stretches accel range for horizontal vs vertical inputs"
#define RA_JK_ARGS_X            "Whole or horizontal accel parameters"
#define RA_JK_ARGS_Y            "Vertical accel parameters"
#define RA_JK_INPUT_SPEED_ARGS  "Input speed calculation parameters"
#define RA_JK_OUTPUT_DPI        "Output DPI"
#define RA_JK_YX_RATIO          "Y/X output DPI ratio (vertical sens multiplier)"
#define RA_JK_LR_RATIO          "L/R output DPI ratio (left sens multiplier)"
#define RA_JK_UD_RATIO          "U/D output DPI ratio (up sens multiplier)"
#define RA_JK_ROTATION          "Degrees of rotation"
#define RA_JK_SNAP              "Degrees of angle snapping"
#define RA_JK_MAXIMUM_SPEED     "Input Speed Cap"

// DeviceConfig.
#define RA_JK_DISABLE           "disable"
#define RA_JK_SET_EXTRA_INFO    "setExtraInfo"
#define RA_JK_POLL_TIME_LOCK    "Use constant time interval based on polling rate"
#define RA_JK_DPI               "DPI (normalizes input speed unit: counts/ms -> in/s)"
#define RA_JK_POLLING_RATE      "Polling rate Hz (keep at 0 for automatic adjustment)"
#define RA_JK_MINIMUM_TIME      "minimumTime"
#define RA_JK_MAXIMUM_TIME      "maximumTime"

// Vec2 component names.
#define RA_JK_X                 "x"
#define RA_JK_Y                 "y"
