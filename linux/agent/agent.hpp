#pragma once

// Userspace counterpart to the driver's DEVICE_EXTENSION: holds the active
// driver_config, debounces incoming updates by WRITE_DELAY, and resolves each
// connected device to a (modifier_settings, device_config) pair for the
// backend. Decoupled from IPC so tests can drive it without sockets.

#include "backend.hpp"
#include "json_io.hpp"

#include "rawaccel.hpp"
#include "rawaccel-base.hpp"
#include "rawaccel-version.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace rawaccel_agent {

namespace ra = rawaccel;

using clock_type = std::chrono::steady_clock;
using time_point = clock_type::time_point;
using duration   = clock_type::duration;

inline constexpr auto WRITE_DELAY = std::chrono::milliseconds(
    static_cast<long long>(ra::WRITE_DELAY));

enum class VersionStatus {
    ok,
    client_too_old,    // mirror of "reinstallation required"
    client_too_new,    // mirror of "newer driver is installed"
};

struct VersionCheck {
    VersionStatus status;
    ra::version_t agent_version;
    std::string message;  // human-readable on failure, empty on ok
};

class Agent : public DeviceListener {
public:
    explicit Agent(Backend& backend);

    // Non-throwing parallel to valid_version_or_throw, so the IPC layer can
    // format an error frame instead of unwinding.
    VersionCheck check_version(const ra::version_t& client) const;

    // Stage a config. Repeated calls within WRITE_DELAY collapse to a single
    // apply of the most recent one; tick() commits when the timer elapses.
    void schedule_apply(const rajson::driver_config& cfg, time_point now);

    bool tick(time_point now);

    // Reset to a noaccel config immediately, bypassing WRITE_DELAY.
    void deactivate();

    rajson::driver_config get_active() const;

    // 0 when the backend has no per-packet visibility (e.g. BPF).
    double current_speed() const;

    struct Status {
        bool has_active_config;
        bool has_pending_apply;
        std::chrono::milliseconds until_apply;  // 0 if no pending
        std::int64_t last_apply_unix_ms;        // 0 if never applied
        std::size_t connected_devices;
    };
    Status status(time_point now) const;

    // Non-empty when the data plane has devices but none are engaged (an apply
    // would write settings that have no effect). Surfaced as an apply error so
    // the GUI/CLI does not report a successful write that does nothing.
    std::optional<std::string> data_plane_failure() const;

    bool load_from_file(const std::string& path);
    void save_to_file(const std::string& path) const;

    void on_device_added(const DeviceInfo& info) override;
    void on_device_removed(DeviceId id) override;

private:
    Backend& backend_;

    mutable std::mutex mu_;
    rajson::driver_config active_;
    std::optional<rajson::driver_config> pending_;
    time_point pending_at_ = time_point::min();
    std::int64_t last_apply_unix_ms_ = 0;
    bool has_active_ = false;

    std::unordered_map<DeviceId, DeviceInfo> known_devices_;

    void apply_locked(const rajson::driver_config& cfg);

    // Match priority: device_settings.id == DeviceInfo.device_sysname, then
    // device_settings.name == DeviceInfo.name. First match wins. Falls back
    // to the first profile + default_device_config when nothing matches.
    void resolve_locked(const DeviceInfo& info,
                        ra::modifier_settings& out_settings,
                        ra::device_config& out_config) const;

    using BindEntry = std::tuple<DeviceId, ra::modifier_settings, ra::device_config>;
    std::vector<BindEntry> collect_binds_locked() const;
};

} // namespace rawaccel_agent
