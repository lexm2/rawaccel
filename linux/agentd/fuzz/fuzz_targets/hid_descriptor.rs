#![no_main]
//! Fuzz the HID report-descriptor parser: it consumes untrusted bytes from
//! sysfs, so it must never panic or read out of bounds on any input.

use libfuzzer_sys::fuzz_target;
use rawaccel_agentd::hid;

fuzz_target!(|data: &[u8]| {
    if let Some(desc) = hid::parse_mouse_descriptor(data) {
        let _ = hid::validate_for_bpf(&desc);
    }
});
