#ifndef GNUMERIC_REGRESSION_H
#define GNUMERIC_REGRESSION_H

#include "numbers.h"

/**
 * linear_regression:
 * @xs: x-vector.  (Ie., independent data.)
 * @ys: y-vector.  (Dependent data.)
 * @n: number of data points.
 * @affine: if true, a non-zero constant is allowed.
 * @res: output place for constant[0] and slope[1].
 *
 * This performs one-dimensional linear regressions on the input points.
 * Fits to "y = ax + b".
 *
 * Returns 0 for ok, non-zero otherwise.  (Errors: less than two points,
 * all points on a vertical line.)
 */

int linear_regression (const float_t *xs, const float_t *ys, int n,
		       int affine,
		       float_t *res);


/**
 * exponential_regression:
 * @xs: x-vector.  (Ie., independent data.)
 * @ys: y-vector.  (Dependent data.)
 * @n: number of data points.
 * @affine: if true, a non-one multiplier is allowed.
 * @res: output place for constant[0] and root[1].
 *
 * This performs one-dimensional linear regressions on the input points.
 * Fits to "y = b * m^x" or equivalently to "log y = log b + x * log m".
 *
 * Returns 0 for ok, non-zero otherwise.  (Errors: less than two points,
 * all points on a vertical line, non-positive y data.)
 */

int exponential_regression (const float_t *xs, const float_t *ys, int n,
			    int affine,
			    float_t *res);

#endif
