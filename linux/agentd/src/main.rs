// rawaccel-agentd (Rust): owns active modifier state, serves the control socket,
// and drives the C++ HID-BPF data plane (libra_backend.so) for discovery/parse/
// state.
//
// --backend auto: probe, pick bpf if supported, else exit.
// --backend bpf:  force HID-BPF (kernel >= 6.11, CAP_BPF).
// --backend noop: control plane only; for tests.
// --probe:        list hidraw devices + their BPF decision, then exit.

use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, OnceLock};
use std::time::Duration;

use rawaccel_agentd::agent::Agent;
use rawaccel_agentd::backend::{self, Backend, FfiBackend, NoopBackend};
use rawaccel_agentd::discovery;
use rawaccel_agentd::server::ControlServer;

static STOP: OnceLock<Arc<AtomicBool>> = OnceLock::new();

extern "C" fn on_signal(_: libc::c_int) {
    if let Some(s) = STOP.get() {
        s.store(true, Ordering::Relaxed); // async-signal-safe
    }
}

fn usage() {
    eprintln!(
        "usage: rawaccel-agentd [--socket PATH] [--settings PATH] \
         [--backend {{auto,bpf,noop}}] [--bpf-object PATH] [--probe] [--doctor]"
    );
}

// rawaccel.bpf.o beside the executable; override via --bpf-object.
fn default_bpf_object_path() -> String {
    std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.join("rawaccel.bpf.o")))
        .map(|p| p.to_string_lossy().into_owned())
        .unwrap_or_else(|| "rawaccel.bpf.o".to_string())
}

fn main() {
    std::process::exit(run());
}

fn run() -> i32 {
    let mut socket_path = "/run/rawaccel/control.sock".to_string();
    let mut settings_path: Option<String> = None;
    let mut backend_name = "auto".to_string();
    let mut bpf_object_path = default_bpf_object_path();
    let mut probe_only = false;
    let mut doctor = false;

    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--socket" if i + 1 < args.len() => {
                socket_path = args[i + 1].clone();
                i += 1;
            }
            "--settings" if i + 1 < args.len() => {
                settings_path = Some(args[i + 1].clone());
                i += 1;
            }
            "--backend" if i + 1 < args.len() => {
                backend_name = args[i + 1].clone();
                i += 1;
            }
            "--bpf-object" if i + 1 < args.len() => {
                bpf_object_path = args[i + 1].clone();
                i += 1;
            }
            "--probe" => probe_only = true,
            "--doctor" => doctor = true,
            "-h" | "--help" => {
                usage();
                return 0;
            }
            _ => {
                usage();
                return 2;
            }
        }
        i += 1;
    }

    if doctor {
        return rawaccel_agentd::doctor::run(&bpf_object_path);
    }

    if probe_only {
        return run_probe();
    }

    let resolved = if backend_name == "auto" {
        let probe = backend::probe_capability();
        if !probe.ok() {
            eprintln!(
                "rawaccel: HID-BPF not available ({}). rawaccel-agentd requires \
                 kernel >= 6.11 with CAP_BPF.",
                probe.reason
            );
            return 1;
        }
        eprintln!(
            "rawaccel: bpf backend (kernel {}.{})",
            probe.kernel_major, probe.kernel_minor
        );
        "bpf".to_string()
    } else {
        backend_name
    };

    match resolved.as_str() {
        "bpf" => match FfiBackend::new(&bpf_object_path) {
            Ok(be) => serve(Agent::new(be), &socket_path, settings_path.as_deref(), true),
            Err(e) => {
                eprintln!("rawaccel: failed to create bpf backend: {e}");
                1
            }
        },
        "noop" => serve(
            Agent::new(NoopBackend::default()),
            &socket_path,
            settings_path.as_deref(),
            false,
        ),
        other => {
            eprintln!("unknown backend: {other}");
            usage();
            2
        }
    }
}

// Load settings, discover devices (bpf only), bind the socket, serve until signalled.
fn serve<B: Backend>(
    mut agent: Agent<B>,
    socket_path: &str,
    settings_path: Option<&str>,
    do_discover: bool,
) -> i32 {
    if let Some(path) = settings_path {
        if !agent.load_from_file(path) {
            eprintln!("warning: could not load settings from '{path}'; using defaults");
        }
    }

    // discover() re-enters via on_device_added, so binds see the config above.
    if do_discover {
        discovery::discover(&mut agent);
    }

    let mut server = ControlServer::new(agent, PathBuf::from(socket_path));
    if let Err(e) = server.listen() {
        eprintln!("listen {socket_path}: {e}");
        return 1;
    }

    let _ = STOP.set(server.stop_handle());
    install_signal_handlers();

    server.run(Duration::from_millis(100));
    0
}

fn run_probe() -> i32 {
    let entries = discovery::probe();
    if entries.is_empty() {
        println!("no hidraw devices found under /sys/class/hidraw");
        return 0;
    }
    for e in entries {
        match e.decision {
            Ok(l) => println!(
                "{} ({}) hid_id=0x{:x}: ACCEPT report_id={} dx=byte{}/{}B dy=byte{}/{}B",
                e.sysname,
                e.device_sysname,
                e.hid_id,
                l.report_id,
                l.dx_byte_offset,
                l.dx_byte_size,
                l.dy_byte_offset,
                l.dy_byte_size,
            ),
            Err(reason) => println!(
                "{} ({}) hid_id=0x{:x}: REJECT {reason}",
                e.sysname, e.device_sysname, e.hid_id
            ),
        }
    }
    0
}

fn install_signal_handlers() {
    let handler = on_signal as extern "C" fn(libc::c_int) as libc::sighandler_t;
    // SAFETY: installing a process-wide handler; on_signal only sets an atomic.
    unsafe {
        libc::signal(libc::SIGINT, handler);
        libc::signal(libc::SIGTERM, handler);
        // a mid-response client disconnect must not kill the daemon
        libc::signal(libc::SIGPIPE, libc::SIG_IGN);
    }
}
