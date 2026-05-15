// Tests for the Step 8 trace format + replay primitive.

#include "evdev_processor.hpp"
#include "test_harness.hpp"
#include "trace_format.hpp"
#include "trace_replay.hpp"

#include <sstream>
#include <vector>

using namespace rawaccel_agent;
namespace ra = rawaccel;

RA_TEST("Trace: roundtrip CSV records")
{
    std::vector<TraceRecord> in = {
        {0,     5, -3},
        {1000,  0,  2},
        {3500, -7,  0},
    };
    std::ostringstream oss;
    write_trace(oss, in, "test trace");

    std::istringstream iss(oss.str());
    std::vector<TraceRecord> back;
    RA_CHECK(read_trace(iss, back));
    RA_CHECK_EQ(static_cast<int>(back.size()), 3);
    RA_CHECK_EQ(back[0].tick_us, 0);
    RA_CHECK_EQ(back[1].dx, 0);
    RA_CHECK_EQ(back[2].dy, 0);
    RA_CHECK_EQ(back[2].dx, -7);
}

RA_TEST("Trace: comments and blank lines are ignored")
{
    std::istringstream iss(
        "# header line\n"
        "\n"
        "100,1,2\n"
        "    # mid-stream comment\n"
        "200,3,4\n");
    std::vector<TraceRecord> records;
    RA_CHECK(read_trace(iss, records));
    RA_CHECK_EQ(static_cast<int>(records.size()), 2);
    RA_CHECK_EQ(records[0].tick_us, 100);
    RA_CHECK_EQ(records[1].dy, 4);
}

RA_TEST("Trace: malformed row returns false")
{
    std::istringstream iss("100,not_a_number,5\n");
    std::vector<TraceRecord> records;
    RA_CHECK(!read_trace(iss, records));
}

RA_TEST("Replay: identity profile passes input deltas through unchanged")
{
    EvdevProcessor p;
    ra::modifier_settings s{};
    p.set_settings(s);

    std::vector<TraceRecord> in = {
        {0,     3,  0},
        {1000, -2,  5},
        {2000,  0,  0},  // zero input: replay emits 0,0 (drop marker).
        {3000,  7, -1},
    };
    auto out = replay_trace(p, in);
    RA_CHECK_EQ(static_cast<int>(out.size()), 4);
    RA_CHECK_EQ(out[0].dx, 3);
    RA_CHECK_EQ(out[1].dy, 5);
    RA_CHECK_EQ(out[2].dx, 0);
    RA_CHECK_EQ(out[2].dy, 0);
    RA_CHECK_EQ(out[3].dx, 7);
}

RA_TEST("Replay: deterministic across two fresh runs")
{
    std::vector<TraceRecord> in;
    for (int i = 0; i < 200; ++i) {
        in.push_back({std::int64_t(i) * 1000, (i % 7) - 3, (i % 5) - 2});
    }
    ra::modifier_settings s{};
    s.prof.accel_x.mode = ra::accel_mode::classic;
    s.prof.accel_x.acceleration = 0.05;
    s.prof.accel_x.exponent_classic = 2.0;
    s.prof.accel_y = s.prof.accel_x;
    s.prof.speed_processor_args.input_speed_smooth_halflife = 20.0;

    EvdevProcessor a, b;
    a.set_settings(s);
    b.set_settings(s);
    auto out_a = replay_trace(a, in);
    auto out_b = replay_trace(b, in);
    RA_CHECK_EQ(out_a.size(), out_b.size());
    for (std::size_t i = 0; i < out_a.size(); ++i) {
        if (out_a[i].dx != out_b[i].dx || out_a[i].dy != out_b[i].dy) {
            RA_CHECK(false);
            break;
        }
    }
}

RA_TEST("Replay: total deltas match expected scaling on identity-scale")
{
    EvdevProcessor p;
    ra::modifier_settings s{};
    s.prof.output_dpi = 1500;  // 1.5x scale.
    p.set_settings(s);

    std::vector<TraceRecord> in;
    for (int i = 0; i < 100; ++i) {
        in.push_back({std::int64_t(i) * 1000, 2, 0});
    }
    auto out = replay_trace(p, in);

    long long sum = 0;
    for (auto& r : out) sum += r.dx;
    RA_CHECK_EQ(static_cast<int>(sum), 300);  // 100 inputs * 2 counts * 1.5.
}
