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
    long min = std::strtol(end + 1, &end, 10);
    if (end == release + 1) return false;
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

    // Probes the same prog type the rawaccel program loads as; returns 1
    // when the kernel and our caps both allow bpf().
    int probe = libbpf_probe_bpf_prog_type(BPF_PROG_TYPE_STRUCT_OPS, nullptr);
    if (probe <= 0) {
        r.reason = "bpf() syscall denied (need CAP_BPF or root)";
        return r;
    }
    r.syscall_ok = true;
    return r;
}

} // namespace rawaccel_agent
