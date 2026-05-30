// Lock the daemon version to the same common/rawaccel-version.h the C++ side
// uses (mirrors cli/build.rs). Also embed the rpath to libra_backend.so so the
// dev binary finds it without LD_LIBRARY_PATH (rustc-link-arg-bins applies to
// this package's binaries; the dependency build script can't emit it).

use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let header = manifest_dir
        .join("..")
        .join("..")
        .join("common")
        .join("rawaccel-version.h");

    println!("cargo:rerun-if-changed={}", header.display());

    let src = fs::read_to_string(&header)
        .unwrap_or_else(|e| panic!("failed to read {}: {}", header.display(), e));

    for name in ["RA_VER_MAJOR", "RA_VER_MINOR", "RA_VER_PATCH"] {
        println!("cargo:rustc-env={name}={}", extract_int(&src, name));
    }

    // min_driver_version = { a, b, c }; gates version negotiation (client_too_old).
    let (min_major, min_minor, min_patch) = extract_min_version(&src);
    println!("cargo:rustc-env=RA_MIN_VER_MAJOR={min_major}");
    println!("cargo:rustc-env=RA_MIN_VER_MINOR={min_minor}");
    println!("cargo:rustc-env=RA_MIN_VER_PATCH={min_patch}");

    println!("cargo:rerun-if-env-changed=RA_BACKEND_LIB_DIR");
    let lib_dir = env::var("RA_BACKEND_LIB_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| manifest_dir.join("..").join("build"));
    let lib_dir = lib_dir.canonicalize().unwrap_or(lib_dir);
    // bins for the daemon, tests for the FFI contract test.
    println!("cargo:rustc-link-arg-bins=-Wl,-rpath,{}", lib_dir.display());
    println!("cargo:rustc-link-arg-tests=-Wl,-rpath,{}", lib_dir.display());
}

// Parse `min_driver_version = { a, b, c }` (whitespace-insensitive) into a triple.
fn extract_min_version(src: &str) -> (i32, i32, i32) {
    let line = src
        .lines()
        .find(|l| l.contains("min_driver_version"))
        .unwrap_or_else(|| panic!("missing min_driver_version in rawaccel-version.h"));
    let braces = line
        .split_once('{')
        .and_then(|(_, r)| r.split_once('}'))
        .map(|(inner, _)| inner)
        .unwrap_or_else(|| panic!("malformed min_driver_version line: {line}"));
    let nums: Vec<i32> = braces
        .split(',')
        .filter_map(|s| s.trim().parse().ok())
        .collect();
    match nums.as_slice() {
        [a, b, c] => (*a, *b, *c),
        _ => panic!("min_driver_version needs 3 ints, got: {braces}"),
    }
}

fn extract_int(src: &str, name: &str) -> i32 {
    let needle = format!("#define {name} ");
    let line = src
        .lines()
        .find(|l| l.trim_start().starts_with(&needle))
        .unwrap_or_else(|| panic!("missing #define {name} in rawaccel-version.h"));
    line.trim_start()
        .trim_start_matches(&needle)
        .split_whitespace()
        .next()
        .and_then(|s| s.parse().ok())
        .unwrap_or_else(|| panic!("could not parse int for {name}"))
}
