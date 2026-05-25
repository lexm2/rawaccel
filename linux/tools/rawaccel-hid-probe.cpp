// Probe every /sys/class/hidraw node, report BPF-backend acceptance. Run on a
// target host before enabling the daemon.

#include "hid_descriptor.hpp"

#include <dirent.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace rawaccel_agent;

namespace {

// 8 KiB cap; kernel bounds sysfs report_descriptor to 4096
// (HID_MAX_DESCRIPTOR_SIZE).
bool read_descriptor(const std::string& path, std::vector<std::uint8_t>& out)
{
    constexpr std::size_t MAX = 8192;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.clear();
    char buf[1024];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        if (out.size() + static_cast<std::size_t>(f.gcount()) > MAX) return false;
        out.insert(out.end(), buf, buf + f.gcount());
    }
    return !out.empty();
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
        std::string desc_path =
            "/sys/class/hidraw/" + name + "/device/report_descriptor";

        std::vector<std::uint8_t> desc;
        if (!read_descriptor(desc_path, desc)) {
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
