//! Driver-config model. The cross-OS JSON contract (key names, shape) is
//! defined by json_io.cpp / wrapper.cpp; this mirrors it. The daemon only
//! interprets the handful of fields it needs for device resolution; the rest
//! (the acceleration math knobs) pass through as opaque serde_json Values, so
//! there is no numeric re-derivation and no chance of format drift. Resolved
//! settings are handed to the C++ backend as verbatim sub-objects.

use anyhow::{anyhow, Context, Result};
use serde_json::{Map, Value};

/// Canonical default profile (noaccel) and device_config, generated from the C++
/// json_io serializer (`ra-config-dump`) so they are guaranteed `.at()`-complete
/// when the backend reparses them. Used as the resolve fallback and on deactivate.
pub const DEFAULT_PROFILE_JSON: &str = include_str!("../assets/default_profile.json");
pub const DEFAULT_DEVICE_CONFIG_JSON: &str = include_str!("../assets/default_device_config.json");

/// Parsed embedded default profile. Panics only if the checked-in asset is invalid.
pub fn default_profile() -> Value {
    serde_json::from_str(DEFAULT_PROFILE_JSON).expect("embedded default_profile.json is valid JSON")
}

/// Parsed embedded default device_config.
pub fn default_device_config() -> Value {
    serde_json::from_str(DEFAULT_DEVICE_CONFIG_JSON)
        .expect("embedded default_device_config.json is valid JSON")
}

/// "major.minor.patch" from the build-embedded version (mirrors RA_VER_STRING).
pub fn version_string() -> String {
    format!(
        "{}.{}.{}",
        env!("RA_VER_MAJOR"),
        env!("RA_VER_MINOR"),
        env!("RA_VER_PATCH")
    )
}

/// JSON keys the daemon interprets (subset of json_io.hpp `key::`).
mod key {
    pub const VERSION: &str = "version";
    pub const DEFAULT_DEVICE_CONFIG: &str = "defaultDeviceConfig";
    pub const PROFILES: &str = "profiles";
    pub const DEVICES: &str = "devices";
    pub const NAME: &str = "name"; // profile name and device name
    pub const DEVICE_PROFILE: &str = "profile";
    pub const DEVICE_ID: &str = "id";
    pub const DEVICE_CONFIG: &str = "config";
}

/// A profile (== one `modifier_settings`). `value` is the full profile object,
/// handed verbatim to the backend so the math knobs never round-trip through us.
#[derive(Clone, Debug)]
pub struct Profile {
    pub name: String,
    pub value: Value,
}

/// A per-device override entry.
#[derive(Clone, Debug)]
pub struct Device {
    pub name: String,
    pub profile: String,
    pub id: String,
    pub config: Value,
}

#[derive(Clone, Debug)]
pub struct DriverConfig {
    raw: Value,
    pub version: String,
    pub default_device_config: Value,
    pub profiles: Vec<Profile>,
    pub devices: Vec<Device>,
}

impl DriverConfig {
    /// Parse from a JSON string (e.g. a settings.json file).
    pub fn from_json_str(s: &str) -> Result<Self> {
        let v: Value = serde_json::from_str(s).context("config is not valid JSON")?;
        Self::from_value(v)
    }

    /// Build from an already-parsed Value (e.g. the `config` field of an apply
    /// request). Required fields mirror json_io.cpp's `.at(...)` (fail-loud).
    pub fn from_value(v: Value) -> Result<Self> {
        let obj = v
            .as_object()
            .ok_or_else(|| anyhow!("config is not a JSON object"))?;

        let version = obj
            .get(key::VERSION)
            .and_then(Value::as_str)
            .unwrap_or("")
            .to_string();

        let default_device_config = obj
            .get(key::DEFAULT_DEVICE_CONFIG)
            .cloned()
            .ok_or_else(|| anyhow!("missing '{}'", key::DEFAULT_DEVICE_CONFIG))?;

        let profiles = obj
            .get(key::PROFILES)
            .and_then(Value::as_array)
            .ok_or_else(|| anyhow!("missing or non-array '{}'", key::PROFILES))?
            .iter()
            .map(|p| Profile {
                name: str_field(p, key::NAME),
                value: p.clone(),
            })
            .collect();

        let devices = obj
            .get(key::DEVICES)
            .and_then(Value::as_array)
            .ok_or_else(|| anyhow!("missing or non-array '{}'", key::DEVICES))?
            .iter()
            .map(|d| Device {
                name: str_field(d, key::NAME),
                profile: str_field(d, key::DEVICE_PROFILE),
                id: str_field(d, key::DEVICE_ID),
                config: d.get(key::DEVICE_CONFIG).cloned().unwrap_or(Value::Null),
            })
            .collect();

        Ok(Self {
            raw: v,
            version,
            default_device_config,
            profiles,
            devices,
        })
    }

    /// Empty/default config: no profiles or devices, embedded default
    /// device_config. Mirrors a default-constructed C++ `driver_config` and is
    /// the active state before the first apply and after a deactivate.
    pub fn empty() -> Self {
        let default_device_config = default_device_config();
        let mut m = Map::new();
        m.insert(key::VERSION.to_string(), Value::String(version_string()));
        m.insert(
            key::DEFAULT_DEVICE_CONFIG.to_string(),
            default_device_config.clone(),
        );
        m.insert(key::PROFILES.to_string(), Value::Array(vec![]));
        m.insert(key::DEVICES.to_string(), Value::Array(vec![]));
        Self {
            raw: Value::Object(m),
            version: version_string(),
            default_device_config,
            profiles: vec![],
            devices: vec![],
        }
    }

    /// The underlying Value (embedded verbatim in the `get` RPC response).
    pub fn as_value(&self) -> &Value {
        &self.raw
    }

    /// Pretty JSON (for save-to-file); byte-stable against the C++ serializer
    /// for canonical input (see tests).
    pub fn to_pretty_string(&self) -> String {
        serde_json::to_string_pretty(&self.raw).unwrap_or_default()
    }
}

fn str_field(v: &Value, k: &str) -> String {
    v.get(k).and_then(Value::as_str).unwrap_or("").to_string()
}

/// Build the resolved-settings JSON the C++ backend's `ra_backend_bind` expects:
/// `{"profile":{..},"config":{..}}`, using the chosen sub-objects verbatim.
pub fn resolved_json(profile: &Value, config: &Value) -> String {
    let mut m = Map::new();
    m.insert("profile".to_string(), profile.clone());
    m.insert("config".to_string(), config.clone());
    Value::Object(m).to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    const FIXTURE: &str = include_str!("../../tests/fixtures/default_config.json");

    #[test]
    fn parses_canonical_fixture() {
        let c = DriverConfig::from_json_str(FIXTURE).unwrap();
        assert_eq!(c.version, "1.7.0");
        assert_eq!(c.profiles.len(), 1);
        assert_eq!(c.profiles[0].name, "default");
        assert!(c.profiles[0].value.is_object());
        assert_eq!(c.devices.len(), 1);
        assert_eq!(c.devices[0].name, "Test Mouse");
        assert_eq!(c.devices[0].id, "0003:046D:C54D.000A");
        assert_eq!(c.devices[0].profile, "");
        assert!(c.default_device_config.is_object());
    }

    #[test]
    fn reserialize_is_byte_stable() {
        // serde_json (BTreeMap key order + ryu float formatting) must reproduce
        // the nlohmann-produced fixture exactly, or the cross-OS contract drifts.
        let v: Value = serde_json::from_str(FIXTURE).unwrap();
        let out = serde_json::to_string_pretty(&v).unwrap();
        assert_eq!(out, FIXTURE.trim_end_matches('\n'));
    }

    #[test]
    fn resolved_json_has_profile_and_config() {
        let c = DriverConfig::from_json_str(FIXTURE).unwrap();
        let rj = resolved_json(&c.profiles[0].value, &c.default_device_config);
        let v: Value = serde_json::from_str(&rj).unwrap();
        assert!(v.get("profile").unwrap().is_object());
        assert!(v.get("config").unwrap().is_object());
    }

    #[test]
    fn missing_required_field_is_rejected() {
        // mirrors json_io.cpp's `.at()` fail-loud on a missing contract field
        assert!(DriverConfig::from_json_str(r#"{"version":"1.7.0"}"#).is_err());
    }
}
