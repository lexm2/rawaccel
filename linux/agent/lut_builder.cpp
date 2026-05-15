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

// Drive a stateless modifier with a single canonical-axis input at the
// requested normalized speed v; return the resulting scale on that axis.
// time_ms = 1 and dpi_factor = 1 so the modifier's internal
// ips_factor = dpi_factor / time = 1, making its speed input numerically
// equal to v -- the same axis the LUT is indexed by.
double scale_at(const ra::modifier& mod,
                const ra::modifier_settings& stateless,
                double v, bool along_x)
{
    // The modifier produces 0 output for 0 input, so scale at v == 0 is
    // mathematically undefined. Sample at a tiny positive epsilon to
    // capture the v -> 0+ limit (which for output_dpi-scaled or weighted
    // profiles is not 1.0).
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
    // Stateless copy: zero out the three smoother halflives. The BPF
    // program runs its own EMA on velocity (smooth_alpha_q16); we would
    // double-count smoothing if the LUT also reflected it.
    ra::modifier_settings stateless = settings;
    stateless.prof.speed_processor_args.input_speed_smooth_halflife = 0;
    stateless.prof.speed_processor_args.scale_smooth_halflife = 0;
    stateless.prof.speed_processor_args.output_speed_smooth_halflife = 0;
    ra::init_data(stateless);
    ra::modifier mod(stateless);

    LutBuildResult out{};

    // Speed bucket size. One LUT step == one normalized speed unit.
    out.lut_step_q16 = RA_Q16_ONE;
    out.lut_max_q16  = static_cast<std::int32_t>(
        static_cast<std::int64_t>(RA_LUT_SIZE) * RA_Q16_ONE - 1);

    // dpi_norm: NORMALIZED_DPI / dev.dpi (in Q16.16). When dev.dpi is
    // unset (0) the BPF program treats raw counts as already normalized.
    if (dev_config.dpi > 0) {
        out.dpi_norm_q16 = q16_round(
            ra::NORMALIZED_DPI / static_cast<double>(dev_config.dpi));
    } else {
        out.dpi_norm_q16 = RA_Q16_ONE;
    }

    // smooth_alpha: derive from input_speed_smooth_halflife assuming a
    // typical 1 ms packet. halflife = 0 -> alpha = 1.0 -> no smoothing.
    // This is approximate: the agent's smoother is time-aware, BPF's is
    // packet-step-aware; under a 1 kHz polling rate they line up exactly.
    double halflife = settings.prof.speed_processor_args.input_speed_smooth_halflife;
    if (halflife > 0.0) {
        double alpha = 1.0 - std::pow(0.5, 1.0 / halflife);
        if (alpha < 0.0) alpha = 0.0;
        if (alpha > 1.0) alpha = 1.0;
        out.smooth_alpha_q16 = q16_round(alpha);
    } else {
        out.smooth_alpha_q16 = RA_Q16_ONE;
    }

    // Walk the curve. Bucket 0 (v == 0) has no defined scale; pick the
    // limit-as-v->0 value (1.0) so the BPF program does the right thing
    // for the very first packet on idle.
    for (int i = 0; i < RA_LUT_SIZE; ++i) {
        double v = static_cast<double>(i);
        out.lut_x[i] = q16_round(scale_at(mod, stateless, v, /*along_x=*/true));
        out.lut_y[i] = q16_round(scale_at(mod, stateless, v, /*along_x=*/false));
    }

    return out;
}

} // namespace rawaccel_agent
