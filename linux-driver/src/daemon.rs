use anyhow::Result;
use signal_hook::consts::signal::*;
use signal_hook::iterator::Signals;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time::Duration;
use tracing::{info, warn, error, debug};

use crate::device::Device;
use crate::ipc::IpcServer;

/// Main daemon structure
pub struct Daemon {
    devices: Vec<Device>,
    ipc_server: IpcServer,
    running: Arc<AtomicBool>,
}

impl Daemon {
    /// Create a new daemon instance
    pub fn create(socket_path: &str) -> Result<Self> {
        info!("Creating daemon");

        let ipc_server = IpcServer::create(socket_path)?;
        let running = Arc::new(AtomicBool::new(true));

        Ok(Self {
            devices: Vec::new(),
            ipc_server,
            running,
        })
    }

    /// Scan /dev/input for mouse devices
    pub fn scan_devices(&mut self) -> Result<()> {
        info!("Scanning for mouse devices (stub)");

        // Stub: create a dummy device for demonstration
        match Device::create("/dev/input/event0") {
            Ok(device) => {
                if device.is_mouse() {
                    info!(
                        path = %device.path(),
                        name = %device.name(),
                        "Found mouse device"
                    );
                    self.devices.push(device);
                }
            }
            Err(e) => {
                warn!(error = %e, "Failed to create device (expected in stub mode)");
            }
        }

        info!(count = self.devices.len(), "Device scan complete");
        Ok(())
    }

    /// Run the main daemon loop
    pub fn run(&mut self) -> Result<()> {
        // Set up signal handling
        let running = self.running.clone();
        let mut signals = Signals::new(&[SIGINT, SIGTERM])?;

        thread::spawn(move || {
            for sig in signals.forever() {
                info!(signal = sig, "Received signal, shutting down");
                running.store(false, Ordering::Relaxed);
            }
        });

        info!("Daemon started, entering main loop (stub)");

        // Main event loop (stub)
        while self.running.load(Ordering::Relaxed) {
            debug!("Main loop iteration (stub)");

            // Stub: handle IPC connections
            if let Err(e) = self.ipc_server.handle_connections(&mut self.devices) {
                error!(error = %e, "Failed to handle IPC connections");
            }

            // Stub: process device events
            for device in &mut self.devices {
                if let Err(e) = device.process_events() {
                    error!(error = %e, path = %device.path(), "Failed to process device events");
                }
            }

            // Sleep to avoid busy-waiting in stub mode
            thread::sleep(Duration::from_millis(100));
        }

        info!("Daemon shutting down");
        Ok(())
    }
}

impl Drop for Daemon {
    fn drop(&mut self) {
        info!("Daemon cleanup complete");
    }
}
