// Replay an input trace through an EvdevProcessor configured from a
// settings.json file. Output goes to stdout in the same CSV format. The
// intended workflow:
//
//   1. On Windows, capture a real mouse trace (input_tick_us, dx, dy) and
//      record the post-acceleration output of the wrapper-tests ManagedAccel
//      pipeline as the expected output.
//   2. On Linux, run:
//        rawaccel-replay --settings my.json --trace input.csv > linux.csv
//   3. diff windows-expected.csv linux.csv  (or numeric near-equality).
//
// The replay does NOT consume real hardware: it runs the same pure math
// the driver/wrapper would, with no sockets or I/O on /dev/input.

#include "evdev_processor.hpp"
#include "json_io.hpp"
#include "trace_format.hpp"
#include "trace_replay.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace ra = rawaccel;
using namespace rawaccel_agent;

namespace {

void usage()
{
    std::fprintf(stderr,
        "usage: rawaccel-replay --settings PATH --trace PATH "
        "[--profile NAME]\n");
}

ra::modifier_settings pick_profile(const rajson::driver_config& cfg,
                                   const std::string& name)
{
    if (cfg.profiles.empty()) {
        return ra::modifier_settings{};
    }
    if (name.empty()) {
        return cfg.profiles.front();
    }
    for (const auto& p : cfg.profiles) {
        // wchar_t name compare against UTF-8 query is approximate: convert
        // both to UTF-8 via rajson's codec and string-compare.
        std::string p8 = rajson::wchar_to_utf8(p.prof.name, ra::MAX_NAME_LEN);
        if (p8 == name) return p;
    }
    return cfg.profiles.front();
}

} // namespace

int main(int argc, char** argv)
{
    std::string settings_path, trace_path, profile_name;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--settings" && i + 1 < argc) settings_path = argv[++i];
        else if (a == "--trace" && i + 1 < argc) trace_path = argv[++i];
        else if (a == "--profile" && i + 1 < argc) profile_name = argv[++i];
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { usage(); return 2; }
    }
    if (settings_path.empty() || trace_path.empty()) { usage(); return 2; }

    std::ifstream sf(settings_path);
    if (!sf) { std::perror(settings_path.c_str()); return 1; }
    std::ostringstream sb; sb << sf.rdbuf();
    rajson::driver_config cfg;
    try {
        cfg = rajson::from_string(sb.str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "settings parse error: %s\n", e.what());
        return 1;
    }
    auto ms = pick_profile(cfg, profile_name);
    ra::init_data(ms);

    std::ifstream tf(trace_path);
    if (!tf) { std::perror(trace_path.c_str()); return 1; }
    std::vector<TraceRecord> in;
    if (!read_trace(tf, in)) {
        std::fprintf(stderr, "malformed trace at %s\n", trace_path.c_str());
        return 1;
    }

    EvdevProcessor p;
    p.set_settings(ms);

    auto out = replay_trace(p, in);
    std::ostringstream banner;
    banner << "rawaccel-replay output\n"
           << "settings: " << settings_path << "\n"
           << "trace:    " << trace_path << "\n"
           << "records:  " << out.size() << "\n";
    write_trace(std::cout, out, banner.str());
    return 0;
}
