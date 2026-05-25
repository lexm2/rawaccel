#include "lut_builder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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

// Evaluate the RAW per-axis acceleration curve f(speed) -- the bare
// accel_union, with no range_weight wrap (callback_template's
// 1 + (f-1)*weight), no domain weighting, and no output-DPI scaling. Those
// are applied in-kernel around the LUT. Sample at a tiny epsilon at v=0 to
// capture the v->0+ limit (gain curves divide by speed).
double raw_curve_at(ra::modifier_settings& s, double v, bool along_x)
{
    double sample = v > 0.0 ? v : 1e-9;
    const ra::accel_args& args = along_x ? s.prof.accel_x : s.prof.accel_y;
    ra::accel_union& u = along_x ? s.data.accel_x : s.data.accel_y;
    return u.visit([&](auto& impl) -> double { return impl(sample, args); }, args);
}

} // namespace

LutBuildResult build_lut(const ra::modifier_settings& settings,
                         const ra::device_config& dev_config)
{
    // The BPF program runs its own EMA via smooth_alpha_q16; zero the
    // userspace smoother halflives so the LUT reflects only the curve.
    ra::modifier_settings stateless = settings;
    stateless.prof.speed_processor_args.input_speed_smooth_halflife = 0;
    stateless.prof.speed_processor_args.scale_smooth_halflife = 0;
    stateless.prof.speed_processor_args.output_speed_smooth_halflife = 0;
    ra::init_data(stateless);

    const auto& prof = stateless.prof;

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

    // Weighting / output scaling, applied in-kernel around the raw curve.
    out.range_w_x_q16      = q16_round(prof.range_weights.x);
    out.range_w_y_q16      = q16_round(prof.range_weights.y);
    out.domain_w_x_q16     = q16_round(prof.domain_weights.x);
    out.domain_w_y_q16     = q16_round(prof.domain_weights.y);
    out.output_dpi_adj_q16 = q16_round(prof.output_dpi / ra::NORMALIZED_DPI);
    out.yx_ratio_q16       = q16_round(prof.yx_output_dpi_ratio);

    // flags / dist_mode are reserved for Phase 1; left at 0 for now.

    for (int i = 0; i < RA_LUT_SIZE; ++i) {
        double v = static_cast<double>(i);
        out.lut_x[i] = q16_round(raw_curve_at(stateless, v, true));
        out.lut_y[i] = q16_round(raw_curve_at(stateless, v, false));
    }
    return out;
}

ra_bpf_config to_bpf_config(const LutBuildResult& lut,
                            const BpfMouseLayout& layout)
{
    ra_bpf_config cfg{};
    std::memset(&cfg, 0, sizeof(cfg));

    cfg.report_id      = layout.report_id;
    cfg.dx_byte_offset = layout.dx_byte_offset;
    cfg.dx_byte_size   = layout.dx_byte_size;
    cfg.dy_byte_offset = layout.dy_byte_offset;
    cfg.dy_byte_size   = layout.dy_byte_size;

    cfg.dpi_norm_q16     = lut.dpi_norm_q16;
    cfg.smooth_alpha_q16 = lut.smooth_alpha_q16;
    cfg.lut_step_q16     = lut.lut_step_q16;
    cfg.lut_max_q16      = lut.lut_max_q16;

    cfg.flags          = lut.flags;
    cfg.dist_mode      = lut.dist_mode;
    cfg.config_version = RA_CONFIG_VERSION;

    cfg.range_w_x_q16     = lut.range_w_x_q16;
    cfg.range_w_y_q16     = lut.range_w_y_q16;
    cfg.domain_w_x_q16    = lut.domain_w_x_q16;
    cfg.domain_w_y_q16    = lut.domain_w_y_q16;
    cfg.output_dpi_adj_q16 = lut.output_dpi_adj_q16;
    cfg.yx_ratio_q16      = lut.yx_ratio_q16;

    return cfg;
}

} // namespace rawaccel_agent
