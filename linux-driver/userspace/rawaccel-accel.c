#include "rawaccel-accel.h"
#include <math.h>
#include <stdio.h>

float rawaccel_lut_lookup(const struct rawaccel_lut *lut, float speed) {
    if (lut->num_points == 0) {
        return 1.0f;  // No acceleration
    }

    // Clamp to bounds
    if (speed <= lut->speeds[0]) {
        return lut->multipliers[0];
    }
    if (speed >= lut->speeds[lut->num_points - 1]) {
        return lut->multipliers[lut->num_points - 1];
    }

    // Binary search for surrounding points
    int left = 0;
    int right = lut->num_points - 1;

    while (right - left > 1) {
        int mid = (left + right) / 2;
        if (lut->speeds[mid] <= speed) {
            left = mid;
        } else {
            right = mid;
        }
    }

    // Linear interpolation
    float t = (speed - lut->speeds[left]) /
              (lut->speeds[right] - lut->speeds[left]);
    return lut->multipliers[left] * (1.0f - t) + lut->multipliers[right] * t;
}

void rawaccel_apply_acceleration(
    const struct rawaccel_device_config *config,
    int dx, int dy,
    int *out_dx, int *out_dy
) {
    if (!config->enabled || (dx == 0 && dy == 0)) {
        *out_dx = dx;
        *out_dy = dy;
        return;
    }

    // Calculate speed (magnitude)
    float speed = sqrtf((float)(dx * dx + dy * dy));

    // Lookup multipliers
    float mult_x, mult_y;
    if (config->separate_axes) {
        mult_x = rawaccel_lut_lookup(&config->lut_x, fabsf((float)dx));
        mult_y = rawaccel_lut_lookup(&config->lut_y, fabsf((float)dy));
    } else {
        // Use combined speed for both axes
        float mult = rawaccel_lut_lookup(&config->lut_x, speed);
        mult_x = mult;
        mult_y = mult;
    }

    // Apply acceleration
    *out_dx = (int)roundf((float)dx * mult_x);
    *out_dy = (int)roundf((float)dy * mult_y);
}
