#ifndef RAWACCEL_FIXEDPOINT_H
#define RAWACCEL_FIXEDPOINT_H

/* Fixed-point (Q16.16) port of common/rawaccel.hpp's modifier::modify.
 *
 * This header is the single source of the per-packet math. It is compiled
 * BOTH into the BPF program (linux/bpf/rawaccel.bpf.c) and into host unit
 * tests (linux/tests/fixedpoint_tests.cpp), so the exact kernel arithmetic
 * is validated in userspace against the double-precision common/ math with
 * no CAP_BPF or live device required.
 *
 * Rules that keep it BPF-clean (and therefore host-identical):
 *   - no floats/doubles, only __s32 / __s64 / __u32 / __u64
 *   - no libc, no math.h: every primitive is defined here
 *   - no global mutable state: per-device state arrives via ra_bpf_state*
 *   - no dynamic loops, no function pointers
 *
 * The only kernel/host difference is how the LUT is read: the kernel fetches
 * entries with bpf_map_lookup_elem (one element per call, the verifier will
 * not let a map pointer stride), while the host indexes a flat array. The
 * arithmetic helpers are shared; ra_modify_q16_flat documents and tests the
 * exact composition the kernel program mirrors.
 */

#include "rawaccel_bpf_layout.h"

#ifdef __BPF__
#define RA_FP_INLINE static __always_inline
#else
#include <cstdint>
#define RA_FP_INLINE static inline
#endif

/* ---- scalar primitives --------------------------------------------- */

RA_FP_INLINE __s32 ra_abs_s32(__s32 v)
{
    return v < 0 ? -v : v;
}

/* Q16.16 multiply: (a * b) >> 16, with a 64-bit intermediate. */
RA_FP_INLINE __s32 ra_mul_q16(__s32 a, __s32 b)
{
    return (__s32)(((__s64)a * (__s64)b) >> RA_Q16_SHIFT);
}

/* Linear interpolation between two Q16.16 values; frac in [0, RA_Q16_ONE). */
RA_FP_INLINE __s32 ra_q16_lerp(__s32 a, __s32 b, __s32 frac_q16)
{
    return a + (__s32)((((__s64)(b - a)) * (__s64)frac_q16) >> RA_Q16_SHIFT);
}

/* ---- pipeline stages ----------------------------------------------- */

/* Phase 0 velocity metric: max(|dx|,|dy|) scaled by the DPI normalization
 * factor, producing Q16.16 in/s. (Phase 1 replaces this with true magnitude
 * and the configured distance mode.) */
RA_FP_INLINE __s32 ra_velocity_q16(__s32 dx, __s32 dy, __s32 dpi_norm_q16)
{
    __s32 ax = ra_abs_s32(dx);
    __s32 ay = ra_abs_s32(dy);
    __s32 v_counts = ax > ay ? ax : ay;
    /* counts(integer) * Q16 == Q16 */
    return (__s32)((__s64)v_counts * (__s64)dpi_norm_q16);
}

/* Single exponential moving average on the velocity. Mutates *smoothed_q16
 * and returns the new value (clamped >= 0). alpha in [0, RA_Q16_ONE];
 * alpha == RA_Q16_ONE (the no-smoothing default) makes this an identity that
 * just latches the sample, matching the halflife-zeroed common/ path. */
RA_FP_INLINE __s32 ra_ema_step(__s32 *smoothed_q16, __s32 sample_q16,
                               __s32 alpha_q16)
{
    if (alpha_q16 < 0) alpha_q16 = 0;
    if (alpha_q16 > RA_Q16_ONE) alpha_q16 = RA_Q16_ONE;
    __s64 diff = (__s64)sample_q16 - (__s64)*smoothed_q16;
    *smoothed_q16 += (__s32)((diff * (__s64)alpha_q16) >> RA_Q16_SHIFT);
    if (*smoothed_q16 < 0) *smoothed_q16 = 0;
    return *smoothed_q16;
}

/* Map a Q16.16 speed onto a LUT bucket index and fractional weight. Both
 * operands are non-negative so the divisions are unsigned (the BPF verifier
 * rejects signed division). idx is clamped to [0, RA_LUT_SIZE - 2] so idx+1
 * is always in range for the lerp. */
RA_FP_INLINE void ra_lut_index(__s32 speed_q16, __s32 step_q16, __s32 max_q16,
                               __u32 *idx_out, __s32 *frac_out)
{
    if (speed_q16 < 0) speed_q16 = 0;
    if (max_q16 > 0 && speed_q16 >= max_q16) speed_q16 = max_q16 - 1;
    if (step_q16 <= 0) step_q16 = RA_Q16_ONE;

    __u64 sv = (__u64)(__u32)speed_q16;
    __u64 st = (__u64)(__u32)step_q16;
    __u32 idx = (__u32)(sv / st);
    __u64 frac = sv % st;
    __s32 frac_q16 = (__s32)((frac << RA_Q16_SHIFT) / st);

    if (idx >= RA_LUT_SIZE - 1) idx = RA_LUT_SIZE - 2;
    *idx_out = idx;
    *frac_out = frac_q16;
}

/* Flat-array LUT fetch + lerp. Used directly by host tests; the kernel does
 * the equivalent with two bpf_map_lookup_elem calls feeding ra_q16_lerp. */
RA_FP_INLINE __s32 ra_lut_sample(const __s32 *lut, __u32 idx, __s32 frac_q16)
{
    __u32 i0 = idx & (RA_LUT_SIZE - 1);
    __u32 i1 = (idx + 1) & (RA_LUT_SIZE - 1);
    return ra_q16_lerp(lut[i0], lut[i1], frac_q16);
}

/* Apply range weighting then output-DPI scaling to a raw curve scale.
 * Mirrors callback_template (1 + (f-1)*weight) followed by the output_dpi
 * adjustment in modify. dir_extra_q16 is the per-axis trailing factor
 * (yx_output_dpi_ratio for Y, RA_Q16_ONE for X). */
RA_FP_INLINE __s32 ra_axis_eff_scale(__s32 raw_q16, __s32 range_w_q16,
                                     __s32 output_dpi_adj_q16,
                                     __s32 dir_extra_q16)
{
    __s32 scale = RA_Q16_ONE + ra_mul_q16(raw_q16 - RA_Q16_ONE, range_w_q16);
    scale = ra_mul_q16(scale, output_dpi_adj_q16);
    scale = ra_mul_q16(scale, dir_extra_q16);
    return scale;
}

/* Full per-packet pipeline against a flat LUT, producing the post-
 * acceleration output vector in Q16.16 (before fractional carry, which the
 * caller owns). The kernel event handler mirrors these exact steps with
 * map-based LUT reads. */
RA_FP_INLINE void ra_modify_q16_flat(const struct ra_bpf_config *cfg,
                                     struct ra_bpf_state *st,
                                     const __s32 *lut_x, const __s32 *lut_y,
                                     __s32 dx, __s32 dy,
                                     __s64 *out_x_q16, __s64 *out_y_q16)
{
    __s32 v  = ra_velocity_q16(dx, dy, cfg->dpi_norm_q16);
    __s32 sv = ra_ema_step(&st->smoothed_v_q16, v, cfg->smooth_alpha_q16);

    /* Per-axis domain weighting folds into the LUT-index speed, reproducing
     * how the old baked LUT sampled the curve at speed * domain_weight. */
    __s32 sx = ra_mul_q16(sv, cfg->domain_w_x_q16);
    __s32 sy = ra_mul_q16(sv, cfg->domain_w_y_q16);

    __u32 ix, iy;
    __s32 fx, fy;
    ra_lut_index(sx, cfg->lut_step_q16, cfg->lut_max_q16, &ix, &fx);
    ra_lut_index(sy, cfg->lut_step_q16, cfg->lut_max_q16, &iy, &fy);

    __s32 raw_x = ra_lut_sample(lut_x, ix, fx);
    __s32 raw_y = ra_lut_sample(lut_y, iy, fy);

    __s32 eff_x = ra_axis_eff_scale(raw_x, cfg->range_w_x_q16,
                                    cfg->output_dpi_adj_q16, RA_Q16_ONE);
    __s32 eff_y = ra_axis_eff_scale(raw_y, cfg->range_w_y_q16,
                                    cfg->output_dpi_adj_q16, cfg->yx_ratio_q16);

    *out_x_q16 = (__s64)dx * (__s64)eff_x;
    *out_y_q16 = (__s64)dy * (__s64)eff_y;
}

#endif /* RAWACCEL_FIXEDPOINT_H */
