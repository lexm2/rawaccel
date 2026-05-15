#pragma once

// Probe whether this kernel + this process can host the rawaccel HID-BPF
// program. Two checks:
//   1. kernel >= 6.11 (where struct_ops HID-BPF landed in the form the
//      .bpf.c expects).
//   2. the bpf() syscall is callable by this process (CAP_BPF or root).
//
// Cheap to call at agent start. Reported in the log on auto backend
// selection.

#include <string>

namespace rawaccel_agent {

struct BpfProbeResult {
    bool kernel_ok = false;
    bool syscall_ok = false;
    int kernel_major = 0;
    int kernel_minor = 0;
    std::string reason;  // human-readable; "" when both checks pass.

    bool ok() const { return kernel_ok && syscall_ok; }
};

BpfProbeResult probe_bpf_capability();

} // namespace rawaccel_agent
