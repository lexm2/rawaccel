#pragma once

// Drives a TraceRecord stream through an EvdevProcessor, capturing the
// post-acceleration integer counts. Pure-math: no sockets, no /dev/input,
// no /dev/uinput; the same primitive feeds the replay tool and the
// determinism tests.

#include "evdev_processor.hpp"
#include "trace_format.hpp"

#include <vector>

namespace rawaccel_agent {

// Replay `records` through `p`. The processor is *not* reset before replay,
// so the caller can chain replays or warm up state. Returns one output
// record per input record, in order; tick_us is copied from the input.
std::vector<TraceRecord> replay_trace(EvdevProcessor& p,
                                      const std::vector<TraceRecord>& records);

} // namespace rawaccel_agent
