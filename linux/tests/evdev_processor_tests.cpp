// Tests for EvdevProcessor. Mirrors driver/driver.cpp:84-131 expectations:
//   - Identity profile is pass-through at all DPI factors.
//   - Carry accumulates across packets and only commits when valid.
//   - Carry validation rejects NaN.
//   - Time delta is clamped to the active time_clamp window.

#include "evdev_processor.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <limits>

using namespace rawaccel_agent;
namespace ra = rawaccel;

RA_TEST("EvdevProcessor: identity profile passes through int deltas")
{
    EvdevProcessor p;
    ra::modifier_settings s{};  // default = noaccel, DPI ratio 1.
    p.set_settings(s);

    auto a = p.process(10, 0, 1.0);
    RA_CHECK(a.emit);
    RA_CHECK_EQ(a.x, 10);
    RA_CHECK_EQ(a.y, 0);

    auto b = p.process(0, -3, 1.0);
    RA_CHECK(b.emit);
    RA_CHECK_EQ(b.x, 0);
    RA_CHECK_EQ(b.y, -3);
}

RA_TEST("EvdevProcessor: zero-input packet does not emit")
{
    EvdevProcessor p;
    ra::modifier_settings s{};
    p.set_settings(s);

    auto r = p.process(0, 0, 1.0);
    RA_CHECK(!r.emit);
}

RA_TEST("EvdevProcessor: carry accumulates and commits an extra count")
{
    EvdevProcessor p;
    ra::modifier_settings s{};
    s.prof.output_dpi = 1500;  // 1.5x scale on the identity curve.
    p.set_settings(s);

    // Each 1-count input produces 1.5 counts output. Carry walks 0.5, 1.0,
    // ... so packets emit 1, 2, 1, 2, ... summing to 1.5 * N over N inputs.
    int sum = 0;
    for (int i = 0; i < 10; ++i) {
        auto r = p.process(1, 0, 1.0);
        RA_CHECK(r.emit);
        sum += r.x;
    }
    RA_CHECK_EQ(sum, 15);  // exactly 1.5 * 10.
}

RA_TEST("EvdevProcessor: valid_carry rejects NaN")
{
    RA_CHECK(!valid_carry(std::nan(""), 0.0));
    RA_CHECK(!valid_carry(0.0, std::nan("")));
    RA_CHECK(valid_carry(0.5, -0.999));
    RA_CHECK(!valid_carry(1.0, 0.0));
    RA_CHECK(!valid_carry(0.0, -1.5));
}

RA_TEST("EvdevProcessor: time_clamp bounds the delta passed downstream")
{
    EvdevProcessor p;
    ra::modifier_settings s{};
    p.set_settings(s);

    ra::time_clamp clamp;
    clamp.min = 1.0;
    clamp.max = 5.0;
    p.set_time_clamp(clamp);

    // A 1000ms delta should not blow up smoother state. The default profile
    // has no smoother, so this is really a smoke test that the clamp call
    // does not corrupt anything; the dispatch is verified by the identity
    // pass-through.
    auto r = p.process(7, 0, 1000.0);
    RA_CHECK(r.emit);
    RA_CHECK_EQ(r.x, 7);
}

RA_TEST("EvdevProcessor: reset zeroes the carry")
{
    EvdevProcessor p;
    ra::modifier_settings s{};
    s.prof.output_dpi = 1500;
    p.set_settings(s);

    p.process(1, 0, 1.0);  // carry now 0.5.
    RA_CHECK(p.carry().x != 0.0);
    p.reset();
    RA_CHECK_EQ(p.carry().x, 0.0);
    RA_CHECK_EQ(p.carry().y, 0.0);
}
