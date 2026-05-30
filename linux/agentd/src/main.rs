// rawaccel-agentd (Rust): owns active modifier state, serves the control socket,
// and drives the C++ HID-BPF data plane (libra_backend.so) for discovery/parse/
// state. Migration in progress; this entry point currently smoke-tests the FFI.

mod backend;

fn main() {
    let probe = backend::probe_capability();
    eprintln!(
        "rawaccel-agentd: kernel {}.{} kernel_ok={} syscall_ok={}{}",
        probe.kernel_major,
        probe.kernel_minor,
        probe.kernel_ok,
        probe.syscall_ok,
        if probe.reason.is_empty() {
            String::new()
        } else {
            format!(" ({})", probe.reason)
        },
    );
    eprintln!(
        "rawaccel-agentd: version {}.{}.{}",
        env!("RA_VER_MAJOR"),
        env!("RA_VER_MINOR"),
        env!("RA_VER_PATCH"),
    );
}
