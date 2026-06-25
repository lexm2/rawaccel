#include "lut_builder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

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

// RAW per-axis curve f(speed): bare accel_union, no weights/DPI. v=0 sampled at epsilon (gain curves divide by speed).
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
    // zero userspace smoothing halflives so the LUT is the raw curve
    // kernel layers its own smoothers
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

    // dpi_factor = NORMALIZED_DPI/device_dpi
    // scales curve domain and output. dpi=0 -> 1.
    double dpi_factor = 1.0;
    if (dev_config.dpi > 0)
        dpi_factor = ra::NORMALIZED_DPI / static_cast<double>(dev_config.dpi);
    out.dpi_norm_q16 = q16_round(dpi_factor);

    // input_speed_smoother: log2 level+trend coeffs (trend halflife 1.25)
    // RA_F_SMOOTH_INPUT gates.
    {
        double hl = settings.prof.speed_processor_args.input_speed_smooth_halflife;
        if (hl > 0.0) {
            constexpr double kInputTrendHalflife = 1.25;
            double win = std::pow(0.5, 1.0 / hl);
            double cut = 1.0 - std::sqrt(1.0 - win);
            double trw = std::pow(0.5, 1.0 / kInputTrendHalflife);
            double trc = 1.0 - std::sqrt(1.0 - trw);
            out.in_log2_win_q16 = q16_round(std::log2(win));
            out.in_log2_cut_q16 = q16_round(std::log2(cut));
            out.in_log2_trw_q16 = q16_round(std::log2(trw));
            out.in_log2_trc_q16 = q16_round(std::log2(trc));
        }
    }

    // scale_smoother: level pair, no trend
    // RA_F_SMOOTH_SCALE gates.
    {
        double hl = settings.prof.speed_processor_args.scale_smooth_halflife;
        if (hl > 0.0) {
            double win = std::pow(0.5, 1.0 / hl);
            double cut = 1.0 - std::sqrt(1.0 - win);
            out.sc_log2_win_q16 = q16_round(std::log2(win));
            out.sc_log2_cut_q16 = q16_round(std::log2(cut));
        }
    }

    // output_speed_smoother: like input, trend halflife 0.7
    // RA_F_SMOOTH_OUTPUT gates.
    {
        double hl = settings.prof.speed_processor_args.output_speed_smooth_halflife;
        if (hl > 0.0) {
            constexpr double kOutputTrendHalflife = 0.7;
            double win = std::pow(0.5, 1.0 / hl);
            double cut = 1.0 - std::sqrt(1.0 - win);
            double trw = std::pow(0.5, 1.0 / kOutputTrendHalflife);
            double trc = 1.0 - std::sqrt(1.0 - trw);
            out.out_log2_win_q16 = q16_round(std::log2(win));
            out.out_log2_cut_q16 = q16_round(std::log2(cut));
            out.out_log2_trw_q16 = q16_round(std::log2(trw));
            out.out_log2_trc_q16 = q16_round(std::log2(trc));
        }
    }

    // Weighting / output scaling, applied in-kernel around the raw curve.
    out.range_w_x_q16      = q16_round(prof.range_weights.x);
    out.range_w_y_q16      = q16_round(prof.range_weights.y);
    out.domain_w_x_q16     = q16_round(prof.domain_weights.x);
    out.domain_w_y_q16     = q16_round(prof.domain_weights.y);
    // output_dpi_adj folds dpi_factor to match Windows output.
    out.output_dpi_adj_q16 = q16_round(prof.output_dpi / ra::NORMALIZED_DPI * dpi_factor);
    out.yx_ratio_q16       = q16_round(prof.yx_output_dpi_ratio);
    out.lr_ratio_q16       = q16_round(prof.lr_output_dpi_ratio);
    out.ud_ratio_q16       = q16_round(prof.ud_output_dpi_ratio);

    // rotation {cos, sin}, already in data.rot_direction via init_data
    out.rot_cos_q16 = q16_round(stateless.data.rot_direction.x);
    out.rot_sin_q16 = q16_round(stateless.data.rot_direction.y);
    out.speed_min_q16 = q16_round(prof.speed_min);
    out.speed_max_q16 = q16_round(prof.speed_max);

    // snap thresholds as tangents (kernel snaps with a multiply)
    // q16_round saturates to INT32_MAX on tan -> inf.
    {
        const double kPi = 3.14159265358979323846;
        double snap_rad = prof.degrees_snap * kPi / 180.0;
        out.snap_lo_tan_q16 = q16_round(std::tan(snap_rad));
        out.snap_hi_tan_q16 = q16_round(std::tan(kPi / 2.0 - snap_rad));
    }

    // dt clamp window
    // kernel clamps measured dt before folding 1/dt into velocity.
    out.time_min_q16 = q16_round(dev_config.clamp.min);
    out.time_max_q16 = q16_round(dev_config.clamp.max);

    // flags fire only when the setting is non-trivial (mirrors modifier_flags)
    out.flags = 0;
    if (prof.lr_output_dpi_ratio != 1.0) out.flags |= RA_F_APPLY_DIR_MUL_X;
    if (prof.ud_output_dpi_ratio != 1.0) out.flags |= RA_F_APPLY_DIR_MUL_Y;
    if (prof.degrees_rotation != 0.0)    out.flags |= RA_F_APPLY_ROTATE;
    if (prof.speed_max > 0.0 && prof.speed_min <= prof.speed_max)
        out.flags |= RA_F_CLAMP_SPEED;
    // whole-mode anisotropic range weights blend by angle in-kernel
    if (prof.speed_processor_args.whole &&
        prof.range_weights.x != prof.range_weights.y)
        out.flags |= RA_F_APPLY_DIR_WEIGHT;
    if (prof.degrees_snap != 0.0) out.flags |= RA_F_APPLY_SNAP;
    // smoothing flags: LUT is stateless, EMA is layered in-kernel
    if (settings.prof.speed_processor_args.input_speed_smooth_halflife > 0.0)
        out.flags |= RA_F_SMOOTH_INPUT;
    if (settings.prof.speed_processor_args.scale_smooth_halflife > 0.0)
        out.flags |= RA_F_SMOOTH_SCALE;
    if (settings.prof.speed_processor_args.output_speed_smooth_halflife > 0.0)
        out.flags |= RA_F_SMOOTH_OUTPUT;

    // Distance mode mirrors speed_processor::init.
    const auto& spa = prof.speed_processor_args;
    if (!spa.whole)
        out.dist_mode = RA_DIST_SEPARATE;
    else if (spa.lp_norm >= ra::MAX_NORM || spa.lp_norm <= 0)
        out.dist_mode = RA_DIST_MAX;
    else if (spa.lp_norm != 2)
        out.dist_mode = RA_DIST_LP;
    else
        out.dist_mode = RA_DIST_EUCLIDEAN;

    // refuse unported features rather than approximate
    // Lp norm deferred (needs fixed-point pow)
    if (out.dist_mode == RA_DIST_LP)
        throw std::runtime_error("rawaccel: Lp distance norm not yet supported on Linux");

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
    ra_bpf_config cfg{};  // value-initialized: all fields zeroed

    cfg.report_id      = layout.report_id;
    cfg.dx_byte_offset = layout.dx_byte_offset;
    cfg.dx_byte_size   = layout.dx_byte_size;
    cfg.dy_byte_offset = layout.dy_byte_offset;
    cfg.dy_byte_size   = layout.dy_byte_size;

    cfg.dpi_norm_q16     = lut.dpi_norm_q16;
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
    cfg.lr_ratio_q16      = lut.lr_ratio_q16;
    cfg.ud_ratio_q16      = lut.ud_ratio_q16;
    cfg.rot_cos_q16       = lut.rot_cos_q16;
    cfg.rot_sin_q16       = lut.rot_sin_q16;
    cfg.speed_min_q16     = lut.speed_min_q16;
    cfg.speed_max_q16     = lut.speed_max_q16;
    cfg.snap_lo_tan_q16   = lut.snap_lo_tan_q16;
    cfg.snap_hi_tan_q16   = lut.snap_hi_tan_q16;
    cfg.time_min_q16      = lut.time_min_q16;
    cfg.time_max_q16      = lut.time_max_q16;
    cfg.in_coeffs.log2_win = lut.in_log2_win_q16;
    cfg.in_coeffs.log2_cut = lut.in_log2_cut_q16;
    cfg.in_coeffs.log2_trw = lut.in_log2_trw_q16;
    cfg.in_coeffs.log2_trc = lut.in_log2_trc_q16;
    cfg.scale_coeffs.log2_win = lut.sc_log2_win_q16;
    cfg.scale_coeffs.log2_cut = lut.sc_log2_cut_q16;
    cfg.out_coeffs.log2_win = lut.out_log2_win_q16;
    cfg.out_coeffs.log2_cut = lut.out_log2_cut_q16;
    cfg.out_coeffs.log2_trw = lut.out_log2_trw_q16;
    cfg.out_coeffs.log2_trc = lut.out_log2_trc_q16;

    return cfg;
}

} // namespace rawaccel_agent
