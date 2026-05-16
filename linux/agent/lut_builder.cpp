#include "lut_builder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rawaccel_agent {

namespace {

std::int32_t q16_round(double d)
{
    constexpr double kMax = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    constexpr double kMin = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    double v = d * static_cast<double>(RA_Q16_ONE);
    if (v >= kMax) return std::numeric_limits<std::int32_t>::max();
    if (v <= kMin) return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(std::lround(v));
}

// time=1 and dpi_factor=1 -> modifier's ips_factor = 1, so the speed seen
// by the curve equals v. Sample at a tiny epsilon at v=0 to capture the
// v->0+ limit (not 1.0 for output_dpi-scaled or weighted profiles).
double scale_at(const ra::modifier& mod,
                const ra::modifier_settings& stateless,
                double v, bool along_x)
{
    double sample_v = v > 0.0 ? v : 1e-9;
    vec2d in = along_x ? vec2d{sample_v, 0.0} : vec2d{0.0, sample_v};
    ra::speed_processor sp{};
    sp.init(stateless.prof.speed_processor_args);
    mod.modify(in, sp, stateless, 1.0, 1.0);
    return (along_x ? in.x : in.y) / sample_v;
}

} // namespace

LutBuildResult build_lut(const ra::modifier_settings& settings,
                         const ra::device_config& dev_config)
{
    // The BPF program runs its own EMA via smooth_alpha_q16; zero the
    // userspace smoother halflives so the LUT does not double-count.
    ra::modifier_settings stateless = settings;
    stateless.prof.speed_processor_args.input_speed_smooth_halflife = 0;
    stateless.prof.speed_processor_args.scale_smooth_halflife = 0;
    stateless.prof.speed_processor_args.output_speed_smooth_halflife = 0;
    ra::init_data(stateless);
    ra::modifier mod(stateless);

    LutBuildResult out{};
    out.lut_step_q16 = RA_Q16_ONE;
    out.lut_max_q16  = static_cast<std::int32_t>(
        static_cast<std::int64_t>(RA_LUT_SIZE) * RA_Q16_ONE - 1);

    // dpi=0 -> the BPF program treats raw counts as already normalized.
    if (dev_config.dpi > 0) {
        out.dpi_norm_q16 = q16_round(
            ra::NORMALIZED_DPI / static_cast<double>(dev_config.dpi));
    } else {
        out.dpi_norm_q16 = RA_Q16_ONE;
    }

    // Derived assuming a 1 ms packet; exact at 1 kHz polling, approximate
    // otherwise. halflife=0 -> alpha=1.0 (no smoothing).
    double halflife = settings.prof.speed_processor_args.input_speed_smooth_halflife;
    if (halflife > 0.0) {
        double alpha = 1.0 - std::pow(0.5, 1.0 / halflife);
        if (alpha < 0.0) alpha = 0.0;
        if (alpha > 1.0) alpha = 1.0;
        out.smooth_alpha_q16 = q16_round(alpha);
    } else {
        out.smooth_alpha_q16 = RA_Q16_ONE;
    }

    for (int i = 0; i < RA_LUT_SIZE; ++i) {
        double v = static_cast<double>(i);
        out.lut_x[i] = q16_round(scale_at(mod, stateless, v, true));
        out.lut_y[i] = q16_round(scale_at(mod, stateless, v, false));
    }
    return out;
}

} // namespace rawaccel_agent
