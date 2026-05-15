#pragma once

// Precompute the per-axis Q16.16 lookup tables that the BPF program reads
// at runtime. The agent walks the active modifier at quantized speeds
// using full-precision common/ math; the resulting LUTs are what live in
// the kernel-side ra_lut_x and ra_lut_y maps.
//
// Step 11 of LINUX_PORT_PLAN.md. Producer side: drives the modifier; runs
// at user-mode 'apply' time. Consumer side (step 12): copies the result
// straight into the BPF maps before attach.

#include "rawaccel.hpp"
#include "rawaccel-base.hpp"
#include "rawaccel_bpf_layout.h"

#include <array>
#include <cstdint>

namespace rawaccel_agent {

namespace ra = rawaccel;

struct LutBuildResult {
    // Per-axis acceleration scale at each quantized speed bucket, Q16.16.
    // Index i corresponds to a normalized speed of i * lut_step (the BPF
    // program multiplies raw HID counts by dpi_norm to enter this space).
    std::array<std::int32_t, RA_LUT_SIZE> lut_x{};
    std::array<std::int32_t, RA_LUT_SIZE> lut_y{};

    // ra_bpf_config fields the LUT builder owns. Step 12 fills the rest
    // (report_id, dx/dy byte offset and size) from the HID descriptor
    // parser and copies all of them into the kernel ra_config map.
    std::int32_t lut_step_q16     = 0;
    std::int32_t lut_max_q16      = 0;
    std::int32_t dpi_norm_q16     = 0;
    std::int32_t smooth_alpha_q16 = 0;
};

// Walk the modifier at RA_LUT_SIZE speed buckets and emit a complete
// LutBuildResult. The settings copy is made stateless internally (smoother
// halflives zeroed) so the LUT only reflects the curve, not any warmup
// state the live agent might be carrying.
LutBuildResult build_lut(const ra::modifier_settings& settings,
                         const ra::device_config& dev_config);

} // namespace rawaccel_agent
