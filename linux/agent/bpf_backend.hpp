#pragma once

// HID-BPF backend. Loads rawaccel.bpf.o once per attached device, fills
// the per-device config and LUT maps with values from lut_builder.cpp,
// sets hid_id, and registers the struct_ops link.
//
// Per-device fallback: when validate_for_bpf rejects a descriptor we skip
// that device entirely under --backend=bpf. The user can choose
// --backend=evdev for blanket coverage of an unusual mouse. Mixing
// backends per device is out of scope here; rawaccel-hid-probe surfaces
// which devices fall in each bucket.

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
    BpfBackend(std::string object_path);
    ~BpfBackend() override;

    BpfBackend(const BpfBackend&) = delete;
    BpfBackend& operator=(const BpfBackend&) = delete;

    // Enumerate /sys/class/hidraw, attach to every mouse whose descriptor
    // passes validate_for_bpf. Returns true if at least one device
    // attached; non-failure when zero devices match (the user might plug
    // one in later or be running on an idle host).
    bool start();

    // Detach every link and close every bpf_object.
    void stop();

    void on_settings_changed(const ra::modifier_settings& s) override;
    void on_device_added(const DeviceInfo&) override;
    void on_device_removed(DeviceId) override;

    std::size_t attached_count() const;

private:
    struct Slot {
        DeviceId id = 0;
        std::string sysname;
        std::uint32_t hid_id = 0;
        BpfMouseLayout layout{};
        ra::device_config dev_config{};
        bpf_object* obj = nullptr;
        bpf_map* config_map = nullptr;
        bpf_map* lut_x_map = nullptr;
        bpf_map* lut_y_map = nullptr;
        bpf_link* link = nullptr;
    };

    bool attach_node(const std::string& sysname);
    void detach_slot(Slot& slot);
    bool populate_maps(Slot& slot, const ra::modifier_settings& s);

    std::string object_path_;
    mutable std::mutex mu_;
    std::unordered_map<DeviceId, std::unique_ptr<Slot>> slots_;
    ra::modifier_settings current_settings_{};
};

// Helpers exposed for tests: walk /sys/class/hidraw to enumerate hidraw
// nodes, and decode a hid_device name like "0003:046D:C54D.000A" into
// the integer hid_id (10 here).
struct HidrawNode {
    std::string sysname;          // hidrawN
    std::string device_sysname;   // 0003:VVVV:PPPP.IIII
    std::uint32_t hid_id = 0;
};
std::vector<HidrawNode> enumerate_hidraw();
bool parse_hid_device_name(const std::string& name, std::uint32_t& hid_id_out);

} // namespace rawaccel_agent
