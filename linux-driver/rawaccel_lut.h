/*
 * RawAccel LUT Lookup Implementation
 * Binary search + linear interpolation for acceleration lookup
 * Header-only for performance (inline functions)
 */

#ifndef RAWACCEL_LUT_H
#define RAWACCEL_LUT_H

#include <linux/kernel.h>
#include "rawaccel_types.h"

/*
 * Fixed-point math helpers (16.16 format)
 */

static inline fp16_t fp16_from_int(int x)
{
	return x << FP16_SHIFT;
}

static inline int fp16_to_int(fp16_t x)
{
	return x >> FP16_SHIFT;
}

static inline fp16_t fp16_abs(fp16_t x)
{
	return x < 0 ? -x : x;
}

static inline fp16_t fp16_mul(fp16_t a, fp16_t b)
{
	return (fp16_t)(((s64)a * b) >> FP16_SHIFT);
}

static inline fp16_t fp16_div(fp16_t a, fp16_t b)
{
	if (unlikely(b == 0))
		return 0;
	return (fp16_t)(((s64)a << FP16_SHIFT) / b);
}

/*
 * lut_lookup - Look up acceleration multiplier from pre-computed LUT
 * @speed_fp: Input speed in 16.16 fixed-point
 * @lut: Pre-computed lookup table from backend
 *
 * Performs binary search to find bracketing points, then linear interpolation.
 * Returns: Acceleration multiplier in 16.16 fixed-point
 */
static inline fp16_t lut_lookup(fp16_t speed_fp, const struct lut_data *lut)
{
	const struct lut_point *p = lut->points;
	int size = lut->size;
	int lo, hi, mid;
	fp16_t t, y;

	/* Edge cases */
	if (unlikely(speed_fp <= 0))
		return FP16_ONE;
	if (unlikely(size < 2))
		return FP16_ONE;

	/* Check if below first point */
	if (speed_fp <= p[0].x_fp) {
		y = p[0].y_fp;
		if (lut->velocity_mode)
			return fp16_div(y, p[0].x_fp);
		return y;
	}

	/* Check if above last point */
	if (speed_fp >= p[size - 1].x_fp) {
		y = p[size - 1].y_fp;
		if (lut->velocity_mode)
			return fp16_div(y, speed_fp);
		return y;
	}

	/* Binary search for bracketing interval */
	lo = 0;
	hi = size - 1;

	while (lo < hi - 1) {
		mid = (lo + hi) >> 1;

		if (speed_fp < p[mid].x_fp)
			hi = mid;
		else if (speed_fp > p[mid].x_fp)
			lo = mid;
		else {
			/* Exact match */
			y = p[mid].y_fp;
			if (lut->velocity_mode)
				return fp16_div(y, speed_fp);
			return y;
		}
	}

	/* Linear interpolation: y = a.y + t * (b.y - a.y) */
	/* t = (speed - a.x) / (b.x - a.x) */
	t = fp16_div(speed_fp - p[lo].x_fp, p[hi].x_fp - p[lo].x_fp);
	y = p[lo].y_fp + fp16_mul(t, p[hi].y_fp - p[lo].y_fp);

	/* Apply velocity mode if enabled */
	if (lut->velocity_mode)
		y = fp16_div(y, speed_fp);

	return y;
}

/*
 * calculate_speed - Calculate Euclidean speed from dx/dy
 * @dx_fp: X displacement in 16.16 fixed-point
 * @dy_fp: Y displacement in 16.16 fixed-point
 *
 * Uses integer square root for performance.
 * Returns: Speed in 16.16 fixed-point
 */
static inline fp16_t calculate_speed(fp16_t dx_fp, fp16_t dy_fp)
{
	s64 dx_sq, dy_sq, sum;
	s64 result, guess, new_guess;
	int i;

	/* Calculate dx^2 + dy^2 */
	dx_sq = (s64)dx_fp * dx_fp;
	dy_sq = (s64)dy_fp * dy_fp;
	sum = (dx_sq + dy_sq) >> FP16_SHIFT;

	if (sum <= 0)
		return 0;

	/* Newton's method for integer square root */
	guess = sum >> 1;
	if (guess == 0)
		guess = 1;

	/* 10 iterations is enough for convergence */
	for (i = 0; i < 10; i++) {
		new_guess = (guess + sum / guess) >> 1;
		if (new_guess >= guess)
			break;
		guess = new_guess;
	}

	result = guess;
	return (fp16_t)result;
}

#endif /* RAWACCEL_LUT_H */
