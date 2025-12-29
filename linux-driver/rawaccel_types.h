/*
 * RawAccel Data Structures
 * LUT-based acceleration types using 16.16 fixed-point arithmetic
 */

#ifndef RAWACCEL_TYPES_H
#define RAWACCEL_TYPES_H

#include <linux/types.h>
#include <linux/list.h>

/* Fixed-point arithmetic (16.16 format) */
typedef s32 fp16_t;

#define FP16_SHIFT 16
#define FP16_ONE   (1 << FP16_SHIFT)

/* Maximum LUT points supported */
#define LUT_MAX_POINTS 129

/* Maximum devices we can track */
#define MAX_DEVICES 16

/*
 * LUT point structure
 * Stores a single (speed, multiplier) pair in fixed-point format
 */
struct lut_point {
	s32 x_fp;    /* Input speed (16.16 fixed-point) */
	s32 y_fp;    /* Output multiplier (16.16 fixed-point) */
};

/*
 * LUT data structure
 * Contains pre-computed acceleration curve from userspace backend
 */
struct lut_data {
	struct lut_point points[LUT_MAX_POINTS];  /* Array of LUT points */
	u8 size;                                   /* Number of valid points (2-129) */
	bool velocity_mode;                        /* If true, divide result by speed */
};

/*
 * Per-device context
 * Stores acceleration state for each mouse device
 */
struct rawaccel_dev {
	/* List linkage */
	struct list_head list;

	/* Device identification */
	char name[64];
	char phys[64];

	/* Acceleration settings */
	struct lut_data lut_x;
	struct lut_data lut_y;
	bool separate_axes;

	/* Configuration */
	bool enabled;
	int dpi;
	fp16_t dpi_factor_fp;

	/* Carry-over state for sub-pixel precision */
	s64 carry_x_fp;
	s64 carry_y_fp;

	/* Timing */
	u64 last_event_ns;

	/* Pending events (buffered until SYN_REPORT) */
	int pending_x;
	int pending_y;
	int out_x;
	int out_y;
	bool has_x;
	bool has_y;
};

#endif /* RAWACCEL_TYPES_H */
