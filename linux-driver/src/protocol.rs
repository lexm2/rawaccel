/// Binary protocol types for IPC compatibility with C# backend
/// These must maintain exact binary layout for compatibility

/// IPC message header
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct IpcMessage {
    pub magic: u32,      // 0x52415943 ("RAWC")
    pub version: u32,    // 1
    pub command: u32,    // 1-5
    pub payload_size: u32,
}

impl IpcMessage {
    pub const MAGIC: u32 = 0x52415943;
    pub const VERSION: u32 = 1;

    pub const CMD_UPDATE_CONFIG: u32 = 1;
    pub const CMD_DISABLE: u32 = 2;
    pub const CMD_ENABLE: u32 = 3;
    pub const CMD_GET_STATUS: u32 = 4;
    pub const CMD_GET_SPEED: u32 = 5;
}

/// Lookup table for acceleration
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct Lut {
    pub speeds: [f32; 256],
    pub multipliers: [f32; 256],
    pub num_points: u32,
}

impl Default for Lut {
    fn default() -> Self {
        Self {
            speeds: [0.0; 256],
            multipliers: [1.0; 256],
            num_points: 0,
        }
    }
}

/// Device configuration
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct DeviceConfig {
    pub enabled: u8,
    pub _pad1: [u8; 3],
    pub lut_x: Lut,
    pub lut_y: Lut,
    pub separate_axes: u8,
    pub _pad2: [u8; 3],
    pub dpi: u32,
}

impl Default for DeviceConfig {
    fn default() -> Self {
        Self {
            enabled: 0,
            _pad1: [0; 3],
            lut_x: Lut::default(),
            lut_y: Lut::default(),
            separate_axes: 0,
            _pad2: [0; 3],
            dpi: 800,
        }
    }
}

/// Speed telemetry data
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct SpeedTelemetry {
    pub speed_x: f32,
    pub speed_y: f32,
    pub speed_magnitude: f32,
    pub _pad: u32,
}

// Compile-time size assertions to ensure binary compatibility
const _: () = assert!(std::mem::size_of::<IpcMessage>() == 16);
const _: () = assert!(std::mem::size_of::<Lut>() == 2052);
const _: () = assert!(std::mem::size_of::<DeviceConfig>() == 4116);
const _: () = assert!(std::mem::size_of::<SpeedTelemetry>() == 16);
