/*
 * regression.c:  Statistical regression functions.
 *
 * Authors:
 *   Morten Welinder <terra@diku.dk>
 *   Andrew Chatham  <andrew.chatham@duke.edu>
 */

#include <config.h>
#include "regression.h"
#include <glib.h>
#include <math.h>


/* ------------------------------------------------------------------------- */

/* Returns in res the solution to the equation L * U * res = b.  */
static int
backsolve (float_t **L, float_t **U, float_t *b, int n, float_t *res)
{
	int i, j;
	float_t subtotal;
	float_t *y = g_new (float_t, n);

	for (i = 0; i < n; i++) {
		subtotal = 0;
		for (j = 0; j < i; j++)
			subtotal +=L [j][i] * y[j];
		y[i] = b[i] - subtotal;
	}

	for (i = n - 1; i >= 0; i--) {
		subtotal = 0;
		for (j = i + 1; j < n; j++)
			subtotal += U[j][i] * res[j];
		res[i] = (y[i] - subtotal) / U[i][i];
	}

	g_free(y);
	return 0;
}

/* Performs an LU Decomposition; L and U must already be allocated.
   A is not destroyed.  */
static int
LUDecomp (float_t **A, float_t **L, float_t **U, int n)
{
	int i,j,k,err;
	float_t **B;
	err=0;

	B = g_new (float_t *, n);
	for (i = 0; i < n; i++)
		B[i] = g_new (float_t, n);
	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
			B[i][j] = A[i][j];

	for (k = 0; k < n; k++) {
		U[k][k] = B[k][k];

		/* FIXME: this isn't right.  The matrix is only singular if
		   all rows below also have zero in this column.  */
		if (U[k][k] == 0) {
			err=2; /* singular; should be SDF Matrix */
			break;
		}
		for (i = k; i < n; i++) {
			L[k][i] = B[k][i] / U[k][k];
			U[i][k] = B[i][k];
		}
		L[k][k] = 1;
		for (i = k + 1; i < n; i++){
			for (j = k + 1; j < n; j++){
				B[j][i] = B[j][i] - L[k][i] * U[j][k];
			}
		}
	}
	for (i = 0; i < n; i++)
		g_free (B[i]);
	g_free (B);
	return err;
}

static int
linear_solve (float_t **A, float_t *b, int n,
	      float_t *res)
{
	int i,err;
	float_t **L, **U;

	err=0;
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

	/* Otherwise, use LU-decomposition to find res such that
	   xTx * res = b. */

	L = g_new (float_t *, n);
	U = g_new (float_t *, n);
	for (i = 0; i < n; i++) {
		L[i] = g_new (float_t, n);
		U[i] = g_new (float_t, n);
	}

	err = LUDecomp (A, L, U, n);
	if (err == 0)
		backsolve (L, U, b, n, res);

	for (i = 0; i < n; i++) {
		g_free (L[i]);
		g_free (U[i]);
	}
	g_free (L);
	g_free (U);
	return err;
}

/* ------------------------------------------------------------------------- */

static int
general_linear_regression (float_t **xss, int xdim,
			   const float_t *ys, int n,
			   float_t *res,
			   regression_stat_t *extra_stat)
{
	float_t *xTy, **xTx;
	int i,j;
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

	if (extra_stat) {
	        int err2;
	        float_t *residuals = g_new (float_t, n);
		float_t **L,**U;
		float_t *e, *inv;

		extra_stat->ybar=0;
		for (i=0; i<n; i++) extra_stat->ybar+=ys[i];
		extra_stat->ybar/=n;
		extra_stat->se_y=0;
		for (i=0; i<n; i++) extra_stat->se_y+=(ys[i]-extra_stat->ybar)
				      *(ys[i]-extra_stat->ybar); /* Not actually SE till later to
								    avoid rounding error when finding
								    R^2 */

		extra_stat->xbar = g_new (float_t, n);

		for (i=0; i<xdim; i++){
		  extra_stat->xbar[i]=0;
		  if (xss[i])
		    for (j=0; j<n; j++)
		      extra_stat->xbar[i]+=xss[i][j];
		  else extra_stat->xbar[i]=n; /* so mean is 1 */
		  extra_stat->xbar[i]/=n;
		}

		for (i=0; i<n; i++){
		  residuals[i]=0;
		  for (j=0; j<xdim; j++){
		    if (xss[j])
		      residuals[i]+=xss[j][i]*res[j];
		    else residuals[i]+=res[j]; /* If NULL, constant factor */
		      }
		  residuals[i]=ys[i]-residuals[i];
		}
		extra_stat->ss_resid=0;
		for (i=0; i<n; i++) extra_stat->ss_resid+=residuals[i]*residuals[i];

		extra_stat->sqr_r = 1 - (extra_stat->ss_resid / extra_stat->se_y);
		extra_stat->adj_sqr_r = 1- (extra_stat->ss_resid / (n-xdim) / (extra_stat->se_y / (n-1))); /* Affine? */
		extra_stat->var = (extra_stat->ss_resid / (n-xdim)); /* Affine? */
	        L = g_new (float_t *, xdim);
		U = g_new (float_t *, xdim);
		for (i=0; i<xdim; i++){
		  L[i] = g_new (float_t, xdim);
		  U[i] = g_new (float_t, xdim);
		}

		err2 = LUDecomp(xTx,L,U,xdim);
		if (err2==0){
		  extra_stat->se = g_new(float_t,xdim);
		  e = g_new (float_t, xdim); /* Elmentary vector */
		  inv = g_new (float_t, xdim);
		  for (i=0; i<xdim; i++)
		    e[i]=0;
		  for (i=0; i<xdim; i++){
		    e[i]=1;
		    backsolve(L,U,e,xdim,inv);
		    extra_stat->se[i] = sqrt(extra_stat->var * inv[i]);
		    e[i]=0;
		  }
		  g_free(e);
		  g_free(inv);
		}
		for (i=0; i<xdim; i++){
		  g_free(L[i]);
		  g_free(U[i]);
		}
		g_free(L);
		g_free(U);

		extra_stat->t  = g_new (float_t, xdim);

		for (i = 0; i < xdim; i++)
			extra_stat->t  [i] = res[i] / extra_stat->se [i];

		extra_stat->F = (extra_stat->sqr_r/(xdim-1)) /
		  ((1-extra_stat->sqr_r)/(n-xdim)); /* non-affine? */

		extra_stat->df = n-xdim; /* is this true with non-affine? */
		extra_stat->ss_reg = 0;
		extra_stat->se_y=sqrt(extra_stat->se_y / n); /* Now it is SE */
		g_free(residuals);
	}
	for (i = 0; i < xdim; i++)
		g_free (xTx[i]);
	g_free (xTx);
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
