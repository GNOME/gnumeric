/*
 * regression.c:  Statistical regression functions.
 *
 * Authors:
 *   Morten Welinder <terra@diku.dk>
 *   Andrew Chatham <andrew.chatham@duke.edu>
 *   Daniel Carrera <dcarrera@math.toronto.edu>
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "regression.h"
#include "rangefunc.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#undef DEBUG_NEAR_SINGULAR

#define ALLOC_MATRIX(var,dim1,dim2)			\
  do { int _i, _d1, _d2;				\
       _d1 = (dim1);					\
       _d2 = (dim2);					\
       (var) = g_new (gnum_float *, _d1);		\
       for (_i = 0; _i < _d1; _i++)			\
	       (var)[_i] = g_new (gnum_float, _d2);	\
  } while (0)

#define FREE_MATRIX(var,dim1,dim2)			\
  do { int _i, _d1;					\
       _d1 = (dim1);					\
       for (_i = 0; _i < _d1; _i++)			\
	       g_free ((var)[_i]);			\
       g_free (var);					\
  } while (0)

#define COPY_MATRIX(dst,src,dim1,dim2)		\
  do { int _i, _j, _d1, _d2;			\
       _d1 = (dim1);				\
       _d2 = (dim2);				\
       for (_i = 0; _i < _d1; _i++)		\
	 for (_j = 0; _j < _d2; _j++)		\
	   (dst)[_i][_j] = (src)[_i][_j];	\
  } while (0)

#define PRINT_MATRIX(var,dim1,dim2)					\
  do {									\
	int _i, _j, _d1, _d2;						\
	_d1 = (dim1);							\
	_d2 = (dim2);							\
	for (_i = 0; _i < _d1; _i++)					\
	  {								\
	    for (_j = 0; _j < _d2; _j++)				\
	      fprintf (stderr, " %19.10" GNUM_FORMAT_g, (var)[_i][_j]);	\
	    fprintf (stderr, "\n");					\
	  }								\
  } while (0)

/*
 *       ---> j
 *
 *  |    ********
 *  |    ********
 *  |    ********        A[i][j]
 *  v    ********
 *       ********
 *  i    ********
 *       ********
 *       ********
 *
 */

/* ------------------------------------------------------------------------- */

/* Returns in res the solution to the equation L * U * res = P * b.

   This function is adapted from pseudocode in
	Introduction to Algorithms_. Cormen, Leiserson, and Rivest.
	p. 753. MIT Press, 1990.
*/
static void
backsolve (gnum_float **LU, int *P, gnum_float *b, int n, gnum_float *res)
{
	int i, j;

	for (i = 0; i < n; i++) {
		res[i] = b[P[i]];
		for (j = 0; j < i; j++)
			res[i] -= LU[i][j] * res[j];
	}

	for (i = n - 1; i >= 0; i--) {
		for (j = i + 1; j < n; j++)
			res[i] -= LU[i][j] * res[j];
		res[i] /= LU[i][i];
	}
}

static RegressionResult
rescale (gnum_float **A, gnum_float *b, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		int j, expn;
		gnum_float scale, max;

		(void)range_maxabs (A[i], n, &max);

		if (max == 0)
			return REG_singular;

		/* Use a power of 2 near sqrt (max) as scale.  */
		(void)frexpgnum (sqrtgnum (max), &expn);
		scale = ldexpgnum (1, expn);
#ifdef DEBUG_NEAR_SINGULAR
		printf ("scale[%d]=%" GNUM_FORMAT_g "\n",
			i, scale);
#endif

		b[i] /= scale;
		for (j = 0; j < n; j++)
			A[i][j] /= scale;
	}
	return REG_ok;
}


/*
 * Performs an LUP Decomposition; LU and P must already be allocated.
 * A is not destroyed.
 *
 * This function is adapted from pseudocode in
 *   _Introduction to Algorithms_. Cormen, Leiserson, and Rivest.
 *   p 759. MIT Press, 1990.
 *
 * A rescaling of rows is done and the b_scaled vector is scaled
 * accordingly.
 */
static RegressionResult
LUPDecomp (gnum_float **A, gnum_float **LU, int *P, int n, gnum_float *b_scaled)
{
	int i, j, k, tempint;
	gnum_float highest = 0;
	gnum_float lowest = GNUM_MAX;
	gnum_float cond;

	COPY_MATRIX (LU, A, n, n);
	for (j = 0; j < n; j++)
		P[j] = j;

#ifdef DEBUG_NEAR_SINGULAR
	PRINT_MATRIX (LU, n, n);
#endif
	{
		RegressionResult err = rescale (LU, b_scaled, n);
		if (err != REG_ok)
			return err;
	}

	for (i = 0; i < n; i++) {
		gnum_float max = 0;
		int mov = -1;

		for (j = i; j < n; j++)
			if (gnumabs (LU[j][i]) > max) {
				max = gnumabs (LU[j][i]);
				mov = j;
			}
#ifdef DEBUG_NEAR_SINGULAR
		PRINT_MATRIX (LU, n, n);
		printf ("max[%d]=%" GNUM_FORMAT_g " at %d\n",
			i, max, mov);
#endif
		if (max == 0)
			return REG_singular;
		if (max > highest)
			highest = max;
		if (max < lowest)
			lowest = max;
		tempint = P[i];
		P[i] = P[mov];
		P[mov] = tempint;
		/*swap the two rows */
		for (j = 0; j < n; j++) {
			gnum_float temp = LU[i][j];
			LU[i][j] = LU[mov][j];
			LU[mov][j] = temp;
		}
		for (j = i + 1; j < n; j++) {
			LU[j][i] /= LU[i][i];
			for (k = i + 1; k < n; k++)
				LU[j][k] -= LU[j][i] * LU[i][k];
		}
	}

	cond = (loggnum (highest) - loggnum (lowest)) / loggnum (2);
#ifdef DEBUG_NEAR_SINGULAR
	printf ("cond=%.20" GNUM_FORMAT_g "\n", cond);
#endif

	/* FIXME: make some science out of this.  */
	if (cond > GNUM_MANT_DIG * 0.75)
		return REG_near_singular_bad;
	else if (cond > GNUM_MANT_DIG * 0.50)
		return REG_near_singular_good;
	else
		return REG_ok;
}


static RegressionResult
linear_solve (gnum_float **A, gnum_float *b, int n, gnum_float *res)
{
	RegressionResult err;
	gnum_float **LU, *b_scaled;
	int *P;

	if (n < 1)
		return REG_not_enough_data;

	/* Special case.  */
	if (n == 1) {
		gnum_float d = A[0][0];
		if (d == 0)
			return REG_singular;

		res[0] = b[0] / d;
		return REG_ok;
	}

	/* Special case.  */
	if (n == 2) {
		gnum_float d = A[0][0] * A[1][1] - A[1][0] * A[0][1];
		if (d == 0)
			return REG_singular;

		res[0] = (A[1][1] * b[0] - A[1][0] * b[1]) / d;
		res[1] = (A[0][0] * b[1] - A[0][1] * b[0]) / d;
		return REG_ok;
	}

	/*
	 * Otherwise, use LUP-decomposition to find res such that
	 * A res = b
	 */
	ALLOC_MATRIX (LU, n, n);
	P = g_new (int, n);

	b_scaled = g_new (gnum_float, n);
	memcpy (b_scaled, b, n * sizeof (gnum_float));

	err = LUPDecomp (A, LU, P, n, b_scaled);

	if (err == REG_ok || err == REG_near_singular_good)
		backsolve (LU, P, b_scaled, n, res);

	FREE_MATRIX (LU, n, n);
	g_free (P);
	g_free (b_scaled);
	return err;
}

/* ------------------------------------------------------------------------- */

static RegressionResult
general_linear_regression (gnum_float **xss, int xdim,
			   const gnum_float *ys, int n,
			   gnum_float *result,
			   regression_stat_t *regression_stat, gboolean affine)
{
	gnum_float *xTy, **xTx;
	int i,j;
	RegressionResult regerr;

	if (regression_stat)
		memset (regression_stat, 0, sizeof (regression_stat_t));

	if (xdim > n)
		return REG_not_enough_data;

	xTy = g_new (gnum_float, xdim);
	for (i = 0; i < xdim; i++) {
		const gnum_float *xs = xss[i];
		register gnum_float res = 0;
		int j;
		if (xs == NULL)
			/* NULL represents a 1-vector.  */
			for (j = 0; j < n; j++)
				res += ys[j];
		else
			for (j = 0; j < n; j++)
				res += xs[j] * ys[j];
		xTy[i] = res;
	}

	ALLOC_MATRIX (xTx, xdim, xdim);

	for (i = 0; i < xdim; i++) {
		const gnum_float *xs1 = xss[i];
		int j;
		for (j = 0; j <= i; j++) {
			const gnum_float *xs2 = xss[j];
			gnum_float res = 0;
			int k;

			if (xs1 == NULL && xs2 == NULL)
				res = n;
			else if (xs1 == NULL)
				for (k = 0; k < n; k++)
					res += xs2[k];
			else if (xs2 == NULL)
				for (k = 0; k < n; k++)
					res += xs1[k];
			else
				for (k = 0; k < n; k++)
					res += xs1[k] * xs2[k];

			xTx[i][j] = xTx[j][i] = res;
		}
	}

	regerr = linear_solve (xTx, xTy, xdim, result);

	if (regression_stat &&
	    (regerr == REG_ok || regerr == REG_near_singular_good)) {
		RegressionResult err2;
		gnum_float *residuals = g_new (gnum_float, n);
		gnum_float **LU, *one_scaled;
		int *P;
		int err;

		/* This should not fail since n >= 1.  */
		err = range_average (ys, n, &regression_stat->ybar);
		g_assert (err == 0);

		/* FIXME: we ought to have a devsq variant that does not
		   recompute the mean.  */
		if (affine)
			err = range_devsq (ys, n, &regression_stat->ss_total);
		else
			err = range_sumsq (ys, n, &regression_stat->ss_total);
		g_assert (err == 0);

		regression_stat->xbar = g_new (gnum_float, n);
		for (i = 0; i < xdim; i++) {
			if (xss[i]) {
				int err = range_average (xss[i], n, &regression_stat->xbar[i]);
				g_assert (err == 0);
			} else {
				regression_stat->xbar[i] = 1;
			}
		}

		for (i = 0; i < n; i++) {
			residuals[i] = 0;
			for (j = 0; j < xdim; j++) {
				if (xss[j])
					residuals[i] += xss[j][i] * result[j];
				else
					residuals[i] += result[j]; /* If NULL, constant factor */
			}
			residuals[i] = ys[i] - residuals[i];
		}

		err = range_sumsq (residuals, n, &regression_stat->ss_resid);
		g_assert (err == 0);

		regression_stat->sqr_r = (regression_stat->ss_total == 0)
			? 1
			: 1 - regression_stat->ss_resid / regression_stat->ss_total;
		/* FIXME: we want to guard against division by zero.  */
		regression_stat->adj_sqr_r = 1 - regression_stat->ss_resid * (n - 1) /
			((n - xdim) * regression_stat->ss_total);
		regression_stat->var = (n == xdim)
			? 0
			: regression_stat->ss_resid / (n - xdim);

		ALLOC_MATRIX (LU, xdim, xdim);
		one_scaled = g_new (gnum_float, xdim);
		for (i = 0; i < xdim; i++) one_scaled[i] = 1;
		P = g_new (int, xdim);

		err2 = LUPDecomp (xTx, LU, P, xdim, one_scaled);
		regression_stat->se = g_new (gnum_float, xdim);
		if (err2 == REG_ok || err2 == REG_near_singular_good) {
			gnum_float *e = g_new (gnum_float, xdim); /* Elementary vector */
			gnum_float *inv = g_new (gnum_float, xdim);
			for (i = 0; i < xdim; i++)
				e[i] = 0;
			for (i = 0; i < xdim; i++) {
				e[i] = one_scaled[i];
				backsolve (LU, P, e, xdim, inv);

				if (inv[i] < 0) {
					/*
					 * If this happens, something is really
					 * wrong, numerically.
					 */
					regerr = REG_near_singular_bad;
				}
				regression_stat->se[i] =
					sqrtgnum (regression_stat->var * inv[i]);
				e[i] = 0;
			}
			g_free (e);
			g_free (inv);
		} else {
			g_assert_not_reached ();
			for (i = 0; i < xdim; i++)
				regression_stat->se[i] = 0;
		}
		FREE_MATRIX (LU, xdim, xdim);
		g_free (P);
		g_free (one_scaled);

		regression_stat->t = g_new (gnum_float, xdim);

		for (i = 0; i < xdim; i++)
			regression_stat->t[i] = (regression_stat->se[i] == 0)
				? +HUGE_VAL
				: result[i] / regression_stat->se[i];

		regression_stat->df_resid = n - xdim;
		regression_stat->df_reg = xdim - (affine ? 1 : 0);
		regression_stat->df_total = regression_stat->df_resid + regression_stat->df_reg;

		regression_stat->F = (regression_stat->sqr_r == 1)
			? HUGE_VAL
			: ((regression_stat->sqr_r / regression_stat->df_reg) /
			   (1 - regression_stat->sqr_r) * regression_stat->df_resid);

		regression_stat->ss_reg =  regression_stat->ss_total - regression_stat->ss_resid;
		regression_stat->se_y = sqrtgnum (regression_stat->ss_total / n);
		regression_stat->ms_reg = (regression_stat->df_reg == 0)
			? 0
			: regression_stat->ss_reg / regression_stat->df_reg;
		regression_stat->ms_resid = (regression_stat->df_resid == 0)
			? 0
			: regression_stat->ss_resid / regression_stat->df_resid;

		g_free (residuals);
	}

	FREE_MATRIX (xTx, xdim, xdim);
	g_free (xTy);

	return regerr;
}

/* ------------------------------------------------------------------------- */
/* Please refer to description in regression.h.  */

RegressionResult
linear_regression (gnum_float **xss, int dim,
		   const gnum_float *ys, int n,
		   gboolean affine,
		   gnum_float *res,
		   regression_stat_t *regression_stat)
{
	RegressionResult result;

	g_return_val_if_fail (dim >= 1, REG_invalid_dimensions);
	g_return_val_if_fail (n >= 1, REG_invalid_dimensions);

	if (affine) {
		gnum_float **xss2;
		xss2 = g_new (gnum_float *, dim + 1);
		xss2[0] = NULL;  /* Substitute for 1-vector.  */
		memcpy (xss2 + 1, xss, dim * sizeof (gnum_float *));

		result = general_linear_regression (xss2, dim + 1, ys, n,
						    res, regression_stat, affine);
		g_free (xss2);
	} else {
		res[0] = 0;
		result = general_linear_regression (xss, dim, ys, n,
						    res + 1, regression_stat, affine);
	}
	return result;
}

/* ------------------------------------------------------------------------- */
/* Please refer to description in regression.h.  */

RegressionResult
exponential_regression (gnum_float **xss, int dim,
			const gnum_float *ys, int n,
			gboolean affine,
			gnum_float *res,
			regression_stat_t *regression_stat)
{
	gnum_float *log_ys;
	RegressionResult result;
	int i;

	g_return_val_if_fail (dim >= 1, REG_invalid_dimensions);
	g_return_val_if_fail (n >= 1, REG_invalid_dimensions);

	log_ys = g_new (gnum_float, n);
	for (i = 0; i < n; i++)
		if (ys[i] > 0)
			log_ys[i] = loggnum (ys[i]);
		else {
			result = REG_invalid_data;
			goto out;
		}

	if (affine) {
		gnum_float **xss2;
		xss2 = g_new (gnum_float *, dim + 1);
		xss2[0] = NULL;  /* Substitute for 1-vector.  */
		memcpy (xss2 + 1, xss, dim * sizeof (gnum_float *));

		result = general_linear_regression (xss2, dim + 1, log_ys,
						    n, res, regression_stat, affine);
		g_free (xss2);
	} else {
		res[0] = 0;
		result = general_linear_regression (xss, dim, log_ys, n,
						    res + 1, regression_stat, affine);
	}

	if (result == 0)
		for (i = 0; i < dim + 1; i++)
			res[i] = expgnum (res[i]);

 out:
	g_free (log_ys);
	return result;
}

/* ------------------------------------------------------------------------- */

regression_stat_t *
regression_stat_new (void)
{
	regression_stat_t * regression_stat = g_new0 (regression_stat_t, 1);

	regression_stat->se = NULL;
	regression_stat->t = NULL;
	regression_stat->xbar = NULL;

	return regression_stat;
}

/* ------------------------------------------------------------------------- */

void
regression_stat_destroy (regression_stat_t *regression_stat)
{
	g_return_if_fail (regression_stat != NULL);

	if (regression_stat->se)
		g_free(regression_stat->se);
	if (regression_stat->t)
		g_free(regression_stat->t);
	if (regression_stat->xbar)
		g_free(regression_stat->xbar);
	g_free (regression_stat);
}

/* ------------------------------------------------------------------------- */

#define DELTA      0.01
/* FIXME:  I pulled this number out of my hat.
 * I need some testing to pick a sensible value.
 */
#define MAX_STEPS   200

/*
 * SYNOPSIS:
 *      result = derivative( f, &df, x, par, i)
 *
 * Approximates the partial derivative of a given function, at (x;params)
 * with respect to the parameter indicated by ith parameter.  The resulst
 * is stored in 'df'.
 *
 * See the header file for more information.
 */
static RegressionResult
derivative (RegressionFunction f,
	    gnum_float *df,
	    gnum_float *x, /* Only one point, not the whole data set. */
	    gnum_float *par,
	    int index)
{
	gnum_float y1, y2;
	RegressionResult result;
	gnum_float par_save = par[index];

	par[index] = par_save - DELTA;
	result = (*f) (x, par, &y1);
	if (result != REG_ok) {
		par[index] = par_save;
		return result;
	}

	par[index] = par_save + DELTA;
	result = (*f) (x, par, &y2);
	if (result != REG_ok) {
		par[index] = par_save;
		return result;
	}

#ifdef DEBUG
	printf ("y1 = %lf\n", y1);
	printf ("y2 = %lf\n", y2);
	printf ("DELTA = %lf\n",DELTA);
#endif

	*df = (y2 - y1) / (2 * DELTA);
	par[index] = par_save;
	return REG_ok;
}

/*
 * SYNOPSIS:
 *   result = chi_squared (f, xvals, par, yvals, sigmas, x_dim, &chisq)
 *
 *                            /  y  - f(x ; par) \ 2
 *            2              |    i      i        |
 *         Chi   ==   Sum (  | ------------------ |   )
 *                            \     sigma        /
 *                                       i
 *
 * sigmas  -> Measurement errors in the dataset (along the y-axis).
 *            NULL means "no errors available", so they are all set to 1.
 *
 * x_dim   -> Number of data points.
 *
 * This value is not very meaningful without the sigmas.  However, it is
 * still useful for the fit.
 */
static RegressionResult
chi_squared (RegressionFunction f,
	     gnum_float ** xvals, /* The entire data set. */
	     gnum_float *par,
	     gnum_float *yvals,   /* Ditto. */
	     gnum_float *sigmas,  /* Ditto. */
	     int x_dim,          /* Number of data points. */
	     gnum_float *chisq)   /* Chi Squared */
{
	int i;
	RegressionResult result;
	gnum_float tmp, y;
	*chisq = 0;

	for (i = 0; i < x_dim; i++) {
		result = f (xvals[i], par, &y);
		if (result != REG_ok)
			return result;

		tmp = (yvals[i] - y ) / (sigmas ? sigmas[i] : 1);

		*chisq += tmp * tmp;
	}

	return REG_ok;
}


/*
 * SYNOPSIS:
 *      result = chi_derivative (f, &dchi, xvals, par, i, yvals,
 *                               sigmas, x_dim)
 *
 * This is a simple adaptation of the derivative() function specific to
 * the Chi Squared.
 */
static RegressionResult
chi_derivative (RegressionFunction f,
		gnum_float *dchi,
		gnum_float **xvals, /* The entire data set. */
		gnum_float *par,
		int index,
		gnum_float *yvals,  /* Ditto. */
		gnum_float *sigmas, /* Ditto. */
		int x_dim)
{
	gnum_float y1, y2;
	RegressionResult result;
	gnum_float par_save = par[index];

	par[index] = par_save - DELTA;
	result = chi_squared (f, xvals, par, yvals, sigmas, x_dim, &y1);
	if (result != REG_ok) {
		par[index] = par_save;
		return result;
	}

	par[index] = par_save + DELTA;
	result = chi_squared (f, xvals, par, yvals, sigmas, x_dim, &y2);
	if (result != REG_ok) {
                par[index] = par_save;
		return result;
	}

#ifdef DEBUG
	printf ("y1 = %lf\n", y1);
	printf ("y2 = %lf\n", y2);
	printf ("DELTA = %lf\n", DELTA);
#endif

	*dchi = (y2 - y1) / (2 * DELTA);
	par[index] = par_save;
	return REG_ok;
}

/*
 * SYNOPSIS:
 *   result = coefficient_matrix (A, f, xvals, par, yvals, sigmas,
 *                                x_dim, p_dim, r)
 *
 * RETURNS:
 *   The coefficient matrix of the LM method.
 *
 * DETAIS:
 *   The coefficient matrix matrix is defined by
 *
 *            N        1      df  df
 *     A   = Sum  ( -------   --  --  ( i == j ? 1 + r : 1 ) a)
 *      ij   k=1    sigma^2   dp  dp
 *                       k      i   j
 *
 * A      -> p_dim X p_dim coefficient matrix.  MUST ALREADY BE ALLOCATED.
 *
 * sigmas -> Measurement errors in the dataset (along the y-axis).
 *           NULL means "no errors available", so they are all set to 1.
 *
 * x_dim  -> Number of data points.
 *
 * p_dim  -> Number of parameters.
 *
 * r      -> Positive constant.  It's value is altered during the LM procedure.
 */

static RegressionResult
coefficient_matrix (gnum_float **A, /* Output matrix. */
		    RegressionFunction f,
		    gnum_float **xvals, /* The entire data set. */
		    gnum_float *par,
		    gnum_float *yvals,  /* Ditto. */
		    gnum_float *sigmas, /* Ditto. */
		    int x_dim,          /* Number of data points. */
		    int p_dim,          /* Number of parameters.  */
		    gnum_float r)
{
	int i, j, k;
	RegressionResult result;
	gnum_float df_i, df_j;
	gnum_float sum, sigma;

	/* Notice that the matrix is symetric.  */
	for (i = 0; i < p_dim; i++) {
		for (j = 0; j <= i; j++) {
			sum = 0;
			for (k = 0; k < x_dim; k++) {
				result = derivative (f, &df_i, xvals[k],
						     par, i);
				if (result != REG_ok)
					return result;

				result = derivative (f, &df_j, xvals[k],
						     par, j);
				if (result != REG_ok)
					return result;

				sigma = (sigmas ? sigmas[k] : 1);

				sum += (df_i * df_j) / (sigma * sigma) *
					(i == j ? 1 + r : 1) ;
			}
			A[i][j] = A[j][i] = sum;
		}
	}

	return REG_ok;
}


/*
 * SYNOPSIS:
 *   result = parameter_errors (f, xvals, par, yvals, sigmas,
 *                              x_dim, p_dim, errors)
 *
 * Returns the errors associated with the parameters.
 * If an error is infinite, it is set to -1.
 *
 * sigmas  -> Measurement errors in the dataset (along the y-axis).
 *            NULL means "no errors available", so they are all set to 1.
 *
 * x_dim   -> Number of data points.
 *
 * p_dim   -> Number of parameters.
 *
 * errors  -> MUST ALREADY BE ALLOCATED.
 */

/* FIXME:  I am not happy with the behaviour with infinite errors.  */
static RegressionResult
parameter_errors (RegressionFunction f,
		  gnum_float **xvals, /* The entire data set. */
		  gnum_float *par,
		  gnum_float *yvals,  /* Ditto. */
		  gnum_float *sigmas, /* Ditto. */
		  int x_dim,          /* Number of data points. */
		  int p_dim,          /* Number of parameters.  */
		  gnum_float *errors)
{
	RegressionResult result;
	gnum_float **A;
	int i;

	ALLOC_MATRIX (A, p_dim, p_dim);

	result = coefficient_matrix (A, f, xvals, par, yvals, sigmas,
				     x_dim, p_dim, 0);
	if (result == REG_ok) {
		for (i = 0; i < p_dim; i++)
			/* FIXME: these were "[i][j]" which makes no sense.  */
			errors[i] = (A[i][i] != 0
				     ? 1 / sqrtgnum (A[i][i])
				     : -1);
	}

	FREE_MATRIX (A, p_dim, p_dim);
	return result;
}


/*
 * SYNOPSIS:
 *   result = non_linear_regression (f, xvals, par, yvals, sigmas,
 *                                   x_dim, p_dim, &chi, errors)
 *
 * Returns the results of the non-linear regression from the given initial
 * values.
 * The resulting parameters are placed back into 'par'.
 *
 * PARAMETERS:
 *
 * sigmas  -> Measurement errors in the dataset (along the y-axis).
 *            NULL means "no errors available", so they are all set to 1.
 *
 * x_dim   -> Number of data points.
 *
 * p_dim   -> Number of parameters.
 *
 * errors  -> MUST ALREADY BE ALLOCATED.  These are the approximated standard
 *            deviation for each parameter.
 *
 * chi     -> Chi Squared of the final result.  This value is not very
 *            meaningful without the sigmas.
 */
RegressionResult
non_linear_regression (RegressionFunction f,
		       gnum_float **xvals, /* The entire data set. */
		       gnum_float *par,
		       gnum_float *yvals,  /* Ditto. */
		       gnum_float *sigmas, /* Ditto. */
		       int x_dim,          /* Number of data points. */
		       int p_dim,          /* Number of parameters.  */
		       gnum_float *chi,
		       gnum_float *errors)
{
	gnum_float r = 0.001; /* Pick a conservative initial value. */
	gnum_float *b, **A;
	gnum_float *dpar;
	gnum_float *tmp_par;
	gnum_float chi_pre, chi_pos, dchi;
	RegressionResult result;
	int i, count;

	result = chi_squared (f, xvals, par, yvals, sigmas, x_dim, &chi_pre);
	if (result != REG_ok)
		return result;

	ALLOC_MATRIX (A, p_dim, p_dim);
	dpar    = g_new (gnum_float, p_dim);
	tmp_par = g_new (gnum_float, p_dim);
	b       = g_new (gnum_float, p_dim);
#ifdef DEBUG
	printf ("Chi Squared : %lf", chi_pre);
#endif

	for (count = 0; count < MAX_STEPS; count++) {
		for (i = 0; i < p_dim; i++) {
			/*
			 *          d Chi
			 *  b   ==  -----
			 *   k       d p
			 *              k
			 */
			result = chi_derivative (f, &dchi, xvals, par, i,
						 yvals, sigmas, x_dim);
			if (result != REG_ok)
				goto out;

			b[i] = - dchi;
		}

		result = coefficient_matrix (A, f, xvals, par, yvals,
					     sigmas, x_dim, p_dim, r);
		if (result != REG_ok)
			goto out;

		result = linear_solve (A, b, p_dim, dpar);
		if (result != REG_ok)
			goto out;

		for(i = 0; i < p_dim; i++)
			tmp_par[i] = par[i] + dpar[i];

		result = chi_squared (f, xvals, par, yvals, sigmas,
				      x_dim, &chi_pos);
		if (result != REG_ok)
			goto out;

#ifdef DEBUG
		printf ("Chi Squared : %lf", chi_pre);
		printf ("Chi Squared : %lf", chi_pos);
		printf ("r  :  %lf", r);
#endif

		if (chi_pos <= chi_pre + DELTA / 2) {
			/* There is improvement */
			r /= 10;
			par = tmp_par;

			if (gnumabs (chi_pos - chi_pre) < DELTA)
				break;

			chi_pre = chi_pos;
		} else {
			r *= 10;
		}
	}

	result = parameter_errors (f, xvals, par, yvals, sigmas,
				   x_dim, p_dim, errors);
	if (result != REG_ok)
		goto out;

	*chi = chi_pos;

 out:
	FREE_MATRIX (A, p_dim, p_dim);
	g_free (dpar);
	g_free (tmp_par);
	g_free (b);

	return result;
}
