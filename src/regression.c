/*
 * regression.c:  Statistical regression functions.
 *
 * Authors:
 *   Morten Welinder <terra@diku.dk>
 *   Andrew Chatham  <andrew.chatham@duke.edu>
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "regression.h"
#include "rangefunc.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

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

#define PRINT_MATRIX(var,dim1,dim2)					\
  do {									\
	int _i, _j, _d1, _d2;						\
	_d1 = (dim1);							\
	_d2 = (dim2);							\
	for (_i = 0; _i < _d1; _i++)					\
	  {								\
	    for (_j = 0; _j < _d2; _j++)				\
	      fprintf (stderr, "%20.10" GNUM_FORMAT_f, var[_i][_j]);	\
	    fprintf (stderr, "\n");					\
	  }								\
  } while (0)

/* ------------------------------------------------------------------------- */

/* Returns in res the solution to the equation L * U * res = P * b.

   This function is adapted from pseudocode in
	Introduction to Algorithms_. Cormen, Leiserson, and Rivest.
	p. 753. MIT Press, 1990.
*/
static void
backsolve (gnum_float **LU, int *P, gnum_float *b, int n, gnum_float *res)
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
LUPDecomp (gnum_float **A, gnum_float **LU, int *P, int n)
{
	int i, j, k, tempint;

	for (j = 0; j < n; j++)
		for (i = 0; i < n; i++)
			LU[j][i] = A[j][i];
	for (j = 0; j < n; j++)
		P[j] = j;

	for (i = 0; i < n; i++) {
		gnum_float max = 0;
		int mov = -1;

		for (j = i; j < n; j++)
			if (fabs (LU[i][j]) > max) {
				max = fabs (LU[i][j]);
				mov = j;
			}
		if (max == 0) return 2;			/*all 0s; singular*/
		tempint = P[i];
		P[i] = P[mov];
		P[mov] = tempint;
		/* FIXME: there's some serious row/col confusion going on here.  */
		for (j = 0; j < n; j++) {		/*swap the two rows */
			gnum_float temp = LU[j][i];
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
linear_solve (gnum_float **A, gnum_float *b, int n, gnum_float *res)
{
	int err;
	gnum_float **LU;
	int *P;

	err = 0;
	if (n < 1)
		return 1;  /* Too few points.  */

	/* Special case.  */
	if (n == 1) {
		gnum_float d = A[0][0];

		if (d == 0)
			return 2;  /* Singular.  */

		res[0] = b[0] / d;
		return 0;
	}

	/* Special case.  */
	if (n == 2) {
		gnum_float d = A[0][0] * A[1][1] - A[1][0] * A[0][1];
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
general_linear_regression (gnum_float **xss, int xdim,
			   const gnum_float *ys, int n,
			   gnum_float *res,
			   regression_stat_t *regression_stat, int affine)
{
	gnum_float *xTy, **xTx;
	int i,j;
	int err;

	if (regression_stat)
		memset (regression_stat, 0, sizeof (regression_stat_t));

	if (xdim > n || n < 1)
		return 1;  /* Too few points.  */

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

	err = linear_solve (xTx, xTy, xdim, res);

	if (regression_stat && err == 0) {
		int err2;
		gnum_float *residuals = g_new (gnum_float, n);
		gnum_float **LU;
		int *P;
		gnum_float *e, *inv;
		gnum_float ybar;
		int err;

		/* This should not fail since n >= 1.  */
		err = range_average (ys, n, &ybar);
		g_assert (err == 0);
		regression_stat->ybar = ybar;

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
					residuals[i] += xss[j][i] * res[j];
				else
					residuals[i] += res[j]; /* If NULL, constant factor */
			}
			residuals[i] = ys[i] - residuals[i];
		}

		err = range_sumsq (residuals, n, &regression_stat->ss_resid);
		g_assert (err == 0);

		/* FIXME: we want to guard against division by zero.  */
		regression_stat->sqr_r = 1 - (regression_stat->ss_resid / regression_stat->ss_total);
		regression_stat->adj_sqr_r = 1 - regression_stat->ss_resid * (n - 1) / 
			((n - xdim) * regression_stat->ss_total);
		regression_stat->var = (regression_stat->ss_resid / (n - xdim));

		ALLOC_MATRIX (LU, xdim, xdim);
		P = g_new (int, n);

		err2 = LUPDecomp (xTx, LU, P, xdim);
		regression_stat->se = g_new (gnum_float, xdim);
		if (err2 == 0) {
			e = g_new (gnum_float, xdim); /* Elmentary vector */
			inv = g_new (gnum_float, xdim);
			for (i = 0; i < xdim; i++)
				e[i] = 0;
			for (i = 0; i < xdim; i++) {
				e[i] = 1;
				backsolve (LU, P, e, xdim, inv);
				regression_stat->se[i] = sqrt (regression_stat->var * inv[i]);
				e[i] = 0;
			}
			g_free (e);
			g_free (inv);
		} else {
			/* FIXME: got any better idea?  */
			for (i = 0; i < xdim; i++)
				regression_stat->se[i] = 1;
		}
		FREE_MATRIX (LU, xdim, xdim);
		g_free (P);

		regression_stat->t = g_new (gnum_float, xdim);

		for (i = 0; i < xdim; i++)
			regression_stat->t[i] = res[i] / regression_stat->se[i];

		regression_stat->df_resid = n - xdim;
		regression_stat->df_reg = xdim - (affine ? 1 : 0);
		regression_stat->df_total = regression_stat->df_resid + regression_stat->df_reg;

		regression_stat->F = (regression_stat->sqr_r / regression_stat->df_reg) /
			((1 - regression_stat->sqr_r) / regression_stat->df_resid);

		regression_stat->ss_reg =  regression_stat->ss_total - regression_stat->ss_resid;
		regression_stat->se_y = sqrt (regression_stat->ss_total / n);
		regression_stat->ms_reg = regression_stat->ss_reg / regression_stat->df_reg;
		regression_stat->ms_resid = regression_stat->ss_resid / regression_stat->df_resid;

		g_free (residuals);
	}

	FREE_MATRIX (xTx, xdim, xdim);
	g_free (xTy);

	return err;
}

/* ------------------------------------------------------------------------- */
/* Please refer to description in regression.h.  */

int
linear_regression (gnum_float **xss, int dim,
		   const gnum_float *ys, int n,
		   int affine,
		   gnum_float *res,
		   regression_stat_t *regression_stat)
{
	int result;

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

int
exponential_regression (gnum_float **xss, int dim,
			const gnum_float *ys, int n,
			int affine,
			gnum_float *res,
			regression_stat_t *regression_stat)
{
	gnum_float *log_ys;
	int result;
	int i;

	log_ys = g_new (gnum_float, n);
	for (i = 0; i < n; i++)
		if (ys[i] > 0)
			log_ys[i] = log (ys[i]);
		else {
			result = 1; /* Bad data.  */
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
			res[i] = exp (res[i]);

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

