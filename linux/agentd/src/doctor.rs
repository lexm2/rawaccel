//! Preflight diagnostics: report each HID-BPF requirement as pass/fail so a
//! failure on an old or misconfigured kernel is one actionable line instead of an
//! opaque load error. `rawaccel doctor` (CLI) execs `rawaccel-agentd --doctor`.

use std::ffi::CStr;
use std::fs;
use std::path::Path;

use crate::backend;

#[derive(PartialEq)]
enum Status {
    Pass,
    Warn,
    Fail,
}

struct Check {
    name: String,
    status: Status,
    detail: String,
}

/// Run every preflight check and print a report. Returns a process exit code:
/// 0 when all required checks pass, 1 when any [FAIL] remains.
pub fn run(bpf_object_path: &str) -> i32 {
    let mut checks = Vec::new();

    let root = unsafe { libc::geteuid() } == 0;

    // Kernel version + struct_ops support, via the same libbpf probe the daemon
    // uses to pick its backend (single source of truth).
    let probe = backend::probe_capability();
    checks.push(Check {
        name: "kernel >= 6.11 (struct_ops HID-BPF API)".into(),
        status: if probe.kernel_ok { Status::Pass } else { Status::Fail },
        detail: format!("running {}.{}", probe.kernel_major, probe.kernel_minor),
    });
    checks.push(Check {
        name: "BPF struct_ops program type".into(),
        status: if probe.syscall_ok {
            Status::Pass
        } else if probe.kernel_ok {
            // kernel new enough but probe failed: usually missing CAP_BPF.
            Status::Warn
        } else {
            Status::Fail
        },
        detail: if probe.syscall_ok {
            "supported".into()
        } else if probe.kernel_ok && !root {
            // libbpf reports the probe as unsupported when it can't load
            // without CAP_BPF
            // don't claim the kernel lacks it. Re-run as root to confirm.
            "probe needs CAP_BPF; re-run as root to confirm".into()
        } else {
            probe.reason.clone()
        },
    });

    // BTF is required for libbpf's CO-RE relocations at load time.
    let btf = Path::new("/sys/kernel/btf/vmlinux");
    checks.push(Check {
        name: "kernel BTF (/sys/kernel/btf/vmlinux)".into(),
        status: if btf.exists() { Status::Pass } else { Status::Fail },
        detail: if btf.exists() {
            "present".into()
        } else {
            "missing (needs CONFIG_DEBUG_INFO_BTF=y)".into()
        },
    });

    // Config flags are informational: the probe above is authoritative, but when
    // it fails these explain why. Unreadable config -> warn, not fail.
    let config = read_kernel_config();
    for flag in ["CONFIG_HID_BPF", "CONFIG_BPF_SYSCALL"] {
        checks.push(config_check(flag, config.as_deref()));
    }

    // The compiled object the daemon loads.
    let obj = Path::new(bpf_object_path);
    checks.push(Check {
        name: "BPF object present".into(),
        status: if obj.is_file() { Status::Pass } else { Status::Fail },
        detail: bpf_object_path.into(),
    });

    // Privilege to load the program.
    checks.push(Check {
        name: "CAP_BPF / root".into(),
        status: if root { Status::Pass } else { Status::Warn },
        detail: if root {
            "running as root".into()
        } else {
            "not root; loading needs CAP_BPF (run via the service or sudo)".into()
        },
    });

    println!("rawaccel preflight (HID-BPF requirements)\n");
    let mut failed = false;
    for c in &checks {
        let mark = match c.status {
            Status::Pass => "[ ok ]",
            Status::Warn => "[warn]",
            Status::Fail => {
                failed = true;
                "[FAIL]"
            }
        };
        println!("  {mark} {:<40} {}", c.name, c.detail);
    }
    println!();
    if failed {
        println!("result: NOT READY -- fix the [FAIL] items before rawaccel-agentd can run.");
        1
    } else {
        println!("result: ready.");
        0
    }
}

// Look for `FLAG=y` / `FLAG=m` in the kernel config; classify against probe truth.
fn config_check(flag: &str, config: Option<&str>) -> Check {
    let name = format!("kernel config {flag}");
    match config {
        None => Check {
            name,
            status: Status::Warn,
            detail: "kernel config not readable (/boot/config-<release>)".into(),
        },
        Some(text) => {
            let set = text
                .lines()
                .any(|l| l == format!("{flag}=y") || l == format!("{flag}=m"));
            Check {
                name,
                status: if set { Status::Pass } else { Status::Fail },
                detail: if set { "enabled".into() } else { "not set".into() },
            }
        }
    }
}

// /boot/config-<release> is plaintext on most distros; /proc/config.gz needs gzip
// (no dep here) so it's skipped -- the libbpf probe already gives ground truth.
fn read_kernel_config() -> Option<String> {
    let mut uts: libc::utsname = unsafe { std::mem::zeroed() };
    if unsafe { libc::uname(&mut uts) } != 0 {
        return None;
    }
    let release = unsafe { CStr::from_ptr(uts.release.as_ptr()) }
        .to_string_lossy()
        .into_owned();
    fs::read_to_string(format!("/boot/config-{release}")).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_flag_classification() {
        let cfg = "CONFIG_HID_BPF=y\nCONFIG_BPF_SYSCALL=m\n# CONFIG_FOO is not set\n";
        assert!(config_check("CONFIG_HID_BPF", Some(cfg)).status == Status::Pass);
        assert!(config_check("CONFIG_BPF_SYSCALL", Some(cfg)).status == Status::Pass);
        assert!(config_check("CONFIG_FOO", Some(cfg)).status == Status::Fail);
        // Unreadable config is a warning, never a hard fail (probe is authoritative).
        assert!(config_check("CONFIG_HID_BPF", None).status == Status::Warn);
    }
}
