#include "bpf_backend.hpp"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace rawaccel_agent {

namespace {

bool read_descriptor(const std::string& syspath, std::vector<std::uint8_t>& out)
{
    // sysfs files do not seek; stream-read until EOF.
    std::ifstream f(syspath + "/device/report_descriptor", std::ios::binary);
    if (!f) return false;
    out.clear();
    char buf[1024];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        out.insert(out.end(), buf, buf + f.gcount());
    }
    return !out.empty();
}

std::string resolve_device_sysname(const std::string& syspath)
{
    // /sys/class/hidraw/hidrawN/device is a symlink; the last path component
    // of the target is "0003:046D:C54D.000A".
    char buf[PATH_MAX] = {};
    ssize_t n = ::readlink((syspath + "/device").c_str(), buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = 0;
    const char* slash = std::strrchr(buf, '/');
    return slash ? std::string(slash + 1) : std::string(buf);
}

std::uint64_t hash_id(const std::string& key)
{
    constexpr std::uint64_t OFFSET = 1469598103934665603ull;
    constexpr std::uint64_t PRIME  = 1099511628211ull;
    std::uint64_t h = OFFSET;
    for (unsigned char c : key) { h ^= c; h *= PRIME; }
    return h;
}

template <typename F>
int find_map_and_update(bpf_object* obj, const char* name, F&& fill)
{
    bpf_map* m = bpf_object__find_map_by_name(obj, name);
    if (!m) return -ENOENT;
    return fill(m);
}

} // namespace

bool parse_hid_device_name(const std::string& name, std::uint32_t& hid_id_out)
{
    // "0003:046D:C54D.000A" -> the trailing ".HHHHHHHH" is hid_id in hex.
    auto pos = name.find_last_of('.');
    if (pos == std::string::npos) return false;
    const char* tail = name.c_str() + pos + 1;
    char* end = nullptr;
    unsigned long v = std::strtoul(tail, &end, 16);
    if (end == tail || *end != 0) return false;
    hid_id_out = static_cast<std::uint32_t>(v);
    return true;
}

std::vector<HidrawNode> enumerate_hidraw()
{
    std::vector<HidrawNode> out;
    DIR* d = ::opendir("/sys/class/hidraw");
    if (!d) return out;
    while (auto* e = ::readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        HidrawNode n;
        n.sysname = name;
        std::string sp = "/sys/class/hidraw/" + name;
        n.device_sysname = resolve_device_sysname(sp);
        if (parse_hid_device_name(n.device_sysname, n.hid_id)) {
            out.push_back(std::move(n));
        }
    }
    ::closedir(d);
    return out;
}

BpfBackend::BpfBackend(std::string object_path)
    : object_path_(std::move(object_path)) {}

BpfBackend::~BpfBackend()
{
    stop();
}

bool BpfBackend::start()
{
    bool any = false;
    for (const auto& node : enumerate_hidraw()) {
        if (attach_node(node.sysname)) any = true;
    }
    if (!any) {
        std::fprintf(stderr,
            "bpf backend: no mouse passed validate_for_bpf at start; "
            "use rawaccel-hid-probe to inspect attached devices\n");
    }
    return true;  // partial start is success: fail-open passthrough.
}

void BpfBackend::stop()
{
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [id, slot] : slots_) {
        detach_slot(*slot);
    }
    slots_.clear();
}

bool BpfBackend::attach_node(const std::string& sysname)
{
    const std::string syspath = "/sys/class/hidraw/" + sysname;
    std::vector<std::uint8_t> desc;
    if (!read_descriptor(syspath, desc)) return false;

    auto md = parse_mouse_descriptor(desc.data(), desc.size());
    if (!md) return false;
    auto dec = validate_for_bpf(*md);
    if (!dec.layout) {
        std::fprintf(stderr,
            "bpf backend: skipping %s: %s\n",
            sysname.c_str(),
            dec.reject ? dec.reject->reason.c_str() : "unknown");
        return false;
    }

    std::string dev_sysname = resolve_device_sysname(syspath);
    std::uint32_t hid_id = 0;
    if (!parse_hid_device_name(dev_sysname, hid_id)) return false;

    bpf_object* obj = bpf_object__open_file(object_path_.c_str(), nullptr);
    if (!obj) {
        std::fprintf(stderr, "bpf backend: open_file(%s) errno=%d\n",
                     object_path_.c_str(), errno);
        return false;
    }

    // Set hid_id on the struct_ops map BEFORE load. The userspace .data
    // image of the struct is bpf_map_lookup_elem-able as map index 0.
    bpf_map* ops = bpf_object__find_map_by_name(obj, "rawaccel_ops");
    if (!ops) {
        bpf_object__close(obj);
        return false;
    }
    // The first 4 bytes of the rawaccel_ops value are the hid_id field
    // (see hid_bpf_ops layout in vmlinux.h).
    bpf_map__set_initial_value(ops, &hid_id, sizeof(hid_id));

    if (bpf_object__load(obj) != 0) {
        const int saved = errno;
        std::fprintf(stderr,
            "bpf backend: load(%s) errno=%d (%s)\n",
            sysname.c_str(), saved, std::strerror(saved));
        bpf_object__close(obj);
        return false;
    }

    auto slot = std::make_unique<Slot>();
    slot->id = hash_id(dev_sysname);
    slot->sysname = sysname;
    slot->hid_id = hid_id;
    slot->layout = *dec.layout;
    slot->obj = obj;
    slot->config_map = bpf_object__find_map_by_name(obj, "ra_config");
    slot->lut_x_map = bpf_object__find_map_by_name(obj, "ra_lut_x");
    slot->lut_y_map = bpf_object__find_map_by_name(obj, "ra_lut_y");
    if (!slot->config_map || !slot->lut_x_map || !slot->lut_y_map) {
        bpf_object__close(obj);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mu_);
        populate_maps(*slot, current_settings_);
    }

    slot->link = bpf_map__attach_struct_ops(ops);
    if (!slot->link) {
        std::fprintf(stderr,
            "bpf backend: attach_struct_ops(%s) errno=%d\n",
            sysname.c_str(), errno);
        bpf_object__close(obj);
        return false;
    }

    std::fprintf(stderr,
        "bpf backend: attached %s (hid_id=0x%x) report_id=%u "
        "dx=byte%u/%uB dy=byte%u/%uB\n",
        sysname.c_str(), unsigned(hid_id),
        unsigned(slot->layout.report_id),
        unsigned(slot->layout.dx_byte_offset),
        unsigned(slot->layout.dx_byte_size),
        unsigned(slot->layout.dy_byte_offset),
        unsigned(slot->layout.dy_byte_size));

    std::lock_guard<std::mutex> lock(mu_);
    slots_.emplace(slot->id, std::move(slot));
    return true;
}

bool BpfBackend::populate_maps(Slot& slot, const ra::modifier_settings& s)
{
    auto lut = build_lut(s, slot.dev_config);

    ra_bpf_config cfg{};
    cfg.report_id      = slot.layout.report_id;
    cfg.dx_byte_offset = slot.layout.dx_byte_offset;
    cfg.dx_byte_size   = slot.layout.dx_byte_size;
    cfg.dy_byte_offset = slot.layout.dy_byte_offset;
    cfg.dy_byte_size   = slot.layout.dy_byte_size;
    cfg.dpi_norm_q16   = lut.dpi_norm_q16;
    cfg.smooth_alpha_q16 = lut.smooth_alpha_q16;
    cfg.lut_step_q16   = lut.lut_step_q16;
    cfg.lut_max_q16    = lut.lut_max_q16;

    int cfg_fd = bpf_map__fd(slot.config_map);
    int lutx_fd = bpf_map__fd(slot.lut_x_map);
    int luty_fd = bpf_map__fd(slot.lut_y_map);
    if (cfg_fd < 0 || lutx_fd < 0 || luty_fd < 0) return false;

    std::uint32_t zero = 0;
    if (bpf_map_update_elem(cfg_fd, &zero, &cfg, BPF_ANY) != 0) {
        std::fprintf(stderr, "bpf backend: write config errno=%d\n", errno);
        return false;
    }
    for (std::uint32_t i = 0; i < RA_LUT_SIZE; ++i) {
        if (bpf_map_update_elem(lutx_fd, &i, &lut.lut_x[i], BPF_ANY) != 0) return false;
        if (bpf_map_update_elem(luty_fd, &i, &lut.lut_y[i], BPF_ANY) != 0) return false;
    }
    return true;
}

void BpfBackend::detach_slot(Slot& slot)
{
    if (slot.link) {
        bpf_link__destroy(slot.link);
        slot.link = nullptr;
    }
    if (slot.obj) {
        bpf_object__close(slot.obj);
        slot.obj = nullptr;
    }
}

void BpfBackend::on_settings_changed(const ra::modifier_settings& s)
{
    std::lock_guard<std::mutex> lock(mu_);
    current_settings_ = s;
    for (auto& [id, slot] : slots_) {
        populate_maps(*slot, s);
    }
}

std::size_t BpfBackend::attached_count() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return slots_.size();
}

} // namespace rawaccel_agent
