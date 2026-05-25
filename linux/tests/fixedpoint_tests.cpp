// Parity tests for the shared fixed-point pipeline (rawaccel_fixedpoint.h),
// the exact code the BPF program runs in-kernel. Each case drives
// ra_modify_q16_flat and compares its Q16.16 output against the authoritative
// double-precision common/ math (rawaccel::modifier::modify).
//
// Phase 0 scope: the kernel keeps the max(|dx|,|dy|) speed metric and the 1 ms
// timing assumption, and applies range weights per-axis. Both match the
// oracle only for PURE-AXIS input (where magnitude == max and the whole-mode
// directional blend collapses to range_weights.x / .y), so every case here
// feeds either (dx, 0) or (0, dy). Diagonal parity arrives with the Phase 1
// distance-mode / directional-weight work.

#include "rawaccel_fixedpoint.h"
#include "lut_builder.hpp"
#include "test_harness.hpp"

#include "rawaccel.hpp"

#include <cmath>
#include <cstdint>

using namespace rawaccel_agent;
namespace ra = rawaccel;

namespace {

// Authoritative output for one axis, mirroring how build_lut prepares the
// curve (smoother halflives zeroed) and how the BPF program is exercised
// (dpi_factor 1, 1 ms slice).
double oracle_axis(const ra::modifier_settings& s,
                   double in_x, double in_y, bool want_x)
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
    mod.modify(v, sp, ms, 1.0, 1.0);
    return want_x ? v.x : v.y;
}

// Run one pure-axis sample through the fixed-point pipeline and assert both
// components match the oracle. dev.dpi stays 0 so dpi_norm is 1 and the kernel
// velocity equals the raw count in in/s, matching modify's ips_factor of 1.
void check_axis(const ra::modifier_settings& s, std::int32_t dx, std::int32_t dy)
{
    ra::device_config dev{};
    LutBuildResult lut = build_lut(s, dev);
    BpfMouseLayout layout{};  // HID fields irrelevant to the math
    ra_bpf_config cfg = to_bpf_config(lut, layout);

    ra_bpf_state st{};  // fresh: smoothed_v and carry both zero

    // __s64 (long long) not int64_t (long): match the helper's signature.
    __s64 ox_q16 = 0, oy_q16 = 0;
    ra_modify_q16_flat(&cfg, &st, lut.lut_x.data(), lut.lut_y.data(),
                       dx, dy, &ox_q16, &oy_q16);

    double kx = static_cast<double>(ox_q16) / RA_Q16_ONE;
    double ky = static_cast<double>(oy_q16) / RA_Q16_ONE;
    double ex = oracle_axis(s, dx, dy, true);
    double ey = oracle_axis(s, dx, dy, false);

    // Tolerance covers Q16.16 quantization of the LUT entries times the count.
    RA_CHECK_NEAR(kx, ex, 1e-2 + std::fabs(ex) * 2e-3);
    RA_CHECK_NEAR(ky, ey, 1e-2 + std::fabs(ey) * 2e-3);
}

} // namespace

RA_TEST("Fixed: noaccel passes pure-axis input through unchanged")
{
    ra::modifier_settings s{};
    for (std::int32_t v : {1, 5, 20, 100, 800}) {
        check_axis(s, v, 0);
        check_axis(s, 0, v);
    }
}

RA_TEST("Fixed: output_dpi 2000 doubles both axes")
{
    ra::modifier_settings s{};
    s.prof.output_dpi = 2000;
    check_axis(s, 50, 0);
    check_axis(s, 0, 50);
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

RA_TEST("Fixed: asymmetric range_weights dampen Y but not X")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.range_weights = vec2d{1.0, 0.5};

    for (std::int32_t v : {5, 20, 100, 400}) {
        check_axis(s, v, 0);
        check_axis(s, 0, v);
    }
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
