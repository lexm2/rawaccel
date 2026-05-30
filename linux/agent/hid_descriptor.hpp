#pragma once

// BpfMouseLayout: byte-level dx/dy offsets the BPF data plane consumes.
// Descriptor parsing now lives in the Rust daemon (agentd/src/hid.rs); the layout
// arrives precomputed over the C ABI.

#include <cstdint>

namespace rawaccel_agent {

struct BpfMouseLayout {
    std::uint8_t report_id = 0;   // 0 when no report ID byte is used
    std::uint8_t dx_byte_offset = 0;
    std::uint8_t dx_byte_size = 0;
    std::uint8_t dy_byte_offset = 0;
    std::uint8_t dy_byte_size = 0;
};

} // namespace rawaccel_agent
