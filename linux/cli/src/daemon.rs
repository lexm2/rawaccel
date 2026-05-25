// Daemon lifecycle for the rawaccel CLI: detect, start, and stop
// rawaccel-agentd. The agent needs root (CAP_BPF/CAP_SYS_ADMIN), so any
// path that must start it escalates with sudo -- a terminal password prompt,
// never pkexec. If escalation is impossible or fails, the command fails with
// a "run as sudo" hint.
//
// Two deployment realities are handled:
//   - installed: a systemd unit (rawaccel-agentd.service), socket at
//     /run/rawaccel/control.sock; started via `systemctl start`.
//   - dev (source tree): linux/build/rawaccel-agentd spawned directly,
//     detached via setsid, socket at $XDG_RUNTIME_DIR/rawaccel.sock.
//
// The dev socket path mirrors linux/run-dev-agent.sh and the .NET
// LinuxAgentDriver resolution, so the GUI connects whether the agent was
// started by systemd, by run-dev-agent.sh, or by `rawaccel start`.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::thread::sleep;
use std::time::{Duration, Instant};

use anyhow::{anyhow, bail, Context, Result};

use crate::client::Client;

const SERVICE: &str = "rawaccel-agentd";
const SYSTEM_SOCKET: &str = "/run/rawaccel/control.sock";

// How long to wait for the socket to appear (start) or vanish (stop).
const START_TIMEOUT: Duration = Duration::from_secs(8);
const STOP_TIMEOUT: Duration = Duration::from_secs(5);

// True if something is accepting connections on `socket`. A stale socket file
// (agent gone) refuses the connect and reads as down, which is what we want.
fn is_up(socket: &Path) -> bool {
    Client::connect(socket, Duration::from_millis(500)).is_ok()
}

// Socket to connect to for client RPCs (status/apply/get/version). Mirrors the
// .NET LinuxAgentDriver order: explicit override, then the system path, then
// the dev-launcher path under XDG_RUNTIME_DIR. First existing wins.
pub fn connect_socket(explicit: &Option<PathBuf>) -> PathBuf {
    if let Some(p) = explicit {
        return p.clone();
    }
    let system = PathBuf::from(SYSTEM_SOCKET);
    if system.exists() {
        return system;
    }
    if let Some(dev) = dev_socket() {
        if dev.exists() {
            return dev;
        }
    }
    system
}

// Where to create the socket when we start the daemon ourselves: an explicit
// override wins; otherwise the system path if the service is installed (the
// unit's RuntimeDirectory= makes /run/rawaccel), else the per-user dev socket.
fn target_socket(explicit: &Option<PathBuf>) -> Result<PathBuf> {
    if let Some(p) = explicit {
        return Ok(p.clone());
    }
    if service_installed() {
        return Ok(PathBuf::from(SYSTEM_SOCKET));
    }
    dev_socket().ok_or_else(|| {
        anyhow!(
            "XDG_RUNTIME_DIR is unset and no rawaccel-agentd service is \
             installed; set RAWACCEL_SOCKET to a writable socket path or \
             install the service (cmake --install)."
        )
    })
}

fn dev_socket() -> Option<PathBuf> {
    env::var_os("XDG_RUNTIME_DIR").map(|d| PathBuf::from(d).join("rawaccel.sock"))
}

// Ensure rawaccel-agentd is running and return the socket it serves. A no-op
// (no sudo) when it is already up. Otherwise starts it via systemctl (when the
// unit is installed) or by spawning the dev binary, then waits for the socket.
pub fn start(explicit: &Option<PathBuf>) -> Result<PathBuf> {
    // Already up at a path we'd connect to? Nothing to do, no escalation.
    let existing = connect_socket(explicit);
    if is_up(&existing) {
        return Ok(existing);
    }

    let socket = target_socket(explicit)?;
    if is_up(&socket) {
        return Ok(socket);
    }

    if service_installed() {
        start_via_systemctl()?;
    } else {
        spawn_dev_agent(&socket)?;
    }

    wait_until_up(&socket).with_context(|| start_failure_hint(&socket))?;
    Ok(socket)
}

// Stop rawaccel-agentd. systemctl when the service is active, otherwise signal
// the directly-spawned dev daemon. Returns false when nothing was running.
pub fn stop(explicit: &Option<PathBuf>) -> Result<bool> {
    let socket = connect_socket(explicit);

    if service_active() {
        let status = privileged("systemctl", &["stop", SERVICE])
            .status()
            .context("failed to run systemctl (is sudo available?)")?;
        if !status.success() {
            bail!(
                "`systemctl stop {SERVICE}` failed; try `sudo systemctl stop \
                 {SERVICE}` or inspect `journalctl -u {SERVICE}`."
            );
        }
    } else if is_up(&socket) {
        // -x matches the exact process name (not the full command line), so we
        // don't accidentally signal something like `tail -f rawaccel-agentd.log`.
        // pkill returns 1 when nothing matched; that just means it already
        // exited, so only a hard error (>=2) is worth reporting.
        let status = privileged("pkill", &["-TERM", "-x", SERVICE])
            .status()
            .context("failed to run pkill (is sudo available?)")?;
        if let Some(code) = status.code() {
            if code >= 2 {
                bail!("pkill failed to signal {SERVICE} (exit {code}).");
            }
        }
    } else {
        // Nothing listening and no active service: already stopped.
        return Ok(false);
    }

    wait_until_down(&socket);
    Ok(true)
}

pub fn restart(explicit: &Option<PathBuf>) -> Result<PathBuf> {
    stop(explicit)?;
    start(explicit)
}

// ---- escalation -------------------------------------------------------------

// Build a command, prefixing sudo when we are not already root. sudo reads the
// password from the controlling terminal (/dev/tty), so the prompt still shows
// even when the child's stdio is redirected.
fn privileged(program: &str, args: &[&str]) -> Command {
    if is_root() {
        let mut c = Command::new(program);
        c.args(args);
        c
    } else {
        eprintln!(
            "rawaccel: {program} {} (sudo: you may be prompted for your password)",
            args.join(" ")
        );
        let mut c = Command::new("sudo");
        c.arg(program).args(args);
        c
    }
}

fn is_root() -> bool {
    euid() == Some(0)
}

// Effective uid from /proc/self/status (no libc dependency). Format:
//   Uid:\t<real>\t<eff>\t<saved>\t<fs>
fn euid() -> Option<u32> {
    let status = fs::read_to_string("/proc/self/status").ok()?;
    for line in status.lines() {
        if let Some(rest) = line.strip_prefix("Uid:") {
            let mut fields = rest.split_whitespace();
            let _real = fields.next();
            return fields.next().and_then(|v| v.parse().ok());
        }
    }
    None
}

fn start_via_systemctl() -> Result<()> {
    let status = privileged("systemctl", &["start", SERVICE])
        .status()
        .context("failed to run systemctl (is sudo available?)")?;
    if !status.success() {
        bail!(
            "`systemctl start {SERVICE}` failed (privilege escalation declined \
             or the unit errored). Try `sudo systemctl start {SERVICE}` or check \
             `journalctl -u {SERVICE}`."
        );
    }
    Ok(())
}

// Spawn the dev-tree daemon detached: setsid --fork reparents it so it outlives
// this process and the controlling terminal. Output is redirected to a log so
// failures can be inspected; sudo still prompts on /dev/tty.
fn spawn_dev_agent(socket: &Path) -> Result<()> {
    let agent = agent_binary().ok_or_else(|| {
        anyhow!(
            "rawaccel-agentd not found next to this executable. Reinstall \
             rawaccel so the daemon ships alongside the CLI. (From a source \
             checkout instead: build linux/build/rawaccel-agentd, or run \
             linux/run-dev-agent.sh.)"
        )
    })?;

    let dir = socket.parent().unwrap_or_else(|| Path::new("/"));
    let log = log_path();
    let script = format!(
        "mkdir -p {dir} && exec setsid --fork {agent} --backend auto \
         --socket {sock} >> {log} 2>&1",
        dir = sh_quote(dir),
        agent = sh_quote(&agent),
        sock = sh_quote(socket),
        log = sh_quote(&log),
    );

    let mut cmd = if is_root() {
        let mut c = Command::new("sh");
        c.arg("-c").arg(&script);
        c
    } else {
        eprintln!(
            "rawaccel: starting {SERVICE} (sudo: you may be prompted for your password)"
        );
        let mut c = Command::new("sudo");
        c.arg("sh").arg("-c").arg(&script);
        c
    };
    // sudo prompts on /dev/tty regardless; let the user see normal stdio.
    cmd.stdin(Stdio::inherit());

    let status = cmd
        .status()
        .context("failed to launch rawaccel-agentd (is sudo available?)")?;
    if !status.success() {
        bail!(
            "could not start {SERVICE} (privilege escalation declined). \
             Re-run with privileges, e.g. `sudo rawaccel start`."
        );
    }
    Ok(())
}

// rawaccel-agentd beside this executable (installed layout) or in the dev
// build dir located via the source tree.
fn agent_binary() -> Option<PathBuf> {
    if let Ok(exe) = env::current_exe() {
        if let Some(dir) = exe.parent() {
            let cand = dir.join(SERVICE);
            if cand.is_file() {
                return Some(cand);
            }
        }
    }
    if let Some(repo) = crate::find_repo_root() {
        let cand = repo.join("linux").join("build").join(SERVICE);
        if cand.is_file() {
            return Some(cand);
        }
    }
    None
}

fn service_installed() -> bool {
    Command::new("systemctl")
        .arg("cat")
        .arg(SERVICE)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

fn service_active() -> bool {
    Command::new("systemctl")
        .arg("is-active")
        .arg("--quiet")
        .arg(SERVICE)
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

// ---- waiting + diagnostics --------------------------------------------------

fn wait_until_up(socket: &Path) -> Result<()> {
    let start = Instant::now();
    loop {
        if is_up(socket) {
            return Ok(());
        }
        if start.elapsed() >= START_TIMEOUT {
            bail!("daemon did not come up within {}s", START_TIMEOUT.as_secs());
        }
        sleep(Duration::from_millis(150));
    }
}

fn wait_until_down(socket: &Path) {
    let start = Instant::now();
    while is_up(socket) {
        if start.elapsed() >= STOP_TIMEOUT {
            return;
        }
        sleep(Duration::from_millis(150));
    }
}

// Failure message for a start that never produced a live socket: include the
// tail of the agent log so the actual cause (e.g. missing rawaccel.bpf.o,
// unsupported kernel) is visible.
fn start_failure_hint(socket: &Path) -> String {
    let mut msg = format!("rawaccel-agentd did not come up at {}", socket.display());
    let log = log_path();
    match fs::read_to_string(&log) {
        Ok(text) => {
            let tail: Vec<&str> = text.lines().rev().take(8).collect();
            if !tail.is_empty() {
                msg.push_str(&format!("\nlast lines of {}:", log.display()));
                for line in tail.into_iter().rev() {
                    msg.push_str("\n  ");
                    msg.push_str(line);
                }
            }
        }
        Err(_) => {
            // systemctl path logs to the journal, not our file.
            msg.push_str(&format!(
                "\nsee {} (dev) or `journalctl -u {SERVICE}` (installed) for details",
                log.display()
            ));
        }
    }
    msg
}

fn log_path() -> PathBuf {
    env::var_os("XDG_RUNTIME_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/tmp"))
        .join("rawaccel-agentd.log")
}

// Single-quote a path for safe embedding in an `sh -c` script.
fn sh_quote(p: &Path) -> String {
    format!("'{}'", p.to_string_lossy().replace('\'', "'\\''"))
}
