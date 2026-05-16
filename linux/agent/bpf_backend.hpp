#pragma once

// HID-BPF backend: loads rawaccel.bpf.o per attached hidraw mouse, fills the
// config and LUT maps from the agent's bind_device call, and registers the
// struct_ops link. Rejected descriptors are skipped (devices remain
// pass-through, not broken). See rawaccel-hid-probe for diagnostics.

#include "backend.hpp"
#include "hid_descriptor.hpp"
#include "lut_builder.hpp"
#include "rawaccel_bpf_layout.h"

#include "rawaccel.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

struct bpf_object;
struct bpf_map;
struct bpf_link;

namespace rawaccel_agent {

class BpfBackend : public Backend {
public:
    explicit BpfBackend(std::string object_path);
    ~BpfBackend() override;

    BpfBackend(const BpfBackend&) = delete;
    BpfBackend& operator=(const BpfBackend&) = delete;

    // Must be set before start() so the backend can report discoveries
    // back into the agent's profile resolver.
    void set_listener(DeviceListener& listener);

    // Enumerate /sys/class/hidraw, prepare a slot per accepted mouse, and
    // notify the listener for each. The struct_ops link is attached lazily
    // on the first bind_device call per slot.
    bool start();
    void stop();

    void bind_device(DeviceId,
                     const ra::modifier_settings&,
                     const ra::device_config&) override;
    void unbind_device(DeviceId) override;

    std::size_t attached_count() const;

private:
    struct Slot {
        DeviceId id = 0;
        std::string sysname;
        std::uint32_t hid_id = 0;
        BpfMouseLayout layout{};
        bpf_object* obj = nullptr;
        bpf_map* ops_map = nullptr;
        bpf_map* config_map = nullptr;
        bpf_map* lut_x_map = nullptr;
        bpf_map* lut_y_map = nullptr;
        bpf_link* link = nullptr;
        bool attached = false;
    };

    bool attach_node(const std::string& sysname);
    void detach_slot(Slot& slot);
    bool populate_maps(Slot& slot,
                       const ra::modifier_settings& s,
                       const ra::device_config& c);

    std::string object_path_;
    DeviceListener* listener_ = nullptr;
    mutable std::mutex mu_;
    std::unordered_map<DeviceId, std::unique_ptr<Slot>> slots_;
};

struct HidrawNode {
    std::string sysname;          // hidrawN
    std::string device_sysname;   // 0003:VVVV:PPPP.IIII
    std::uint32_t hid_id = 0;
};
std::vector<HidrawNode> enumerate_hidraw();
bool parse_hid_device_name(const std::string& name, std::uint32_t& hid_id_out);

struct HidrawIdentity {
    std::uint32_t vendor_id = 0;
    std::uint32_t product_id = 0;
    std::string name;
};
HidrawIdentity read_hidraw_identity(const std::string& syspath);

} // namespace rawaccel_agent
