/*
 * mathfunc.c:  Mathematical functions.
 *
 * Authors:
 *   Ross Ihaka (See also note below.)
 *   Morten Welinder <terra@diku.dk>
 */

/*
 * NOTE: most of this file comes from the "R" package, notably version 0.64.
 * "R" is distributed under GPL licence, see file COPYING.
 * The relevant parts are copyright (C) 1998 Ross Ihaka.
 *
 * Thank you Ross!
 */

#include <config.h>
#include "mathfunc.h"
#include <math.h>
#include <errno.h>
#include <float.h>
#ifdef HAVE_IEEEFP
#include <ieeefp.h>
/* Make sure we have this symbol defined, since the existance of the header
   file implies it.  */
#ifndef IEEE_754
#define IEEE_754
#endif
#endif
#include <glib.h>

#define M_LN_SQRT_2PI   0.918938533204672741780329736406  /* log(sqrt(2*pi)) */
#define M_SQRT_32       5.656854249492380195206754896838  /* sqrt(32) */
#define M_1_SQRT_2PI    0.398942280401432677939946059934  /* 1/sqrt(2pi) */
#define M_PI_half       M_PI_2

#ifndef ISNAN
#define ISNAN isnan
#endif

#ifndef FINITE
#define FINITE finite
#endif

/* Any better idea for a quick hack?  */
#define ML_NAN (0.0 / 0.0)
#define ML_NEGINF (-1.0 / 0.0)
#define ML_POSINF (+1.0 / 0.0)
#define ML_UNDERFLOW (DBL_EPSILON * DBL_EPSILON)
#define ML_VALID(_x) (!ISNAN (_x))

#define ML_ERROR(cause) /* Nothing */

#define fmin2(_x,_y) MIN(_x, _y)
#define fmax2(_x,_y) MAX(_x, _y)
#define lgammafn(_x) lgamma (_x)
#define gammafn(_x) exp (lgammafn (_x))

static inline double
d1mach (int i)
{
        switch (i) {
	case 1: return DBL_MIN;
	case 2: return DBL_MAX;
	case 3: return pow (FLT_RADIX, -DBL_MANT_DIG);
	case 4: return pow (FLT_RADIX, 1 - DBL_MANT_DIG);
	case 5: return log10 (2.0);
	default:return 0.0;
	}
}

/* MW ---------------------------------------------------------------------- */

/* Arithmetic sum.  */
int
range_sum (const float_t *xs, int n, float_t *res)
{
	float_t sum = 0;
	int i;

	for (i = 0; i < n; i++)
		sum += xs[i];
	*res = sum;
	return 0;
}

/* Arithmetic sum of squares.  */
int
range_sumsq (const float_t *xs, int n, float_t *res)
{
	float_t sum = 0;
	int i;

	for (i = 0; i < n; i++)
		sum += xs[i] * xs[i];
	*res = sum;
	return 0;
}

/* Arithmetic average.  */
int
range_average (const float_t *xs, int n, float_t *res)
{
	if (n <= 0 || range_sum (xs, n, res))
		return 1;

	*res /= n;
	return 0;
}

int
range_min (const float_t *xs, int n, float_t *res)
{
	if (n > 0) {
		float_t min = xs[0];
		int i;

		for (i = 1; i < n; i++)
			if (xs[i] < min)
				min = xs[i];
		*res = min;
		return 0;
	} else
		return 1;
}

int
range_max (const float_t *xs, int n, float_t *res)
{
	if (n > 0) {
		float_t max = xs[0];
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
range_avedev (const float_t *xs, int n, float_t *res)
{
	if (n > 0) {
		float_t m, s = 0;
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
range_devsq (const float_t *xs, int n, float_t *res)
{
	float_t m, dx, q = 0;
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
range_var_pop (const float_t *xs, int n, float_t *res)
{
	if (n > 0) {
		float_t q;

		range_devsq (xs, n, &q);
		*res = q / n;
		return 0;
	} else
		return 1;
}

/* Variance with weight N-1.  */
int
range_var_est (const float_t *xs, int n, float_t *res)
{
	if (n > 1) {
		float_t q;

		range_devsq (xs, n, &q);
		*res = q / (n - 1);
		return 0;
	} else
		return 1;
}

/* Standard deviation with weight N.  */
int
range_stddev_pop (const float_t *xs, int n, float_t *res)
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
range_stddev_est (const float_t *xs, int n, float_t *res)
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
range_skew_pop (const float_t *xs, int n, float_t *res)
{
	float_t m, s, dxn, x3 = 0;
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
range_skew_est (const float_t *xs, int n, float_t *res)
{
	float_t m, s, dxn, x3 = 0;
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
range_kurtosis_m3_pop (const float_t *xs, int n, float_t *res)
{
	float_t m, s, dxn, x4 = 0;
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
range_kurtosis_m3_est (const float_t *xs, int n, float_t *res)
{
	float_t m, s, dxn, x4 = 0;
	float_t common_den, nth, three;
	int i;

	if (n < 4 || range_average (xs, n, &m) || range_stddev_est (xs, n, &s))
		return 1;
	if (s == 0)
		return 1;

	for (i = 0; i < n; i++) {
		dxn = (xs[i] - m) / s;
		x4 += (dxn * dxn) * (dxn * dxn);
	}

	common_den = (float_t)(n - 2) * (n - 3);
	nth = (float_t)n * (n + 1) / ((n - 1) * common_den);
	three = 3.0 * (n - 1) * (n - 1) / common_den;

	*res = x4 * nth - three;
	return 0;
}

/* Harmonic mean of positive numbers.  */
int
range_harmonic_mean (const float_t *xs, int n, float_t *res)
{
	if (n > 0) {
		float_t invsum = 0;
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

/* Product.  */
int
range_product (const float_t *xs, int n, float_t *res)
{
	float_t product = 1;
	int i;

	/* FIXME: we should work harder at avoiding
	   overflow here.  */
	for (i = 0; i < n; i++) {
		product *= xs[i];
	}
	*res = product;
	return 0;
}

/* Geometric mean of positive numbers.  */
int
range_geometric_mean (const float_t *xs, int n, float_t *res)
{
	if (n > 0) {
		float_t product = 1;
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

/* src/nmath/dnorm.c ------------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double dnorm(double x, double mu, double sigma);
 *
 *  DESCRIPTION
 *
 *    Compute the density of the normal distribution.
 *
 * 	M_1_SQRT_2PI = 1 / sqrt(2 * pi)
 */

/* #include "Mathlib.h" */

	/* The Normal Density Function */

double dnorm(double x, double mu, double sigma)
{

#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(mu) || ISNAN(sigma))
	return x + mu + sigma;
#endif
    if (sigma <= 0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }

    x = (x - mu) / sigma;
    return M_1_SQRT_2PI * exp(-0.5 * x * x) / sigma;
}

/* src/nmath/pnorm.c ------------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double pnorm(double x, double mu, double sigma);
 *
 *  DESCRIPTION
 *
 *    The main computation evaluates near-minimax approximations derived
 *    from those in "Rational Chebyshev approximations for the error
 *    function" by W. J. Cody, Math. Comp., 1969, 631-637.  This
 *    transportable program uses rational functions that theoretically
 *    approximate the normal distribution function to at least 18
 *    significant decimal digits.  The accuracy achieved depends on the
 *    arithmetic system, the compiler, the intrinsic functions, and
 *    proper selection of the machine-dependent constants.
 *
 *  REFERENCE
 *
 *    Cody, W. D. (1993).
 *    ALGORITHM 715: SPECFUN - A Portable FORTRAN Package of
 *    Special Function Routines and Test Drivers".
 *    ACM Transactions on Mathematical Software. 19, 22-32.
 */

/* #include "Mathlib.h" */

/*  Mathematical Constants */

#define SIXTEN	1.6					/* Magic Cutoff */

double pnorm(double x, double mu, double sigma)
{
    static double c[9] = {
	0.39894151208813466764,
	8.8831497943883759412,
	93.506656132177855979,
	597.27027639480026226,
	2494.5375852903726711,
	6848.1904505362823326,
	11602.651437647350124,
	9842.7148383839780218,
	1.0765576773720192317e-8
    };

    static double d[8] = {
	22.266688044328115691,
	235.38790178262499861,
	1519.377599407554805,
	6485.558298266760755,
	18615.571640885098091,
	34900.952721145977266,
	38912.003286093271411,
	19685.429676859990727
    };

    static double p[6] = {
	0.21589853405795699,
	0.1274011611602473639,
	0.022235277870649807,
	0.001421619193227893466,
	2.9112874951168792e-5,
	0.02307344176494017303
    };

    static double q[5] = {
	1.28426009614491121,
	0.468238212480865118,
	0.0659881378689285515,
	0.00378239633202758244,
	7.29751555083966205e-5
    };

    static double a[5] = {
	2.2352520354606839287,
	161.02823106855587881,
	1067.6894854603709582,
	18154.981253343561249,
	0.065682337918207449113
    };

    static double b[4] = {
	47.20258190468824187,
	976.09855173777669322,
	10260.932208618978205,
	45507.789335026729956
    };

    double xden, temp, xnum, result, ccum;
    double del, min, eps, xsq;
    double y;
    int i;

    /* Note: The structure of these checks has been */
    /* carefully thought through.  For example, if x == mu */
    /* and sigma == 0, we still get the correct answer. */

#ifdef IEEE_754
    if(ISNAN(x) || ISNAN(mu) || ISNAN(sigma))
	return x + mu + sigma;
#endif
    if (sigma < 0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    x = (x - mu) / sigma;
#ifdef IEEE_754
    if(!finite(x)) {
	if(x < 0) return 0;
	else return 1;
    }
#endif

    eps = DBL_EPSILON * 0.5;
    min = DBL_MIN;
    y = fabs(x);
    if (y <= 0.66291) {
	xsq = 0.0;
	if (y > eps) {
	    xsq = x * x;
	}
	xnum = a[4] * xsq;
	xden = xsq;
	for (i = 1; i <= 3; ++i) {
	    xnum = (xnum + a[i - 1]) * xsq;
	    xden = (xden + b[i - 1]) * xsq;
	}
	result = x * (xnum + a[3]) / (xden + b[3]);
	temp = result;
	result = 0.5 + temp;
	ccum = 0.5 - temp;
    }
    else if (y <= M_SQRT_32) {

	/* Evaluate pnorm for 0.66291 <= |z| <= sqrt(32) */

	xnum = c[8] * y;
	xden = y;
	for (i = 1; i <= 7; ++i) {
	    xnum = (xnum + c[i - 1]) * y;
	    xden = (xden + d[i - 1]) * y;
	}
	result = (xnum + c[7]) / (xden + d[7]);
	xsq = floor(y * SIXTEN) / SIXTEN;
	del = (y - xsq) * (y + xsq);
	result = exp(-xsq * xsq * 0.5) * exp(-del * 0.5) * result;
	ccum = 1.0 - result;
	if (x > 0.0) {
	    temp = result;
	    result = ccum;
	    ccum = temp;
	}
    }
    else if(y < 50) {

	/* Evaluate pnorm for sqrt(32) < |z| < 50 */

	result = 0.0;
	xsq = 1.0 / (x * x);
	xnum = p[5] * xsq;
	xden = xsq;
	for (i = 1; i <= 4; ++i) {
	    xnum = (xnum + p[i - 1]) * xsq;
	    xden = (xden + q[i - 1]) * xsq;
	}
	result = xsq * (xnum + p[4]) / (xden + q[4]);
	result = (M_1_SQRT_2PI - result) / y;
	xsq = floor(x * SIXTEN) / SIXTEN;
	del = (x - xsq) * (x + xsq);
	result = exp(-xsq * xsq * 0.5) * exp(-del * 0.5) * result;
	ccum = 1.0 - result;
	if (x > 0.0) {
	    temp = result;
	    result = ccum;
	    ccum = temp;
	}
    }
    else {
	if(x > 0) {
	    result = 1.0;
	    ccum = 0.0;
	}
	else {
	    result = 0.0;
	    ccum = 1.0;
	}
    }
    if (result < min) {
	result = 0.0;
    }
    if (ccum < min) {
	ccum = 0.0;
    }
    return result;
}

#undef SIXTEN

/* src/nmath/qnorm.c ------------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    double pnorm(double p, double mu, double sigma);
 *
 *  DESCRIPTION
 *
 *    Compute the quantile function for the normal distribution.
 *
 *    For small to moderate probabilities, algorithm referenced
 *    below is used to obtain an initial approximation which is
 *    polished with a final Newton step.
 *
 *    For very large arguments, an algorithm of Wichura is used.
 *
 *  REFERENCE
 *
 *    Beasley, J. D. and S. G. Springer (1977).
 *    Algorithm AS 111: The percentage points of the normal distribution,
 *    Applied Statistics, 26, 118-121.
 */

/* #include "Mathlib.h" */


double qnorm(double p, double mu, double sigma)
{
    double q, r, val;

#ifdef IEEE_754
    if (ISNAN(p) || ISNAN(mu) || ISNAN(sigma))
	return p + mu + sigma;
#endif
    if (p < 0.0 || p > 1.0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }

    q = p - 0.5;

    if (fabs(q) <= 0.42) {

	/* 0.08 < p < 0.92 */

	r = q * q;
	val = q * (((-25.44106049637 * r + 41.39119773534) * r
		    - 18.61500062529) * r + 2.50662823884)
	    / ((((3.13082909833 * r - 21.06224101826) * r
		 + 23.08336743743) * r + -8.47351093090) * r + 1.0);
    }
    else {

	/* p < 0.08 or p > 0.92, set r = min(p, 1 - p) */

	r = p;
	if (q > 0.0)
	    r = 1.0 - p;

	if(r > DBL_EPSILON) {
	    r = sqrt(-log(r));
	    val = (((2.32121276858 * r + 4.85014127135) * r
		    - 2.29796479134) * r - 2.78718931138)
		/ ((1.63706781897 * r + 3.54388924762) * r + 1.0);
	    if (q < 0.0)
		val = -val;
	}
	else if(r > 1e-300) {		/* Assuming IEEE here? */
	    val = -2 * log(p);
	    r = log(6.283185307179586476925286766552 * val);
	    r = r/val + (2 - r)/(val * val)
		+ (-14 + 6 * r - r * r)/(2 * val * val * val);
	    val = sqrt(val * (1 - r));
	    if(q < 0.0)
		val = -val;
	    return val;
	}
	else {
	    ML_ERROR(ME_RANGE);
	    if(q < 0.0) {
		return ML_NEGINF;
	    }
	    else {
		return ML_POSINF;
	    }
	}
    }
    val = val - (pnorm(val, 0.0, 1.0) - p) / dnorm(val, 0.0, 1.0);
    return mu + sigma * val;
}


/* src/nmath/plnorm.c ------------------------------------------------------ */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double plnorm(double x, double logmean, double logsd);
 *
 *  DESCRIPTION
 *
 *    The lognormal distribution function.
 */

/* #include "Mathlib.h" */

double plnorm(double x, double logmean, double logsd)
{
#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(logmean) || ISNAN(logsd))
	return x + logmean + logsd;
#endif
    if (logsd <= 0) {
        ML_ERROR(ME_DOMAIN);
        return ML_NAN;
    }
    if (x > 0)
	return pnorm(log(x), logmean, logsd);
    return 0;
}


/* src/nmath/qlnorm.c ------------------------------------------------------ */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double qlnorm(double x, double logmean, double logsd);
 *
 *  DESCRIPTION
 *
 *    This the lognormal quantile function.
 */

/* #include "Mathlib.h" */

double qlnorm(double x, double logmean, double logsd)
{
#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(logmean) || ISNAN(logsd))
	return x + logmean + logsd;
#endif
    if(x < 0 || x > 1 || logsd <= 0) {
        ML_ERROR(ME_DOMAIN);
        return ML_NAN;
    }
    if (x == 1) return ML_POSINF;
    if (x > 0) return exp(qnorm(x, logmean, logsd));
    return 0;
}


/* src/nmath/dgamma.c ------------------------------------------------------ */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double dgamma(double x, double shape, double scale);
 *
 *  DESCRIPTION
 *
 *    Computes the density of the gamma distribution.
 */

/* #include "Mathlib.h" */

double dgamma(double x, double shape, double scale)
{
#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(shape) || ISNAN(scale))
	return x + shape + scale;
#endif
    if (shape <= 0 || scale <= 0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    if (x < 0)
	return 0;
    if (x == 0) {
	if (shape < 1) {
	    ML_ERROR(ME_RANGE);
	    return ML_POSINF;
	}
	if (shape > 1) {
	    return 0;
	}
	return 1 / scale;
    }
    x = x / scale;
    return exp((shape - 1) * log(x) - lgammafn(shape) - x) / scale;
}

/* src/nmath/pgamma.c ------------------------------------------------------ */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double pgamma(double x, double a, double scale);
 *
 *  DESCRIPTION
 *
 *    This function computes the distribution function for the
 *    gamma distribution with shape parameter a and scale parameter
 *    scale.  This is also known as the incomplete gamma function.
 *    See Abramowitz and Stegun (6.5.1) for example.
 *
 *  NOTES
 *
 *    This function is an adaptation of Algorithm 239 from the
 *    Applied Statistics Series.  The algorithm is faster than
 *    those by W. Fullerton in the FNLIB library and also the
 *    TOMS 542 alorithm of W. Gautschi.  It provides comparable
 *    accuracy to those algorithms and is considerably simpler.
 *
 *  REFERENCES
 *
 *    Algorithm AS 239, Incomplete Gamma Function
 *    Applied Statistics 37, 1988.
 */

/* #include "Mathlib.h" */

static const double
    third = 1.0 / 3.0,
    zero = 0.0,
    one = 1.0,
    two = 2.0,
    three = 3.0,
    nine = 9.0,
    xbig = 1.0e+8,
    oflo = 1.0e+37,
    plimit = 1000.0e0,
    elimit = -88.0e0;

double pgamma(double x, double p, double scale)
{
    double pn1, pn2, pn3, pn4, pn5, pn6, arg, c, rn, a, b, an;
    double sum;

    /* check that we have valid values for x and p */

#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(p) || ISNAN(scale))
	return x + p + scale;
#endif
    if(p <= zero || scale <= zero) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    x = x / scale;
    if (x <= zero)
	return 0.0;

    /* use a normal approximation if p > plimit */

    if (p > plimit) {
	pn1 = sqrt(p) * three * (pow(x/p, third) + one / (p * nine) - one);
	return pnorm(pn1, 0.0, 1.0);
    }

    /* if x is extremely large compared to p then return 1 */

    if (x > xbig)
	return one;

    if (x <= one || x < p) {

	/* use pearson's series expansion. */

	arg = p * log(x) - x - lgammafn(p + one);
	c = one;
	sum = one;
	a = p;
	do {
	    a = a + one;
	    c = c * x / a;
	    sum = sum + c;
	} while (c > DBL_EPSILON);
	arg = arg + log(sum);
	sum = zero;
	if (arg >= elimit)
	    sum = exp(arg);
    } else {

	/* use a continued fraction expansion */

	arg = p * log(x) - x - lgammafn(p);
	a = one - p;
	b = a + x + one;
	c = zero;
	pn1 = one;
	pn2 = x;
	pn3 = x + one;
	pn4 = x * b;
	sum = pn3 / pn4;
	for (;;) {
	    a = a + one;
	    b = b + two;
	    c = c + one;
	    an = a * c;
	    pn5 = b * pn3 - an * pn1;
	    pn6 = b * pn4 - an * pn2;
	    if (fabs(pn6) > zero) {
		rn = pn5 / pn6;
		if (fabs(sum - rn) <= fmin2(DBL_EPSILON, DBL_EPSILON * rn))
		    break;
		sum = rn;
	    }
	    pn1 = pn3;
	    pn2 = pn4;
	    pn3 = pn5;
	    pn4 = pn6;
	    if (fabs(pn5) >= oflo) {

                /* re-scale the terms in continued fraction */
		/* if they are large */

		pn1 = pn1 / oflo;
		pn2 = pn2 / oflo;
		pn3 = pn3 / oflo;
		pn4 = pn4 / oflo;
	    }
	}
	arg = arg + log(sum);
	sum = one;
	if (arg >= elimit)
	    sum = one - exp(arg);
    }
    return sum;
}


/* src/nmath/qgamma.c ------------------------------------------------------ */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double qgamma(double p, double shape, double scale);
 *
 *  DESCRIPTION
 *
 *    Compute the quantile function of the gamma distribution.
 *
 *  NOTES
 *
 *    This function is based on the Applied Statistics
 *    Algorithm AS 91 and AS 239.
 *
 *  REFERENCES
 *
 *    Best, D. J. and D. E. Roberts (1975).
 *    Percentage Points of the Chi-Squared Disribution.
 *    Applied Statistics 24, page 385.
 */

/* #include "Mathlib.h" */


double qgamma(double p, double alpha, double scale)
{
	const double C7 =	4.67;
	const double C8 =	6.66;
	const double C9 =	6.73;
	const double C10 =	13.32;

	const double C11 =	60;
	const double C12 =	70;
	const double C13 =	84;
	const double C14 =	105;
	const double C15 =	120;
	const double C16 =	127;
	const double C17 =	140;
	const double C18 =	1175;
	const double C19 =	210;

	const double C20 =	252;
	const double C21 =	2264;
	const double C22 =	294;
	const double C23 =	346;
	const double C24 =	420;
	const double C25 =	462;
	const double C26 =	606;
	const double C27 =	672;
	const double C28 =	707;
	const double C29 =	735;

	const double C30 =	889;
	const double C31 =	932;
	const double C32 =	966;
	const double C33 =	1141;
	const double C34 =	1182;
	const double C35 =	1278;
	const double C36 =	1740;
	const double C37 =	2520;
	const double C38 =	5040;

	const double EPS0 = 5e-7/* originally: IDENTICAL to EPS2; not clear why */;
	const double EPS1 = 1e-2;
	const double EPS2 = 5e-7;
	const double MAXIT = 20;

	const double pMIN = 0.000002;
	const double pMAX = 0.999998;

    double a, b, c, ch, g, p1, v;
    double p2, q, s1, s2, s3, s4, s5, s6, t, x;
    int i;

    /* test arguments and initialise */

#ifdef IEEE_754
    if (ISNAN(p) || ISNAN(alpha) || ISNAN(scale))
	return p + alpha + scale;
#endif

    if (p < 0 || p > 1 || alpha <= 0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    if (/* 0 <= */ p < pMIN) return 0;
    if (/* 1 >= */ p > pMAX) return ML_POSINF;

    v = 2*alpha;

    c = alpha-1;
    g = lgammafn(alpha);/* log Gamma(v/2) */

    if(v < (-1.24)*log(p)) {
      /* starting approximation for small chi-squared */

	ch = pow(p*alpha*exp(g+alpha*M_LN2), 1/alpha);
	if(ch < EPS0) {
	    ML_ERROR(ME_DOMAIN);
	    return ML_NAN;
	}

    } else if(v > 0.32) {

	/* starting approximation using Wilson and Hilferty estimate */

	x = qnorm(p, 0, 1);
	p1 = 0.222222/v;
	ch = v*pow(x*sqrt(p1)+1-p1, 3);

	/* starting approximation for p tending to 1 */

	if( ch > 2.2*v + 6 )
	    ch = -2*(log(1-p) - c*log(0.5*ch) + g);

    } else { /* starting approximation for v <= 0.32 */

	ch = 0.4;
	a = log(1-p) + g + c*M_LN2;
	do {
	    q = ch;
	    p1 = 1+ch*(C7+ch);
	    p2 = ch*(C9+ch*(C8+ch));
	    t = -0.5 +(C7+2*ch)/p1 - (C9+ch*(C10+3*ch))/p2;
	    ch -= (1- exp(a+0.5*ch)*p2/p1)/t;
	} while(fabs(q/ch - 1) > EPS1);
    }

    /* algorithm AS 239 and calculation of seven term taylor series */

    for( i=1 ; i <= MAXIT ; i++ ) {
	q = ch;
	p1 = 0.5*ch;
	p2 = p - pgamma(p1, alpha, 1);
#ifdef IEEE_754
	if(!finite(p2))
#else
	if(errno != 0)
#endif
		return ML_NAN;

	t = p2*exp(alpha*M_LN2+g+p1-c*log(ch));
	b = t/ch;
	a = 0.5*t-b*c;
	s1 = (C19+a*(C17+a*(C14+a*(C13+a*(C12+C11*a)))))/C24;
	s2 = (C24+a*(C29+a*(C32+a*(C33+C35*a))))/C37;
	s3 = (C19+a*(C25+a*(C28+C31*a)))/C37;
	s4 = (C20+a*(C27+C34*a)+c*(C22+a*(C30+C36*a)))/C38;
	s5 = (C13+C21*a+c*(C18+C26*a))/C37;
	s6 = (C15+c*(C23+C16*c))/C38;
	ch = ch+t*(1+0.5*t*s1-b*c*(s1-b*(s2-b*(s3-b*(s4-b*(s5-b*s6))))));
	if(fabs(q/ch-1) > EPS2)
	    return 0.5*scale*ch;
    }
    ML_ERROR(ME_PRECISION);
    return 0.5*scale*ch;
}


/* src/nmath/chebyshev.c --------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    int chebyshev_init(double *dos, int nos, double eta)
 *    double chebyshev_eval(double x, double *a, int n)
 *
 *  DESCRIPTION
 *
 *    "chebyshev_init" determines the number of terms for the
 *    double precision orthogonal series "dos" needed to insure
 *    the error is no larger than "eta".  Ordinarily eta will be
 *    chosen to be one-tenth machine precision.
 *
 *    "chebyshev_eval" evaluates the n-term Chebyshev series
 *    "a" at "x".
 *
 *  NOTES
 *
 *    These routines are translations into C of Fortran routines
 *    by W. Fullerton of Los Alamos Scientific Laboratory.
 *
 *    Based on the Fortran routine dcsevl by W. Fullerton.
 *    Adapted from R. Broucke, Algorithm 446, CACM., 16, 254 (1973).
 */

/* #include "Mathlib.h" */

/* NaNs propagated correctly */

static
int chebyshev_init(double *dos, int nos, double eta)
{
    int i, ii;
    double err;

    if (nos < 1)
	return 0;

    err = 0.0;
    i = 0;			/* just to avoid compiler warnings */
    for (ii=1; ii<=nos; ii++) {
	i = nos - ii;
	err += fabs(dos[i]);
	if (err > eta) {
	    return i;
	}
    }
    return i;
}


static
double chebyshev_eval(double x, double *a, int n)
{
    double b0, b1, b2, twox;
    int i;

    if (n < 1 || n > 1000) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }

    if (x < -1.1 || x > 1.1) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }

    twox = x * 2;
    b2 = b1 = 0;
    b0 = 0;
    for (i = 1; i <= n; i++) {
	b2 = b1;
	b1 = b0;
	b0 = twox * b1 - b2 + a[n - i];
    }
    return (b0 - b2) * 0.5;
}


/* src/nmath/lgammacor.c --------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double lgammacor(double x);
 *
 *  DESCRIPTION
 *
 *    Compute the log gamma correction factor for x >= 10 so that
 *
 *    log(gamma(x)) = .5*log(2*pi) + (x-.5)*log(x) -x + lgammacor(x)
 *
 *    [ lgammacor(x) is called	Del(x)	in other contexts (e.g. dcdflib)]
 *
 *  NOTES
 *
 *    This routine is a translation into C of a Fortran subroutine
 *    written by W. Fullerton of Los Alamos Scientific Laboratory.
 */

/* #include "Mathlib.h" */

static
double lgammacor(double x)
{
    static double algmcs[15] = {
	+.1666389480451863247205729650822e+0,
	-.1384948176067563840732986059135e-4,
	+.9810825646924729426157171547487e-8,
	-.1809129475572494194263306266719e-10,
	+.6221098041892605227126015543416e-13,
	-.3399615005417721944303330599666e-15,
	+.2683181998482698748957538846666e-17,
	-.2868042435334643284144622399999e-19,
	+.3962837061046434803679306666666e-21,
	-.6831888753985766870111999999999e-23,
	+.1429227355942498147573333333333e-24,
	-.3547598158101070547199999999999e-26,
	+.1025680058010470912000000000000e-27,
	-.3401102254316748799999999999999e-29,
	+.1276642195630062933333333333333e-30
    };
    static int nalgm = 0;
    static double xbig = 0;
    static double xmax = 0;
    double tmp;

    if (nalgm == 0) {
	nalgm = chebyshev_init(algmcs, 15, d1mach(3));
	xbig = 1 / sqrt(d1mach(3));
	xmax = exp(fmin2(log(d1mach(2) / 12), -log(12 * d1mach(1))));
    }

    if (x < 10) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    else if (x >= xmax) {
	ML_ERROR(ME_UNDERFLOW);
	return ML_UNDERFLOW;
    }
    else if (x < xbig) {
	tmp = 10 / x;
	return chebyshev_eval(tmp * tmp * 2 - 1, algmcs, nalgm) / x;
    }
    else return 1 / (x * 12);
}


/* src/nmath/logrelerr.c --------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double dlnrel(double x);
 *
 *  DESCRIPTION
 *
 *    Compute the relative error logarithm.
 *
 *                      log(1 + x)
 *
 *  NOTES
 *
 *    This code is a translation of a Fortran subroutine of the
 *    same name written by W. Fullerton of Los Alamos Scientific
 *    Laboratory.
 */

/* #include "Mathlib.h" */

static
double logrelerr(double x)
{
    /* series for alnr on the interval -3.75000e-01 to  3.75000e-01 */
    /*                               with weighted error   6.35e-32 */
    /*                                log weighted error  31.20     */
    /*                      significant figures required  30.93     */
    /*                           decimal places required  32.01     */
    static double alnrcs[43] = {
	+.10378693562743769800686267719098e+1,
	-.13364301504908918098766041553133e+0,
	+.19408249135520563357926199374750e-1,
	-.30107551127535777690376537776592e-2,
	+.48694614797154850090456366509137e-3,
	-.81054881893175356066809943008622e-4,
	+.13778847799559524782938251496059e-4,
	-.23802210894358970251369992914935e-5,
	+.41640416213865183476391859901989e-6,
	-.73595828378075994984266837031998e-7,
	+.13117611876241674949152294345011e-7,
	-.23546709317742425136696092330175e-8,
	+.42522773276034997775638052962567e-9,
	-.77190894134840796826108107493300e-10,
	+.14075746481359069909215356472191e-10,
	-.25769072058024680627537078627584e-11,
	+.47342406666294421849154395005938e-12,
	-.87249012674742641745301263292675e-13,
	+.16124614902740551465739833119115e-13,
	-.29875652015665773006710792416815e-14,
	+.55480701209082887983041321697279e-15,
	-.10324619158271569595141333961932e-15,
	+.19250239203049851177878503244868e-16,
	-.35955073465265150011189707844266e-17,
	+.67264542537876857892194574226773e-18,
	-.12602624168735219252082425637546e-18,
	+.23644884408606210044916158955519e-19,
	-.44419377050807936898878389179733e-20,
	+.83546594464034259016241293994666e-21,
	-.15731559416479562574899253521066e-21,
	+.29653128740247422686154369706666e-22,
	-.55949583481815947292156013226666e-23,
	+.10566354268835681048187284138666e-23,
	-.19972483680670204548314999466666e-24,
	+.37782977818839361421049855999999e-25,
	-.71531586889081740345038165333333e-26,
	+.13552488463674213646502024533333e-26,
	-.25694673048487567430079829333333e-27,
	+.48747756066216949076459519999999e-28,
	-.92542112530849715321132373333333e-29,
	+.17578597841760239233269760000000e-29,
	-.33410026677731010351377066666666e-30,
	+.63533936180236187354180266666666e-31,
    };
    static int nlnrel = 0;
    static double xmin = 0.;

    if (nlnrel == 0) {
        nlnrel = chebyshev_init(alnrcs, 43, 0.1 * d1mach(3));
        xmin = -1.0 + sqrt(d1mach(4));
    }

    if (x <= -1) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }

    if (x < xmin) {
	/* answer less than half precision because x too near -1 */
	ML_ERROR(ME_PRECISION);
    }

    if (fabs(x) <= .375)
	return x * (1 - x * chebyshev_eval(x / .375, alnrcs, nlnrel));
    else
	return log(x + 1);
}


/* src/nmath/lbeta.c ------------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double lbeta(double a, double b);
 *
 *  DESCRIPTION
 *
 *    This function returns the value of the log beta function.
 *
 *  NOTES
 *
 *    This routine is a translation into C of a Fortran subroutine
 *    by W. Fullerton of Los Alamos Scientific Laboratory.
 */

/* #include "Mathlib.h" */

static
double lbeta(double a, double b)
{
    static double corr, p, q;

    p = q = a;
    if(b < p) p = b;/* := min(a,b) */
    if(b > q) q = b;/* := max(a,b) */

#ifdef IEEE_754
    if(ISNAN(a) || ISNAN(b))
	return a + b;
#endif

    /* both arguments must be >= 0 */

    if (p < 0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    else if (p == 0) {
	return ML_POSINF;
    }
#ifdef IEEE_754
    else if (!FINITE(q)) {
	return ML_NEGINF;
    }
#endif

    if (p >= 10) {
	/* p and q are big. */
	corr = lgammacor(p) + lgammacor(q) - lgammacor(p + q);
	return log(q) * -0.5 + M_LN_SQRT_2PI + corr
		+ (p - 0.5) * log(p / (p + q)) + q * logrelerr(-p / (p + q));
    }
    else if (q >= 10) {
	/* p is small, but q is big. */
	corr = lgammacor(q) - lgammacor(p + q);
	return lgammafn(p) + corr + p - p * log(p + q)
		+ (q - 0.5) * logrelerr(-p / (p + q));
    }
    else
	/* p and q are small: p <= q > 10. */
	return log(gammafn(p) * (gammafn(q) / gammafn(p + q)));
}

/* src/nmath/pbeta.c ------------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double pbeta(double x, double pin, double qin);
 *
 *  DESCRIPTION
 *
 *    Returns distribution function of the beta distribution.
 *    ( = The incomplete beta ratio I_x(p,q) ).
 *
 *  NOTES
 *
 *    This routine is a translation into C of a Fortran subroutine
 *    by W. Fullerton of Los Alamos Scientific Laboratory.
 *
 *  REFERENCE
 *
 *    Bosten and Battiste (1974).
 *    Remark on Algorithm 179,
 *    CACM 17, p153, (1974).
 */

/* #include "Mathlib.h" */

/* This is called from	qbeta(.) in a root-finding loop --- be FAST! */

static
double pbeta_raw(double x, double pin, double qin)
{
    double ans, c, finsum, p, ps, p1, q, term, xb, xi, y;
    int n, i, ib;

    static double eps = 0;
    static double alneps = 0;
    static double sml = 0;
    static double alnsml = 0;

    if (eps == 0) {/* initialize machine constants ONCE */
	eps = d1mach(3);
	alneps = log(eps);
	sml = d1mach(1);
	alnsml = log(sml);
    }

    y = x;
    p = pin;
    q = qin;

    /* swap tails if x is greater than the mean */

    if (p / (p + q) < x) {
	y = 1 - y;
	p = qin;
	q = pin;
    }

    if ((p + q) * y / (p + 1) < eps) {

	/* tail approximation */

	ans = 0;
	xb = p * log(fmax2(y, sml)) - log(p) - lbeta(p, q);
	if (xb > alnsml && y != 0)
	    ans = exp(xb);
	if (y != x || p != pin)
	    ans = 1 - ans;
    }
    else {
	/*___ FIXME ___:  This takes forever (or ends wrongly) 
	  when (one or) both p & q  are huge 
	*/

	/* evaluate the infinite sum first.  term will equal */
	/* y^p / beta(ps, p) * (1 - ps)-sub-i * y^i / fac(i) */

	ps = q - floor(q);
	if (ps == 0)
	    ps = 1;
	xb = p * log(y) - lbeta(ps, p) - log(p);
	ans = 0;
	if (xb >= alnsml) {
	    ans = exp(xb);
	    term = ans * p;
	    if (ps != 1) {
		n = fmax2(alneps/log(y), 4.0);
		for(i=1 ; i<= n ; i++) {
		    xi = i;
		    term = term * (xi - ps) * y / xi;
		    ans = ans + term / (p + xi);
		}
	    }
	}

	/* now evaluate the finite sum, maybe. */

	if (q > 1) {
	    xb = p * log(y) + q * log(1 - y) - lbeta(p, q) - log(q);
	    ib = fmax2(xb / alnsml, 0.0);
	    term = exp(xb - ib * alnsml);
	    c = 1 / (1 - y);
	    p1 = q * c / (p + q - 1);

	    finsum = 0;
	    n = q;
	    if (q == n)
		n = n - 1;
	    for(i=1 ; i<=n ; i++) {
		if (p1 <= 1 && term / eps <= finsum)
		    break;
		xi = i;
		term = (q - xi + 1) * c * term / (p + q - xi);
		if (term > 1) {
		    ib = ib - 1;
		    term = term * sml;
		}
		if (ib == 0)
		    finsum = finsum + term;
	    }
	    ans = ans + finsum;
	}
	if (y != x || p != pin)
	    ans = 1 - ans;
	ans = fmax2(fmin2(ans, 1.0), 0.0);
    }
    return ans;
}

double pbeta(double x, double pin, double qin)
{
#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(pin) || ISNAN(qin))
	return x + pin + qin;
#endif

    if (pin <= 0 || qin <= 0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    if (x <= 0)
	return 0;
    if (x >= 1)
	return 1;
    return pbeta_raw(x, pin, qin);
}


/* src/nmath/qbeta.c ------------------------------------------------------- */
/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1998--1999  The R Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *

 * Reference:
 * Cran, G. W., K. J. Martin and G. E. Thomas (1977).
 *	Remark AS R19 and Algorithm AS 109,
 *	Applied Statistics, 26(1), 111-114.
 * Remark AS R83 (v.39, 309-310) and the correction (v.40(1) p.236)
 *	have been incorporated in this version.
 */

/* #include "Mathlib.h" */

static volatile double xtrunc;

double qbeta(double alpha, double p, double q)
{
	const double const1 = 2.30753;
	const double const2 = 0.27061;
	const double const3 = 0.99229;
	const double const4 = 0.04481;

	const double zero = 0.0;
	const double fpu = 3e-308;
	/* acu_min:  Minimal value for accuracy 'acu' which will depend on (a,p);
	   acu_min >= fpu ! */
	const double acu_min = 1e-300;
	const double lower = fpu;
	const double upper = 1-2.22e-16;

	int swap_tail, i_pb, i_inn;
	double a, adj, logbeta, g, h, pp, prev, qq, r, s, t, tx, w, y, yprev;
	double acu;
	volatile double xinbta;

	/* define accuracy and initialize */

	xinbta = alpha;

	/* test for admissibility of parameters */

#ifdef IEEE_754
	if (ISNAN(p) || ISNAN(q) || ISNAN(alpha))
		return p + q + alpha;
#endif
	if(p < zero || q < zero || alpha < zero || alpha > 1) {
		ML_ERROR(ME_DOMAIN);
		return ML_NAN;
	}
	if (alpha == zero || alpha == 1)
		return alpha;

	logbeta = lbeta(p, q);

	/* change tail if necessary;  afterwards   0 < a <= 1/2	 */
	if (alpha <= 0.5) {
		a = alpha;	pp = p; qq = q; swap_tail = 0;
	} else { /* change tail, swap  p <-> q :*/
		a = 1 - alpha; pp = q; qq = p; swap_tail = 1;
	}

	/* calculate the initial approximation */

	r = sqrt(-log(a * a));
	y = r - (const1 + const2 * r) / (1 + (const3 + const4 * r) * r);
	if (pp > 1 && qq > 1) {
		r = (y * y - 3) / 6;
		s = 1 / (pp + pp - 1);
		t = 1 / (qq + qq - 1);
		h = 2 / (s + t);
		w = y * sqrt(h + r) / h - (t - s) * (r + 5 / 6 - 2 / (3 * h));
		xinbta = pp / (pp + qq * exp(w + w));
	} else {
		r = qq + qq;
		t = 1 / (9 * qq);
		t = r * pow(1 - t + y * sqrt(t), 3);
		if (t <= zero)
			xinbta = 1 - exp((log((1 - a) * qq) + logbeta) / qq);
		else {
			t = (4 * pp + r - 2) / t;
			if (t <= 1)
				xinbta = exp((log(a * pp) + logbeta) / pp);
			else
				xinbta = 1 - 2 / (t + 1);
		}
	}

	/* solve for x by a modified newton-raphson method, */
	/* using the function pbeta_raw */

	r = 1 - pp;
	t = 1 - qq;
	yprev = zero;
	adj = 1;
	if (xinbta < lower)
	  xinbta = lower;
	else if (xinbta > upper)
	  xinbta = upper;

	/* Desired accuracy should depend on  (a,p)
	 * This is from Remark .. on AS 109, adapted.
	 * However, it's not clear if this is "optimal" for IEEE double prec.

	 * acu = fmax2(acu_min, pow(10., -25. - 5./(pp * pp) - 1./(a * a)));

	 * NEW: 'acu' accuracy NOT for squared adjustment, but simple;
	 * ---- i.e.,  "new acu" = sqrt(old acu)

	 */
	acu = fmax2(acu_min, pow(10., -13 - 2.5/(pp * pp) - 0.5/(a * a)));
	tx = prev = zero;	/* keep -Wall happy */

	for (i_pb=0; i_pb < 1000; i_pb++) {
		y = pbeta_raw(xinbta, pp, qq);
		/* y = pbeta_raw2(xinbta, pp, qq, logbeta) -- to SAVE CPU; */
#ifdef IEEE_754
		if(!FINITE(y))
#else
		if (errno)
#endif
		{ ML_ERROR(ME_DOMAIN); return ML_NAN; }
		y = (y - a) *
			exp(logbeta + r * log(xinbta) + t * log(1 - xinbta));
		if (y * yprev <= zero)
			prev = fmax2(fabs(adj),fpu);
		g = 1;
		for (i_inn=0; i_inn < 1000;i_inn++) {
		  adj = g * y;
		  if (fabs(adj) < prev) {
		    tx = xinbta - adj; /* trial new x */
		    if (tx >= zero && tx <= 1) {
		      if (prev <= acu)	  goto L_converged;
		      if (fabs(y) <= acu) goto L_converged;
		      if (tx != zero && tx != 1)
			break;
		    }
		  }
		  g /= 3;
		}
		xtrunc = tx;	/* this prevents trouble with excess FPU */
				/* precision on some machines. */
		if (xtrunc == xinbta)
			goto L_converged;
		xinbta = tx;
		yprev = y;
	}
	/*-- NOT converged: Iteration count --*/
	ML_ERROR(ME_PRECISION);

      L_converged:
	if (swap_tail)
		xinbta = 1 - xinbta;
	return xinbta;
}


/* src/nmath/pt.c ---------------------------------------------------------- */
/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* #include "Mathlib.h" */

double pt(double x, double n)
{
/* return  P[ T <= x ]	where  
 * T ~ t_{n}  (t distrib. with n degrees of freedom).	

 *	--> ./pnt.c for NON-central
 */
    double val;
#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(n))
	return x + n;
#endif
    if (n <= 0.0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
#ifdef IEEE_754
    if(!finite(x))
	return (x < 0) ? 0 : 1;
    if(!finite(n))
	return pnorm(x, 0.0, 1.0);
#endif
    if (n > 4e5) { /*-- Fixme(?): test should depend on `n' AND `x' ! */
	/* Approx. from	 Abramowitz & Stegun 26.7.8 (p.949) */
	val = 1./(4.*n);
	return pnorm(x*(1. - val)/sqrt(1. + x*x*2.*val), 0.0, 1.0);
    }
    val = 0.5 * pbeta(n / (n + x * x), n / 2.0, 0.5);
    return (x > 0.0) ? 1 - val : val;
}


/* src/nmath/pt.c ---------------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double qt(double p, double ndf);
 *
 *  DESCRIPTION
 *
 *    The "Student" t distribution quantile function.
 *
 *  NOTES
 *
 *    This is a C translation of the Fortran routine given in:
 *    Algorithm 396: Student's t-quantiles by G.W. Hill
 *    CACM 13(10), 619-620, October 1970
 */

/* #include "Mathlib.h" */

static double eps = 1.e-12;

double qt(double p, double ndf)
{
	double a, b, c, d, prob, P, q, x, y;
	int neg;

#ifdef IEEE_754
	if (ISNAN(p) || ISNAN(ndf))
		return p + ndf;
	if(ndf < 1 || p > 1 || p < 0) {
	    ML_ERROR(ME_DOMAIN);
	    return ML_NAN;
	}
	if (p == 0) return ML_NEGINF;
	if (p == 1) return ML_POSINF;
#else
	if (ndf < 1 || p > 1 || p < 0) {
	    ML_ERROR(ME_DOMAIN);
	    return ML_NAN;
	}
#endif
	/* FIXME: This test should depend on  ndf  AND p  !!
	 * -----  and in fact should be replaced by
	 * something like Abramowitz & Stegun 26.7.5 (p.949) 
	 */
	if (ndf > 1e20) return qnorm(p, 0.0, 1.0);

	if(p > 0.5) {
		neg = 0; P = 2 * (1 - p);
	} else {
		neg = 1; P = 2 * p;
	}

	if (fabs(ndf - 2) < eps) {
		/* df ~= 2 */
		q = sqrt(2 / (P * (2 - P)) - 2);
	}
	else if (ndf < 1 + eps) {
		/* df ~= 1 */
		prob = P * M_PI_half;
		q = cos(prob) / sin(prob);
	}
	else {
		/*-- usual case;  including, e.g.,  df = 1.1 */
		a = 1 / (ndf - 0.5);
		b = 48 / (a * a);
		c = ((20700 * a / b - 98) * a - 16) * a + 96.36;
		d = ((94.5 / (b + c) - 3) / b + 1) * sqrt(a * M_PI_half) * ndf;
		y = pow(d * P, 2 / ndf);

		if (y > 0.05 + a) {
			/* Asymptotic inverse expansion about normal */
			x = qnorm(0.5 * P, 0.0, 1.0);
			y = x * x;
			if (ndf < 5)
				c = c + 0.3 * (ndf - 4.5) * (x + 0.6);
			c = (((0.05 * d * x - 5) * x - 7) * x - 2) * x + b + c;
			y = (((((0.4 * y + 6.3) * y + 36) * y + 94.5) / c - y - 3) / b + 1) * x;
			y = a * y * y;
			if (y > 0.002)
				y = exp(y) - 1;
			else {
				/* Taylor of  e^y -1 : */
				y = 0.5 * y * y + y;
			}
		} else {
			y = ((1 / (((ndf + 6) / (ndf * y) - 0.089 * d - 0.822)
				   * (ndf + 2) * 3) + 0.5 / (ndf + 4))
			     * y - 1) * (ndf + 1) / (ndf + 2) + 1 / y;
		}
		q = sqrt(ndf * y);
	}
	if(neg) q = -q;
	return q;
}

/* src/nmath/pf.c ---------------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double pf(double x, double n1, double n2);
 *
 *  DESCRIPTION
 *
 *    The distribution function of the F distribution.
 */

/* #include "Mathlib.h" */

double pf(double x, double n1, double n2)
{
#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(n1) || ISNAN(n2))
	return x + n2 + n1;
#endif
    if (n1 <= 0.0 || n2 <= 0.0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    if (x <= 0.0)
	return 0.0;
    x = 1.0 - pbeta(n2 / (n2 + n1 * x), n2 / 2.0, n1 / 2.0);
    return ML_VALID(x) ? x : ML_NAN;
}


/* src/nmath/qf.c ---------------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double qf(double x, double n1, double n2);
 *
 *  DESCRIPTION
 *
 *    The quantile function of the F distribution.
*/

/* #include "Mathlib.h" */

double qf(double x, double n1, double n2)
{
#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(n1) || ISNAN(n2))
	return x + n1 + n2;
#endif
    if (n1 <= 0.0 || n2 <= 0.0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    if (x <= 0.0)
	return 0.0;
    x = (1.0 / qbeta(1.0 - x, n2 / 2.0, n1 / 2.0) - 1.0) * (n2 / n1);
    return ML_VALID(x) ? x : ML_NAN;
}


/* src/nmath/pchisq.c ------------------------------------------------------ */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double pchisq(double x, double df);
 *
 *  DESCRIPTION
 *
 *    The disribution function of the chi-squared distribution.
 */

/* #include "Mathlib.h" */

double pchisq(double x, double df)
{
    return pgamma(x, df / 2.0, 2.0);
}


/* src/nmath/qchisq.c ------------------------------------------------------ */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  SYNOPSIS
 *
 *    #include "Mathlib.h"
 *    double qchisq(double p, double df);
 *
 *  DESCRIPTION
 *
 *    The quantile function of the chi-squared distribution.
 */

/* #include "Mathlib.h" */

double qchisq(double p, double df)
{
    return qgamma(p, 0.5 * df, 2.0);
}
