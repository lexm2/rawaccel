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

/* Version structure */
struct rawaccel_version {
	u8 major;
	u8 minor;
	u8 patch;
	u8 reserved;
};

/* Configuration header */
struct rawaccel_io_header {
	u32 num_profiles;
	u32 num_devices;
	u32 total_size;
};

/* IOCTL command definitions */
#define RAWACCEL_IOC_GET_VERSION \
	_IOR(RAWACCEL_IOC_MAGIC, 0, struct rawaccel_version)

#define RAWACCEL_IOC_READ \
	_IOR(RAWACCEL_IOC_MAGIC, 1, struct rawaccel_io_header)

#define RAWACCEL_IOC_WRITE \
	_IOW(RAWACCEL_IOC_MAGIC, 2, struct rawaccel_io_header)

/* Function prototypes */
long rawaccel_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif /* RAWACCEL_IOCTL_H */
