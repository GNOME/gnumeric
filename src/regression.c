/*
 * regression.c:  Statistical regression functions.
 *
 * Authors:
 *   Morten Welinder <terra@diku.dk>
 */

#include <config.h>
#include "regression.h"
#include <glib.h>
#include <math.h>

/* ------------------------------------------------------------------------- */

static int
linear_solve (float_t **A, float_t *b, int n,
	      float_t *res)
{
	if (n < 1)
		return 1;  /* Too few points.  */

	/* Special case.  */
	if (n == 1) {
		float_t d = A[0][0];

		if (d == 0)
			return 2;  /* Singular.  */

		res[0] = b[0] / d;
		return 0;
	}

	/* Special case.  */
	if (n == 2) {
		float_t d = A[0][0] * A[1][1] - A[1][0] * A[0][1];

		if (d == 0)
			return 2;  /* Singular.  */

		res[0] = (A[1][1] * b[0] - A[1][0] * b[1]) / d;
		res[1] = (A[0][0] * b[1] - A[0][1] * b[0]) / d;
		return 0;
	}

	return 3;  /* Unimplemented.  */
}

/* ------------------------------------------------------------------------- */

static int
general_linear_regression (float_t **xss, int xdim,
			   const float_t *ys, int n,
			   float_t *res,
			   regression_stat_t *extra_stat)
{
	float_t *xTy, **xTx;
	int i;
	int err;

	if (xdim > n || n < 1)
		return 1;  /* Too few points.  */

	xTy = g_new (float_t, xdim);
	for (i = 0; i < xdim; i++) {
		const float_t *xs = xss[i];
		register float_t res = 0;
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

	xTx = g_new (float_t *, xdim);
	for (i = 0; i < xdim; i++)
		xTx[i] = g_new (float_t, xdim);

	for (i = 0; i < xdim; i++) {
		const float_t *xs1 = xss[i];
		int j;
		for (j = 0; j <= i; j++) {
			const float_t *xs2 = xss[j];
			float_t res = 0;
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

	err = linear_solve (xTx, xTy, xdim, res);

	for (i = 0; i < xdim; i++)
		g_free (xTx[i]);
	g_free (xTx);
	g_free (xTy);

	if (extra_stat) {
		extra_stat->se = g_new (float_t, xdim);

	        for (i = 0; i < xdim; i++)
		        extra_stat->se [i] = 0;

		extra_stat->sqr_r = 0;
		extra_stat->se_y = 0;
		extra_stat->F = 0;
		extra_stat->df = 0;
		extra_stat->ss_reg = 0;
		extra_stat->ss_resid = 0;
	}

	return err;
}

/* ------------------------------------------------------------------------- */
/* Please refer to description in regression.h.  */

int
linear_regression (float_t **xss, int dim,
		   const float_t *ys, int n,
		   int affine,
		   float_t *res,
		   regression_stat_t *extra_stat)
{
	int result;

	if (affine) {
		float_t **xss2;
		xss2 = g_new (float_t *, dim + 1);
		xss2[0] = NULL;  /* Substitute for 1-vector.  */
		memcpy (xss2 + 1, xss, dim * sizeof (float_t *));

		result = general_linear_regression (xss2, dim + 1, ys, n, 
						    res, extra_stat);
		g_free (xss2);
	} else {
		res[0] = 0;
		result = general_linear_regression (xss, dim, ys, n, 
						    res + 1, extra_stat);
	}

	return result;
}

/* ------------------------------------------------------------------------- */
/* Please refer to description in regression.h.  */

int
exponential_regression (float_t **xss, int dim,
			const float_t *ys, int n,
			int affine,
			float_t *res,
			regression_stat_t *extra_stat)
{
	float_t *log_ys;
	int result;
	int i;

	log_ys = g_new (float_t, n);
	for (i = 0; i < n; i++)
		if (ys[i] > 0)
			log_ys[i] = log (ys[i]);
		else {
			result = 1; /* Bad data.  */
			goto out;
		}

	if (affine) {
		float_t **xss2;
		xss2 = g_new (float_t *, dim + 1);
		xss2[0] = NULL;  /* Substitute for 1-vector.  */
		memcpy (xss2 + 1, xss, dim * sizeof (float_t *));

		result = general_linear_regression (xss2, dim + 1, log_ys,
						    n, res, extra_stat);
		g_free (xss2);
	} else {
		res[0] = 0;
		result = general_linear_regression (xss, dim, log_ys, n,
						    res + 1, extra_stat);
	}

	if (result == 0)
		for (i = 0; i < dim + 1; i++)
			res[i] = exp (res[i]);

 out:
	g_free (log_ys);
	return result;
}

/* ------------------------------------------------------------------------- */
