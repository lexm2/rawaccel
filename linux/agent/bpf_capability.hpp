#pragma once

// Can this kernel + process host the rawaccel HID-BPF program?
//   1. kernel >= 6.11 (struct_ops HID-BPF the .bpf.c expects)
//   2. bpf() callable here (CAP_BPF or root)

#include <string>

namespace rawaccel_agent {

struct BpfProbeResult {
    bool kernel_ok = false;
    bool syscall_ok = false;
    int kernel_major = 0;
    int kernel_minor = 0;
    std::string reason;  // "" when both checks pass

    bool ok() const { return kernel_ok && syscall_ok; }
};

BpfProbeResult probe_bpf_capability();

} // namespace rawaccel_agent
