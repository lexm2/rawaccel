/*
 * RawAccel IOCTL Implementation
 * Handles userspace communication via IOCTL
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>

#include "rawaccel_ioctl.h"
#include "rawaccel_input.h"

#define DRIVER_VERSION_MAJOR 1
#define DRIVER_VERSION_MINOR 0
#define DRIVER_VERSION_PATCH 0

/*
 * Handle IOCTL_GET_VERSION
 * Returns driver version information
 */
static long handle_get_version(void __user *argp)
{
	struct rawaccel_version version = {
		.major = DRIVER_VERSION_MAJOR,
		.minor = DRIVER_VERSION_MINOR,
		.patch = DRIVER_VERSION_PATCH,
		.reserved = 0
	};

	if (copy_to_user(argp, &version, sizeof(version)))
		return -EFAULT;

	return 0;
}

/*
 * Handle IOCTL_READ
 * Read current driver configuration
 */
static long handle_read_config(void __user *argp)
{
	/* TODO: Implement configuration reading */
	pr_info("rawaccel: READ config requested (not yet implemented)\n");
	return -ENOSYS;
}

/*
 * Validate LUT configuration
 * Checks size, sorted order, and positive values
 */
static int validate_lut(const struct rawaccel_lut_config *lut)
{
	int i;

	/* Check size */
	if (lut->size < 2 || lut->size > LUT_MAX_POINTS) {
		pr_err("rawaccel: Invalid LUT size: %u (must be 2-%d)\n",
		       lut->size, LUT_MAX_POINTS);
		return -EINVAL;
	}

	/* Check that X values are sorted and positive */
	for (i = 0; i < lut->size; i++) {
		if (lut->points_x[i] < 0) {
			pr_err("rawaccel: Negative X value at point %d: %d\n",
			       i, lut->points_x[i]);
			return -EINVAL;
		}

		if (i > 0 && lut->points_x[i] <= lut->points_x[i - 1]) {
			pr_err("rawaccel: LUT not sorted at point %d: %d <= %d\n",
			       i, lut->points_x[i], lut->points_x[i - 1]);
			return -EINVAL;
		}

		/* Y values should be positive */
		if (lut->points_y[i] < 0) {
			pr_err("rawaccel: Negative Y value at point %d: %d\n",
			       i, lut->points_y[i]);
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * Handle IOCTL_WRITE
 * Write new driver configuration
 */
static long handle_write_config(void __user *argp)
{
	struct rawaccel_device_config config;
	int ret;

	/* Copy configuration from userspace */
	if (copy_from_user(&config, argp, sizeof(config)))
		return -EFAULT;

	/* Validate X-axis LUT */
	ret = validate_lut(&config.lut_x);
	if (ret)
		return ret;

	/* Validate Y-axis LUT if separate */
	if (config.separate_axes) {
		ret = validate_lut(&config.lut_y);
		if (ret)
			return ret;
	}

	/* Validate DPI */
	if (config.dpi < 100 || config.dpi > 50000) {
		pr_err("rawaccel: Invalid DPI: %u (must be 100-50000)\n",
		       config.dpi);
		return -EINVAL;
	}

	/* Update all connected devices */
	rawaccel_update_all_devices(&config);

	pr_info("rawaccel: Configuration updated successfully\n");
	return 0;
}

/*
 * Main IOCTL handler
 * Routes IOCTL commands to appropriate handlers
 */
long rawaccel_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case RAWACCEL_IOC_GET_VERSION:
		return handle_get_version(argp);

	case RAWACCEL_IOC_READ:
		return handle_read_config(argp);

	case RAWACCEL_IOC_WRITE:
		return handle_write_config(argp);

	default:
		pr_warn("rawaccel: Unknown IOCTL command: 0x%x\n", cmd);
		return -ENOTTY;
	}
}
