#include "control_server.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace rawaccel_agent {

using json = nlohmann::json;

namespace {

bool read_exact(int fd, void* buf, std::size_t n)
{
    auto* p = static_cast<unsigned char*>(buf);
    while (n) {
        ssize_t r = ::read(fd, p, n);
        if (r == 0) return false;
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += r;
        n -= static_cast<std::size_t>(r);
    }
    return true;
}

bool write_exact(int fd, const void* buf, std::size_t n)
{
    const auto* p = static_cast<const unsigned char*>(buf);
    while (n) {
        ssize_t w = ::write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

json error_response(const std::string& msg)
{
    return json{{"ok", false}, {"error", msg}};
}

json version_response(const ra::version_t& v)
{
    return json{{"major", v.major}, {"minor", v.minor}, {"patch", v.patch}};
}

} // namespace

bool read_frame(int fd, std::string& out)
{
    std::uint32_t len_be = 0;
    if (!read_exact(fd, &len_be, sizeof(len_be))) return false;
    std::uint32_t len = ntohl(len_be);
    if (len > MAX_FRAME_BYTES) return false;
    out.resize(len);
    if (len == 0) return true;
    return read_exact(fd, out.data(), len);
}

bool write_frame(int fd, const std::string& payload)
{
    if (payload.size() > MAX_FRAME_BYTES) return false;
    std::uint32_t len_be = htonl(static_cast<std::uint32_t>(payload.size()));
    if (!write_exact(fd, &len_be, sizeof(len_be))) return false;
    if (payload.empty()) return true;
    return write_exact(fd, payload.data(), payload.size());
}

std::string dispatch(Agent& agent, const std::string& request_json,
                     time_point now)
{
    json req;
    try {
        req = json::parse(request_json);
    } catch (const std::exception& e) {
        return error_response(std::string("parse error: ") + e.what()).dump();
    }

    if (!req.is_object() || !req.contains("cmd") || !req["cmd"].is_string()) {
        return error_response("missing cmd field").dump();
    }

    const auto cmd = req["cmd"].get<std::string>();

    if (cmd == "version") {
        if (!req.contains("client") || !req["client"].is_object()) {
            return error_response("version: client field required").dump();
        }
        const auto& c = req["client"];
        ra::version_t cv{
            c.value("major", 0),
            c.value("minor", 0),
            c.value("patch", 0),
        };
        auto vc = agent.check_version(cv);
        json resp;
        resp["agent"] = version_response(vc.agent_version);
        if (vc.status == VersionStatus::ok) {
            resp["ok"] = true;
        } else {
            resp["ok"] = false;
            resp["error"] = vc.message;
            resp["reason"] = (vc.status == VersionStatus::client_too_old)
                ? "client_too_old"
                : "client_too_new";
        }
        return resp.dump();
    }

    if (cmd == "apply") {
        if (!req.contains("config")) {
            return error_response("apply: config field required").dump();
        }
        rajson::driver_config cfg;
        try {
            cfg = rajson::from_jobject(req["config"]);
        } catch (const std::exception& e) {
            return error_response(std::string("apply: invalid config: ") +
                                  e.what()).dump();
        }
        agent.schedule_apply(cfg, now);
        json resp;
        resp["ok"] = true;
        resp["deferred_ms"] =
            std::chrono::duration_cast<std::chrono::milliseconds>(WRITE_DELAY)
                .count();
        return resp.dump();
    }

    if (cmd == "get") {
        json resp;
        resp["ok"] = true;
        resp["config"] = rajson::to_jobject(agent.get_active());
        return resp.dump();
    }

    if (cmd == "deactivate") {
        agent.deactivate();
        json resp;
        resp["ok"] = true;
        return resp.dump();
    }

    if (cmd == "stats") {
        json resp;
        resp["ok"] = true;
        resp["current_speed"] = agent.current_speed();
        return resp.dump();
    }

    if (cmd == "status") {
        auto s = agent.status(now);
        json resp;
        resp["ok"] = true;
        resp["has_active_config"] = s.has_active_config;
        resp["has_pending_apply"] = s.has_pending_apply;
        resp["until_apply_ms"] = s.until_apply.count();
        resp["last_apply_unix_ms"] = s.last_apply_unix_ms;
        resp["agent"] = version_response(ra::version);
        return resp.dump();
    }

    return error_response("unknown cmd: " + cmd).dump();
}

ControlServer::ControlServer(Agent& agent, std::string socket_path)
    : agent_(agent), socket_path_(std::move(socket_path)) {}

ControlServer::~ControlServer()
{
    stop();
    if (listener_fd_ >= 0) ::close(listener_fd_);
    if (!socket_path_.empty()) ::unlink(socket_path_.c_str());
}

bool ControlServer::listen()
{
    listener_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener_fd_ < 0) return false;

    // Best-effort: remove a stale socket file. Bind would otherwise fail with
    // EADDRINUSE. We do not unlink anything that is not a socket.
    struct stat st;
    if (::stat(socket_path_.c_str(), &st) == 0 && S_ISSOCK(st.st_mode)) {
        ::unlink(socket_path_.c_str());
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path_.size() >= sizeof(addr.sun_path)) {
        errno = ENAMETOOLONG;
        return false;
    }
    std::strncpy(addr.sun_path, socket_path_.c_str(),
                 sizeof(addr.sun_path) - 1);

    if (::bind(listener_fd_, reinterpret_cast<sockaddr*>(&addr),
               sizeof(addr)) < 0) {
        return false;
    }
    // 0660 by default; CAP_BPF service unit should set umask 0117 to restrict
    // to the rawaccel group. For local testing this is fine.
    ::chmod(socket_path_.c_str(), 0660);

    // When invoked via sudo (the dev launcher path), chown the socket back
    // to the calling user so the unprivileged GUI can connect. sudo exports
    // SUDO_UID / SUDO_GID for exactly this purpose. Production systemd unit
    // does not set these, so behavior there is unchanged (owner stays as
    // whatever User= the unit specifies).
    if (::geteuid() == 0) {
        const char* sudo_uid = std::getenv("SUDO_UID");
        const char* sudo_gid = std::getenv("SUDO_GID");
        if (sudo_uid && sudo_gid) {
            uid_t uid = static_cast<uid_t>(std::strtoul(sudo_uid, nullptr, 10));
            gid_t gid = static_cast<gid_t>(std::strtoul(sudo_gid, nullptr, 10));
            ::chown(socket_path_.c_str(), uid, gid);
        }
    }

    if (::listen(listener_fd_, 4) < 0) return false;
    return true;
}

void ControlServer::run(std::chrono::milliseconds poll_interval)
{
    while (!stop_.load(std::memory_order_relaxed)) {
        agent_.tick(clock_type::now());

        pollfd pfd{};
        pfd.fd = listener_fd_;
        pfd.events = POLLIN;
        int r = ::poll(&pfd, 1, static_cast<int>(poll_interval.count()));
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) continue;
        if (pfd.revents & POLLIN) {
            int client = ::accept4(listener_fd_, nullptr, nullptr, SOCK_CLOEXEC);
            if (client < 0) continue;
            handle_client(client);
            ::close(client);
        }
    }
}

void ControlServer::stop()
{
    stop_.store(true, std::memory_order_relaxed);
}

void ControlServer::handle_client(int fd)
{
    // One request per connection. Keeps lifecycle trivial; the CLI client
    // opens a fresh socket per command anyway.
    std::string req;
    if (!read_frame(fd, req)) return;
    auto resp = dispatch(agent_, req, clock_type::now());
    write_frame(fd, resp);
}

} // namespace rawaccel_agent
