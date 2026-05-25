// Scenario tests for the BPF per-packet pipeline driven as the kernel runs it:
// build a config from modifier_settings, then push a SEQUENCE of fake packets
// through the shared fixed-point code (rawaccel_fixedpoint.h) while threading a
// single ra_bpf_state, and check the emitted integer output.
//
// This is the multi-packet, integer-emission complement to fixedpoint_tests.cpp
// (which checks one packet's Q16.16 output). It exercises the two pieces of
// state that only matter across packets -- the fractional carry accumulator and
// the velocity EMA -- plus ra_emit_q16, the carry/emit/ValidCarry step the BPF
// program (rawaccel.bpf.c) now shares with these tests instead of inlining.
//
// Expected values come from two sources (the "oracle + invariants" design):
//   - parity: the authoritative double-precision common/ math run over the same
//     sequence, accumulating carry exactly as ra_emit_q16 does;
//   - invariants: relational properties robust to Q16.16 quantization (a clamp
//     pins fast input to the cap, the negative direction is scaled and the
//     positive is not, smoothing ramps monotonically to its steady state).
//
// Pure-axis input is used throughout: with dev.dpi 0 the kernel velocity equals
// the raw count in in/s (ips_factor 1) and the whole-mode directional blend
// collapses to range_weights.x / .y, the same Phase 0 parity constraint
// fixedpoint_tests.cpp documents.

#include "rawaccel_fixedpoint.h"
#include "lut_builder.hpp"
#include "test_harness.hpp"

#include "rawaccel.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

using namespace rawaccel_agent;
namespace ra = rawaccel;

namespace {

using Packet = std::pair<int, int>;
using Packets = std::vector<Packet>;

struct Emit {
    int x;
    int y;
    bool emitted;  // false => ValidCarry drop (raw passthrough); never fires here
};

// Authoritative continuous output for one axis, mirroring how build_lut
// prepares the curve (smoother halflives zeroed) and how the BPF program is
// exercised (dpi_factor 1, 1 ms slice). Stateless per call, so it is a valid
// per-packet oracle only for non-smoothing configs.
double oracle_axis(const ra::modifier_settings& s,
                   double in_x, double in_y, bool want_x)
{
    ra::modifier_settings ms = s;
    ms.prof.speed_processor_args.input_speed_smooth_halflife = 0;
    ms.prof.speed_processor_args.scale_smooth_halflife = 0;
    ms.prof.speed_processor_args.output_speed_smooth_halflife = 0;
    ra::init_data(ms);

    ra::modifier mod(ms);
    ra::speed_processor sp{};
    sp.init(ms.prof.speed_processor_args);

    vec2d v{in_x, in_y};
    mod.modify(v, sp, ms, 1.0, 1.0);
    return want_x ? v.x : v.y;
}

// Drive the fixed-point pipeline + emission across a packet sequence, threading
// one ra_bpf_state, exactly as rawaccel.bpf.c's event handler does: an idle
// (0,0) packet returns early -- emitting (0,0) without touching carry -- and a
// ValidCarry drop leaves the report unmodified (raw passthrough). If acc_out is
// given it receives each packet's pre-emit Q16.16 output (the carry-free scale
// result), used by invariants that must dodge carry quantization.
std::vector<Emit> run_fixed(const ra::modifier_settings& s,
                            const ra::device_config& dev,
                            const Packets& packets,
                            std::vector<std::pair<long long, long long>>* acc_out = nullptr)
{
    LutBuildResult lut = build_lut(s, dev);
    BpfMouseLayout layout{};  // HID fields irrelevant to the math
    ra_bpf_config cfg = to_bpf_config(lut, layout);
    ra_bpf_state st{};  // fresh: smoothed_v and carry both zero

    std::vector<Emit> out;
    out.reserve(packets.size());
    for (const auto& pk : packets) {
        int dx = pk.first, dy = pk.second;
        if (dx == 0 && dy == 0) {
            out.push_back({0, 0, true});
            if (acc_out) acc_out->push_back({0, 0});
            continue;
        }
        __s64 ax = 0, ay = 0;
        // These sequences assume a 1 ms slice (RA_Q16_ONE); dt-varying behavior
        // is covered by fixedpoint_tests.cpp's P2.1 cases.
        ra_modify_q16_flat(&cfg, &st, lut.lut_x.data(), lut.lut_y.data(),
                           dx, dy, RA_Q16_ONE, &ax, &ay);
        if (acc_out) acc_out->push_back({(long long)ax, (long long)ay});

        __s32 ex = 0, ey = 0;
        if (ra_emit_q16(&st, ax, ay, &ex, &ey))
            out.push_back({ex, ey, true});
        else
            out.push_back({dx, dy, false});  // drop -> original report passes through
    }
    return out;
}

// Predicted integer emission from the double-precision oracle, accumulating
// carry the way ra_emit_q16 does: floor toward -inf (arithmetic >>), remainder
// in [0, 1). Valid only for non-smoothing configs (oracle_axis is stateless).
std::vector<Emit> run_oracle(const ra::modifier_settings& s, const Packets& packets)
{
    double cx = 0.0, cy = 0.0;
    std::vector<Emit> out;
    out.reserve(packets.size());
    for (const auto& pk : packets) {
        int dx = pk.first, dy = pk.second;
        if (dx == 0 && dy == 0) {
            out.push_back({0, 0, true});
            continue;
        }
        double ox = oracle_axis(s, dx, dy, true) + cx;
        double oy = oracle_axis(s, dx, dy, false) + cy;
        long ex = (long)std::floor(ox);
        long ey = (long)std::floor(oy);
        cx = ox - (double)ex;
        cy = oy - (double)ey;
        out.push_back({(int)ex, (int)ey, true});
    }
    return out;
}

// A packet with an explicit per-packet slice: {dx, dy, dt_ms}.
using DtPacket = std::array<double, 3>;
using DtPackets = std::vector<DtPacket>;

// Drive the fixed-point pipeline across a dt-varying sequence, threading one
// ra_bpf_state, and return each packet's pre-emit Q16.16 output as doubles (the
// carry-free scale result -- the analog used to compare smoother transients
// without carry quantization). Idle packets emit (0,0) and skip the smoother,
// exactly as rawaccel.bpf.c does.
std::vector<std::pair<double, double>>
run_fixed_acc(const ra::modifier_settings& s, const ra::device_config& dev,
              const DtPackets& packets)
{
    LutBuildResult lut = build_lut(s, dev);
    BpfMouseLayout layout{};
    ra_bpf_config cfg = to_bpf_config(lut, layout);
    ra_bpf_state st{};

    std::vector<std::pair<double, double>> out;
    out.reserve(packets.size());
    for (const auto& p : packets) {
        int dx = static_cast<int>(p[0]), dy = static_cast<int>(p[1]);
        if (dx == 0 && dy == 0) { out.push_back({0.0, 0.0}); continue; }
        __s32 dt_q16 = static_cast<__s32>(std::lround(p[2] * RA_Q16_ONE));
        __s64 ax = 0, ay = 0;
        ra_modify_q16_flat(&cfg, &st, lut.lut_x.data(), lut.lut_y.data(),
                           dx, dy, dt_q16, &ax, &ay);
        out.push_back({static_cast<double>(ax) / RA_Q16_ONE,
                       static_cast<double>(ay) / RA_Q16_ONE});
    }
    return out;
}

// Stateful double-precision reference: ONE modifier + speed_processor kept
// across the whole sequence with the REAL smoother halflives, so the EMAs
// accumulate exactly as the kernel's ra_bpf_state does. Returns each packet's
// continuous (pre-carry) output -- the oracle for run_fixed_acc. Idle packets
// skip modify so the smoother state freezes, matching the kernel's early return.
std::vector<std::pair<double, double>>
run_oracle_stateful(const ra::modifier_settings& s, const DtPackets& packets)
{
    ra::modifier_settings ms = s;
    ra::init_data(ms);
    ra::modifier mod(ms);
    ra::speed_processor sp{};
    sp.init(ms.prof.speed_processor_args);

    std::vector<std::pair<double, double>> out;
    out.reserve(packets.size());
    for (const auto& p : packets) {
        if (p[0] == 0.0 && p[1] == 0.0) { out.push_back({0.0, 0.0}); continue; }
        vec2d v{p[0], p[1]};
        mod.modify(v, sp, ms, 1.0, p[2]);
        out.push_back({v.x, v.y});
    }
    return out;
}

} // namespace

RA_TEST("Seq: carry accumulates fractional output_dpi into whole counts")
{
    ra::modifier_settings s{};
    s.prof.output_dpi = 1500;  // 1.5x NORMALIZED_DPI, an exact-half scale
    ra::device_config dev{};

    Packets pk(8, {1, 0});  // eight 1-count moves -> 1.5 each, carry bridges
    auto f = run_fixed(s, dev, pk);
    auto o = run_oracle(s, pk);

    long sum_f = 0, sum_o = 0;
    for (std::size_t i = 0; i < pk.size(); ++i) {
        RA_CHECK_EQ(f[i].x, o[i].x);  // exact: the per-packet scale is exactly 1.5
        RA_CHECK_EQ(f[i].y, 0);
        sum_f += f[i].x;
        sum_o += o[i].x;
    }
    // 1.5 with carry emits the 1,2,1,2,... pattern; 8 packets total 12 counts.
    RA_CHECK_EQ(f[0].x, 1);
    RA_CHECK_EQ(f[1].x, 2);
    RA_CHECK_EQ(sum_f, 12);
    RA_CHECK_EQ(sum_f, sum_o);
}

RA_TEST("Seq: speed clamp caps sustained fast input to the clamp speed")
{
    ra::modifier_settings s{};
    s.prof.speed_max = 50.0;  // dpi_norm 1 so count == in/s; cap at 50
    ra::device_config dev{};

    // Sustained 200-count input is clamped to 50 before the (noaccel) curve, so
    // it must emit the same stream as sustained 50-count input.
    Packets fast(16, {200, 0});
    Packets at_cap(16, {50, 0});
    auto f_fast = run_fixed(s, dev, fast);
    auto f_cap = run_fixed(s, dev, at_cap);

    for (std::size_t i = 0; i < fast.size(); ++i) {
        RA_CHECK_EQ(f_fast[i].x, f_cap[i].x);
        RA_CHECK_EQ(f_fast[i].x, 50);  // noaccel: output == clamped speed
    }
    // Parity with the oracle.
    auto o_fast = run_oracle(s, fast);
    for (std::size_t i = 0; i < fast.size(); ++i)
        RA_CHECK(std::abs(f_fast[i].x - o_fast[i].x) <= 1);
}

RA_TEST("Seq: directional output DPI scales only the negative direction")
{
    ra::modifier_settings s{};
    s.prof.lr_output_dpi_ratio = 1.25;  // X, applied only when output < 0
    s.prof.ud_output_dpi_ratio = 0.8;   // Y, applied only when output < 0
    ra::device_config dev{};

    const int N = 20;
    Packets posx(N, {10, 0}), negx(N, {-10, 0});
    Packets posy(N, {0, 10}), negy(N, {0, -10});
    auto fpx = run_fixed(s, dev, posx);
    auto fnx = run_fixed(s, dev, negx);
    auto fpy = run_fixed(s, dev, posy);
    auto fny = run_fixed(s, dev, negy);

    long spx = 0, snx = 0, spy = 0, sny = 0;
    for (int i = 0; i < N; ++i) {
        spx += fpx[i].x;
        snx += fnx[i].x;
        spy += fpy[i].y;
        sny += fny[i].y;
    }
    // Positive directions are untouched, so their totals are exact (scale 1.0).
    RA_CHECK_EQ(spx, (long)(10 * N));
    RA_CHECK_EQ(spy, (long)(10 * N));
    // Negative directions are scaled by the ratio. The cumulative total is
    // within 1 count of the analytic value: a ratio not exactly representable
    // in Q16.16 (0.8 rounds to 0.800003) drifts the sum by a count over the
    // sequence, the off-by-one the carry convention can produce at a boundary.
    RA_CHECK(std::abs(snx - -(long)std::llround(10.0 * N * 1.25)) <= 1);
    RA_CHECK(std::abs(sny - -(long)std::llround(10.0 * N * 0.8)) <= 1);
    // The contract itself: negative is scaled away from the untouched positive.
    RA_CHECK(std::abs(snx) > spx);   // 1.25x grows the leftward magnitude
    RA_CHECK(std::abs(sny) < spy);   // 0.8x shrinks the downward magnitude

    // Parity with the oracle on both X streams.
    auto opx = run_oracle(s, posx);
    auto onx = run_oracle(s, negx);
    for (int i = 0; i < N; ++i) {
        RA_CHECK(std::abs(fpx[i].x - opx[i].x) <= 1);
        RA_CHECK(std::abs(fnx[i].x - onx[i].x) <= 1);
    }
}

RA_TEST("Seq: classic curve output rises with speed and matches the oracle")
{
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    ra::device_config dev{};

    Packets sweep;
    for (int v : {2, 5, 10, 20, 40, 80, 160, 320}) sweep.push_back({v, 0});

    std::vector<std::pair<long long, long long>> acc;
    auto f = run_fixed(s, dev, sweep, &acc);
    auto o = run_oracle(s, sweep);

    for (std::size_t i = 0; i < sweep.size(); ++i)
        RA_CHECK(std::abs(f[i].x - o[i].x) <= 1);

    // Output magnitude is monotone non-decreasing as input speed rises (classic
    // gain >= 1 and increasing). Use the pre-emit Q16.16 acc so carry rounding
    // does not perturb the comparison.
    for (std::size_t i = 1; i < sweep.size(); ++i)
        RA_CHECK(acc[i].first >= acc[i - 1].first);
}

RA_TEST("Seq: input speed smoothing matches the stateful oracle (1 ms)")
{
    // The kernel's linear-EMA input smoother (with trend extrapolation) must
    // track the common/ linear_ema_smoother packet for packet. Compare the
    // pre-emit Q16.16 output against a stateful oracle over a step-up / step-down
    // sequence (which exercises the trend term in both directions) at a constant
    // 1 ms slice. dt-varying parity is added in P2.5.
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.speed_processor_args.input_speed_smooth_halflife = 20;  // ms
    ra::device_config dev{};

    DtPackets pk;
    for (int i = 0; i < 150; ++i) pk.push_back({120.0, 0.0, 1.0});  // step up from rest
    for (int i = 0; i < 150; ++i) pk.push_back({20.0, 0.0, 1.0});   // step down

    auto f = run_fixed_acc(s, dev, pk);
    auto o = run_oracle_stateful(s, pk);

    for (std::size_t i = 0; i < pk.size(); ++i) {
        RA_CHECK_NEAR(f[i].first, o[i].first, 1e-2 + std::fabs(o[i].first) * 5e-3);
        RA_CHECK_NEAR(f[i].second, o[i].second, 1e-2);
    }

    // The smoother must actually be ramping (first packet from rest is well
    // below the steady-state scale the oracle settles at).
    RA_CHECK(f[0].first < f[140].first);
}

RA_TEST("Seq: whole-mode input smoothing matches the stateful oracle (1 ms)")
{
    // Whole mode smooths the single aggregate (euclidean) speed via smoother_x;
    // drive a diagonal step and check parity against the stateful oracle.
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.speed_processor_args.input_speed_smooth_halflife = 15;
    ra::device_config dev{};

    DtPackets pk;
    for (int i = 0; i < 200; ++i) pk.push_back({90.0, 120.0, 1.0});  // |v| = 150

    auto f = run_fixed_acc(s, dev, pk);
    auto o = run_oracle_stateful(s, pk);
    for (std::size_t i = 0; i < pk.size(); ++i) {
        RA_CHECK_NEAR(f[i].first,  o[i].first,  1e-2 + std::fabs(o[i].first) * 5e-3);
        RA_CHECK_NEAR(f[i].second, o[i].second, 1e-2 + std::fabs(o[i].second) * 5e-3);
    }
}

RA_TEST("Seq: scale smoothing matches the stateful oracle (whole + separate)")
{
    // The scale smoother (simple EMA) smooths the range-weighted curve scale
    // before the output-DPI multiply. Both totals start at 0, so the scale
    // ramps in from below; parity must hold from the first packet through the
    // ramp into steady state, in whole and separate mode.
    ra::device_config dev{};

    {   // whole mode, diagonal hold
        ra::modifier_settings s{};
        s.prof.accel_x.mode = ra::accel_mode::classic;
        s.prof.accel_x.acceleration = 0.05;
        s.prof.accel_x.exponent_classic = 2.0;
        s.prof.accel_y = s.prof.accel_x;
        s.prof.speed_processor_args.scale_smooth_halflife = 25;
        DtPackets pk;
        for (int i = 0; i < 250; ++i) pk.push_back({90.0, 120.0, 1.0});
        auto f = run_fixed_acc(s, dev, pk);
        auto o = run_oracle_stateful(s, pk);
        for (std::size_t i = 0; i < pk.size(); ++i) {
            RA_CHECK_NEAR(f[i].first,  o[i].first,  1e-2 + std::fabs(o[i].first) * 5e-3);
            RA_CHECK_NEAR(f[i].second, o[i].second, 1e-2 + std::fabs(o[i].second) * 5e-3);
        }
    }
    {   // separate mode, asymmetric curves -> per-axis scale smoothing
        ra::modifier_settings s{};
        s.prof.accel_x.mode = ra::accel_mode::classic;
        s.prof.accel_x.acceleration = 0.05;
        s.prof.accel_x.exponent_classic = 2.0;
        s.prof.accel_y.mode = ra::accel_mode::classic;
        s.prof.accel_y.acceleration = 0.02;
        s.prof.accel_y.exponent_classic = 2.0;
        s.prof.speed_processor_args.whole = false;
        s.prof.speed_processor_args.scale_smooth_halflife = 25;
        DtPackets pk;
        for (int i = 0; i < 250; ++i) pk.push_back({110.0, 70.0, 1.0});
        auto f = run_fixed_acc(s, dev, pk);
        auto o = run_oracle_stateful(s, pk);
        for (std::size_t i = 0; i < pk.size(); ++i) {
            RA_CHECK_NEAR(f[i].first,  o[i].first,  1e-2 + std::fabs(o[i].first) * 5e-3);
            RA_CHECK_NEAR(f[i].second, o[i].second, 1e-2 + std::fabs(o[i].second) * 5e-3);
        }
    }
}

RA_TEST("Seq: idle (0,0) packets emit nothing and preserve carry")
{
    ra::modifier_settings s{};
    s.prof.output_dpi = 1500;  // fractional scale so carry actually matters
    ra::device_config dev{};

    // The same motion, once dense and once with an idle after every move. Idles
    // must emit (0,0) and leave carry untouched, so the moving packets emit the
    // identical sub-sequence and the totals match.
    Packets dense(6, {1, 0});
    Packets spaced;
    for (const auto& p : dense) {
        spaced.push_back(p);
        spaced.push_back({0, 0});
    }
    auto fd = run_fixed(s, dev, dense);
    auto fs = run_fixed(s, dev, spaced);

    long sum_dense = 0, sum_spaced = 0;
    for (const auto& e : fd) sum_dense += e.x;

    std::size_t di = 0;
    for (std::size_t i = 0; i < spaced.size(); ++i) {
        if (spaced[i].first == 0 && spaced[i].second == 0) {
            RA_CHECK_EQ(fs[i].x, 0);
        } else {
            RA_CHECK_EQ(fs[i].x, fd[di].x);  // moving slot matches the dense stream
            ++di;
            sum_spaced += fs[i].x;
        }
    }
    RA_CHECK_EQ(sum_dense, sum_spaced);
}
