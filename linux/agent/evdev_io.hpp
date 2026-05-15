#pragma once

// Thin wrappers around the raw evdev / uinput kernel ABIs.
//
// We deliberately avoid libevdev/libinput to keep dependencies minimal and to
// keep the per-packet path obvious. The wrappers accept already-opened file
// descriptors where it helps testability (pipes can stand in for real evdev
// or uinput nodes for the read/write loop test).

#include "backend.hpp"

#include <linux/input.h>

#include <cstdint>
#include <string>
#include <vector>

namespace rawaccel_agent {

// Read one input_event from fd. Returns false on EOF or unrecoverable I/O
// error. Handles EINTR. Partial reads are an error (the kernel always emits
// whole events for a clean read on /dev/input/eventN; partial reads only
// arise from misuse or pipes).
bool read_event(int fd, input_event& out);

// Write one input_event to fd. Same error semantics as read_event.
bool write_event(int fd, const input_event& ev);

// Query the bitmask of supported event types on the source device. Returns
// false if the ioctl fails.
struct EvdevCapabilities {
    bool has_rel_x = false;
    bool has_rel_y = false;
    bool has_rel_wheel = false;
    bool has_rel_hwheel = false;
    std::vector<std::uint16_t> key_codes;  // BTN_* codes the device reports
    std::string name;                       // EVIOCGNAME
    input_id id{};                          // EVIOCGID
};

bool query_capabilities(int fd, EvdevCapabilities& caps);

// EVIOCGRAB / EVIOCGRAB(0). grab returns true on success; ungrab is silent.
bool evdev_grab(int fd);
void evdev_ungrab(int fd);

// Create a uinput device that mirrors the source caps. Returns the uinput
// fd on success, -1 on failure (errno set). The caller closes it via
// uinput_destroy().
int uinput_create_mirror(const EvdevCapabilities& src, const std::string& name);
void uinput_destroy(int fd);

// Open a /dev/input/eventN-style path read-only.
int evdev_open(const std::string& path);

} // namespace rawaccel_agent
