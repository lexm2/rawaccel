//! Contract test: Rust-built resolved JSON must parse in the real C++ json_io
//! via FFI. Unknown device id exercises only the parse path (bind no-ops on a
//! missing slot), so no libbpf/kernel interaction.

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

    // SAFETY: create/bind/destroy on one handle; unknown id only parses JSON, no kernel calls.
    unsafe {
        let be = sys::ra_backend_create(path.as_ptr());
        assert!(!be.is_null(), "ra_backend_create returned null");
        let rc = sys::ra_backend_bind(be, 999, rj_c.as_ptr());
        sys::ra_backend_destroy(be);
        assert_eq!(rc, 0, "C++ json_io rejected the Rust-built resolved_json");
    }
}

#[test]
fn cpp_backend_accepts_embedded_defaults() {
    // deactivate / no-match fallback feed the backend embedded defaults; json_io's
    // .at() requires them field-complete or bind throws -> rc != 0.
    let rj = config::resolved_json(&config::default_profile(), &config::default_device_config());

    let path = CString::new("/nonexistent.bpf.o").unwrap();
    let rj_c = CString::new(rj).unwrap();

    // SAFETY: as above, unknown id only exercises the JSON parse path.
    unsafe {
        let be = sys::ra_backend_create(path.as_ptr());
        assert!(!be.is_null(), "ra_backend_create returned null");
        let rc = sys::ra_backend_bind(be, 999, rj_c.as_ptr());
        sys::ra_backend_destroy(be);
        assert_eq!(rc, 0, "C++ json_io rejected the embedded default profile/config");
    }
}
