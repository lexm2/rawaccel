//! Shared helpers for the ported agent/control-server test suites. Builds
//! driver-config JSON `Value`s using the real cross-OS key names (json_io.hpp),
//! since the verbatim daemon stores and resolves opaque values.

#![allow(dead_code)]

use rawaccel_agentd::backend::DeviceInfo;
use rawaccel_agentd::config::DriverConfig;
use serde_json::{json, Value};

// Re-export so the suites can write `config::default_device_config()`.
pub use rawaccel_agentd::config;

/// json_io.hpp key::OUTPUT_DPI (profile discriminator used by the resolve tests).
pub const OUTPUT_DPI: &str = "Output DPI";
/// json_io.hpp key::DPI (device_config discriminator).
pub const DPI: &str = "DPI (normalizes input speed unit: counts/ms -> in/s)";

pub fn profile(name: &str, output_dpi: f64) -> Value {
    json!({ "name": name, OUTPUT_DPI: output_dpi })
}

pub fn device_config(dpi: i64) -> Value {
    json!({ DPI: dpi })
}

/// A device override entry; pass "" for fields that should not match.
pub fn device(id: &str, name: &str, prof: &str, config: Value) -> Value {
    json!({ "id": id, "name": name, "profile": prof, "config": config })
}

/// Assemble a driver-config Value (the shape `apply`/`from_value` consumes).
pub fn cfg_value(profiles: Vec<Value>, devices: Vec<Value>, default_dc: Value) -> Value {
    json!({
        "version": config::version_string(),
        "defaultDeviceConfig": default_dc,
        "profiles": profiles,
        "devices": devices,
    })
}

/// Single-empty-profile config used by the timing/debounce tests.
pub fn cfg_one_profile(name: &str, output_dpi: f64) -> DriverConfig {
    DriverConfig::from_value(cfg_value(
        vec![profile(name, output_dpi)],
        vec![],
        config::default_device_config(),
    ))
    .expect("test config is valid")
}

pub fn parse(v: Value) -> DriverConfig {
    DriverConfig::from_value(v).expect("test config is valid")
}

pub fn dev_info(id: u64, sysname: &str, device_sysname: &str, name: &str) -> DeviceInfo {
    DeviceInfo {
        id,
        sysname: sysname.into(),
        device_sysname: device_sysname.into(),
        name: name.into(),
        ..DeviceInfo::default()
    }
}
