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
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

namespace rawaccel_agent {

namespace {

// Cap reads; kernel caps report_descriptor at 4096 but don't trust that here.
bool read_descriptor(const std::string& syspath, std::vector<std::uint8_t>& out)
{
    constexpr std::size_t MAX = 8192;
    std::ifstream f(syspath + "/device/report_descriptor", std::ios::binary);
    if (!f) return false;
    out.clear();
    char buf[1024];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
        if (out.size() + static_cast<std::size_t>(f.gcount()) > MAX) return false;
        out.insert(out.end(), buf, buf + f.gcount());
    }
    return !out.empty();
}

// Reads /sys/class/hidraw/hidrawN/device -> "0003:VVVV:PPPP.IIII".
std::string resolve_device_sysname(const std::string& syspath)
{
    char buf[PATH_MAX] = {};
    ssize_t n = ::readlink((syspath + "/device").c_str(), buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = 0;
    const char* slash = std::strrchr(buf, '/');
    return slash ? std::string(slash + 1) : std::string(buf);
}

DeviceId hash_id(const std::string& key)
{
    constexpr DeviceId OFFSET = 1469598103934665603ull;
    constexpr DeviceId PRIME  = 1099511628211ull;
    DeviceId h = OFFSET;
    for (unsigned char c : key) { h ^= c; h *= PRIME; }
    return h;
}

// "0003:046D:C54D.000A" -> 0x046D / 0xC54D.
bool parse_vid_pid(const std::string& device_sysname,
                   std::uint32_t& vid, std::uint32_t& pid)
{
    auto first  = device_sysname.find(':');
    if (first == std::string::npos) return false;
    auto second = device_sysname.find(':', first + 1);
    if (second == std::string::npos) return false;
    auto dot    = device_sysname.find('.', second + 1);
    if (dot == std::string::npos) return false;

    auto from_hex = [](const std::string& s, std::uint32_t& out) {
        char* end = nullptr;
        unsigned long v = std::strtoul(s.c_str(), &end, 16);
        if (end == s.c_str() || *end != 0) return false;
        out = static_cast<std::uint32_t>(v);
        return true;
    };
    return from_hex(device_sysname.substr(first + 1, second - first - 1), vid)
        && from_hex(device_sysname.substr(second + 1, dot - second - 1), pid);
}

} // namespace

// "0003:046D:C54D.000A" -> trailing ".HHHHHHHH" is hid_id in hex.
bool parse_hid_device_name(const std::string& name, std::uint32_t& hid_id_out)
{
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

HidrawIdentity read_hidraw_identity(const std::string& syspath)
{
    HidrawIdentity out;

    std::string dev_sysname = resolve_device_sysname(syspath);
    parse_vid_pid(dev_sysname, out.vendor_id, out.product_id);

    std::ifstream f(syspath + "/device/uevent");
    std::string line;
    while (std::getline(f, line)) {
        constexpr const char* prefix = "HID_NAME=";
        if (line.rfind(prefix, 0) == 0) {
            out.name = line.substr(std::strlen(prefix));
            break;
        }
    }
    return out;
}

BpfBackend::BpfBackend(std::string object_path)
    : object_path_(std::move(object_path)) {}

BpfBackend::~BpfBackend()
{
    stop();
}

void BpfBackend::set_listener(DeviceListener& listener)
{
    listener_ = &listener;
}

bool BpfBackend::start()
{
    // Fail-open: zero matches still succeeds (hotplug later); rejects pass through.
    bool any = false;
    for (const auto& node : enumerate_hidraw()) {
        if (attach_node(node.sysname)) any = true;
    }
    if (!any) {
        std::fprintf(stderr,
            "bpf backend: no mouse passed validate_for_bpf at start; "
            "use rawaccel-hid-probe to inspect attached devices\n");
    }
    return true;
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

    // hid_id (offset 0 of hid_bpf_ops) must be set before load(), else
    // attach_struct_ops gives EINVAL. set_initial_value() refuses a partial
    // write, so patch the field directly in the map's mutable initial value.
    bpf_map* ops = bpf_object__find_map_by_name(obj, "rawaccel_ops");
    if (!ops) {
        bpf_object__close(obj);
        return false;
    }
    {
        std::size_t ops_val_size = 0;
        void* ops_val = bpf_map__initial_value(ops, &ops_val_size);
        if (!ops_val || ops_val_size < sizeof(hid_id)) {
            std::fprintf(stderr,
                "bpf backend: cannot patch hid_id on rawaccel_ops (%s)\n",
                sysname.c_str());
            bpf_object__close(obj);
            return false;
        }
        std::memcpy(ops_val, &hid_id, sizeof(hid_id));
    }

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
    slot->ops_map = ops;
    slot->config_map = bpf_object__find_map_by_name(obj, "ra_config");
    slot->lut_x_map = bpf_object__find_map_by_name(obj, "ra_lut_x");
    slot->lut_y_map = bpf_object__find_map_by_name(obj, "ra_lut_y");
    slot->state_map = bpf_object__find_map_by_name(obj, "ra_state");
    if (!slot->config_map || !slot->lut_x_map || !slot->lut_y_map ||
        !slot->state_map) {
        bpf_object__close(obj);
        return false;
    }

    std::fprintf(stderr,
        "bpf backend: prepared %s (hid_id=0x%x) report_id=%u "
        "dx=byte%u/%uB dy=byte%u/%uB\n",
        sysname.c_str(), unsigned(hid_id),
        unsigned(slot->layout.report_id),
        unsigned(slot->layout.dx_byte_offset),
        unsigned(slot->layout.dx_byte_size),
        unsigned(slot->layout.dy_byte_offset),
        unsigned(slot->layout.dy_byte_size));

    // Eager attach with an identity config: device is live before any apply,
    // and attach failures surface here.
    if (!populate_maps(*slot, ra::modifier_settings{}, ra::device_config{})) {
        std::fprintf(stderr, "bpf backend: identity populate failed for %s\n",
                     sysname.c_str());
        bpf_object__close(obj);
        return false;
    }
    slot->link = bpf_map__attach_struct_ops(slot->ops_map);
    if (slot->link) {
        slot->attached = true;
        std::fprintf(stderr, "bpf backend: attached %s (hid_id=0x%x)\n",
                     sysname.c_str(), unsigned(hid_id));
    } else {
        slot->attached = false;
        slot->attach_error = std::string(std::strerror(errno)) +
                             " (errno " + std::to_string(errno) + ")";
        std::fprintf(stderr,
            "bpf backend: attach_struct_ops(%s) errno=%d (%s)\n",
            sysname.c_str(), errno, std::strerror(errno));
        // keep the unattached slot so health()/apply can report the failure
    }

    DeviceInfo info;
    info.id = slot->id;
    info.sysname = sysname;
    info.device_sysname = dev_sysname;
    auto ident = read_hidraw_identity(syspath);
    info.vendor_id = ident.vendor_id;
    info.product_id = ident.product_id;
    info.name = std::move(ident.name);

    {
        std::lock_guard<std::mutex> lock(mu_);
        slots_.emplace(slot->id, std::move(slot));
    }

    // notify outside the lock: on_device_added re-enters bind_device (takes mu_)
    if (listener_) listener_->on_device_added(info);
    return true;
}

bool BpfBackend::populate_maps(Slot& slot,
                               const ra::modifier_settings& s,
                               const ra::device_config& c)
{
    // build_lut throws on an unported feature; refuse the bind.
    LutBuildResult lut;
    try {
        lut = build_lut(s, c);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bpf backend: %s (%s left unaccelerated)\n",
                     e.what(), slot.sysname.c_str());
        return false;
    }

    // raw curve -> lut_x/lut_y; weighting, output-DPI, HID layout -> config
    ra_bpf_config cfg = to_bpf_config(lut, slot.layout);

    // Cache what current_speed_sample needs to convert the kernel's telemetry.
    slot.domain_w_x_q16 = cfg.domain_w_x_q16;
    slot.domain_w_y_q16 = cfg.domain_w_y_q16;

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
    slot.attached = false;
}

void BpfBackend::bind_device(DeviceId id,
                             const ra::modifier_settings& s,
                             const ra::device_config& c)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = slots_.find(id);
    if (it == slots_.end()) return;

    Slot& slot = *it->second;
    // link is attached in attach_node; bind just refreshes the maps. If never
    // attached, settings land in the maps but stay dormant.
    if (!populate_maps(slot, s, c)) {
        std::fprintf(stderr,
            "bpf backend: populate_maps failed for %s\n",
            slot.sysname.c_str());
    }
}

void BpfBackend::unbind_device(DeviceId id)
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = slots_.find(id);
    if (it == slots_.end()) return;
    detach_slot(*it->second);
    slots_.erase(it);
}

std::size_t BpfBackend::attached_count() const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::size_t n = 0;
    for (const auto& [id, slot] : slots_) {
        if (slot->attached) ++n;
    }
    return n;
}

DataPlaneHealth BpfBackend::health() const
{
    std::lock_guard<std::mutex> lock(mu_);
    DataPlaneHealth h;
    h.devices = slots_.size();
    for (const auto& [id, slot] : slots_) {
        if (slot->attached) {
            ++h.attached;
        } else if (h.error.empty()) {
            h.error = slot->sysname + ": attach failed" +
                      (slot->attach_error.empty() ? "" : ": " + slot->attach_error);
        }
    }
    return h;
}

SpeedSample BpfBackend::current_speed_sample() const
{
    // No packet within this window -> the mouse is effectively stopped, so the
    // GUI's indicator lines fade out. Larger than any inter-packet gap during
    // continuous motion (>=125 Hz), small enough to feel responsive.
    constexpr std::uint64_t STALE_NS = 150ull * 1000 * 1000;  // 150 ms

    std::lock_guard<std::mutex> lock(mu_);

    // Most-recently-active device wins: a multi-mouse setup reports the one the
    // user is actually moving.
    const Slot* best = nullptr;
    ra_bpf_state best_state{};
    std::uint64_t best_ts = 0;

    for (const auto& [id, slot] : slots_) {
        if (!slot->attached || !slot->state_map) continue;
        int fd = bpf_map__fd(slot->state_map);
        if (fd < 0) continue;
        std::uint32_t zero = 0;
        ra_bpf_state st{};
        if (bpf_map_lookup_elem(fd, &zero, &st) != 0) continue;
        if (st.last_ts_ns > best_ts) {
            best_ts = st.last_ts_ns;
            best_state = st;
            best = slot.get();
        }
    }

    if (!best || best_ts == 0) return {};

    // last_ts_ns is bpf_ktime_get_ns() == CLOCK_MONOTONIC, so compare against it.
    struct timespec now_ts{};
    clock_gettime(CLOCK_MONOTONIC, &now_ts);
    std::uint64_t now = static_cast<std::uint64_t>(now_ts.tv_sec) * 1000000000ull +
                        static_cast<std::uint64_t>(now_ts.tv_nsec);
    if (now > best_ts && now - best_ts > STALE_NS) return {};

    // Kernel telemetry is Q16.16 in/s, domain-weighted. Chart X is normalized
    // in/s, so divide out the domain weight: chart_x = awv_q16 / domain_w_q16.
    const double dw_x = best->domain_w_x_q16 ? static_cast<double>(best->domain_w_x_q16)
                                             : static_cast<double>(RA_Q16_ONE);
    const double dw_y = best->domain_w_y_q16 ? static_cast<double>(best->domain_w_y_q16)
                                             : static_cast<double>(RA_Q16_ONE);

    // Always report the genuinely-distinct per-axis speeds (the BPF writes
    // awv_x/awv_y separately in both whole and separate mode) plus the combined
    // aggregate. The GUI decides which to show: x/y for the two per-axis lines,
    // combined for the single line. Never force x == y - that coupled the two
    // lines whenever the device ran in whole mode (the default).
    SpeedSample out;
    out.x = static_cast<double>(best_state.tele_speed_x_q16) / dw_x;
    out.y = static_cast<double>(best_state.tele_speed_y_q16) / dw_y;
    out.combined = static_cast<double>(best_state.tele_speed_combined_q16) / dw_x;
    return out;
}

} // namespace rawaccel_agent
