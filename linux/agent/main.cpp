// rawaccel-agentd: long-running daemon that owns the active rawaccel modifier
// state, accepts apply/get/version/status RPCs over an AF_UNIX control socket,
// and forwards settings changes to the active backend.
//
// Step 5 wires up just the control plane against a NoopBackend; the evdev and
// HID-BPF backends arrive in later steps.

#include "agent.hpp"
#include "backend.hpp"
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
        "[--backend {noop,evdev}]\n");
}

} // namespace

int main(int argc, char** argv)
{
    std::string socket_path = "/run/rawaccel/control.sock";
    std::string settings_path;
    std::string backend_name = "noop";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (a == "--settings" && i + 1 < argc) {
            settings_path = argv[++i];
        } else if (a == "--backend" && i + 1 < argc) {
            backend_name = argv[++i];
        } else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    std::unique_ptr<rawaccel_agent::Backend> backend;
    rawaccel_agent::EvdevBackend* evdev_ptr = nullptr;
    if (backend_name == "evdev") {
        auto eb = std::make_unique<rawaccel_agent::EvdevBackend>();
        evdev_ptr = eb.get();
        backend = std::move(eb);
    } else {
        backend = std::make_unique<rawaccel_agent::NoopBackend>();
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
    return 0;
}
