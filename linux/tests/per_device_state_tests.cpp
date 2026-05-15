// Verify that per-device speed_processor state lives inside each
// EvdevProcessor instance, smoothers are wired through the processor path,
// and a settings update resets that state (mirror of
// driver/driver.cpp:402,417 calling speed_processor.init on every write).

#include "evdev_processor.hpp"
#include "test_harness.hpp"

#include "rawaccel.hpp"

#include <cmath>

using namespace rawaccel_agent;
namespace ra = rawaccel;

namespace {

// Build a profile that exercises scale smoothing on top of a classic accel
// curve. The smoothing makes the per-packet output time-dependent, so any
// drift between two processors with the same settings would surface as
// divergent outputs over a repeated input stream.
ra::modifier_settings smoothed_classic_profile()
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;

    s.prof.speed_processor_args.lp_norm = 2.0;
    s.prof.speed_processor_args.scale_smooth_halflife = 50.0;
    return s;
}

} // namespace

RA_TEST("PerDevice: two processors with same settings are independent")
{
    EvdevProcessor a, b;
    auto s = smoothed_classic_profile();
    a.set_settings(s);
    b.set_settings(s);

    // Drive only `a` through a warmup stream. `b` should remain at its
    // initial state; if speed_processor were shared, b would inherit a's
    // smoother totals.
    for (int i = 0; i < 50; ++i) {
        a.process(10, 0, 1.0);
    }

    auto a_out = a.process(10, 0, 1.0);
    auto b_out = b.process(10, 0, 1.0);

    // b's first packet must look like the very first packet for `a` did,
    // not like its 51st. Independent state => different outputs after one
    // processor has been warmed up.
    RA_CHECK(a_out.emit);
    RA_CHECK(b_out.emit);
    RA_CHECK(a_out.x != b_out.x || a_out.y != b_out.y);
}

RA_TEST("PerDevice: set_settings resets the smoother state")
{
    EvdevProcessor p;
    auto s = smoothed_classic_profile();
    p.set_settings(s);

    // Warm up.
    for (int i = 0; i < 100; ++i) {
        p.process(10, 0, 1.0);
    }
    auto warmed = p.process(10, 0, 1.0);

    // Reapply the same settings: by mirror of driver/driver.cpp:402,417 the
    // speed_processor must be reinitialized, so the next packet looks like
    // the very first packet again.
    p.set_settings(s);
    auto fresh = p.process(10, 0, 1.0);

    // First-packet output (smoother total=0) differs from steady-state
    // output (smoother total close to the warmed-up scale).
    RA_CHECK(warmed.emit);
    RA_CHECK(fresh.emit);
    RA_CHECK(warmed.x != fresh.x || warmed.y != fresh.y);
}

RA_TEST("PerDevice: constant input reaches a stable steady state")
{
    EvdevProcessor p;
    auto s = smoothed_classic_profile();
    p.set_settings(s);

    // Many packets of identical input. After long enough the smoothed
    // scale converges; the output count should stabilize within a one-count
    // band (carry oscillates by <1 per packet).
    int last_x = 0;
    for (int i = 0; i < 5000; ++i) {
        auto out = p.process(10, 0, 1.0);
        if (i == 4999) last_x = out.x;
    }

    int sample[16] = {};
    int min_x = last_x, max_x = last_x;
    for (int i = 0; i < 16; ++i) {
        auto out = p.process(10, 0, 1.0);
        sample[i] = out.x;
        if (out.x < min_x) min_x = out.x;
        if (out.x > max_x) max_x = out.x;
    }
    // Carry never grows past 1, so the steady-state output oscillates by at
    // most one count.
    RA_CHECK((max_x - min_x) <= 1);
}
