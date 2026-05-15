// Tests for the HID report descriptor parser. Synthetic descriptors come
// straight from the USB HID 1.11 spec examples and known-good real-mouse
// shapes; rejection cases check that the BPF-safety filter is strict
// enough to keep the kernel rewriter from clobbering wheel/button bytes.

#include "hid_descriptor.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <vector>

using namespace rawaccel_agent;

namespace {

// Canonical boot-mouse descriptor (HID 1.11 Appendix E.10): 3 buttons + 5
// bits padding + signed 8-bit X + signed 8-bit Y. Report is 3 bytes; no
// report ID prefix.
const std::vector<std::uint8_t> BOOT_MOUSE = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x03,        //     Usage Maximum (3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Var, Abs)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x03,        //     Input (Const, Var, Abs)  (padding)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data, Var, Rel)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

// 16-bit X/Y with a Report ID prefix. Mirrors what most modern gaming mice
// emit (Logitech G Pro shape, generic high-DPI 16-bit).
const std::vector<std::uint8_t> HIGH_DPI_MOUSE = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x05,        //     Usage Maximum (5)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x05,        //     Report Count (5)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Var, Abs)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x03,        //     Report Size (3)
    0x81, 0x03,        //     Input (Const, Var, Abs)  (padding)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x16, 0x01, 0x80,  //     Logical Minimum (-32767)
    0x26, 0xFF, 0x7F,  //     Logical Maximum (32767)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data, Var, Rel)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

// A descriptor whose X axis is 12 bits, packed alongside Y in a 24-bit
// field. Common on some older trackballs. The BPF backend cannot safely
// rewrite this without bit-level packing, so it must fail validation.
const std::vector<std::uint8_t> PACKED_12BIT = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x16, 0x01, 0xF8,  //     Logical Minimum (-2047)
    0x26, 0xFF, 0x07,  //     Logical Maximum (2047)
    0x75, 0x0C,        //     Report Size (12)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data, Var, Rel)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

} // namespace

RA_TEST("HID: boot-mouse descriptor places X at byte 1, Y at byte 2")
{
    auto md = parse_mouse_descriptor(BOOT_MOUSE.data(), BOOT_MOUSE.size());
    RA_CHECK(md.has_value());
    RA_CHECK(!md->has_report_id);
    RA_CHECK(md->x.present);
    RA_CHECK(md->y.present);
    RA_CHECK_EQ(static_cast<int>(md->x.bit_offset_in_payload), 8);
    RA_CHECK_EQ(static_cast<int>(md->x.bit_size), 8);
    RA_CHECK_EQ(static_cast<int>(md->y.bit_offset_in_payload), 16);
    RA_CHECK_EQ(static_cast<int>(md->y.bit_size), 8);
    RA_CHECK(md->x.is_signed);
    RA_CHECK(md->y.is_signed);
}

RA_TEST("HID: boot-mouse passes BPF validation with byte 1/2 layout")
{
    auto md = parse_mouse_descriptor(BOOT_MOUSE.data(), BOOT_MOUSE.size());
    RA_CHECK(md.has_value());
    auto dec = validate_for_bpf(*md);
    RA_CHECK(dec.layout.has_value());
    RA_CHECK_EQ(static_cast<int>(dec.layout->report_id), 0);
    RA_CHECK_EQ(static_cast<int>(dec.layout->dx_byte_offset), 1);
    RA_CHECK_EQ(static_cast<int>(dec.layout->dx_byte_size), 1);
    RA_CHECK_EQ(static_cast<int>(dec.layout->dy_byte_offset), 2);
    RA_CHECK_EQ(static_cast<int>(dec.layout->dy_byte_size), 1);
}

RA_TEST("HID: 16-bit mouse with report ID places X/Y past the prefix byte")
{
    auto md = parse_mouse_descriptor(HIGH_DPI_MOUSE.data(), HIGH_DPI_MOUSE.size());
    RA_CHECK(md.has_value());
    RA_CHECK(md->has_report_id);
    RA_CHECK_EQ(static_cast<int>(md->report_id), 1);
    RA_CHECK_EQ(static_cast<int>(md->x.bit_size), 16);
    RA_CHECK_EQ(static_cast<int>(md->y.bit_size), 16);
    // Payload layout: 5 button bits + 3 padding bits + 16 X + 16 Y.
    RA_CHECK_EQ(static_cast<int>(md->x.bit_offset_in_payload), 8);
    RA_CHECK_EQ(static_cast<int>(md->y.bit_offset_in_payload), 24);
}

RA_TEST("HID: 16-bit mouse validates with correct BPF byte offsets")
{
    auto md = parse_mouse_descriptor(HIGH_DPI_MOUSE.data(), HIGH_DPI_MOUSE.size());
    RA_CHECK(md.has_value());
    auto dec = validate_for_bpf(*md);
    RA_CHECK(dec.layout.has_value());
    // Report ID byte 0; payload starts at byte 1. X at payload bit 8 -> byte 2;
    // Y at payload bit 24 -> byte 4.
    RA_CHECK_EQ(static_cast<int>(dec.layout->report_id), 1);
    RA_CHECK_EQ(static_cast<int>(dec.layout->dx_byte_offset), 2);
    RA_CHECK_EQ(static_cast<int>(dec.layout->dx_byte_size), 2);
    RA_CHECK_EQ(static_cast<int>(dec.layout->dy_byte_offset), 4);
    RA_CHECK_EQ(static_cast<int>(dec.layout->dy_byte_size), 2);
}

RA_TEST("HID: 12-bit packed descriptor is rejected by BPF validation")
{
    auto md = parse_mouse_descriptor(PACKED_12BIT.data(), PACKED_12BIT.size());
    RA_CHECK(md.has_value());
    auto dec = validate_for_bpf(*md);
    RA_CHECK(!dec.layout.has_value());
    RA_CHECK(dec.reject.has_value());
}

RA_TEST("HID: empty descriptor returns no mouse")
{
    auto md = parse_mouse_descriptor(nullptr, 0);
    RA_CHECK(!md.has_value());
}

RA_TEST("HID: descriptor without X/Y is rejected")
{
    // Buttons-only collection, no relative axes.
    std::vector<std::uint8_t> buttons_only = {
        0x05, 0x01,        // Usage Page (Generic Desktop)
        0x09, 0x02,        // Usage (Mouse)
        0xA1, 0x01,        // Collection (Application)
        0x05, 0x09,        //   Usage Page (Button)
        0x19, 0x01,        //   Usage Minimum (1)
        0x29, 0x08,        //   Usage Maximum (8)
        0x15, 0x00, 0x25, 0x01,
        0x95, 0x08, 0x75, 0x01,
        0x81, 0x02,        //   Input (Data, Var, Abs)
        0xC0,              // End Collection
    };
    auto md = parse_mouse_descriptor(buttons_only.data(), buttons_only.size());
    RA_CHECK(!md.has_value());
}
