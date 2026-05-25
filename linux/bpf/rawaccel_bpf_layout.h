#ifndef RAWACCEL_BPF_LAYOUT_H
#define RAWACCEL_BPF_LAYOUT_H

/* Shared map layout between the BPF program and its userspace loader.
 *
 * Keep field types stdint-style so the same struct is consumable on both
 * sides without C++ <-> kernel-C dance. The BPF program treats them as
 * the natural-width kernel types via implicit conversion.
 */

/* On the BPF side vmlinux.h supplies the kernel fixed-width types
 * (__u8 / __s8 / ... __u64 / __s64).  On the userspace side we pull
 * <linux/types.h> for the same names so a single struct definition is
 * consumable in both translation units. */
#ifdef __BPF__
#include "vmlinux.h"
#else
#include <linux/types.h>
#endif

/* Q16.16 fixed-point: 16 integer bits + 16 fractional bits. All scale,
 * velocity, and carry values are stored at this precision. */
#define RA_Q16_SHIFT 16
#define RA_Q16_ONE   (1 << RA_Q16_SHIFT)

/* LUT density. 4096 entries spanning the agent-chosen velocity range
 * gives a quantization step <= 0.025 in/s at typical NORMALIZED_DPI. */
#define RA_LUT_SIZE 4096

/* Bumped whenever ra_bpf_config / ra_bpf_state layout or semantics change so
 * a stale agent and a freshly built object cannot silently disagree. */
#define RA_CONFIG_VERSION 1

/* modifier_flags / speed_processor_flags mirror (see common/rawaccel.hpp).
 * Reserved bits are emitted as 0 by the agent until the matching kernel path
 * lands; Phase 0 reads none of them. */
#define RA_F_APPLY_ROTATE        (1u << 0)
#define RA_F_COMPUTE_REF_ANGLE   (1u << 1)
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

/* The two per-axis lookup tables (anisotropy) store the RAW curve scale
 * f(speed) in Q16.16. Range/domain weighting, output-DPI scaling, and the
 * directional multipliers live in ra_bpf_config and are applied in-kernel
 * around the LUT, mirroring common/rawaccel.hpp's modifier::modify. */
struct ra_bpf_config {
    /* HID report layout, copied verbatim from
     * linux/agent/hid_descriptor.hpp's BpfMouseLayout. */
    __u8  report_id;        /* 0 if reports have no ID prefix byte */
    __u8  dx_byte_offset;
    __u8  dx_byte_size;     /* 1 or 2 */
    __u8  dy_byte_offset;
    __u8  dy_byte_size;     /* 1 or 2 */
    __u8  _pad[3];

    /* Velocity-domain configuration in Q16.16. */
    __s32 dpi_norm_q16;     /* counts/ms -> in/s normalization factor */
    __s32 smooth_alpha_q16; /* per-packet EMA coefficient, 0..RA_Q16_ONE */
    __s32 lut_step_q16;     /* velocity per LUT step, in/s in Q16.16 */
    __s32 lut_max_q16;      /* clamp velocities at or above this value */

    /* modifier_flags bitfield + distance mode. Reserved for Phase 1; the
     * agent fills them now so the layout is stable. */
    __u32 flags;
    __u8  dist_mode;
    __u8  config_version;   /* RA_CONFIG_VERSION */
    __u8  _pad2[2];

    /* Per-axis weighting and output scaling, all Q16.16. Moved out of the
     * LUT (which now holds the raw curve) and applied in-kernel. */
    __s32 range_w_x_q16;    /* range_weights.x */
    __s32 range_w_y_q16;    /* range_weights.y */
    __s32 domain_w_x_q16;   /* domain_weights.x (pre-curve speed scale) */
    __s32 domain_w_y_q16;   /* domain_weights.y */
    __s32 output_dpi_adj_q16; /* output_dpi / NORMALIZED_DPI */
    __s32 yx_ratio_q16;     /* yx_output_dpi_ratio (applied to Y) */
};

#ifndef __BPF__
/* Host side is always C++ (agent + tests); BPF side skips this. Catches
 * accidental padding/layout drift between agent and kernel. */
static_assert(sizeof(struct ra_bpf_config) == 56,
              "ra_bpf_config layout changed; update kernel + agent in lockstep");
#endif

/* Per-device runtime state. One instance per BPF object load. */
struct ra_bpf_state {
    __u64 last_ts_ns;       /* bpf_ktime_get_ns() at the last packet */
    __s32 smoothed_v_q16;   /* smoothed |velocity| in Q16.16 in/s */
    __s32 carry_x_q16;      /* fractional carry that did not emit yet */
    __s32 carry_y_q16;
};

#endif /* RAWACCEL_BPF_LAYOUT_H */
