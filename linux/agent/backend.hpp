#pragma once

// Backend interface for rawaccel-agentd: the agent owns the math/state,
// backends own the per-packet transport.

#include "rawaccel.hpp"

#include <cstdint>
#include <string>

namespace rawaccel_agent {

namespace ra = rawaccel;

using DeviceId = std::uint64_t;

struct DeviceInfo {
    DeviceId id = 0;
    std::string sys_path;  // /sys/class/input/eventN or hidraw equivalent
    std::string name;      // human-readable, from EVIOCGNAME / device descriptor
    std::string uniq;      // UNIQ id from libevdev when present (mac/serial)
};

struct Backend {
    virtual ~Backend() = default;
    virtual void on_settings_changed(const ra::modifier_settings&) = 0;
    virtual void on_device_added(const DeviceInfo&) = 0;
    virtual void on_device_removed(DeviceId) = 0;
};

// Used by tests and as the default until a real backend is selected.
struct NoopBackend : Backend {
    int settings_changes = 0;
    int devices_added = 0;
    int devices_removed = 0;
    ra::modifier_settings last_settings{};

    void on_settings_changed(const ra::modifier_settings& s) override {
        last_settings = s;
        ++settings_changes;
    }
    void on_device_added(const DeviceInfo&) override { ++devices_added; }
    void on_device_removed(DeviceId) override { ++devices_removed; }
};

} // namespace rawaccel_agent
