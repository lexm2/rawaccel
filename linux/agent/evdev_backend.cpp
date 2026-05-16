#include "evdev_backend.hpp"

#include "evdev_io.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>

namespace rawaccel_agent {

// ----- Panic-ungrab registry. -----

namespace {

constexpr int MAX_GRABBED = 64;
std::array<std::atomic<int>, MAX_GRABBED>& grab_table()
{
    static std::array<std::atomic<int>, MAX_GRABBED> table{};
    static bool initialized = []{
        for (auto& slot : table) slot.store(-1);
        return true;
    }();
    (void)initialized;
    return table;
}

extern "C" void panic_signal_handler(int signo)
{
    grab_registry_panic_ungrab_all();
    // Re-raise with default disposition so the process exits with the
    // expected status (e.g. shells distinguish SIGTERM from SIGINT).
    ::signal(signo, SIG_DFL);
    ::raise(signo);
}

} // namespace

void grab_registry_add(int fd)
{
    auto& table = grab_table();
    for (auto& slot : table) {
        int expected = -1;
        if (slot.compare_exchange_strong(expected, fd)) return;
    }
    // Overflow: more grabbed devices than registry slots. Log and continue;
    // panic-ungrab will still ungrab the first MAX_GRABBED.
    std::fprintf(stderr, "rawaccel: grab registry overflow (>%d devices)\n",
                 MAX_GRABBED);
}

void grab_registry_remove(int fd)
{
    auto& table = grab_table();
    for (auto& slot : table) {
        int expected = fd;
        if (slot.compare_exchange_strong(expected, -1)) return;
    }
}

void grab_registry_panic_ungrab_all()
{
    auto& table = grab_table();
    for (auto& slot : table) {
        int fd = slot.load();
        if (fd >= 0) ::ioctl(fd, EVIOCGRAB, 0);
    }
}

void install_panic_handler()
{
    struct sigaction sa{};
    sa.sa_handler = panic_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;  // single-shot; we re-raise with SIG_DFL
    ::sigaction(SIGINT,  &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    // SIGSEGV/SIGABRT also want ungrab, but a signal-handler ioctl after a
    // segfault is borderline; skip for safety. Cleaning up on segfault is a
    // best-effort thing that systemd's ExecStopPost can also catch.
}

// ----- EvdevBackend. -----

EvdevBackend::EvdevBackend() = default;

EvdevBackend::~EvdevBackend()
{
    stop();
}

bool EvdevBackend::start()
{
    if (!watcher_.start()) {
        std::fprintf(stderr, "rawaccel: failed to start udev monitor\n");
        return false;
    }
    for (const auto& node : watcher_.enumerate()) {
        attach(node);
    }
    hotplug_thread_ = std::thread(&EvdevBackend::hotplug_loop, this);
    return true;
}

void EvdevBackend::stop()
{
    if (stopping_.exchange(true)) return;

    // Unblock every reader thread by closing its source fd. read() then
    // returns 0 (EOF) and step() returns false.
    {
        std::lock_guard<std::mutex> lock(slots_mu_);
        for (auto& [id, slot] : slots_) {
            if (slot->src_fd >= 0) {
                evdev_ungrab(slot->src_fd);
                grab_registry_remove(slot->src_fd);
                ::close(slot->src_fd);
                slot->src_fd = -1;
            }
            if (slot->dev) slot->dev->stop();
        }
    }
    {
        std::lock_guard<std::mutex> lock(slots_mu_);
        for (auto& [id, slot] : slots_) {
            if (slot->reader.joinable()) slot->reader.join();
            if (slot->sink_fd >= 0) {
                uinput_destroy(slot->sink_fd);
                slot->sink_fd = -1;
            }
        }
        slots_.clear();
    }

    if (hotplug_thread_.joinable()) hotplug_thread_.join();
}

void EvdevBackend::on_settings_changed(const ra::modifier_settings& s)
{
    std::lock_guard<std::mutex> lock(slots_mu_);
    current_settings_ = s;
    for (auto& [id, slot] : slots_) {
        if (slot->dev) slot->dev->set_settings(s);
    }
}

void EvdevBackend::on_device_added(const DeviceInfo&) {}
void EvdevBackend::on_device_removed(DeviceId) {}

std::size_t EvdevBackend::active_device_count() const
{
    std::lock_guard<std::mutex> lock(slots_mu_);
    return slots_.size();
}

double EvdevBackend::current_speed() const
{
    std::lock_guard<std::mutex> lock(slots_mu_);
    double max = 0.0;
    for (const auto& kv : slots_) {
        if (!kv.second || !kv.second->dev) continue;
        double s = kv.second->dev->processor().current_speed();
        if (s > max) max = s;
    }
    return max;
}

bool EvdevBackend::attach(const UdevDevice& node)
{
    int src = evdev_open(node.devnode);
    if (src < 0) {
        std::fprintf(stderr,
            "rawaccel: open(%s) failed; fail-open passthrough\n",
            node.devnode.c_str());
        return false;
    }

    EvdevCapabilities caps;
    if (!query_capabilities(src, caps) || !caps.has_rel_x || !caps.has_rel_y) {
        // Not a relative-motion device (joystick, keyboard event node, etc.).
        ::close(src);
        return false;
    }

    if (!evdev_grab(src)) {
        std::fprintf(stderr,
            "rawaccel: EVIOCGRAB(%s) failed; fail-open passthrough\n",
            node.devnode.c_str());
        ::close(src);
        return false;
    }
    grab_registry_add(src);

    int sink = uinput_create_mirror(caps, node.name);
    if (sink < 0) {
        std::fprintf(stderr,
            "rawaccel: uinput create for %s failed; fail-open passthrough\n",
            node.devnode.c_str());
        evdev_ungrab(src);
        grab_registry_remove(src);
        ::close(src);
        return false;
    }

    auto slot = std::make_unique<Slot>();
    slot->src_fd = src;
    slot->sink_fd = sink;
    slot->id = node.devid;
    slot->syspath = node.syspath;
    slot->dev = std::make_unique<EvdevDevice>(src, sink);
    {
        std::lock_guard<std::mutex> lock(slots_mu_);
        slot->dev->set_settings(current_settings_);
        slot->reader = std::thread([d = slot->dev.get()]{ d->run(); });
        slots_.emplace(node.devid, std::move(slot));
    }
    return true;
}

void EvdevBackend::detach(DeviceId id)
{
    std::unique_ptr<Slot> slot;
    {
        std::lock_guard<std::mutex> lock(slots_mu_);
        auto it = slots_.find(id);
        if (it == slots_.end()) return;
        slot = std::move(it->second);
        slots_.erase(it);
    }
    if (slot->src_fd >= 0) {
        evdev_ungrab(slot->src_fd);
        grab_registry_remove(slot->src_fd);
        ::close(slot->src_fd);
        slot->src_fd = -1;
    }
    if (slot->dev) slot->dev->stop();
    if (slot->reader.joinable()) slot->reader.join();
    if (slot->sink_fd >= 0) {
        uinput_destroy(slot->sink_fd);
        slot->sink_fd = -1;
    }
}

void EvdevBackend::hotplug_loop()
{
    int mfd = watcher_.monitor_fd();
    while (!stopping_.load(std::memory_order_relaxed)) {
        pollfd pfd{};
        pfd.fd = mfd;
        pfd.events = POLLIN;
        int r = ::poll(&pfd, 1, 200);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) continue;
        watcher_.pump_events(
            [this](const UdevDevice& d){ attach(d); },
            [this](DeviceId id){ detach(id); });
    }
}

} // namespace rawaccel_agent
