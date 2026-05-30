# Packaging the rawaccel Linux agent

The Linux agent is split across two toolchains:

- **CMake (C++)** builds `libra_backend.so` (libbpf + curve math behind the C ABI),
  the GUI curve shim `librawaccel_common.so`, and the BPF object `rawaccel.bpf.o`.
- **cargo (Rust)** builds the daemon `rawaccel-agentd` and the CLI `rawaccel`.

There is no CMake -> cargo coupling, so installing is a two-step process.

## Build + install

```sh
# bindir CMake installs into; the Rust binaries must land in the SAME dir so the
# systemd unit's ExecStart path resolves. Distro packages set PREFIX=/usr.
PREFIX=/usr/local

# 1. C++ artifacts: .so, shim, BPF object, and the systemd unit.
cmake -S linux -B linux/build -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build linux/build
sudo cmake --install linux/build            # honors --prefix / DESTDIR

# 2. Rust daemon + CLI (link libra_backend.so, built above) into the same bindir.
cargo build --release --manifest-path linux/Cargo.toml -p rawaccel-agentd -p rawaccel-cli
sudo install -m755 linux/target/release/rawaccel-agentd "$PREFIX/bin/"
sudo install -m755 linux/target/release/rawaccel        "$PREFIX/bin/"
```

The installed systemd unit's `ExecStart` is
`<bindir>/rawaccel-agentd --bpf-object <libdir>/rawaccel/rawaccel.bpf.o`.

> The unit's absolute paths are baked at **configure** time from
> `CMAKE_INSTALL_PREFIX`. Set the prefix on `cmake -S/-B` (as above), not via
> `cmake --install --prefix`; overriding it only at install time moves the files
> but not the paths written into the unit, so they diverge. Use `DESTDIR` for
> staged/packaged installs (it prepends to every path without touching the unit).

`libra_backend.so` installs into `<libdir>` (a default `ld.so` search path), so the
daemon finds it at runtime without an rpath. The dev-tree rpath baked into the cargo
binary points at `linux/build` and is simply ignored on an install box.

## What gets installed where

| Artifact | Destination | Installed by |
| --- | --- | --- |
| `libra_backend.so` | `<libdir>` | cmake |
| `librawaccel_common.so` | `<libdir>` | cmake |
| `rawaccel.bpf.o` | `<libdir>/rawaccel/` | cmake |
| `rawaccel-agentd.service` | systemd unit dir | cmake |
| `rawaccel-agentd` (daemon) | `<bindir>` | cargo + install |
| `rawaccel` (CLI) | `<bindir>` | cargo + install |
