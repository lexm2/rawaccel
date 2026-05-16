#include "agent.hpp"
#include "backend.hpp"
#include "test_harness.hpp"

#include "rawaccel-version.h"

#include <chrono>

using namespace rawaccel_agent;
namespace ra = rawaccel;

RA_TEST("Agent: current_speed delegates to backend")
{
    struct SpeedBackend : NoopBackend {
        double speed = 0.0;
        double current_speed() const override { return speed; }
    };
    SpeedBackend backend;
    Agent agent(backend);

    RA_CHECK_EQ(agent.current_speed(), 0.0);
    backend.speed = 12.5;
    RA_CHECK_EQ(agent.current_speed(), 12.5);
}

RA_TEST("Agent: deactivate clears pending, applies default immediately")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    cfg.profiles.emplace_back();

    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);
    {
        auto s = agent.status(t0);
        RA_CHECK(s.has_pending_apply);
    }

    agent.deactivate();

    auto s = agent.status(t0);
    RA_CHECK(!s.has_pending_apply);
    RA_CHECK(s.has_active_config);
    RA_CHECK_EQ(backend.settings_changes, 1);

    auto active = agent.get_active();
    RA_CHECK(active.profiles.empty());

    // A subsequent tick should be a no-op (no pending).
    RA_CHECK(!agent.tick(t0 + std::chrono::milliseconds(2000)));
    RA_CHECK_EQ(backend.settings_changes, 1);
}

RA_TEST("Agent: apply debounces to one backend call")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg_a;
    rajson::driver_config cfg_b;
    cfg_b.profiles.emplace_back();  // distinguish from cfg_a (empty profiles).

    auto t0 = clock_type::now();
    agent.schedule_apply(cfg_a, t0);
    agent.schedule_apply(cfg_b, t0 + std::chrono::milliseconds(100));

    // 100ms after the second schedule_apply: still within the 1s window.
    RA_CHECK(!agent.tick(t0 + std::chrono::milliseconds(200)));
    RA_CHECK_EQ(backend.settings_changes, 0);

    // 1.05s after the second schedule_apply: deadline reached. Only cfg_b
    // should apply; cfg_a is debounced out.
    RA_CHECK(agent.tick(t0 + std::chrono::milliseconds(1150)));
    RA_CHECK_EQ(backend.settings_changes, 1);

    // Repeat tick: no more pending.
    RA_CHECK(!agent.tick(t0 + std::chrono::milliseconds(2000)));
    RA_CHECK_EQ(backend.settings_changes, 1);
}

RA_TEST("Agent: apply held until WRITE_DELAY elapses")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    cfg.profiles.emplace_back();

    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);

    // Just before the deadline: no apply.
    RA_CHECK(!agent.tick(t0 + std::chrono::milliseconds(999)));
    RA_CHECK_EQ(backend.settings_changes, 0);

    // At the deadline: apply.
    RA_CHECK(agent.tick(t0 + WRITE_DELAY));
    RA_CHECK_EQ(backend.settings_changes, 1);
}

RA_TEST("Agent: version check matches driver semantics")
{
    NoopBackend backend;
    Agent agent(backend);

    // Same version: ok.
    {
        auto vc = agent.check_version(ra::version);
        RA_CHECK(vc.status == VersionStatus::ok);
    }
    // Older than minimum: client_too_old.
    {
        ra::version_t old_v{0, 0, 1};
        auto vc = agent.check_version(old_v);
        RA_CHECK(vc.status == VersionStatus::client_too_old);
    }
    // Newer than agent: client_too_new.
    {
        ra::version_t newer{
            ra::version.major,
            ra::version.minor,
            ra::version.patch + 1
        };
        auto vc = agent.check_version(newer);
        RA_CHECK(vc.status == VersionStatus::client_too_new);
    }
}

RA_TEST("Agent: status reports pending window")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    auto t0 = clock_type::now();

    {
        auto s = agent.status(t0);
        RA_CHECK(!s.has_active_config);
        RA_CHECK(!s.has_pending_apply);
        RA_CHECK_EQ(s.until_apply.count(), 0);
    }

    agent.schedule_apply(cfg, t0);
    {
        auto s = agent.status(t0 + std::chrono::milliseconds(250));
        RA_CHECK(s.has_pending_apply);
        // 1000ms window minus 250ms elapsed = 750ms remaining.
        RA_CHECK_EQ(s.until_apply.count(), 750);
    }

    agent.tick(t0 + WRITE_DELAY);
    {
        auto s = agent.status(t0 + WRITE_DELAY);
        RA_CHECK(s.has_active_config);
        RA_CHECK(!s.has_pending_apply);
    }
}
