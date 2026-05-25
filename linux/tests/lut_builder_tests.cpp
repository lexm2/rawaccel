// Tests for the curve -> LUT precomputation.
//
// The LUT must reflect what the BPF program will compute at runtime:
//   v_q16 = raw_count * dpi_norm_q16
//   idx   = v_q16 / lut_step_q16
//   scale = lut_x[idx] (or lut_y[idx])
//   out   = (raw_count * scale + carry) >> 16
//
// The LUT now holds the RAW per-axis curve f(speed); range/domain weighting
// and output-DPI scaling are emitted as config fields and applied in-kernel.
// So a correct LUT for a noaccel profile is the identity; for a classic accel
// profile it is monotone increasing from 1.0; output_dpi and range_weights no
// longer change the LUT (they show up in output_dpi_adj_q16 / range_w_*_q16),
// and fixedpoint_tests checks the kernel math composes them correctly.

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

    // Every bucket should round to exactly Q16_ONE for both axes.
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

    // output_dpi now scales in-kernel via output_dpi_adj_q16, not in the LUT.
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

    // Bucket 0 is the v=0 limit -> 1.0.
    RA_CHECK(q16_near(r.lut_x[0], 1.0, 1e-4));
    RA_CHECK(q16_near(r.lut_y[0], 1.0, 1e-4));

    // Monotone non-decreasing across non-trivial range.
    for (int i = 1; i < 200; ++i) {
        RA_CHECK(r.lut_x[i] >= r.lut_x[i - 1]);
        RA_CHECK(r.lut_y[i] >= r.lut_y[i - 1]);
    }
    // At a large speed the scale must be strictly > 1.
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
    // Same curve on both axes; only the range weight differs. Weighting is
    // applied in-kernel now, so the raw per-axis LUTs are identical and the
    // asymmetry lives in range_w_*_q16. (fixedpoint_tests confirms the kernel
    // math then diverges X vs Y.)
    s.prof.range_weights = vec2d{1.0, 0.5};

    ra::device_config dev{};
    auto r = build_lut(s, dev);

    for (int i = 0; i < RA_LUT_SIZE; ++i) {
        RA_CHECK_EQ(r.lut_x[i], r.lut_y[i]);
    }
    RA_CHECK(q16_near(r.range_w_x_q16, 1.0, 1e-9));
    RA_CHECK(q16_near(r.range_w_y_q16, 0.5, 1e-9));
    RA_CHECK(r.range_w_x_q16 != r.range_w_y_q16);
}

RA_TEST("Lut: dpi_norm is NORMALIZED_DPI/dev_dpi when dpi is set")
{
    ra::modifier_settings s{};
    ra::device_config dev{};
    dev.dpi = 2000;
    auto r = build_lut(s, dev);
    // 1000/2000 = 0.5 -> 0.5 * 65536 = 32768.
    RA_CHECK_EQ(r.dpi_norm_q16, 32768);
}

RA_TEST("Lut: dpi_norm defaults to 1.0 when dev_dpi is 0")
{
    ra::modifier_settings s{};
    ra::device_config dev{};  // dpi defaults to 0
    auto r = build_lut(s, dev);
    RA_CHECK_EQ(r.dpi_norm_q16, RA_Q16_ONE);
}

RA_TEST("Lut: smooth_alpha derives from input_speed_smooth_halflife")
{
    ra::modifier_settings s{};
    ra::device_config dev{};

    // halflife=0 -> no smoothing, alpha=1.0
    {
        auto r = build_lut(s, dev);
        RA_CHECK_EQ(r.smooth_alpha_q16, RA_Q16_ONE);
    }
    // halflife=50ms -> alpha = 1 - 2^(-1/50) ~= 0.01376
    {
        s.prof.speed_processor_args.input_speed_smooth_halflife = 50;
        auto r = build_lut(s, dev);
        double expected = 1.0 - std::pow(0.5, 1.0 / 50.0);
        RA_CHECK(q16_near(r.smooth_alpha_q16, expected, 1e-3));
    }
}

RA_TEST("Lut: step and max span the full quantized range")
{
    ra::modifier_settings s{};
    ra::device_config dev{};
    auto r = build_lut(s, dev);
    RA_CHECK_EQ(r.lut_step_q16, RA_Q16_ONE);
    // Whole buckets: max ~= LUT_SIZE * step - 1.
    RA_CHECK(r.lut_max_q16 >= (std::int32_t)(RA_LUT_SIZE - 1) * RA_Q16_ONE);
}
