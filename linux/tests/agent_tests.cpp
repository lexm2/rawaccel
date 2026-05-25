#include "agent.hpp"
#include "backend.hpp"
#include "json_io.hpp"
#include "test_harness.hpp"

#include "rawaccel-version.h"

#include <chrono>
#include <cstring>

using namespace rawaccel_agent;
namespace ra = rawaccel;

namespace {

DeviceInfo make_info(DeviceId id,
                     const std::string& sysname,
                     const std::string& device_sysname,
                     const std::string& name = {})
{
    DeviceInfo info;
    info.id = id;
    info.sysname = sysname;
    info.device_sysname = device_sysname;
    info.name = name;
    return info;
}

void wcopy(wchar_t* dst, std::size_t cap, const wchar_t* src)
{
    std::size_t i = 0;
    for (; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

ra::modifier_settings named_profile(const wchar_t* name, double output_dpi)
{
    ra::modifier_settings m{};
    wcopy(m.prof.name, ra::MAX_NAME_LEN, name);
    m.prof.output_dpi = output_dpi;
    ra::init_data(m);
    return m;
}

} // namespace

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

RA_TEST("Agent: deactivate clears pending and rebinds known devices")
{
    NoopBackend backend;
    Agent agent(backend);
    agent.on_device_added(make_info(1, "hidraw0", "0003:046D:C54D.000A"));
    RA_CHECK_EQ(backend.binds, 0);  // no active config yet

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
    // deactivate rebinds every known device against the default config
    RA_CHECK_EQ(backend.binds, 1);

    auto active = agent.get_active();
    RA_CHECK(active.profiles.empty());

    // subsequent tick is a no-op
    RA_CHECK(!agent.tick(t0 + std::chrono::milliseconds(2000)));
    RA_CHECK_EQ(backend.binds, 1);
}

RA_TEST("Agent: apply debounces to one bind per known device")
{
    NoopBackend backend;
    Agent agent(backend);
    agent.on_device_added(make_info(1, "hidraw0", "0003:046D:C54D.000A"));

    rajson::driver_config cfg_a;
    rajson::driver_config cfg_b;
    cfg_b.profiles.emplace_back();  // distinguish from cfg_a (empty)

    auto t0 = clock_type::now();
    agent.schedule_apply(cfg_a, t0);
    agent.schedule_apply(cfg_b, t0 + std::chrono::milliseconds(100));

    // 100ms after second schedule_apply: still inside the 1s window
    RA_CHECK(!agent.tick(t0 + std::chrono::milliseconds(200)));
    RA_CHECK_EQ(backend.binds, 0);

    // deadline reached: only cfg_b applies, cfg_a debounced out -> one bind
    RA_CHECK(agent.tick(t0 + std::chrono::milliseconds(1150)));
    RA_CHECK_EQ(backend.binds, 1);

    // repeat tick: nothing pending
    RA_CHECK(!agent.tick(t0 + std::chrono::milliseconds(2000)));
    RA_CHECK_EQ(backend.binds, 1);
}

RA_TEST("Agent: apply held until WRITE_DELAY elapses")
{
    NoopBackend backend;
    Agent agent(backend);
    agent.on_device_added(make_info(1, "hidraw0", "0003:046D:C54D.000A"));

    rajson::driver_config cfg;
    cfg.profiles.emplace_back();

    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);

    // just before deadline: no apply
    RA_CHECK(!agent.tick(t0 + std::chrono::milliseconds(999)));
    RA_CHECK_EQ(backend.binds, 0);

    // at deadline: apply
    RA_CHECK(agent.tick(t0 + WRITE_DELAY));
    RA_CHECK_EQ(backend.binds, 1);
}

RA_TEST("Agent: apply with no known devices binds nothing")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    cfg.profiles.emplace_back();

    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);
    agent.tick(t0 + WRITE_DELAY);

    RA_CHECK_EQ(backend.binds, 0);
}

RA_TEST("Agent: on_device_added before apply waits for active config")
{
    NoopBackend backend;
    Agent agent(backend);
    agent.on_device_added(make_info(1, "hidraw0", "0003:046D:C54D.000A"));
    // no apply yet -> no bind
    RA_CHECK_EQ(backend.binds, 0);
}

RA_TEST("Agent: on_device_added after apply binds immediately")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    cfg.profiles.emplace_back(named_profile(L"default", 1234.5));
    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);
    agent.tick(t0 + WRITE_DELAY);
    RA_CHECK_EQ(backend.binds, 0);

    agent.on_device_added(make_info(7, "hidraw7", "0003:046D:C54D.000A"));
    RA_CHECK_EQ(backend.binds, 1);
    RA_CHECK_EQ(backend.last_settings[7].prof.output_dpi, 1234.5);
}

RA_TEST("Agent: on_device_removed unbinds and is idempotent")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    cfg.profiles.emplace_back();
    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);
    agent.tick(t0 + WRITE_DELAY);

    agent.on_device_added(make_info(42, "hidraw0", "0003:046D:C54D.000A"));
    RA_CHECK_EQ(backend.binds, 1);

    agent.on_device_removed(42);
    RA_CHECK_EQ(backend.unbinds, 1);

    // second removal: no-op
    agent.on_device_removed(42);
    RA_CHECK_EQ(backend.unbinds, 1);
}

RA_TEST("Agent: resolve picks profile matched by device id")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    cfg.profiles.emplace_back(named_profile(L"default",  1000.0));
    cfg.profiles.emplace_back(named_profile(L"gaming",   8000.0));
    cfg.profiles.emplace_back(named_profile(L"trackpad", 1600.0));

    ra::device_settings ds{};
    const wchar_t* id = L"0003:046D:C54D.000A";
    wcopy(ds.id, ra::MAX_DEV_ID_LEN, id);
    wcopy(ds.profile, ra::MAX_NAME_LEN, L"gaming");
    cfg.devices.push_back(ds);

    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);
    agent.tick(t0 + WRITE_DELAY);

    agent.on_device_added(make_info(1, "hidraw0", "0003:046D:C54D.000A"));
    RA_CHECK_EQ(backend.binds, 1);
    RA_CHECK_EQ(backend.last_settings[1].prof.output_dpi, 8000.0);

    // different device falls back to the first profile
    agent.on_device_added(make_info(2, "hidraw1", "0003:1234:5678.000B", "Some Trackpad"));
    RA_CHECK_EQ(backend.binds, 2);
    RA_CHECK_EQ(backend.last_settings[2].prof.output_dpi, 1000.0);
}

RA_TEST("Agent: resolve picks profile matched by device name")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    cfg.profiles.emplace_back(named_profile(L"default", 1000.0));
    cfg.profiles.emplace_back(named_profile(L"by_name", 4242.0));

    ra::device_settings ds{};
    wcopy(ds.name, ra::MAX_NAME_LEN, L"Logitech G Pro");
    wcopy(ds.profile, ra::MAX_NAME_LEN, L"by_name");
    cfg.devices.push_back(ds);

    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);
    agent.tick(t0 + WRITE_DELAY);

    agent.on_device_added(make_info(5, "hidraw0", "0003:046D:C54D.000A",
                                    "Logitech G Pro"));
    RA_CHECK_EQ(backend.last_settings[5].prof.output_dpi, 4242.0);
}

RA_TEST("Agent: resolve falls back when profile name is unknown")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    cfg.profiles.emplace_back(named_profile(L"default", 1000.0));

    ra::device_settings ds{};
    const wchar_t* id = L"0003:046D:C54D.000A";
    wcopy(ds.id, ra::MAX_DEV_ID_LEN, id);
    wcopy(ds.profile, ra::MAX_NAME_LEN, L"nonexistent");
    cfg.devices.push_back(ds);

    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);
    agent.tick(t0 + WRITE_DELAY);

    agent.on_device_added(make_info(1, "hidraw0", "0003:046D:C54D.000A"));
    RA_CHECK_EQ(backend.last_settings[1].prof.output_dpi, 1000.0);
}

RA_TEST("Agent: resolve uses device_config for matched devices, default otherwise")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    cfg.profiles.emplace_back(named_profile(L"default", 1000.0));
    cfg.default_device_config.dpi = 800;

    ra::device_settings ds{};
    const wchar_t* id = L"0003:046D:C54D.000A";
    wcopy(ds.id, ra::MAX_DEV_ID_LEN, id);
    wcopy(ds.profile, ra::MAX_NAME_LEN, L"default");
    ds.config.dpi = 16000;
    cfg.devices.push_back(ds);

    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);
    agent.tick(t0 + WRITE_DELAY);

    agent.on_device_added(make_info(1, "hidraw0", "0003:046D:C54D.000A"));
    RA_CHECK_EQ(backend.last_configs[1].dpi, 16000);

    agent.on_device_added(make_info(2, "hidraw1", "0003:0001:0002.000B"));
    RA_CHECK_EQ(backend.last_configs[2].dpi, 800);
}

RA_TEST("Agent: apply rebinds every known device")
{
    NoopBackend backend;
    Agent agent(backend);
    agent.on_device_added(make_info(1, "hidraw0", "0003:046D:C54D.000A"));
    agent.on_device_added(make_info(2, "hidraw1", "0003:1234:5678.000B"));

    rajson::driver_config cfg;
    cfg.profiles.emplace_back();
    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);
    agent.tick(t0 + WRITE_DELAY);

    RA_CHECK_EQ(backend.binds, 2);
}

RA_TEST("Agent: version check matches driver semantics")
{
    NoopBackend backend;
    Agent agent(backend);

    // same version: ok
    {
        auto vc = agent.check_version(ra::version);
        RA_CHECK(vc.status == VersionStatus::ok);
    }
    // older than minimum: client_too_old
    {
        ra::version_t old_v{0, 0, 1};
        auto vc = agent.check_version(old_v);
        RA_CHECK(vc.status == VersionStatus::client_too_old);
    }
    // newer than agent: client_too_new
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

RA_TEST("Agent: status reports pending window and device count")
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
        RA_CHECK_EQ(static_cast<int>(s.connected_devices), 0);
    }

    agent.on_device_added(make_info(1, "hidraw0", "0003:046D:C54D.000A"));
    {
        auto s = agent.status(t0);
        RA_CHECK_EQ(static_cast<int>(s.connected_devices), 1);
    }

    agent.schedule_apply(cfg, t0);
    {
        auto s = agent.status(t0 + std::chrono::milliseconds(250));
        RA_CHECK(s.has_pending_apply);
        // 1000ms window - 250ms elapsed = 750ms remaining
        RA_CHECK_EQ(s.until_apply.count(), 750);
    }

    agent.tick(t0 + WRITE_DELAY);
    {
        auto s = agent.status(t0 + WRITE_DELAY);
        RA_CHECK(s.has_active_config);
        RA_CHECK(!s.has_pending_apply);
    }
}
