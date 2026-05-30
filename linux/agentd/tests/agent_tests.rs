//! Port of linux/tests/agent_tests.cpp: agent state machine + RPC dispatch over
//! a NoopBackend. Resolve assertions check bound profile/config JSON, not native structs.

mod common;
use common::*;

use std::time::{Duration, Instant};

use rawaccel_agentd::agent::{agent_version, Agent, VersionStatus, WRITE_DELAY};
use rawaccel_agentd::backend::{DataPlaneHealth, NoopBackend, SpeedSample};
use rawaccel_agentd::server::dispatch;
use serde_json::{json, Value};

const LOGI: &str = "0003:046D:C54D.000A";
const OTHER_B: &str = "0003:1234:5678.000B";

fn new_agent() -> Agent<NoopBackend> {
    Agent::new(NoopBackend::default())
}

fn call(agent: &mut Agent<NoopBackend>, req: Value, now: Instant) -> Value {
    serde_json::from_str(&dispatch(agent, &req.to_string(), now)).unwrap()
}

fn dispatch_str(agent: &mut Agent<NoopBackend>, raw: &str, now: Instant) -> Value {
    serde_json::from_str(&dispatch(agent, raw, now)).unwrap()
}

// ---- version negotiation ----------------------------------------------------

#[test]
fn version_ok_for_matching_version() {
    let mut a = new_agent();
    let (maj, min, pat) = agent_version();
    let resp = call(
        &mut a,
        json!({"cmd":"version","client":{"major":maj,"minor":min,"patch":pat}}),
        Instant::now(),
    );
    assert!(resp["ok"].as_bool().unwrap());
    assert_eq!(resp["agent"]["major"].as_i64().unwrap(), maj as i64);
    assert_eq!(resp["agent"]["minor"].as_i64().unwrap(), min as i64);
    assert_eq!(resp["agent"]["patch"].as_i64().unwrap(), pat as i64);
}

#[test]
fn version_rejects_client_below_min() {
    let mut a = new_agent();
    let resp = call(
        &mut a,
        json!({"cmd":"version","client":{"major":0,"minor":0,"patch":1}}),
        Instant::now(),
    );
    assert!(!resp["ok"].as_bool().unwrap());
    assert_eq!(resp["reason"], "client_too_old");
}

#[test]
fn version_rejects_client_above_agent() {
    let mut a = new_agent();
    let (maj, min, pat) = agent_version();
    let resp = call(
        &mut a,
        json!({"cmd":"version","client":{"major":maj,"minor":min,"patch":pat+1}}),
        Instant::now(),
    );
    assert!(!resp["ok"].as_bool().unwrap());
    assert_eq!(resp["reason"], "client_too_new");
}

#[test]
fn version_check_matches_driver_semantics() {
    let a = new_agent();
    assert_eq!(a.check_version(agent_version()).status, VersionStatus::Ok);
    assert_eq!(
        a.check_version((0, 0, 1)).status,
        VersionStatus::ClientTooOld
    );
    let (maj, min, pat) = agent_version();
    assert_eq!(
        a.check_version((maj, min, pat + 1)).status,
        VersionStatus::ClientTooNew
    );
}

// ---- apply / debounce -------------------------------------------------------

#[test]
fn apply_schedules_pending() {
    let mut a = new_agent();
    a.on_device_added(dev_info(1, "hidraw0", LOGI, ""));
    let t0 = Instant::now();
    let resp = call(
        &mut a,
        json!({"cmd":"apply","config": cfg_value(vec![profile("default", 1000.0)], vec![], config::default_device_config())}),
        t0,
    );
    assert!(resp["ok"].as_bool().unwrap());
    assert_eq!(resp["deferred_ms"].as_i64().unwrap(), 1000);
    assert_eq!(a.backend().binds, 0);
    assert!(a.status(t0).has_pending_apply);
    assert!(a.tick(t0 + WRITE_DELAY));
    assert_eq!(a.backend().binds, 1);
}

#[test]
fn apply_fails_when_data_plane_dead() {
    let mut a = new_agent();
    a.backend_mut().forced_health = DataPlaneHealth {
        devices: 1,
        attached: 0,
        error: "hidraw0: attach failed: Invalid argument (errno 22)".into(),
    };
    let t0 = Instant::now();
    let resp = call(
        &mut a,
        json!({"cmd":"apply","config": cfg_value(vec![profile("default", 1000.0)], vec![], config::default_device_config())}),
        t0,
    );
    assert!(!resp["ok"].as_bool().unwrap());
    assert!(resp["error"].as_str().unwrap().contains("attach failed"));
    assert!(!a.status(t0).has_pending_apply);
}

#[test]
fn apply_succeeds_when_attached() {
    let mut a = new_agent();
    a.backend_mut().forced_health = DataPlaneHealth {
        devices: 1,
        attached: 1,
        error: String::new(),
    };
    let t0 = Instant::now();
    let resp = call(
        &mut a,
        json!({"cmd":"apply","config": cfg_value(vec![profile("default", 1000.0)], vec![], config::default_device_config())}),
        t0,
    );
    assert!(resp["ok"].as_bool().unwrap());
    assert!(a.status(t0).has_pending_apply);
}

#[test]
fn apply_debounces_to_one_bind_per_device() {
    let mut a = new_agent();
    a.on_device_added(dev_info(1, "hidraw0", LOGI, ""));
    let t0 = Instant::now();
    a.schedule_apply(cfg_one_profile("a", 1.0), t0);
    a.schedule_apply(cfg_one_profile("b", 2.0), t0 + Duration::from_millis(100));
    assert!(!a.tick(t0 + Duration::from_millis(200)));
    assert_eq!(a.backend().binds, 0);
    assert!(a.tick(t0 + Duration::from_millis(1150)));
    assert_eq!(a.backend().binds, 1);
    assert!(!a.tick(t0 + Duration::from_millis(2000)));
    assert_eq!(a.backend().binds, 1);
}

#[test]
fn apply_held_until_write_delay() {
    let mut a = new_agent();
    a.on_device_added(dev_info(1, "hidraw0", LOGI, ""));
    let t0 = Instant::now();
    a.schedule_apply(cfg_one_profile("default", 1.0), t0);
    assert!(!a.tick(t0 + Duration::from_millis(999)));
    assert_eq!(a.backend().binds, 0);
    assert!(a.tick(t0 + WRITE_DELAY));
    assert_eq!(a.backend().binds, 1);
}

#[test]
fn apply_with_no_devices_binds_nothing() {
    let mut a = new_agent();
    let t0 = Instant::now();
    a.schedule_apply(cfg_one_profile("default", 1.0), t0);
    a.tick(t0 + WRITE_DELAY);
    assert_eq!(a.backend().binds, 0);
}

#[test]
fn apply_rebinds_every_known_device() {
    let mut a = new_agent();
    a.on_device_added(dev_info(1, "hidraw0", LOGI, ""));
    a.on_device_added(dev_info(2, "hidraw1", OTHER_B, ""));
    let t0 = Instant::now();
    a.schedule_apply(cfg_one_profile("default", 1.0), t0);
    a.tick(t0 + WRITE_DELAY);
    assert_eq!(a.backend().binds, 2);
}

// ---- get / deactivate / unknown ---------------------------------------------

#[test]
fn get_returns_active_config() {
    let mut a = new_agent();
    let t0 = Instant::now();
    a.schedule_apply(cfg_one_profile("default", 1.0), t0);
    a.tick(t0 + WRITE_DELAY);
    let resp = call(&mut a, json!({"cmd":"get"}), t0);
    assert!(resp["ok"].as_bool().unwrap());
    assert!(resp["config"].is_object());
    assert!(resp["config"]["profiles"].is_array());
}

#[test]
fn rejects_unknown_cmd() {
    let mut a = new_agent();
    let resp = call(&mut a, json!({"cmd":"nope"}), Instant::now());
    assert!(!resp["ok"].as_bool().unwrap());
}

#[test]
fn rejects_malformed_json() {
    let mut a = new_agent();
    let resp = dispatch_str(&mut a, "{not json", Instant::now());
    assert!(!resp["ok"].as_bool().unwrap());
}

#[test]
fn status_includes_agent_version() {
    let mut a = new_agent();
    let resp = call(&mut a, json!({"cmd":"status"}), Instant::now());
    assert!(resp["ok"].as_bool().unwrap());
    assert_eq!(
        resp["agent"]["major"].as_i64().unwrap(),
        agent_version().0 as i64
    );
}

#[test]
fn deactivate_clears_pending_and_rebinds() {
    let mut a = new_agent();
    a.on_device_added(dev_info(1, "hidraw0", LOGI, ""));
    assert_eq!(a.backend().binds, 0);
    let t0 = Instant::now();
    a.schedule_apply(cfg_one_profile("default", 1.0), t0);
    assert!(a.status(t0).has_pending_apply);
    a.deactivate();
    let s = a.status(t0);
    assert!(!s.has_pending_apply);
    assert!(s.has_active_config);
    assert_eq!(a.backend().binds, 1);
    assert!(a.get_active().profiles.is_empty());
    assert!(!a.tick(t0 + Duration::from_millis(2000)));
    assert_eq!(a.backend().binds, 1);
}

// ---- device add / remove ----------------------------------------------------

#[test]
fn added_before_apply_waits_for_active_config() {
    let mut a = new_agent();
    a.on_device_added(dev_info(1, "hidraw0", LOGI, ""));
    assert_eq!(a.backend().binds, 0);
}

#[test]
fn added_after_apply_binds_immediately() {
    let mut a = new_agent();
    let t0 = Instant::now();
    a.schedule_apply(cfg_one_profile("default", 1234.5), t0);
    a.tick(t0 + WRITE_DELAY);
    assert_eq!(a.backend().binds, 0);
    a.on_device_added(dev_info(7, "hidraw7", LOGI, ""));
    assert_eq!(a.backend().binds, 1);
    assert_eq!(a.backend().last_profile[&7][OUTPUT_DPI].as_f64().unwrap(), 1234.5);
}

#[test]
fn removed_unbinds_and_is_idempotent() {
    let mut a = new_agent();
    let t0 = Instant::now();
    a.schedule_apply(cfg_one_profile("default", 1.0), t0);
    a.tick(t0 + WRITE_DELAY);
    a.on_device_added(dev_info(42, "hidraw0", LOGI, ""));
    assert_eq!(a.backend().binds, 1);
    a.on_device_removed(42);
    assert_eq!(a.backend().unbinds, 1);
    a.on_device_removed(42);
    assert_eq!(a.backend().unbinds, 1);
}

// ---- resolve ----------------------------------------------------------------

#[test]
fn resolve_picks_profile_matched_by_device_id() {
    let mut a = new_agent();
    let cfg = parse(cfg_value(
        vec![
            profile("default", 1000.0),
            profile("gaming", 8000.0),
            profile("trackpad", 1600.0),
        ],
        vec![device(LOGI, "", "gaming", Value::Null)],
        config::default_device_config(),
    ));
    let t0 = Instant::now();
    a.schedule_apply(cfg, t0);
    a.tick(t0 + WRITE_DELAY);
    a.on_device_added(dev_info(1, "hidraw0", LOGI, ""));
    assert_eq!(a.backend().binds, 1);
    assert_eq!(a.backend().last_profile[&1][OUTPUT_DPI].as_f64().unwrap(), 8000.0);
    a.on_device_added(dev_info(2, "hidraw1", OTHER_B, "Some Trackpad"));
    assert_eq!(a.backend().binds, 2);
    assert_eq!(a.backend().last_profile[&2][OUTPUT_DPI].as_f64().unwrap(), 1000.0);
}

#[test]
fn resolve_picks_profile_matched_by_device_name() {
    let mut a = new_agent();
    let cfg = parse(cfg_value(
        vec![profile("default", 1000.0), profile("by_name", 4242.0)],
        vec![device("", "Logitech G Pro", "by_name", Value::Null)],
        config::default_device_config(),
    ));
    let t0 = Instant::now();
    a.schedule_apply(cfg, t0);
    a.tick(t0 + WRITE_DELAY);
    a.on_device_added(dev_info(5, "hidraw0", LOGI, "Logitech G Pro"));
    assert_eq!(a.backend().last_profile[&5][OUTPUT_DPI].as_f64().unwrap(), 4242.0);
}

#[test]
fn resolve_falls_back_when_profile_name_unknown() {
    let mut a = new_agent();
    let cfg = parse(cfg_value(
        vec![profile("default", 1000.0)],
        vec![device(LOGI, "", "nonexistent", Value::Null)],
        config::default_device_config(),
    ));
    let t0 = Instant::now();
    a.schedule_apply(cfg, t0);
    a.tick(t0 + WRITE_DELAY);
    a.on_device_added(dev_info(1, "hidraw0", LOGI, ""));
    assert_eq!(a.backend().last_profile[&1][OUTPUT_DPI].as_f64().unwrap(), 1000.0);
}

#[test]
fn resolve_uses_device_config_for_matched_devices() {
    let mut a = new_agent();
    let cfg = parse(cfg_value(
        vec![profile("default", 1000.0)],
        vec![device(LOGI, "", "default", device_config(16000))],
        device_config(800),
    ));
    let t0 = Instant::now();
    a.schedule_apply(cfg, t0);
    a.tick(t0 + WRITE_DELAY);
    a.on_device_added(dev_info(1, "hidraw0", LOGI, ""));
    assert_eq!(a.backend().last_config[&1][DPI].as_i64().unwrap(), 16000);
    a.on_device_added(dev_info(2, "hidraw1", "0003:0001:0002.000B", ""));
    assert_eq!(a.backend().last_config[&2][DPI].as_i64().unwrap(), 800);
}

// ---- speed / status ---------------------------------------------------------

#[test]
fn current_speed_sample_delegates_to_backend() {
    let mut a = new_agent();
    assert_eq!(a.current_speed_sample().combined, 0.0);
    a.backend_mut().forced_speed = SpeedSample {
        x: 3.0,
        y: 4.0,
        combined: 5.0,
    };
    assert_eq!(a.current_speed_sample().x, 3.0);
    assert_eq!(a.current_speed_sample().y, 4.0);
    assert_eq!(a.current_speed_sample().combined, 5.0);
}

#[test]
fn status_reports_pending_window_and_device_count() {
    let mut a = new_agent();
    let t0 = Instant::now();
    let s = a.status(t0);
    assert!(!s.has_active_config);
    assert!(!s.has_pending_apply);
    assert_eq!(s.until_apply_ms, 0);
    assert_eq!(s.connected_devices, 0);

    a.on_device_added(dev_info(1, "hidraw0", LOGI, ""));
    assert_eq!(a.status(t0).connected_devices, 1);

    a.schedule_apply(cfg_one_profile("default", 1.0), t0);
    let s = a.status(t0 + Duration::from_millis(250));
    assert!(s.has_pending_apply);
    assert_eq!(s.until_apply_ms, 750);

    a.tick(t0 + WRITE_DELAY);
    let s = a.status(t0 + WRITE_DELAY);
    assert!(s.has_active_config);
    assert!(!s.has_pending_apply);
}
