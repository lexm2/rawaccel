#include "rawaccel-device.h"
#include "rawaccel-accel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

bool rawaccel_device_is_mouse(const char *path) {
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }

    struct libevdev *dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        close(fd);
        return false;
    }

    // Check if device has relative axes (mouse characteristic)
    bool is_mouse = libevdev_has_event_type(dev, EV_REL) &&
                    libevdev_has_event_code(dev, EV_REL, REL_X) &&
                    libevdev_has_event_code(dev, EV_REL, REL_Y);

    libevdev_free(dev);
    close(fd);

    return is_mouse;
}

struct rawaccel_device *rawaccel_device_create(const char *path) {
    struct rawaccel_device *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return NULL;
    }

    dev->path = strdup(path);
    dev->fd = -1;

    // Open device
    dev->fd = open(path, O_RDONLY | O_NONBLOCK);
    if (dev->fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        goto error;
    }

    // Create libevdev handle
    int rc = libevdev_new_from_fd(dev->fd, &dev->evdev);
    if (rc < 0) {
        fprintf(stderr, "Failed to create libevdev for %s: %s\n",
                path, strerror(-rc));
        goto error;
    }

    // Get device name
    const char *name = libevdev_get_name(dev->evdev);
    strncpy(dev->name, name ? name : "Unknown", sizeof(dev->name) - 1);

    // Grab device (exclusive access)
    rc = libevdev_grab(dev->evdev, LIBEVDEV_GRAB);
    if (rc < 0) {
        fprintf(stderr, "Failed to grab %s: %s\n", path, strerror(-rc));
        goto error;
    }

    // Create virtual device
    dev->uinput = rawaccel_uinput_create(dev->name);
    if (!dev->uinput) {
        fprintf(stderr, "Failed to create virtual device for %s\n", path);
        goto error;
    }

    // Initialize config (disabled by default)
    dev->config.enabled = false;
    dev->config.separate_axes = false;
    dev->config.dpi = 800;

    printf("Created device: %s (%s)\n", dev->name, path);

    return dev;

error:
    rawaccel_device_destroy(dev);
    return NULL;
}

void rawaccel_device_destroy(struct rawaccel_device *dev) {
    if (!dev) {
        return;
    }

    if (dev->uinput) {
        rawaccel_uinput_destroy(dev->uinput);
    }

    if (dev->evdev) {
        libevdev_grab(dev->evdev, LIBEVDEV_UNGRAB);
        libevdev_free(dev->evdev);
    }

    if (dev->fd >= 0) {
        close(dev->fd);
    }

    free(dev->path);
    free(dev);
}

int rawaccel_device_process_event(struct rawaccel_device *dev) {
    struct input_event ev;
    int rc = libevdev_next_event(dev->evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

    if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
        // Buffer relative motion events
        if (ev.type == EV_REL && ev.code == REL_X) {
            dev->dx_accum += ev.value;
            return 0;
        } else if (ev.type == EV_REL && ev.code == REL_Y) {
            dev->dy_accum += ev.value;
            return 0;
        }
        // On SYN_REPORT, apply acceleration and emit
        else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            if (dev->dx_accum != 0 || dev->dy_accum != 0) {
                int out_dx, out_dy;

                // Apply acceleration
                rawaccel_apply_acceleration(&dev->config,
                                           dev->dx_accum, dev->dy_accum,
                                           &out_dx, &out_dy);

                // Emit to virtual device
                if (out_dx != 0) {
                    rawaccel_uinput_emit_rel(dev->uinput, REL_X, out_dx);
                }
                if (out_dy != 0) {
                    rawaccel_uinput_emit_rel(dev->uinput, REL_Y, out_dy);
                }

                // Reset accumulators
                dev->dx_accum = 0;
                dev->dy_accum = 0;
            }

            // Emit SYN_REPORT
            rawaccel_uinput_emit_syn(dev->uinput, SYN_REPORT, 0);
            return 0;
        }
        // Forward other events unchanged (buttons, wheel, etc.)
        else {
            rawaccel_uinput_emit(dev->uinput, ev.type, ev.code, ev.value);
            return 0;
        }
    } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
        // Device dropped events, need to sync
        fprintf(stderr, "Device %s dropped events, syncing...\n", dev->name);

        while (rc == LIBEVDEV_READ_STATUS_SYNC) {
            rc = libevdev_next_event(dev->evdev, LIBEVDEV_READ_FLAG_SYNC, &ev);
            if (rc == LIBEVDEV_READ_STATUS_SYNC || rc == LIBEVDEV_READ_STATUS_SUCCESS) {
                rawaccel_uinput_emit(dev->uinput, ev.type, ev.code, ev.value);
            }
        }
        return 0;
    } else if (rc == -EAGAIN) {
        // No more events
        return 0;
    } else {
        fprintf(stderr, "Error reading from %s: %s\n", dev->name, strerror(-rc));
        return -1;
    }
}

void rawaccel_device_update_config(struct rawaccel_device *dev,
                                   const struct rawaccel_device_config *config) {
    memcpy(&dev->config, config, sizeof(dev->config));
    printf("Updated config for %s: enabled=%d, dpi=%u, num_points=%u\n",
           dev->name, config->enabled, config->dpi, config->lut_x.num_points);
}
