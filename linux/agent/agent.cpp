#include "agent.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>

namespace rawaccel_agent {

ra::modifier_settings primary_profile(const rajson::driver_config& cfg)
{
    ra::modifier_settings out{};
    if (!cfg.profiles.empty()) {
        out = cfg.profiles.front();
    }
    ra::init_data(out);
    return out;
}

Agent::Agent(Backend& backend) : backend_(backend) {}

VersionCheck Agent::check_version(const ra::version_t& client) const
{
    VersionCheck v;
    v.agent_version = ra::version;

    // Symmetric to common/rawaccel-io.hpp:107-120: the user-mode caller checks
    // the driver version. Here the agent plays the driver role, so:
    //   client < min_driver_version : ask client to upgrade (reinstall).
    //   agent.version < client      : agent is older than client; ask client
    //                                 to downgrade or the user to upgrade the
    //                                 agent.
    if (client < ra::min_driver_version) {
        v.status = VersionStatus::client_too_old;
        v.message = "client below minimum supported version";
        return v;
    }
    if (ra::version < client) {
        v.status = VersionStatus::client_too_new;
        v.message = "agent is older than client";
        return v;
    }
    v.status = VersionStatus::ok;
    return v;
}

void Agent::schedule_apply(const rajson::driver_config& cfg, time_point now)
{
    std::lock_guard<std::mutex> lock(mu_);
    pending_ = cfg;
    pending_at_ = now + WRITE_DELAY;
}

bool Agent::tick(time_point now)
{
    std::optional<rajson::driver_config> to_apply;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!pending_ || now < pending_at_) return false;
        to_apply = std::move(pending_);
        pending_.reset();
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        apply_locked(*to_apply);
        last_apply_unix_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Notify outside the lock so a slow backend can't stall further RPCs.
    auto ms = primary_profile(*to_apply);
    backend_.on_settings_changed(ms);
    return true;
}

void Agent::apply_locked(const rajson::driver_config& cfg)
{
    active_ = cfg;
    has_active_ = true;
}

void Agent::deactivate()
{
    rajson::driver_config default_cfg;
    {
        std::lock_guard<std::mutex> lock(mu_);
        pending_.reset();
        pending_at_ = time_point::min();
        apply_locked(default_cfg);
        last_apply_unix_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    auto ms = primary_profile(default_cfg);
    backend_.on_settings_changed(ms);
}

rajson::driver_config Agent::get_active() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return active_;
}

double Agent::current_speed() const
{
    return backend_.current_speed();
}

Agent::Status Agent::status(time_point now) const
{
    std::lock_guard<std::mutex> lock(mu_);
    Status s{};
    s.has_active_config = has_active_;
    s.has_pending_apply = pending_.has_value();
    s.until_apply = std::chrono::milliseconds(0);
    if (pending_ && now < pending_at_) {
        s.until_apply = std::chrono::duration_cast<std::chrono::milliseconds>(
            pending_at_ - now);
    }
    s.last_apply_unix_ms = last_apply_unix_ms_;
    return s;
}

bool Agent::load_from_file(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    try {
        auto cfg = rajson::from_string(ss.str());
        std::lock_guard<std::mutex> lock(mu_);
        apply_locked(cfg);
    } catch (...) {
        return false;
    }
    // Surface the loaded profile to the backend immediately. There is no
    // settle delay on startup; the WriteDelay debounce only applies to
    // user-triggered apply RPCs.
    ra::modifier_settings ms;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ms = primary_profile(active_);
    }
    backend_.on_settings_changed(ms);
    return true;
}

void Agent::save_to_file(const std::string& path) const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::ofstream f(path, std::ios::trunc);
    f << rajson::to_string(active_);
}

} // namespace rawaccel_agent
