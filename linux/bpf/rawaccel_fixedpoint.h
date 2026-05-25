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

/* Saturate a 64-bit value into signed 32-bit range. Speeds past this are far
 * beyond lut_max and get clamped at index time anyway. (Defined early so the
 * divide can reuse it.) */
RA_FP_INLINE __s32 ra_sat_s32(__s64 v)
{
    if (v > 0x7fffffff) return 0x7fffffff;
    if (v < -0x7fffffff) return -0x7fffffff;
    return (__s32)v;
}

/* Q16.16 divide: (num / den) in Q16.16. The BPF verifier rejects signed
 * division, so the magnitudes are divided as u64 and the sign reapplied.
 * den == 0 returns 0; the result saturates to s32. den == RA_Q16_ONE returns
 * num exactly, so dividing by a unit dt is a no-op. */
RA_FP_INLINE __s32 ra_div_q16(__s32 num, __s32 den)
{
    if (den == 0) return 0;
    int neg = (num < 0) ^ (den < 0);
    __u64 n = (__u64)(num < 0 ? -(__s64)num : (__s64)num);
    __u64 d = (__u64)(den < 0 ? -(__s64)den : (__s64)den);
    __u64 q = (n << RA_Q16_SHIFT) / d;
    return ra_sat_s32(neg ? -(__s64)q : (__s64)q);
}

/* ---- s64 Q16.16 (extended range) ----------------------------------- *
 * The EMA smoother accumulators are kept as __s64 holding Q16.16 values so the
 * linear smoother's trend*time term cannot overflow s32 (trend can be large and
 * time spans up to ~100 ms). Multiplies keep the product in s64 -- no __int128
 * needed -- and divides reuse the unsigned-magnitude trick (the verifier rejects
 * signed division). */
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

/* 2^x in Q16.16 for x <= 0. The per-packet EMA decay is 2^(dt * log2(coeff))
 * with coeff in (0,1), so the exponent is always <= 0. Splits x into a floor
 * and a fraction: 2^x = 2^frac >> (-floor). 2^frac for frac in [0,1) is a
 * minimax cubic that hits 1 at 0 and 2 at 1 (so there is no jump across integer
 * boundaries), accurate to < 1e-3. A shift past the Q16.16 range underflows to
 * 0, i.e. a long dt drives the coefficient to 0 and the alpha below to 1 (full
 * tracking, the "reset after a pause" behavior). __noinline: verified once. */
RA_FP_NOINLINE __s32 ra_exp2_q16(__s32 x_q16)
{
    if (x_q16 >= 0) return RA_Q16_ONE;            /* domain is x <= 0; 2^0 = 1 */
    __s32 ipart = x_q16 >> RA_Q16_SHIFT;          /* floor toward -inf, <= -1 */
    __s32 frac  = x_q16 - (ipart << RA_Q16_SHIFT);/* in [0, RA_Q16_ONE) */
    int shift = -ipart;                            /* >= 1 right shifts */
    if (shift >= 32) return 0;

    /* p(frac) ~= 2^frac via Horner: 1 + f(a + f(b + f c)), coefficients in
     * Q16.16 (a=0.696066, b=0.224494, c=0.079442). */
    __s32 p = 14714 + ra_mul_q16(5206, frac);     /* b + c f */
    p = 45620 + ra_mul_q16(p, frac);              /* a + f(b + c f) */
    p = RA_Q16_ONE + ra_mul_q16(p, frac);         /* 1 + f(...) -> [ONE, 2 ONE) */
    return (__s32)((__u32)p >> shift);
}

/* Per-packet EMA alpha = 1 - 2^(dt * log2(coeff)), in Q16.16 [0, ONE]. The
 * agent precomputes log2(coeff) (negative) so the kernel never needs log/pow;
 * here dt scales it and ra_exp2_q16 turns it back into the decay. Mirrors the
 * `1 - pow(coeff, time)` in common/rawaccel.hpp's smoothers. */
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

/* Rotate a Q16.16 vector by the precomputed direction {cos, sin}. Mirrors
 * common/math-vec2.hpp rotate(): {x*cos - y*sin, x*sin + y*cos}. */
RA_FP_INLINE void ra_rotate_q16(__s64 *x, __s64 *y, __s32 cos_q16, __s32 sin_q16)
{
    __s64 rx = (*x * (__s64)cos_q16 - *y * (__s64)sin_q16) >> RA_Q16_SHIFT;
    __s64 ry = (*x * (__s64)sin_q16 + *y * (__s64)cos_q16) >> RA_Q16_SHIFT;
    *x = rx;
    *y = ry;
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
 * (magnitude * eff_dpi_norm, in/s) to [speed_min, speed_max] and rescale the
 * vector by the resulting ratio. eff_dpi_norm already folds in 1/dt, so the
 * clamp threshold is compared in real in/s (modify's magnitude * ips_factor).
 * __noinline: verified once. */
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

    /* ratio = clamped / speed, Q16.16. Both operands are positive, so the
     * division is unsigned (the verifier rejects signed division). */
    __u64 ratio = ((__u64)(__u32)clamped << RA_Q16_SHIFT) / (__u64)(__u32)speed;
    *inx = (*inx * (__s64)ratio) >> RA_Q16_SHIFT;
    *iny = (*iny * (__s64)ratio) >> RA_Q16_SHIFT;
}

/* Angle snapping (modifier::modify lines 331-342): if the movement's angle to
 * an axis is within degrees_snap, collapse the working vector onto that axis,
 * preserving magnitude. The decision avoids atan by comparing |y|/|x| against
 * precomputed tangents (atan is monotone increasing):
 *   ref_angle < snap         <=> |y| < tan(snap)        * |x|  -> snap to X
 *   ref_angle > pi/2 - snap   <=> |y| > tan(pi/2 - snap) * |x|  -> snap to Y
 * Cross-multiplied to stay in integer math (s64 holds |comp| << 16 vs a Q16
 * tangent times |comp|). y == 0 (already on X) returns early; x == 0 (already
 * on Y) collapses to Y as a no-op, both matching modify. __noinline: verified
 * once. */
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

/* Trend dampening 0.75 in Q16.16 (linear_ema_smoother::trendDampening). */
#define RA_TREND_DAMP_Q16 49152

/* Linear EMA smoother, the fixed-point mirror of common/rawaccel.hpp's
 * linear_ema_smoother::smooth (lines 126-162). Keeps a level and a trend, each
 * as a window/cutoff pair, in __s64 Q16.16 accumulators (win/cut/win_tr/cut_tr).
 * Per packet: dampen the trend, extrapolate the level by trend*dt, pull the
 * level toward the sample by the dt-adaptive alpha, clamp >= 0, then update the
 * trend from the level change. Returns min(window, cutoff) saturated to s32.
 * Used for the input-speed and output-speed stages (their trend halflives, and
 * thus log2 coefficients, differ; the agent supplies the four log2(coeff)s).
 * __noinline: verified once. */
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

    /* dampen trends, then extrapolate the level along the dampened trend. */
    s->win_tr = ra_mul_q16_s64(s->win_tr, RA_TREND_DAMP_Q16);
    s->cut_tr = ra_mul_q16_s64(s->cut_tr, RA_TREND_DAMP_Q16);
    s->win += ra_mul_q16_s64(s->win_tr, dt_ms_q16);
    s->cut += ra_mul_q16_s64(s->cut_tr, dt_ms_q16);

    /* level EMA toward the sample. */
    s->win += ra_mul_q16_s64((__s64)sample_q16 - s->win, a_win);
    s->cut += ra_mul_q16_s64((__s64)sample_q16 - s->cut, a_cut);

    /* don't let the trend carry the level below 0. */
    if (s->win < 0) s->win = 0;
    if (s->cut < 0) s->cut = 0;

    /* update the trend from this packet's level change per unit time. */
    __s64 new_trw = ra_div_q16_s64(s->win - old_win, dt_ms_q16);
    __s64 new_trc = ra_div_q16_s64(s->cut - old_cut, dt_ms_q16);
    s->win_tr += ra_mul_q16_s64(new_trw - s->win_tr, a_trw);
    s->cut_tr += ra_mul_q16_s64(new_trc - s->cut_tr, a_trc);

    __s64 m = s->win < s->cut ? s->win : s->cut;
    return ra_sat_s32(m);
}

/* Simple EMA smoother, the fixed-point mirror of common/rawaccel.hpp's
 * simple_ema_smoother::smooth (lines 79-90). A window/cutoff level pair pulled
 * toward the sample by the dt-adaptive alpha; no trend term and no zero clamp
 * (the smoothed scale stays positive). Returns min(window, cutoff). Used for
 * the scale-smoothing stage. __noinline: verified once. */
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

/* The pipeline is split around the LUT fetch so the kernel program and the
 * host tests share everything except how the table is read (map lookups vs a
 * flat array). ra_pre_lut and ra_post_lut are the shared, host-tested halves;
 * the working vector (inx/iny, Q16.16) carries the per-packet transforms that
 * later phases add (rotation, snap, speed clamp) from one half to the other.
 *
 * Stage 1: raw counts -> working vector + per-axis curve LUT indices. */
RA_FP_INLINE void ra_pre_lut(const struct ra_bpf_config *cfg,
                             struct ra_bpf_state *st,
                             __s32 dx, __s32 dy, __s32 dt_ms_q16,
                             __s64 *inx_q16, __s64 *iny_q16,
                             __u32 *ix, __s32 *fx, __u32 *iy, __s32 *fy,
                             __u8 *single_scale, __s32 *weight_q16)
{
    /* Working vector starts as the raw counts in Q16.16, then carries each
     * per-packet transform (rotation, snap, speed clamp) to the LUT stage. */
    __s64 inx = (__s64)dx << RA_Q16_SHIFT;
    __s64 iny = (__s64)dy << RA_Q16_SHIFT;

    /* Fold the real per-packet dt into the velocity normalization. modify uses
     * speed = component * dpi_factor / time (ips_factor); here dpi_norm plays
     * dpi_factor's role, so eff_dpi_norm = dpi_norm / dt_ms gives in/s at the
     * actual polling interval. dt_ms_q16 is already clamped by the caller, and
     * ra_div_q16(x, RA_Q16_ONE) == x, so a 1 ms packet leaves velocity intact
     * (matching the Phase-1 behavior). The output vector below stays in raw
     * counts: dt scales only the speed used for indexing and the clamp. */
    __s32 eff_dpi_norm_q16 = ra_div_q16(cfg->dpi_norm_q16, dt_ms_q16);

    /* Rotation is the first transform in modifier::modify. Velocity and the
     * curve index are derived from the rotated vector. */
    if (cfg->flags & RA_F_APPLY_ROTATE)
        ra_rotate_q16(&inx, &iny, cfg->rot_cos_q16, cfg->rot_sin_q16);

    /* Angle snapping collapses near-axis movement onto the axis before the
     * reference angle and curve are derived; the directional-weight block below
     * then reads the snapped vector, yielding weight = range_w_x (snapped to X)
     * or range_w_y (snapped to Y) for free. */
    if (cfg->flags & RA_F_APPLY_SNAP)
        ra_snap(cfg, &inx, &iny);

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
        ra_clamp_speed(cfg, eff_dpi_norm_q16, &inx, &iny);

    *inx_q16 = inx;
    *iny_q16 = iny;

    /* Per-axis abs weighted velocity (modify's abs_weighted_vel), in real in/s
     * via the dt-folded normalization. */
    __s32 awv_x = ra_axis_speed_q16(inx, eff_dpi_norm_q16, cfg->domain_w_x_q16);
    __s32 awv_y = ra_axis_speed_q16(iny, eff_dpi_norm_q16, cfg->domain_w_y_q16);

    if (cfg->dist_mode == RA_DIST_SEPARATE) {
        /* Separate: each axis indexes its own curve at its own speed, smoothed
         * by its own input-speed EMA (calc_speed_separate). */
        if (cfg->flags & RA_F_SMOOTH_INPUT) {
            awv_x = ra_linear_ema_step(&st->in_x, &cfg->in_coeffs, awv_x, dt_ms_q16);
            awv_y = ra_linear_ema_step(&st->in_y, &cfg->in_coeffs, awv_y, dt_ms_q16);
        }
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

        /* calc_speed_whole smooths the single aggregate speed via smoother_x's
         * input EMA. With RA_F_SMOOTH_INPUT clear the raw speed indexes
         * directly (halflife 0 -> no smoothing). */
        if (cfg->flags & RA_F_SMOOTH_INPUT)
            S = ra_linear_ema_step(&st->in_x, &cfg->in_coeffs, S, dt_ms_q16);
        ra_lut_index(S, cfg->lut_step_q16, cfg->lut_max_q16, ix, fx);
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
                              struct ra_bpf_state *st,
                              __s64 inx_q16, __s64 iny_q16,
                              __s32 raw_x, __s32 raw_y, __u8 single_scale,
                              __s32 weight_q16, __s32 dt_ms_q16,
                              __s64 *out_x_q16, __s64 *out_y_q16)
{
    /* Range-weighted curve scale 1 + (f - 1)*weight (callback_template). This
     * is the value the scale smoother operates on, BEFORE output-DPI scaling
     * (modify smooths scale_x/scale_y, then multiplies in by the dpi
     * adjustment). Whole mode smooths a single scale via sc_x and applies it to
     * both axes; separate mode smooths each axis with its own state. */
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

    /* Output-DPI scaling then the per-axis trailing factor (yx_output_dpi_ratio
     * on Y), following the (smoothed) scale, matching modify's order. */
    __s32 eff_x = ra_mul_q16(ws_x, cfg->output_dpi_adj_q16);
    __s32 eff_y = ra_mul_q16(ra_mul_q16(ws_y, cfg->output_dpi_adj_q16),
                             cfg->yx_ratio_q16);

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

/* Carry-accumulated Q16.16 -> integer emission. Adds the retained fractional
 * carry to the post-LUT output, splits off the integer counts to emit, and
 * keeps the new fraction for the next packet. Returns 1 (and writes
 * *out_x / *out_y, updating carry) when the packet should be emitted, or 0 to
 * drop it. The drop is the ValidCarry mirror of driver/driver.cpp:37-42, which
 * refuses a carry outside [-1, 1) rather than emit a surprising spike; the
 * arithmetic >> floors toward -inf so the remainder is always in
 * [0, RA_Q16_ONE) and the bounds check is the defensive guard the driver
 * carries. On a drop, carry is left untouched. Shared so the BPF program and
 * the host sequence tests run the exact same emission. */
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
