//! rawaccel-agentd library: control plane + device orchestration for the Rust
//! daemon. The acceleration math and libbpf data plane live in
//! libra_backend.so, reached through ra-backend-sys; everything here is safe
//! Rust (the `unsafe` FFI is confined to the `backend` module).

pub mod agent;
pub mod backend;
pub mod config;
pub mod discovery;
pub mod doctor;
pub mod hid;
pub mod server;
