#pragma once

// Cross-OS input/output trace format used for parity comparisons between
// the Windows driver path and the Linux agent path.
//
// The trace is plain CSV so it can be produced from any environment:
//   * Header: a comment line starting with '#'. Free-form, ignored on read.
//   * Records, one per packet:  tick_us, dx, dy
//     - tick_us: monotonic time since trace start, in microseconds.
//                On Windows, QPC delta * (1e6 / QpcFrequency).
//                On Linux,  input_event.time delta * 1e6 + .tv_usec.
//     - dx, dy: int32 relative motion as it was reported pre-acceleration.
//
// Lines starting with '#' or that are blank are ignored anywhere in the
// file. This keeps the format trivially diffable.
//
// Output traces use the same shape: tick_us is the input tick (so a
// per-row diff lines up trivially); dx, dy are the post-acceleration
// integer counts written to the output stream. Rows where the processor
// emitted nothing (carry-validation drop, zero input) are still present
// with dx=0, dy=0 so row indices match the input.

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace rawaccel_agent {

struct TraceRecord {
    std::int64_t tick_us = 0;
    std::int32_t dx = 0;
    std::int32_t dy = 0;
};

// Read a trace from a stream. Returns false on malformed input.
bool read_trace(std::istream& in, std::vector<TraceRecord>& out);

// Write a trace to a stream. Always succeeds for well-formed records.
void write_trace(std::ostream& out, const std::vector<TraceRecord>& records,
                 const std::string& comment = {});

} // namespace rawaccel_agent
