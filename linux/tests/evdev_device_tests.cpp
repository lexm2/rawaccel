// Tests for EvdevDevice's read/transform/write loop. Uses pipe pairs to
// stand in for /dev/input/eventN (source) and /dev/uinput (sink). The kernel
// uinput device is not required.

#include "evdev_device.hpp"
#include "evdev_processor.hpp"
#include "test_harness.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#include <vector>

using namespace rawaccel_agent;
namespace ra = rawaccel;

namespace {

struct Pipe {
    int r = -1;
    int w = -1;
    Pipe() {
        int fds[2];
        if (::pipe(fds) == 0) { r = fds[0]; w = fds[1]; }
    }
    void set_read_nonblocking() {
        int flags = ::fcntl(r, F_GETFL, 0);
        ::fcntl(r, F_SETFL, flags | O_NONBLOCK);
    }
    ~Pipe() {
        if (r >= 0) ::close(r);
        if (w >= 0) ::close(w);
    }
};

input_event make_event(unsigned type, unsigned code, int value,
                       long sec = 0, long usec = 0)
{
    input_event ev{};
    ev.time.tv_sec = sec;
    ev.time.tv_usec = usec;
    ev.type = static_cast<__u16>(type);
    ev.code = static_cast<__u16>(code);
    ev.value = value;
    return ev;
}

void push(int fd, const input_event& ev)
{
    ssize_t w = ::write(fd, &ev, sizeof(ev));
    (void)w;
}

bool drain(int fd, std::vector<input_event>& out)
{
    input_event ev{};
    ssize_t r = ::read(fd, &ev, sizeof(ev));
    if (r != sizeof(ev)) return false;
    out.push_back(ev);
    return true;
}

} // namespace

RA_TEST("EvdevDevice: passes button events through unchanged")
{
    Pipe src, sink;
    sink.set_read_nonblocking();
    EvdevDevice dev(src.r, sink.w);
    ra::modifier_settings s{};
    dev.set_settings(s);

    push(src.w, make_event(EV_KEY, BTN_LEFT, 1));
    push(src.w, make_event(EV_SYN, SYN_REPORT, 0));
    push(src.w, make_event(EV_KEY, BTN_LEFT, 0));
    push(src.w, make_event(EV_SYN, SYN_REPORT, 0));

    for (int i = 0; i < 4; ++i) RA_CHECK(dev.step());

    std::vector<input_event> out;
    while (drain(sink.r, out)) {}
    RA_CHECK_EQ(static_cast<int>(out.size()), 4);
    RA_CHECK_EQ(out[0].type, EV_KEY);
    RA_CHECK_EQ(out[0].code, BTN_LEFT);
    RA_CHECK_EQ(out[0].value, 1);
    RA_CHECK_EQ(out[1].type, EV_SYN);
    RA_CHECK_EQ(out[2].value, 0);
}

RA_TEST("EvdevDevice: identity profile relays REL_X/REL_Y per SYN_REPORT")
{
    Pipe src, sink;
    sink.set_read_nonblocking();
    EvdevDevice dev(src.r, sink.w);
    ra::modifier_settings s{};
    dev.set_settings(s);

    push(src.w, make_event(EV_REL, REL_X,  4, 0, 1000));
    push(src.w, make_event(EV_REL, REL_Y, -2, 0, 1000));
    push(src.w, make_event(EV_SYN, SYN_REPORT, 0, 0, 1000));

    for (int i = 0; i < 3; ++i) RA_CHECK(dev.step());

    std::vector<input_event> out;
    while (drain(sink.r, out)) {}
    // Expected: REL_X(4), REL_Y(-2), SYN_REPORT. Order: x, y, syn.
    RA_CHECK_EQ(static_cast<int>(out.size()), 3);
    RA_CHECK_EQ(out[0].type, EV_REL); RA_CHECK_EQ(out[0].code, REL_X);
    RA_CHECK_EQ(out[0].value, 4);
    RA_CHECK_EQ(out[1].type, EV_REL); RA_CHECK_EQ(out[1].code, REL_Y);
    RA_CHECK_EQ(out[1].value, -2);
    RA_CHECK_EQ(out[2].type, EV_SYN); RA_CHECK_EQ(out[2].code, SYN_REPORT);
}

RA_TEST("EvdevDevice: REL_WHEEL passes through and does not feed processor")
{
    Pipe src, sink;
    sink.set_read_nonblocking();
    EvdevDevice dev(src.r, sink.w);
    ra::modifier_settings s{};
    s.prof.output_dpi = 2000;  // 2x scale so motion would be doubled.
    dev.set_settings(s);

    push(src.w, make_event(EV_REL, REL_WHEEL, 1));
    push(src.w, make_event(EV_SYN, SYN_REPORT, 0));

    for (int i = 0; i < 2; ++i) RA_CHECK(dev.step());

    std::vector<input_event> out;
    while (drain(sink.r, out)) {}
    RA_CHECK_EQ(static_cast<int>(out.size()), 2);
    RA_CHECK_EQ(out[0].code, REL_WHEEL);
    RA_CHECK_EQ(out[0].value, 1);
    RA_CHECK_EQ(out[1].type, EV_SYN);
}

RA_TEST("EvdevDevice: zero-motion SYN_REPORT still emits SYN to sink")
{
    Pipe src, sink;
    sink.set_read_nonblocking();
    EvdevDevice dev(src.r, sink.w);
    ra::modifier_settings s{};
    dev.set_settings(s);

    push(src.w, make_event(EV_SYN, SYN_REPORT, 0));
    RA_CHECK(dev.step());

    std::vector<input_event> out;
    while (drain(sink.r, out)) {}
    RA_CHECK_EQ(static_cast<int>(out.size()), 1);
    RA_CHECK_EQ(out[0].type, EV_SYN);
}

RA_TEST("EvdevDevice: scaled profile accumulates exact total over many syns")
{
    Pipe src, sink;
    sink.set_read_nonblocking();
    EvdevDevice dev(src.r, sink.w);
    ra::modifier_settings s{};
    s.prof.output_dpi = 1500;  // 1.5x identity.
    dev.set_settings(s);

    constexpr int N = 20;
    for (int i = 0; i < N; ++i) {
        push(src.w, make_event(EV_REL, REL_X, 1, 0, 1000 * (i + 1)));
        push(src.w, make_event(EV_SYN, SYN_REPORT, 0, 0, 1000 * (i + 1)));
    }
    for (int i = 0; i < 2 * N; ++i) RA_CHECK(dev.step());

    std::vector<input_event> out;
    while (drain(sink.r, out)) {}

    int sum_x = 0;
    int syn_count = 0;
    for (const auto& ev : out) {
        if (ev.type == EV_REL && ev.code == REL_X) sum_x += ev.value;
        if (ev.type == EV_SYN) ++syn_count;
    }
    RA_CHECK_EQ(sum_x, static_cast<int>(N * 1.5));
    RA_CHECK_EQ(syn_count, N);
}
