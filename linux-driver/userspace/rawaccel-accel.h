#ifndef RAWACCEL_ACCEL_H
#define RAWACCEL_ACCEL_H

#include "rawaccel-types.h"

// Perform LUT lookup with binary search and linear interpolation
float rawaccel_lut_lookup(const struct rawaccel_lut *lut, float speed);

// Apply acceleration to a pair of dx, dy values
void rawaccel_apply_acceleration(
    const struct rawaccel_device_config *config,
    int dx, int dy,
    int *out_dx, int *out_dy
);

#endif // RAWACCEL_ACCEL_H
