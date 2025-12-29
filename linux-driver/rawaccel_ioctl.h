/*
 * RawAccel IOCTL Interface
 * Defines IOCTL commands for userspace communication
 */

#ifndef RAWACCEL_IOCTL_H
#define RAWACCEL_IOCTL_H

#include <linux/ioctl.h>
#include "rawaccel_types.h"

/* IOCTL magic number */
#define RAWACCEL_IOC_MAGIC 0x88

/* Configuration magic number */
#define RAWACCEL_CONFIG_MAGIC 0x52415743  /* "RAWC" */
#define RAWACCEL_CONFIG_VERSION 1

/* Version structure */
struct rawaccel_version {
	u8 major;
	u8 minor;
	u8 patch;
	u8 reserved;
};

/* LUT configuration for a single axis */
struct rawaccel_lut_config {
	u32 size;              /* Number of points (2-129) */
	bool velocity_mode;    /* Velocity/gain mode flag */
	u8 reserved[3];        /* Padding */
	s32 points_x[LUT_MAX_POINTS];  /* X values (16.16 fixed-point) */
	s32 points_y[LUT_MAX_POINTS];  /* Y values (16.16 fixed-point) */
};

/* Device configuration */
struct rawaccel_device_config {
	bool enabled;          /* Enable acceleration for this device */
	bool separate_axes;    /* Use separate LUTs for X and Y */
	u8 reserved[2];        /* Padding */
	u32 dpi;               /* Device DPI */
	struct rawaccel_lut_config lut_x;  /* X-axis LUT */
	struct rawaccel_lut_config lut_y;  /* Y-axis LUT (if separate_axes) */
};

/* Configuration header */
struct rawaccel_io_header {
	u32 magic;             /* Magic number for validation */
	u32 version;           /* Protocol version */
	u32 config_size;       /* Size of device_config structure */
};

/* IOCTL command definitions */
#define RAWACCEL_IOC_GET_VERSION \
	_IOR(RAWACCEL_IOC_MAGIC, 0, struct rawaccel_version)

#define RAWACCEL_IOC_READ \
	_IOR(RAWACCEL_IOC_MAGIC, 1, struct rawaccel_device_config)

#define RAWACCEL_IOC_WRITE \
	_IOW(RAWACCEL_IOC_MAGIC, 2, struct rawaccel_device_config)

/* Function prototypes */
long rawaccel_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif /* RAWACCEL_IOCTL_H */
