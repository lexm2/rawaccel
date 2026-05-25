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

#include "rawaccel_fixedpoint.h"

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

/* The scalar math (abs, lerp, EMA, indexing, weighting) lives in
 * rawaccel_fixedpoint.h so host tests exercise the exact same code. The only
 * kernel-specific piece is the LUT fetch: a BPF array-map pointer cannot
 * stride past one element, so each entry is looked up on its own here and
 * fed into the shared ra_q16_lerp. */
static __always_inline __s32 q16_lookup(void *map, __u32 idx)
{
    __u32 k = idx & (RA_LUT_SIZE - 1);
    __s32 *p = bpf_map_lookup_elem(map, &k);
    return p ? *p : RA_Q16_ONE;
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

    /* Narrow byte sizes to {1, 2} so the size-branch in read_signed /
     * write_signed has a known shape. */
    if (cfg->dx_byte_size != 1 && cfg->dx_byte_size != 2) return 0;
    if (cfg->dy_byte_size != 1 && cfg->dy_byte_size != 2) return 0;

    /* Mask offsets to keep the verifier happy with __u8* + __u8
     * arithmetic. Then tighten unconditionally to (RA_REPORT_VIEW_BYTES
     * - 2): even when dx_byte_size == 1 we reserve room for two bytes,
     * because the verifier tracks dx_off independently of dx_byte_size
     * and will not infer 'dx_off+1 in range' from 'dx_off+size <= 16'
     * inside the size==2 branch. Losing one byte at the high end of the
     * view is harmless: real mouse descriptors put X/Y near the start
     * of the report, never at byte 15. */
    __u32 dx_off = cfg->dx_byte_offset & (RA_REPORT_VIEW_BYTES - 1);
    __u32 dy_off = cfg->dy_byte_offset & (RA_REPORT_VIEW_BYTES - 1);
    if (dx_off > RA_REPORT_VIEW_BYTES - 2) return 0;
    if (dy_off > RA_REPORT_VIEW_BYTES - 2) return 0;

    __s32 dx = read_signed(rpt + dx_off, cfg->dx_byte_size);
    __s32 dy = read_signed(rpt + dy_off, cfg->dy_byte_size);

    if (dx == 0 && dy == 0) {
        return 0;  /* idle packet, no carry update */
    }

    /* read_signed's 8- vs 16-bit branch leaves dx/dy with different value
     * ranges; without this the two ranges never merge and double the verifier
     * state count through every downstream branch. The barrier collapses them
     * to one unbounded-scalar state (dx/dy are only scaled, never used as
     * pointer offsets, so losing the range bound is safe). */
    RA_BARRIER(dx);
    RA_BARRIER(dy);

    /* Per-packet pipeline. ra_pre_lut / ra_post_lut (rawaccel_fixedpoint.h)
     * are shared with the host parity tests; the only kernel-specific step is
     * the LUT read, since a BPF array-map pointer cannot stride past one
     * element. Each entry is fetched on its own and fed into ra_q16_lerp. */
    __s64 inx, iny;
    __u32 ix, iy;
    __s32 fx, fy;
    __u8 single_scale;
    __s32 weight;
    ra_pre_lut(cfg, st, dx, dy, &inx, &iny, &ix, &fx, &iy, &fy,
               &single_scale, &weight);

    __s32 raw_x = ra_q16_lerp(q16_lookup(&ra_lut_x, ix),
                              q16_lookup(&ra_lut_x, ix + 1), fx);
    __s32 raw_y = ra_q16_lerp(q16_lookup(&ra_lut_y, iy),
                              q16_lookup(&ra_lut_y, iy + 1), fy);

    __s64 acc_x, acc_y;
    ra_post_lut(cfg, inx, iny, raw_x, raw_y, single_scale, weight,
                &acc_x, &acc_y);

    /* Carry-accumulated output in Q16.16. */
    __s64 out_x_q16 = acc_x + (__s64)st->carry_x_q16;
    __s64 out_y_q16 = acc_y + (__s64)st->carry_y_q16;

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
    /* hid_id is patched at load time by the userspace loader. */
    .hid_id = 0,
    .hid_device_event = (void *)rawaccel_hid_device_event,
};
