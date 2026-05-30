//! HID report-descriptor parser for the BPF backend's needs: relative X/Y Input
//! fields. Safe-Rust port of C++ hid_descriptor.cpp; `validate_for_bpf` enforces
//! a conservative shape (byte-aligned, 8/16-bit signed, Mouse/Pointer collection).
//! Every byte is read via `slice::get`, so malformed input returns `None`, not OOB.

// Hard caps: untrusted descriptors; reject anything that explodes memory or wraps arithmetic.
const MAX_REPORT_SIZE: u32 = 64;
const MAX_REPORT_COUNT: u32 = 1024;
const MAX_REPORT_BITS: u32 = 1 << 16; // 8 KiB per report
const MAX_LOCAL_USAGES: usize = 1024;
const MAX_USAGE_RANGE: u32 = 256;

// Item-prefix types.
const TYPE_MAIN: u8 = 0;
const TYPE_GLOBAL: u8 = 1;
const TYPE_LOCAL: u8 = 2;

// Main item tags.
const TAG_INPUT: u8 = 0x8;
const TAG_COLLECTION: u8 = 0xA;
const TAG_END_COLLEC: u8 = 0xC;

// Global item tags.
const TAG_USAGE_PAGE: u8 = 0x0;
const TAG_LOG_MIN: u8 = 0x1;
const TAG_LOG_MAX: u8 = 0x2;
const TAG_REPORT_SIZE: u8 = 0x7;
const TAG_REPORT_ID: u8 = 0x8;
const TAG_REPORT_COUNT: u8 = 0x9;
const TAG_PUSH: u8 = 0xA;
const TAG_POP: u8 = 0xB;

// Local item tags.
const TAG_USAGE: u8 = 0x0;
const TAG_USAGE_MIN: u8 = 0x1;
const TAG_USAGE_MAX: u8 = 0x2;

// Usage page / usages we care about.
const UP_GENERIC_DESKTOP: u32 = 0x01;
const USAGE_POINTER: u32 = 0x01;
const USAGE_MOUSE: u32 = 0x02;
const USAGE_X: u32 = 0x30;
const USAGE_Y: u32 = 0x31;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MouseAxis {
    pub present: bool,
    pub bit_offset_in_payload: u32, // excludes report-ID prefix
    pub bit_size: u32,
    pub is_signed: bool,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MouseDescriptor {
    pub has_report_id: bool,
    pub report_id: u8,
    pub x: MouseAxis,
    pub y: MouseAxis,
    pub report_bits: u32, // payload bits, excludes report-ID prefix
}

/// Byte offsets/sizes the BPF program reads (mirrors C++ BpfMouseLayout).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MouseLayout {
    pub report_id: u8, // 0 when no report ID byte is used
    pub dx_byte_offset: u8,
    pub dx_byte_size: u8,
    pub dy_byte_offset: u8,
    pub dy_byte_size: u8,
}

#[derive(Clone, Copy, Default)]
struct GlobalState {
    usage_page: u32,
    logical_min: i32,
    report_size: u32,
    report_count: u32,
    has_report_id: bool,
    report_id: u8,
}

#[derive(Clone, Copy, Default)]
struct PerReport {
    report_id: u8,
    has_report_id: bool,
    bit_offset: u32, // running bit cursor
    x: MouseAxis,
    y: MouseAxis,
}

fn read_signed(p: &[u8]) -> i32 {
    let n = p.len();
    if n == 0 {
        return 0;
    }
    let mut v: i32 = 0;
    for (i, &b) in p.iter().enumerate() {
        v |= (b as i32) << (8 * i);
    }
    if n < 4 {
        let mask = 1i32 << (8 * n - 1);
        if v & mask != 0 {
            v |= !((1i32 << (8 * n)) - 1);
        }
    }
    v
}

fn read_unsigned(p: &[u8]) -> u32 {
    let mut v: u32 = 0;
    for (i, &b) in p.iter().enumerate() {
        v |= (b as u32) << (8 * i);
    }
    v
}

fn is_relative(flags: u32) -> bool {
    flags & 0x04 != 0 // bit 2 = Relative
}
fn is_variable(flags: u32) -> bool {
    flags & 0x02 != 0 // bit 1 = Variable
}
fn is_data(flags: u32) -> bool {
    flags & 0x01 == 0 // bit 0 = Constant when set
}

// False on overflow; caller abandons the descriptor.
fn process_input(g: &GlobalState, usages: &[u32], flags: u32, rep: &mut PerReport) -> bool {
    if g.report_size > MAX_REPORT_SIZE || g.report_count > MAX_REPORT_COUNT {
        return false;
    }
    // ReportCount x ReportSize bits, one Usage each; repeat last (HID 1.11 6.2.2.7).
    for i in 0..g.report_count {
        let usage = if usages.is_empty() {
            0
        } else {
            *usages
                .get(i as usize)
                .unwrap_or_else(|| usages.last().unwrap())
        };

        if is_variable(flags) && is_data(flags) && is_relative(flags) && g.usage_page == UP_GENERIC_DESKTOP {
            if usage == USAGE_X && !rep.x.present {
                rep.x = MouseAxis {
                    present: true,
                    bit_offset_in_payload: rep.bit_offset,
                    bit_size: g.report_size,
                    is_signed: g.logical_min < 0,
                };
            } else if usage == USAGE_Y && !rep.y.present {
                rep.y = MouseAxis {
                    present: true,
                    bit_offset_in_payload: rep.bit_offset,
                    bit_size: g.report_size,
                    is_signed: g.logical_min < 0,
                };
            }
        }

        if rep.bit_offset > MAX_REPORT_BITS - g.report_size {
            return false;
        }
        rep.bit_offset += g.report_size;
    }
    true
}

fn commit_if_complete(rep: &PerReport, result: &mut Option<MouseDescriptor>) {
    if rep.x.present && rep.y.present && result.is_none() {
        *result = Some(MouseDescriptor {
            has_report_id: rep.has_report_id,
            report_id: rep.report_id,
            x: rep.x,
            y: rep.y,
            report_bits: rep.bit_offset,
        });
    }
}

pub fn parse_mouse_descriptor(desc: &[u8]) -> Option<MouseDescriptor> {
    let len = desc.len();
    let mut g = GlobalState::default();
    let mut push_stack: Vec<GlobalState> = Vec::new();
    let mut usages: Vec<u32> = Vec::new();
    let mut usage_min: i32 = 0;
    let mut usage_min_set = false;
    let mut usage_max_set = false;

    let mut coll_usages: Vec<u32> = Vec::new();
    // Depth (coll_usages.len()) at which we entered a mouse/pointer collection; None when outside one.
    let mut mouse_depth: Option<usize> = None;
    let mut rep = PerReport::default();
    let mut result: Option<MouseDescriptor> = None;

    // Main items reset Local items (HID 1.11 6.2.2.8).
    macro_rules! flush_locals {
        () => {{
            usages.clear();
            usage_min_set = false;
            usage_max_set = false;
        }};
    }

    let mut i = 0usize;
    while i < len {
        let prefix = desc[i];
        i += 1;

        if prefix == 0xFE {
            // long item: bSize + bLongItemTag + data
            let dsize = *desc.get(i)? as usize;
            i += 1;
            if i >= len {
                return None; // long tag byte
            }
            i += 1;
            if i + dsize > len {
                return None; // match short-item bound
            }
            i += dsize;
            continue;
        }

        let bsize_code = prefix & 0x03;
        let btype = (prefix >> 2) & 0x03;
        let btag = (prefix >> 4) & 0x0F;
        let dsize = if bsize_code == 3 { 4 } else { bsize_code as usize };
        let dp = desc.get(i..i + dsize)?;
        i += dsize;

        if btype == TYPE_GLOBAL {
            let sv = read_signed(dp);
            let uv = read_unsigned(dp);
            match btag {
                TAG_USAGE_PAGE => g.usage_page = uv,
                TAG_LOG_MIN => g.logical_min = sv,
                TAG_LOG_MAX => {} // logical_max unused by the BPF layout
                TAG_REPORT_SIZE => {
                    if uv == 0 {
                        return None; // spec-invalid (HID 1.11 6.2.2.7)
                    }
                    g.report_size = uv;
                }
                TAG_REPORT_COUNT => g.report_count = uv,
                TAG_REPORT_ID => {
                    if uv > 0xFF {
                        return None; // Report ID is one byte (HID 1.11 6.2.2.7)
                    }
                    g.has_report_id = true;
                    g.report_id = uv as u8;
                    // new Report ID resets the cursor; commit the prior report
                    if rep.report_id != g.report_id || rep.has_report_id != g.has_report_id {
                        commit_if_complete(&rep, &mut result);
                        rep = PerReport {
                            has_report_id: g.has_report_id,
                            report_id: g.report_id,
                            ..PerReport::default()
                        };
                    }
                }
                TAG_PUSH => push_stack.push(g),
                TAG_POP => {
                    if let Some(prev) = push_stack.pop() {
                        g = prev;
                    }
                }
                _ => {}
            }
        } else if btype == TYPE_LOCAL {
            let uv = read_unsigned(dp);
            match btag {
                TAG_USAGE => {
                    if usages.len() >= MAX_LOCAL_USAGES {
                        return None;
                    }
                    usages.push(uv);
                }
                TAG_USAGE_MIN => {
                    usage_min = uv as i32;
                    usage_min_set = true;
                }
                TAG_USAGE_MAX => {
                    let usage_max = uv as i32;
                    usage_max_set = true;
                    if usage_min_set {
                        // reject nonsensical ranges before they expand
                        if usage_max < usage_min {
                            return None;
                        }
                        let span = (usage_max - usage_min) as u32;
                        if span > MAX_USAGE_RANGE {
                            return None;
                        }
                        if usages.len() + span as usize + 1 > MAX_LOCAL_USAGES {
                            return None;
                        }
                        for u in usage_min..=usage_max {
                            usages.push(u as u32);
                        }
                        usage_min_set = false;
                        usage_max_set = false;
                    }
                }
                _ => {}
            }
            let _ = usage_max_set;
        } else if btype == TYPE_MAIN {
            let flags = read_unsigned(dp);
            if btag == TAG_COLLECTION {
                let coll_usage = usages.first().copied().unwrap_or(0);
                coll_usages.push(coll_usage);
                // Record only the outermost mouse/pointer collection's depth.
                if mouse_depth.is_none()
                    && g.usage_page == UP_GENERIC_DESKTOP
                    && (coll_usage == USAGE_MOUSE || coll_usage == USAGE_POINTER)
                {
                    mouse_depth = Some(coll_usages.len());
                }
                flush_locals!();
            } else if btag == TAG_END_COLLEC {
                coll_usages.pop()?; // None on unmatched End Collection
                // Clear once we pop out of the collection that set the flag.
                if matches!(mouse_depth, Some(d) if coll_usages.len() < d) {
                    mouse_depth = None;
                }
                if coll_usages.is_empty() {
                    commit_if_complete(&rep, &mut result);
                    // result is only ever set from captures made inside a mouse collection.
                    if result.is_some() {
                        return result;
                    }
                }
                flush_locals!();
            } else if btag == TAG_INPUT {
                if g.report_size > MAX_REPORT_SIZE || g.report_count > MAX_REPORT_COUNT {
                    return None;
                }
                if mouse_depth.is_some() {
                    if !process_input(&g, &usages, flags, &mut rep) {
                        return None;
                    }
                } else {
                    let advance = g.report_size as u64 * g.report_count as u64;
                    if advance > (MAX_REPORT_BITS - rep.bit_offset) as u64 {
                        return None;
                    }
                    rep.bit_offset += advance as u32;
                }
                flush_locals!();
            } else {
                // Output/Feature: no Input layout, but still flush Locals
                flush_locals!();
            }
        }
    }

    commit_if_complete(&rep, &mut result);
    result
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum BpfDecision {
    Accept(MouseLayout),
    Reject(String),
}

pub fn validate_for_bpf(d: &MouseDescriptor) -> BpfDecision {
    use BpfDecision::Reject;
    if !d.x.present || !d.y.present {
        return Reject("X or Y axis missing".into());
    }
    if d.x.bit_size != 8 && d.x.bit_size != 16 {
        return Reject("X is not 8 or 16 bits".into());
    }
    if d.y.bit_size != 8 && d.y.bit_size != 16 {
        return Reject("Y is not 8 or 16 bits".into());
    }
    if d.x.bit_offset_in_payload % 8 != 0 {
        return Reject("X is not byte-aligned".into());
    }
    if d.y.bit_offset_in_payload % 8 != 0 {
        return Reject("Y is not byte-aligned".into());
    }
    if !d.x.is_signed {
        return Reject("X is unsigned".into());
    }
    if !d.y.is_signed {
        return Reject("Y is unsigned".into());
    }

    // layout offsets are u8; reject >255 (past any real mouse report)
    let prefix = if d.has_report_id { 1u32 } else { 0 };
    let dx_off = prefix + d.x.bit_offset_in_payload / 8;
    let dy_off = prefix + d.y.bit_offset_in_payload / 8;
    if dx_off > 255 || dy_off > 255 {
        return Reject("X or Y offset exceeds 255 bytes".into());
    }

    BpfDecision::Accept(MouseLayout {
        report_id: if d.has_report_id { d.report_id } else { 0 },
        dx_byte_offset: dx_off as u8,
        dx_byte_size: (d.x.bit_size / 8) as u8,
        dy_byte_offset: dy_off as u8,
        dy_byte_size: (d.y.bit_size / 8) as u8,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    // Boot-mouse descriptor (HID 1.11 Appendix E.10): 3 buttons + 5 padding
    // bits + signed 8-bit X + signed 8-bit Y. 3-byte report, no report ID.
    const BOOT_MOUSE: &[u8] = &[
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29,
        0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
        0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95,
        0x02, 0x81, 0x06, 0xC0, 0xC0,
    ];

    // 16-bit X/Y with a Report ID prefix (Logitech G Pro shape, high-DPI).
    const HIGH_DPI_MOUSE: &[u8] = &[
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19,
        0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01,
        0x75, 0x03, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF,
        0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0,
    ];

    // 12-bit X packed alongside Y in a 24-bit field; not byte-aligned -> rejected.
    const PACKED_12BIT: &[u8] = &[
        0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x01, 0x09, 0x30, 0x09,
        0x31, 0x16, 0x01, 0xF8, 0x26, 0xFF, 0x07, 0x75, 0x0C, 0x95, 0x02, 0x81, 0x06, 0xC0, 0xC0,
    ];

    #[test]
    fn boot_mouse_places_x_at_byte1_y_at_byte2() {
        let md = parse_mouse_descriptor(BOOT_MOUSE).expect("parse");
        assert!(!md.has_report_id);
        assert!(md.x.present && md.y.present);
        assert_eq!(md.x.bit_offset_in_payload, 8);
        assert_eq!(md.x.bit_size, 8);
        assert_eq!(md.y.bit_offset_in_payload, 16);
        assert_eq!(md.y.bit_size, 8);
        assert!(md.x.is_signed && md.y.is_signed);
    }

    #[test]
    fn boot_mouse_validates() {
        let md = parse_mouse_descriptor(BOOT_MOUSE).unwrap();
        match validate_for_bpf(&md) {
            BpfDecision::Accept(l) => {
                assert_eq!(l.report_id, 0);
                assert_eq!(l.dx_byte_offset, 1);
                assert_eq!(l.dx_byte_size, 1);
                assert_eq!(l.dy_byte_offset, 2);
                assert_eq!(l.dy_byte_size, 1);
            }
            BpfDecision::Reject(r) => panic!("rejected: {r}"),
        }
    }

    #[test]
    fn high_dpi_report_id_offsets() {
        let md = parse_mouse_descriptor(HIGH_DPI_MOUSE).unwrap();
        assert!(md.has_report_id);
        assert_eq!(md.report_id, 1);
        assert_eq!(md.x.bit_size, 16);
        assert_eq!(md.y.bit_size, 16);
        assert_eq!(md.x.bit_offset_in_payload, 8);
        assert_eq!(md.y.bit_offset_in_payload, 24);
    }

    #[test]
    fn high_dpi_validates_with_byte_offsets() {
        let md = parse_mouse_descriptor(HIGH_DPI_MOUSE).unwrap();
        match validate_for_bpf(&md) {
            BpfDecision::Accept(l) => {
                assert_eq!(l.report_id, 1);
                assert_eq!(l.dx_byte_offset, 2);
                assert_eq!(l.dx_byte_size, 2);
                assert_eq!(l.dy_byte_offset, 4);
                assert_eq!(l.dy_byte_size, 2);
            }
            BpfDecision::Reject(r) => panic!("rejected: {r}"),
        }
    }

    #[test]
    fn packed_12bit_rejected() {
        let md = parse_mouse_descriptor(PACKED_12BIT).unwrap();
        assert!(matches!(validate_for_bpf(&md), BpfDecision::Reject(_)));
    }

    #[test]
    fn empty_descriptor_none() {
        assert!(parse_mouse_descriptor(&[]).is_none());
    }

    #[test]
    fn report_size_zero_rejected() {
        let bad = [0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x75, 0x00, 0xC0];
        assert!(parse_mouse_descriptor(&bad).is_none());
    }

    #[test]
    fn report_id_over_255_rejected() {
        // Report ID (256) via 2-byte item
        let bad = [0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x86, 0x00, 0x01, 0xC0];
        assert!(parse_mouse_descriptor(&bad).is_none());
    }

    #[test]
    fn unmatched_end_collection_rejected() {
        let bad = [0xC0u8]; // End Collection, empty stack
        assert!(parse_mouse_descriptor(&bad).is_none());
    }

    #[test]
    fn truncated_long_item_no_overread() {
        // long-item prefix claiming 5 data bytes in a 4-byte buffer
        let bad = [0xFEu8, 0x05, 0x00, 0xAA];
        assert!(parse_mouse_descriptor(&bad).is_none());
    }

    #[test]
    fn descriptor_without_xy_rejected() {
        // buttons-only, no relative axes
        let buttons_only = [
            0x05u8, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00,
            0x25, 0x01, 0x95, 0x08, 0x75, 0x01, 0x81, 0x02, 0xC0,
        ];
        assert!(parse_mouse_descriptor(&buttons_only).is_none());
    }
}
