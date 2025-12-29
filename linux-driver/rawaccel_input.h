/*
 * RawAccel Input Handler
 * Mouse event interception and processing
 */

#ifndef RAWACCEL_INPUT_H
#define RAWACCEL_INPUT_H

#include <linux/input.h>
#include "rawaccel_types.h"
#include "rawaccel_ioctl.h"

/* Register/unregister input handler */
int rawaccel_input_register(void);
void rawaccel_input_unregister(void);

/* Update all devices with new configuration */
void rawaccel_update_all_devices(const struct rawaccel_device_config *config);

#endif /* RAWACCEL_INPUT_H */
