use anyhow::Result;
use clap::Parser;
use tracing::info;
use tracing_subscriber::EnvFilter;

mod daemon;
mod device;
mod ipc;
mod protocol;

use daemon::Daemon;

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// Path to the Unix domain socket
    #[arg(short, long, default_value = "/tmp/rawaccel.sock")]
    socket_path: String,

    /// Enable verbose logging
    #[arg(short, long)]
    verbose: bool,
}

fn init_logging(verbose: bool) {
    let filter = if verbose {
        EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("debug"))
    } else {
        EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info"))
    };

    tracing_subscriber::fmt()
        .with_env_filter(filter)
        .with_target(true)
        .with_line_number(true)
        .init();

    info!(
        version = env!("CARGO_PKG_VERSION"),
        "RawAccel daemon starting"
    );
}

fn main() -> Result<()> {
    let args = Args::parse();
    init_logging(args.verbose);

    let mut daemon = Daemon::create(&args.socket_path)?;
    daemon.scan_devices()?;
    daemon.run()?;

    Ok(())
}
