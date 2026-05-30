//! Length-prefixed JSON RPC over AF_UNIX SOCK_STREAM. Port of
//! `linux/agent/control_server.cpp`. Frame: [u32 length, net order][N bytes JSON].
//! Single-threaded; the control plane is low-volume. `std` handles bind/accept;
//! `libc` covers the bits std does not expose (umask, chown, SO_PEERCRED).

use std::io::{Read, Write};
use std::os::unix::fs::FileTypeExt;
use std::os::unix::io::AsRawFd;
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use serde_json::{json, Value};

use crate::agent::{Agent, VersionStatus};
use crate::backend::Backend;
use crate::config::DriverConfig;

/// 64 KiB: above any real driver_config, below JSON parser stack-OOM. Matches the C++ server.
pub const MAX_FRAME_BYTES: u32 = 64 * 1024;

fn read_exact_or_none(stream: &mut UnixStream, buf: &mut [u8]) -> bool {
    stream.read_exact(buf).is_ok()
}

/// Read one frame. None on EOF/short read/oversize. Zero-length is valid (empty vec).
pub fn read_frame(stream: &mut UnixStream) -> Option<Vec<u8>> {
    let mut len_be = [0u8; 4];
    if !read_exact_or_none(stream, &mut len_be) {
        return None;
    }
    let len = u32::from_be_bytes(len_be);
    if len > MAX_FRAME_BYTES {
        return None;
    }
    let mut out = vec![0u8; len as usize];
    if len != 0 && !read_exact_or_none(stream, &mut out) {
        return None;
    }
    Some(out)
}

/// Write one frame. False on oversize or a broken pipe.
pub fn write_frame(stream: &mut UnixStream, payload: &[u8]) -> bool {
    if payload.len() > MAX_FRAME_BYTES as usize {
        return false;
    }
    let len_be = (payload.len() as u32).to_be_bytes();
    stream.write_all(&len_be).is_ok() && stream.write_all(payload).is_ok()
}

fn error_response(msg: &str) -> String {
    json!({"ok": false, "error": msg}).to_string()
}

fn version_obj(v: (i32, i32, i32)) -> Value {
    json!({"major": v.0, "minor": v.1, "patch": v.2})
}

/// Dispatch one request JSON, return the response JSON. Socket-free for tests.
pub fn dispatch<B: Backend>(agent: &mut Agent<B>, request_json: &str, now: Instant) -> String {
    let req: Value = match serde_json::from_str(request_json) {
        Ok(v) => v,
        Err(e) => return error_response(&format!("parse error: {e}")),
    };
    let Some(cmd) = req.get("cmd").and_then(Value::as_str) else {
        return error_response("missing cmd field");
    };

    match cmd {
        "version" => {
            let Some(c) = req.get("client").filter(|c| c.is_object()) else {
                return error_response("version: client field required");
            };
            let client = (
                c.get("major").and_then(Value::as_i64).unwrap_or(0) as i32,
                c.get("minor").and_then(Value::as_i64).unwrap_or(0) as i32,
                c.get("patch").and_then(Value::as_i64).unwrap_or(0) as i32,
            );
            let vc = agent.check_version(client);
            let mut resp = json!({ "agent": version_obj(vc.agent_version) });
            if vc.status == VersionStatus::Ok {
                resp["ok"] = json!(true);
            } else {
                resp["ok"] = json!(false);
                resp["error"] = json!(vc.message);
                resp["reason"] = json!(match vc.status {
                    VersionStatus::ClientTooOld => "client_too_old",
                    _ => "client_too_new",
                });
            }
            resp.to_string()
        }
        "apply" => {
            let Some(cfg_value) = req.get("config") else {
                return error_response("apply: config field required");
            };
            let cfg = match DriverConfig::from_value(cfg_value.clone()) {
                Ok(c) => c,
                Err(e) => return error_response(&format!("apply: invalid config: {e}")),
            };
            // Fail loudly when devices exist but none attached, else apply reports false success.
            if let Some(err) = agent.data_plane_failure() {
                return error_response(&format!("apply: {err}"));
            }
            agent.schedule_apply(cfg, now);
            json!({"ok": true, "deferred_ms": crate::agent::WRITE_DELAY.as_millis() as i64})
                .to_string()
        }
        "get" => json!({"ok": true, "config": agent.active_value()}).to_string(),
        "deactivate" => {
            agent.deactivate();
            json!({"ok": true}).to_string()
        }
        "stats" => {
            let s = agent.current_speed_sample();
            json!({
                "ok": true,
                "current_speed": s.combined,
                "current_speed_x": s.x,
                "current_speed_y": s.y,
            })
            .to_string()
        }
        "status" => {
            let s = agent.status(now);
            json!({
                "ok": true,
                "has_active_config": s.has_active_config,
                "has_pending_apply": s.has_pending_apply,
                "until_apply_ms": s.until_apply_ms,
                "last_apply_unix_ms": s.last_apply_unix_ms,
                "agent": version_obj(crate::agent::agent_version()),
            })
            .to_string()
        }
        other => error_response(&format!("unknown cmd: {other}")),
    }
}

pub struct ControlServer<B: Backend> {
    agent: Agent<B>,
    socket_path: PathBuf,
    listener: Option<UnixListener>,
    expected_uid: u32,
    stop: Arc<AtomicBool>,
}

impl<B: Backend> ControlServer<B> {
    pub fn new(agent: Agent<B>, socket_path: impl Into<PathBuf>) -> Self {
        Self {
            agent,
            socket_path: socket_path.into(),
            listener: None,
            expected_uid: 0,
            stop: Arc::new(AtomicBool::new(false)),
        }
    }

    /// A flag whose `store(true)` makes `run()` return at the next poll tick.
    pub fn stop_handle(&self) -> Arc<AtomicBool> {
        self.stop.clone()
    }

    pub fn path(&self) -> &Path {
        &self.socket_path
    }

    /// Bind the listener (removes a stale socket first). Err on error (see source).
    pub fn listen(&mut self) -> anyhow::Result<()> {
        // Remove a stale socket; warn (don't clobber) if a non-socket sits there.
        match std::fs::symlink_metadata(&self.socket_path) {
            Ok(md) if md.file_type().is_socket() => {
                let _ = std::fs::remove_file(&self.socket_path);
            }
            Ok(_) => {
                eprintln!(
                    "control: {} exists and is not a socket; bind() will fail",
                    self.socket_path.display()
                );
            }
            Err(_) => {}
        }

        // umask 0177 around bind() (widened to 0660 after chown) closes the bind()/chmod() world-write race.
        // SAFETY: umask is process-global but we restore it immediately.
        let prev_umask = unsafe { libc::umask(0o177) };
        let bind_result = UnixListener::bind(&self.socket_path);
        unsafe { libc::umask(prev_umask) };
        let listener = bind_result?;

        // Under sudo, chown to SUDO_UID/GID so the client can connect; SO_PEERCRED enforces access.
        // SAFETY: geteuid is always safe.
        self.expected_uid = unsafe { libc::geteuid() };
        if self.expected_uid == 0 {
            if let Some((uid, gid)) = sudo_uid_gid() {
                if chown(&self.socket_path, uid, gid) {
                    self.expected_uid = uid;
                } else {
                    eprintln!("control: chown to sudo invoker failed");
                }
            }
        }
        chmod(&self.socket_path, 0o660);

        self.listener = Some(listener);
        Ok(())
    }

    /// Accept/dispatch loop until stop(); ticks the agent every poll_interval.
    pub fn run(&mut self, poll_interval: Duration) {
        let listener = self
            .listener
            .as_ref()
            .expect("run() before successful listen()");
        // Non-blocking accept + poll, so tick() runs even with no connections.
        listener
            .set_nonblocking(true)
            .expect("set_nonblocking on listener");
        let listener_fd = listener.as_raw_fd();

        while !self.stop.load(Ordering::Relaxed) {
            self.agent.tick(Instant::now());

            if !poll_readable(listener_fd, poll_interval) {
                continue;
            }
            // Re-borrow the listener here so handle_client can take &mut self below.
            let accepted = self.listener.as_ref().unwrap().accept();
            match accepted {
                Ok((stream, _)) => {
                    if self.peer_allowed(&stream) {
                        self.handle_client(stream);
                    }
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
                Err(e) => eprintln!("control: accept errno={e}"),
            }
        }
    }

    pub fn stop(&self) {
        self.stop.store(true, Ordering::Relaxed);
    }

    pub fn agent_mut(&mut self) -> &mut Agent<B> {
        &mut self.agent
    }

    // root always; else only the chowned owner UID (the sudo invoker).
    fn peer_allowed(&self, stream: &UnixStream) -> bool {
        match peer_uid(stream) {
            Some(uid) => uid == 0 || uid == self.expected_uid,
            None => false,
        }
    }

    fn handle_client(&mut self, mut stream: UnixStream) {
        // one request per connection (CLI opens a fresh socket per command)
        stream
            .set_read_timeout(Some(Duration::from_secs(5)))
            .ok();
        // write timeout too: a peer that never reads must not wedge the single-threaded loop
        stream
            .set_write_timeout(Some(Duration::from_secs(5)))
            .ok();
        let Some(req) = read_frame(&mut stream) else {
            return;
        };
        let req_str = String::from_utf8_lossy(&req);
        let resp = dispatch(&mut self.agent, &req_str, Instant::now());
        if !write_frame(&mut stream, resp.as_bytes()) {
            eprintln!("control: write_frame failed (client gone?)");
        }
    }
}

impl<B: Backend> Drop for ControlServer<B> {
    fn drop(&mut self) {
        if self.listener.is_some() {
            let _ = std::fs::remove_file(&self.socket_path);
        }
    }
}

// ---- libc glue --------------------------------------------------------------

fn sudo_uid_gid() -> Option<(u32, u32)> {
    let uid = std::env::var("SUDO_UID").ok()?.parse().ok()?;
    let gid = std::env::var("SUDO_GID").ok()?.parse().ok()?;
    Some((uid, gid))
}

fn chown(path: &Path, uid: u32, gid: u32) -> bool {
    let Ok(c) = std::ffi::CString::new(path.to_string_lossy().as_bytes()) else {
        return false;
    };
    // SAFETY: c is a valid NUL-terminated path for the duration of the call.
    unsafe { libc::chown(c.as_ptr(), uid, gid) == 0 }
}

fn chmod(path: &Path, mode: libc::mode_t) {
    if let Ok(c) = std::ffi::CString::new(path.to_string_lossy().as_bytes()) {
        // SAFETY: c is a valid NUL-terminated path for the duration of the call.
        unsafe { libc::chmod(c.as_ptr(), mode) };
    }
}

fn peer_uid(stream: &UnixStream) -> Option<u32> {
    let mut cred = libc::ucred {
        pid: 0,
        uid: 0,
        gid: 0,
    };
    let mut len = std::mem::size_of::<libc::ucred>() as libc::socklen_t;
    // SAFETY: getsockopt writes a ucred of `len` bytes into &cred on success.
    let rc = unsafe {
        libc::getsockopt(
            stream.as_raw_fd(),
            libc::SOL_SOCKET,
            libc::SO_PEERCRED,
            &mut cred as *mut _ as *mut libc::c_void,
            &mut len,
        )
    };
    (rc == 0).then_some(cred.uid)
}

// Block up to `timeout` for the listener fd to be readable (a pending connection).
fn poll_readable(fd: i32, timeout: Duration) -> bool {
    let mut pfd = libc::pollfd {
        fd,
        events: libc::POLLIN,
        revents: 0,
    };
    let ms = timeout.as_millis().min(i32::MAX as u128) as i32;
    // SAFETY: single valid pollfd; the kernel only writes revents.
    let r = unsafe { libc::poll(&mut pfd, 1, ms) };
    r > 0 && (pfd.revents & libc::POLLIN) != 0
}
