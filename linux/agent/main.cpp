// rawaccel-agentd: owns the active modifier state, serves apply/get/version/
// status RPCs over /run/rawaccel/control.sock, pushes settings to the backend.
//
// --backend auto: probe, pick bpf if supported, else exit.
// --backend bpf:  force HID-BPF (kernel >= 6.11, CAP_BPF).
// --backend noop: control plane only; for tests.

#include "agent.hpp"
#include "backend.hpp"
#include "bpf_backend.hpp"
#include "bpf_capability.hpp"
#include "control_server.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unistd.h>

namespace {

rawaccel_agent::ControlServer* g_server = nullptr;

void on_signal(int)
{
    if (g_server) g_server->stop();
}

void usage()
{
    std::fprintf(stderr,
        "usage: rawaccel-agentd [--socket PATH] [--settings PATH] "
        "[--backend {auto,bpf,noop}] [--bpf-object PATH]\n");
}

// Beside the executable; installs override via --bpf-object.
std::string default_bpf_object_path(const char* argv0)
{
    std::string p = argv0 ? argv0 : "";
    auto slash = p.find_last_of('/');
    std::string dir = (slash == std::string::npos) ? "." : p.substr(0, slash);
    return dir + "/rawaccel.bpf.o";
}

} // namespace

int main(int argc, char** argv)
{
    std::string socket_path = "/run/rawaccel/control.sock";
    std::string settings_path;
    std::string backend_name = "auto";
    std::string bpf_object_path = default_bpf_object_path(argv[0]);

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (a == "--settings" && i + 1 < argc) {
            settings_path = argv[++i];
        } else if (a == "--backend" && i + 1 < argc) {
            backend_name = argv[++i];
        } else if (a == "--bpf-object" && i + 1 < argc) {
            bpf_object_path = argv[++i];
        } else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    std::string resolved = backend_name;
    if (resolved == "auto") {
        auto probe = rawaccel_agent::probe_bpf_capability();
        if (!probe.ok()) {
            std::fprintf(stderr,
                "rawaccel: HID-BPF not available (%s). "
                "rawaccel-agentd requires kernel >= 6.11 with CAP_BPF.\n",
                probe.reason.c_str());
            return 1;
        }
        resolved = "bpf";
        std::fprintf(stderr,
            "rawaccel: bpf backend (kernel %d.%d)\n",
            probe.kernel_major, probe.kernel_minor);
    }

    std::unique_ptr<rawaccel_agent::Backend> backend;
    rawaccel_agent::BpfBackend* bpf_ptr = nullptr;

    if (resolved == "bpf") {
        auto bb = std::make_unique<rawaccel_agent::BpfBackend>(bpf_object_path);
        bpf_ptr = bb.get();
        backend = std::move(bb);
    } else if (resolved == "noop") {
        backend = std::make_unique<rawaccel_agent::NoopBackend>();
    } else {
        std::fprintf(stderr, "unknown backend: %s\n", resolved.c_str());
        usage();
        return 2;
    }

    rawaccel_agent::Agent agent(*backend);

    if (bpf_ptr) {
        bpf_ptr->set_listener(agent);
    }

    if (!settings_path.empty()) {
        if (!agent.load_from_file(settings_path)) {
            std::fprintf(stderr,
                "warning: could not load settings from '%s'; using defaults\n",
                settings_path.c_str());
        }
    }

    // start() re-enters via on_device_added, so binds see the config loaded above
    if (bpf_ptr) {
        if (!bpf_ptr->start()) {
            std::fprintf(stderr, "bpf backend failed to start\n");
            return 1;
        }
    }

    rawaccel_agent::ControlServer server(agent, socket_path);
    g_server = &server;
    if (!server.listen()) {
        std::perror("listen");
        return 1;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    // a mid-response client disconnect must not kill the daemon
    std::signal(SIGPIPE, SIG_IGN);

    server.run();

    if (bpf_ptr) bpf_ptr->stop();
    return 0;
}
