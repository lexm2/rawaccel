#pragma once

// Can this kernel + process host the rawaccel HID-BPF program? (kernel >= 6.11, bpf() callable via CAP_BPF/root)

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
