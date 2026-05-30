#pragma once

// HID report-descriptor parser for the BPF backend: finds relative X/Y Input fields.
// validate_for_bpf enforces a conservative shape (byte-aligned, 8/16-bit signed).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rawaccel_agent {

struct MouseAxis {
    bool present = false;
    std::uint32_t bit_offset_in_payload = 0;  // excludes report-ID prefix
    std::uint32_t bit_size = 0;
    bool is_signed = true;
};

struct MouseDescriptor {
    bool has_report_id = false;
    std::uint8_t report_id = 0;
    MouseAxis x;
    MouseAxis y;
    std::uint32_t report_bits = 0;  // payload bits, excludes report-ID prefix
};

std::optional<MouseDescriptor> parse_mouse_descriptor(
    const std::uint8_t* descriptor, std::size_t len);

struct BpfMouseLayout {
    std::uint8_t report_id = 0;   // 0 when no report ID byte is used
    std::uint8_t dx_byte_offset = 0;
    std::uint8_t dx_byte_size = 0;
    std::uint8_t dy_byte_offset = 0;
    std::uint8_t dy_byte_size = 0;
};

struct BpfRejection { std::string reason; };
struct BpfDecision {
    std::optional<BpfMouseLayout> layout;
    std::optional<BpfRejection> reject;
};
BpfDecision validate_for_bpf(const MouseDescriptor& d);

} // namespace rawaccel_agent
