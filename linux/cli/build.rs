// Parse common/rawaccel-version.h at build time so the CLI version stays
// locked to the same RA_VER_* constants the agent uses.

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

    let major = extract_int(&src, "RA_VER_MAJOR");
    let minor = extract_int(&src, "RA_VER_MINOR");
    let patch = extract_int(&src, "RA_VER_PATCH");

    println!("cargo:rustc-env=RA_VER_MAJOR={major}");
    println!("cargo:rustc-env=RA_VER_MINOR={minor}");
    println!("cargo:rustc-env=RA_VER_PATCH={patch}");
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
