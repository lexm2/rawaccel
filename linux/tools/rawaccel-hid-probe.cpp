// Probe every /sys/class/hidraw/hidrawN device on the system, parse its
// report descriptor, and report whether the BPF backend would accept it.
//
// Diagnostic tool for Step 9 / Step 10 onboarding: run on a target machine
// and confirm the parser handles every connected mouse before flipping the
// BPF backend on.

#include "hid_descriptor.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace rawaccel_agent;

namespace {

bool read_file(const std::string& path, std::vector<std::uint8_t>& out)
{
    // sysfs files report size 4096 but seekg/tellg is unreliable on them.
    // Stream-read until EOF instead.
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.clear();
    char buf[1024];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        out.insert(out.end(), buf, buf + f.gcount());
    }
    return !out.empty();
}

std::string read_text(const std::string& path)
{
    std::ifstream f(path);
    std::string out;
    if (f) std::getline(f, out);
    return out;
}

} // namespace

int main()
{
    DIR* d = ::opendir("/sys/class/hidraw");
    if (!d) {
        std::perror("/sys/class/hidraw");
        return 1;
    }
    while (auto* e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string syspath = "/sys/class/hidraw/" + name;
        std::string desc_path = syspath + "/device/report_descriptor";
        std::string product_path = syspath + "/device/uevent";

        std::vector<std::uint8_t> desc;
        if (!read_file(desc_path, desc)) {
            std::printf("%s : (cannot read descriptor)\n", name.c_str());
            continue;
        }
        auto md = parse_mouse_descriptor(desc.data(), desc.size());
        if (!md) {
            std::printf("%s : not a mouse (%zu byte descriptor)\n",
                        name.c_str(), desc.size());
            continue;
        }
        auto dec = validate_for_bpf(*md);
        if (dec.reject) {
            std::printf("%s : mouse, BPF=REJECT (%s)\n",
                        name.c_str(), dec.reject->reason.c_str());
            continue;
        }
        const auto& l = *dec.layout;
        std::printf("%s : mouse, BPF=OK  report_id=%u dx=byte%u/%uB dy=byte%u/%uB\n",
                    name.c_str(),
                    unsigned(l.report_id),
                    unsigned(l.dx_byte_offset), unsigned(l.dx_byte_size),
                    unsigned(l.dy_byte_offset), unsigned(l.dy_byte_size));
    }
    ::closedir(d);
    return 0;
}
