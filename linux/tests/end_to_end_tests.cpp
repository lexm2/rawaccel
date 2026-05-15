// Native C++ port of wrapper-tests/EndToEndTests.cs.
// Exercises ra::modifier the same way ManagedAccel.Accelerate does
// in wrapper/wrapper.cpp:482.

#include "test_harness.hpp"

#include "rawaccel.hpp"

namespace {

vec2d accelerate(rawaccel::modifier_settings& settings,
                 rawaccel::speed_processor& sp,
                 double x, double y,
                 double dpi_factor, double time)
{
    rawaccel::modifier mod{settings};
    vec2d v{x, y};
    mod.modify(v, sp, settings, dpi_factor, time);
    return v;
}

} // namespace

RA_TEST("EndToEnd: default profile is identity at (1,1)")
{
    rawaccel::modifier_settings settings{};
    rawaccel::init_data(settings);
    rawaccel::speed_processor sp{};

    vec2d out = accelerate(settings, sp, 1.0, 1.0, 1.0, 1.0);

    RA_CHECK_EQ(out.x, 1.0);
    RA_CHECK_EQ(out.y, 1.0);
}

RA_TEST("EndToEnd: outputDPI scales output by the expected factor")
{
    constexpr double factor = 2.0;
    constexpr double normalized_dpi = 1000.0;

    rawaccel::modifier_settings settings{};
    settings.prof.output_dpi = factor * normalized_dpi;
    rawaccel::init_data(settings);
    rawaccel::speed_processor sp{};

    vec2d out = accelerate(settings, sp, 1.0, 1.0, 1.0, 1.0);

    RA_CHECK_EQ(out.x, factor * 1.0);
    RA_CHECK_EQ(out.y, factor * 1.0);
}
