// Native C++ port of wrapper-tests/SynchronousAccelTests.cs.
//
// The reference simulator below independently implements the synchronous-curve
// math (SynchronousAccelTests.cs:80+). It MUST NOT delegate to
// common/accel-synchronous.hpp; the point is to cross-check common/.

#include "test_harness.hpp"

#include "rawaccel.hpp"

#include <cmath>

namespace {

struct sync_simulator {
    double sync_speed;
    double motivity;
    double gamma;
    double sharpness;

    sync_simulator(double s, double m, double g, double smooth)
        : sync_speed(s), motivity(m), gamma(g),
          sharpness(smooth <= 0 ? 16.0 : 0.5 / smooth) {}

    double log_space(double x) const
    {
        const double ratio = x / sync_speed;
        const double lg = std::log(ratio) / std::log(motivity);
        return gamma * lg;
    }

    double activation(double x) const
    {
        if (sharpness >= 16.0) {
            return std::min(1.0, std::max(-1.0, x));
        }
        const double sgn = (x > 0) - (x < 0);
        return sgn * std::pow(std::tanh(std::pow(std::fabs(x), sharpness)),
                              1.0 / sharpness);
    }

    double accelerate(double speed) const
    {
        return std::pow(motivity, activation(log_space(speed)));
    }
};

double drive(rawaccel::modifier_settings& settings,
             rawaccel::speed_processor& sp,
             double x_in)
{
    rawaccel::modifier mod{settings};
    vec2d v{x_in, 0.0};
    mod.modify(v, sp, settings, 1.0, 10.0);
    return v.x;
}

void run_sync_case(double smooth)
{
    constexpr double sync_speed = 20.0;
    constexpr double gamma = 0.5;
    constexpr double motivity = 1.3;

    rawaccel::modifier_settings settings{};
    settings.prof.output_dpi = 1000.0;
    settings.prof.accel_x.mode = rawaccel::accel_mode::synchronous;
    settings.prof.accel_x.gain = false;
    settings.prof.accel_x.sync_speed = sync_speed;
    settings.prof.accel_x.gamma = gamma;
    settings.prof.accel_x.motivity = motivity;
    settings.prof.accel_x.smooth = smooth;
    rawaccel::init_data(settings);

    rawaccel::speed_processor sp{};
    sync_simulator ref{sync_speed, motivity, gamma, smooth};

    const int inputs[] = {1,  1,    2,   3,   5,   8,    13,   21,   34,    55,
                          89, 144, 233, 377, 610, 987, 1597, 2584, 4181};

    for (int in : inputs) {
        const double actual = drive(settings, sp, double(in));
        const double expected = in * ref.accelerate(in / 10.0);
        RA_CHECK_NEAR(actual, expected, std::fabs(expected) * 1e-4);
    }
}

} // namespace

RA_TEST("SyncAccel: matches reference simulator (smooth=0.5)")
{
    run_sync_case(0.5);
}

RA_TEST("SyncAccel: matches reference simulator (smooth=1.0)")
{
    run_sync_case(1.0);
}
