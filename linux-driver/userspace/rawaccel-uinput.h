#ifndef RAWACCEL_UINPUT_H
#define RAWACCEL_UINPUT_H

#include <stdint.h>
#include <linux/input.h>

struct rawaccel_uinput;

// Create a virtual input device that mirrors the given physical device
struct rawaccel_uinput *rawaccel_uinput_create(const char *name);

// Destroy the virtual device
void rawaccel_uinput_destroy(struct rawaccel_uinput *uinput);

// Emit an event to the virtual device
int rawaccel_uinput_emit(struct rawaccel_uinput *uinput,
                         uint16_t type, uint16_t code, int32_t value);

// Emit a REL event
int rawaccel_uinput_emit_rel(struct rawaccel_uinput *uinput,
                              uint16_t code, int32_t value);

// Emit a SYN event
int rawaccel_uinput_emit_syn(struct rawaccel_uinput *uinput,
                              uint16_t code, int32_t value);

#endif // RAWACCEL_UINPUT_H
