// Daemon lifecycle (detect/start/stop) for rawaccel-agentd. The agent needs
// root (CAP_BPF/CAP_SYS_ADMIN), so starting it escalates with sudo -- terminal
// prompt, never pkexec; failure surfaces a "run as sudo" hint.
//
// Two deployments: installed (systemd unit, socket /run/rawaccel/control.sock,
// `systemctl start`) and dev (rawaccel-agentd spawned via setsid, socket
// $XDG_RUNTIME_DIR/rawaccel.sock -- mirrors run-dev-agent.sh and .NET LinuxAgentDriver).

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

// Wait for the socket to appear (start) / vanish (stop).
const START_TIMEOUT: Duration = Duration::from_secs(8);
const STOP_TIMEOUT: Duration = Duration::from_secs(5);

// True if something accepts connections on `socket`. A stale socket file
// (agent gone) refuses the connect and reads as down -- intended.
fn is_up(socket: &Path) -> bool {
    Client::connect(socket, Duration::from_millis(500)).is_ok()
}

// Socket for client RPCs. Mirrors .NET LinuxAgentDriver order: explicit
// override, system path, then dev path under XDG_RUNTIME_DIR; first existing wins.
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

// Where to create the socket when we start the daemon: explicit override, else
// the system path if the service is installed (its RuntimeDirectory= makes
// /run/rawaccel), else the per-user dev socket.
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

// Ensure rawaccel-agentd is running; return its socket. No-op (no sudo) if
// already up; else systemctl start or spawn the dev binary, then wait.
pub fn start(explicit: &Option<PathBuf>) -> Result<PathBuf> {
    // already up at a path we'd connect to -> no escalation
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

// Stop rawaccel-agentd: systemctl if active, else signal the dev daemon.
// Returns false when nothing was running.
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
        // -x matches the exact process name, not the command line (avoids
        // signalling e.g. `tail -f rawaccel-agentd.log`). pkill exit 1 == no
        // match == already gone
        // only >=2 is a real error.
        let status = privileged("pkill", &["-TERM", "-x", SERVICE])
            .status()
            .context("failed to run pkill (is sudo available?)")?;
        if let Some(code) = status.code() {
            if code >= 2 {
                bail!("pkill failed to signal {SERVICE} (exit {code}).");
            }
        }
    } else {
        // nothing listening, no active service -> already stopped
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

// Build a command, prefixing sudo when not already root. sudo reads the
// password from /dev/tty, so the prompt shows even with stdio redirected.
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

// Effective uid from /proc/self/status (no libc). Format:
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

// Spawn the dev-tree daemon detached: setsid --fork reparents it to outlive
// this process and the terminal. Output goes to a log; sudo still prompts on /dev/tty.
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
    // sudo prompts on /dev/tty regardless
    // let the user see normal stdio
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

// Exec `rawaccel-agentd --doctor`: the daemon links libbpf, so it runs the real
// capability probe. exec() makes the daemon's exit code our own (1 == NOT READY).
pub fn doctor() -> Result<()> {
    use std::os::unix::process::CommandExt;
    let agent = agent_binary().ok_or_else(|| {
        anyhow!(
            "rawaccel-agentd not found next to this executable or in the dev build \
             dir; build linux/target/release/rawaccel-agentd first"
        )
    })?;
    let mut cmd = Command::new(&agent);
    cmd.arg("--doctor");
    // In a dev checkout the BPF object sits in linux/build, not beside the daemon
    // point --doctor at it so the "object present" check reflects reality.
    if let Some(repo) = crate::find_repo_root() {
        let obj = repo.join("linux").join("build").join("rawaccel.bpf.o");
        if obj.is_file() {
            cmd.arg("--bpf-object").arg(obj);
        }
    }
    Err(cmd.exec()).context("failed to run rawaccel-agentd --doctor")
}

// rawaccel-agentd beside this exe (installed) or in the dev build dir
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
        // Rust daemon lives in the cargo workspace target dir (release first).
        let target = repo.join("linux").join("target");
        for profile in ["release", "debug"] {
            let cand = target.join(profile).join(SERVICE);
            if cand.is_file() {
                return Some(cand);
            }
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
            eprintln!(
                "rawaccel: warning: {SERVICE} still responding after {}s",
                STOP_TIMEOUT.as_secs()
            );
            return;
        }
        sleep(Duration::from_millis(150));
    }
}

// Failure message for a start that never produced a live socket; appends the
// agent log tail so the real cause (missing rawaccel.bpf.o, etc.) shows.
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
            // systemctl path logs to the journal, not our file
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

// Single-quote a path for safe embedding in `sh -c`.
fn sh_quote(p: &Path) -> String {
    format!("'{}'", p.to_string_lossy().replace('\'', "'\\''"))
}
