//! Sysfs HID device discovery, port of the discovery half of bpf_backend.cpp.
//! Enumerates /sys/class/hidraw, parses each report descriptor (via `hid`), and
//! attaches mouse-shaped devices through the backend. Discover-once at startup,
//! matching the C++ design (no hotplug monitor yet).

use std::path::Path;

use crate::agent::Agent;
use crate::backend::{Backend, DeviceInfo};
use crate::hid::{self, BpfDecision};

const HIDRAW_ROOT: &str = "/sys/class/hidraw";
const MAX_DESCRIPTOR: usize = 8192; // kernel caps at 4096; don't trust that here

/// Stable 64-bit device id from a string. FNV-1a; constants MUST match
/// `hash_id` in bpf_backend.cpp or ids drift across the C++/Rust boundary.
pub fn hash_id(key: &str) -> u64 {
    const OFFSET: u64 = 1469598103934665603;
    const PRIME: u64 = 1099511628211;
    let mut h = OFFSET;
    for b in key.bytes() {
        h ^= b as u64;
        h = h.wrapping_mul(PRIME);
    }
    h
}

/// "0003:046D:C54D.000A" -> trailing ".HHHH" is the hid_id in hex.
pub fn parse_hid_id(device_sysname: &str) -> Option<u32> {
    let tail = device_sysname.rsplit_once('.')?.1;
    if tail.is_empty() {
        return None;
    }
    u32::from_str_radix(tail, 16).ok()
}

/// "0003:046D:C54D.000A" -> (0x046D, 0xC54D).
pub fn parse_vid_pid(device_sysname: &str) -> Option<(u32, u32)> {
    let first = device_sysname.find(':')?;
    let second = device_sysname[first + 1..].find(':')? + first + 1;
    let dot = device_sysname[second + 1..].find('.')? + second + 1;
    let vid = u32::from_str_radix(&device_sysname[first + 1..second], 16).ok()?;
    let pid = u32::from_str_radix(&device_sysname[second + 1..dot], 16).ok()?;
    Some((vid, pid))
}

#[derive(Clone, Debug)]
pub struct HidrawNode {
    pub sysname: String,        // hidrawN
    pub device_sysname: String, // 0003:VVVV:PPPP.IIII
    pub hid_id: u32,
}

/// Read `<syspath>/device` symlink -> "0003:VVVV:PPPP.IIII" (its basename).
fn resolve_device_sysname(syspath: &Path) -> Option<String> {
    let target = std::fs::read_link(syspath.join("device")).ok()?;
    target
        .file_name()
        .map(|s| s.to_string_lossy().into_owned())
}

/// Read `<syspath>/device/report_descriptor` (capped); Err carries a distinct skip reason.
fn read_descriptor(syspath: &Path) -> Result<Vec<u8>, &'static str> {
    let bytes = std::fs::read(syspath.join("device/report_descriptor"))
        .map_err(|_| "cannot read report descriptor")?;
    if bytes.is_empty() {
        return Err("empty report descriptor");
    }
    if bytes.len() > MAX_DESCRIPTOR {
        return Err("report descriptor exceeds MAX_DESCRIPTOR");
    }
    Ok(bytes)
}

/// Parse the `HID_NAME=` line of `<syspath>/device/uevent`.
fn read_hid_name(syspath: &Path) -> String {
    let Ok(text) = std::fs::read_to_string(syspath.join("device/uevent")) else {
        return String::new();
    };
    for line in text.lines() {
        if let Some(name) = line.strip_prefix("HID_NAME=") {
            return name.to_string();
        }
    }
    String::new()
}

pub fn enumerate_hidraw() -> Vec<HidrawNode> {
    enumerate_in(Path::new(HIDRAW_ROOT))
}

fn enumerate_in(root: &Path) -> Vec<HidrawNode> {
    let Ok(entries) = std::fs::read_dir(root) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for entry in entries.flatten() {
        let sysname = entry.file_name().to_string_lossy().into_owned();
        let syspath = root.join(&sysname);
        let Some(device_sysname) = resolve_device_sysname(&syspath) else {
            continue;
        };
        if let Some(hid_id) = parse_hid_id(&device_sysname) {
            out.push(HidrawNode {
                sysname,
                device_sysname,
                hid_id,
            });
        }
    }
    out
}

/// Discover all hidraw nodes and attach each mouse-shaped one. Fail-open like
/// the C++ start(): rejects/parse failures are skipped, returns how many attached.
pub fn discover<B: Backend>(agent: &mut Agent<B>) -> usize {
    discover_in(agent, Path::new(HIDRAW_ROOT))
}

fn discover_in<B: Backend>(agent: &mut Agent<B>, root: &Path) -> usize {
    let mut attached = 0;
    for node in enumerate_in(root) {
        if try_attach(agent, root, &node) {
            attached += 1;
        }
    }
    if attached == 0 {
        eprintln!(
            "rawaccel: no mouse passed validate_for_bpf at start; \
             use `rawaccel-agentd --probe` to inspect attached devices"
        );
    }
    attached
}

fn try_attach<B: Backend>(agent: &mut Agent<B>, root: &Path, node: &HidrawNode) -> bool {
    let syspath = root.join(&node.sysname);
    let desc = match read_descriptor(&syspath) {
        Ok(d) => d,
        Err(reason) => {
            eprintln!("rawaccel: skipping {}: {reason}", node.sysname);
            return false;
        }
    };
    let Some(md) = hid::parse_mouse_descriptor(&desc) else {
        return false;
    };
    let layout = match hid::validate_for_bpf(&md) {
        BpfDecision::Accept(l) => l,
        BpfDecision::Reject(reason) => {
            eprintln!("rawaccel: skipping {}: {reason}", node.sysname);
            return false;
        }
    };

    let id = hash_id(&node.device_sysname);
    let (vendor_id, product_id) = parse_vid_pid(&node.device_sysname).unwrap_or((0, 0));
    let info = DeviceInfo {
        id,
        sysname: node.sysname.clone(),
        device_sysname: node.device_sysname.clone(),
        vendor_id,
        product_id,
        name: read_hid_name(&syspath),
    };

    match agent.attach_device(info, node.hid_id, &layout) {
        Ok(()) => true,
        Err(e) => {
            eprintln!("rawaccel: attach failed for {}: {e}", node.sysname);
            false
        }
    }
}

/// Inspect every hidraw node and report the BPF decision; backs `--probe`.
pub struct ProbeEntry {
    pub sysname: String,
    pub device_sysname: String,
    pub hid_id: u32,
    pub decision: Result<hid::MouseLayout, String>,
}

pub fn probe() -> Vec<ProbeEntry> {
    probe_in(Path::new(HIDRAW_ROOT))
}

fn probe_in(root: &Path) -> Vec<ProbeEntry> {
    enumerate_in(root)
        .into_iter()
        .map(|node| {
            let syspath = root.join(&node.sysname);
            let decision = match read_descriptor(&syspath) {
                Err(reason) => Err(reason.to_string()),
                Ok(d) => match hid::parse_mouse_descriptor(&d) {
                    None => Err("no mouse X/Y in report descriptor".to_string()),
                    Some(md) => match hid::validate_for_bpf(&md) {
                        BpfDecision::Accept(l) => Ok(l),
                        BpfDecision::Reject(r) => Err(r),
                    },
                },
            };
            ProbeEntry {
                sysname: node.sysname,
                device_sysname: node.device_sysname,
                hid_id: node.hid_id,
                decision,
            }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fnv1a_matches_cpp_reference() {
        // pinned against the C++ hash_id (and a python FNV-1a reference)
        assert_eq!(hash_id("0003:046D:C54D.000A"), 10373955226206672985);
        assert_eq!(hash_id("hidraw0"), 10540557010149253038);
        assert_eq!(hash_id(""), 1469598103934665603); // FNV offset basis
    }

    #[test]
    fn parses_hid_id_from_sysname() {
        assert_eq!(parse_hid_id("0003:046D:C54D.000A"), Some(0x000A));
        assert_eq!(parse_hid_id("0003:046D:C54D.0011"), Some(0x11));
        assert_eq!(parse_hid_id("no-dot"), None);
        assert_eq!(parse_hid_id("trailing."), None);
    }

    #[test]
    fn parses_vid_pid() {
        assert_eq!(parse_vid_pid("0003:046D:C54D.000A"), Some((0x046D, 0xC54D)));
        assert_eq!(parse_vid_pid("0003:1234:5678.000B"), Some((0x1234, 0x5678)));
        assert_eq!(parse_vid_pid("garbage"), None);
    }

    #[test]
    fn enumerate_reads_temp_sysfs_shape() {
        // Build a /sys/class/hidraw-shaped tree under a temp dir and enumerate it.
        let root = std::env::temp_dir().join(format!("ra-hidraw-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        let hidraw0 = root.join("hidraw0");
        let device = root.join("0003:046D:C54D.000A");
        std::fs::create_dir_all(&device).unwrap();
        std::fs::create_dir_all(&hidraw0).unwrap();
        std::os::unix::fs::symlink(&device, hidraw0.join("device")).unwrap();

        let nodes = enumerate_in(&root);
        let _ = std::fs::remove_dir_all(&root);

        assert_eq!(nodes.len(), 1);
        assert_eq!(nodes[0].sysname, "hidraw0");
        assert_eq!(nodes[0].device_sysname, "0003:046D:C54D.000A");
        assert_eq!(nodes[0].hid_id, 0x000A);
    }
}
