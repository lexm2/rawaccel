// Measure input-to-uinput latency for one device under the running
// rawaccel-agentd. Opens the source evdev node and the corresponding
// uinput-mirrored output node, pairs SYN_REPORTs by sequence, and prints
// p50/p95/p99 deltas in microseconds.
//
// Real hardware required: feed the source mouse for ~30 seconds while this
// tool runs. Repeat under `stress -c $(nproc)` to characterize tail.
//
// Out of scope: cross-device pairing, multi-source aggregation, GUI. This
// is a Step 8 calibration aid, not a long-running monitor.

#include "evdev_io.hpp"

#include <linux/input.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace rawaccel_agent;

namespace {

double percentile(std::vector<double>& v, double q)
{
    if (v.empty()) return 0.0;
    std::size_t i = static_cast<std::size_t>(q * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + i, v.end());
    return v[i];
}

void usage()
{
    std::fprintf(stderr,
        "usage: rawaccel-latency --source /dev/input/eventN "
        "--sink /dev/input/eventM [--duration SEC]\n");
}

std::int64_t mono_us()
{
    auto t = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(t).count();
}

} // namespace

int main(int argc, char** argv)
{
    std::string src_path, sink_path;
    int duration_sec = 30;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--source" && i + 1 < argc) src_path = argv[++i];
        else if (a == "--sink" && i + 1 < argc) sink_path = argv[++i];
        else if (a == "--duration" && i + 1 < argc) duration_sec = std::atoi(argv[++i]);
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { usage(); return 2; }
    }
    if (src_path.empty() || sink_path.empty()) { usage(); return 2; }

    int sfd = evdev_open(src_path);
    if (sfd < 0) { std::perror(src_path.c_str()); return 1; }
    int kfd = evdev_open(sink_path);
    if (kfd < 0) { std::perror(sink_path.c_str()); return 1; }

    // Pair the nth SYN_REPORT on the source with the nth SYN_REPORT on the
    // sink. The agent emits exactly one SYN_REPORT per input SYN_REPORT, so
    // sequence-pairing is well-defined.
    std::vector<std::int64_t> src_syn_us;
    std::vector<std::int64_t> sink_syn_us;
    std::vector<double> deltas_us;

    std::int64_t deadline = mono_us() + std::int64_t(duration_sec) * 1'000'000;

    while (mono_us() < deadline) {
        pollfd pfds[2] = {{sfd, POLLIN, 0}, {kfd, POLLIN, 0}};
        int r = ::poll(pfds, 2, 100);
        if (r < 0) break;
        for (int i = 0; i < 2; ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;
            int fd = pfds[i].fd;
            input_event ev;
            if (!read_event(fd, ev)) continue;
            if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                std::int64_t t = mono_us();
                if (fd == sfd) src_syn_us.push_back(t);
                else           sink_syn_us.push_back(t);
            }
        }
        // Drain pairs as they become available.
        std::size_t n = std::min(src_syn_us.size(), sink_syn_us.size());
        while (n > deltas_us.size()) {
            std::size_t i = deltas_us.size();
            deltas_us.push_back(double(sink_syn_us[i] - src_syn_us[i]));
            ++i;
            (void)i;
        }
    }

    if (deltas_us.empty()) {
        std::fprintf(stderr, "no SYN pairs observed; check device paths\n");
        return 1;
    }

    auto p50 = percentile(deltas_us, 0.50);
    auto p95 = percentile(deltas_us, 0.95);
    auto p99 = percentile(deltas_us, 0.99);
    std::printf("samples: %zu\n", deltas_us.size());
    std::printf("p50: %.1f us\n", p50);
    std::printf("p95: %.1f us\n", p95);
    std::printf("p99: %.1f us\n", p99);
    return 0;
}
