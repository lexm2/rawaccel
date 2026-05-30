//! rawaccel-agentd library: control plane + device orchestration for the Rust
//! daemon. The acceleration math and libbpf data plane live in
//! libra_backend.so, reached through ra-backend-sys; everything here is safe
//! Rust (the `unsafe` FFI is confined to the `backend` module).

pub mod backend;
pub mod config;
#[allow(dead_code)] // wired into discovery in Phase 5
pub mod hid;
