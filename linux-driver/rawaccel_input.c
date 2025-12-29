/*
 * RawAccel Input Handler Implementation
 * Intercepts and processes mouse input events
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/ktime.h>

#include "rawaccel_input.h"
#include "rawaccel_types.h"
#include "rawaccel_lut.h"

/* Input handler instance */
static struct input_handler rawaccel_handler;

/*
 * Process buffered movement and apply acceleration
 */
static void rawaccel_process(struct rawaccel_dev *dev)
{
	fp16_t dx_fp, dy_fp, speed_fp, multiplier;
	int out_x, out_y;

	/* Convert to fixed-point */
	dx_fp = fp16_from_int(dev->pending_x);
	dy_fp = fp16_from_int(dev->pending_y);

	/* Calculate speed (Euclidean distance) */
	speed_fp = calculate_speed(dx_fp, dy_fp);

	/* Look up acceleration multiplier from LUT */
	/* For now, use a simple 1:1 multiplier (no acceleration) */
	/* TODO: Use actual LUT when configuration is implemented */
	multiplier = FP16_ONE;

	/* Apply multiplier */
	dx_fp = fp16_mul(dx_fp, multiplier);
	dy_fp = fp16_mul(dy_fp, multiplier);

	/* Convert back to integer */
	out_x = fp16_to_int(dx_fp);
	out_y = fp16_to_int(dy_fp);

	/* Store output */
	dev->out_x = out_x;
	dev->out_y = out_y;
}

/*
 * Event handler - called for every input event
 * Buffers REL_X and REL_Y, processes on SYN_REPORT
 */
static void rawaccel_event(struct input_handle *handle,
                           unsigned int type,
                           unsigned int code,
                           int value)
{
	struct rawaccel_dev *dev = handle->private;

	/* Only process if we have per-device context */
	if (!dev)
		return;

	/* Buffer relative movement events */
	if (type == EV_REL) {
		switch (code) {
		case REL_X:
			dev->pending_x = value;
			dev->has_x = true;
			return;  /* Don't forward yet */
		case REL_Y:
			dev->pending_y = value;
			dev->has_y = true;
			return;  /* Don't forward yet */
		}
	}

	/* Process buffered movement on sync */
	if (type == EV_SYN && code == SYN_REPORT) {
		if (dev->has_x || dev->has_y) {
			/* Apply acceleration */
			rawaccel_process(dev);

			/* Forward modified REL_X if we have it */
			if (dev->has_x) {
				input_event(handle->dev, EV_REL, REL_X, dev->out_x);
				dev->has_x = false;
			}

			/* Forward modified REL_Y if we have it */
			if (dev->has_y) {
				input_event(handle->dev, EV_REL, REL_Y, dev->out_y);
				dev->has_y = false;
			}

			/* Reset pending values */
			dev->pending_x = 0;
			dev->pending_y = 0;
		}
	}

	/* Forward all other events unchanged */
	input_event(handle->dev, type, code, value);
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
	struct rawaccel_dev *ra_dev;
	int ret;

	/* Only connect to devices with relative axes (mice) */
	if (!test_bit(EV_REL, dev->evbit))
		return -ENODEV;

	/* Allocate handle */
	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	/* Allocate per-device context */
	ra_dev = kzalloc(sizeof(*ra_dev), GFP_KERNEL);
	if (!ra_dev) {
		ret = -ENOMEM;
		goto err_free_handle;
	}

	/* Initialize device context */
	strscpy(ra_dev->name, dev->name, sizeof(ra_dev->name));
	if (dev->phys)
		strscpy(ra_dev->phys, dev->phys, sizeof(ra_dev->phys));

	ra_dev->enabled = true;
	ra_dev->dpi = 1000;  /* Default DPI */
	ra_dev->dpi_factor_fp = FP16_ONE;
	ra_dev->separate_axes = false;

	/* Initialize LUT with default (no acceleration) */
	ra_dev->lut_x.size = 2;
	ra_dev->lut_x.points[0].x_fp = fp16_from_int(0);
	ra_dev->lut_x.points[0].y_fp = FP16_ONE;
	ra_dev->lut_x.points[1].x_fp = fp16_from_int(100);
	ra_dev->lut_x.points[1].y_fp = FP16_ONE;
	ra_dev->lut_x.velocity_mode = false;

	/* Same for Y axis */
	ra_dev->lut_y = ra_dev->lut_x;

	/* Setup handle */
	handle->dev = dev;
	handle->handler = handler;
	handle->name = "rawaccel";
	handle->private = ra_dev;

	/* Register the handle */
	ret = input_register_handle(handle);
	if (ret) {
		pr_err("rawaccel: Failed to register handle for %s: %d\n",
		       dev->name, ret);
		goto err_free_dev;
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
err_free_dev:
	kfree(ra_dev);
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
	struct rawaccel_dev *ra_dev = handle->private;

	pr_info("rawaccel: Disconnecting from device: %s\n",
	        handle->dev->name);

	input_close_device(handle);
	input_unregister_handle(handle);

	/* Free per-device context */
	kfree(ra_dev);
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
