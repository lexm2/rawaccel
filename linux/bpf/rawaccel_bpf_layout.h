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

/* Per-axis lookup tables (anisotropy). The agent fills these at apply-time. */
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
};

/* Per-device runtime state. One instance per BPF object load. */
struct ra_bpf_state {
    __u64 last_ts_ns;       /* bpf_ktime_get_ns() at the last packet */
    __s32 smoothed_v_q16;   /* smoothed |velocity| in Q16.16 in/s */
    __s32 carry_x_q16;      /* fractional carry that did not emit yet */
    __s32 carry_y_q16;
};

#endif /* RAWACCEL_BPF_LAYOUT_H */
