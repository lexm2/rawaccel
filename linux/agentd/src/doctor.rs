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
    Info,
    Warn,
    Fail,
}

// Where the kernel config came from. /boot/config-<release> is plaintext we can
// grep; /proc/config.gz is present but compressed (no gzip dep here) so it's
// skipped, not an error -- the libbpf probe is authoritative either way.
enum ConfigSource {
    Text(String),
    Compressed,
    Missing,
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
        checks.push(config_check(flag, &config));
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
            Status::Info => "[info]",
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
fn config_check(flag: &str, config: &ConfigSource) -> Check {
    let name = format!("kernel config {flag}");
    match config {
        // No plaintext config and no /proc/config.gz: can't say, so warn.
        ConfigSource::Missing => Check {
            name,
            status: Status::Warn,
            detail: "kernel config not readable (no /boot/config-<release> or /proc/config.gz)"
                .into(),
        },
        // Config exists but is gzip'd; we don't decompress here. Not a problem --
        // the struct_ops probe above is authoritative -- so report it as info.
        ConfigSource::Compressed => Check {
            name,
            status: Status::Info,
            detail: "present but compressed (/proc/config.gz); skipped, probe is authoritative"
                .into(),
        },
        ConfigSource::Text(text) => {
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

// /boot/config-<release> is plaintext on most distros and we grep it directly.
// Arch/Fedora ship the config gzip'd at /proc/config.gz; decompressing needs a
// gzip dep we deliberately avoid, so we just note its presence -- the libbpf
// probe already gives ground truth on whether HID-BPF actually works.
fn read_kernel_config() -> ConfigSource {
    let mut uts: libc::utsname = unsafe { std::mem::zeroed() };
    if unsafe { libc::uname(&mut uts) } == 0 {
        let release = unsafe { CStr::from_ptr(uts.release.as_ptr()) }
            .to_string_lossy()
            .into_owned();
        if let Ok(text) = fs::read_to_string(format!("/boot/config-{release}")) {
            return ConfigSource::Text(text);
        }
    }
    if Path::new("/proc/config.gz").exists() {
        return ConfigSource::Compressed;
    }
    ConfigSource::Missing
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_flag_classification() {
        let cfg = ConfigSource::Text(
            "CONFIG_HID_BPF=y\nCONFIG_BPF_SYSCALL=m\n# CONFIG_FOO is not set\n".into(),
        );
        assert!(config_check("CONFIG_HID_BPF", &cfg).status == Status::Pass);
        assert!(config_check("CONFIG_BPF_SYSCALL", &cfg).status == Status::Pass);
        assert!(config_check("CONFIG_FOO", &cfg).status == Status::Fail);
        // Compressed config (Arch/Fedora) is info, not a warning or fail.
        assert!(config_check("CONFIG_HID_BPF", &ConfigSource::Compressed).status == Status::Info);
        // No config source at all is a warning, never a hard fail (probe is authoritative).
        assert!(config_check("CONFIG_HID_BPF", &ConfigSource::Missing).status == Status::Warn);
    }
}
