#include "bpf_backend.hpp"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace rawaccel_agent {

BpfBackend::BpfBackend(std::string object_path)
    : object_path_(std::move(object_path)) {}

BpfBackend::~BpfBackend()
{
    stop();
}

void BpfBackend::stop()
{
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [id, slot] : slots_) {
        detach_slot(*slot);
    }
    slots_.clear();
}

bool BpfBackend::attach_prepared(DeviceId id, std::uint32_t hid_id,
                                 const std::string& sysname,
                                 const BpfMouseLayout& layout)
{
    bpf_object* obj = bpf_object__open_file(object_path_.c_str(), nullptr);
    if (!obj) {
        std::fprintf(stderr, "bpf backend: open_file(%s) errno=%d\n",
                     object_path_.c_str(), errno);
        return false;
    }

    // hid_id must be patched before load() (else attach EINVAL)
    // write directly since set_initial_value() refuses partial writes.
    bpf_map* ops = bpf_object__find_map_by_name(obj, RA_MAP_NAME(RA_MAP_OPS));
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
    slot->id = id;
    slot->sysname = sysname;
    slot->hid_id = hid_id;
    slot->layout = layout;
    slot->obj = obj;
    slot->ops_map = ops;
    slot->config_map = bpf_object__find_map_by_name(obj, RA_MAP_NAME(RA_MAP_CONFIG));
    slot->lut_x_map = bpf_object__find_map_by_name(obj, RA_MAP_NAME(RA_MAP_LUT_X));
    slot->lut_y_map = bpf_object__find_map_by_name(obj, RA_MAP_NAME(RA_MAP_LUT_Y));
    slot->state_map = bpf_object__find_map_by_name(obj, RA_MAP_NAME(RA_MAP_STATE));
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

    // Eager attach with identity config so the device is live before any apply.
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

    std::lock_guard<std::mutex> lock(mu_);
    // Replace any stale slot for this id (re-attach without unbind) so its
    // link+object are torn down instead of leaked by a silent emplace noop.
    auto existing = slots_.find(slot->id);
    if (existing != slots_.end()) {
        detach_slot(*existing->second);
        existing->second = std::move(slot);
    } else {
        slots_.emplace(slot->id, std::move(slot));
    }
    return true;
}

bool BpfBackend::populate_maps(Slot& slot,
                               const ra::modifier_settings& s,
                               const ra::device_config& c)
{
    LutBuildResult lut;
    try {
        lut = build_lut(s, c);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bpf backend: %s (%s left unaccelerated)\n",
                     e.what(), slot.sysname.c_str());
        return false;
    }

    // raw curve -> lut_x/lut_y
    // weighting, output-DPI, HID layout -> config
    ra_bpf_config cfg = to_bpf_config(lut, slot.layout);
    
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
        if (bpf_map_update_elem(lutx_fd, &i, &lut.lut_x[i], BPF_ANY) != 0) {
            std::fprintf(stderr, "bpf backend: write lut_x[%u] errno=%d\n", i, errno);
            return false;
        }
        if (bpf_map_update_elem(luty_fd, &i, &lut.lut_y[i], BPF_ANY) != 0) {
            std::fprintf(stderr, "bpf backend: write lut_y[%u] errno=%d\n", i, errno);
            return false;
        }
    }
    return true;
}

BpfBackend::Slot::~Slot()
{
    if (link) bpf_link__destroy(link);
    if (obj) bpf_object__close(obj);
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
    // link attaches in attach_prepared
    // bind only refreshes maps (dormant until attached).
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
    // No packet within this window -> idle
    // above any >=125 Hz gap so responsive.
    constexpr std::uint64_t STALE_NS = 150ull * 1000 * 1000;  // 150 ms

    std::lock_guard<std::mutex> lock(mu_);

    // Most-recently-active device wins (multi-mouse: the one being moved).
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
    if (clock_gettime(CLOCK_MONOTONIC, &now_ts) != 0) return {};
    std::uint64_t now = static_cast<std::uint64_t>(now_ts.tv_sec) * 1000000000ull +
                        static_cast<std::uint64_t>(now_ts.tv_nsec);
    if (now > best_ts && now - best_ts > STALE_NS) return {};

    // Telemetry is Q16.16 in/s weighted by each axis's domain weight
    // divide it back out for normalized in/s.
    const double dw_x = best->domain_w_x_q16 ? static_cast<double>(best->domain_w_x_q16)
                                             : static_cast<double>(RA_Q16_ONE);
    const double dw_y = best->domain_w_y_q16 ? static_cast<double>(best->domain_w_y_q16)
                                             : static_cast<double>(RA_Q16_ONE);

    // Per-axis speeds plus combined
    // the GUI picks. Never force x == y.
    SpeedSample out;
    out.x = static_cast<double>(best_state.tele_speed_x_q16) / dw_x;
    out.y = static_cast<double>(best_state.tele_speed_y_q16) / dw_y;
    
    out.combined = std::hypot(out.x, out.y);
    return out;
}

} // namespace rawaccel_agent
