/*
 * regression.c:  Statistical regression functions.
 *
 * Authors:
 *   Morten Welinder <terra@diku.dk>
 *   Andrew Chatham  <andrew.chatham@duke.edu>
 */

#include <config.h>
#include <glib.h>
#include <math.h>
#include "regression.h"

#define ALLOC_MATRIX(var,dim1,dim2)			\
  do { int _i, _d1, _d2;				\
       _d1 = (dim1);					\
       _d2 = (dim2);					\
       (var) = g_new (float_t *, _d1);			\
       for (_i = 0; _i < _d1; _i++)			\
	       (var)[_i] = g_new (float_t, _d2);	\
  } while (0)

#define FREE_MATRIX(var,dim1,dim2)			\
  do { int _i, _d1;					\
       _d1 = (dim1);					\
       for (_i = 0; _i < _d1; _i++)			\
	       g_free ((var)[_i]);			\
       g_free (var);					\
  } while (0)

/* ------------------------------------------------------------------------- */

/* Returns in res the solution to the equation L * U * res = P * b.

   This function is adapted from pseudocode in
	Introduction to Algorithms_. Cormen, Leiserson, and Rivest.
	p. 753. MIT Press, 1990.
*/
static void
backsolve (float_t **LU, int *P, float_t *b, int n, float_t *res)
{
	int i,j;

	for (i = 0; i < n; i++) {
		res[i] = b[P[i]];
		for (j = 0; j < i; j++)
			res[i] -= LU[j][i] * res[j];
	}

	for (i = n - 1; i >= 0; i--) {
		for (j = i + 1; j < n; j++)
			res[i] -= LU[j][i] * res[j];
		res[i] = res[i] / LU[i][i];
	}
}

/* Performs an LUP Decomposition; LU and P must already be allocated.
	 A is not destroyed

This function is adapted from pseudocode in
	_Introduction to Algorithms_. Cormen, Leiserson, and Rivest.
	p 759. MIT Press, 1990.
*/
static int
LUPDecomp (float_t **A, float_t **LU, int *P, int n)
{
	int i, j, k, tempint;
	float_t temp;

	for (j = 0; j < n; j++)
		for (i = 0; i < n; i++)
			LU[j][i] = A[j][i];
	for (j = 0; j < n; j++)
		P[j] = j;

	for (i = 0; i < n; i++) {
		float_t max = 0;
		int mov = -1;
		for (j = i; j < n; j++)
			if (abs (LU[i][j]) > max) {
				max = abs (LU[i][j]);
				mov = j;
			}
		if (max == 0) return 2;			/*all 0s; singular*/
		tempint = P[i];
		P[i] = P[mov];
		P[mov] = tempint;
		for (j = 0; j < n; j++) {		/*swap the two rows */
			temp = LU[j][i];
		  	LU[j][i] = LU[j][mov];
		  	LU[j][mov] = temp;
		}
		for (j = i + 1; j < n; j++) {
			LU[i][j] = LU[i][j] / LU[i][i];
			for (k = i + 1; k < n; k++)
				LU[k][j] = LU[k][j] - LU[i][j] * LU[k][i];
		}
	}
	return 0;
}

static int
linear_solve (float_t **A, float_t *b, int n, float_t *res)
{
	int err;
	float_t **LU;
	int *P;

	err = 0;
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

	/* Otherwise, use LUP-decomposition to find res such that
		 xTx * res = b */
	ALLOC_MATRIX (LU, n, n);
	P = g_new (int, n);

	err = LUPDecomp (A, LU, P, n);
	if (err == 0)
		backsolve (LU, P, b, n, res);

	FREE_MATRIX (LU, n, n);
	g_free (P);
	return err;
}

/* ------------------------------------------------------------------------- */

static int
general_linear_regression (float_t **xss, int xdim,
			   const float_t *ys, int n,
			   float_t *res,
			   regression_stat_t *extra_stat, int affine)
{
	float_t *xTy, **xTx;
	int i,j;
	int err;

	if (extra_stat)
		memset (extra_stat, 0, sizeof (regression_stat_t));

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

	ALLOC_MATRIX (xTx, xdim, xdim);

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

	if (extra_stat && err == 0) {
		int err2;
		float_t *residuals = g_new (float_t, n);
		float_t **LU;
		float_t ss_total = 0;
		int *P;
		float_t *e, *inv;

		extra_stat->ybar = 0;
		for (i = 0; i < n; i++)
			extra_stat->ybar += ys[i];
		extra_stat->ybar /= n;
		extra_stat->se_y = 0;
		for (i = 0; i < n; i++)
			extra_stat->se_y += (ys[i] - extra_stat->ybar)
			  * (ys[i] - extra_stat->ybar);
			/* Not actually SE till later to avoid rounding error
			   when finding R^2 */

		extra_stat->xbar = g_new (float_t, n);

		for (i = 0; i < xdim; i++) {
			extra_stat->xbar[i] = 0;
			if (xss[i]) {
				for (j = 0; j < n; j++)
					extra_stat->xbar[i] += xss[i][j];
			} else {
				extra_stat->xbar[i] = n; /* so mean is 1 */
			}
			extra_stat->xbar[i] /= n;
		}

		for (i = 0; i < n; i++) {
			residuals[i] = 0;
			for (j = 0; j < xdim; j++) {
				if (xss[j])
					residuals[i] += xss[j][i] * res[j];
				else
					residuals[i] += res[j]; /* If NULL, constant factor */
			}
			residuals[i] = ys[i] - residuals[i];
		}
		extra_stat->ss_resid = 0;
		for (i = 0; i < n; i++) extra_stat->ss_resid += residuals[i] * residuals[i];

		/* FIXME: we want to guard against division by zero.  */
		extra_stat->sqr_r = 1 - (extra_stat->ss_resid / extra_stat->se_y);
		extra_stat->adj_sqr_r = 1 - (extra_stat->ss_resid / (n - xdim)) / (extra_stat->se_y / (n - 1));
		extra_stat->var = (extra_stat->ss_resid / (n - xdim));

		ALLOC_MATRIX (LU, xdim, xdim);
		P = g_new (int, n);

		err2 = LUPDecomp (xTx, LU, P, xdim);
		if (err2 == 0) {
			extra_stat->se = g_new (float_t, xdim);
			e = g_new (float_t, xdim); /* Elmentary vector */
			inv = g_new (float_t, xdim);
			for (i = 0; i < xdim; i++)
				e[i] = 0;
			for (i = 0; i < xdim; i++) {
				e[i] = 1;
				backsolve (LU, P, e, xdim, inv);
				extra_stat->se[i] = sqrt (extra_stat->var * inv[i]);
				e[i] = 0;
		 	}
		 	g_free (e);
		  	g_free (inv);
		}
		FREE_MATRIX (LU, xdim, xdim);
		g_free (P);

		extra_stat->t  = g_new (float_t, xdim);

		for (i = 0; i < xdim; i++)
			extra_stat->t[i] = res[i] / extra_stat->se[i];

		extra_stat->F = (extra_stat->sqr_r / (xdim - affine)) /
		  		((1 - extra_stat->sqr_r) / (n - xdim));

		extra_stat->df = n-xdim;
		for (i = 0; i < n; i++)
			ss_total += (ys[i] - extra_stat->ybar) *
				    (ys[i] - extra_stat->ybar);
		extra_stat->ss_reg = ss_total - extra_stat->ss_resid;
		extra_stat->se_y = sqrt (extra_stat->se_y / n); /* Now it is SE */
		g_free (residuals);
	}

	FREE_MATRIX (xTx, xdim, xdim);
	g_free (xTy);

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
						    res, extra_stat, affine);
		g_free (xss2);
	} else {
		res[0] = 0;
		result = general_linear_regression (xss, dim, ys, n,
						    res + 1, extra_stat, affine);
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
						    n, res, extra_stat, affine);
		g_free (xss2);
	} else {
		res[0] = 0;
		result = general_linear_regression (xss, dim, log_ys, n,
						    res + 1, extra_stat, affine);
	}

	if (result == 0)
		for (i = 0; i < dim + 1; i++)
			res[i] = exp (res[i]);

 out:
	g_free (log_ys);
	return result;
}

/* ------------------------------------------------------------------------- */
