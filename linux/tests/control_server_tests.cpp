#include "agent.hpp"
#include "backend.hpp"
#include "control_server.hpp"
#include "test_harness.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>

using namespace rawaccel_agent;
namespace ra = rawaccel;
using json = nlohmann::json;

RA_TEST("Dispatch: version ok for matching version")
{
    NoopBackend backend;
    Agent agent(backend);

    json req = {
        {"cmd", "version"},
        {"client", {
            {"major", ra::version.major},
            {"minor", ra::version.minor},
            {"patch", ra::version.patch},
        }},
    };
    auto resp = json::parse(dispatch(agent, req.dump(), clock_type::now()));
    RA_CHECK(resp["ok"].get<bool>());
    RA_CHECK_EQ(resp["agent"]["major"].get<int>(), ra::version.major);
    RA_CHECK_EQ(resp["agent"]["minor"].get<int>(), ra::version.minor);
    RA_CHECK_EQ(resp["agent"]["patch"].get<int>(), ra::version.patch);
}

RA_TEST("Dispatch: version rejects client below min")
{
    NoopBackend backend;
    Agent agent(backend);

    json req = {
        {"cmd", "version"},
        {"client", {{"major", 0}, {"minor", 0}, {"patch", 1}}},
    };
    auto resp = json::parse(dispatch(agent, req.dump(), clock_type::now()));
    RA_CHECK(!resp["ok"].get<bool>());
    RA_CHECK(resp["reason"].get<std::string>() == "client_too_old");
}

RA_TEST("Dispatch: version rejects client above agent")
{
    NoopBackend backend;
    Agent agent(backend);

    json req = {
        {"cmd", "version"},
        {"client", {
            {"major", ra::version.major},
            {"minor", ra::version.minor},
            {"patch", ra::version.patch + 1},
        }},
    };
    auto resp = json::parse(dispatch(agent, req.dump(), clock_type::now()));
    RA_CHECK(!resp["ok"].get<bool>());
    RA_CHECK(resp["reason"].get<std::string>() == "client_too_new");
}

RA_TEST("Dispatch: apply schedules pending")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    cfg.profiles.emplace_back();
    json req = {
        {"cmd", "apply"},
        {"config", rajson::to_jobject(cfg)},
    };
    auto t0 = clock_type::now();
    auto resp = json::parse(dispatch(agent, req.dump(), t0));
    RA_CHECK(resp["ok"].get<bool>());
    RA_CHECK_EQ(resp["deferred_ms"].get<int>(), 1000);

    // The settings change has not yet reached the backend.
    RA_CHECK_EQ(backend.settings_changes, 0);
    auto s = agent.status(t0);
    RA_CHECK(s.has_pending_apply);

    // Advance past the deadline and tick.
    agent.tick(t0 + WRITE_DELAY);
    RA_CHECK_EQ(backend.settings_changes, 1);
}

RA_TEST("Dispatch: get returns active config")
{
    NoopBackend backend;
    Agent agent(backend);

    rajson::driver_config cfg;
    cfg.profiles.emplace_back();
    auto t0 = clock_type::now();
    agent.schedule_apply(cfg, t0);
    agent.tick(t0 + WRITE_DELAY);

    json req = {{"cmd", "get"}};
    auto resp = json::parse(dispatch(agent, req.dump(), clock_type::now()));
    RA_CHECK(resp["ok"].get<bool>());
    RA_CHECK(resp["config"].is_object());
    RA_CHECK(resp["config"].contains(std::string("profiles")) ||
             resp["config"].is_object());
}

RA_TEST("Dispatch: rejects unknown cmd")
{
    NoopBackend backend;
    Agent agent(backend);
    json req = {{"cmd", "nope"}};
    auto resp = json::parse(dispatch(agent, req.dump(), clock_type::now()));
    RA_CHECK(!resp["ok"].get<bool>());
}

RA_TEST("Dispatch: rejects malformed json")
{
    NoopBackend backend;
    Agent agent(backend);
    auto resp = json::parse(dispatch(agent, "{not json", clock_type::now()));
    RA_CHECK(!resp["ok"].get<bool>());
}

RA_TEST("Dispatch: status includes agent version")
{
    NoopBackend backend;
    Agent agent(backend);
    json req = {{"cmd", "status"}};
    auto resp = json::parse(dispatch(agent, req.dump(), clock_type::now()));
    RA_CHECK(resp["ok"].get<bool>());
    RA_CHECK_EQ(resp["agent"]["major"].get<int>(), ra::version.major);
}
