// Tests for the curve -> LUT precomputation.
//
// Runtime LUT use:
//   v_q16 = raw_count * dpi_norm_q16
//   idx   = v_q16 / lut_step_q16
//   scale = lut_x[idx] (or lut_y[idx])
//   out   = (raw_count * scale + carry) >> 16
//
// The LUT holds the RAW per-axis curve f(speed); weighting and output-DPI are
// config fields applied in-kernel. So noaccel -> identity LUT, classic accel ->
// monotone increasing from 1.0; output_dpi/range_weights live in
// output_dpi_adj_q16 / range_w_*_q16, not the LUT (fixedpoint_tests checks the
// kernel composes them).

#include "lut_builder.hpp"
#include "test_harness.hpp"

#include "rawaccel.hpp"

#include <cmath>

using namespace rawaccel_agent;
namespace ra = rawaccel;

namespace {

bool q16_near(std::int32_t actual, double expected_real, double tol)
{
    double act_real = static_cast<double>(actual) / RA_Q16_ONE;
    return std::fabs(act_real - expected_real) <= tol;
}

} // namespace

RA_TEST("Lut: noaccel profile produces identity LUT on both axes")
{
    ra::modifier_settings s{};
    ra::device_config dev{};
    auto r = build_lut(s, dev);

    // every bucket exactly Q16_ONE on both axes
    int bad = 0;
    for (int i = 0; i < RA_LUT_SIZE; ++i) {
        if (r.lut_x[i] != RA_Q16_ONE) { ++bad; break; }
        if (r.lut_y[i] != RA_Q16_ONE) { ++bad; break; }
    }
    RA_CHECK_EQ(bad, 0);
}

RA_TEST("Lut: output_dpi 2000 leaves the raw LUT at 1.0 and sets adj to 2.0x")
{
    ra::modifier_settings s{};
    s.prof.output_dpi = 2000;  // 2x NORMALIZED_DPI (=1000)
    ra::device_config dev{};
    auto r = build_lut(s, dev);

    // output_dpi scales in-kernel via output_dpi_adj_q16, not the LUT
    for (int i = 0; i < RA_LUT_SIZE; ++i) {
        RA_CHECK_EQ(r.lut_x[i], RA_Q16_ONE);
        RA_CHECK_EQ(r.lut_y[i], RA_Q16_ONE);
    }
    RA_CHECK(q16_near(r.output_dpi_adj_q16, 2.0, 1e-9));
}

RA_TEST("Lut: classic accel is monotone increasing from 1.0")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    ra::device_config dev{};
    auto r = build_lut(s, dev);

    // bucket 0 is the v=0 limit -> 1.0
    RA_CHECK(q16_near(r.lut_x[0], 1.0, 1e-4));
    RA_CHECK(q16_near(r.lut_y[0], 1.0, 1e-4));

    // monotone non-decreasing
    for (int i = 1; i < 200; ++i) {
        RA_CHECK(r.lut_x[i] >= r.lut_x[i - 1]);
        RA_CHECK(r.lut_y[i] >= r.lut_y[i - 1]);
    }
    // large speed -> scale strictly > 1
    RA_CHECK(q16_near(r.lut_x[200], 1.0, 0.0) ? false : true);
    RA_CHECK(r.lut_x[200] > RA_Q16_ONE);
}

RA_TEST("Lut: anisotropic range_weights live in config, not the raw LUT")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    // Same curve, only the range weight differs. Weighting is in-kernel, so the
    // raw LUTs are identical and the asymmetry lives in range_w_*_q16.
    s.prof.range_weights = vec2d{1.0, 0.5};
    s.prof.speed_processor_args.whole = false;

    ra::device_config dev{};
    auto r = build_lut(s, dev);

    for (int i = 0; i < RA_LUT_SIZE; ++i) {
        RA_CHECK_EQ(r.lut_x[i], r.lut_y[i]);
    }
    RA_CHECK(q16_near(r.range_w_x_q16, 1.0, 1e-9));
    RA_CHECK(q16_near(r.range_w_y_q16, 0.5, 1e-9));
    RA_CHECK(r.range_w_x_q16 != r.range_w_y_q16);
}

RA_TEST("Lut: features not yet ported throw instead of silently approximating")
{
    auto throws = [](const ra::modifier_settings& s) {
        ra::device_config dev{};
        try { (void)build_lut(s, dev); return false; }
        catch (...) { return true; }
    };

    // Lp norm (lp_norm in (0,16), != 2, whole) still needs fixed-point pow
    // the only remaining unsupported feature.
    { ra::modifier_settings s{}; s.prof.speed_processor_args.lp_norm = 3.0;
      RA_CHECK(throws(s)); }

    // plain supported profile builds
    { ra::modifier_settings s{}; RA_CHECK(!throws(s)); }
    // angle snapping builds
    { ra::modifier_settings s{}; s.prof.degrees_snap = 5.0;
      RA_CHECK(!throws(s)); }
}

RA_TEST("Lut: angle snapping sets APPLY_SNAP and emits the threshold tangents")
{
    ra::device_config dev{};

    // no snap: flag clear, thresholds zero
    {
        ra::modifier_settings s{};
        auto r = build_lut(s, dev);
        RA_CHECK((r.flags & RA_F_APPLY_SNAP) == 0);
    }
    // 15 deg snap: flag set
    // tan(15) ~= 0.2679, tan(75) ~= 3.7321
    {
        ra::modifier_settings s{};
        s.prof.degrees_snap = 15.0;
        auto r = build_lut(s, dev);
        RA_CHECK((r.flags & RA_F_APPLY_SNAP) != 0);
        RA_CHECK(q16_near(r.snap_lo_tan_q16, std::tan(15.0 * M_PI / 180.0), 1e-4));
        RA_CHECK(q16_near(r.snap_hi_tan_q16, std::tan(75.0 * M_PI / 180.0), 1e-4));
        RA_CHECK(r.snap_lo_tan_q16 < r.snap_hi_tan_q16);
    }
}

RA_TEST("Lut: whole-mode anisotropic range weights set APPLY_DIR_WEIGHT")
{
    ra::device_config dev{};

    // whole + asymmetric weights -> directional weighting flag
    {
        ra::modifier_settings s{};
        s.prof.range_weights = vec2d{1.0, 0.5};  // whole is the default
        auto r = build_lut(s, dev);
        RA_CHECK((r.flags & RA_F_APPLY_DIR_WEIGHT) != 0);
    }
    // symmetric weights: no blend
    {
        ra::modifier_settings s{};
        auto r = build_lut(s, dev);
        RA_CHECK((r.flags & RA_F_APPLY_DIR_WEIGHT) == 0);
    }
    // separate mode never sets it (whole-mode construct)
    {
        ra::modifier_settings s{};
        s.prof.range_weights = vec2d{1.0, 0.5};
        s.prof.speed_processor_args.whole = false;
        auto r = build_lut(s, dev);
        RA_CHECK((r.flags & RA_F_APPLY_DIR_WEIGHT) == 0);
    }
}

RA_TEST("Lut: dpi_norm is NORMALIZED_DPI/dev_dpi when dpi is set")
{
    ra::modifier_settings s{};
    ra::device_config dev{};
    dev.dpi = 2000;
    auto r = build_lut(s, dev);
    // 1000/2000 = 0.5 -> 0.5 * 65536 = 32768
    RA_CHECK_EQ(r.dpi_norm_q16, 32768);
}

RA_TEST("Lut: dpi_norm defaults to 1.0 when dev_dpi is 0")
{
    ra::modifier_settings s{};
    ra::device_config dev{};  // dpi 0
    auto r = build_lut(s, dev);
    RA_CHECK_EQ(r.dpi_norm_q16, RA_Q16_ONE);
}

RA_TEST("Lut: input smoothing sets RA_F_SMOOTH_INPUT and emits log2 coefficients")
{
    ra::device_config dev{};

    // halflife 0 -> no smoothing: flag clear, coeffs 0
    {
        ra::modifier_settings s{};
        auto r = build_lut(s, dev);
        RA_CHECK((r.flags & RA_F_SMOOTH_INPUT) == 0);
        RA_CHECK_EQ(r.in_log2_win_q16, 0);
    }
    // halflife 50 ms -> flag set
    // log2(coeff) matches linear_ema_smoother init
    // with input_trend_halflife = 1.25
    {
        ra::modifier_settings s{};
        s.prof.speed_processor_args.input_speed_smooth_halflife = 50;
        auto r = build_lut(s, dev);
        RA_CHECK((r.flags & RA_F_SMOOTH_INPUT) != 0);

        double win = std::pow(0.5, 1.0 / 50.0);
        double cut = 1.0 - std::sqrt(1.0 - win);
        double trw = std::pow(0.5, 1.0 / 1.25);
        double trc = 1.0 - std::sqrt(1.0 - trw);
        RA_CHECK(q16_near(r.in_log2_win_q16, std::log2(win), 1e-3));
        RA_CHECK(q16_near(r.in_log2_cut_q16, std::log2(cut), 1e-3));
        RA_CHECK(q16_near(r.in_log2_trw_q16, std::log2(trw), 1e-3));
        RA_CHECK(q16_near(r.in_log2_trc_q16, std::log2(trc), 1e-3));
        RA_CHECK(r.in_log2_win_q16 < 0);  // coeff in (0,1)
    }
}

RA_TEST("Lut: step and max span the full quantized range")
{
    ra::modifier_settings s{};
    ra::device_config dev{};
    auto r = build_lut(s, dev);
    RA_CHECK_EQ(r.lut_step_q16, RA_Q16_ONE);
    // whole buckets: max ~= LUT_SIZE * step - 1
    RA_CHECK(r.lut_max_q16 >= (std::int32_t)(RA_LUT_SIZE - 1) * RA_Q16_ONE);
}
