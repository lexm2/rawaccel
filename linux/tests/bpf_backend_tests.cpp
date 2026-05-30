// Capability probe seam for the BPF backend. The full attach path needs CAP_BPF
// and a live mouse (the interactive privileged smoke test). Discovery and HID
// descriptor parsing now live in the Rust daemon (linux/agentd).

#include "bpf_capability.hpp"
#include "test_harness.hpp"

using namespace rawaccel_agent;

RA_TEST("BpfCapability: probe reports a kernel version on this host")
{
    auto r = probe_bpf_capability();
    // not ok(): CI/unprivileged lacks CAP_BPF. Only check uname parses cleanly.
    RA_CHECK(r.kernel_major >= 4);
    RA_CHECK(r.kernel_minor >= 0);
}
