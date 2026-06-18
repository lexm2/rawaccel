mod client;
mod daemon;

use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command as SysCommand, ExitCode};
use std::time::Duration;

use anyhow::{anyhow, Context, Result};
use clap::{Parser, Subcommand};
use serde_json::{json, Value};

use crate::client::Client;

const RA_VER_MAJOR: i32 = parse_env_int(env!("RA_VER_MAJOR"));
const RA_VER_MINOR: i32 = parse_env_int(env!("RA_VER_MINOR"));
const RA_VER_PATCH: i32 = parse_env_int(env!("RA_VER_PATCH"));

const fn parse_env_int(s: &str) -> i32 {
    let bytes = s.as_bytes();
    let mut i = 0;
    let mut acc: i32 = 0;
    while i < bytes.len() {
        let b = bytes[i];
        if b < b'0' || b > b'9' {
            panic!("RA_VER_* env var contained a non-digit byte");
        }
        acc = acc * 10 + (b - b'0') as i32;
        i += 1;
    }
    acc
}

#[derive(Parser)]
#[command(
    name = "rawaccel",
    version = concat!(env!("RA_VER_MAJOR"), ".", env!("RA_VER_MINOR"), ".", env!("RA_VER_PATCH")),
    about = "rawaccel: launch the GUI (no command), or control rawaccel-agentd"
)]
struct Cli {
    /// Path to the agent's control socket (overrides autodetection)
    #[arg(long, global = true, env = "RAWACCEL_SOCKET")]
    socket: Option<PathBuf>,

    /// Socket I/O timeout in seconds
    #[arg(long, global = true, default_value_t = 5)]
    timeout: u64,

    #[command(subcommand)]
    command: Option<Command>,
}

#[derive(Subcommand)]
enum Command {
    /// Launch the GUI (default with no command); starts rawaccel-agentd first
    /// if needed.
    Gui,
    /// Start rawaccel-agentd if not already running (uses sudo when root is
    /// needed)
    Start,
    /// Stop rawaccel-agentd (uses sudo)
    Stop,
    /// Restart rawaccel-agentd (uses sudo)
    Restart,
    /// Push a settings.json to the agent (debounced 1s by the agent's WriteDelay)
    Apply {
        /// Path to a settings.json
        file: PathBuf,
    },
    /// Print the agent's currently active config as JSON
    Get {
        /// Write the config here instead of stdout
        #[arg(short, long)]
        output: Option<PathBuf>,
    },
    /// Negotiate version with the agent
    Version,
    /// Print agent status (active/pending config, time until apply)
    Status,
    /// Check this system meets the HID-BPF requirements (kernel, BTF, config)
    Doctor,
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    match run(cli) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("rawaccel: {e:#}");
            ExitCode::from(1)
        }
    }
}

fn run(cli: Cli) -> Result<()> {
    let timeout = Duration::from_secs(cli.timeout);
    // no subcommand -> GUI
    let command = cli.command.unwrap_or(Command::Gui);

    match command {
        // start daemon, then GUI; a privilege failure aborts before the GUI
        Command::Gui => launch_gui_with_daemon(&cli.socket),
        Command::Start => {
            let socket = daemon::start(&cli.socket)?;
            println!("rawaccel-agentd is running ({})", socket.display());
            Ok(())
        }
        Command::Stop => {
            if daemon::stop(&cli.socket)? {
                println!("rawaccel-agentd stopped");
            } else {
                println!("rawaccel-agentd is not running");
            }
            Ok(())
        }
        Command::Restart => {
            let socket = daemon::restart(&cli.socket)?;
            println!("rawaccel-agentd restarted ({})", socket.display());
            Ok(())
        }
        // run the daemon's own preflight (it links libbpf for the real probe)
        Command::Doctor => daemon::doctor(),
        // client RPCs connect to whichever socket is live
        other => {
            let socket = daemon::connect_socket(&cli.socket);
            let mut client = Client::connect(&socket, timeout)?;
            match other {
                Command::Apply { file } => cmd_apply(&mut client, &file),
                Command::Get { output } => cmd_get(&mut client, output.as_deref()),
                Command::Version => cmd_version(&mut client),
                Command::Status => cmd_status(&mut client),
                Command::Gui | Command::Start | Command::Stop | Command::Restart
                | Command::Doctor => {
                    unreachable!("handled above")
                }
            }
        }
    }
}

// Start daemon, pin RAWACCEL_SOCKET to its socket, then hand off to the GUI.
fn launch_gui_with_daemon(socket: &Option<PathBuf>) -> Result<()> {
    let resolved = daemon::start(socket)
        .context("could not start rawaccel-agentd; the GUI needs it to apply settings")?;
    env::set_var("RAWACCEL_SOCKET", &resolved);
    launch_gui()
}

// Exec the GUI: $RAWACCEL_GUI, then a rawaccel-gui binary, then dev dotnet fallback.
fn launch_gui() -> Result<()> {
    // explicit override: $RAWACCEL_GUI is the command line to run
    if let Some(raw) = env::var_os("RAWACCEL_GUI") {
        let cow = raw.to_string_lossy();
        let cmd = cow.trim();
        if !cmd.is_empty() {
            let mut parts = cmd.split_whitespace();
            let prog = parts.next().unwrap(); // non-empty after trim
            let mut c = SysCommand::new(prog);
            c.args(parts);
            eprintln!("rawaccel: launching GUI via $RAWACCEL_GUI ({cmd})");
            return exec_or_err(&mut c);
        }
    }

    if let Some(gui) = find_gui_binary() {
        eprintln!("rawaccel: launching GUI ({})", gui.display());
        return exec_or_err(&mut SysCommand::new(gui));
    }

    if let Some(repo) = find_repo_root() {
        let project = repo.join("userinterface");
        let mut c = SysCommand::new("dotnet");
        c.arg("run").arg("--project").arg(&project);

        // so the GUI finds librawaccel_common.so in the dev build dir
        let shim_dir = repo.join("linux").join("build");
        if shim_dir.is_dir() {
            let mut ld = shim_dir.into_os_string();
            if let Some(existing) = env::var_os("LD_LIBRARY_PATH") {
                ld.push(":");
                ld.push(existing);
            }
            c.env("LD_LIBRARY_PATH", ld);
        }
        eprintln!("rawaccel: launching GUI via dotnet ({})", project.display());
        return exec_or_err(&mut c);
    }

    Err(anyhow!(
        "could not locate the rawaccel GUI. Set RAWACCEL_GUI to the GUI command \
         (for an installed build), or run from the source tree so `dotnet run \
         --project userinterface` can be used."
    ))
}

// exec() replaces the process image; only returns on failure
fn exec_or_err(cmd: &mut SysCommand) -> Result<()> {
    use std::os::unix::process::CommandExt;
    Err(cmd.exec()).context("failed to launch the GUI")
}

fn find_gui_binary() -> Option<PathBuf> {
    if let Ok(exe) = env::current_exe() {
        if let Some(dir) = exe.parent() {
            let cand = dir.join("rawaccel-gui");
            if cand.is_file() {
                return Some(cand);
            }
        }
    }
    let paths = env::var_os("PATH")?;
    env::split_paths(&paths)
        .map(|d| d.join("rawaccel-gui"))
        .find(|c| c.is_file())
}

// Find the source-tree root (userinterface/userinterface.csproj).
pub(crate) fn find_repo_root() -> Option<PathBuf> {
    fn search(start: &Path) -> Option<PathBuf> {
        let mut dir = Some(start);
        while let Some(d) = dir {
            if d.join("userinterface").join("userinterface.csproj").is_file() {
                return Some(d.to_path_buf());
            }
            dir = d.parent();
        }
        None
    }
    if let Ok(exe) = env::current_exe() {
        if let Some(found) = exe.parent().and_then(search) {
            return Some(found);
        }
    }
    env::current_dir().ok().and_then(|c| search(&c))
}

fn cmd_apply(client: &mut Client, file: &std::path::Path) -> Result<()> {
    let raw = fs::read_to_string(file)
        .with_context(|| format!("read {}", file.display()))?;
    let config: Value = serde_json::from_str(&raw)
        .with_context(|| format!("{} is not valid JSON", file.display()))?;

    let resp = client.call(&json!({ "cmd": "apply", "config": config }))?;
    require_ok(&resp)?;

    let deferred = resp.get("deferred_ms").and_then(Value::as_i64).unwrap_or(0);
    println!("apply: queued; agent will activate in {deferred} ms");
    Ok(())
}

fn cmd_get(client: &mut Client, output: Option<&std::path::Path>) -> Result<()> {
    let resp = client.call(&json!({ "cmd": "get" }))?;
    require_ok(&resp)?;

    let config = resp
        .get("config")
        .ok_or_else(|| anyhow!("agent response missing `config`"))?;
    let pretty = serde_json::to_string_pretty(config)?;

    match output {
        Some(path) => fs::write(path, pretty)
            .with_context(|| format!("write {}", path.display()))?,
        None => println!("{pretty}"),
    }
    Ok(())
}

fn cmd_version(client: &mut Client) -> Result<()> {
    let resp = client.call(&json!({
        "cmd": "version",
        "client": {
            "major": RA_VER_MAJOR,
            "minor": RA_VER_MINOR,
            "patch": RA_VER_PATCH,
        },
    }))?;

    let agent = resp
        .get("agent")
        .ok_or_else(|| anyhow!("agent response missing `agent`"))?;
    let av = format_version(agent);
    println!("client: {RA_VER_MAJOR}.{RA_VER_MINOR}.{RA_VER_PATCH}");
    println!("agent:  {av}");

    let ok = resp.get("ok").and_then(Value::as_bool).unwrap_or(false);
    if ok {
        Ok(())
    } else {
        let reason = resp
            .get("reason")
            .and_then(Value::as_str)
            .unwrap_or("incompatible");
        let msg = resp
            .get("error")
            .and_then(Value::as_str)
            .unwrap_or("version mismatch");
        Err(anyhow!("version: {msg} ({reason})"))
    }
}

fn cmd_status(client: &mut Client) -> Result<()> {
    let resp = client.call(&json!({ "cmd": "status" }))?;
    require_ok(&resp)?;

    let agent_v = resp
        .get("agent")
        .map(format_version)
        .unwrap_or_else(|| "unknown".into());
    let active = resp
        .get("has_active_config")
        .and_then(Value::as_bool)
        .unwrap_or(false);
    let pending = resp
        .get("has_pending_apply")
        .and_then(Value::as_bool)
        .unwrap_or(false);
    let until = resp.get("until_apply_ms").and_then(Value::as_i64).unwrap_or(0);
    let last = resp
        .get("last_apply_unix_ms")
        .and_then(Value::as_i64)
        .unwrap_or(0);

    println!("agent version:     {agent_v}");
    println!("active config:     {}", if active { "yes" } else { "no" });
    println!("pending apply:     {}", if pending { "yes" } else { "no" });
    if pending {
        println!("apply in:          {until} ms");
    }
    if last > 0 {
        println!("last apply (unix): {last} ms");
    }
    Ok(())
}

fn require_ok(resp: &Value) -> Result<()> {
    let ok = resp.get("ok").and_then(Value::as_bool).unwrap_or(false);
    if ok {
        return Ok(());
    }
    let msg = resp
        .get("error")
        .and_then(Value::as_str)
        .unwrap_or("agent returned ok=false");
    Err(anyhow!("{msg}"))
}

fn format_version(v: &Value) -> String {
    let major = v.get("major").and_then(Value::as_i64).unwrap_or(0);
    let minor = v.get("minor").and_then(Value::as_i64).unwrap_or(0);
    let patch = v.get("patch").and_then(Value::as_i64).unwrap_or(0);
    format!("{major}.{minor}.{patch}")
}
