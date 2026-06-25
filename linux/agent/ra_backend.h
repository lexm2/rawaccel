#ifndef RA_BACKEND_H
#define RA_BACKEND_H

// C ABI over the HID-BPF data plane + curve math. Rust calls down here for the
// libbpf attach/bind lifecycle and LUT/curve math; never called back. Strings
// are UTF-8 NUL-terminated; out-params written only on success unless noted.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mirrors rawaccel_agent::BpfMouseLayout (5 x u8); the only struct crossing the boundary.
typedef struct {
    uint8_t report_id;       // 0 when no report ID byte is used
    uint8_t dx_byte_offset;
    uint8_t dx_byte_size;
    uint8_t dy_byte_offset;
    uint8_t dy_byte_size;
} ra_mouse_layout;

typedef struct {
    int  kernel_major;
    int  kernel_minor;
    int  kernel_ok;          // bool: kernel >= required
    int  syscall_ok;         // bool: bpf() callable (CAP_BPF / root)
    char reason[128];        // "" when ok
    // truncated diagnostic otherwise
} ra_probe_result;

// Wraps probe_bpf_capability(). Returns 1 if both checks pass, else 0.
int ra_probe_capability(ra_probe_result* out);

typedef struct ra_backend ra_backend_t;

// `bpf_object_path` is the rawaccel.bpf.o loaded per device (open at attach);
// returns null only on OOM, not on bad path.
ra_backend_t* ra_backend_create(const char* bpf_object_path);
void          ra_backend_destroy(ra_backend_t*);

// Open/patch/load/attach the BPF for one device. `id` is the caller's stable
// device id. Returns 0 on success (slot live, possibly unattached if struct_ops
// attach failed; see ra_backend_health), -1 on hard failure (no slot kept).
int  ra_backend_attach(ra_backend_t*, uint64_t id, uint32_t hid_id,
                       const char* sysname, const ra_mouse_layout* layout);
void ra_backend_detach(ra_backend_t*, uint64_t id);

// Refresh a slot's maps from resolved settings. `resolved_json` is
// {"profile":{..},"config":{..}}. Returns 0 on success, -1 on unknown id / parse / write failure.
int  ra_backend_bind(ra_backend_t*, uint64_t id, const char* resolved_json);

typedef struct {
    size_t devices;          // slots prepared
    size_t attached;         // slots with a live struct_ops link
    char   error[256];       // first attach failure, "" if none
} ra_backend_health_t;
void ra_backend_health(const ra_backend_t*, ra_backend_health_t* out);

typedef struct { double x; double y; double combined; } ra_speed_sample_t;
void ra_backend_speed(const ra_backend_t*, ra_speed_sample_t* out);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // RA_BACKEND_H
