#ifndef GNUMERIC_REGRESSION_H
#define GNUMERIC_REGRESSION_H

#include "numbers.h"

/**
 * linear_regression:
 * @xss: x-vectors.  (Ie., independent data.)
 * @dim: number of x-vectors.
 * @ys: y-vector.  (Dependent data.)
 * @n: number of data points.
 * @affine: if true, a non-zero constant is allowed.
 * @res: output place for constant[0] and slope1[1], slope2[2], ...
 *   There will be dim+1 results.
 *
 * This performs multi-dimensional linear regressions on the input points.
 * Fits to "y = b + a1 * x1 + ... ad * xd".
 *
 * Returns 0 for ok, non-zero otherwise.  (Errors: less than two points,
 * all points on a vertical line.)
 */

typedef struct {
        gnum_float *se; /*SE for each parameter estimator*/
        gnum_float *t;  /*t values for each parameter estimator*/
        gnum_float sqr_r;
	gnum_float adj_sqr_r;
        gnum_float se_y; /* The Standard Error of Y */
        gnum_float F;
        int     df;
        gnum_float ss_reg;
        gnum_float ss_resid;
	gnum_float ybar;
	gnum_float *xbar;
	gnum_float var; /* The variance of the entire regression:
			sum(errors^2)/(n-xdim) */
} regression_stat_t;

int linear_regression (gnum_float **xss, int dim,
		       const gnum_float *ys, int n,
		       int affine,
		       gnum_float *res, regression_stat_t *stat);


/**
 * exponential_regression:
 * @xss: x-vectors.  (Ie., independent data.)
 * @dim: number of x-vectors.
 * @ys: y-vector.  (Dependent data.)
 * @n: number of data points.
 * @affine: if true, a non-one multiplier is allowed.
 * @res: output place for constant[0] and root1[1], root2[2], ...
 *   There will be dim+1 results.
 *
 * This performs one-dimensional linear regressions on the input points.
 * Fits to "y = b * m1^x1 * ... * md^xd " or equivalently to
 * "log y = log b + x1 * log m1 + ... + xd * log md".
 *
 * Returns 0 for ok, non-zero otherwise.  (Errors: less than two points,
 * all points on a vertical line, non-positive y data.)
 */

int exponential_regression (gnum_float **xss, int dim,
			    const gnum_float *ys, int n,
			    int affine,
			    gnum_float *res, regression_stat_t *stat);

#endif
