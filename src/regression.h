#ifndef GNUMERIC_REGRESSION_H
#define GNUMERIC_REGRESSION_H

#include "numbers.h"


typedef enum {
	REG_ok,
	REG_invalid_dimensions,
	REG_invalid_data,
	REG_not_enough_data,
	REG_near_singular_good, /* Probably good result */
	REG_near_singular_bad, /* Probably bad result */
	REG_singular
} RegressionResult;


/**
 * linear_regression:
 * @xss: x-vectors.  (I.e., independent data.)
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
 * Returns RegressionResult as above.
 */

typedef struct {
        gnum_float *se; /*SE for each parameter estimator*/
        gnum_float *t;  /*t values for each parameter estimator*/
        gnum_float sqr_r;
	gnum_float adj_sqr_r;
        gnum_float se_y; /* The Standard Error of Y */
        gnum_float F;
        int        df_reg;
        int        df_resid;
        int        df_total;
        gnum_float ss_reg;
        gnum_float ss_resid;
        gnum_float ss_total;
        gnum_float ms_reg;
        gnum_float ms_resid;
	gnum_float ybar;
	gnum_float *xbar;
	gnum_float var; /* The variance of the entire regression:
			sum(errors^2)/(n-xdim) */
} regression_stat_t;

regression_stat_t * regression_stat_new (void);
void regression_stat_destroy (regression_stat_t *regression_stat);

RegressionResult linear_regression (gnum_float **xss, int dim,
				    const gnum_float *ys, int n,
				    gboolean affine,
				    gnum_float *res,
				    regression_stat_t *stat);


/**
 * exponential_regression:
 * @xss: x-vectors.  (I.e., independent data.)
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
 * Returns RegressionResult as above.
 */

RegressionResult exponential_regression (gnum_float **xss, int dim,
					 const gnum_float *ys, int n,
					 gboolean affine,
					 gnum_float *res,
					 regression_stat_t *stat);



typedef RegressionResult (*RegressionFunction)
    (gnum_float * x, gnum_float * params, gnum_float *f);


RegressionResult non_linear_regression (RegressionFunction f,
					gnum_float **xvals,
					gnum_float *par,
					gnum_float *yvals,
					gnum_float *sigmas,
					int x_dim,
					int p_dim,
					gnum_float *chi,
					gnum_float *errors);

#endif
