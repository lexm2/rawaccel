// Parity tests for the shared fixed-point pipeline (rawaccel_fixedpoint.h), the
// code the BPF program runs in-kernel. Each case drives ra_modify_q16_flat and
// compares its Q16.16 output against the double-precision common/ math
// (rawaccel::modifier::modify).

#include "rawaccel_fixedpoint.h"
#include "lut_builder.hpp"
#include "test_harness.hpp"

#include "rawaccel.hpp"

#include <cmath>
#include <cstdint>

using namespace rawaccel_agent;
namespace ra = rawaccel;

namespace {

// Oracle output for one axis (smoother halflives zeroed, dpi_factor 1, dt_ms
// slice -> ips_factor 1/dt_ms). dt_ms defaults to 1; dt-aware cases pass a slice.
double oracle_axis(const ra::modifier_settings& s,
                   double in_x, double in_y, bool want_x, double dt_ms = 1.0,
                   double dpi_factor = 1.0)
{
    ra::modifier_settings ms = s;
    ms.prof.speed_processor_args.input_speed_smooth_halflife = 0;
    ms.prof.speed_processor_args.scale_smooth_halflife = 0;
    ms.prof.speed_processor_args.output_speed_smooth_halflife = 0;
    ra::init_data(ms);

    ra::modifier mod(ms);
    ra::speed_processor sp{};
    sp.init(ms.prof.speed_processor_args);

    vec2d v{in_x, in_y};
    mod.modify(v, sp, ms, dpi_factor, dt_ms);
    return want_x ? v.x : v.y;
}

// Compare one sample against the oracle with a prebuilt LUT/config and a
// caller-chosen tolerance. dev.dpi 0 -> dpi_norm 1, velocity == raw count in/s.
void check_with(const ra::modifier_settings& s, const LutBuildResult& lut,
                const ra_bpf_config& cfg, std::int32_t dx, std::int32_t dy,
                double abs_tol, double rel_tol, double dt_ms = 1.0,
                double dpi_factor = 1.0)
{
    ra_bpf_state st{};  // fresh: smoother state and carry zero

    // Quantize dt and feed the SAME value to both sides so dt quantization isn't
    // counted as parity error. dt stays inside the clamp window, so the
    // kernel-only dt clamp is a no-op and the (unclamped) oracle agrees.
    __s32 dt_q16 = static_cast<__s32>(std::lround(dt_ms * RA_Q16_ONE));
    if (dt_q16 <= 0) dt_q16 = 1;
    double dt_exact = static_cast<double>(dt_q16) / RA_Q16_ONE;

    // __s64 (long long) not int64_t (long): match the helper's signature.
    __s64 ox_q16 = 0, oy_q16 = 0;
    ra_modify_q16_flat(&cfg, &st, lut.lut_x.data(), lut.lut_y.data(),
                       dx, dy, dt_q16, &ox_q16, &oy_q16);

    double kx = static_cast<double>(ox_q16) / RA_Q16_ONE;
    double ky = static_cast<double>(oy_q16) / RA_Q16_ONE;
    double ex = oracle_axis(s, dx, dy, true, dt_exact, dpi_factor);
    double ey = oracle_axis(s, dx, dy, false, dt_exact, dpi_factor);

    RA_CHECK_NEAR(kx, ex, abs_tol + std::fabs(ex) * rel_tol);
    RA_CHECK_NEAR(ky, ey, abs_tol + std::fabs(ey) * rel_tol);
}

// Run one sample through the pipeline; assert both components match the oracle.
void check_axis(const ra::modifier_settings& s, std::int32_t dx, std::int32_t dy)
{
    ra::device_config dev{};
    LutBuildResult lut = build_lut(s, dev);
    BpfMouseLayout layout{};  // HID fields irrelevant to the math
    ra_bpf_config cfg = to_bpf_config(lut, layout);
    check_with(s, lut, cfg, dx, dy, 1e-2, 2e-3);
}

// Sweep a dense grid of directions/magnitudes through one profile, LUT built
// once. Grid values sit off the 15-degree marks so a snap profile's tan
// threshold never lands on a sample (where fixed-point vs double could disagree).
void sweep_grid(const ra::modifier_settings& s,
                double abs_tol = 1e-2, double rel_tol = 2e-3)
{
    ra::device_config dev{};
    LutBuildResult lut = build_lut(s, dev);
    BpfMouseLayout layout{};
    ra_bpf_config cfg = to_bpf_config(lut, layout);

    static const std::int32_t vals[] = {
        -300, -128, -50, -17, -5, 0, 5, 17, 50, 128, 300};
    for (std::int32_t dx : vals)
        for (std::int32_t dy : vals)
            if (dx != 0 || dy != 0)
                check_with(s, lut, cfg, dx, dy, abs_tol, rel_tol);
}

} // namespace

RA_TEST("Fixed: ra_exp2_q16 approximates 2^x for x <= 0")
{
    // Smoother decay feeds this x <= 0
    // check the cubic against libc.
    for (double x = 0.0; x >= -30.0; x -= 0.011) {
        __s32 xq = static_cast<__s32>(std::lround(x * RA_Q16_ONE));
        double got = static_cast<double>(ra_exp2_q16(xq)) / RA_Q16_ONE;
        double want = std::exp2(x);
        RA_CHECK_NEAR(got, want, 1e-3 + want * 2e-3);
    }
    RA_CHECK_EQ(ra_exp2_q16(0), RA_Q16_ONE);                     // 2^0 = 1
    // deep underflow saturates to 0
    RA_CHECK_EQ(ra_exp2_q16(static_cast<__s32>(-60 * RA_Q16_ONE)), 0);
}

RA_TEST("Fixed: noaccel passes pure-axis input through unchanged")
{
    ra::modifier_settings s{};
    // both signs exercise the Q16.16 working vector
    for (std::int32_t v : {1, 5, 20, 100, 800}) {
        check_axis(s, v, 0);
        check_axis(s, 0, v);
        check_axis(s, -v, 0);
        check_axis(s, 0, -v);
    }
}

RA_TEST("Fixed: directional output DPI scales only the negative direction")
{
    ra::modifier_settings s{};
    s.prof.lr_output_dpi_ratio = 1.25;  // X, leftward (output < 0)
    s.prof.ud_output_dpi_ratio = 0.8;   // Y, downward (output < 0)

    for (std::int32_t v : {10, 50, 300}) {
        // positive untouched, negative scaled
        check_axis(s, v, 0);
        check_axis(s, -v, 0);
        check_axis(s, 0, v);
        check_axis(s, 0, -v);
    }
}

RA_TEST("Fixed: output_dpi 2000 doubles both axes")
{
    ra::modifier_settings s{};
    s.prof.output_dpi = 2000;
    check_axis(s, 50, 0);
    check_axis(s, 0, 50);
}

RA_TEST("Fixed: rotation matches the oracle (noaccel, speed-independent)")
{
    // noaccel scale is 1 at every speed, so rotation parity holds for any input.
    for (double deg : {15.0, 45.0, -30.0, 90.0}) {
        ra::modifier_settings s{};
        s.prof.degrees_rotation = deg;
        check_axis(s, 100, 0);
        check_axis(s, 0, 100);
        check_axis(s, 60, 80);
        check_axis(s, -50, 40);
    }
}

RA_TEST("Fixed: classic curve matches the oracle on each axis")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;

    for (std::int32_t v : {1, 3, 8, 21, 55, 144, 377}) {
        check_axis(s, v, 0);
        check_axis(s, 0, v);
    }
}

RA_TEST("Fixed: separate mode applies asymmetric range_weights per axis")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.range_weights = vec2d{1.0, 0.5};
    // separate mode: per-axis weight, no directional blend
    s.prof.speed_processor_args.whole = false;

    for (std::int32_t v : {5, 20, 100, 400}) {
        check_axis(s, v, 0);
        check_axis(s, 0, v);
        check_axis(s, v, v);
    }
}

RA_TEST("Fixed: whole euclidean curve matches oracle on diagonals")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;  // whole mode uses accel_x for both
    // default speed_processor_args: lp_norm 2 -> euclidean

    check_axis(s, 30, 40);     // |v| = 50
    check_axis(s, 60, 80);     // |v| = 100
    check_axis(s, -90, 120);   // |v| = 150
    check_axis(s, 200, 200);
}

RA_TEST("Fixed: whole-mode directional weighting blends range_weights by angle")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;       // whole mode uses accel_x for both
    s.prof.range_weights = vec2d{0.5, 1.5};  // asymmetric -> angular blend
    // default speed_processor_args: whole, euclidean

    // reference angle blends range_w_x (horizontal) to range_w_y (vertical)
    // the in-kernel atan must track the oracle's.
    check_axis(s, 100, 0);    // 0 deg   -> weight 0.5
    check_axis(s, 0, 100);    // 90 deg  -> weight 1.5
    check_axis(s, 100, 100);  // 45 deg  -> weight 1.0
    check_axis(s, 150, 50);   // shallow
    check_axis(s, 50, 150);   // steep
    check_axis(s, -120, 90);  // negative quadrant, angle sign-independent
    check_axis(s, 200, 200);
}

RA_TEST("Fixed: separate mode curve matches oracle on diagonals")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y.mode = ra::accel_mode::classic;
    s.prof.accel_y.acceleration = 0.02;   // different Y
    s.prof.accel_y.exponent_classic = 2.0;
    s.prof.speed_processor_args.whole = false;

    check_axis(s, 30, 40);
    check_axis(s, 60, 80);
    check_axis(s, -90, 120);
}

RA_TEST("Fixed: speed clamp matches oracle (noaccel, isolates the clamp)")
{
    ra::modifier_settings s{};
    s.prof.speed_min = 10.0;
    s.prof.speed_max = 50.0;
    // dpi_norm 1 (dev.dpi 0) so count == in/s
    check_axis(s, 5, 0);     // speed 5 -> boosted to 10
    check_axis(s, 30, 0);    // in range
    check_axis(s, 100, 0);   // capped to 50
    check_axis(s, 0, -120);  // capped to 50 on Y
    check_axis(s, 30, 40);   // |v| 50, at the cap
    check_axis(s, 60, 80);   // |v| 100 -> capped to 50
}

RA_TEST("Fixed: speed clamp composes with a curve")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.speed_max = 80.0;  // cap
    // speed_min stays 0

    check_axis(s, 40, 0);
    check_axis(s, 200, 0);   // clamped to 80 before the curve
    check_axis(s, 90, 120);  // |v| 150 -> clamped to 80
}

// ---- P2.1: real per-packet dt ------------------------------------------
//
// The kernel folds 1/dt into the velocity used for LUT indexing and the speed
// clamp, so the same displacement over a different polling interval lands at a
// different speed. dt stays inside the clamp window so the dt clamp is inert.

RA_TEST("Fixed: real dt scales the curve-input speed (poll-rate independence)")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;

    ra::device_config dev{};
    LutBuildResult lut = build_lut(s, dev);
    BpfMouseLayout layout{};
    ra_bpf_config cfg = to_bpf_config(lut, layout);

    // 8 kHz .. 125 Hz
    // magnitudes keep 1/dt * |v| inside the LUT span.
    for (double dt : {0.125, 0.5, 1.0, 2.0, 4.0, 8.0}) {
        for (std::int32_t v : {5, 20, 80, 200}) {
            check_with(s, lut, cfg, v, 0, 1e-2, 2e-3, dt);
            check_with(s, lut, cfg, 0, v, 1e-2, 2e-3, dt);
            check_with(s, lut, cfg, v, v, 1e-2, 3e-3, dt);
        }
    }
}

RA_TEST("Fixed: noaccel output is dt-invariant (scale is 1 at every speed)")
{
    // noaccel scale is 1 regardless of speed, so dt changes nothing.
    ra::modifier_settings s{};
    ra::device_config dev{};
    LutBuildResult lut = build_lut(s, dev);
    BpfMouseLayout layout{};
    ra_bpf_config cfg = to_bpf_config(lut, layout);

    for (double dt : {0.125, 1.0, 5.0, 50.0})
        for (std::int32_t v : {7, 33, 150})
            check_with(s, lut, cfg, v, -v, 1e-2, 2e-3, dt);
}

RA_TEST("Fixed: speed clamp tracks the dt-scaled velocity")
{
    // Clamp threshold is in/s, so the same displacement clamps differently per
    // poll rate: a slow slice lowers the IPS below the cap.
    ra::modifier_settings s{};
    s.prof.speed_min = 10.0;
    s.prof.speed_max = 50.0;

    ra::device_config dev{};
    LutBuildResult lut = build_lut(s, dev);
    BpfMouseLayout layout{};
    ra_bpf_config cfg = to_bpf_config(lut, layout);

    for (double dt : {0.25, 1.0, 3.0}) {
        check_with(s, lut, cfg, 30, 0, 1e-2, 2e-3, dt);
        check_with(s, lut, cfg, 100, 0, 1e-2, 2e-3, dt);
        check_with(s, lut, cfg, 30, 40, 1e-2, 2e-3, dt);
    }
}

RA_TEST("Fixed: device DPI scales the output to match Windows (dpi_factor)")
{
    // Windows scales output by output_dpi_adjustment_factor * dpi_factor
    // (rawaccel.hpp:412), dpi_factor = NORMALIZED_DPI/device_dpi. The agent folds
    // dpi_factor into output_dpi_adj_q16. dev.dpi != 0 here (other tests use 0 ->
    // dpi_factor 1, never exercising this multiply).
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.output_dpi = 1500;  // output_dpi_adjustment_factor 1.5

    BpfMouseLayout layout{};
    // dpi_factor = 1000/dpi, all exact in Q16.16: 0.625, 1.25, 2.0.
    for (int dpi : {1600, 800, 500}) {
        ra::device_config dev{};
        dev.dpi = dpi;
        double dpi_factor = ra::NORMALIZED_DPI / static_cast<double>(dpi);
        LutBuildResult lut = build_lut(s, dev);
        ra_bpf_config cfg = to_bpf_config(lut, layout);

        // speed domain folds dpi_factor/dt, output folds dpi_factor (no dt)
        // both must compose.
        for (double dt : {0.5, 1.0, 2.0}) {
            check_with(s, lut, cfg, 40, 0, 1e-2, 3e-3, dt, dpi_factor);
            check_with(s, lut, cfg, 0, 90, 1e-2, 3e-3, dt, dpi_factor);
            check_with(s, lut, cfg, 60, 80, 1e-2, 3e-3, dt, dpi_factor);
        }
    }
}

RA_TEST("Fixed: angle snapping collapses near-axis input onto the axis")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.degrees_snap = 15.0;  // snap within 15 deg of either axis

    // within 15 deg of X (atan(20/150)=7.6): collapse to pure X
    check_axis(s, 150, 20);
    check_axis(s, -150, 20);
    // within 15 deg of Y: collapse to pure Y
    check_axis(s, 20, 150);
    check_axis(s, 20, -150);
    // 45 deg: untouched
    check_axis(s, 100, 100);
    // atan(50/150)=18.4 > 15: not snapped
    check_axis(s, 150, 50);
    // pure-axis: no-op
    check_axis(s, 200, 0);
    check_axis(s, 0, 200);
}

RA_TEST("Fixed: angle snapping composes with directional weighting")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.degrees_snap = 15.0;
    s.prof.range_weights = vec2d{0.5, 1.5};  // whole-mode angular blend

    // snapped to X -> angle 0 -> weight 0.5
    check_axis(s, 150, 20);
    // snapped to Y -> angle pi/2 -> weight 1.5
    check_axis(s, 20, 150);
    // unsnapped diagonal -> blended weight
    check_axis(s, 150, 80);
}

// ---- P1.7: consolidated grid parity --------------------------------------
//
// Per-feature tests above pin each transform; these sweep a dense grid across
// whole profiles to catch composition bugs (sign, quadrant symmetry,
// magnitude/angle interplay) that hand-picked points could miss.

RA_TEST("Grid: noaccel is identity across every direction")
{
    ra::modifier_settings s{};
    sweep_grid(s);
}

RA_TEST("Grid: whole euclidean classic matches oracle across directions")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;  // whole mode uses accel_x for both
    sweep_grid(s);
}

RA_TEST("Grid: separate-mode asymmetric curves match oracle across directions")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y.mode = ra::accel_mode::classic;
    s.prof.accel_y.acceleration = 0.02;
    s.prof.accel_y.exponent_classic = 2.0;
    s.prof.speed_processor_args.whole = false;
    sweep_grid(s);
}

RA_TEST("Grid: max distance mode matches oracle across directions")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.speed_processor_args.lp_norm = 16.0;  // >= MAX_NORM -> max
    sweep_grid(s);
}

RA_TEST("Grid: rotation + classic matches oracle across directions")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.degrees_rotation = 23.0;
    sweep_grid(s);
}

RA_TEST("Grid: speed clamp + classic matches oracle across directions")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.speed_min = 20.0;
    s.prof.speed_max = 200.0;
    sweep_grid(s);
}

RA_TEST("Grid: directional weighting matches oracle across directions")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.range_weights = vec2d{0.6, 1.4};  // whole-mode angular blend
    // atan is a polynomial fit (< 0.0015 rad)
    // wider band than curve-only grids
    sweep_grid(s, 1e-2, 5e-3);
}

RA_TEST("Grid: directional output DPI matches oracle across directions")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.lr_output_dpi_ratio = 1.3;  // X when output < 0
    s.prof.ud_output_dpi_ratio = 0.7;  // Y when output < 0
    sweep_grid(s);
}

RA_TEST("Fixed: max distance mode matches oracle on diagonals")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    // lp_norm >= MAX_NORM -> distance_mode::max
    s.prof.speed_processor_args.lp_norm = 16.0;

    check_axis(s, 30, 40);
    check_axis(s, 80, 60);
    check_axis(s, -120, 90);
}

RA_TEST("Fixed: yx_output_dpi_ratio scales the Y axis")
{
    ra::modifier_settings s{};
    s.prof.yx_output_dpi_ratio = 1.5;
    for (std::int32_t v : {10, 50, 250}) {
        check_axis(s, v, 0);
        check_axis(s, 0, v);
    }
}
