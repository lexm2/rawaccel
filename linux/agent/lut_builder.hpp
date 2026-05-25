#pragma once

// Precompute the per-axis Q16.16 lookup tables plus the scalar config the BPF
// program reads at runtime. The LUTs hold the RAW curve scale f(speed) (one
// per axis); range/domain weighting, output-DPI scaling, and the directional
// multipliers are emitted as config fields and applied in-kernel around the
// LUT, mirroring common/rawaccel.hpp's modifier::modify. The agent walks the
// active modifier's curves at quantized speeds using full-precision common/
// math; the resulting tables are what live in the kernel-side ra_lut_x /
// ra_lut_y maps.

#include "hid_descriptor.hpp"   // BpfMouseLayout
#include "rawaccel.hpp"
#include "rawaccel-base.hpp"
#include "rawaccel_bpf_layout.h"

#include <array>
#include <cstdint>

namespace rawaccel_agent {

namespace ra = rawaccel;

struct LutBuildResult {
    // Per-axis RAW acceleration curve f(speed) at each quantized speed
    // bucket, Q16.16. Index i corresponds to a normalized speed of
    // i * lut_step (the kernel multiplies raw HID counts by dpi_norm, then by
    // the per-axis domain weight, to enter this space).
    std::array<std::int32_t, RA_LUT_SIZE> lut_x{};
    std::array<std::int32_t, RA_LUT_SIZE> lut_y{};

    // Velocity-domain config the LUT builder owns (the BPF backend fills the
    // HID layout fields from the descriptor parser).
    std::int32_t lut_step_q16     = 0;
    std::int32_t lut_max_q16      = 0;
    std::int32_t dpi_norm_q16     = 0;
    std::int32_t smooth_alpha_q16 = 0;

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

    // modifier_flags bitfield + distance mode (RA_F_* / RA_DIST_*). Reserved
    // for Phase 1; emitted now so the config layout is stable.
    std::uint32_t flags    = 0;
    std::uint8_t  dist_mode = 0;
};

// Walk the modifier's per-axis curves at RA_LUT_SIZE speed buckets and emit a
// complete LutBuildResult. The settings copy is made stateless internally
// (smoother halflives zeroed) so the LUT reflects only the curve, not any
// warmup state the live agent might be carrying.
LutBuildResult build_lut(const ra::modifier_settings& settings,
                         const ra::device_config& dev_config);

// Assemble the kernel config struct from a built LUT plus the device's HID
// report layout. Shared by the BPF backend (populate_maps) and the host
// parity tests so the two never drift.
ra_bpf_config to_bpf_config(const LutBuildResult& lut,
                            const BpfMouseLayout& layout);

} // namespace rawaccel_agent
