#ifndef RAWACCEL_BPF_LAYOUT_H
#define RAWACCEL_BPF_LAYOUT_H

/* Shared map layout between the BPF program and its userspace loader.
 * stdint-style field types so one struct is consumable on both sides. */

/* Fixed-width types: vmlinux.h on the BPF side, <linux/types.h> on the host. */
#ifdef __BPF__
#include "vmlinux.h"
#else
#include <linux/types.h>
#endif

/* Q16.16 fixed-point: 16 integer + 16 fractional bits. */
#define RA_Q16_SHIFT 16
#define RA_Q16_ONE   (1 << RA_Q16_SHIFT)

/* Signed-integer saturation bounds (vmlinux.h is BTF-only, no S*_MAX macros). */
#define RA_S32_MAX   0x7fffffff
#define RA_S16_MAX   32767
#define RA_S16_MIN   (-32768)
#define RA_S8_MAX    127
#define RA_S8_MIN    (-128)

/* LUT density: 4096 entries -> step <= 0.025 in/s at typical NORMALIZED_DPI. */
#define RA_LUT_SIZE 4096

/* BPF map names: single source of truth for the BPF<->host map-name contract.
 * One macro drives both the in-program map identifier (the BPF side declares
 * `} RA_MAP_CONFIG SEC(".maps")` and dereferences `&RA_MAP_CONFIG`) and the
 * host-side lookup string (find_map_by_name(obj, RA_MAP_NAME(RA_MAP_CONFIG))),
 * so a rename can't desync the two files. RA_MAP_NAME needs two levels so the
 * argument macro expands before being stringized. */
#define RA_STRINGIFY_(x) #x
#define RA_MAP_NAME(x)   RA_STRINGIFY_(x)
#define RA_MAP_CONFIG    ra_config
#define RA_MAP_STATE     ra_state
#define RA_MAP_LUT_X     ra_lut_x
#define RA_MAP_LUT_Y     ra_lut_y
#define RA_MAP_OPS       rawaccel_ops

/* Bump on any ra_bpf_config/ra_bpf_state layout or semantics change.
 * v4: added the tele_speed_* telemetry fields to ra_bpf_state. */
#define RA_CONFIG_VERSION 4

/* modifier_flags / speed_processor_flags mirror (common/rawaccel.hpp). */
#define RA_F_APPLY_ROTATE        (1u << 0)
/* bit 1 reserved (was RA_F_COMPUTE_REF_ANGLE) */
#define RA_F_APPLY_SNAP          (1u << 2)
#define RA_F_CLAMP_SPEED         (1u << 3)
#define RA_F_APPLY_DIR_WEIGHT    (1u << 4)
#define RA_F_APPLY_DIR_MUL_X     (1u << 5)
#define RA_F_APPLY_DIR_MUL_Y     (1u << 6)
#define RA_F_SMOOTH_INPUT        (1u << 7)
#define RA_F_SMOOTH_SCALE        (1u << 8)
#define RA_F_SMOOTH_OUTPUT       (1u << 9)

/* dist_mode values mirror common/rawaccel.hpp's distance_mode. */
#define RA_DIST_EUCLIDEAN 0
#define RA_DIST_SEPARATE  1
#define RA_DIST_MAX       2
#define RA_DIST_LP        3

/* Linear EMA coefficients: log2(coeff) Q16.16 (negative) for the level and
 * trend window/cutoff pairs. Struct so it passes by pointer (BPF: <=5 args). */
struct ra_linear_ema_coeffs {
    __s32 log2_win;
    __s32 log2_cut;
    __s32 log2_trw;
    __s32 log2_trc;
};

/* Simple EMA coefficients: log2(coeff) Q16.16 for the window/cutoff pair. No trend. */
struct ra_simple_ema_coeffs {
    __s32 log2_win;
    __s32 log2_cut;
};

/* Per-axis LUTs hold the raw curve scale f(speed) in Q16.16; weighting,
 * output-DPI, and directional multipliers live here and apply in-kernel. */
struct ra_bpf_config {
    /* HID report layout, from hid_descriptor.hpp's BpfMouseLayout. */
    __u8  report_id;        /* 0 if reports have no ID prefix byte */
    __u8  dx_byte_offset;
    __u8  dx_byte_size;     /* 1 or 2 */
    __u8  dy_byte_offset;
    __u8  dy_byte_size;     /* 1 or 2 */
    __u8  _pad[3];

    /* Velocity-domain configuration in Q16.16. */
    __s32 dpi_norm_q16;     /* counts/ms -> in/s normalization factor */
    __s32 lut_step_q16;     /* velocity per LUT step, in/s in Q16.16 */
    __s32 lut_max_q16;      /* clamp velocities at or above this value */

    /* modifier_flags bitfield + distance mode. */
    __u32 flags;
    __u8  dist_mode;
    __u8  config_version;   /* RA_CONFIG_VERSION */
    __u8  _pad2[2];

    /* Per-axis weighting and output scaling, Q16.16, applied in-kernel. */
    __s32 range_w_x_q16;    /* range_weights.x */
    __s32 range_w_y_q16;    /* range_weights.y */
    __s32 domain_w_x_q16;   /* domain_weights.x (pre-curve speed scale) */
    __s32 domain_w_y_q16;   /* domain_weights.y */
    __s32 output_dpi_adj_q16; /* output_dpi / NORMALIZED_DPI */
    __s32 yx_ratio_q16;     /* yx_output_dpi_ratio (applied to Y) */

    /* Directional output-DPI multipliers, applied when the output is negative. */
    __s32 lr_ratio_q16;     /* lr_output_dpi_ratio (X when output < 0) */
    __s32 ud_ratio_q16;     /* ud_output_dpi_ratio (Y when output < 0) */

    /* Rotation direction {cos, sin}(degrees_rotation), applied first when RA_F_APPLY_ROTATE. */
    __s32 rot_cos_q16;
    __s32 rot_sin_q16;

    /* Speed clamp bounds, Q16.16 in/s, applied before the curve when RA_F_CLAMP_SPEED. */
    __s32 speed_min_q16;
    __s32 speed_max_q16;

    /* Angle-snap thresholds as tangents (Q16.16) so the kernel snaps with a
     * multiply, not an atan: snap_lo = tan(snap) -> X, snap_hi = tan(pi/2-snap) -> Y. */
    __s32 snap_lo_tan_q16;
    __s32 snap_hi_tan_q16;

    /* Per-packet dt clamp, Q16.16 ms (device_config::clamp, default 0.0625..100). */
    __s32 time_min_q16;
    __s32 time_max_q16;

    /* input_speed_smoother coeffs (linear EMA), used when RA_F_SMOOTH_INPUT. */
    struct ra_linear_ema_coeffs in_coeffs;

    /* scale_smoother coeffs (simple EMA), used when RA_F_SMOOTH_SCALE. */
    struct ra_simple_ema_coeffs scale_coeffs;

    /* output_speed_smoother coeffs (linear EMA), used when RA_F_SMOOTH_OUTPUT. */
    struct ra_linear_ema_coeffs out_coeffs;
};

#ifndef __BPF__
/* Host-only: catches padding/layout drift between agent and kernel. */
static_assert(sizeof(struct ra_bpf_config) == 132,
              "ra_bpf_config layout changed; update kernel + agent in lockstep");
#endif

/* Linear EMA accumulators: level + trend, each a window/cutoff pair, s64 Q16.16
 * (extended range so trend*time can't overflow s32). */
struct ra_linear_ema_state {
    __s64 win;
    __s64 cut;
    __s64 win_tr;
    __s64 cut_tr;
};

/* Simple EMA smoother accumulators: a window/cutoff level pair, __s64 Q16.16. */
struct ra_simple_ema_state {
    __s64 win;
    __s64 cut;
};

/* Per-device runtime state. One instance per BPF object load. */
struct ra_bpf_state {
    __u64 last_ts_ns;       /* bpf_ktime_get_ns() at the last packet */
    __s32 carry_x_q16;      /* fractional carry that did not emit yet */
    __s32 carry_y_q16;

    /* input_speed_smoother state. Whole: in_x; separate: in_x/in_y per axis. */
    struct ra_linear_ema_state in_x;
    struct ra_linear_ema_state in_y;

    /* scale_smoother state. Whole: sc_x; separate: sc_x/sc_y per axis. */
    struct ra_simple_ema_state sc_x;
    struct ra_simple_ema_state sc_y;

    /* output_speed_smoother state. Whole: out_x; separate: out_x/out_y per axis. */
    struct ra_linear_ema_state out_x;
    struct ra_linear_ema_state out_y;

    /* Telemetry: speed (Q16.16 in/s, domain-weighted) handed to the LUT index, all three
     * written every packet for the agent's stats RPC. Separate: x/y per axis, combined =
     * magnitude; whole: combined = aggregate S, x/y = pre-combine components. Read-only here. */
    __s32 tele_speed_x_q16;
    __s32 tele_speed_y_q16;
    __s32 tele_speed_combined_q16;
    __s32 _tele_pad;        /* keep 8-byte alignment / explicit tail padding */
};

#ifndef __BPF__
static_assert(sizeof(struct ra_bpf_state) == 192,
              "ra_bpf_state layout changed; update kernel + agent in lockstep");
#endif

#endif /* RAWACCEL_BPF_LAYOUT_H */
