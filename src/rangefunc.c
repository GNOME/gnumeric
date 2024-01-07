/*
 * rangefunc.c: Functions on ranges (data sets).
 *
 * Authors:
 *   Copyright (C) 2007-2009 Morten Welinder (terra@gnome.org)
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <rangefunc.h>

#include <mathfunc.h>
#include <sf-gamma.h>
#include <gutils.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <tools/analysis-tools.h>

int
gnm_range_count (G_GNUC_UNUSED gnm_float const *xs, int n, gnm_float *res)
{
	*res = n;
	return 0;
}

int
gnm_range_hypot (gnm_float const *xs, int n, gnm_float *res)
{
	/* Drop outside zeros because the n<=2 cases are more accurate.  */
	while (n > 0 && xs[0] == 0)
		xs++, n--;
	while (n > 0 && xs[n - 1] == 0)
		n--;

	switch (n) {
	case 0: *res = 0; return 0;
	case 1: *res = gnm_abs (xs[0]); return 0;
	case 2: *res = gnm_hypot (xs[0], xs[1]); return 0;
	default:
		if (gnm_range_sumsq (xs, n, res))
			return 1;
		*res = gnm_sqrt (*res);
		return 0;
	}
}

/* Average absolute deviation from mean.  */
int
gnm_range_avedev (gnm_float const *xs, int n, gnm_float *res)
{
	if (n > 0) {
		gnm_float m, s = 0;
		int i;

		gnm_range_average (xs, n, &m);
		for (i = 0; i < n; i++)
			s += gnm_abs (xs[i] - m);
		*res = s / n;
		return 0;
	} else
		return 1;
}

/* Variance with weight N.  */
int
gnm_range_var_pop (gnm_float const *xs, int n, gnm_float *res)
{
	if (n > 0) {
		gnm_float q;

		gnm_range_devsq (xs, n, &q);
		*res = q / n;
		return 0;
	} else
		return 1;
}

/* Variance with weight N-1.  */
int
gnm_range_var_est (gnm_float const *xs, int n, gnm_float *res)
{
	if (n > 1) {
		gnm_float q;

		gnm_range_devsq (xs, n, &q);
		*res = q / (n - 1);
		return 0;
	} else
		return 1;
}

/* Standard deviation with weight N.  */
int
gnm_range_stddev_pop (gnm_float const *xs, int n, gnm_float *res)
{
	if (gnm_range_var_pop (xs, n, res))
		return 1;
	else {
		*res = gnm_sqrt (*res);
		return 0;
	}
}

/* Standard deviation with weight N-1.  */
int
gnm_range_stddev_est (gnm_float const *xs, int n, gnm_float *res)
{
	if (gnm_range_var_est (xs, n, res))
		return 1;
	else {
		*res = gnm_sqrt (*res);
		return 0;
	}
}

/* Population skew.  */
int
gnm_range_skew_pop (gnm_float const *xs, int n, gnm_float *res)
{
	gnm_float m, s, dxn, x3 = 0;
	int i;

	if (n < 1 || gnm_range_average (xs, n, &m) || gnm_range_stddev_pop (xs, n, &s))
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
gnm_range_skew_est (gnm_float const *xs, int n, gnm_float *res)
{
	gnm_float m, s, dxn, x3 = 0;
	int i;

	if (n < 3 || gnm_range_average (xs, n, &m) || gnm_range_stddev_est (xs, n, &s))
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
gnm_range_kurtosis_m3_pop (gnm_float const *xs, int n, gnm_float *res)
{
	gnm_float m, s, dxn, x4 = 0;
	int i;

	if (n < 1 || gnm_range_average (xs, n, &m) || gnm_range_stddev_pop (xs, n, &s))
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
gnm_range_kurtosis_m3_est (gnm_float const *xs, int n, gnm_float *res)
{
	gnm_float m, s, dxn, x4 = 0;
	gnm_float common_den, nth, three;
	int i;

	if (n < 4 || gnm_range_average (xs, n, &m) || gnm_range_stddev_est (xs, n, &s))
		return 1;
	if (s == 0)
		return 1;

	for (i = 0; i < n; i++) {
		dxn = (xs[i] - m) / s;
		x4 += (dxn * dxn) * (dxn * dxn);
	}

	common_den = (gnm_float)(n - 2) * (n - 3);
	nth = (gnm_float)n * (n + 1) / ((n - 1) * common_den);
	three = GNM_const(3.) * (n - 1) * (n - 1) / common_den;

	*res = x4 * nth - three;
	return 0;
}

/* Harmonic mean of positive numbers.  */
int
gnm_range_harmonic_mean (gnm_float const *xs, int n, gnm_float *res)
{
	if (n > 0) {
		gnm_float invsum = 0;
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

static void
product_helper (gnm_float const *xs, int n,
		gnm_float *res, int *expb,
		gboolean *zerop, gboolean *anynegp)
{
	gnm_float x0 = xs[0];
	*zerop = (x0 == 0);
	*anynegp = (x0 < 0);

	if (n == 1 || *zerop) {
		*res = x0;
		*expb = 0;
	} else {
		int e;
		gnm_float mant = gnm_unscalbn (x0, &e);
		int i;

		for (i = 1; i < n; i++) {
			int thise;
			gnm_float x = xs[i];

			if (x == 0) {
				*zerop = TRUE;
				*res = 0;
				*expb = 0;
				return;
			}
			if (x < 0) *anynegp = TRUE;

			mant *= gnm_unscalbn (x, &thise);
			e += thise;

			/* Keep 1/base < |mant| <= 1.  */
			if (gnm_abs (mant) <= GNM_const(1.) / GNM_RADIX) {
				mant *= GNM_RADIX;
				e--;
			}
		}

		*expb = e;
		*res = mant;
	}
}


/* Geometric mean of positive numbers.  */
int
gnm_range_geometric_mean (gnm_float const *xs, int n, gnm_float *res)
{
	int expb;
	gboolean zerop, anynegp;

	if (n < 1)
		return 1;

	product_helper (xs, n, res, &expb, &zerop, &anynegp);
	if (zerop || anynegp)
		return anynegp;

	/* Now compute (res * base^expb) ^ (1/n).  */
	if (expb >= 0)
		*res = gnm_scalbn (gnm_pow (gnm_scalbn (*res, expb % n), 1.0 / n), expb / n);
	else
		*res = gnm_scalbn (gnm_pow (gnm_scalbn (*res, -((-expb) % n)), 1.0 / n), expb / n);

	return 0;
}


/* Product.  */
int
gnm_range_product (gnm_float const *xs, int n, gnm_float *res)
{
	if (n == 0) {
		*res = 1;
	} else {
		int expb;
		gboolean zerop, anynegp;

		product_helper (xs, n, res, &expb, &zerop, &anynegp);
		if (expb)
			*res = gnm_scalbn (*res, expb);
	}

	return 0;
}

int
gnm_range_multinomial (gnm_float const *xs, int n, gnm_float *res)
{
	gnm_float result = 1;
	int sum = 0;
	int i;

	for (i = 0; i < n; i++) {
		gnm_float x = xs[i];
		int xi;

		if (x < 0 || x > INT_MAX)
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

/* Population co-variance.  */
int
gnm_range_covar_pop (gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res)
{
	gnm_float ux, uy, s = 0;
	int i;

	if (n <= 0 || gnm_range_average (xs, n, &ux) || gnm_range_average (ys, n, &uy))
		return 1;

	for (i = 0; i < n; i++)
		s += (xs[i] - ux) * (ys[i] - uy);
	*res = s / n;
	return 0;
}

/* Estimation co-variance.  */
int
gnm_range_covar_est (gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res)
{
	gnm_float ux, uy, s = 0;
	int i;

	if (n <= 1 || gnm_range_average (xs, n, &ux) || gnm_range_average (ys, n, &uy))
		return 1;

	for (i = 0; i < n; i++)
		s += (xs[i] - ux) * (ys[i] - uy);
	*res = s / (n - 1);
	return 0;
}

/* Population correlation coefficient.  */
int
gnm_range_correl_pop (gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res)
{
	gnm_float sx, sy, vxy, c;

	if (gnm_range_stddev_pop (xs, n, &sx) || sx == 0 ||
	    gnm_range_stddev_pop (ys, n, &sy) || sy == 0 ||
	    gnm_range_covar_pop (xs, ys, n, &vxy))
		return 1;

	c = vxy / (sx * sy);

	// Rounding errors can push us beyond [-1,+1].  Avoid that.
	// This isn't a great solution, but it'll have to do until
	// someone comes up with a better approach.
	c = CLAMP (c, -1, +1);

	*res = c;
	return 0;
}

/* Population R-squared.  */
int
gnm_range_rsq_pop (gnm_float const *xs, const gnm_float *ys, int n, gnm_float *res)
{
	if (gnm_range_correl_pop (xs, ys, n, res))
		return 1;

	*res *= *res;
	return 0;
}

/* Most-common element.  (The one whose first occurrence comes first in
   case of several equally common.)  */
int
gnm_range_mode (gnm_float const *xs, int n, gnm_float *res)
{
	GHashTable *h;
	int i;
	gnm_float mode = 0;
	gconstpointer mode_key = NULL;
	int dups = 0;

	if (n <= 1) return 1;

	h = g_hash_table_new_full ((GHashFunc)gnm_float_hash,
				   (GCompareFunc)gnm_float_equal,
				   NULL,
				   (GDestroyNotify)g_free);
	for (i = 0; i < n; i++) {
		gpointer rkey, rval;
		gboolean found = g_hash_table_lookup_extended (h, &xs[i], &rkey, &rval);
		int *pdups;

		if (found) {
			pdups = (int *)rval;
			(*pdups)++;
			if (*pdups == dups && rkey < mode_key) {
				mode = xs[i];
				mode_key = rkey;
			}
		} else {
			pdups = g_new (int, 1);
			*pdups = 1;
			rkey = (gpointer)(xs + i);
			g_hash_table_insert (h, rkey, pdups);
		}

		if (*pdups > dups) {
			dups = *pdups;
			mode = xs[i];
			mode_key = rkey;
		}
	}
	g_hash_table_destroy (h);

	if (dups <= 1)
		return 1;

	*res = mode;
	return 0;
}

int
gnm_range_adtest    (gnm_float const *xs, int n, gnm_float *pvalue,
		     gnm_float *statistics)
{
	gnm_float mu = 0.;
	gnm_float sigma = 1.;

	if ((n < 8) || gnm_range_average (xs, n, &mu)
	    || gnm_range_stddev_est (xs, n, &sigma))
		return 1;
	else {
		int i;
		gnm_float total = 0.;
		gnm_float p;
		gnm_float *ys;

		ys = gnm_range_sort (xs, n);

		for (i = 0; i < n; i++) {
			gnm_float val = (pnorm (ys[i], mu, sigma, TRUE, TRUE) +
					 pnorm (ys[n - i - 1],
						mu, sigma, FALSE, TRUE));
			total += ((2 * i + 1) * val);
		}

		total = - n - total/n;
		g_free (ys);

		total *= (1 + GNM_const(0.75) / n + GNM_const(2.25) / ((gnm_float)n * n));
		if (total < GNM_const(0.20))
			p = -gnm_expm1 (GNM_const(-13.436) + GNM_const(101.14) * total - GNM_const(223.73) * total * total);
		else if (total < GNM_const(0.34))
			p = -gnm_expm1 (GNM_const(-8.318) + GNM_const(42.796) * total - GNM_const(59.938) * total * total);
		else if (total < GNM_const(0.6))
			p = gnm_exp (GNM_const(0.9177) - GNM_const(4.279) * total - GNM_const(1.38) * total * total);
		else
			p = gnm_exp (GNM_const(1.2937) - GNM_const(5.709) * total + GNM_const(0.0186) * total * total);
		if (statistics)
			*statistics = total;
		if (pvalue)
			*pvalue = p;
		return 0;
	}
}
