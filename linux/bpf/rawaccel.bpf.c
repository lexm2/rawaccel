/* Rawaccel HID-BPF kernel program. Rewrites the dx/dy bytes of a mouse HID
 * report via a Q16.16 LUT + fixed-point velocity EMA. One BPF object per
 * device, each bound to a single hid_id. Pipeline mirrors driver.cpp in
 * fixed point: parse -> |v| -> EMA -> LUT scale -> carry-accumulate -> write.
 * Verifier defenses: no floats, no dynamic loops, masked LUT indices,
 * bounded hid_bpf_get_data() reads. */

/* clang -target bpf defines __BPF__; the layout header tests it. */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "rawaccel_fixedpoint.h"

char LICENSE[] SEC("license") = "GPL";

/* hid_bpf kfunc prototypes (kernel exports via __ksym). */
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

/* Scalar math lives in rawaccel_fixedpoint.h (shared with host tests). The
 * one kernel-specific piece is the LUT fetch: a BPF array-map pointer can't
 * stride, so each entry is looked up on its own and fed to ra_q16_lerp. */
static __always_inline __s32 q16_lookup(void *map, __u32 idx)
{
    __u32 k = idx & (RA_LUT_SIZE - 1);
    __s32 *p = bpf_map_lookup_elem(map, &k);
    return p ? *p : RA_Q16_ONE;
}

/* Read a signed 8/16-bit value; fixed sizes per branch for the verifier. */
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

/* Verifier-bounded report view; accessed bytes gated on the config offsets. */
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

    /* With a report-ID prefix, only act on the configured type; others pass through. */
    if (cfg->report_id != 0 && rpt[0] != cfg->report_id) {
        return 0;
    }

    /* Narrow byte sizes to {1, 2} for read_signed/write_signed. */
    if (cfg->dx_byte_size != 1 && cfg->dx_byte_size != 2) return 0;
    if (cfg->dy_byte_size != 1 && cfg->dy_byte_size != 2) return 0;

    /* Mask offsets for the verifier, then tighten to VIEW-2: it tracks
     * dx_off independently of dx_byte_size, so reserve room for two bytes
     * always. Harmless: X/Y sit near the report start, never at byte 15. */
    __u32 dx_off = cfg->dx_byte_offset & (RA_REPORT_VIEW_BYTES - 1);
    __u32 dy_off = cfg->dy_byte_offset & (RA_REPORT_VIEW_BYTES - 1);
    if (dx_off > RA_REPORT_VIEW_BYTES - 2) return 0;
    if (dy_off > RA_REPORT_VIEW_BYTES - 2) return 0;

    __s32 dx = read_signed(rpt + dx_off, cfg->dx_byte_size);
    __s32 dy = read_signed(rpt + dy_off, cfg->dy_byte_size);

    if (dx == 0 && dy == 0) {
        return 0;  /* idle packet, no carry update */
    }

    /* read_signed's 8/16-bit branches leave dx/dy with different ranges that
     * never merge, doubling verifier state downstream. The barrier collapses
     * them to one scalar (dx/dy are only scaled, never pointer offsets). */
    RA_BARRIER(dx);
    RA_BARRIER(dy);

    /* Per-packet dt. last_ts_ns == 0 (first packet after a config load) ->
     * assume 1 ms to match the host oracle. Elapsed ns capped at 1 s (keeps
     * << 16 in u64), then clamped to [time_min, time_max]; a long idle gap
     * collapses to time_max. Computed here since bpf_ktime_get_ns is kernel-only. */
    __u64 now = bpf_ktime_get_ns();
    __s32 dt_ms_q16;
    if (st->last_ts_ns == 0) {
        dt_ms_q16 = RA_Q16_ONE;
    } else {
        __u64 dt_ns = now - st->last_ts_ns;
        if (dt_ns > 1000000000ULL) dt_ns = 1000000000ULL;
        __s32 dt = (__s32)((dt_ns << RA_Q16_SHIFT) / 1000000ULL);
        if (dt < cfg->time_min_q16) dt = cfg->time_min_q16;
        if (cfg->time_max_q16 > 0 && dt > cfg->time_max_q16) dt = cfg->time_max_q16;
        dt_ms_q16 = dt;
    }

    /* Per-packet pipeline; only the LUT read (map lookups) is kernel-specific. */
    __s64 inx, iny;
    __u32 ix, iy;
    __s32 fx, fy;
    __u8 single_scale;
    __s32 weight;
    ra_pre_lut(cfg, st, dx, dy, dt_ms_q16, &inx, &iny, &ix, &fx, &iy, &fy,
               &single_scale, &weight);

    __s32 raw_x = ra_q16_lerp(q16_lookup(&ra_lut_x, ix),
                              q16_lookup(&ra_lut_x, ix + 1), fx);
    __s32 raw_y = ra_q16_lerp(q16_lookup(&ra_lut_y, iy),
                              q16_lookup(&ra_lut_y, iy + 1), fy);

    __s64 acc_x, acc_y;
    ra_post_lut(cfg, st, inx, iny, raw_x, raw_y, single_scale, weight, dt_ms_q16,
                &acc_x, &acc_y);

    /* Carry-accumulate + emit; ra_emit_q16 drops a packet whose carry leaves [-1, 1). */
    __s32 out_x, out_y;
    if (!ra_emit_q16(st, acc_x, acc_y, &out_x, &out_y)) return 0;

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
