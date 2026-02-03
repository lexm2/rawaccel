#ifndef RAWACCEL_DEVICE_H
#define RAWACCEL_DEVICE_H

#include <libevdev/libevdev.h>
#include "rawaccel-types.h"
#include "rawaccel-uinput.h"

struct rawaccel_device {
    char *path;                              // Device path (e.g., /dev/input/event0)
    char name[256];                          // Device name
    int fd;                                  // File descriptor
    struct libevdev *evdev;                  // libevdev handle
    struct rawaccel_uinput *uinput;         // Virtual device
    struct rawaccel_device_config config;    // Acceleration configuration

    // Accumulated motion (for combining REL_X and REL_Y before SYN_REPORT)
    int dx_accum;
    int dy_accum;
};

// Create and grab a device
struct rawaccel_device *rawaccel_device_create(const char *path);

// Release and destroy a device
void rawaccel_device_destroy(struct rawaccel_device *dev);

// Process an event from the device
int rawaccel_device_process_event(struct rawaccel_device *dev);

// Update device configuration
void rawaccel_device_update_config(struct rawaccel_device *dev,
                                   const struct rawaccel_device_config *config);

// Check if a device path is a mouse
bool rawaccel_device_is_mouse(const char *path);

#endif // RAWACCEL_DEVICE_H
