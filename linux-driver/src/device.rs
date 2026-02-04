use anyhow::Result;
use tracing::{info, debug};

use crate::protocol::{DeviceConfig, SpeedTelemetry};

/// Represents a physical mouse device and its virtual counterpart
pub struct Device {
    path: String,
    name: String,
    config: DeviceConfig,
    telemetry: SpeedTelemetry,
}

impl Device {
    /// Create a new device from a path
    pub fn create(path: &str) -> Result<Self> {
        info!(path = %path, "Creating device (stub)");

        Ok(Self {
            path: path.to_string(),
            name: format!("Device at {}", path),
            config: DeviceConfig::default(),
            telemetry: SpeedTelemetry::default(),
        })
    }

    /// Check if this is a mouse device
    pub fn is_mouse(&self) -> bool {
        // Stub: assume all devices we create are mice
        debug!(path = %self.path, "Checking if device is mouse (stub - returning true)");
        true
    }

    /// Get device name
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Get device path
    pub fn path(&self) -> &str {
        &self.path
    }

    /// Update device configuration
    pub fn update_config(&mut self, config: DeviceConfig) {
        info!(
            path = %self.path,
            enabled = config.enabled,
            lut_x_points = config.lut_x.num_points,
            lut_y_points = config.lut_y.num_points,
            "Updating device config (stub)"
        );
        self.config = config;
    }

    /// Get current telemetry data
    pub fn telemetry(&self) -> SpeedTelemetry {
        debug!(path = %self.path, "Getting telemetry (stub)");
        self.telemetry
    }

    /// Process events from the device
    pub fn process_events(&mut self) -> Result<()> {
        debug!(path = %self.path, "Processing events (stub)");
        // Stub: no actual event processing yet
        Ok(())
    }
}
