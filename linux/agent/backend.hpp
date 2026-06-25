#pragma once

// Backend interface. Agent resolves each device to (modifier_settings, device_config) before bind_device; backends never touch driver_config.

#include "rawaccel.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace rawaccel_agent {

namespace ra = rawaccel;

using DeviceId = std::uint64_t;

// DataPlaneHealth how many of the devices found are actually attached, lets the UI throw error when nothing is attached
struct DataPlaneHealth {
    std::size_t devices = 0;
    std::size_t attached = 0;
    std::string error;
};

// Current input speed in chart units (counts/ms at NORMALIZED_DPI). `combined` is the lp-norm/hypot magnitude for combined mode; x/y are per-axis for separate mode. All zero = idle or no per-packet visibility.
struct SpeedSample {
    double x = 0.0;
    double y = 0.0;
    double combined = 0.0;
};

struct Backend {
    virtual ~Backend() = default;

    virtual void bind_device(DeviceId,
                             const ra::modifier_settings&,
                             const ra::device_config&) = 0;

    virtual void unbind_device(DeviceId) = 0;

    virtual SpeedSample current_speed_sample() const { return {}; }

    virtual DataPlaneHealth health() const { return {}; }
};

// Test/default backend.
struct NoopBackend : Backend {
    int binds = 0;
    int unbinds = 0;
    std::unordered_map<DeviceId, ra::modifier_settings> last_settings;
    std::unordered_map<DeviceId, ra::device_config> last_configs;

    void bind_device(DeviceId id,
                     const ra::modifier_settings& s,
                     const ra::device_config& c) override {
        last_settings[id] = s;
        last_configs[id] = c;
        ++binds;
    }
    void unbind_device(DeviceId id) override {
        last_settings.erase(id);
        last_configs.erase(id);
        ++unbinds;
    }
};

} // namespace rawaccel_agent
