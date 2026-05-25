// ControlServer wire framing: real AF_UNIX socket on a temp path, server in
// a thread, length-prefixed JSON frame sent from the test thread.

#include "agent.hpp"
#include "backend.hpp"
#include "control_server.hpp"
#include "test_harness.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

using namespace rawaccel_agent;
namespace ra = rawaccel;
using json = nlohmann::json;

namespace {

std::string make_tmp_socket_path()
{
    char buf[] = "/tmp/rawaccel-agent-test-XXXXXX";
    int fd = ::mkstemp(buf);
    if (fd < 0) return {};
    ::close(fd);
    ::unlink(buf);  // want the path, not the file
    return std::string(buf);
}

int connect_unix(const std::string& path)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

} // namespace

RA_TEST("Socket: status roundtrip over AF_UNIX frame")
{
    auto path = make_tmp_socket_path();
    RA_CHECK(!path.empty());

    NoopBackend backend;
    Agent agent(backend);
    ControlServer server(agent, path);
    RA_CHECK(server.listen());

    std::thread t([&]{ server.run(std::chrono::milliseconds(20)); });

    // retry connect: listener may not be in poll() yet
    int fd = -1;
    for (int i = 0; i < 50 && fd < 0; ++i) {
        fd = connect_unix(path);
        if (fd < 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    RA_CHECK(fd >= 0);

    json req = {{"cmd", "status"}};
    auto payload = req.dump();
    RA_CHECK(write_frame(fd, payload));

    std::string resp_buf;
    RA_CHECK(read_frame(fd, resp_buf));
    auto resp = json::parse(resp_buf);
    RA_CHECK(resp["ok"].get<bool>());
    RA_CHECK_EQ(resp["agent"]["major"].get<int>(), ra::version.major);

    ::close(fd);
    server.stop();
    t.join();
}

RA_TEST("Socket: apply then status then get")
{
    auto path = make_tmp_socket_path();
    RA_CHECK(!path.empty());

    NoopBackend backend;
    Agent agent(backend);

    DeviceInfo info;
    info.id = 1;
    info.sysname = "hidraw0";
    info.device_sysname = "0003:046D:C54D.000A";
    agent.on_device_added(info);

    ControlServer server(agent, path);
    RA_CHECK(server.listen());

    std::thread t([&]{ server.run(std::chrono::milliseconds(20)); });

    auto send = [&](const json& req) -> json {
        int fd = -1;
        for (int i = 0; i < 50 && fd < 0; ++i) {
            fd = connect_unix(path);
            if (fd < 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (fd < 0) return json{};
        auto payload = req.dump();
        write_frame(fd, payload);
        std::string resp_buf;
        read_frame(fd, resp_buf);
        ::close(fd);
        return json::parse(resp_buf);
    };

    rajson::driver_config cfg;
    cfg.profiles.emplace_back();

    auto r_apply = send({{"cmd", "apply"}, {"config", rajson::to_jobject(cfg)}});
    RA_CHECK(r_apply["ok"].get<bool>());

    auto r_status_before = send({{"cmd", "status"}});
    RA_CHECK(r_status_before["has_pending_apply"].get<bool>());

    // past the debounce
    std::this_thread::sleep_for(WRITE_DELAY + std::chrono::milliseconds(150));

    auto r_status_after = send({{"cmd", "status"}});
    RA_CHECK(!r_status_after["has_pending_apply"].get<bool>());
    RA_CHECK(r_status_after["has_active_config"].get<bool>());
    RA_CHECK_EQ(backend.binds, 1);

    auto r_get = send({{"cmd", "get"}});
    RA_CHECK(r_get["ok"].get<bool>());
    RA_CHECK(r_get["config"]["profiles"].is_array());
    RA_CHECK_EQ(static_cast<int>(r_get["config"]["profiles"].size()), 1);

    server.stop();
    t.join();
}
