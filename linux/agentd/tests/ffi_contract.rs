//! Cross-language contract test: the resolved-settings JSON built by the Rust
//! config layer must be parseable by the real C++ json_io via the FFI. Uses an
//! unknown device id so ra_backend_bind only exercises the JSON parse path
//! (bind_device no-ops on a missing slot) - no libbpf/kernel interaction.

use std::ffi::CString;

use ra_backend_sys as sys;
use rawaccel_agentd::config::{self, DriverConfig};

const FIXTURE: &str = include_str!("../../tests/fixtures/default_config.json");

#[test]
fn cpp_backend_accepts_rust_resolved_json() {
    let cfg = DriverConfig::from_json_str(FIXTURE).expect("parse fixture");
    let rj = config::resolved_json(&cfg.profiles[0].value, &cfg.default_device_config);

    let path = CString::new("/nonexistent.bpf.o").unwrap();
    let rj_c = CString::new(rj).unwrap();

    // SAFETY: create/bind/destroy on a single handle; bind with an unknown id
    // only parses the JSON then no-ops (no slot), so no kernel calls happen.
    unsafe {
        let be = sys::ra_backend_create(path.as_ptr());
        assert!(!be.is_null(), "ra_backend_create returned null");
        let rc = sys::ra_backend_bind(be, 999, rj_c.as_ptr());
        sys::ra_backend_destroy(be);
        assert_eq!(rc, 0, "C++ json_io rejected the Rust-built resolved_json");
    }
}
