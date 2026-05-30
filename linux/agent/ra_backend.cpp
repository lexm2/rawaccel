#include "ra_backend.h"

#include "bpf_backend.hpp"
#include "bpf_capability.hpp"
#include "json_io.hpp"

#include <nlohmann/json.hpp>

#include <cstring>
#include <exception>
#include <string>

using rawaccel_agent::BpfBackend;
using rawaccel_agent::BpfMouseLayout;
namespace ra = rawaccel;

namespace {

void copy_truncated(char* dst, std::size_t cap, const std::string& src)
{
    if (cap == 0) return;
    const std::size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

} // namespace

extern "C" {

int ra_probe_capability(ra_probe_result* out)
{
    if (!out) return 0;
    auto p = rawaccel_agent::probe_bpf_capability();
    out->kernel_major = p.kernel_major;
    out->kernel_minor = p.kernel_minor;
    out->kernel_ok = p.kernel_ok ? 1 : 0;
    out->syscall_ok = p.syscall_ok ? 1 : 0;
    copy_truncated(out->reason, sizeof(out->reason), p.reason);
    return p.ok() ? 1 : 0;
}

ra_backend_t* ra_backend_create(const char* bpf_object_path)
{
    try {
        auto* be = new BpfBackend(bpf_object_path ? bpf_object_path : "");
        return reinterpret_cast<ra_backend_t*>(be);
    } catch (...) {
        return nullptr;
    }
}

void ra_backend_destroy(ra_backend_t* be)
{
    delete reinterpret_cast<BpfBackend*>(be);
}

int ra_backend_attach(ra_backend_t* be, uint64_t id, uint32_t hid_id,
                      const char* sysname, const ra_mouse_layout* layout)
{
    if (!be || !sysname || !layout) return -1;
    BpfMouseLayout l;
    l.report_id     = layout->report_id;
    l.dx_byte_offset = layout->dx_byte_offset;
    l.dx_byte_size   = layout->dx_byte_size;
    l.dy_byte_offset = layout->dy_byte_offset;
    l.dy_byte_size   = layout->dy_byte_size;
    try {
        return reinterpret_cast<BpfBackend*>(be)
                   ->attach_prepared(id, hid_id, sysname, l) ? 0 : -1;
    } catch (...) {
        return -1;
    }
}

void ra_backend_detach(ra_backend_t* be, uint64_t id)
{
    if (be) reinterpret_cast<BpfBackend*>(be)->unbind_device(id);
}

int ra_backend_bind(ra_backend_t* be, uint64_t id, const char* resolved_json)
{
    if (!be || !resolved_json) return -1;
    try {
        auto j = nlohmann::json::parse(resolved_json);
        ra::modifier_settings ms =
            rajson::modifier_settings_from_jobject(j.at("profile"));
        ra::device_config dc =
            rajson::device_config_from_jobject(j.at("config"));
        reinterpret_cast<BpfBackend*>(be)->bind_device(id, ms, dc);
        return 0;
    } catch (...) {
        return -1;
    }
}

void ra_backend_health(const ra_backend_t* be, ra_backend_health_t* out)
{
    if (!out) return;
    out->devices = 0;
    out->attached = 0;
    out->error[0] = '\0';
    if (!be) return;
    auto h = reinterpret_cast<const BpfBackend*>(be)->health();
    out->devices = h.devices;
    out->attached = h.attached;
    copy_truncated(out->error, sizeof(out->error), h.error);
}

void ra_backend_speed(const ra_backend_t* be, ra_speed_sample_t* out)
{
    if (!out) return;
    out->x = out->y = out->combined = 0.0;
    if (!be) return;
    auto s = reinterpret_cast<const BpfBackend*>(be)->current_speed_sample();
    out->x = s.x;
    out->y = s.y;
    out->combined = s.combined;
}

} // extern "C"
