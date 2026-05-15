#pragma once

// Linux fallback backend: per-device evdev grab + uinput mirror, running the
// per-packet math from EvdevProcessor in userspace. Mirrors the Windows
// kernel filter (mouclass-above driver/driver.cpp) at the cost of slightly
// higher latency.
//
// Lifecycle:
//   start()    : open udev, enumerate, spin up a reader thread per mouse.
//   on_settings_changed() : broadcast new modifier_settings to every device.
//   stop()     : close every src fd to unblock its reader, join threads,
//                ungrab and destroy uinput mirrors.
//
// Two robustness mitigations:
//   - Panic-ungrab: a static registry of grabbed fds; install_panic_handler()
//     wires up a signal handler that walks the registry and EVIOCGRAB(0)s
//     every fd before the process exits. ioctl is the only call inside the
//     handler.
//   - Fail-open: if grabbing or opening uinput fails, the device is skipped
//     so the user's mouse keeps working through libinput as if the agent
//     were not running at all.

#include "backend.hpp"
#include "evdev_device.hpp"
#include "udev_watcher.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace rawaccel_agent {

class EvdevBackend : public Backend {
public:
    EvdevBackend();
    ~EvdevBackend() override;

    EvdevBackend(const EvdevBackend&) = delete;
    EvdevBackend& operator=(const EvdevBackend&) = delete;

    // Open udev, enumerate, spin up per-device readers. Returns true even if
    // some devices fail-open: a partial start is reported via log only.
    bool start();

    // Stop and join all reader threads, ungrab devices, destroy uinput mirrors.
    void stop();

    // Backend hooks.
    void on_settings_changed(const ra::modifier_settings& s) override;
    void on_device_added(const DeviceInfo&) override;
    void on_device_removed(DeviceId) override;

    std::size_t active_device_count() const;

private:
    struct Slot {
        std::unique_ptr<EvdevDevice> dev;
        std::thread reader;
        int src_fd = -1;
        int sink_fd = -1;
        DeviceId id = 0;
        std::string syspath;
    };

    UdevWatcher watcher_;
    mutable std::mutex slots_mu_;
    std::unordered_map<DeviceId, std::unique_ptr<Slot>> slots_;
    ra::modifier_settings current_settings_{};
    std::thread hotplug_thread_;
    std::atomic<bool> stopping_{false};

    // Try to attach to a node. Returns true if a slot was created. Logs and
    // returns false on fail-open paths.
    bool attach(const UdevDevice& node);
    void detach(DeviceId id);

    void hotplug_loop();
};

// Process-wide panic-ungrab registry. Async-signal-safe via atomics; the
// signal handler installed by install_panic_handler() walks it and ioctls
// EVIOCGRAB(0) on every registered fd.
void grab_registry_add(int fd);
void grab_registry_remove(int fd);
void grab_registry_panic_ungrab_all();
void install_panic_handler();

} // namespace rawaccel_agent
