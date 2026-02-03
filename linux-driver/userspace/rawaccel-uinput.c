#include "rawaccel-uinput.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>

struct rawaccel_uinput {
    int fd;
    char name[256];
};

struct rawaccel_uinput *rawaccel_uinput_create(const char *name) {
    struct rawaccel_uinput *uinput = calloc(1, sizeof(*uinput));
    if (!uinput) {
        return NULL;
    }

    // Open uinput device
    uinput->fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput->fd < 0) {
        fprintf(stderr, "Failed to open /dev/uinput\n");
        free(uinput);
        return NULL;
    }

    // Enable event types
    ioctl(uinput->fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput->fd, UI_SET_EVBIT, EV_REL);
    ioctl(uinput->fd, UI_SET_EVBIT, EV_SYN);

    // Enable mouse buttons
    ioctl(uinput->fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(uinput->fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(uinput->fd, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(uinput->fd, UI_SET_KEYBIT, BTN_SIDE);
    ioctl(uinput->fd, UI_SET_KEYBIT, BTN_EXTRA);
    ioctl(uinput->fd, UI_SET_KEYBIT, BTN_FORWARD);
    ioctl(uinput->fd, UI_SET_KEYBIT, BTN_BACK);

    // Enable relative axes
    ioctl(uinput->fd, UI_SET_RELBIT, REL_X);
    ioctl(uinput->fd, UI_SET_RELBIT, REL_Y);
    ioctl(uinput->fd, UI_SET_RELBIT, REL_WHEEL);
    ioctl(uinput->fd, UI_SET_RELBIT, REL_HWHEEL);

    // Setup device
    struct uinput_setup setup = {0};
    snprintf(setup.name, sizeof(setup.name), "RawAccel Virtual Mouse (%s)", name);
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1234;
    setup.id.product = 0x5678;
    setup.id.version = 1;

    if (ioctl(uinput->fd, UI_DEV_SETUP, &setup) < 0) {
        fprintf(stderr, "Failed to setup uinput device\n");
        close(uinput->fd);
        free(uinput);
        return NULL;
    }

    // Create device
    if (ioctl(uinput->fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "Failed to create uinput device\n");
        close(uinput->fd);
        free(uinput);
        return NULL;
    }

    strncpy(uinput->name, name, sizeof(uinput->name) - 1);
    printf("Created virtual device: %s\n", setup.name);

    return uinput;
}

void rawaccel_uinput_destroy(struct rawaccel_uinput *uinput) {
    if (!uinput) {
        return;
    }

    if (uinput->fd >= 0) {
        ioctl(uinput->fd, UI_DEV_DESTROY);
        close(uinput->fd);
    }

    free(uinput);
}

int rawaccel_uinput_emit(struct rawaccel_uinput *uinput,
                         uint16_t type, uint16_t code, int32_t value) {
    struct input_event ev = {0};
    ev.type = type;
    ev.code = code;
    ev.value = value;

    if (write(uinput->fd, &ev, sizeof(ev)) != sizeof(ev)) {
        return -1;
    }

    return 0;
}

int rawaccel_uinput_emit_rel(struct rawaccel_uinput *uinput,
                              uint16_t code, int32_t value) {
    return rawaccel_uinput_emit(uinput, EV_REL, code, value);
}

int rawaccel_uinput_emit_syn(struct rawaccel_uinput *uinput,
                              uint16_t code, int32_t value) {
    return rawaccel_uinput_emit(uinput, EV_SYN, code, value);
}
