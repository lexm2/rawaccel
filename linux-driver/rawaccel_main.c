/*
 * RawAccel Linux Kernel Driver
 * Mouse acceleration driver using LUT-based approach
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>

#include "rawaccel_types.h"
#include "rawaccel_ioctl.h"

/* Module metadata */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("RawAccel Contributors");
MODULE_DESCRIPTION("Mouse acceleration driver with LUT support");
MODULE_VERSION("1.0.0");

/* File operations for /dev/rawaccel */
static const struct file_operations rawaccel_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = rawaccel_ioctl,
	.compat_ioctl   = rawaccel_ioctl,
};

/* Misc device structure */
static struct miscdevice rawaccel_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "rawaccel",
	.fops  = &rawaccel_fops,
	.mode  = 0666,
};

/*
 * Module initialization
 * Called when module is loaded with insmod
 */
static int __init rawaccel_init(void)
{
	int ret;

	pr_info("rawaccel: Loading RawAccel driver v1.0.0\n");
	pr_info("rawaccel: LUT-only mode (no kernel FPU required)\n");

	/* Register misc device for /dev/rawaccel */
	ret = misc_register(&rawaccel_misc);
	if (ret) {
		pr_err("rawaccel: Failed to register misc device: %d\n", ret);
		return ret;
	}
	pr_info("rawaccel: Registered /dev/rawaccel\n");

	/* TODO: Register input handler for mouse interception */

	pr_info("rawaccel: Driver loaded successfully\n");
	return 0;
}

/*
 * Module cleanup
 * Called when module is unloaded with rmmod
 */
static void __exit rawaccel_exit(void)
{
	pr_info("rawaccel: Unloading RawAccel driver\n");

	/* TODO: Unregister input handler */

	/* Unregister misc device */
	misc_deregister(&rawaccel_misc);
	pr_info("rawaccel: Unregistered /dev/rawaccel\n");

	pr_info("rawaccel: Driver unloaded successfully\n");
}

module_init(rawaccel_init);
module_exit(rawaccel_exit);
