#![no_main]
//! Fuzz the driver-config JSON parser: settings.json is user-supplied, so
//! parsing arbitrary text must fail gracefully, never panic.

use libfuzzer_sys::fuzz_target;
use rawaccel_agentd::config::DriverConfig;

fuzz_target!(|data: &[u8]| {
    if let Ok(s) = std::str::from_utf8(data) {
        let _ = DriverConfig::from_json_str(s);
    }
});
