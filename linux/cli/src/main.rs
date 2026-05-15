mod client;

use std::fs;
use std::path::PathBuf;
use std::process::ExitCode;
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
    name = "rawaccel-cli",
    version = concat!(env!("RA_VER_MAJOR"), ".", env!("RA_VER_MINOR"), ".", env!("RA_VER_PATCH")),
    about = "Control client for rawaccel-agentd"
)]
struct Cli {
    /// Path to the agent's control socket
    #[arg(
        long,
        global = true,
        env = "RAWACCEL_SOCKET",
        default_value = "/run/rawaccel/control.sock"
    )]
    socket: PathBuf,

    /// Socket I/O timeout in seconds
    #[arg(long, global = true, default_value_t = 5)]
    timeout: u64,

    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
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
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    match run(cli) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("rawaccel-cli: {e:#}");
            ExitCode::from(1)
        }
    }
}

fn run(cli: Cli) -> Result<()> {
    let timeout = Duration::from_secs(cli.timeout);
    let mut client = Client::connect(&cli.socket, timeout)?;

    match cli.command {
        Command::Apply { file } => cmd_apply(&mut client, &file),
        Command::Get { output } => cmd_get(&mut client, output.as_deref()),
        Command::Version => cmd_version(&mut client),
        Command::Status => cmd_status(&mut client),
    }
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
