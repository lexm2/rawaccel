/*
 * RawAccel Input Handler
 * Mouse event interception and processing
 */

#ifndef RAWACCEL_INPUT_H
#define RAWACCEL_INPUT_H

#include <linux/input.h>
#include "rawaccel_types.h"

/* Register/unregister input handler */
int rawaccel_input_register(void);
void rawaccel_input_unregister(void);

#endif /* RAWACCEL_INPUT_H */
