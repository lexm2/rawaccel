//! Safe wrapper over `ra-backend-sys`. Owns the opaque backend handle; all the
//! `unsafe` FFI is confined here so the rest of the daemon is safe Rust.

use std::ffi::{CStr, CString};

use ra_backend_sys as sys;

/// Result of `ra_probe_capability`.
#[derive(Clone, Debug)]
pub struct ProbeResult {
    pub kernel_major: i32,
    pub kernel_minor: i32,
    pub kernel_ok: bool,
    pub syscall_ok: bool,
    pub reason: String,
}

impl ProbeResult {
    pub fn ok(&self) -> bool {
        self.kernel_ok && self.syscall_ok
    }
}

/// Probe whether this kernel + process can host the HID-BPF program.
pub fn probe_capability() -> ProbeResult {
    let mut r = sys::RaProbeResult::default();
    // SAFETY: `r` is a valid, fully-initialized out-param.
    unsafe { sys::ra_probe_capability(&mut r) };
    ProbeResult {
        kernel_major: r.kernel_major,
        kernel_minor: r.kernel_minor,
        kernel_ok: r.kernel_ok != 0,
        syscall_ok: r.syscall_ok != 0,
        reason: cstr_to_string(&r.reason),
    }
}

fn cstr_to_string(buf: &[std::os::raw::c_char]) -> String {
    // SAFETY: the C side always NUL-terminates these fixed buffers.
    let bytes = unsafe { CStr::from_ptr(buf.as_ptr()) };
    bytes.to_string_lossy().into_owned()
}

/// Owns a `ra_backend_t*`; destroyed on drop.
pub struct Backend {
    raw: *mut sys::RaBackend,
}

impl Backend {
    pub fn new(bpf_object_path: &str) -> anyhow::Result<Self> {
        let path = CString::new(bpf_object_path)?;
        // SAFETY: `path` outlives the call; the C side copies what it needs.
        let raw = unsafe { sys::ra_backend_create(path.as_ptr()) };
        if raw.is_null() {
            anyhow::bail!("ra_backend_create returned null (OOM?)");
        }
        Ok(Self { raw })
    }
}

impl Drop for Backend {
    fn drop(&mut self) {
        // SAFETY: `raw` came from ra_backend_create and is dropped once.
        unsafe { sys::ra_backend_destroy(self.raw) };
    }
}
