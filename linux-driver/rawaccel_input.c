/*
 * RawAccel Input Handler Implementation
 * Intercepts and processes mouse input events
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/slab.h>

#include "rawaccel_input.h"
#include "rawaccel_types.h"

/* Input handler instance */
static struct input_handler rawaccel_handler;

/*
 * Event handler - called for every input event
 * This is where we intercept mouse movements
 */
static void rawaccel_event(struct input_handle *handle,
                           unsigned int type,
                           unsigned int code,
                           int value)
{
	/* For now, just pass through all events unmodified */
	/* TODO: Buffer REL_X and REL_Y until SYN_REPORT */
	/* TODO: Apply acceleration and forward modified values */
}

/*
 * Connect callback - called when a matching device is found
 * Allocates per-device context and registers the handle
 */
static int rawaccel_connect(struct input_handler *handler,
                            struct input_dev *dev,
                            const struct input_device_id *id)
{
	struct input_handle *handle;
	int ret;

	/* Only connect to devices with relative axes (mice) */
	if (!test_bit(EV_REL, dev->evbit))
		return -ENODEV;

	/* Allocate handle */
	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "rawaccel";

	/* Register the handle */
	ret = input_register_handle(handle);
	if (ret) {
		pr_err("rawaccel: Failed to register handle for %s: %d\n",
		       dev->name, ret);
		goto err_free_handle;
	}

	/* Open the device */
	ret = input_open_device(handle);
	if (ret) {
		pr_err("rawaccel: Failed to open device %s: %d\n",
		       dev->name, ret);
		goto err_unregister;
	}

	pr_info("rawaccel: Connected to device: %s\n", dev->name);
	return 0;

err_unregister:
	input_unregister_handle(handle);
err_free_handle:
	kfree(handle);
	return ret;
}

/*
 * Disconnect callback - called when device is removed
 * Cleans up per-device context
 */
static void rawaccel_disconnect(struct input_handle *handle)
{
	pr_info("rawaccel: Disconnecting from device: %s\n",
	        handle->dev->name);

	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

/*
 * Device ID table - defines which devices we want to intercept
 * Currently matches all devices with EV_REL (relative movement)
 */
static const struct input_device_id rawaccel_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_REL) },
	},
	{ },  /* Terminator */
};

MODULE_DEVICE_TABLE(input, rawaccel_ids);

/*
 * Input handler structure
 */
static struct input_handler rawaccel_handler = {
	.event      = rawaccel_event,
	.connect    = rawaccel_connect,
	.disconnect = rawaccel_disconnect,
	.name       = "rawaccel",
	.id_table   = rawaccel_ids,
};

/*
 * Register the input handler
 */
int rawaccel_input_register(void)
{
	int ret;

	ret = input_register_handler(&rawaccel_handler);
	if (ret) {
		pr_err("rawaccel: Failed to register input handler: %d\n", ret);
		return ret;
	}

	pr_info("rawaccel: Input handler registered\n");
	return 0;
}

/*
 * Unregister the input handler
 */
void rawaccel_input_unregister(void)
{
	input_unregister_handler(&rawaccel_handler);
	pr_info("rawaccel: Input handler unregistered\n");
}
