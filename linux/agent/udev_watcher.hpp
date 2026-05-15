#pragma once

// libudev wrapper: enumerates current mice and watches the input subsystem
// for hotplug events. Pure infrastructure; the EvdevBackend owns one of
// these and drives device lifecycle in response to callbacks.

#include "backend.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct udev;
struct udev_monitor;

namespace rawaccel_agent {

struct UdevDevice {
    std::string syspath;   // /sys/class/input/eventN
    std::string devnode;   // /dev/input/eventN
    std::string name;      // ID_MODEL or NAME hint
    std::string id;        // ID_VENDOR_ID:ID_MODEL_ID:phys path; stable per node
    DeviceId    devid;     // FNV-1a hash of `id`; opaque key for callers
};

class UdevWatcher {
public:
    UdevWatcher();
    ~UdevWatcher();
    UdevWatcher(const UdevWatcher&) = delete;
    UdevWatcher& operator=(const UdevWatcher&) = delete;

    // Open libudev and the input-subsystem monitor. Returns false on error.
    bool start();

    // File descriptor for the monitor, suitable for poll/epoll. Valid after
    // a successful start().
    int monitor_fd() const;

    // Walk the current input subsystem and return every mouse-class device.
    std::vector<UdevDevice> enumerate();

    // Drain any pending hotplug events on the monitor fd. Calls on_add for
    // each new mouse-class node and on_remove for each removal. Non-blocking.
    void pump_events(std::function<void(const UdevDevice&)> on_add,
                     std::function<void(DeviceId)> on_remove);

private:
    udev* udev_ = nullptr;
    udev_monitor* mon_ = nullptr;
};

// Compute the stable DeviceId for a node. Exposed for tests.
DeviceId compute_device_id(const std::string& stable_key);

} // namespace rawaccel_agent
