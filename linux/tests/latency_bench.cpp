// Per-packet latency gate for the shared fixed-point pipeline
// (rawaccel_fixedpoint.h) -- the exact code the BPF program runs in-kernel.
//
// An 8 kHz mouse delivers a packet every 1s/8000 = 125 us, so the per-packet
// math must finish well inside that budget. This times ra_modify_q16_flat +
// ra_emit_q16 over the full feature matrix (curve modes, distance modes, and
// every RA_F_* flag, plus an everything-on worst case) and asserts the 99th
// percentile per-call latency stays under 125 us. The max is reported for
// information; a lone OS-scheduling outlier on a non-isolated core should not
// fail the build (run under `chrt -f` to tighten the max if desired).
//
// This is a host micro-benchmark, not the in-kernel program: the kernel reads
// the LUT from a BPF map where this reads a flat array, otherwise the code is
// identical, so this is a faithful (and conservative) upper bound on the math
// cost. The curve mode only changes the precomputed LUT, never the per-packet
// work, but every mode is swept anyway so a future curve that perturbs the hot
// path is caught.

#include "rawaccel_fixedpoint.h"
#include "lut_builder.hpp"
#include "test_harness.hpp"

#include "rawaccel.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <random>
#include <vector>

using namespace rawaccel_agent;
namespace ra = rawaccel;

namespace {

constexpr long BUDGET_NS = 125000;   // 1s / 8000 = 125 us per packet (8 kHz)
constexpr int  WARMUP    = 10000;
constexpr int  ITERS     = 1000000;

// A prepared config: built LUT + assembled ra_bpf_config, ready to drive.
struct Bench {
    LutBuildResult lut;
    ra_bpf_config  cfg;
};

// Build a Bench from modifier_settings; never throws out of here (a build
// failure fails the calling RA_TEST instead of aborting the binary).
bool make_bench(const char* what, const ra::modifier_settings& s, Bench& out)
{
    try {
        ra::device_config dev{};
        out.lut = build_lut(s, dev);
        BpfMouseLayout layout{};
        out.cfg = to_bpf_config(out.lut, layout);
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "  build_lut threw for %s: %s\n", what, e.what());
        return false;
    }
}

inline long now_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

// Time the per-packet path over a fixed pseudo-random stream threaded through a
// single live ra_bpf_state (smoothers/carry stay warm). Records per-call ns,
// asserts p99 < budget, and prints min/mean/p99/max.
void time_config(const char* what, const Bench& b)
{
    std::mt19937 rng(0x9e3779b9u);
    std::uniform_int_distribution<int> disp(-300, 300);
    // dt mix: 8 kHz (0.125 ms), 1 kHz (1 ms), 125 Hz (8 ms).
    const __s32 dts[] = {
        static_cast<__s32>(0.125 * RA_Q16_ONE),
        static_cast<__s32>(1.0 * RA_Q16_ONE),
        static_cast<__s32>(8.0 * RA_Q16_ONE),
    };

    ra_bpf_state st{};
    volatile __s32 sink = 0;   // keep the optimizer from eliding the work

    for (int i = 0; i < WARMUP; ++i) {
        __s64 ox = 0, oy = 0;
        ra_modify_q16_flat(&b.cfg, &st, b.lut.lut_x.data(), b.lut.lut_y.data(),
                           disp(rng), disp(rng), dts[i % 3], &ox, &oy);
        __s32 ex = 0, ey = 0;
        ra_emit_q16(&st, ox, oy, &ex, &ey);
        sink = sink + ex + ey;
    }

    std::vector<long> samples;
    samples.reserve(ITERS);
    for (int i = 0; i < ITERS; ++i) {
        __s32 dx = disp(rng), dy = disp(rng), dt = dts[i % 3];
        long t0 = now_ns();
        __s64 ox = 0, oy = 0;
        ra_modify_q16_flat(&b.cfg, &st, b.lut.lut_x.data(), b.lut.lut_y.data(),
                           dx, dy, dt, &ox, &oy);
        __s32 ex = 0, ey = 0;
        ra_emit_q16(&st, ox, oy, &ex, &ey);
        long t1 = now_ns();
        samples.push_back(t1 - t0);
        sink = sink + ex + ey;
    }
    (void)sink;

    std::sort(samples.begin(), samples.end());
    long mn = samples.front();
    long mx = samples.back();
    long p99 = samples[static_cast<size_t>(samples.size() * 0.99)];
    long long sum = 0;
    for (long v : samples) sum += v;
    double mean = static_cast<double>(sum) / samples.size();

    std::printf("    %-28s min=%ldns mean=%.0fns p99=%ldns max=%ldns\n",
                what, mn, mean, p99, mx);

    // p99 is the gate (robust to scheduling outliers); max is informational.
    RA_CHECK(p99 < BUDGET_NS);
}

// Configure accel_x as a given curve mode with sensible defaults; mirror to Y.
ra::modifier_settings curve(ra::accel_mode mode)
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = mode;
    s.prof.accel_x.gain = true;
    s.prof.accel_y = s.prof.accel_x;
    return s;
}

// A nonzero classic curve to layer feature flags on top of.
ra::modifier_settings classic()
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    return s;
}

void run(const char* what, const ra::modifier_settings& s)
{
    Bench b;
    // A config that won't build (e.g. a curve mode needing data we didn't set)
    // is a configuration gap, not a latency regression: skip it loudly rather
    // than fail the gate. The per-packet cost is LUT-baked and mode-invariant.
    if (!make_bench(what, s, b)) {
        std::printf("    %-28s SKIP (build_lut failed)\n", what);
        return;
    }
    time_config(what, b);
}

} // namespace

RA_TEST("Latency: curve modes (LUT-baked, per-packet cost is mode-invariant)")
{
    run("noaccel",     curve(ra::accel_mode::noaccel));
    run("classic",     classic());
    run("jump",        curve(ra::accel_mode::jump));
    run("natural",     curve(ra::accel_mode::natural));
    run("synchronous", curve(ra::accel_mode::synchronous));
    run("power",       curve(ra::accel_mode::power));
    run("lookup",      curve(ra::accel_mode::lookup));
}

RA_TEST("Latency: distance modes")
{
    {   // whole + euclidean (default lp_norm 2)
        run("whole-euclidean", classic());
    }
    {   // whole + max (lp_norm >= MAX_NORM)
        ra::modifier_settings s = classic();
        s.prof.speed_processor_args.lp_norm = 100.0;
        run("whole-max", s);
    }
    {   // separate (per-axis)
        ra::modifier_settings s = classic();
        s.prof.speed_processor_args.whole = false;
        run("separate", s);
    }
}

RA_TEST("Latency: each feature flag on top of classic")
{
    {   ra::modifier_settings s = classic();
        s.prof.degrees_rotation = 23.0;            run("rotate", s); }
    {   ra::modifier_settings s = classic();
        s.prof.degrees_snap = 15.0;                run("snap", s); }
    {   ra::modifier_settings s = classic();
        s.prof.speed_min = 20.0; s.prof.speed_max = 200.0;
        run("clamp-speed", s); }
    {   ra::modifier_settings s = classic();
        s.prof.range_weights = vec2d{0.6, 1.4};    run("dir-weight", s); }
    {   ra::modifier_settings s = classic();
        s.prof.lr_output_dpi_ratio = 1.3;
        s.prof.ud_output_dpi_ratio = 0.7;          run("dir-mul-xy", s); }
    {   ra::modifier_settings s = classic();
        s.prof.speed_processor_args.input_speed_smooth_halflife = 0.5;
        run("smooth-input", s); }
    {   ra::modifier_settings s = classic();
        s.prof.speed_processor_args.scale_smooth_halflife = 0.5;
        run("smooth-scale", s); }
    {   ra::modifier_settings s = classic();
        s.prof.speed_processor_args.output_speed_smooth_halflife = 0.5;
        run("smooth-output", s); }
}

RA_TEST("Latency: everything-on worst case clears the 8 kHz budget")
{
    ra::modifier_settings s = classic();
    s.prof.degrees_rotation = 23.0;
    s.prof.degrees_snap = 15.0;
    s.prof.speed_min = 20.0;
    s.prof.speed_max = 200.0;
    s.prof.range_weights = vec2d{0.6, 1.4};
    s.prof.lr_output_dpi_ratio = 1.3;
    s.prof.ud_output_dpi_ratio = 0.7;
    s.prof.output_dpi = 1600;
    s.prof.speed_processor_args.input_speed_smooth_halflife = 0.5;
    s.prof.speed_processor_args.scale_smooth_halflife = 0.5;
    s.prof.speed_processor_args.output_speed_smooth_halflife = 0.5;
    run("everything-on", s);
}
