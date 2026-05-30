#include "bpf_capability.hpp"

#include <bpf/libbpf.h>

#include <sys/utsname.h>

#include <cstdio>
#include <cstdlib>

namespace rawaccel_agent {

namespace {

constexpr int MIN_KERNEL_MAJOR = 6;
constexpr int MIN_KERNEL_MINOR = 11;

bool parse_kernel_version(const char* release, int& major, int& minor)
{
    if (!release) return false;
    char* end = nullptr;
    long maj = std::strtol(release, &end, 10);
    if (end == release || *end != '.') return false;
    const char* min_start = end + 1;
    long min = std::strtol(min_start, &end, 10);
    if (end == min_start) return false;  // no minor digits (e.g. "6.")
    major = static_cast<int>(maj);
    minor = static_cast<int>(min);
    return true;
}

} // namespace

BpfProbeResult probe_bpf_capability()
{
    BpfProbeResult r;

    struct utsname u{};
    if (::uname(&u) != 0) {
        r.reason = "uname() failed";
        return r;
    }
    if (!parse_kernel_version(u.release, r.kernel_major, r.kernel_minor)) {
        r.reason = "could not parse kernel release";
        return r;
    }
    if (r.kernel_major > MIN_KERNEL_MAJOR ||
        (r.kernel_major == MIN_KERNEL_MAJOR && r.kernel_minor >= MIN_KERNEL_MINOR)) {
        r.kernel_ok = true;
    } else {
        char buf[64];
        std::snprintf(buf, sizeof(buf),
            "kernel %d.%d < required %d.%d",
            r.kernel_major, r.kernel_minor,
            MIN_KERNEL_MAJOR, MIN_KERNEL_MINOR);
        r.reason = buf;
        return r;
    }

    // probe the prog type rawaccel loads: 1 = ok, 0 = unsupported, <0 = error
    int probe = libbpf_probe_bpf_prog_type(BPF_PROG_TYPE_STRUCT_OPS, nullptr);
    if (probe == 0) {
        r.reason = "kernel lacks BPF struct_ops support for HID-BPF";
        return r;
    }
    if (probe < 0) {
        r.reason = "bpf() probe failed (need CAP_BPF or root)";
        return r;
    }
    r.syscall_ok = true;
    return r;
}

} // namespace rawaccel_agent
