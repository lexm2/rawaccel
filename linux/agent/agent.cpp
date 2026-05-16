#include "agent.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <utility>

namespace rawaccel_agent {

namespace {

bool has_wchar(const wchar_t* buf, std::size_t cap)
{
    for (std::size_t i = 0; i < cap; ++i) {
        if (buf[i] == 0) return i > 0;
    }
    return true;
}

bool name_matches(const wchar_t* a, std::size_t a_cap,
                  const wchar_t* b, std::size_t b_cap)
{
    std::size_t i = 0;
    for (; i < a_cap && i < b_cap; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == 0) return true;
    }
    return (i == a_cap || a[i] == 0) && (i == b_cap || b[i] == 0);
}

// Byte-wise compare; sufficient for canonical hidraw IDs ("0003:046D:..")
// and ASCII device names. Returns false when the wchar field is empty.
bool wchar_equals_utf8(const wchar_t* wbuf, std::size_t cap,
                       const std::string& s)
{
    if (!has_wchar(wbuf, cap)) return false;
    std::size_t i = 0;
    for (; i < s.size() && i < cap; ++i) {
        if (wbuf[i] == 0) return false;
        if (static_cast<wchar_t>(static_cast<unsigned char>(s[i])) != wbuf[i]) {
            return false;
        }
    }
    if (i < cap && wbuf[i] != 0) return false;
    return i == s.size();
}

} // namespace

Agent::Agent(Backend& backend) : backend_(backend) {}

VersionCheck Agent::check_version(const ra::version_t& client) const
{
    VersionCheck v;
    v.agent_version = ra::version;

    // The agent plays the driver role: clients below min_driver_version are
    // asked to upgrade; clients newer than the agent are asked to downgrade.
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
    std::vector<BindEntry> binds;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!pending_ || now < pending_at_) return false;
        auto to_apply = std::move(*pending_);
        pending_.reset();
        apply_locked(to_apply);
        last_apply_unix_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        binds = collect_binds_locked();
    }

    // Outside the lock: a slow backend must not stall queued RPCs.
    for (auto& [id, ms, dc] : binds) {
        backend_.bind_device(id, ms, dc);
    }
    return true;
}

void Agent::apply_locked(const rajson::driver_config& cfg)
{
    active_ = cfg;
    has_active_ = true;
}

void Agent::deactivate()
{
    std::vector<BindEntry> binds;
    {
        std::lock_guard<std::mutex> lock(mu_);
        pending_.reset();
        pending_at_ = time_point::min();
        rajson::driver_config default_cfg;
        apply_locked(default_cfg);
        last_apply_unix_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        binds = collect_binds_locked();
    }
    for (auto& [id, ms, dc] : binds) {
        backend_.bind_device(id, ms, dc);
    }
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
    s.connected_devices = known_devices_.size();
    return s;
}

bool Agent::load_from_file(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();

    rajson::driver_config cfg;
    try {
        cfg = rajson::from_string(ss.str());
    } catch (...) {
        return false;
    }

    std::vector<BindEntry> binds;
    {
        std::lock_guard<std::mutex> lock(mu_);
        apply_locked(cfg);
        binds = collect_binds_locked();
    }
    // Startup load skips WRITE_DELAY; the debounce only guards against
    // bursts of user-triggered apply RPCs.
    for (auto& [id, ms, dc] : binds) {
        backend_.bind_device(id, ms, dc);
    }
    return true;
}

void Agent::save_to_file(const std::string& path) const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::ofstream f(path, std::ios::trunc);
    f << rajson::to_string(active_);
}

void Agent::on_device_added(const DeviceInfo& info)
{
    ra::modifier_settings ms;
    ra::device_config dc;
    bool should_bind = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        known_devices_[info.id] = info;
        if (has_active_) {
            resolve_locked(info, ms, dc);
            should_bind = true;
        }
    }
    if (should_bind) {
        backend_.bind_device(info.id, ms, dc);
    }
}

void Agent::on_device_removed(DeviceId id)
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (known_devices_.erase(id) == 0) return;
    }
    backend_.unbind_device(id);
}

void Agent::resolve_locked(const DeviceInfo& info,
                           ra::modifier_settings& out_settings,
                           ra::device_config& out_config) const
{
    out_settings = active_.profiles.empty()
                       ? ra::modifier_settings{}
                       : active_.profiles.front();
    out_config = active_.default_device_config;

    for (const auto& dev : active_.devices) {
        const bool id_match  = wchar_equals_utf8(dev.id,   ra::MAX_DEV_ID_LEN, info.device_sysname);
        const bool name_match = wchar_equals_utf8(dev.name, ra::MAX_NAME_LEN,   info.name);
        if (!id_match && !name_match) continue;

        if (has_wchar(dev.profile, ra::MAX_NAME_LEN)) {
            for (const auto& mod : active_.profiles) {
                if (name_matches(mod.prof.name, ra::MAX_NAME_LEN,
                                 dev.profile, ra::MAX_NAME_LEN)) {
                    out_settings = mod;
                    break;
                }
            }
        }
        out_config = dev.config;
        return;
    }
}

std::vector<Agent::BindEntry> Agent::collect_binds_locked() const
{
    std::vector<BindEntry> out;
    out.reserve(known_devices_.size());
    for (const auto& [id, info] : known_devices_) {
        ra::modifier_settings ms;
        ra::device_config dc;
        resolve_locked(info, ms, dc);
        out.emplace_back(id, ms, dc);
    }
    return out;
}

} // namespace rawaccel_agent
