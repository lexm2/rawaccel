#pragma once

// Backend interface. The agent owns the math/state and resolves each
// device to a (modifier_settings, device_config) pair before calling
// bind_device; backends never reach into driver_config themselves.

#include "rawaccel.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace rawaccel_agent {

namespace ra = rawaccel;

using DeviceId = std::uint64_t;

// All fields are best-effort; only the populated ones are matched against
// driver_config.devices[].
struct DeviceInfo {
    DeviceId id = 0;
    std::string sysname;          // hidrawN
    std::string device_sysname;   // 0003:VVVV:PPPP.IIII
    std::uint32_t vendor_id = 0;
    std::uint32_t product_id = 0;
    std::string name;             // HID_NAME
};

// Implemented by Agent; declared here so backend code does not depend on
// agent.hpp.
struct DeviceListener {
    virtual ~DeviceListener() = default;
    virtual void on_device_added(const DeviceInfo&) = 0;
    virtual void on_device_removed(DeviceId) = 0;
};

struct Backend {
    virtual ~Backend() = default;

    // Set or replace the active settings for one device. May assume id was
    // previously reported via DeviceListener::on_device_added.
    virtual void bind_device(DeviceId,
                             const ra::modifier_settings&,
                             const ra::device_config&) = 0;

    // Safe to call for an unknown id (no-op).
    virtual void unbind_device(DeviceId) = 0;

    // Returns 0 when the backend has no per-packet visibility (e.g. BPF).
    virtual double current_speed() const { return 0.0; }
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
