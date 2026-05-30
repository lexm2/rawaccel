#pragma once

// HID-BPF backend: one rawaccel.bpf.o per hidraw mouse, fills config/LUT maps,
// registers the struct_ops link. Rejected descriptors stay pass-through.

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

    void stop();

    // libbpf attach core (descriptor already parsed, id pre-hashed), driven by the
    // C ABI: identity populate then attach.
    bool attach_prepared(DeviceId id, std::uint32_t hid_id,
                         const std::string& sysname, const BpfMouseLayout& layout);

    void bind_device(DeviceId,
                     const ra::modifier_settings&,
                     const ra::device_config&) override;
    void unbind_device(DeviceId) override;

    // devices = slots; attached = those with a live struct_ops link
    DataPlaneHealth health() const override;

    // Most-recently-active device's ra_state telemetry as normalized in/s.
    // Zero when idle (stale) or none attached.
    SpeedSample current_speed_sample() const override;

private:
    struct Slot {
        // Closes link + object if still open, so dropping a Slot never leaks.
        ~Slot();

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

        // Cached from last populate_maps to dequantize Q16.16 telemetry.
        std::int32_t domain_w_x_q16 = RA_Q16_ONE;
        std::int32_t domain_w_y_q16 = RA_Q16_ONE;
    };

    void detach_slot(Slot& slot);
    bool populate_maps(Slot& slot,
                       const ra::modifier_settings& s,
                       const ra::device_config& c);

    std::string object_path_;
    mutable std::mutex mu_;
    std::unordered_map<DeviceId, std::unique_ptr<Slot>> slots_;
};

} // namespace rawaccel_agent
