//! Userspace counterpart to the driver's DEVICE_EXTENSION: holds the active
//! config, debounces updates by WRITE_DELAY, resolves each device to a
//! (profile, config) JSON pair. Port of `linux/agent/agent.cpp`, verbatim model:
//! profiles/configs stay opaque `serde_json::Value`s, matched by plain string
//! equality (no wchar_t fixed buffers). Single-threaded; the C++ mutex is gone
//! because discovery is discover-once at startup, not a concurrent thread.

use std::collections::HashMap;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use serde_json::Value;

use crate::backend::{Backend, DeviceInfo, SpeedSample};
use crate::config::{self, DriverConfig};

/// Anti-abuse write debounce, mirrors `ra::WRITE_DELAY` (1000 ms).
pub const WRITE_DELAY: Duration = Duration::from_millis(1000);

type Version = (i32, i32, i32);

pub fn agent_version() -> Version {
    (
        env!("RA_VER_MAJOR").parse().unwrap(),
        env!("RA_VER_MINOR").parse().unwrap(),
        env!("RA_VER_PATCH").parse().unwrap(),
    )
}

pub fn min_driver_version() -> Version {
    (
        env!("RA_MIN_VER_MAJOR").parse().unwrap(),
        env!("RA_MIN_VER_MINOR").parse().unwrap(),
        env!("RA_MIN_VER_PATCH").parse().unwrap(),
    )
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum VersionStatus {
    Ok,
    ClientTooOld, // "reinstallation required"
    ClientTooNew, // "newer driver is installed"
}

#[derive(Clone, Debug)]
pub struct VersionCheck {
    pub status: VersionStatus,
    pub agent_version: Version,
    pub message: String, // empty on ok
}

#[derive(Clone, Debug)]
pub struct Status {
    pub has_active_config: bool,
    pub has_pending_apply: bool,
    pub until_apply_ms: i64, // 0 if no pending
    pub last_apply_unix_ms: i64,
    pub connected_devices: usize,
}

pub struct Agent<B: Backend> {
    backend: B,
    active: DriverConfig,
    has_active: bool,
    pending: Option<(DriverConfig, Instant)>,
    last_apply_unix_ms: i64,
    known_devices: HashMap<u64, DeviceInfo>,
}

impl<B: Backend> Agent<B> {
    pub fn new(backend: B) -> Self {
        Self {
            backend,
            active: DriverConfig::empty(),
            has_active: false,
            pending: None,
            last_apply_unix_ms: 0,
            known_devices: HashMap::new(),
        }
    }

    pub fn backend(&self) -> &B {
        &self.backend
    }
    pub fn backend_mut(&mut self) -> &mut B {
        &mut self.backend
    }

    /// Agent plays the driver role: too-old clients upgrade, too-new downgrade.
    pub fn check_version(&self, client: Version) -> VersionCheck {
        let agent = agent_version();
        if client < min_driver_version() {
            VersionCheck {
                status: VersionStatus::ClientTooOld,
                agent_version: agent,
                message: "client below minimum supported version".into(),
            }
        } else if agent < client {
            VersionCheck {
                status: VersionStatus::ClientTooNew,
                agent_version: agent,
                message: "agent is older than client".into(),
            }
        } else {
            VersionCheck {
                status: VersionStatus::Ok,
                agent_version: agent,
                message: String::new(),
            }
        }
    }

    /// Stage a config
    /// calls within WRITE_DELAY collapse to the latest, tick() commits.
    pub fn schedule_apply(&mut self, cfg: DriverConfig, now: Instant) {
        self.pending = Some((cfg, now + WRITE_DELAY));
    }

    /// Commit a pending apply once its deadline passes
    /// rebinds known devices.
    /// Returns true if it fired.
    pub fn tick(&mut self, now: Instant) -> bool {
        match &self.pending {
            Some((_, at)) if now >= *at => {}
            _ => return false,
        }
        let (cfg, _) = self.pending.take().unwrap();
        self.apply(cfg);
        self.last_apply_unix_ms = unix_now_ms();
        self.rebind_all();
        true
    }

    /// Reset to embedded default (noaccel) now, bypassing WRITE_DELAY
    /// stays "active" with a default config rather than reporting none.
    pub fn deactivate(&mut self) {
        self.pending = None;
        self.apply(DriverConfig::empty());
        self.last_apply_unix_ms = unix_now_ms();
        self.rebind_all();
    }

    /// Sets active state only
    /// the apply timestamp is stamped by tick/deactivate, not by startup load (matches the C++ port).
    fn apply(&mut self, cfg: DriverConfig) {
        self.active = cfg;
        self.has_active = true;
    }

    fn rebind_all(&mut self) {
        let binds = self.collect_binds();
        for (id, profile, config) in binds {
            self.backend.bind_device(id, &profile, &config);
        }
    }

    pub fn get_active(&self) -> &DriverConfig {
        &self.active
    }

    pub fn active_value(&self) -> &Value {
        self.active.as_value()
    }

    pub fn current_speed_sample(&self) -> SpeedSample {
        self.backend.current_speed_sample()
    }

    pub fn status(&self, now: Instant) -> Status {
        let until_apply_ms = match &self.pending {
            Some((_, at)) if now < *at => (*at - now).as_millis() as i64,
            _ => 0,
        };
        Status {
            has_active_config: self.has_active,
            has_pending_apply: self.pending.is_some(),
            until_apply_ms,
            last_apply_unix_ms: self.last_apply_unix_ms,
            connected_devices: self.known_devices.len(),
        }
    }

    /// Non-empty when devices exist but none are attached (apply would no-op)
    /// surfaced as an apply error so the GUI/CLI doesn't report a false success.
    pub fn data_plane_failure(&self) -> Option<String> {
        let h = self.backend.health();
        if h.devices > 0 && h.attached == 0 {
            Some(if h.error.is_empty() {
                "no connected device could be attached to the kernel data plane".into()
            } else {
                h.error
            })
        } else {
            None
        }
    }

    pub fn load_from_file(&mut self, path: &str) -> bool {
        let Ok(text) = std::fs::read_to_string(path) else {
            return false;
        };
        let Ok(cfg) = DriverConfig::from_json_str(&text) else {
            return false;
        };
        // startup load skips WRITE_DELAY (debounce only guards apply-RPC bursts)
        self.apply(cfg);
        self.rebind_all();
        true
    }

    pub fn save_to_file(&self, path: &str) -> bool {
        std::fs::write(path, self.active.to_pretty_string()).is_ok()
    }

    /// Prepare a device's kernel slot, then register it (resolve + bind if a
    /// config is active). Mirrors the C++ attach_node -> on_device_added flow.
    pub fn attach_device(
        &mut self,
        info: DeviceInfo,
        hid_id: u32,
        layout: &crate::hid::MouseLayout,
    ) -> anyhow::Result<()> {
        self.backend.attach(info.id, hid_id, &info.sysname, layout)?;
        self.on_device_added(info);
        Ok(())
    }

    pub fn on_device_added(&mut self, info: DeviceInfo) {
        let id = info.id;
        let resolved = self.has_active.then(|| self.resolve(&info));
        self.known_devices.insert(id, info);
        if let Some((profile, config)) = resolved {
            self.backend.bind_device(id, &profile, &config);
        }
    }

    pub fn on_device_removed(&mut self, id: u64) {
        if self.known_devices.remove(&id).is_some() {
            self.backend.unbind_device(id);
        }
    }

    /// Port of resolve_locked. First device entry whose id (== device_sysname) or
    /// name matches wins, in list order
    /// empty config fields never match. No match
    /// -> first profile (or embedded default) + default_device_config.
    fn resolve(&self, info: &DeviceInfo) -> (Value, Value) {
        let default_profile = self
            .active
            .profiles
            .first()
            .map(|p| p.value.clone())
            .unwrap_or_else(config::default_profile);
        let default_config = self.active.default_device_config.clone();

        for dev in &self.active.devices {
            let id_match = !dev.id.is_empty() && dev.id == info.device_sysname;
            let name_match = !dev.name.is_empty() && dev.name == info.name;
            if !id_match && !name_match {
                continue;
            }
            let mut profile = default_profile.clone();
            if !dev.profile.is_empty() {
                if let Some(p) = self.active.profiles.iter().find(|p| p.name == dev.profile) {
                    profile = p.value.clone();
                }
            }
            // dev.config Null when absent: fall back to default_device_config (can't synthesize one).
            let config = if dev.config.is_object() {
                dev.config.clone()
            } else {
                default_config.clone()
            };
            return (profile, config);
        }
        (default_profile, default_config)
    }

    fn collect_binds(&self) -> Vec<(u64, Value, Value)> {
        self.known_devices
            .values()
            .map(|info| {
                let (p, c) = self.resolve(info);
                (info.id, p, c)
            })
            .collect()
    }
}

fn unix_now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}
