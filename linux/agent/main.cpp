// rawaccel-agentd: long-running daemon that owns the active rawaccel modifier
// state, accepts apply/get/version/status RPCs over an AF_UNIX control socket,
// and forwards settings changes to the active backend.
//
// Backend choices:
//   --backend auto  : probe kernel + bpf(); pick bpf if supported, else evdev.
//   --backend bpf   : force HID-BPF (rawaccel.bpf.o, kernel >= 6.11, CAP_BPF).
//   --backend evdev : force the evdev/uinput userspace fallback.
//   --backend noop  : no transport; control-plane only. Tests use this.

#include "agent.hpp"
#include "backend.hpp"
#include "bpf_backend.hpp"
#include "bpf_capability.hpp"
#include "control_server.hpp"
#include "evdev_backend.hpp"

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
    // Async-signal-safe path: ioctl EVIOCGRAB(0) on every grabbed device so a
    // hard exit cannot leave the user's mouse stuck. Then nudge the control
    // server to leave its poll loop; full thread joins / uinput destroy run
    // on the main thread after the loop exits.
    rawaccel_agent::grab_registry_panic_ungrab_all();
    if (g_server) g_server->stop();
}

void usage()
{
    std::fprintf(stderr,
        "usage: rawaccel-agentd [--socket PATH] [--settings PATH] "
        "[--backend {auto,bpf,evdev,noop}] [--bpf-object PATH]\n");
}

std::string default_bpf_object_path(const char* argv0)
{
    // Look beside the executable. Production installs override with
    // --bpf-object pointing at /usr/share/rawaccel/rawaccel.bpf.o.
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

    // Resolve auto: probe BPF capability and announce the choice.
    std::string resolved = backend_name;
    if (resolved == "auto") {
        auto probe = rawaccel_agent::probe_bpf_capability();
        if (probe.ok()) {
            resolved = "bpf";
            std::fprintf(stderr,
                "rawaccel: auto-selected bpf backend (kernel %d.%d)\n",
                probe.kernel_major, probe.kernel_minor);
        } else {
            resolved = "evdev";
            std::fprintf(stderr,
                "rawaccel: auto-selected evdev backend (%s)\n",
                probe.reason.c_str());
        }
    }

    std::unique_ptr<rawaccel_agent::Backend> backend;
    rawaccel_agent::EvdevBackend* evdev_ptr = nullptr;
    rawaccel_agent::BpfBackend* bpf_ptr = nullptr;

    if (resolved == "evdev") {
        auto eb = std::make_unique<rawaccel_agent::EvdevBackend>();
        evdev_ptr = eb.get();
        backend = std::move(eb);
    } else if (resolved == "bpf") {
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

    if (!settings_path.empty()) {
        if (!agent.load_from_file(settings_path)) {
            std::fprintf(stderr,
                "warning: could not load settings from '%s'; using defaults\n",
                settings_path.c_str());
        }
    }

    if (evdev_ptr) {
        if (!evdev_ptr->start()) {
            std::fprintf(stderr, "evdev backend failed to start\n");
            return 1;
        }
    }
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

    server.run();

    if (evdev_ptr) evdev_ptr->stop();
    if (bpf_ptr) bpf_ptr->stop();
    return 0;
}
