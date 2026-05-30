//! Backend abstraction + the safe wrapper over `ra-backend-sys`. The `Agent`
//! resolves each device to a (profile, config) JSON pair before `bind_device`;
//! backends never see the full driver_config. Mirrors `linux/agent/backend.hpp`.
//! All `unsafe` FFI is confined to `FfiBackend`.

use std::collections::HashMap;
use std::ffi::{CStr, CString};

use ra_backend_sys as sys;
use serde_json::Value;

use crate::config;
use crate::hid::MouseLayout;

/// Best-effort device identity; only populated fields match driver_config.devices[].
#[derive(Clone, Debug, Default)]
pub struct DeviceInfo {
    pub id: u64,
    pub sysname: String,         // hidrawN
    pub device_sysname: String,  // 0003:VVVV:PPPP.IIII
    pub vendor_id: u32,
    pub product_id: u32,
    pub name: String,            // HID_NAME
}

/// Current input speed in chart units (normalized in/s). Mirrors `ra_speed_sample_t`.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct SpeedSample {
    pub x: f64,
    pub y: f64,
    pub combined: f64,
}

/// Kernel data-plane health: slots prepared vs actually attached; `error` is the
/// first attach failure. Lets the control plane fail an apply when nothing attached.
#[derive(Clone, Debug, Default)]
pub struct DataPlaneHealth {
    pub devices: usize,
    pub attached: usize,
    pub error: String,
}

/// Set/replace settings for one device, by resolved (profile, config) JSON.
pub trait Backend {
    /// Prepare a device slot (open/patch/load/attach). Default: no-op (Noop).
    fn attach(
        &mut self,
        _id: u64,
        _hid_id: u32,
        _sysname: &str,
        _layout: &MouseLayout,
    ) -> anyhow::Result<()> {
        Ok(())
    }
    fn bind_device(&mut self, id: u64, profile: &Value, config: &Value);
    /// Safe to call for an unknown id (no-op).
    fn unbind_device(&mut self, id: u64);
    fn current_speed_sample(&self) -> SpeedSample {
        SpeedSample::default()
    }
    fn health(&self) -> DataPlaneHealth {
        DataPlaneHealth::default()
    }
}

/// Test/default backend: records the resolved values per device.
#[derive(Default)]
pub struct NoopBackend {
    pub binds: u32,
    pub unbinds: u32,
    pub last_profile: HashMap<u64, Value>,
    pub last_config: HashMap<u64, Value>,
    pub forced_health: DataPlaneHealth,
    pub forced_speed: SpeedSample,
}

impl Backend for NoopBackend {
    fn bind_device(&mut self, id: u64, profile: &Value, config: &Value) {
        self.last_profile.insert(id, profile.clone());
        self.last_config.insert(id, config.clone());
        self.binds += 1;
    }
    fn unbind_device(&mut self, id: u64) {
        self.last_profile.remove(&id);
        self.last_config.remove(&id);
        self.unbinds += 1;
    }
    fn current_speed_sample(&self) -> SpeedSample {
        self.forced_speed
    }
    fn health(&self) -> DataPlaneHealth {
        self.forced_health.clone()
    }
}

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

/// Owns a `ra_backend_t*`; destroyed on drop. Drives the C++ HID-BPF data plane.
pub struct FfiBackend {
    raw: *mut sys::RaBackend,
}

impl FfiBackend {
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

impl Backend for FfiBackend {
    fn attach(
        &mut self,
        id: u64,
        hid_id: u32,
        sysname: &str,
        layout: &MouseLayout,
    ) -> anyhow::Result<()> {
        let sysname_c = CString::new(sysname)?;
        let l = sys::RaMouseLayout {
            report_id: layout.report_id,
            dx_byte_offset: layout.dx_byte_offset,
            dx_byte_size: layout.dx_byte_size,
            dy_byte_offset: layout.dy_byte_offset,
            dy_byte_size: layout.dy_byte_size,
        };
        // SAFETY: valid handle; sysname_c/&l outlive the call.
        let rc = unsafe { sys::ra_backend_attach(self.raw, id, hid_id, sysname_c.as_ptr(), &l) };
        if rc != 0 {
            anyhow::bail!("ra_backend_attach failed for {sysname} (rc={rc})");
        }
        Ok(())
    }

    fn bind_device(&mut self, id: u64, profile: &Value, config: &Value) {
        let rj = config::resolved_json(profile, config);
        let Ok(rj_c) = CString::new(rj) else { return };
        // SAFETY: valid handle; rj_c outlives the call. -1 (unknown id / parse /
        // map-write) is non-fatal here; health() surfaces a dead data plane.
        unsafe { sys::ra_backend_bind(self.raw, id, rj_c.as_ptr()) };
    }

    fn unbind_device(&mut self, id: u64) {
        // SAFETY: valid handle; detach on an unknown id is a no-op C-side.
        unsafe { sys::ra_backend_detach(self.raw, id) };
    }

    fn current_speed_sample(&self) -> SpeedSample {
        let mut s = sys::RaSpeedSample::default();
        // SAFETY: valid handle; `s` is a valid out-param.
        unsafe { sys::ra_backend_speed(self.raw, &mut s) };
        SpeedSample {
            x: s.x,
            y: s.y,
            combined: s.combined,
        }
    }

    fn health(&self) -> DataPlaneHealth {
        let mut h = sys::RaBackendHealth::default();
        // SAFETY: valid handle; `h` is a valid out-param.
        unsafe { sys::ra_backend_health(self.raw, &mut h) };
        DataPlaneHealth {
            devices: h.devices,
            attached: h.attached,
            error: cstr_to_string(&h.error),
        }
    }
}

impl Drop for FfiBackend {
    fn drop(&mut self) {
        // SAFETY: `raw` came from ra_backend_create and is dropped once.
        unsafe { sys::ra_backend_destroy(self.raw) };
    }
}
