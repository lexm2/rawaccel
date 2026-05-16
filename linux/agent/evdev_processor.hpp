#pragma once

// Per-device input transform. Pure math, no I/O. Mirrors the per-packet flow
// in driver/driver.cpp:84-131 (RawaccelCallback): apply modifier.modify(),
// add carry, truncate to int, validate carry, write back if valid.
//
// The processor is stateful: it owns the modifier_settings, the modifier
// (which carries internal modifier_flags + rot direction + accel unions),
// the speed_processor (smoother state), and the fractional carry. Reset
// settings via set_settings() to install a new profile.

#include "rawaccel.hpp"
#include "rawaccel-base.hpp"

#include <atomic>
#include <cstdint>

namespace rawaccel_agent {

namespace ra = rawaccel;

struct ProcessedDelta {
    std::int32_t x;
    std::int32_t y;
    bool emit;  // false when the input was zero or carry validation failed.
};

class EvdevProcessor {
public:
    EvdevProcessor();

    // Install a new profile. Calls ra::init_data() so the modifier_flags,
    // rot_direction and accel_union dispatch are derived once per apply
    // (matches the Windows IOCTL_WRITE path in driver/driver.cpp:213-247).
    void set_settings(const ra::modifier_settings& s);

    // Input-DPI normalization factor: NORMALIZED_DPI (1000) divided by the
    // device's reported DPI. Defaults to 1 when DPI is unknown.
    void set_dpi_factor(double f);

    // Active time clamp (defaults to ra::time_clamp{}).
    void set_time_clamp(ra::time_clamp c);

    // Process one relative-motion packet. time_ms is the inter-packet delta
    // in milliseconds (already clamped by the caller, or pass raw and let
    // the processor clamp). Returns the transformed delta. If the carry
    // validation fails or both inputs are zero, ProcessedDelta::emit is
    // false and the caller should drop the packet (matches driver behavior).
    ProcessedDelta process(std::int32_t dx, std::int32_t dy, ra::milliseconds time_ms);

    // Reset carry / smoother state. Called when a device is reattached or
    // settings change in a way that would invalidate smoother continuity.
    void reset();

    const ra::modifier_settings& settings() const { return settings_; }
    const vec2d& carry() const { return carry_; }

    // Last EMA-smoothed input speed (DPI-normalized magnitude per ms, the
    // same unit the curve sees in modify()). Updated on every process()
    // call; read by Agent::current_speed() for the UI gauge. Atomic so the
    // control thread can read while the IO thread writes.
    double current_speed() const
    {
        return current_speed_.load(std::memory_order_relaxed);
    }

private:
    ra::modifier_settings settings_{};
    ra::modifier mod_;
    ra::speed_processor speed_;
    vec2d carry_{0.0, 0.0};
    ra::time_clamp clamp_{};
    double dpi_factor_ = 1.0;
    std::atomic<double> current_speed_{0.0};
};

// Carry validity check from driver/driver.cpp:37-42. NaN-safe: returns false
// for any NaN input because both comparisons are false.
bool valid_carry(double x, double y);

} // namespace rawaccel_agent
