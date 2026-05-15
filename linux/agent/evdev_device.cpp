#include "evdev_device.hpp"

#include "evdev_io.hpp"

#include <linux/input.h>

namespace rawaccel_agent {

double timeval_diff_ms(const timeval& a, const timeval& b)
{
    // a - b in milliseconds, treating both as signed time points. Guards
    // against the (rare in practice, but real) usec wraparound by computing
    // both halves in double-precision before adding.
    double sec  = static_cast<double>(a.tv_sec)  - static_cast<double>(b.tv_sec);
    double usec = static_cast<double>(a.tv_usec) - static_cast<double>(b.tv_usec);
    return sec * 1000.0 + usec / 1000.0;
}

EvdevDevice::EvdevDevice(int src_fd, int sink_fd)
    : src_fd_(src_fd), sink_fd_(sink_fd) {}

void EvdevDevice::set_settings(const ra::modifier_settings& s)
{
    std::lock_guard<std::mutex> lock(proc_mu_);
    proc_.set_settings(s);
}

void EvdevDevice::set_dpi_factor(double f)
{
    std::lock_guard<std::mutex> lock(proc_mu_);
    proc_.set_dpi_factor(f);
}

void EvdevDevice::set_time_clamp(ra::time_clamp c)
{
    std::lock_guard<std::mutex> lock(proc_mu_);
    proc_.set_time_clamp(c);
}

bool EvdevDevice::step()
{
    input_event ev{};
    if (!read_event(src_fd_, ev)) return false;

    if (ev.type == EV_REL) {
        if (ev.code == REL_X) {
            pending_dx_ += ev.value;
            return true;
        }
        if (ev.code == REL_Y) {
            pending_dy_ += ev.value;
            return true;
        }
        // REL_WHEEL, REL_HWHEEL, REL_WHEEL_HI_RES, etc. pass through.
        return write_event(sink_fd_, ev);
    }

    if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
        on_syn_report(ev);
        return true;
    }

    // EV_KEY (buttons), EV_MSC, anything else: forward verbatim. The driver
    // only ever touches relative motion on Windows; we mirror that scope.
    return write_event(sink_fd_, ev);
}

void EvdevDevice::on_syn_report(const input_event& syn)
{
    ra::milliseconds dt = ra::DEFAULT_TIME_MIN;
    if (have_last_syn_) {
        dt = timeval_diff_ms(syn.time, last_syn_time_);
        last_packet_ms_ = dt;
    }
    last_syn_time_ = syn.time;
    have_last_syn_ = true;

    if (pending_dx_ != 0 || pending_dy_ != 0) {
        ProcessedDelta out;
        {
            std::lock_guard<std::mutex> lock(proc_mu_);
            out = proc_.process(pending_dx_, pending_dy_, dt);
        }
        if (out.emit) {
            if (out.x != 0) {
                input_event xe = syn;
                xe.type = EV_REL;
                xe.code = REL_X;
                xe.value = out.x;
                write_event(sink_fd_, xe);
            }
            if (out.y != 0) {
                input_event ye = syn;
                ye.type = EV_REL;
                ye.code = REL_Y;
                ye.value = out.y;
                write_event(sink_fd_, ye);
            }
        }
        // If !emit (zero input or carry invalid), we still need to emit
        // SYN_REPORT to flush any buttons/wheel that may have ridden in
        // this frame; the syn write follows below unconditionally.
    }

    write_event(sink_fd_, syn);

    pending_dx_ = 0;
    pending_dy_ = 0;
}

void EvdevDevice::run()
{
    while (!stop_.load(std::memory_order_relaxed)) {
        if (!step()) break;
    }
}

} // namespace rawaccel_agent
