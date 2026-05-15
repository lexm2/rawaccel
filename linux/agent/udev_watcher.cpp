#include "udev_watcher.hpp"

#include <libudev.h>

#include <cstring>

namespace rawaccel_agent {

namespace {

const char* getp(udev_device* d, const char* key)
{
    auto v = udev_device_get_property_value(d, key);
    return v ? v : "";
}

bool looks_like_mouse(udev_device* d)
{
    // libudev's input-id helpers set ID_INPUT_MOUSE=1 for mice. This is the
    // same hint X11/libinput rely on; trustworthy on every udev-shipping
    // distro since ~2014. Pointing-stick/touchpad nodes also set MOUSE=1, so
    // belt and braces: require an evdev devnode (ID_INPUT) and rule out
    // touchpads explicitly. Tablet/joystick nodes won't pass.
    if (std::strcmp(getp(d, "ID_INPUT"), "1") != 0) return false;
    if (std::strcmp(getp(d, "ID_INPUT_MOUSE"), "1") != 0) return false;
    if (std::strcmp(getp(d, "ID_INPUT_TOUCHPAD"), "1") == 0) return false;
    auto node = udev_device_get_devnode(d);
    if (!node) return false;
    // Filter out /dev/input/mouseN legacy nodes; we only want eventN.
    if (std::strstr(node, "/dev/input/event") != node) return false;
    return true;
}

UdevDevice describe(udev_device* d)
{
    UdevDevice out;
    out.syspath = udev_device_get_syspath(d) ? udev_device_get_syspath(d) : "";
    out.devnode = udev_device_get_devnode(d) ? udev_device_get_devnode(d) : "";
    out.name    = getp(d, "ID_MODEL");
    if (out.name.empty()) {
        auto sysname = udev_device_get_sysname(d);
        out.name = sysname ? sysname : "";
    }
    // Stable key: prefer ID_PATH (bus topology, persists across reboots),
    // fall back to syspath. The numerical id is a hash of this.
    auto path = getp(d, "ID_PATH");
    out.id = (path[0] ? path : out.syspath);
    out.devid = compute_device_id(out.id);
    return out;
}

} // namespace

DeviceId compute_device_id(const std::string& stable_key)
{
    // FNV-1a 64-bit. Good enough for an opaque 64-bit handle; collisions on
    // realistic input keys are astronomically unlikely.
    constexpr std::uint64_t OFFSET = 1469598103934665603ull;
    constexpr std::uint64_t PRIME  = 1099511628211ull;
    std::uint64_t h = OFFSET;
    for (unsigned char c : stable_key) {
        h ^= c;
        h *= PRIME;
    }
    return h;
}

UdevWatcher::UdevWatcher() = default;

UdevWatcher::~UdevWatcher()
{
    if (mon_) udev_monitor_unref(mon_);
    if (udev_) udev_unref(udev_);
}

bool UdevWatcher::start()
{
    udev_ = udev_new();
    if (!udev_) return false;
    mon_ = udev_monitor_new_from_netlink(udev_, "udev");
    if (!mon_) return false;
    udev_monitor_filter_add_match_subsystem_devtype(mon_, "input", nullptr);
    if (udev_monitor_enable_receiving(mon_) < 0) return false;
    return true;
}

int UdevWatcher::monitor_fd() const
{
    return mon_ ? udev_monitor_get_fd(mon_) : -1;
}

std::vector<UdevDevice> UdevWatcher::enumerate()
{
    std::vector<UdevDevice> out;
    if (!udev_) return out;

    udev_enumerate* e = udev_enumerate_new(udev_);
    if (!e) return out;
    udev_enumerate_add_match_subsystem(e, "input");
    udev_enumerate_scan_devices(e);

    udev_list_entry* entry;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
        auto syspath = udev_list_entry_get_name(entry);
        auto d = udev_device_new_from_syspath(udev_, syspath);
        if (d) {
            if (looks_like_mouse(d)) out.push_back(describe(d));
            udev_device_unref(d);
        }
    }
    udev_enumerate_unref(e);
    return out;
}

void UdevWatcher::pump_events(
    std::function<void(const UdevDevice&)> on_add,
    std::function<void(DeviceId)> on_remove)
{
    if (!mon_) return;
    while (auto* d = udev_monitor_receive_device(mon_)) {
        auto action = udev_device_get_action(d);
        if (action && looks_like_mouse(d)) {
            auto info = describe(d);
            if (std::strcmp(action, "add") == 0) {
                if (on_add) on_add(info);
            } else if (std::strcmp(action, "remove") == 0) {
                if (on_remove) on_remove(info.devid);
            }
        }
        udev_device_unref(d);
    }
}

} // namespace rawaccel_agent
