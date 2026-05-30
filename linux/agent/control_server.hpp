#pragma once

// Length-prefixed JSON RPC over AF_UNIX SOCK_STREAM.
// Frame: [uint32 length, net order][N bytes UTF-8 JSON].
// Single-threaded; the control plane is low-volume.

#include "agent.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <sys/types.h>

namespace rawaccel_agent {

// 64 KiB: above any real driver_config, below the nlohmann::json parser-stack OOM risk.
inline constexpr std::uint32_t MAX_FRAME_BYTES = 64u * 1024u;

// Frame codec, exposed for testing.
bool read_frame(int fd, std::string& out);
bool write_frame(int fd, const std::string& payload);

// Dispatch one request JSON, return the response JSON. Socket-free for tests.
std::string dispatch(Agent& agent, const std::string& request_json,
                     time_point now);

class ControlServer {
public:
    ControlServer(Agent& agent, std::string socket_path);
    ~ControlServer();

    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // Bind the listener (removes a stale socket first). False on error; see errno.
    bool listen();

    // Accept/dispatch loop until stop(); ticks the agent every poll_interval.
    void run(std::chrono::milliseconds poll_interval =
             std::chrono::milliseconds(100));

    void stop();

    int listener_fd() const { return listener_fd_; }
    const std::string& path() const { return socket_path_; }

private:
    Agent& agent_;
    std::string socket_path_;
    int listener_fd_ = -1;
    uid_t expected_uid_ = 0;
    std::atomic<bool> stop_{false};

    bool peer_allowed(int fd) const;
    void handle_client(int fd);
};

} // namespace rawaccel_agent
