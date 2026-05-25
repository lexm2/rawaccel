// Tests for the BPF backend helpers. The full BpfBackend::start() path needs
// CAP_BPF and a live mouse (the interactive privileged smoke test); these
// cover the unit-testable seams.

#include "bpf_backend.hpp"
#include "bpf_capability.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <string>

using namespace rawaccel_agent;

RA_TEST("HidSysfs: parse_hid_device_name handles canonical form")
{
    std::uint32_t id = 0;
    RA_CHECK(parse_hid_device_name("0003:046D:C54D.000A", id));
    RA_CHECK_EQ(static_cast<int>(id), 10);

    RA_CHECK(parse_hid_device_name("0003:0001:0001.0001", id));
    RA_CHECK_EQ(static_cast<int>(id), 1);

    RA_CHECK(parse_hid_device_name("0003:1E71:3012.000C", id));
    RA_CHECK_EQ(static_cast<int>(id), 12);
}

RA_TEST("HidSysfs: parse_hid_device_name rejects malformed strings")
{
    std::uint32_t id = 999;
    RA_CHECK(!parse_hid_device_name("no_dot_here", id));
    RA_CHECK(!parse_hid_device_name("dot.no_hex", id));
    RA_CHECK(!parse_hid_device_name("", id));
}

RA_TEST("BpfCapability: probe reports a kernel version on this host")
{
    auto r = probe_bpf_capability();
    // not ok(): CI/unprivileged lacks CAP_BPF. Only check uname parses cleanly.
    RA_CHECK(r.kernel_major >= 4);
    RA_CHECK(r.kernel_minor >= 0);
}

RA_TEST("HidSysfs: enumerate_hidraw returns nodes whose hid_id parses")
{
    // may be empty on CI. Assert only that every entry has a parseable
    // hid_id and that the empty case doesn't crash.
    auto nodes = enumerate_hidraw();
    for (const auto& n : nodes) {
        std::uint32_t id = 0;
        RA_CHECK(parse_hid_device_name(n.device_sysname, id));
        RA_CHECK_EQ(static_cast<int>(id), static_cast<int>(n.hid_id));
        RA_CHECK(!n.sysname.empty());
    }
}
