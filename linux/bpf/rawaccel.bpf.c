/* Rawaccel HID-BPF kernel program.
 *
 * Rewrites the dx/dy bytes of an incoming mouse HID report using a
 * precomputed Q16.16 lookup table plus a fixed-point exponential moving
 * average on velocity. One BPF object per attached device: each load
 * binds its own state, config, and LUT maps to a single hid_id.
 *
 * Pipeline mirrors driver/driver.cpp:84-131 in fixed point:
 *   parse(dx, dy) -> |v| -> EMA -> LUT[scale_x], LUT[scale_y]
 *                 -> per-axis (dx * scale + carry) >> 16
 *                 -> write_back, save new carry
 *
 * Verifier defenses: no floats, no dynamic loops, LUT indices masked to
 * (RA_LUT_SIZE - 1), report-buffer access via hid_bpf_get_data() with a
 * compile-time bounded size argument.
 */

/* clang -target bpf defines __BPF__ for us, so the layout header just
 * needs to test the macro; no manual define here. */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "rawaccel_bpf_layout.h"

char LICENSE[] SEC("license") = "GPL";

/* hid_bpf kfunc prototypes. The kernel exports these via __ksym; bpf_helpers
 * pulls them in by extern declaration. */
extern __u8 *hid_bpf_get_data(struct hid_bpf_ctx *ctx,
                              unsigned int offset,
                              const size_t rdwr_buf_size) __ksym;

/* ---- Map declarations ----------------------------------------------- */

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct ra_bpf_config);
} ra_config SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct ra_bpf_state);
} ra_state SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, RA_LUT_SIZE);
    __type(key, __u32);
    __type(value, __s32);
} ra_lut_x SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, RA_LUT_SIZE);
    __type(key, __u32);
    __type(value, __s32);
} ra_lut_y SEC(".maps");

/* ---- Helpers -------------------------------------------------------- */

static __always_inline __s32 abs_s32(__s32 v)
{
    return v < 0 ? -v : v;
}

static __always_inline __s32 q16_lookup(void *map, __u32 idx)
{
    __u32 k = idx & (RA_LUT_SIZE - 1);
    __s32 *p = bpf_map_lookup_elem(map, &k);
    return p ? *p : RA_Q16_ONE;
}

/* Linear interpolation between two Q16.16 LUT entries.
 *   frac is the fractional part of (smoothed_v / step), already shifted
 *   left by RA_Q16_SHIFT and masked to the low 16 bits.
 */
static __always_inline __s32 q16_lerp(__s32 a, __s32 b, __s32 frac)
{
    return a + (__s32)(((__s64)(b - a) * frac) >> RA_Q16_SHIFT);
}

/* Read a signed 8 or 16 bit value from a known offset in the report
 * buffer. The verifier needs to see fixed sizes per branch. */
static __always_inline __s32 read_signed(const __u8 *p, __u32 size)
{
    if (size == 2) {
        __s16 v = (__s16)(p[0] | ((__u16)p[1] << 8));
        return (__s32)v;
    }
    return (__s32)(__s8)p[0];
}

static __always_inline void write_signed(__u8 *p, __u32 size, __s32 v)
{
    if (size == 2) {
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        p[0] = (__u8)(v & 0xff);
        p[1] = (__u8)((v >> 8) & 0xff);
    } else {
        if (v >  127) v =  127;
        if (v < -128) v = -128;
        p[0] = (__u8)v;
    }
}

/* ---- struct_ops --------------------------------------------------- */

/* Verifier-bounded view of the report. Picked so a typical 16-bit
 * mouse with a 1-byte report ID prefix fits comfortably; the actual
 * accessed bytes are gated on the per-device config offsets. */
#define RA_REPORT_VIEW_BYTES 16

SEC("struct_ops/hid_device_event")
int BPF_PROG(rawaccel_hid_device_event,
             struct hid_bpf_ctx *hctx,
             enum hid_report_type report_type, __u64 source)
{
    __u32 zero = 0;
    struct ra_bpf_config *cfg = bpf_map_lookup_elem(&ra_config, &zero);
    if (!cfg) return 0;
    struct ra_bpf_state *st = bpf_map_lookup_elem(&ra_state, &zero);
    if (!st) return 0;

    __u8 *rpt = hid_bpf_get_data(hctx, 0, RA_REPORT_VIEW_BYTES);
    if (!rpt) return 0;

    /* If the device prefixes reports with an ID byte, only act on the
     * configured report type. Unknown reports flow through untouched. */
    if (cfg->report_id != 0 && rpt[0] != cfg->report_id) {
        return 0;
    }

    /* Bounds-check the offsets the verifier needs to see against the
     * view size we asked for above. */
    if (cfg->dx_byte_offset + cfg->dx_byte_size > RA_REPORT_VIEW_BYTES) return 0;
    if (cfg->dy_byte_offset + cfg->dy_byte_size > RA_REPORT_VIEW_BYTES) return 0;
    if (cfg->dx_byte_size != 1 && cfg->dx_byte_size != 2) return 0;
    if (cfg->dy_byte_size != 1 && cfg->dy_byte_size != 2) return 0;

    /* The verifier can't trust an arbitrary __u8* + __u8 arithmetic, so
     * mask offsets to a known small range. RA_REPORT_VIEW_BYTES is a
     * power of 2 minus 1 so the mask is cheap. */
    __u32 dx_off = cfg->dx_byte_offset & (RA_REPORT_VIEW_BYTES - 1);
    __u32 dy_off = cfg->dy_byte_offset & (RA_REPORT_VIEW_BYTES - 1);

    __s32 dx = read_signed(rpt + dx_off, cfg->dx_byte_size);
    __s32 dy = read_signed(rpt + dy_off, cfg->dy_byte_size);

    if (dx == 0 && dy == 0) {
        return 0;  /* idle packet, no carry update */
    }

    /* Instantaneous velocity magnitude approximated as max(|dx|, |dy|)
     * scaled by dpi_norm. The agent fills the LUT against the same
     * approximation so the two sides agree by construction. */
    __s32 ax = abs_s32(dx);
    __s32 ay = abs_s32(dy);
    __s32 v_counts = ax > ay ? ax : ay;
    __s32 v_q16 = (__s32)(((__s64)v_counts * cfg->dpi_norm_q16));

    /* EMA: smoothed += alpha * (sample - smoothed). All Q16.16. */
    __s64 diff = (__s64)v_q16 - (__s64)st->smoothed_v_q16;
    __s32 alpha = cfg->smooth_alpha_q16;
    if (alpha < 0) alpha = 0;
    if (alpha > RA_Q16_ONE) alpha = RA_Q16_ONE;
    st->smoothed_v_q16 += (__s32)((diff * alpha) >> RA_Q16_SHIFT);
    if (st->smoothed_v_q16 < 0) st->smoothed_v_q16 = 0;

    __s32 sv = st->smoothed_v_q16;
    if (cfg->lut_max_q16 > 0 && sv >= cfg->lut_max_q16) sv = cfg->lut_max_q16 - 1;

    /* LUT index + fractional weight. step is in/s per LUT bucket. The
     * verifier rejects signed division on BPF, but both operands are
     * non-negative here (sv was clamped to >= 0 above; step is forced
     * to RA_Q16_ONE if the userspace config left it unset). */
    __s32 step = cfg->lut_step_q16;
    if (step <= 0) step = RA_Q16_ONE;
    __u64 sv_u = (__u64)(__u32)sv;
    __u64 step_u = (__u64)(__u32)step;
    __u32 idx = (__u32)(sv_u / step_u);
    __u64 frac_u = sv_u % step_u;
    __s32 frac_q16 = (__s32)((frac_u << RA_Q16_SHIFT) / step_u);

    if (idx >= RA_LUT_SIZE - 1) idx = RA_LUT_SIZE - 2;

    __s32 sx_a = q16_lookup(&ra_lut_x, idx);
    __s32 sx_b = q16_lookup(&ra_lut_x, idx + 1);
    __s32 sy_a = q16_lookup(&ra_lut_y, idx);
    __s32 sy_b = q16_lookup(&ra_lut_y, idx + 1);
    __s32 scale_x = q16_lerp(sx_a, sx_b, frac_q16);
    __s32 scale_y = q16_lerp(sy_a, sy_b, frac_q16);

    /* Scaled, carry-accumulated output in Q16.16. */
    __s64 out_x_q16 = (__s64)dx * scale_x + (__s64)st->carry_x_q16;
    __s64 out_y_q16 = (__s64)dy * scale_y + (__s64)st->carry_y_q16;

    __s32 out_x = (__s32)(out_x_q16 >> RA_Q16_SHIFT);
    __s32 out_y = (__s32)(out_y_q16 >> RA_Q16_SHIFT);

    __s32 new_carry_x = (__s32)(out_x_q16 - ((__s64)out_x << RA_Q16_SHIFT));
    __s32 new_carry_y = (__s32)(out_y_q16 - ((__s64)out_y << RA_Q16_SHIFT));

    /* ValidCarry mirror (driver/driver.cpp:37-42): if the carry would land
     * outside the [-1, 1) interval, drop the packet rather than emit a
     * surprising spike. NaN cannot exist in Q16.16 so the check reduces to
     * the magnitude bounds. */
    if (new_carry_x >= RA_Q16_ONE || new_carry_x <= -RA_Q16_ONE) return 0;
    if (new_carry_y >= RA_Q16_ONE || new_carry_y <= -RA_Q16_ONE) return 0;

    st->carry_x_q16 = new_carry_x;
    st->carry_y_q16 = new_carry_y;

    write_signed(rpt + dx_off, cfg->dx_byte_size, out_x);
    write_signed(rpt + dy_off, cfg->dy_byte_size, out_y);

    st->last_ts_ns = bpf_ktime_get_ns();
    return 0;
}

SEC(".struct_ops.link")
struct hid_bpf_ops rawaccel_ops = {
    /* hid_id is patched at load time by the userspace loader (step 12). */
    .hid_id = 0,
    .hid_device_event = (void *)rawaccel_hid_device_event,
};
