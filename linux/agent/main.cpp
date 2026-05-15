// rawaccel-agentd: long-running daemon that owns the active rawaccel modifier
// state, accepts apply/get/version/status RPCs over an AF_UNIX control socket,
// and forwards settings changes to the active backend.
//
// Step 5 wires up just the control plane against a NoopBackend; the evdev and
// HID-BPF backends arrive in later steps.

#include "agent.hpp"
#include "backend.hpp"
#include "control_server.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
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
        "usage: rawaccel-agentd [--socket PATH] [--settings PATH]\n");
}

} // namespace

int main(int argc, char** argv)
{
    std::string socket_path = "/run/rawaccel/control.sock";
    std::string settings_path;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--socket" && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (a == "--settings" && i + 1 < argc) {
            settings_path = argv[++i];
        } else if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    rawaccel_agent::NoopBackend backend;
    rawaccel_agent::Agent agent(backend);

    if (!settings_path.empty()) {
        if (!agent.load_from_file(settings_path)) {
            std::fprintf(stderr,
                "warning: could not load settings from '%s'; using defaults\n",
                settings_path.c_str());
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
    return 0;
}
