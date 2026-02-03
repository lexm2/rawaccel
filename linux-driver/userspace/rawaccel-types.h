#ifndef RAWACCEL_TYPES_H
#define RAWACCEL_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define RAWACCEL_MAX_LUT_POINTS 256
#define RAWACCEL_IPC_MAGIC 0x52415743  // "RAWC"
#define RAWACCEL_IPC_VERSION 1

// LUT configuration (per-axis)
struct rawaccel_lut {
    float speeds[RAWACCEL_MAX_LUT_POINTS];      // Input speeds
    float multipliers[RAWACCEL_MAX_LUT_POINTS]; // Output multipliers
    uint32_t num_points;                         // Number of LUT points
};

// Device configuration
struct rawaccel_device_config {
    bool enabled;
    struct rawaccel_lut lut_x;
    struct rawaccel_lut lut_y;
    bool separate_axes;
    uint32_t dpi;
};

// IPC commands
enum rawaccel_ipc_command {
    RAWACCEL_CMD_UPDATE_CONFIG = 1,
    RAWACCEL_CMD_DISABLE = 2,
    RAWACCEL_CMD_ENABLE = 3,
    RAWACCEL_CMD_GET_STATUS = 4
};

// IPC message format
struct rawaccel_ipc_message {
    uint32_t magic;         // 0x52415743 ("RAWC")
    uint32_t version;       // Protocol version
    uint32_t command;       // rawaccel_ipc_command
    uint32_t payload_size;  // Size of payload following this header
};

#endif // RAWACCEL_TYPES_H
