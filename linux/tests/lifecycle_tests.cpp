// FFI lifecycle leak test for the C ABI (ra_backend.h). Built as a pure C++
// binary linking ra_backend directly (no Rust), so under -fsanitize=address it
// uses one ASAN runtime with no cross-language mismatch. LSAN then proves the
// create/bind/health/speed/detach/destroy cycle leaks nothing across many
// iterations -- the libbpf object handle, the Slot map, and the JSON parse
// churn in particular.
//
// This stays kernel-free (no CAP_BPF): ra_backend_create opens nothing until a
// device attaches, and ra_backend_bind with an unknown id only parses the JSON
// then no-ops on the missing slot (the same trick agentd's ffi_contract.rs
// uses). ra_backend_attach (which needs CAP_BPF) is intentionally not called.

#include "ra_backend.h"
#include "test_harness.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>

#ifndef RA_FIXTURE_PATH
#error "RA_FIXTURE_PATH must be defined (path to default_config.json)"
#endif

namespace {

// The resolved-settings doc ra_backend_bind parses: {"profile":..,"config":..}.
// Assembled from the frozen contract fixture so the keys never drift from the
// matching _from_jobject parsers.
std::string build_resolved()
{
    std::ifstream f(RA_FIXTURE_PATH);
    std::stringstream ss;
    ss << f.rdbuf();
    const nlohmann::json fixture = nlohmann::json::parse(ss.str());
    nlohmann::json j;
    j["profile"] = fixture.at("profiles").at(0);
    j["config"]  = fixture.at("defaultDeviceConfig");
    return j.dump();
}

} // namespace

RA_TEST("Lifecycle: create/destroy churns no memory")
{
    for (int i = 0; i < 1000; ++i) {
        ra_backend_t* be = ra_backend_create("/nonexistent.bpf.o");
        RA_CHECK(be != nullptr);
        ra_backend_destroy(be);
    }
}

RA_TEST("Lifecycle: full create/bind/health/speed/detach/destroy loop is leak-free")
{
    const std::string resolved = build_resolved();

    for (int i = 0; i < 1000; ++i) {
        ra_backend_t* be = ra_backend_create("/nonexistent.bpf.o");
        RA_CHECK(be != nullptr);

        // Unknown id -> JSON parse path only, no kernel/libbpf interaction.
        int rc = ra_backend_bind(be, 999, resolved.c_str());
        RA_CHECK(rc == 0);  // json_io accepted the round-tripped doc

        ra_backend_health_t h{};
        ra_backend_health(be, &h);
        RA_CHECK(h.devices == 0);   // nothing attached

        ra_speed_sample_t s{};
        ra_backend_speed(be, &s);

        ra_backend_detach(be, 999); // no slot -> no-op, must not crash/leak
        ra_backend_destroy(be);
    }
}

RA_TEST("Lifecycle: bad input is rejected without leaking")
{
    const std::string resolved = build_resolved();

    ra_backend_t* be = ra_backend_create("/nonexistent.bpf.o");
    RA_CHECK(be != nullptr);

    // Malformed JSON: parse throws inside, caught, returns -1, no leak.
    RA_CHECK(ra_backend_bind(be, 1, "{ not valid json") == -1);
    // Null-arg guards.
    RA_CHECK(ra_backend_bind(be, 1, nullptr) == -1);
    RA_CHECK(ra_backend_bind(nullptr, 1, resolved.c_str()) == -1);

    ra_backend_destroy(be);
}
