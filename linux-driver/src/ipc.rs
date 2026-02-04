use anyhow::{Result, anyhow};
use tracing::{info, warn, debug};
use std::os::unix::net::{UnixListener, UnixStream};
use std::io::{Read, Write, ErrorKind};
use std::fs;
use std::path::Path;
use std::ffi::CString;

use crate::device::Device;
use crate::protocol::{IpcMessage, DeviceConfig, SpeedTelemetry};

/// Unix domain socket IPC server
pub struct IpcServer {
    socket_path: String,
    listener: UnixListener,
    clients: Vec<UnixStream>,
}

impl IpcServer {
    /// Create a new IPC server
    pub fn create(socket_path: &str) -> Result<Self> {
        // Remove stale socket file
        if Path::new(socket_path).exists() {
            fs::remove_file(socket_path)?;
        }

        // Create Unix domain socket
        let listener = UnixListener::bind(socket_path)?;
        listener.set_nonblocking(true)?;

        // Set permissions to 0666 (rw-rw-rw-)
        unsafe {
            let c_path = CString::new(socket_path)?;
            libc::chmod(c_path.as_ptr(), 0o666);
        }

        info!(socket_path = %socket_path, "IPC server listening");

        Ok(Self {
            socket_path: socket_path.to_string(),
            listener,
            clients: Vec::new(),
        })
    }

    /// Handle incoming IPC connections
    pub fn handle_connections(&mut self, devices: &mut [Device]) -> Result<()> {
        // Accept new connections (non-blocking)
        loop {
            match self.listener.accept() {
                Ok((stream, _)) => {
                    stream.set_nonblocking(true)?;
                    self.clients.push(stream);
                    debug!("New IPC client connected");
                }
                Err(e) if e.kind() == ErrorKind::WouldBlock => break,
                Err(e) => return Err(e.into()),
            }
        }

        // Handle messages from existing clients
        self.clients.retain_mut(|stream| {
            match handle_client_message(stream, devices) {
                Ok(true) => true,   // Keep connection
                Ok(false) | Err(_) => {
                    debug!("Client disconnected");
                    false  // Remove connection
                }
            }
        });

        Ok(())
    }
}

impl Drop for IpcServer {
    fn drop(&mut self) {
        // Close all client connections
        self.clients.clear();

        // Remove socket file
        if let Err(e) = fs::remove_file(&self.socket_path) {
            warn!(error = %e, socket_path = %self.socket_path, "Failed to remove socket file");
        } else {
            info!(socket_path = %self.socket_path, "Removed socket file");
        }
    }
}

/// Parse IpcMessage header from 16 bytes
fn parse_header(bytes: [u8; 16]) -> IpcMessage {
    unsafe { std::ptr::read(bytes.as_ptr() as *const IpcMessage) }
}

/// Parse DeviceConfig from 4116 bytes
fn parse_device_config(bytes: &[u8]) -> Result<DeviceConfig> {
    if bytes.len() != std::mem::size_of::<DeviceConfig>() {
        return Err(anyhow!("Invalid config size: {} != {}",
            bytes.len(), std::mem::size_of::<DeviceConfig>()));
    }
    Ok(unsafe { std::ptr::read(bytes.as_ptr() as *const DeviceConfig) })
}

/// Serialize SpeedTelemetry to 16 bytes
fn serialize_telemetry(telemetry: &SpeedTelemetry) -> [u8; 16] {
    unsafe {
        std::ptr::read(telemetry as *const _ as *const [u8; 16])
    }
}

/// Send 4-byte ACK response (value: 0x00000001 little-endian)
fn send_ack(stream: &mut UnixStream) -> Result<()> {
    let ack: u32 = 1;
    let bytes = ack.to_le_bytes();
    stream.write_all(&bytes)?;
    Ok(())
}

fn handle_client_message(stream: &mut UnixStream, devices: &mut [Device]) -> Result<bool> {
    // Try to read 16-byte header
    let mut header_buf = [0u8; 16];
    match stream.read_exact(&mut header_buf) {
        Ok(_) => {},
        Err(e) if e.kind() == ErrorKind::WouldBlock => return Ok(true),  // No data yet
        Err(e) if e.kind() == ErrorKind::UnexpectedEof => return Ok(false),  // Disconnected
        Err(e) => return Err(e.into()),
    }

    let header = parse_header(header_buf);

    // Validate protocol
    if header.magic != IpcMessage::MAGIC || header.version != IpcMessage::VERSION {
        warn!(magic = header.magic, version = header.version, "Invalid protocol header");
        return Ok(false);  // Close connection
    }

    debug!(command = header.command, payload_size = header.payload_size, "Received IPC command");

    // Dispatch based on command
    match header.command {
        IpcMessage::CMD_UPDATE_CONFIG => {
            handle_update_config_impl(stream, header.payload_size as usize, devices)?;
        }
        IpcMessage::CMD_DISABLE => {
            handle_disable_impl(devices)?;
            send_ack(stream)?;
        }
        IpcMessage::CMD_ENABLE => {
            handle_enable_impl(devices)?;
            send_ack(stream)?;
        }
        IpcMessage::CMD_GET_STATUS => {
            handle_get_status_impl(devices)?;
            send_ack(stream)?;
        }
        IpcMessage::CMD_GET_SPEED => {
            let telemetry = handle_get_speed_impl(devices);
            let bytes = serialize_telemetry(&telemetry);
            stream.write_all(&bytes)?;
        }
        _ => {
            warn!(command = header.command, "Unknown IPC command");
            return Ok(false);
        }
    }

    Ok(true)
}

fn handle_update_config_impl(stream: &mut UnixStream, payload_size: usize, devices: &mut [Device]) -> Result<()> {
    // Read 4116-byte payload
    let mut payload_buf = vec![0u8; payload_size];
    stream.read_exact(&mut payload_buf)?;

    // Parse config
    let config = parse_device_config(&payload_buf)?;

    // Apply to all devices
    for device in devices.iter_mut() {
        device.update_config(config);
    }

    info!(
        enabled = config.enabled,
        lut_x_points = config.lut_x.num_points,
        lut_y_points = config.lut_y.num_points,
        device_count = devices.len(),
        "Applied config update"
    );

    // Send ACK
    send_ack(stream)?;
    Ok(())
}

fn handle_disable_impl(devices: &mut [Device]) -> Result<()> {
    info!(device_count = devices.len(), "Disabling acceleration");
    let mut config = DeviceConfig::default();
    config.enabled = 0;
    for device in devices.iter_mut() {
        device.update_config(config);
    }
    Ok(())
}

fn handle_enable_impl(devices: &mut [Device]) -> Result<()> {
    info!(device_count = devices.len(), "Enabling acceleration");
    let mut config = DeviceConfig::default();
    config.enabled = 1;
    for device in devices.iter_mut() {
        device.update_config(config);
    }
    Ok(())
}

fn handle_get_status_impl(devices: &[Device]) -> Result<()> {
    debug!(device_count = devices.len(), "Get status request");
    Ok(())
}

fn handle_get_speed_impl(devices: &[Device]) -> SpeedTelemetry {
    devices.first()
        .map(|d| d.telemetry())
        .unwrap_or_default()
}
