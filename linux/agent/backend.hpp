#pragma once

// Backend interface. The agent resolves each device to a
// (modifier_settings, device_config) pair before bind_device;
// backends never touch driver_config.

#include "rawaccel.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace rawaccel_agent {

namespace ra = rawaccel;

using DeviceId = std::uint64_t;

// Best-effort fields; only populated ones match driver_config.devices[].
struct DeviceInfo {
    DeviceId id = 0;
    std::string sysname;          // hidrawN
    std::string device_sysname;   // 0003:VVVV:PPPP.IIII
    std::uint32_t vendor_id = 0;
    std::uint32_t product_id = 0;
    std::string name;             // HID_NAME
};

// Implemented by Agent; here so backends don't depend on agent.hpp.
struct DeviceListener {
    virtual ~DeviceListener() = default;
    virtual void on_device_added(const DeviceInfo&) = 0;
    virtual void on_device_removed(DeviceId) = 0;
};

// Kernel data-plane health: devices prepared vs actually attached; `error` is
// the first failure. Lets the control plane fail an apply when nothing attached.
struct DataPlaneHealth {
    std::size_t devices = 0;
    std::size_t attached = 0;
    std::string error;
};

// Snapshot of the current input speed in chart units (normalized in/s, i.e.
// counts/ms at NORMALIZED_DPI). `combined` is the lp-norm/hypot magnitude used
// when X and Y are combined; x/y are the per-axis speeds used in separate mode.
// All zero means idle or no per-packet visibility.
struct SpeedSample {
    double x = 0.0;
    double y = 0.0;
    double combined = 0.0;
};

struct Backend {
    virtual ~Backend() = default;

    // Set/replace settings for one device (id was reported via on_device_added).
    virtual void bind_device(DeviceId,
                             const ra::modifier_settings&,
                             const ra::device_config&) = 0;

    // Safe to call for an unknown id (no-op).
    virtual void unbind_device(DeviceId) = 0;

    // Per-axis + combined current input speed. Default: all zero (backends
    // without per-packet visibility, e.g. Noop).
    virtual SpeedSample current_speed_sample() const { return {}; }

    // Data-plane attach health. Default: nothing to report (e.g. Noop).
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
