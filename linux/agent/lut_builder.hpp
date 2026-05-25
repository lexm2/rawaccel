#pragma once

// Precompute the per-axis Q16.16 LUTs plus the scalar config the BPF program
// reads. LUTs hold the RAW curve scale f(speed); weighting, output-DPI, and
// directional multipliers are config fields applied in-kernel around the LUT
// (mirrors modifier::modify). Tables are sampled with full-precision common/
// math and live in the kernel ra_lut_x / ra_lut_y maps.

#include "hid_descriptor.hpp"   // BpfMouseLayout
#include "rawaccel.hpp"
#include "rawaccel-base.hpp"
#include "rawaccel_bpf_layout.h"

#include <array>
#include <cstdint>

namespace rawaccel_agent {

namespace ra = rawaccel;

struct LutBuildResult {
    // Per-axis RAW curve f(speed), Q16.16. Index i = speed i * lut_step
    // (kernel maps counts into this space via dpi_norm and domain weight).
    std::array<std::int32_t, RA_LUT_SIZE> lut_x{};
    std::array<std::int32_t, RA_LUT_SIZE> lut_y{};

    // velocity-domain config (HID layout fields filled by the BPF backend)
    std::int32_t lut_step_q16     = 0;
    std::int32_t lut_max_q16      = 0;
    std::int32_t dpi_norm_q16     = 0;

    // Weighting / output scaling moved out of the LUT and applied in-kernel.
    std::int32_t range_w_x_q16     = 0;
    std::int32_t range_w_y_q16     = 0;
    std::int32_t domain_w_x_q16    = 0;
    std::int32_t domain_w_y_q16    = 0;
    std::int32_t output_dpi_adj_q16 = 0;
    std::int32_t yx_ratio_q16      = 0;
    std::int32_t lr_ratio_q16      = 0;
    std::int32_t ud_ratio_q16      = 0;
    std::int32_t rot_cos_q16       = 0;
    std::int32_t rot_sin_q16       = 0;
    std::int32_t speed_min_q16     = 0;
    std::int32_t speed_max_q16     = 0;
    std::int32_t snap_lo_tan_q16   = 0;
    std::int32_t snap_hi_tan_q16   = 0;
    std::int32_t time_min_q16      = 0;
    std::int32_t time_max_q16      = 0;

    // input_speed_smoother (linear EMA) log2 coeffs (negative); RA_F_SMOOTH_INPUT.
    std::int32_t in_log2_win_q16   = 0;
    std::int32_t in_log2_cut_q16   = 0;
    std::int32_t in_log2_trw_q16   = 0;
    std::int32_t in_log2_trc_q16   = 0;

    // scale_smoother (simple EMA) log2 coefficients; RA_F_SMOOTH_SCALE.
    std::int32_t sc_log2_win_q16   = 0;
    std::int32_t sc_log2_cut_q16   = 0;

    // output_speed_smoother (linear EMA) log2 coefficients; RA_F_SMOOTH_OUTPUT.
    std::int32_t out_log2_win_q16  = 0;
    std::int32_t out_log2_cut_q16  = 0;
    std::int32_t out_log2_trw_q16  = 0;
    std::int32_t out_log2_trc_q16  = 0;

    // modifier_flags + distance mode (RA_F_* / RA_DIST_*)
    std::uint32_t flags    = 0;
    std::uint8_t  dist_mode = 0;
};

// Sample the per-axis curves at RA_LUT_SIZE buckets into a LutBuildResult.
// Settings are zeroed stateless internally so the LUT is the curve only.
LutBuildResult build_lut(const ra::modifier_settings& settings,
                         const ra::device_config& dev_config);

// Assemble the kernel config from a built LUT + HID layout. Shared by the BPF
// backend and the host parity tests so the two never drift.
ra_bpf_config to_bpf_config(const LutBuildResult& lut,
                            const BpfMouseLayout& layout);

} // namespace rawaccel_agent
