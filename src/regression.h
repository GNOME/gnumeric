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

/**
 * logarithmic_regression:
 * @xss: x-vectors.  (Ie., independent data.)
 * @dim: number of x-vectors.
 * @ys: y-vector.  (Dependent data.)
 * @n: number of data points.
 * @affine: if true, a non-zero constant is allowed.
 * @res: output place for constant[0] and factor1[1], factor2[2], ...
 *   There will be dim+1 results.
 *
 * This is almost a copy of linear_regression and produces multi-dimensional
 * linear regressions on the input points after transforming xss to ln(xss).
 * Fits to "y = b + a1 * z1 + ... ad * zd" with "zi = ln (xi)".
 * Problems with arrays in the calling function: see comment to
 * gnumeric_linest, which is also valid for gnumeric_logreg.
 *
 * Returns RegressionResult as above.  (Errors: less than two points,
 * all points on a vertical line, non-positive x data.)
 */

RegressionResult logarithmic_regression (gnum_float **xss, int dim,
					 const gnum_float *ys, int n,
					 gboolean affine,
					 gnum_float *res,
					 regression_stat_t *stat);

/**
 * logarithmic_fit:
 * @xs: x-vector.  (Ie., independent data.)
 * @ys: y-vector.  (Dependent data.)
 * @n: number of data points.
 * @res: output place for sign[0], a[1], b[2], c[3], and
 * sum of squared residuals[4].
 *
 * This performs a two-dimensional non-linear fitting on the input points.
 * Fits to "y = a + b * ln (sign * (x - c))", with sign in {-1, +1}.
 * The graph is a logarithmic curve moved horizontally by c and possibly
 * mirrored across the y-axis (if sign = -1).
 *
 * Returns RegressionResult as above.
 * (Requires: at least 3 different x values, at least 3 different y values.)
 *
 * Fits c (and sign) by iterative trials, but seems to be fast enough even
 * for automatic recomputation.
 *
 * Adapts c until a local minimum of squared residuals is reached. For each
 * new c tried out the corresponding a and b are calculated by linear
 * regression. If no local minimum is found, an error is returned. If there
 * is more than one local minimum, the one found is not necessarily the
 * smallest (i.e., there might be cases in which the returned fit is not the
 * best possible). If the shape of the point cloud is to different from
 * ``logarithmic'', either sign can not be determined (error returned) or no
 * local minimum will be found.
 */

/* Final accuracy of c is: width of x-range rounded to the next smaller
 * (10^integer), the result times LOGFIT_C_ACCURACY.
 * If you change it, remember to change the help-text for LOGFIT. 
 * FIXME: Is there a way to stringify this macros value for the help-text? */
#define LOGFIT_C_ACCURACY 0.000001

/* Stepwidth for testing for sign is: width of x-range 
 * times LOGFIT_C_STEP_FACTOR. Value is tested a bit. */
#define LOGFIT_C_STEP_FACTOR 0.05

/* Width of fitted c-range is: width of x-range
 * times LOGFIT_C_RANGE_FACTOR. Value is tested a bit.
 * Point clouds with a local minimum of squared residuals outside the fitted
 * c-range are very weakly bent. */
#define LOGFIT_C_RANGE_FACTOR 100

RegressionResult logarithmic_fit (gnum_float *xs,
				  const gnum_float *ys, int n,
				  gnum_float *res);



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
