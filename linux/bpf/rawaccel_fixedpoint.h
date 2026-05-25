#ifndef RAWACCEL_FIXEDPOINT_H
#define RAWACCEL_FIXEDPOINT_H

/* Fixed-point (Q16.16) port of common/rawaccel.hpp modifier::modify.
 * Compiled into both the BPF program and host tests, so kernel arithmetic
 * is validated against the common/ doubles.
 * BPF-clean: __s32/__s64/__u32/__u64 only, no libc/math.h, no globals
 * (state via ra_bpf_state*), no dynamic loops or function pointers.
 * Kernel vs host differ only in the LUT read (map lookups vs flat array);
 * ra_modify_q16_flat is the shared composition the kernel mirrors. */

#include "rawaccel_bpf_layout.h"

#ifdef __BPF__
#define RA_FP_INLINE static __always_inline
/* Expensive helpers as BPF subprograms (verified once) instead of inlined
 * everywhere, which would trip the verifier complexity limit. Plain inline on host. */
#define RA_FP_NOINLINE static __noinline
#else
#include <cstdint>
#define RA_FP_INLINE static inline
#define RA_FP_NOINLINE static inline
#endif

/* Optimization barrier: forces a value to a register so clang can't turn a
 * sign-mask back into a branch (explodes verifier path count). No-op on host. */
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

/* Q16.16 multiply: (a * b) >> 16, 64-bit intermediate. */
RA_FP_INLINE __s32 ra_mul_q16(__s32 a, __s32 b)
{
    return (__s32)(((__s64)a * (__s64)b) >> RA_Q16_SHIFT);
}

/* Saturate s64 into s32 range. */
RA_FP_INLINE __s32 ra_sat_s32(__s64 v)
{
    if (v > 0x7fffffff) return 0x7fffffff;
    if (v < -0x7fffffff) return -0x7fffffff;
    return (__s32)v;
}

/* Q16.16 divide. Verifier rejects signed division, so divide magnitudes as
 * u64 and reapply sign. den==0 -> 0; saturates to s32. */
RA_FP_INLINE __s32 ra_div_q16(__s32 num, __s32 den)
{
    if (den == 0) return 0;
    int neg = (num < 0) ^ (den < 0);
    __u64 n = (__u64)(num < 0 ? -(__s64)num : (__s64)num);
    __u64 d = (__u64)(den < 0 ? -(__s64)den : (__s64)den);
    __u64 q = (n << RA_Q16_SHIFT) / d;
    return ra_sat_s32(neg ? -(__s64)q : (__s64)q);
}

/* ---- s64 Q16.16 (extended range) ----------------------------------- */
/* EMA accumulators are s64 Q16.16 so the trend*time term can't overflow s32. */
RA_FP_INLINE __s64 ra_mul_q16_s64(__s64 a_q16, __s32 b_q16)
{
    return (a_q16 * (__s64)b_q16) >> RA_Q16_SHIFT;
}

RA_FP_INLINE __s64 ra_div_q16_s64(__s64 num_q16, __s32 den_q16)
{
    if (den_q16 <= 0) return 0;                 /* dt is always > 0 here */
    int neg = num_q16 < 0;
    __u64 n = (__u64)(neg ? -num_q16 : num_q16);
    __u64 q = (n << RA_Q16_SHIFT) / (__u64)(__u32)den_q16;
    return neg ? -(__s64)q : (__s64)q;
}

/* 2^x in Q16.16 for x <= 0 (the EMA decay exponent is always <= 0).
 * 2^x = 2^frac >> (-floor); 2^frac is a minimax cubic, accurate to < 1e-3.
 * Underflow to 0 -> long dt gives full tracking (reset after a pause). */
RA_FP_NOINLINE __s32 ra_exp2_q16(__s32 x_q16)
{
    if (x_q16 >= 0) return RA_Q16_ONE;            /* domain is x <= 0; 2^0 = 1 */
    __s32 ipart = x_q16 >> RA_Q16_SHIFT;          /* floor toward -inf, <= -1 */
    __s32 frac  = x_q16 - (ipart << RA_Q16_SHIFT);/* in [0, RA_Q16_ONE) */
    int shift = -ipart;                            /* >= 1 right shifts */
    if (shift >= 32) return 0;

    /* 2^frac via Horner: 1 + f(a + f(b + fc)), a=0.696066 b=0.224494 c=0.079442 (Q16). */
    __s32 p = 14714 + ra_mul_q16(5206, frac);     /* b + c f */
    p = 45620 + ra_mul_q16(p, frac);              /* a + f(b + c f) */
    p = RA_Q16_ONE + ra_mul_q16(p, frac);         /* 1 + f(...) -> [ONE, 2 ONE) */
    return (__s32)((__u32)p >> shift);
}

/* Per-packet EMA alpha = 1 - 2^(dt * log2(coeff)), Q16.16 [0, ONE].
 * Agent precomputes log2(coeff) so the kernel needs no log/pow.
 * Mirrors `1 - pow(coeff, time)` in common/ smoothers. */
RA_FP_INLINE __s32 ra_ema_alpha_q16(__s32 dt_ms_q16, __s32 log2coeff_q16)
{
    __s32 x = ra_mul_q16(dt_ms_q16, log2coeff_q16);   /* dt * log2(coeff) <= 0 */
    return RA_Q16_ONE - ra_exp2_q16(x);
}

/* Linear interpolation between two Q16.16 values; frac in [0, RA_Q16_ONE). */
RA_FP_INLINE __s32 ra_q16_lerp(__s32 a, __s32 b, __s32 frac_q16)
{
    return a + (__s32)((((__s64)(b - a)) * (__s64)frac_q16) >> RA_Q16_SHIFT);
}

/* ---- pipeline stages ----------------------------------------------- */

/* Rotate a Q16.16 vector by {cos, sin}. Mirrors common/math-vec2.hpp rotate(). */
RA_FP_INLINE void ra_rotate_q16(__s64 *x, __s64 *y, __s32 cos_q16, __s32 sin_q16)
{
    __s64 rx = (*x * (__s64)cos_q16 - *y * (__s64)sin_q16) >> RA_Q16_SHIFT;
    __s64 ry = (*x * (__s64)sin_q16 + *y * (__s64)cos_q16) >> RA_Q16_SHIFT;
    *x = rx;
    *y = ry;
}

/* Euclidean magnitude of a Q16.16 vector, in Q16.16: sqrt((x<<16)^2 + (y<<16)^2).
 * Newton's method seeded with max(ax, ay); 4 iterations reach full precision.
 * Branchless (unsigned divide, no compares) to keep the verifier flat.
 * Inputs bounded to 2^30 so the sum stays in u64. __noinline: verified once. */
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

/* atan(|num/den|) in Q16.16 radians, [0, pi/2]. Mirrors modify's reference_angle
 * (den==0 -> pi/2, num==0 -> 0). Rajan's minimax polynomial on r in [0,1]:
 *   atan(r) ~= (pi/4)r - r(r-1)(0.2447 + 0.0663r),  < 0.0015 rad.
 * r > 1 folds via atan(r) = pi/2 - atan(1/r). __noinline: verified once. */
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

/* Speed clamp (modify): clamp speed (magnitude * eff_dpi_norm, in/s) to
 * [speed_min, speed_max] and rescale the vector by the ratio. __noinline. */
RA_FP_NOINLINE void ra_clamp_speed(const struct ra_bpf_config *cfg,
                                   __s32 eff_dpi_norm_q16,
                                   __s64 *inx, __s64 *iny)
{
    __s32 mag = ra_magnitude_q16(*inx, *iny);          /* Q16 counts */
    __s32 speed = ra_mul_q16(mag, eff_dpi_norm_q16);   /* Q16 in/s */
    if (speed <= 0) return;

    __s32 clamped = speed;
    if (clamped < cfg->speed_min_q16) clamped = cfg->speed_min_q16;
    if (clamped > cfg->speed_max_q16) clamped = cfg->speed_max_q16;

    /* ratio = clamped / speed, unsigned (verifier rejects signed division). */
    __u64 ratio = ((__u64)(__u32)clamped << RA_Q16_SHIFT) / (__u64)(__u32)speed;
    *inx = (*inx * (__s64)ratio) >> RA_Q16_SHIFT;
    *iny = (*iny * (__s64)ratio) >> RA_Q16_SHIFT;
}

/* Angle snapping (modify): collapse near-axis movement onto the axis, keeping
 * magnitude. Avoids atan by comparing |y| against precomputed tangents * |x|:
 *   |y| < tan(snap)*|x|        -> snap to X
 *   |y| > tan(pi/2 - snap)*|x| -> snap to Y
 * Cross-multiplied to stay in integers. __noinline: verified once. */
RA_FP_NOINLINE void ra_snap(const struct ra_bpf_config *cfg,
                            __s64 *inx, __s64 *iny)
{
    __s64 x = *inx, y = *iny;
    if (y == 0) return;                          /* on the X axis already */

    __s64 ax = x < 0 ? -x : x;
    __s64 ay = y < 0 ? -y : y;
    __s64 lhs = ay << RA_Q16_SHIFT;              /* |y| as Q16 of a Q16 count */

    if (lhs < (__s64)cfg->snap_lo_tan_q16 * ax) {
        __s32 mag = ra_magnitude_q16(x, y);
        *inx = x < 0 ? -(__s64)mag : (__s64)mag;
        *iny = 0;
    } else if (lhs > (__s64)cfg->snap_hi_tan_q16 * ax) {
        __s32 mag = ra_magnitude_q16(x, y);
        *iny = y < 0 ? -(__s64)mag : (__s64)mag;
        *inx = 0;
    }
}

/* Per-axis abs weighted velocity (modify's abs_weighted_vel):
 * |comp| * dpi_norm * domain_weight, Q16.16 in/s, saturated. */
RA_FP_NOINLINE __s32 ra_axis_speed_q16(__s64 comp_q16, __s32 dpi_norm_q16,
                                       __s32 domain_w_q16)
{
    __s64 a = comp_q16 < 0 ? -comp_q16 : comp_q16;        /* |comp| Q16 counts */
    __s64 v = (a * (__s64)dpi_norm_q16) >> RA_Q16_SHIFT;  /* Q16 in/s */
    v = (v * (__s64)domain_w_q16) >> RA_Q16_SHIFT;        /* weighted */
    return ra_sat_s32(v);
}

/* Trend dampening 0.75 in Q16.16 (linear_ema_smoother::trendDampening). */
#define RA_TREND_DAMP_Q16 49152

/* Linear EMA smoother, fixed-point mirror of common/ linear_ema_smoother::smooth.
 * Level + trend, each a window/cutoff pair. Per packet: dampen trend,
 * extrapolate level by trend*dt, pull toward sample by dt-adaptive alpha,
 * clamp >= 0, update trend. Returns min(window, cutoff). __noinline. */
RA_FP_NOINLINE __s32 ra_linear_ema_step(struct ra_linear_ema_state *s,
                                        const struct ra_linear_ema_coeffs *c,
                                        __s32 sample_q16, __s32 dt_ms_q16)
{
    __s32 a_win = ra_ema_alpha_q16(dt_ms_q16, c->log2_win);
    __s32 a_cut = ra_ema_alpha_q16(dt_ms_q16, c->log2_cut);
    __s32 a_trw = ra_ema_alpha_q16(dt_ms_q16, c->log2_trw);
    __s32 a_trc = ra_ema_alpha_q16(dt_ms_q16, c->log2_trc);

    __s64 old_win = s->win;
    __s64 old_cut = s->cut;

    /* dampen trends, then extrapolate level along the trend */
    s->win_tr = ra_mul_q16_s64(s->win_tr, RA_TREND_DAMP_Q16);
    s->cut_tr = ra_mul_q16_s64(s->cut_tr, RA_TREND_DAMP_Q16);
    s->win += ra_mul_q16_s64(s->win_tr, dt_ms_q16);
    s->cut += ra_mul_q16_s64(s->cut_tr, dt_ms_q16);

    /* level EMA toward the sample */
    s->win += ra_mul_q16_s64((__s64)sample_q16 - s->win, a_win);
    s->cut += ra_mul_q16_s64((__s64)sample_q16 - s->cut, a_cut);

    /* clamp level >= 0 */
    if (s->win < 0) s->win = 0;
    if (s->cut < 0) s->cut = 0;

    /* update trend from level change per unit time */
    __s64 new_trw = ra_div_q16_s64(s->win - old_win, dt_ms_q16);
    __s64 new_trc = ra_div_q16_s64(s->cut - old_cut, dt_ms_q16);
    s->win_tr += ra_mul_q16_s64(new_trw - s->win_tr, a_trw);
    s->cut_tr += ra_mul_q16_s64(new_trc - s->cut_tr, a_trc);

    __s64 m = s->win < s->cut ? s->win : s->cut;
    return ra_sat_s32(m);
}

/* Simple EMA smoother, fixed-point mirror of common/ simple_ema_smoother::smooth.
 * Window/cutoff level pair pulled toward the sample; no trend, no clamp.
 * Returns min(window, cutoff). __noinline. */
RA_FP_NOINLINE __s32 ra_simple_ema_step(struct ra_simple_ema_state *s,
                                        const struct ra_simple_ema_coeffs *c,
                                        __s32 sample_q16, __s32 dt_ms_q16)
{
    __s32 a_win = ra_ema_alpha_q16(dt_ms_q16, c->log2_win);
    __s32 a_cut = ra_ema_alpha_q16(dt_ms_q16, c->log2_cut);
    s->win += ra_mul_q16_s64((__s64)sample_q16 - s->win, a_win);
    s->cut += ra_mul_q16_s64((__s64)sample_q16 - s->cut, a_cut);
    __s64 m = s->win < s->cut ? s->win : s->cut;
    return ra_sat_s32(m);
}

/* Map a Q16.16 speed to a LUT index + fractional weight. Unsigned divides;
 * idx clamped to [0, RA_LUT_SIZE - 2] so idx+1 stays in range for the lerp. */
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

/* Flat-array LUT fetch + lerp (host). Kernel does the same via two map lookups. */
RA_FP_INLINE __s32 ra_lut_sample(const __s32 *lut, __u32 idx, __s32 frac_q16)
{
    __u32 i0 = idx & (RA_LUT_SIZE - 1);
    __u32 i1 = (idx + 1) & (RA_LUT_SIZE - 1);
    return ra_q16_lerp(lut[i0], lut[i1], frac_q16);
}

/* Pipeline split around the LUT fetch so kernel and host share everything but
 * the table read. ra_pre_lut/ra_post_lut are the shared halves; the working
 * vector (inx/iny) carries the per-packet transforms between them.
 * Stage 1: raw counts -> working vector + per-axis curve LUT indices. */
RA_FP_INLINE void ra_pre_lut(const struct ra_bpf_config *cfg,
                             struct ra_bpf_state *st,
                             __s32 dx, __s32 dy, __s32 dt_ms_q16,
                             __s64 *inx_q16, __s64 *iny_q16,
                             __u32 *ix, __s32 *fx, __u32 *iy, __s32 *fy,
                             __u8 *single_scale, __s32 *weight_q16)
{
    /* Working vector: raw counts in Q16.16, transformed toward the LUT stage. */
    __s64 inx = (__s64)dx << RA_Q16_SHIFT;
    __s64 iny = (__s64)dy << RA_Q16_SHIFT;

    /* Fold per-packet dt into velocity normalization: eff_dpi_norm =
     * dpi_norm / dt_ms gives in/s at the real polling interval (a 1 ms packet
     * is a no-op). Output stays in raw counts; dt scales only speed/clamp. */
    __s32 eff_dpi_norm_q16 = ra_div_q16(cfg->dpi_norm_q16, dt_ms_q16);

    /* Rotation first (modify); velocity and curve index use the rotated vector. */
    if (cfg->flags & RA_F_APPLY_ROTATE)
        ra_rotate_q16(&inx, &iny, cfg->rot_cos_q16, cfg->rot_sin_q16);

    /* Snap near-axis movement onto the axis before deriving angle/curve. */
    if (cfg->flags & RA_F_APPLY_SNAP)
        ra_snap(cfg, &inx, &iny);

    /* Whole-mode directional weight: lerp range_w_x..range_w_y by
     * (2/pi)*ref_angle (modify). Defaults to range_w_x. */
    __s32 weight = cfg->range_w_x_q16;
    if (cfg->flags & RA_F_APPLY_DIR_WEIGHT) {
        __s32 ang = ra_atan_ratio_q16(iny, inx);            /* [0, pi/2] */
        __s32 t   = ra_mul_q16(RA_TWO_OVER_PI_Q16, ang);    /* [0, 1] */
        weight = ra_q16_lerp(cfg->range_w_x_q16, cfg->range_w_y_q16, t);
    }
    *weight_q16 = weight;

    /* clamp the rotated vector before the curve */
    if (cfg->flags & RA_F_CLAMP_SPEED)
        ra_clamp_speed(cfg, eff_dpi_norm_q16, &inx, &iny);

    *inx_q16 = inx;
    *iny_q16 = iny;

    /* Per-axis abs weighted velocity (modify), real in/s via dt-folded norm. */
    __s32 awv_x = ra_axis_speed_q16(inx, eff_dpi_norm_q16, cfg->domain_w_x_q16);
    __s32 awv_y = ra_axis_speed_q16(iny, eff_dpi_norm_q16, cfg->domain_w_y_q16);

    if (cfg->dist_mode == RA_DIST_SEPARATE) {
        /* Separate: each axis indexes its own curve at its own (smoothed) speed. */
        if (cfg->flags & RA_F_SMOOTH_INPUT) {
            awv_x = ra_linear_ema_step(&st->in_x, &cfg->in_coeffs, awv_x, dt_ms_q16);
            awv_y = ra_linear_ema_step(&st->in_y, &cfg->in_coeffs, awv_y, dt_ms_q16);
        }
        ra_lut_index(awv_x, cfg->lut_step_q16, cfg->lut_max_q16, ix, fx);
        ra_lut_index(awv_y, cfg->lut_step_q16, cfg->lut_max_q16, iy, fy);
        *single_scale = 0;

        /* Telemetry: the per-axis speeds that just indexed the curve. */
        st->tele_speed_x_q16 = awv_x;
        st->tele_speed_y_q16 = awv_y;
        st->tele_speed_combined_q16 = ra_magnitude_q16(awv_x, awv_y);
    } else {
        /* Whole: one aggregate speed, one accel_x scale for the whole vector. */
        __s32 S;
        if (cfg->dist_mode == RA_DIST_MAX)
            S = awv_x > awv_y ? awv_x : awv_y;
        else
            /* euclidean (agent rejects Lp, so RA_DIST_LP never reaches the kernel) */
            S = ra_magnitude_q16(awv_x, awv_y);

        /* smooth the aggregate speed via the input EMA (calc_speed_whole) */
        if (cfg->flags & RA_F_SMOOTH_INPUT)
            S = ra_linear_ema_step(&st->in_x, &cfg->in_coeffs, S, dt_ms_q16);
        ra_lut_index(S, cfg->lut_step_q16, cfg->lut_max_q16, ix, fx);
        *iy = *ix;            /* y reuses the accel_x curve; raw_y is discarded */
        *fy = *fx;
        *single_scale = 1;

        /* Telemetry: the aggregate speed that indexed the curve, plus the
         * pre-combine components (informational; the agent uses combined here). */
        st->tele_speed_combined_q16 = S;
        st->tele_speed_x_q16 = awv_x;
        st->tele_speed_y_q16 = awv_y;
    }
}

/* Stage 2: working vector + raw curve scales -> output vector in Q16.16 (carry
 * is the caller's). Applies range weighting, output-DPI scaling, and directional
 * multipliers. Whole mode: both axes use the accel_x scale and weight_q16;
 * separate mode: each axis its own. */
RA_FP_INLINE void ra_post_lut(const struct ra_bpf_config *cfg,
                              struct ra_bpf_state *st,
                              __s64 inx_q16, __s64 iny_q16,
                              __s32 raw_x, __s32 raw_y, __u8 single_scale,
                              __s32 weight_q16, __s32 dt_ms_q16,
                              __s64 *out_x_q16, __s64 *out_y_q16)
{
    /* Range-weighted curve scale 1 + (f-1)*weight, smoothed before output-DPI
     * scaling (modify). Whole mode smooths one scale via sc_x for both axes. */
    __s32 ws_x, ws_y;
    if (single_scale) {
        __s32 ws = RA_Q16_ONE + ra_mul_q16(raw_x - RA_Q16_ONE, weight_q16);
        if (cfg->flags & RA_F_SMOOTH_SCALE)
            ws = ra_simple_ema_step(&st->sc_x, &cfg->scale_coeffs, ws, dt_ms_q16);
        ws_x = ws;
        ws_y = ws;
    } else {
        ws_x = RA_Q16_ONE + ra_mul_q16(raw_x - RA_Q16_ONE, cfg->range_w_x_q16);
        ws_y = RA_Q16_ONE + ra_mul_q16(raw_y - RA_Q16_ONE, cfg->range_w_y_q16);
        if (cfg->flags & RA_F_SMOOTH_SCALE) {
            ws_x = ra_simple_ema_step(&st->sc_x, &cfg->scale_coeffs, ws_x, dt_ms_q16);
            ws_y = ra_simple_ema_step(&st->sc_y, &cfg->scale_coeffs, ws_y, dt_ms_q16);
        }
    }

    /* Apply the smoothed scale -> counts, the value the output smoother sees. */
    __s64 sx = (inx_q16 * (__s64)ws_x) >> RA_Q16_SHIFT;
    __s64 sy = (iny_q16 * (__s64)ws_y) >> RA_Q16_SHIFT;

    if (cfg->flags & RA_F_SMOOTH_OUTPUT) {
        if (single_scale) {
            /* Whole: smooth the output magnitude, rescale both axes by smoothed/mag. */
            __s32 mag = ra_magnitude_q16(sx, sy);
            if (mag > 0) {
                __s32 sm = ra_linear_ema_step(&st->out_x, &cfg->out_coeffs,
                                              mag, dt_ms_q16);
                __s32 ratio = ra_div_q16(sm, mag);
                sx = (sx * (__s64)ratio) >> RA_Q16_SHIFT;
                sy = (sy * (__s64)ratio) >> RA_Q16_SHIFT;
            }
        } else {
            /* Separate: smooth |component|, reapply sign (copysign). */
            __s32 ax = ra_sat_s32(sx < 0 ? -sx : sx);
            __s32 ay = ra_sat_s32(sy < 0 ? -sy : sy);
            __s32 smx = ra_linear_ema_step(&st->out_x, &cfg->out_coeffs,
                                           ax, dt_ms_q16);
            __s32 smy = ra_linear_ema_step(&st->out_y, &cfg->out_coeffs,
                                           ay, dt_ms_q16);
            sx = sx < 0 ? -(__s64)smx : (__s64)smx;
            sy = sy < 0 ? -(__s64)smy : (__s64)smy;
        }
    }

    /* Output-DPI scaling, then yx_output_dpi_ratio on Y (modify order). */
    __s32 dpi_x = cfg->output_dpi_adj_q16;
    __s32 dpi_y = ra_mul_q16(cfg->output_dpi_adj_q16, cfg->yx_ratio_q16);
    __s64 ox = (sx * (__s64)dpi_x) >> RA_Q16_SHIFT;
    __s64 oy = (sy * (__s64)dpi_y) >> RA_Q16_SHIFT;

    /* Directional output DPI: scale a component only when its output is negative. */
    if ((cfg->flags & RA_F_APPLY_DIR_MUL_X) && ox < 0)
        ox = (ox * (__s64)cfg->lr_ratio_q16) >> RA_Q16_SHIFT;
    if ((cfg->flags & RA_F_APPLY_DIR_MUL_Y) && oy < 0)
        oy = (oy * (__s64)cfg->ud_ratio_q16) >> RA_Q16_SHIFT;

    *out_x_q16 = ox;
    *out_y_q16 = oy;
}

/* Full per-packet pipeline against a flat LUT (host tests; the kernel mirrors
 * this with map-based LUT reads). */
RA_FP_INLINE void ra_modify_q16_flat(const struct ra_bpf_config *cfg,
                                     struct ra_bpf_state *st,
                                     const __s32 *lut_x, const __s32 *lut_y,
                                     __s32 dx, __s32 dy, __s32 dt_ms_q16,
                                     __s64 *out_x_q16, __s64 *out_y_q16)
{
    __s64 inx, iny;
    __u32 ix, iy;
    __s32 fx, fy;
    __u8 single_scale;
    __s32 weight;
    ra_pre_lut(cfg, st, dx, dy, dt_ms_q16, &inx, &iny, &ix, &fx, &iy, &fy,
               &single_scale, &weight);

    __s32 raw_x = ra_lut_sample(lut_x, ix, fx);
    __s32 raw_y = ra_lut_sample(lut_y, iy, fy);

    ra_post_lut(cfg, st, inx, iny, raw_x, raw_y, single_scale, weight, dt_ms_q16,
                out_x_q16, out_y_q16);
}

/* Carry-accumulated Q16.16 -> integer emission. Adds the saved carry, splits
 * off integer counts, keeps the new fraction. Returns 1 and writes out_x/out_y
 * (updating carry), or 0 to drop the packet. The drop mirrors driver.cpp
 * ValidCarry: a carry outside [-1, 1) is refused, carry left untouched. */
RA_FP_INLINE int ra_emit_q16(struct ra_bpf_state *st,
                             __s64 acc_x_q16, __s64 acc_y_q16,
                             __s32 *out_x, __s32 *out_y)
{
    __s64 out_x_q16 = acc_x_q16 + (__s64)st->carry_x_q16;
    __s64 out_y_q16 = acc_y_q16 + (__s64)st->carry_y_q16;

    __s32 ix = (__s32)(out_x_q16 >> RA_Q16_SHIFT);
    __s32 iy = (__s32)(out_y_q16 >> RA_Q16_SHIFT);

    __s32 new_carry_x = (__s32)(out_x_q16 - ((__s64)ix << RA_Q16_SHIFT));
    __s32 new_carry_y = (__s32)(out_y_q16 - ((__s64)iy << RA_Q16_SHIFT));

    if (new_carry_x >= RA_Q16_ONE || new_carry_x <= -RA_Q16_ONE) return 0;
    if (new_carry_y >= RA_Q16_ONE || new_carry_y <= -RA_Q16_ONE) return 0;

    st->carry_x_q16 = new_carry_x;
    st->carry_y_q16 = new_carry_y;
    *out_x = ix;
    *out_y = iy;
    return 1;
}

#endif /* RAWACCEL_FIXEDPOINT_H */
