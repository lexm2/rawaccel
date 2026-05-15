#include "evdev_processor.hpp"

#include <cmath>

namespace rawaccel_agent {

bool valid_carry(double x, double y)
{
    // Mirror of ValidCarry in driver/driver.cpp:37-42 (precise FP). NaN-safe:
    // any comparison with NaN is false, so a NaN-poisoned carry is rejected
    // and the output of that packet is dropped, just like on Windows.
    return std::fabs(x) < 1 && std::fabs(y) < 1;
}

EvdevProcessor::EvdevProcessor() : mod_(settings_)
{
    speed_.init(settings_.prof.speed_processor_args);
}

void EvdevProcessor::set_settings(const ra::modifier_settings& s)
{
    settings_ = s;
    ra::init_data(settings_);
    mod_ = ra::modifier(settings_);
    speed_.init(settings_.prof.speed_processor_args);
    // Smoother state lives inside speed_processor; init() resets it. The
    // carry survives a profile swap on purpose: in Windows the carry is
    // owned by DEVICE_EXTENSION, not by mod_settings, so a settings write
    // does not zero it. Match that behavior.
}

void EvdevProcessor::set_dpi_factor(double f)
{
    dpi_factor_ = f;
}

void EvdevProcessor::set_time_clamp(ra::time_clamp c)
{
    clamp_ = c;
}

ProcessedDelta EvdevProcessor::process(std::int32_t dx, std::int32_t dy,
                                       ra::milliseconds time_ms)
{
    // Driver semantics: zero-input packets are passed through without state
    // changes and without emitting a transformed packet. See
    // driver/driver.cpp:107 ("if (it->LastX || it->LastY)").
    if (dx == 0 && dy == 0) {
        return ProcessedDelta{0, 0, false};
    }

    const ra::milliseconds t = ra::clampsd(time_ms, clamp_.min, clamp_.max);

    vec2d in{static_cast<double>(dx), static_cast<double>(dy)};
    mod_.modify(in, speed_, settings_, dpi_factor_, t);

    const double carried_x = in.x + carry_.x;
    const double carried_y = in.y + carry_.y;

    const auto out_x = static_cast<std::int32_t>(carried_x);
    const auto out_y = static_cast<std::int32_t>(carried_y);

    const double new_carry_x = carried_x - out_x;
    const double new_carry_y = carried_y - out_y;

    if (!valid_carry(new_carry_x, new_carry_y)) {
        // Match driver/driver.cpp:124-129: when the carry would be poisoned
        // (NaN, or magnitude >= 1 from a pathological modifier), drop the
        // packet entirely and leave carry/out_event untouched. The kernel
        // path neither updates carry nor writes back LastX/LastY in this
        // case; we mirror that with emit=false.
        return ProcessedDelta{0, 0, false};
    }

    carry_.x = new_carry_x;
    carry_.y = new_carry_y;
    return ProcessedDelta{out_x, out_y, true};
}

void EvdevProcessor::reset()
{
    carry_ = {0.0, 0.0};
    speed_.init(settings_.prof.speed_processor_args);
}

} // namespace rawaccel_agent
