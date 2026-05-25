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
/* Expensive helpers are compiled as BPF-to-BPF subprograms (verified once)
 * instead of inlined into every control-flow path, which would multiply the
 * verifier's processed-instruction count and trip the complexity limit
 * (-E2BIG). On the host they are ordinary inline functions. */
#define RA_FP_NOINLINE static __noinline
#else
#include <cstdint>
#define RA_FP_INLINE static inline
#define RA_FP_NOINLINE static inline
#endif

/* Optimization barrier. On BPF it forces a value to a register and stops clang
 * from "seeing through" an arithmetic sign-mask and reconstructing it as a
 * conditional branch (which, inside a loop, explodes the verifier's path
 * count). A no-op on the host. */
#ifdef __BPF__
#define RA_BARRIER(x) asm volatile("" : "+r"(x))
#else
#define RA_BARRIER(x) ((void)0)
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

/* Rotate a Q16.16 vector by the precomputed direction {cos, sin}. Mirrors
 * common/math-vec2.hpp rotate(): {x*cos - y*sin, x*sin + y*cos}. */
RA_FP_INLINE void ra_rotate_q16(__s64 *x, __s64 *y, __s32 cos_q16, __s32 sin_q16)
{
    __s64 rx = (*x * (__s64)cos_q16 - *y * (__s64)sin_q16) >> RA_Q16_SHIFT;
    __s64 ry = (*x * (__s64)sin_q16 + *y * (__s64)cos_q16) >> RA_Q16_SHIFT;
    *x = rx;
    *y = ry;
}

/* Saturate a 64-bit value into signed 32-bit range. Speeds past this are far
 * beyond lut_max and get clamped at index time anyway. */
RA_FP_INLINE __s32 ra_sat_s32(__s64 v)
{
    if (v > 0x7fffffff) return 0x7fffffff;
    if (v < -0x7fffffff) return -0x7fffffff;
    return (__s32)v;
}

/* Integer sqrt of a u64, digit-by-digit, fixed 32-iteration loop.
 *
 * BRANCHLESS on purpose: a data-dependent `if` in this loop would fork the
 * verifier at every iteration (~2^32 paths -> the load fails with -E2BIG), so
 * the "n >= trial" decision is turned into an all-ones / all-zero mask via the
 * sign bit and applied arithmetically. Callers must keep n < 2^63 so the
 * signed compare is valid (ra_magnitude_q16 bounds its inputs for this). */
/* Euclidean magnitude of a Q16.16 vector, in Q16.16. (x*2^16)^2 + (y*2^16)^2
 * is mag^2 << 32, so the integer sqrt of that sum is mag << 16.
 *
 * Computed by Newton's method seeded with max(ax, ay): the true root lies in
 * [max, sqrt(2)*max], so four iterations converge to full Q16.16 precision.
 * Newton's loop is branchless (unsigned division, no comparisons), which keeps
 * it short and keeps the verifier's processed-instruction count flat -- a
 * digit-by-digit sqrt is ~30 iterations and was re-walked per call-site state.
 * Inputs are bounded to 2^30 so ax^2 + ay^2 stays within u64 and anything past
 * lut_max collapses to the same index anyway. __noinline: verified once. */
RA_FP_NOINLINE __s32 ra_magnitude_q16(__s64 x_q16, __s64 y_q16)
{
    __u64 ax = (__u64)(x_q16 < 0 ? -x_q16 : x_q16);
    __u64 ay = (__u64)(y_q16 < 0 ? -y_q16 : y_q16);
    if (ax > 0x40000000ULL) ax = 0x40000000ULL;
    if (ay > 0x40000000ULL) ay = 0x40000000ULL;

    __u64 n = ax * ax + ay * ay;           /* mag^2 << 32 */
    __u64 x = ax > ay ? ax : ay;           /* seed in [mag/sqrt2, mag] */
    if (x == 0) return 0;                  /* n == 0 */

    x = (x + n / x) >> 1;
    x = (x + n / x) >> 1;
    x = (x + n / x) >> 1;
    x = (x + n / x) >> 1;

    return x > 0x7fffffff ? 0x7fffffff : (__s32)x;
}

/* Q16.16 angle constants. */
#define RA_PI_2_Q16        102944  /* (pi/2)  * 2^16 */
#define RA_PI_4_Q16         51472  /* (pi/4)  * 2^16 */
#define RA_TWO_OVER_PI_Q16  41721  /* (2/pi)  * 2^16 */

/* atan(|num/den|) in Q16.16 radians, range [0, pi/2]. Mirrors modify's
 * reference_angle: den == 0 (purely vertical) -> pi/2, num == 0 (purely
 * horizontal) -> 0.
 *
 * The unit-interval atan is Rajan's minimax polynomial:
 *   atan(r) ~= (pi/4) r - r (r - 1) (0.2447 + 0.0663 r),   r in [0, 1]
 * accurate to < 0.0015 rad. For r > 1 the identity atan(r) = pi/2 - atan(1/r)
 * folds the argument back into [0, 1], so one branch covers every angle and
 * the polynomial is never evaluated outside its fit range. Operands are made
 * non-negative and ordered lo <= hi, so the ratio division is unsigned (the
 * verifier rejects signed division). __noinline: verified once. */
RA_FP_NOINLINE __s32 ra_atan_ratio_q16(__s64 num, __s64 den)
{
    __u64 a = (__u64)(num < 0 ? -num : num);
    __u64 b = (__u64)(den < 0 ? -den : den);
    if (b == 0) return RA_PI_2_Q16;   /* purely vertical */
    if (a == 0) return 0;             /* purely horizontal */

    __u64 lo, hi;
    int swap;
    if (a <= b) { lo = a; hi = b; swap = 0; }
    else        { lo = b; hi = a; swap = 1; }

    __s32 r = (__s32)((lo << RA_Q16_SHIFT) / hi);   /* r in [0, RA_Q16_ONE] */

    __s32 inner = 16038 + ra_mul_q16(4345, r);      /* 0.2447 + 0.0663 r */
    __s32 corr  = ra_mul_q16(ra_mul_q16(r, r - RA_Q16_ONE), inner);
    __s32 at    = ra_mul_q16(RA_PI_4_Q16, r) - corr;   /* atan(r), r in [0,1] */

    return swap ? RA_PI_2_Q16 - at : at;
}

/* Speed clamp (modifier::modify): clamp the working vector's speed
 * (magnitude * dpi_norm, in/s) to [speed_min, speed_max] and rescale the
 * vector by the resulting ratio. __noinline: verified once. */
RA_FP_NOINLINE void ra_clamp_speed(const struct ra_bpf_config *cfg,
                                   __s64 *inx, __s64 *iny)
{
    __s32 mag = ra_magnitude_q16(*inx, *iny);          /* Q16 counts */
    __s32 speed = ra_mul_q16(mag, cfg->dpi_norm_q16);  /* Q16 in/s */
    if (speed <= 0) return;

    __s32 clamped = speed;
    if (clamped < cfg->speed_min_q16) clamped = cfg->speed_min_q16;
    if (clamped > cfg->speed_max_q16) clamped = cfg->speed_max_q16;

    /* ratio = clamped / speed, Q16.16. Both operands are positive, so the
     * division is unsigned (the verifier rejects signed division). */
    __u64 ratio = ((__u64)(__u32)clamped << RA_Q16_SHIFT) / (__u64)(__u32)speed;
    *inx = (*inx * (__s64)ratio) >> RA_Q16_SHIFT;
    *iny = (*iny * (__s64)ratio) >> RA_Q16_SHIFT;
}

/* Per-axis abs weighted velocity (modify's abs_weighted_vel component):
 * |component| * dpi_norm * domain_weight, in Q16.16 in/s, saturated >= 0. */
RA_FP_NOINLINE __s32 ra_axis_speed_q16(__s64 comp_q16, __s32 dpi_norm_q16,
                                       __s32 domain_w_q16)
{
    __s64 a = comp_q16 < 0 ? -comp_q16 : comp_q16;        /* |comp| Q16 counts */
    __s64 v = (a * (__s64)dpi_norm_q16) >> RA_Q16_SHIFT;  /* Q16 in/s */
    v = (v * (__s64)domain_w_q16) >> RA_Q16_SHIFT;        /* weighted */
    return ra_sat_s32(v);
}

/* Single exponential moving average on the velocity. Mutates *smoothed_q16
 * and returns the new value (clamped >= 0). alpha in [0, RA_Q16_ONE];
 * alpha == RA_Q16_ONE (the no-smoothing default) makes this an identity that
 * just latches the sample, matching the halflife-zeroed common/ path. */
RA_FP_NOINLINE __s32 ra_ema_step(__s32 *smoothed_q16, __s32 sample_q16,
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
RA_FP_NOINLINE void ra_lut_index(__s32 speed_q16, __s32 step_q16, __s32 max_q16,
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

/* The pipeline is split around the LUT fetch so the kernel program and the
 * host tests share everything except how the table is read (map lookups vs a
 * flat array). ra_pre_lut and ra_post_lut are the shared, host-tested halves;
 * the working vector (inx/iny, Q16.16) carries the per-packet transforms that
 * later phases add (rotation, snap, speed clamp) from one half to the other.
 *
 * Stage 1: raw counts -> working vector + per-axis curve LUT indices. */
RA_FP_INLINE void ra_pre_lut(const struct ra_bpf_config *cfg,
                             struct ra_bpf_state *st,
                             __s32 dx, __s32 dy,
                             __s64 *inx_q16, __s64 *iny_q16,
                             __u32 *ix, __s32 *fx, __u32 *iy, __s32 *fy,
                             __u8 *single_scale, __s32 *weight_q16)
{
    /* Working vector starts as the raw counts in Q16.16. Snap (a later Phase 1
     * step) will also transform (inx, iny) here. */
    __s64 inx = (__s64)dx << RA_Q16_SHIFT;
    __s64 iny = (__s64)dy << RA_Q16_SHIFT;

    /* Rotation is the first transform in modifier::modify. Velocity and the
     * curve index are derived from the rotated vector. */
    if (cfg->flags & RA_F_APPLY_ROTATE)
        ra_rotate_q16(&inx, &iny, cfg->rot_cos_q16, cfg->rot_sin_q16);

    /* Whole-mode directional weighting: the per-vector range weight is blended
     * between range_weights.x and .y by the movement's reference angle (modify
     * lines 383-388: weight = range_w_x + (2/pi)*ref_angle*(range_w_y -
     * range_w_x), a lerp keyed on (2/pi)*ref_angle). Taken from the rotated
     * vector, before the angle-preserving speed clamp, matching modify's order.
     * Defaults to range_w_x; only the whole branch consumes it. */
    __s32 weight = cfg->range_w_x_q16;
    if (cfg->flags & RA_F_APPLY_DIR_WEIGHT) {
        __s32 ang = ra_atan_ratio_q16(iny, inx);            /* [0, pi/2] */
        __s32 t   = ra_mul_q16(RA_TWO_OVER_PI_Q16, ang);    /* [0, 1] */
        weight = ra_q16_lerp(cfg->range_w_x_q16, cfg->range_w_y_q16, t);
    }
    *weight_q16 = weight;

    /* Speed clamp acts on the (rotated) vector before the curve. */
    if (cfg->flags & RA_F_CLAMP_SPEED)
        ra_clamp_speed(cfg, &inx, &iny);

    *inx_q16 = inx;
    *iny_q16 = iny;

    /* Per-axis abs weighted velocity (modify's abs_weighted_vel). */
    __s32 awv_x = ra_axis_speed_q16(inx, cfg->dpi_norm_q16, cfg->domain_w_x_q16);
    __s32 awv_y = ra_axis_speed_q16(iny, cfg->dpi_norm_q16, cfg->domain_w_y_q16);

    if (cfg->dist_mode == RA_DIST_SEPARATE) {
        /* Separate: each axis indexes its own curve at its own speed. Per-axis
         * input smoothing is a Phase 2 addition; with halflife 0 the EMA is an
         * identity so it is omitted here. */
        ra_lut_index(awv_x, cfg->lut_step_q16, cfg->lut_max_q16, ix, fx);
        ra_lut_index(awv_y, cfg->lut_step_q16, cfg->lut_max_q16, iy, fy);
        *single_scale = 0;
    } else {
        /* Whole modes: one aggregate speed, one scale (from accel_x) applied
         * to the whole vector. */
        __s32 S;
        if (cfg->dist_mode == RA_DIST_MAX)
            S = awv_x > awv_y ? awv_x : awv_y;
        else
            /* euclidean. The agent rejects Lp configs, so RA_DIST_LP never
             * reaches the kernel; this branch only ever sees euclidean. */
            S = ra_magnitude_q16(awv_x, awv_y);

        __s32 sv = ra_ema_step(&st->smoothed_v_q16, S, cfg->smooth_alpha_q16);
        ra_lut_index(sv, cfg->lut_step_q16, cfg->lut_max_q16, ix, fx);
        *iy = *ix;            /* y reuses the accel_x curve; raw_y is discarded */
        *fy = *fx;
        *single_scale = 1;
    }
}

/* Stage 2: working vector + raw per-axis curve scales -> output vector in
 * Q16.16 (before fractional carry, which the caller owns). Applies range
 * weighting, output-DPI scaling, and the directional output-DPI multipliers.
 * In a whole mode (single_scale) both axes use the accel_x curve and the single
 * blended weight_q16 (range_weights.x when directional weighting is off),
 * matching modify; separate mode uses each axis's own raw curve and weight. */
RA_FP_INLINE void ra_post_lut(const struct ra_bpf_config *cfg,
                              __s64 inx_q16, __s64 iny_q16,
                              __s32 raw_x, __s32 raw_y, __u8 single_scale,
                              __s32 weight_q16,
                              __s64 *out_x_q16, __s64 *out_y_q16)
{
    __s32 ry = single_scale ? raw_x : raw_y;
    __s32 wx = single_scale ? weight_q16 : cfg->range_w_x_q16;
    __s32 wy = single_scale ? weight_q16 : cfg->range_w_y_q16;

    __s32 eff_x = ra_axis_eff_scale(raw_x, wx,
                                    cfg->output_dpi_adj_q16, RA_Q16_ONE);
    __s32 eff_y = ra_axis_eff_scale(ry, wy,
                                    cfg->output_dpi_adj_q16, cfg->yx_ratio_q16);

    __s64 ox = (inx_q16 * (__s64)eff_x) >> RA_Q16_SHIFT;
    __s64 oy = (iny_q16 * (__s64)eff_y) >> RA_Q16_SHIFT;

    /* Directional output DPI (modifier::modify): scale a component only when
     * its post-scale output is negative. */
    if ((cfg->flags & RA_F_APPLY_DIR_MUL_X) && ox < 0)
        ox = (ox * (__s64)cfg->lr_ratio_q16) >> RA_Q16_SHIFT;
    if ((cfg->flags & RA_F_APPLY_DIR_MUL_Y) && oy < 0)
        oy = (oy * (__s64)cfg->ud_ratio_q16) >> RA_Q16_SHIFT;

    *out_x_q16 = ox;
    *out_y_q16 = oy;
}

/* Full per-packet pipeline against a flat LUT (host tests + the reference
 * composition the kernel event handler mirrors with map-based LUT reads). */
RA_FP_INLINE void ra_modify_q16_flat(const struct ra_bpf_config *cfg,
                                     struct ra_bpf_state *st,
                                     const __s32 *lut_x, const __s32 *lut_y,
                                     __s32 dx, __s32 dy,
                                     __s64 *out_x_q16, __s64 *out_y_q16)
{
    __s64 inx, iny;
    __u32 ix, iy;
    __s32 fx, fy;
    __u8 single_scale;
    __s32 weight;
    ra_pre_lut(cfg, st, dx, dy, &inx, &iny, &ix, &fx, &iy, &fy,
               &single_scale, &weight);

    __s32 raw_x = ra_lut_sample(lut_x, ix, fx);
    __s32 raw_y = ra_lut_sample(lut_y, iy, fy);

    ra_post_lut(cfg, inx, iny, raw_x, raw_y, single_scale, weight,
                out_x_q16, out_y_q16);
}

#endif /* RAWACCEL_FIXEDPOINT_H */
