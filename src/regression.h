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
        float_t *se; /*SE for each parameter estimator*/
        float_t *t;  /*t values for each parameter estimator*/
        float_t sqr_r;
	float_t adj_sqr_r;
        float_t se_y; /* The Standard Error of Y */
        float_t F;
        int     df;
        float_t ss_reg;
        float_t ss_resid;
	float_t ybar;
	float_t *xbar;
	float_t var; /* The variance of the entire regression: 
			sum(errors^2)/(n-xdim) */
} regression_stat_t;

int linear_regression (float_t **xss, int dim,
		       const float_t *ys, int n,
		       int affine,
		       float_t *res, regression_stat_t *stat);


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

int exponential_regression (float_t **xss, int dim,
			    const float_t *ys, int n,
			    int affine,
			    float_t *res, regression_stat_t *stat);

#endif
