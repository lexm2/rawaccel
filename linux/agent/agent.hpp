#pragma once

// Agent state container. Mirrors DEVICE_EXTENSION in driver/driver.h:21-34 but
// at the userspace level: holds the active modifier_settings + modifier and
// applies the 1-second WriteDelay debounce from driver/driver.cpp:446-451 to
// any incoming settings update.
//
// The Agent is decoupled from IPC so tests can drive it without sockets.

#include "backend.hpp"
#include "json_io.hpp"

#include "rawaccel.hpp"
#include "rawaccel-base.hpp"
#include "rawaccel-version.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

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

class Agent {
public:
    explicit Agent(Backend& backend);

    // Mirror of valid_version_or_throw in common/rawaccel-io.hpp:107-120, but
    // returns rather than throws so the IPC layer can format an error frame.
    VersionCheck check_version(const ra::version_t& client) const;

    // Stage a new config. The active modifier is not swapped immediately;
    // tick(now) does that after WRITE_DELAY has elapsed since the last
    // schedule_apply call. Repeated calls within the window collapse to a
    // single apply of the most recent config (matches the spirit of the
    // Windows-side delay: short bursts of IOCTL writes are debounced).
    void schedule_apply(const rajson::driver_config& cfg, time_point now);

    // Drive the debounce. Called from the control loop on a poll tick. Returns
    // true if it swapped settings into the backend this call.
    bool tick(time_point now);

    // Reset to a default (no-acceleration) config immediately, bypassing
    // the WRITE_DELAY debounce. Cancels any pending apply. Notifies the
    // backend on the same call so input passes through with no scaling.
    void deactivate();

    // Snapshot of the currently active config. Includes nothing pending.
    rajson::driver_config get_active() const;

    // Most recent smoothed input speed across all backend-tracked devices,
    // in the same units the curve sees (DPI-normalized magnitude per ms).
    // Delegates to Backend::current_speed(); returns 0 when the backend
    // has no per-packet visibility (e.g. BPF).
    double current_speed() const;

    // Status info for the "status" RPC.
    struct Status {
        bool has_active_config;
        bool has_pending_apply;
        std::chrono::milliseconds until_apply;  // 0 if no pending
        std::int64_t last_apply_unix_ms;        // 0 if never applied
    };
    Status status(time_point now) const;

    // Load settings.json from disk on startup. Returns false if the file is
    // missing or unparseable; the caller decides whether to fall back to
    // defaults.
    bool load_from_file(const std::string& path);

    // Persist the active config back to disk. Called after a successful apply.
    void save_to_file(const std::string& path) const;

private:
    Backend& backend_;

    mutable std::mutex mu_;
    rajson::driver_config active_;
    std::optional<rajson::driver_config> pending_;
    time_point pending_at_ = time_point::min();
    std::int64_t last_apply_unix_ms_ = 0;
    bool has_active_ = false;

    void apply_locked(const rajson::driver_config& cfg);
};

// Build a ra::modifier_settings from the first profile in a driver_config.
// The agent surfaces one active profile to the backend; the BPF backend
// applies it across every attached hidraw mouse.
ra::modifier_settings primary_profile(const rajson::driver_config& cfg);

} // namespace rawaccel_agent
