#pragma once

// Length-prefixed JSON RPC over an AF_UNIX SOCK_STREAM listener.
// Frame: [uint32 length, network byte order][N bytes UTF-8 JSON].
// Max frame size: 16 MiB.
//
// The server is single-threaded: one client at a time. The control plane is
// low-volume (apply/get/version/status) so threading would only add bugs.

#include "agent.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace rawaccel_agent {

inline constexpr std::uint32_t MAX_FRAME_BYTES = 16u * 1024u * 1024u;

// Encode/decode helpers exposed for testing.
bool read_frame(int fd, std::string& out);
bool write_frame(int fd, const std::string& payload);

// Dispatches a single decoded request JSON against the agent and returns the
// response JSON. Pure function so tests can exercise it without a socket.
std::string dispatch(Agent& agent, const std::string& request_json,
                     time_point now);

class ControlServer {
public:
    ControlServer(Agent& agent, std::string socket_path);
    ~ControlServer();

    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // Bind the AF_UNIX listener at socket_path. Returns false on error; check
    // errno. Removes any stale socket file at the path first.
    bool listen();

    // Run the accept/dispatch loop until stop() is called. Calls agent.tick()
    // every poll_interval to drive the write-delay debounce.
    void run(std::chrono::milliseconds poll_interval =
             std::chrono::milliseconds(100));

    void stop();

    int listener_fd() const { return listener_fd_; }
    const std::string& path() const { return socket_path_; }

private:
    Agent& agent_;
    std::string socket_path_;
    int listener_fd_ = -1;
    std::atomic<bool> stop_{false};

    void handle_client(int fd);
};

} // namespace rawaccel_agent
