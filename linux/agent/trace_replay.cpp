#include "trace_replay.hpp"

namespace rawaccel_agent {

std::vector<TraceRecord> replay_trace(EvdevProcessor& p,
                                      const std::vector<TraceRecord>& records)
{
    std::vector<TraceRecord> out;
    out.reserve(records.size());

    std::int64_t prev_tick = 0;
    bool have_prev = false;

    for (const auto& r : records) {
        // Inter-packet time delta in milliseconds. The first record has no
        // predecessor; the processor's time clamp coerces it into the
        // active [min,max] window.
        ra::milliseconds dt = 0.0;
        if (have_prev) {
            dt = static_cast<double>(r.tick_us - prev_tick) / 1000.0;
        } else {
            dt = ra::DEFAULT_TIME_MIN;
        }
        have_prev = true;
        prev_tick = r.tick_us;

        auto pd = p.process(r.dx, r.dy, dt);
        TraceRecord o;
        o.tick_us = r.tick_us;
        // Dropped packets (zero input or carry invalid) appear as 0,0 so the
        // row index lines up with the input. A diff tool reading both halves
        // can spot drops by checking whether the input was also zero.
        o.dx = pd.emit ? pd.x : 0;
        o.dy = pd.emit ? pd.y : 0;
        out.push_back(o);
    }
    return out;
}

} // namespace rawaccel_agent
