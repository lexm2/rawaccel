//! Raw FFI bindings to `libra_backend.so` (see `linux/agent/ra_backend.h`).
//!
//! These mirror the C ABI 1:1 and are all `unsafe` to call. Safe wrappers and
//! the device-orchestration logic live in the daemon crate; this crate is just
//! the boundary. The kernel structs (`ra_bpf_config`/`ra_bpf_state`) and the
//! curve math never cross - they stay inside the `.so`.

use std::os::raw::{c_char, c_int};

/// Mirrors `rawaccel_agent::BpfMouseLayout` (the one struct that crosses).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct RaMouseLayout {
    pub report_id: u8,
    pub dx_byte_offset: u8,
    pub dx_byte_size: u8,
    pub dy_byte_offset: u8,
    pub dy_byte_size: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RaProbeResult {
    pub kernel_major: c_int,
    pub kernel_minor: c_int,
    pub kernel_ok: c_int,
    pub syscall_ok: c_int,
    pub reason: [c_char; 128],
}

impl Default for RaProbeResult {
    fn default() -> Self {
        Self {
            kernel_major: 0,
            kernel_minor: 0,
            kernel_ok: 0,
            syscall_ok: 0,
            reason: [0; 128],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RaBackendHealth {
    pub devices: usize,
    pub attached: usize,
    pub error: [c_char; 256],
}

impl Default for RaBackendHealth {
    fn default() -> Self {
        Self {
            devices: 0,
            attached: 0,
            error: [0; 256],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct RaSpeedSample {
    pub x: f64,
    pub y: f64,
    pub combined: f64,
}

/// Opaque backend handle.
#[repr(C)]
pub struct RaBackend {
    _private: [u8; 0],
}

extern "C" {
    pub fn ra_probe_capability(out: *mut RaProbeResult) -> c_int;

    pub fn ra_backend_create(bpf_object_path: *const c_char) -> *mut RaBackend;
    pub fn ra_backend_destroy(be: *mut RaBackend);

    pub fn ra_backend_attach(
        be: *mut RaBackend,
        id: u64,
        hid_id: u32,
        sysname: *const c_char,
        layout: *const RaMouseLayout,
    ) -> c_int;
    pub fn ra_backend_detach(be: *mut RaBackend, id: u64);

    pub fn ra_backend_bind(
        be: *mut RaBackend,
        id: u64,
        resolved_json: *const c_char,
    ) -> c_int;

    pub fn ra_backend_health(be: *const RaBackend, out: *mut RaBackendHealth);
    pub fn ra_backend_speed(be: *const RaBackend, out: *mut RaSpeedSample);
}
