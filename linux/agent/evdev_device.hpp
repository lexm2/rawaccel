#pragma once

// Per-device read/transform/write loop. Takes already-opened source and sink
// file descriptors (real evdev/uinput nodes in production, pipe ends in
// tests) and runs the same per-packet pipeline as driver/driver.cpp:84-131:
//
//   1. Accumulate REL_X / REL_Y events between SYN_REPORTs.
//   2. On SYN_REPORT, run the EvdevProcessor on the accumulated dx,dy.
//   3. Emit REL_X / REL_Y (if non-zero) followed by SYN_REPORT on the sink.
//   4. Forward every other event (buttons, wheel, MSC_*) verbatim.

#include "backend.hpp"
#include "evdev_processor.hpp"

#include <linux/input.h>

#include <atomic>
#include <cstdint>
#include <mutex>

namespace rawaccel_agent {

class EvdevDevice {
public:
    EvdevDevice(int src_fd, int sink_fd);

    // Returns the average inter-SYN_REPORT delta in milliseconds, useful for
    // diagnostics. Only updated after the second SYN_REPORT.
    double last_packet_ms() const { return last_packet_ms_; }

    // Install a new profile. Safe to call from another thread.
    void set_settings(const ra::modifier_settings& s);
    void set_dpi_factor(double f);
    void set_time_clamp(ra::time_clamp c);

    // Read one event and act on it. Returns false on EOF or hard read error.
    // Public so tests can drive the loop one step at a time without spinning
    // up a thread.
    bool step();

    // Run step() until it returns false or stop() is called.
    void run();
    void stop() { stop_.store(true, std::memory_order_relaxed); }

    EvdevProcessor& processor() { return proc_; }

private:
    int src_fd_;
    int sink_fd_;

    std::mutex proc_mu_;
    EvdevProcessor proc_;

    std::int32_t pending_dx_ = 0;
    std::int32_t pending_dy_ = 0;
    timeval last_syn_time_{};
    bool have_last_syn_ = false;
    double last_packet_ms_ = 0.0;

    std::atomic<bool> stop_{false};

    void on_syn_report(const input_event& syn);
};

double timeval_diff_ms(const timeval& a, const timeval& b);

} // namespace rawaccel_agent
