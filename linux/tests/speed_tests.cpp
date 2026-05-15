// Native C++ port of wrapper-tests/SpeedTests.cs.
//
// The C# tests cross-validate ra::speed_processor smoothers against a
// reimplemented EMA written in C#. To keep the parity test *meaningful*
// after porting, the reference EMAs below are intentionally written from
// scratch here: they must NOT delegate to common/, or the test would be
// tautological.

#include "test_harness.hpp"

#include "rawaccel.hpp"

#include <cmath>

namespace {

// Independent reference: simple exponential moving average.
// Mirrors wrapper-tests/SpeedTests.cs:224-254 (SimpleExponentialSmoother).
struct ref_simple_ema {
    double window_coeff;
    double cutoff_coeff;
    double window_total = 0;
    double cutoff_total = 0;

    explicit ref_simple_ema(double half_life)
        : window_coeff(std::pow(0.5, 1.0 / half_life)),
          cutoff_coeff(1.0 - std::sqrt(1.0 - window_coeff)) {}

    double smooth(double speed, double dt)
    {
        const double aw = 1.0 - std::pow(window_coeff, dt);
        window_total += aw * (speed - window_total);
        const double ac = 1.0 - std::pow(cutoff_coeff, dt);
        cutoff_total += ac * (speed - cutoff_total);
        return std::fmin(window_total, cutoff_total);
    }
};

// Independent reference: linear-trend EMA.
// Mirrors wrapper-tests/SpeedTests.cs:256-318 (LinearExponentialSmoother).
struct ref_linear_ema {
    double window_coeff;
    double window_trend_coeff;
    double cutoff_coeff;
    double cutoff_trend_coeff;
    double window_total = 0;
    double window_trend = 0;
    double cutoff_total = 0;
    double cutoff_trend = 0;
    static constexpr double trend_damp = 0.75;

    ref_linear_ema(double half_life, double trend_half_life)
        : window_coeff(std::pow(0.5, 1.0 / half_life)),
          window_trend_coeff(std::pow(0.5, 1.0 / trend_half_life)),
          cutoff_coeff(1.0 - std::sqrt(1.0 - window_coeff)),
          cutoff_trend_coeff(1.0 - std::sqrt(1.0 - window_trend_coeff)) {}

    double smooth(double speed, double dt)
    {
        const double old_w = window_total;
        window_total += trend_damp * (window_trend * dt);
        const double aw = 1.0 - std::pow(window_coeff, dt);
        window_total += aw * (speed - window_total);
        window_total = std::fmax(window_total, 0.0);
        const double awt = 1.0 - std::pow(window_trend_coeff, dt);
        window_trend *= trend_damp;
        window_trend += awt * ((window_total - old_w) / dt - window_trend);

        const double old_c = cutoff_total;
        cutoff_total += trend_damp * (cutoff_trend * dt);
        const double ac = 1.0 - std::pow(cutoff_coeff, dt);
        cutoff_total += ac * (speed - cutoff_total);
        cutoff_total = std::fmax(cutoff_total, 0.0);
        const double act = 1.0 - std::pow(cutoff_trend_coeff, dt);
        cutoff_trend *= trend_damp;
        cutoff_trend += act * ((cutoff_total - old_c) / dt - cutoff_trend);

        return std::fmin(window_total, cutoff_total);
    }
};

double mag(double x, double y) { return std::sqrt(x * x + y * y); }

} // namespace

RA_TEST("Speed: zero vector returns zero")
{
    rawaccel::speed_processor sp{};
    vec2d in{0, 0};
    RA_CHECK_EQ(sp.calc_speed_whole(in, 1.0), 0.0);
}

RA_TEST("Speed: default calculator returns unsmoothed magnitude")
{
    rawaccel::speed_processor sp{};

    const struct { double x, y; } inputs[] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
    const double dt = 1.0;
    double speed = 0;
    for (auto& p : inputs) {
        vec2d v{p.x, p.y};
        speed = sp.calc_speed_whole(v, dt);
    }

    const double expected = mag(inputs[3].x, inputs[3].y) / dt;
    RA_CHECK_NEAR(speed, expected, 1e-4);
}

RA_TEST("Speed: scale smoother matches reference simple-EMA")
{
    constexpr double half_life = 50.0;
    constexpr double dt = 1.0;

    rawaccel::speed_args args{};
    args.lp_norm = 2;
    args.scale_smooth_halflife = half_life;
    rawaccel::speed_processor sp{};
    sp.init(args);

    ref_simple_ema ref{half_life};

    const double inputs[] = {0, 1, 2, 3};
    double actual = 0;
    double expected = 0;
    for (double in : inputs) {
        actual = sp.smoother_x.scale_smoother.smooth(in, dt);
        expected = ref.smooth(in / dt, dt);
    }
    RA_CHECK_NEAR(actual, expected, 1e-4);
}

RA_TEST("Speed: input smoother matches reference linear-EMA")
{
    constexpr double half_life = 50.0;
    constexpr double trend_half_life = 1.25; // ra::speed_processor::input_trend_halflife
    constexpr double dt = 1.0;

    rawaccel::speed_args args{};
    args.lp_norm = 2;
    args.input_speed_smooth_halflife = half_life;
    rawaccel::speed_processor sp{};
    sp.init(args);

    ref_linear_ema ref{half_life, trend_half_life};

    const struct { double x, y; } inputs[] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
    double actual = 0;
    double expected = 0;
    for (auto& p : inputs) {
        vec2d v{p.x, p.y};
        actual = sp.calc_speed_whole(v, dt);
        expected = ref.smooth(mag(p.x, p.y), dt);
    }
    RA_CHECK_NEAR(actual, expected, 1e-4);
}

RA_TEST("Speed: output smoother matches reference linear-EMA")
{
    constexpr double half_life = 50.0;
    constexpr double trend_half_life = 0.7; // ra::speed_processor::output_trend_halflife
    constexpr double dt = 1.0;

    rawaccel::speed_args args{};
    args.lp_norm = 2;
    args.output_speed_smooth_halflife = half_life;
    rawaccel::speed_processor sp{};
    sp.init(args);

    ref_linear_ema ref{half_life, trend_half_life};

    const struct { double x, y; } inputs[] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
    double actual = 0;
    double expected = 0;
    for (auto& p : inputs) {
        const double m = mag(p.x, p.y);
        actual = sp.smoother_x.output_speed_smoother.smooth(m, dt);
        expected = ref.smooth(m, dt);
    }
    RA_CHECK_NEAR(actual, expected, 1e-4);
}

RA_TEST("Speed: re-init resets smoother state")
{
    constexpr double half_life = 50.0;
    constexpr double trend_half_life = 0.7;
    constexpr double dt = 1.0;

    rawaccel::speed_args first{};
    first.lp_norm = 2;
    first.input_speed_smooth_halflife = 100;
    first.scale_smooth_halflife = 100;
    first.output_speed_smooth_halflife = half_life;

    rawaccel::speed_args second{};
    second.lp_norm = 2;
    second.output_speed_smooth_halflife = half_life;

    rawaccel::speed_processor sp{};
    sp.init(first);
    sp.init(second);

    ref_linear_ema ref{half_life, trend_half_life};

    const struct { double x, y; } inputs[] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
    double actual = 0;
    double expected = 0;
    for (auto& p : inputs) {
        const double m = mag(p.x, p.y);
        actual = sp.smoother_x.output_speed_smoother.smooth(m, dt);
        expected = ref.smooth(m, dt);
    }
    RA_CHECK_NEAR(actual, expected, 1e-4);
}
