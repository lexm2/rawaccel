//! Port of linux/tests/control_server_tests.cpp: real AF_UNIX frame roundtrips
//! against a threaded ControlServer, plus frame-codec edge cases.

mod common;
use common::*;

use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::thread;
use std::time::Duration;

use rawaccel_agentd::agent::{agent_version, Agent, WRITE_DELAY};
use rawaccel_agentd::backend::NoopBackend;
use rawaccel_agentd::server::{read_frame, write_frame, ControlServer, MAX_FRAME_BYTES};
use serde_json::{json, Value};

fn tmp_socket_path(tag: &str) -> PathBuf {
    std::env::temp_dir().join(format!("ra-test-{}-{}.sock", std::process::id(), tag))
}

// One request per connection, mirroring the CLI client.
fn send(path: &PathBuf, req: &Value) -> Value {
    let mut stream = None;
    for _ in 0..50 {
        if let Ok(s) = UnixStream::connect(path) {
            stream = Some(s);
            break;
        }
        thread::sleep(Duration::from_millis(10));
    }
    let mut s = stream.expect("connect within 50 attempts");
    let payload = serde_json::to_vec(req).unwrap();
    s.write_all(&(payload.len() as u32).to_be_bytes()).unwrap();
    s.write_all(&payload).unwrap();

    let mut len_be = [0u8; 4];
    s.read_exact(&mut len_be).unwrap();
    let len = u32::from_be_bytes(len_be) as usize;
    let mut buf = vec![0u8; len];
    s.read_exact(&mut buf).unwrap();
    serde_json::from_slice(&buf).unwrap()
}

#[test]
fn status_roundtrip_over_af_unix() {
    let path = tmp_socket_path("status");
    let _ = std::fs::remove_file(&path);
    let mut server = ControlServer::new(Agent::new(NoopBackend::default()), path.clone());
    server.listen().expect("listen");
    let stop = server.stop_handle();
    let handle = thread::spawn(move || {
        server.run(Duration::from_millis(20));
    });

    let resp = send(&path, &json!({"cmd":"status"}));
    assert!(resp["ok"].as_bool().unwrap());
    assert_eq!(
        resp["agent"]["major"].as_i64().unwrap(),
        agent_version().0 as i64
    );

    stop.store(true, std::sync::atomic::Ordering::Relaxed);
    handle.join().unwrap();
}

#[test]
fn apply_then_status_then_get() {
    let path = tmp_socket_path("apply");
    let _ = std::fs::remove_file(&path);
    let mut agent = Agent::new(NoopBackend::default());
    agent.on_device_added(dev_info(1, "hidraw0", "0003:046D:C54D.000A", ""));
    let mut server = ControlServer::new(agent, path.clone());
    server.listen().expect("listen");
    let stop = server.stop_handle();
    let handle = thread::spawn(move || {
        server.run(Duration::from_millis(20));
        server // hand the server (and its agent/backend) back for assertions
    });

    let cfg = cfg_value(
        vec![profile("default", 1000.0)],
        vec![],
        config::default_device_config(),
    );
    let r_apply = send(&path, &json!({"cmd":"apply","config": cfg}));
    assert!(r_apply["ok"].as_bool().unwrap());

    let r_before = send(&path, &json!({"cmd":"status"}));
    assert!(r_before["has_pending_apply"].as_bool().unwrap());

    thread::sleep(WRITE_DELAY + Duration::from_millis(150));

    let r_after = send(&path, &json!({"cmd":"status"}));
    assert!(!r_after["has_pending_apply"].as_bool().unwrap());
    assert!(r_after["has_active_config"].as_bool().unwrap());

    let r_get = send(&path, &json!({"cmd":"get"}));
    assert!(r_get["ok"].as_bool().unwrap());
    assert!(r_get["config"]["profiles"].is_array());
    assert_eq!(r_get["config"]["profiles"].as_array().unwrap().len(), 1);

    stop.store(true, std::sync::atomic::Ordering::Relaxed);
    let mut server = handle.join().unwrap();
    assert_eq!(server.agent_mut().backend().binds, 1);
}

// ---- frame codec edge cases -------------------------------------------------

#[test]
fn frame_zero_length_roundtrips() {
    let (mut a, mut b) = UnixStream::pair().unwrap();
    assert!(write_frame(&mut a, b""));
    assert_eq!(read_frame(&mut b), Some(Vec::new()));
}

#[test]
fn frame_roundtrips_payload() {
    let (mut a, mut b) = UnixStream::pair().unwrap();
    assert!(write_frame(&mut a, b"hello"));
    assert_eq!(read_frame(&mut b), Some(b"hello".to_vec()));
}

#[test]
fn frame_rejects_oversize_length() {
    let (mut a, mut b) = UnixStream::pair().unwrap();
    // a length header above the cap must be rejected before reading the body
    a.write_all(&(MAX_FRAME_BYTES + 1).to_be_bytes()).unwrap();
    assert_eq!(read_frame(&mut b), None);
}

#[test]
fn frame_rejects_truncated_payload() {
    let (mut a, mut b) = UnixStream::pair().unwrap();
    a.write_all(&10u32.to_be_bytes()).unwrap(); // claim 10 bytes
    drop(a); // then hang up before sending them
    assert_eq!(read_frame(&mut b), None);
}

#[test]
fn write_frame_rejects_oversize_payload() {
    let (mut a, _b) = UnixStream::pair().unwrap();
    let big = vec![0u8; MAX_FRAME_BYTES as usize + 1];
    assert!(!write_frame(&mut a, &big));
}
