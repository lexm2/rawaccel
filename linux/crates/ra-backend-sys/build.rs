// Link against libra_backend.so produced by the CMake build. Default location
// is linux/build (../../build from this crate); override with RA_BACKEND_LIB_DIR.
// link-search/link-lib propagate transitively to dependents via the `links` key;
// the runtime rpath is emitted by the binary crate (agentd/build.rs), since
// rustc-link-arg does not propagate from a dependency build script.

use std::env;
use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-env-changed=RA_BACKEND_LIB_DIR");
    println!("cargo:rustc-link-search=native={}", lib_dir().display());
    println!("cargo:rustc-link-lib=dylib=ra_backend");
}

fn lib_dir() -> PathBuf {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let default_dir = manifest_dir.join("..").join("..").join("build");
    let dir = env::var("RA_BACKEND_LIB_DIR")
        .map(PathBuf::from)
        .unwrap_or(default_dir);
    dir.canonicalize().unwrap_or(dir)
}
