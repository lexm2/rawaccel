#pragma once

// HID report descriptor parser scoped to what the BPF backend needs:
// locating the relative X / Y motion fields inside an Input report so the
// kernel-side program can rewrite their bytes in place.
//
// The parser walks the report-descriptor bytecode as defined in the USB HID
// 1.11 specification, maintaining the Global/Local/Main state stacks. For
// each Input main item it expands ReportCount entries of ReportSize bits
// and consumes one Local Usage per entry (or repeats the last one if the
// usage list is exhausted, per the spec). When an entry's resolved
// (UsagePage, Usage) is (Generic Desktop, X) or (..., Y) we record its
// position within the report.
//
// `validate_for_bpf` enforces the conservative shape the plan calls for:
// byte-aligned, 8- or 16-bit signed, same report, in a top-level Mouse or
// Pointer collection. Anything weirder falls back to the evdev backend.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rawaccel_agent {

struct MouseAxis {
    bool present = false;
    std::uint32_t bit_offset_in_payload = 0;  // excludes the report-ID prefix.
    std::uint32_t bit_size = 0;
    bool is_signed = true;
};

struct MouseDescriptor {
    bool has_report_id = false;
    std::uint8_t report_id = 0;
    MouseAxis x;
    MouseAxis y;
    // Report length in bits, excluding the report-ID prefix byte. Useful
    // for sanity checks against actual report sizes from the kernel.
    std::uint32_t report_bits = 0;
};

// Parse `descriptor` and return the first Mouse/Pointer collection that has
// both X and Y relative axes. Returns nullopt if no such collection exists.
std::optional<MouseDescriptor> parse_mouse_descriptor(
    const std::uint8_t* descriptor, std::size_t len);

// Byte-level locations after accounting for the report-ID prefix (1 byte
// before the payload when has_report_id is true).
struct BpfMouseLayout {
    std::uint8_t report_id = 0;   // 0 when no report ID byte is used.
    std::uint8_t dx_byte_offset = 0;
    std::uint8_t dx_byte_size = 0;
    std::uint8_t dy_byte_offset = 0;
    std::uint8_t dy_byte_size = 0;
};

// Verify the descriptor is in the conservative shape the BPF backend
// supports, and return its byte-level layout. Returns nullopt with a
// human-readable reason on rejection.
struct BpfRejection { std::string reason; };
struct BpfDecision {
    std::optional<BpfMouseLayout> layout;
    std::optional<BpfRejection> reject;
};
BpfDecision validate_for_bpf(const MouseDescriptor& d);

} // namespace rawaccel_agent
