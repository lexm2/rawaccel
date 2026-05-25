#pragma once

// HID-BPF backend: one rawaccel.bpf.o per hidraw mouse, fills config/LUT maps
// from bind_device, registers the struct_ops link. Rejected descriptors skip
// (stay pass-through). See rawaccel-hid-probe for diagnostics.

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

    // Set before start() so discoveries reach the agent's profile resolver.
    void set_listener(DeviceListener& listener);

    // Enumerate hidraw, slot per accepted mouse, eager attach (pass-through),
    // notify the listener. Attaching at discovery surfaces failures immediately.
    bool start();
    void stop();

    void bind_device(DeviceId,
                     const ra::modifier_settings&,
                     const ra::device_config&) override;
    void unbind_device(DeviceId) override;

    std::size_t attached_count() const;

    // devices = slots; attached = those with a live struct_ops link
    DataPlaneHealth health() const override;

    // Reads the most-recently-active device's ra_state telemetry and converts it
    // to chart units (normalized in/s). Zero when idle (stale) or none attached.
    SpeedSample current_speed_sample() const override;

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
        bpf_map* state_map = nullptr;  // ra_state, read for the stats RPC
        bpf_link* link = nullptr;
        bool attached = false;
        std::string attach_error;  // populated when attach failed

        // Cached from the last populate_maps so current_speed_sample can convert
        // the kernel's Q16.16 telemetry into chart units without re-reading config.
        std::int32_t domain_w_x_q16 = RA_Q16_ONE;
        std::int32_t domain_w_y_q16 = RA_Q16_ONE;
        std::uint8_t dist_mode = RA_DIST_EUCLIDEAN;
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
