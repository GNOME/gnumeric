/*
 * rangefunc.c: Functions on ranges (data sets).
 *
 * Authors:
 *   Morten Welinder <terra@diku.dk>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "rangefunc.h"

#include "mathfunc.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Arithmetic sum.  */
int
range_sum (const gnum_float *xs, int n, gnum_float *res)
{
	gnum_float sum_1 = 0;
	gnum_float sum_2 = 0;
	gnum_float xbar = 0;
	int i;

	if (n == 0) {
		*res = 0.0;
		return 0;
	}

	for (i = 0; i < n; i++)
		sum_1 += xs[i];
	xbar = sum_1 / n;
	for (i = 0; i < n; i++)
		sum_2 += (xs[i] - xbar);
	*res = sum_1 + sum_2;
	return 0;
}

/* Arithmetic sum of squares.  */
int
range_sumsq (const gnum_float *xs, int n, gnum_float *res)
{
	gnum_float sum_1 = 0;
	gnum_float sum_2 = 0;
	gnum_float xbar = 0;
	int i;

	if (n == 0) {
		*res = 0.0;
		return 0;
	}

	for (i = 0; i < n; i++)
		sum_1 += xs[i] * xs[i];
	xbar = sum_1 / n;
	for (i = 0; i < n; i++)
		sum_1 += ((xs[i] * xs[i]) - xbar);
	*res = sum_1 + sum_2;
	return 0;
}

/* Arithmetic average.  */
int
range_average (const gnum_float *xs, int n, gnum_float *res)
{
	if (n <= 0 || range_sum (xs, n, res))
		return 1;

	*res /= n;
	return 0;
}

/* Minimum element.  */
int
range_min (const gnum_float *xs, int n, gnum_float *res)
{
	if (n > 0) {
		gnum_float min = xs[0];
		int i;

		for (i = 1; i < n; i++)
			if (xs[i] < min)
				min = xs[i];
		*res = min;
		return 0;
	} else
		return 1;
}

/* Maximum element.  */
int
range_max (const gnum_float *xs, int n, gnum_float *res)
{
	if (n > 0) {
		gnum_float max = xs[0];
		int i;

		for (i = 1; i < n; i++)
			if (xs[i] > max)
				max = xs[i];
		*res = max;
		return 0;
	} else
		return 1;
}


/* Average absolute deviation from mean.  */
int
range_avedev (const gnum_float *xs, int n, gnum_float *res)
{
	if (n > 0) {
		gnum_float m, s = 0;
		int i;

		range_average (xs, n, &m);
		for (i = 0; i < n; i++)
			s += fabs (xs[i] - m);
		*res = s / n;
		return 0;
	} else
		return 1;
}


/* Sum of square deviations from mean.  */
int
range_devsq (const gnum_float *xs, int n, gnum_float *res)
{
	gnum_float m, dx, q = 0;
	if (n > 0) {
		int i;

		range_average (xs, n, &m);
		for (i = 0; i < n; i++) {
			dx = xs[i] - m;
			q += dx * dx;
		}
	}
	*res = q;
	return 0;
}

/* Variance with weight N.  */
int
range_var_pop (const gnum_float *xs, int n, gnum_float *res)
{
	if (n > 0) {
		gnum_float q;

		range_devsq (xs, n, &q);
		*res = q / n;
		return 0;
	} else
		return 1;
}

/* Variance with weight N-1.  */
int
range_var_est (const gnum_float *xs, int n, gnum_float *res)
{
	if (n > 1) {
		gnum_float q;

		range_devsq (xs, n, &q);
		*res = q / (n - 1);
		return 0;
	} else
		return 1;
}

/* Standard deviation with weight N.  */
int
range_stddev_pop (const gnum_float *xs, int n, gnum_float *res)
{
	if (range_var_pop (xs, n, res))
		return 1;
	else {
		*res = sqrt (*res);
		return 0;
	}
}

/* Standard deviation with weight N-1.  */
int
range_stddev_est (const gnum_float *xs, int n, gnum_float *res)
{
	if (range_var_est (xs, n, res))
		return 1;
	else {
		*res = sqrt (*res);
		return 0;
	}
}

/* Population skew.  */
int
range_skew_pop (const gnum_float *xs, int n, gnum_float *res)
{
	gnum_float m, s, dxn, x3 = 0;
	int i;

	if (n < 1 || range_average (xs, n, &m) || range_stddev_pop (xs, n, &s))
		return 1;
	if (s == 0)
		return 1;

	for (i = 0; i < n; i++) {
		dxn = (xs[i] - m) / s;
		x3 += dxn * dxn *dxn;
	}

	*res = x3 / n;
	return 0;
}

/* Maximum-likelyhood estimator for skew.  */
int
range_skew_est (const gnum_float *xs, int n, gnum_float *res)
{
	gnum_float m, s, dxn, x3 = 0;
	int i;

	if (n < 3 || range_average (xs, n, &m) || range_stddev_est (xs, n, &s))
		return 1;
	if (s == 0)
		return 1;

	for (i = 0; i < n; i++) {
		dxn = (xs[i] - m) / s;
		x3 += dxn * dxn *dxn;
	}

	*res = ((x3 * n) / (n - 1)) / (n - 2);
	return 0;
}

/* Population kurtosis (with offset 3).  */
int
range_kurtosis_m3_pop (const gnum_float *xs, int n, gnum_float *res)
{
	gnum_float m, s, dxn, x4 = 0;
	int i;

	if (n < 1 || range_average (xs, n, &m) || range_stddev_pop (xs, n, &s))
		return 1;
	if (s == 0)
		return 1;

	for (i = 0; i < n; i++) {
		dxn = (xs[i] - m) / s;
		x4 += (dxn * dxn) * (dxn * dxn);
	}

	*res = x4 / n - 3;
	return 0;
}

/* Unbiased, I hope, estimator for kurtosis (with offset 3).  */
int
range_kurtosis_m3_est (const gnum_float *xs, int n, gnum_float *res)
{
	gnum_float m, s, dxn, x4 = 0;
	gnum_float common_den, nth, three;
	int i;

	if (n < 4 || range_average (xs, n, &m) || range_stddev_est (xs, n, &s))
		return 1;
	if (s == 0)
		return 1;

	for (i = 0; i < n; i++) {
		dxn = (xs[i] - m) / s;
		x4 += (dxn * dxn) * (dxn * dxn);
	}

	common_den = (gnum_float)(n - 2) * (n - 3);
	nth = (gnum_float)n * (n + 1) / ((n - 1) * common_den);
	three = 3.0 * (n - 1) * (n - 1) / common_den;

	*res = x4 * nth - three;
	return 0;
}

/* Harmonic mean of positive numbers.  */
int
range_harmonic_mean (const gnum_float *xs, int n, gnum_float *res)
{
	if (n > 0) {
		gnum_float invsum = 0;
		int i;

		for (i = 0; i < n; i++) {
			if (xs[i] <= 0)
				return 1;
			invsum += 1 / xs[i];
		}
		*res = n / invsum;
		return 0;
	} else
		return 1;
}

/* Geometric mean of positive numbers.  */
int
range_geometric_mean (const gnum_float *xs, int n, gnum_float *res)
{
	if (n > 0) {
		gnum_float product = 1;
		int i;

		/* FIXME: we should work harder at avoiding
		   overflow here.  */
		for (i = 0; i < n; i++) {
			if (xs[i] <= 0)
				return 1;
			product *= xs[i];
		}
		*res = pow (product, 1.0 / n);
		return 0;
	} else
		return 1;
}


/* Product.  */
int
range_product (const gnum_float *xs, int n, gnum_float *res)
{
	gnum_float product = 1;
	int i;

	/* FIXME: we should work harder at avoiding overflow here.  */
	for (i = 0; i < n; i++) {
		product *= xs[i];
	}
	*res = product;
	return 0;
}

int
range_multinomial (const gnum_float *xs, int n, gnum_float *res)
{
	gnum_float result = 1;
	int sum = 0;
	int i;

	for (i = 0; i < n; i++) {
		gnum_float x = xs[i];
		int xi;

		if (x < 0)
			return 1;

		xi = (int)x;
		if (sum == 0 || xi == 0)
			; /* Nothing.  */
		else if (xi < 20) {
			int j;
			int f = sum + xi;

			result *= f--;
			for (j = 2; j <= xi; j++)
				result = result * f-- / j;
		} else {
			/* Same as above, only faster.  */
			result *= combin (sum + xi, xi);
		}

		sum += xi;
	}

	*res = result;
	return 0;
}

/* Co-variance.  */
int
range_covar (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res)
{
	gnum_float ux, uy, s = 0;
	int i;

	if (n <= 0 || range_average (xs, n, &ux) || range_average (ys, n, &uy))
		return 1;

	for (i = 0; i < n; i++)
		s += (xs[i] - ux) * (ys[i] - uy);
	*res = s / n;
	return 0;
}

/* Population correlation coefficient.  */
int
range_correl_pop (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res)
{
	gnum_float sx, sy, vxy;

	if (range_stddev_pop (xs, n, &sx) || sx == 0 ||
	    range_stddev_pop (ys, n, &sy) || sy == 0 ||
	    range_covar (xs, ys, n, &vxy))
		return 1;

	*res = vxy / (sx * sy);
	return 0;
}

/* Maximum-likelyhood correlation coefficient.  */
int
range_correl_est (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res)
{
	gnum_float sx, sy, vxy;

	if (range_stddev_est (xs, n, &sx) || sx == 0 ||
	    range_stddev_est (ys, n, &sy) || sy == 0 ||
	    range_covar (xs, ys, n, &vxy))
		return 1;

	*res = vxy / (sx * sy);
	return 0;
}

/* Population R-squared.  */
int
range_rsq_pop (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res)
{
	if (range_correl_pop (xs, ys, n, res))
		return 1;

	*res *= *res;
	return 0;
}

/* Maximum-likelyhood R-squared.  */
int
range_rsq_est (const gnum_float *xs, const gnum_float *ys, int n, gnum_float *res)
{
	if (range_correl_est (xs, ys, n, res))
		return 1;

	*res *= *res;
	return 0;
}


static void
cb_range_mode (gpointer key, gpointer value, gpointer user_data)
{
	g_free (value);
}

static guint
float_hash (const gnum_float *d)
{
	guint h = 0;
	size_t i;
	const unsigned char *p = (const unsigned char *)d;

	for (i = 0; i < sizeof (gnum_float); i++)
		h ^= h / 3 + (h << 9) + p[i];

        return h;
}

static gint
float_equal (const gnum_float *a, const gnum_float *b)
{
	if (*a == *b)
	        return 1;
	return 0;
}

/* Most-common element.  (First one, in case of several equally common.  */
int
range_mode (const gnum_float *xs, int n, gnum_float *res)
{
	GHashTable *h;
	int i;
	gnum_float mode = 0;
	int dups = 0;

	if (n <= 1) return 1;

	h = g_hash_table_new ((GHashFunc)float_hash,
			      (GCompareFunc)float_equal);
	for (i = 0; i < n; i++) {
		int *pdups = g_hash_table_lookup (h, &xs[i]);

		if (pdups)
			(*pdups)++;
		else {
			pdups = g_new (int, 1);
			*pdups = 1;
			g_hash_table_insert (h, (gpointer)(&xs[i]), pdups);
		}

		if (*pdups > dups) {
			dups = *pdups;
			mode = xs[i];
		}
	}
	g_hash_table_foreach (h, cb_range_mode, NULL);
	g_hash_table_destroy (h);

	if (dups <= 1)
		return 1;

	*res = mode;
	return 0;
}


static gint
float_compare (const gnum_float *a, const gnum_float *b)
{
        if (*a < *b)
                return -1;
	else if (*a == *b)
	        return 0;
	else
	        return 1;
}

static gnum_float *
range_sort (const gnum_float *xs, int n)
{
	if (n <= 0)
		return NULL;
	else {
		gnum_float *ys = g_new (gnum_float, n);
		memcpy (ys, xs, n * sizeof (gnum_float));
		qsort (ys, n, sizeof (ys[0]), (int (*) (const void *, const void *))&float_compare);
		return ys;
	}
}


/* This requires sorted data.  */
static int
range_fractile_inter_sorted (const gnum_float *xs, int n, gnum_float *res, gnum_float f)
{
	gnum_float fpos, residual;
	int pos;

	if (n <= 0 || f < 0.0 || f > 1.0)
		return 1;

	fpos = (n - 1) * f;
	pos = (int)fpos;
	residual = fpos - pos;

	if (residual == 0.0 || pos + 1 >= n)
		*res = xs[pos];
	else
		*res = (1 - residual) * xs[pos] + residual * xs[pos + 1];

	return 0;
}

/* Interpolative fractile.  */
int
range_fractile_inter (const gnum_float *xs, int n, gnum_float *res, gnum_float f)
{
	gnum_float *ys = range_sort (xs, n);
	int error = range_fractile_inter_sorted (ys, n, res, f);
	g_free (ys);
	return error;
}

/* Interpolative fractile.  */
/* This version may reorder data points.  */
int
range_fractile_inter_nonconst (gnum_float *xs, int n, gnum_float *res, gnum_float f)
{
	qsort (xs, n, sizeof (xs[0]), (int (*) (const void *, const void *))&float_compare);
	return range_fractile_inter_sorted (xs, n, res, f);
}

/* Interpolative median.  */
int
range_median_inter (const gnum_float *xs, int n, gnum_float *res)
{
	return range_fractile_inter (xs, n, res, 0.5);
}

/* Interpolative median.  */
/* This version may reorder data points.  */
int
range_median_inter_nonconst (gnum_float *xs, int n, gnum_float *res)
{
	return range_fractile_inter_nonconst (xs, n, res, 0.5);
}

/* k-th smallest.  Note: k is zero-based.  */
int
range_min_k (const gnum_float *xs, int n, gnum_float *res, int k)
{
	gnum_float *ys;

	if (k < 0 || k >= n)
		return 1;
	if (k == 0)
		return range_min (xs, n, res);
	if (k == n - 1)
		return range_max (xs, n, res);

	ys = range_sort (xs, n);
	*res = ys[k];
	g_free (ys);
	return 0;
}

/* k-th smallest.  Note: k is zero-based.  */
/* This version may reorder data points.  */
int
range_min_k_nonconst (gnum_float *xs, int n, gnum_float *res, int k)
{
	if (k < 0 || k >= n)
		return 1;
	if (k == 0)
		return range_min (xs, n, res);
	if (k == n - 1)
		return range_max (xs, n, res);

	qsort (xs, n, sizeof (xs[0]), (int (*) (const void *, const void *))&float_compare);
	*res = xs[k];
	return 0;
}
