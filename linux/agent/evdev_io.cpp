#include "evdev_io.hpp"

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>

namespace rawaccel_agent {

namespace {

bool ioread_n(int fd, void* buf, std::size_t n)
{
    auto* p = static_cast<unsigned char*>(buf);
    std::size_t got = 0;
    while (got < n) {
        ssize_t r = ::read(fd, p + got, n - got);
        if (r == 0) return false;          // EOF
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        got += static_cast<std::size_t>(r);
    }
    return true;
}

bool iowrite_n(int fd, const void* buf, std::size_t n)
{
    const auto* p = static_cast<const unsigned char*>(buf);
    std::size_t put = 0;
    while (put < n) {
        ssize_t w = ::write(fd, p + put, n - put);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        put += static_cast<std::size_t>(w);
    }
    return true;
}

bool test_bit(const unsigned long* bits, std::size_t n)
{
    return (bits[n / (sizeof(long) * 8)] >> (n % (sizeof(long) * 8))) & 1ul;
}

} // namespace

bool read_event(int fd, input_event& out)
{
    return ioread_n(fd, &out, sizeof(out));
}

bool write_event(int fd, const input_event& ev)
{
    return iowrite_n(fd, &ev, sizeof(ev));
}

bool query_capabilities(int fd, EvdevCapabilities& caps)
{
    caps = {};

    constexpr std::size_t REL_LONGS =
        (REL_MAX + sizeof(long) * 8) / (sizeof(long) * 8);
    constexpr std::size_t KEY_LONGS =
        (KEY_MAX + sizeof(long) * 8) / (sizeof(long) * 8);

    unsigned long rel_bits[REL_LONGS] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bits)), rel_bits) < 0) {
        return false;
    }
    caps.has_rel_x      = test_bit(rel_bits, REL_X);
    caps.has_rel_y      = test_bit(rel_bits, REL_Y);
    caps.has_rel_wheel  = test_bit(rel_bits, REL_WHEEL);
    caps.has_rel_hwheel = test_bit(rel_bits, REL_HWHEEL);

    unsigned long key_bits[KEY_LONGS] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0) {
        // Walk the mouse button range and a small extension for extra
        // buttons. Anything outside this range is unusual for a mouse and
        // we leave it to the source to express.
        for (int k = BTN_MISC; k <= BTN_GEAR_UP; ++k) {
            if (test_bit(key_bits, k)) caps.key_codes.push_back(k);
        }
    }

    char name[256] = {};
    if (::ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
        caps.name = name;
    }

    ::ioctl(fd, EVIOCGID, &caps.id);
    return true;
}

bool evdev_grab(int fd)
{
    return ::ioctl(fd, EVIOCGRAB, 1) == 0;
}

void evdev_ungrab(int fd)
{
    ::ioctl(fd, EVIOCGRAB, 0);
}

int uinput_create_mirror(const EvdevCapabilities& src, const std::string& name)
{
    int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;

    if (::ioctl(fd, UI_SET_EVBIT, EV_SYN) < 0) goto fail;
    if (::ioctl(fd, UI_SET_EVBIT, EV_REL) < 0) goto fail;

    if (src.has_rel_x      && ::ioctl(fd, UI_SET_RELBIT, REL_X)      < 0) goto fail;
    if (src.has_rel_y      && ::ioctl(fd, UI_SET_RELBIT, REL_Y)      < 0) goto fail;
    if (src.has_rel_wheel  && ::ioctl(fd, UI_SET_RELBIT, REL_WHEEL)  < 0) goto fail;
    if (src.has_rel_hwheel && ::ioctl(fd, UI_SET_RELBIT, REL_HWHEEL) < 0) goto fail;

    if (!src.key_codes.empty() && ::ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) {
        goto fail;
    }
    for (auto k : src.key_codes) {
        if (::ioctl(fd, UI_SET_KEYBIT, k) < 0) goto fail;
    }

    {
        uinput_setup setup{};
        setup.id = src.id;
        // Some kernels treat vendor==0 as "synthetic device" and skip quirks.
        // Keep the source id verbatim so userspace tools see the same VID/PID.
        std::string virt_name = "rawaccel virtual: " + name;
        std::strncpy(setup.name, virt_name.c_str(), sizeof(setup.name) - 1);
        if (::ioctl(fd, UI_DEV_SETUP, &setup) < 0) goto fail;
    }

    if (::ioctl(fd, UI_DEV_CREATE) < 0) goto fail;
    return fd;

fail:
    int saved = errno;
    ::close(fd);
    errno = saved;
    return -1;
}

void uinput_destroy(int fd)
{
    if (fd < 0) return;
    ::ioctl(fd, UI_DEV_DESTROY);
    ::close(fd);
}

int evdev_open(const std::string& path)
{
    return ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
}

} // namespace rawaccel_agent
