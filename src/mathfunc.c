/*
 * mathfunc.c:  Mathematical functions.
 *
 * Authors:
 *   Ross Ihaka (See also note below.)
 *   Morten Welinder <terra@diku.dk>
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 */

/*
 * NOTE: most of this file comes from the "R" package, notably version 0.64.
 * "R" is distributed under GPL licence, see file COPYING.
 * The relevant parts are copyright (C) 1998 Ross Ihaka.
 *
 * Thank you Ross!
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "mathfunc.h"

#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <float.h>
#include <fcntl.h>
#include <unistd.h>

#if defined (HAVE_IEEEFP_H) || defined (HAVE_IEEE754_H)
/* Make sure we have this symbol defined, since the existance of either
   header file implies it.  */
#ifndef IEEE_754
#define IEEE_754
#endif
#endif

#define M_LN_SQRT_2PI   0.918938533204672741780329736406  /* log(sqrt(2*pi)) */
#define M_SQRT_32       5.656854249492380195206754896838  /* sqrt(32) */
#define M_1_SQRT_2PI    0.398942280401432677939946059934  /* 1/sqrt(2pi) */
#define M_SQRT_2dPI     0.797884560802865355879892119869  /* sqrt(2/pi) */
#define M_PI_half       M_PI_2

#ifndef ISNAN
#define ISNAN isnan
#endif

/* Any better idea for a quick hack?  */
#define ML_NAN (-HUGE_VAL * 0.0)
#define ML_NEGINF (-HUGE_VAL)
#define ML_POSINF (HUGE_VAL)
#define ML_UNDERFLOW (DBL_EPSILON * DBL_EPSILON)
#define ML_VALID(_x) (!ISNAN (_x))
#define ML_ERROR(cause) /* Nothing */
#define MATHLIB_WARNING2(_f,_a,_b) do { (void)(_f); (void)(_a); (void)(_b); } while (0)
#define MATHLIB_WARNING4(_f,_a,_b,_c,_d) do { (void)(_f); (void)(_a); (void)(_b); (void)(_c); (void)(_c); } while (0)

#define fmin2(_x,_y) MIN(_x, _y)
#define imin2(_x,_y) MIN(_x, _y)
#define fmax2(_x,_y) MAX(_x, _y)
#define imax2(_x,_y) MAX(_x, _y)

#define lgammafn(_x) lgamma (_x)
#define gammafn(_x) exp (lgammafn (_x))
#define gamma_cody(_x) gammafn (_x)
#define lfastchoose(_n,_k) (lgammafn((_n) + 1.0) - lgammafn((_k) + 1.0) - lgammafn((_n) - (_k) + 1.0))
#define ftrunc(_x) floor (_x)
#define pow_di(_px,_pn) pow (*(_px), *(_pn))

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

/*
 * In preparation for truncation, make the value a tiny bit larger (seen
 * absolutely).  This makes ROUND (etc.) behave a little closer to what
 * people want, even if it is a bit bogus.
 */
double
gnumeric_add_epsilon (double x)
{
  if (!FINITE (x) || x == 0)
    return x;
  else {
    int exp;
    double mant = frexp (fabs (x), &exp);
    double absres = ldexp (mant + DBL_EPSILON, exp);
    return (x < 0) ? -absres : absres;
  }
}

double
gnumeric_sub_epsilon (double x)
{
  if (!FINITE (x) || x == 0)
    return x;
  else {
    int exp;
    double mant = frexp (fabs (x), &exp);
    double absres = ldexp (mant - DBL_EPSILON, exp);
    return (x < 0) ? -absres : absres;
  }
}

double
gnumeric_fake_floor (double x)
{
  return floor (gnumeric_add_epsilon (x));
}

double
gnumeric_fake_ceil (double x)
{
  return ceil (gnumeric_sub_epsilon (x));
}

double
gnumeric_fake_round (double x)
{
  return (x >= 0)
    ? gnumeric_fake_floor (x + 0.5)
    : -gnumeric_fake_floor (-x + 0.5);
}

double
gnumeric_fake_trunc (double x)
{
  return (x >= 0)
    ? gnumeric_fake_floor (x)
    : gnumeric_fake_ceil (x);
}


double
pweibull (double x, double shape, double scale)
{
    if (shape <= 0 || scale <= 0)
	    return ML_NAN;
    else if (x <= 0)
	    return 0;
    else
	    return -expm1 (-pow (x / scale, shape));
}

double
dexp (double x, double scale)
{
	if (scale <= 0)
		return ML_NAN;
	else if (x < 0)
		return 0;
	else
		return exp(-x / scale) / scale;
}


double
pexp (double x, double scale)
{
	if (scale <= 0)
		return ML_NAN;
	else if (x <= 0)
		return 0;
	else
		return -expm1 (-x / scale);
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
    if(!FINITE (x)) {
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
	if(!FINITE (p2))
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
    else if (!FINITE (q)) {
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
		if(!FINITE (y))
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
    if(!FINITE (x))
	return (x < 0) ? 0 : 1;
    if(!FINITE (n))
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


/* src/nmath/dweibull.c ---------------------------------------------------- */
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
 *    double dweibull(double x, double shape, double scale);
 *
 *  DESCRIPTION
 *
 *    The density function of the Weibull distribution.
 */

/* #include "Mathlib.h" */

double dweibull(double x, double shape, double scale)
{
    double tmp1, tmp2;
#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(shape) || ISNAN(scale))
	return x + shape + scale;
#endif
    if (shape <= 0 || scale <= 0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    if (x <= 0) return 0;
#ifdef IEEE_754
    if (!FINITE (x)) return 0;
#endif
    tmp1 = pow(x / scale, shape - 1);
    tmp2 = tmp1 * (x / scale);
    return shape * tmp1 * exp(-tmp2) / scale;
}


/* src/nmath/ppois.c ------------------------------------------------------- */
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
 *    double ppois(double x, double lambda)
 *
 *  DESCRIPTION
 *
 *    The distribution function of the Poisson distribution.
 */

/* #include "Mathlib.h" */

double ppois(double x, double lambda)
{
#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(lambda))
	return x + lambda;
#endif
    if(lambda <= 0.0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    x = floor(x + 0.5);
    if (x < 0)
	return 0;
#ifdef IEEE_754
    if (!FINITE (x))
	return 1;
#endif
    return  1 - pgamma(lambda, x + 1, 1.0);
}


/* src/nmath/dpois.c ------------------------------------------------------- */
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
 *    double dpois(double x, double lambda)
 *
 *  DESCRIPTION
 *
 *    The density function of the Poisson distribution.
 */

/* #include "Mathlib.h" */

double dpois(double x, double lambda)
{
#ifdef IEEE_754
    if(ISNAN(x) || ISNAN(lambda))
	return x + lambda;
#endif
    x = floor(x + 0.5);
    if(lambda <= 0.0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    if (x < 0)
	return 0;
#ifdef IEEE_754
    if(!FINITE (x))
	return 0;
#endif
    return exp(x * log(lambda) - lambda - lgammafn(x + 1));
}


/* src/nmath/pbinom.c ------------------------------------------------------ */
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
 *    double pbinom(double x, double n, double p)
 *
 *  DESCRIPTION
 *
 *    The distribution function of the binomial distribution.
 */

/* #include "Mathlib.h" */

double pbinom(double x, double n, double p)
{
#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(n) || ISNAN(p))
	return x + n + p;
    if (!FINITE (n) || !FINITE (p)) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
#endif
    n = floor(n + 0.5);
    if(n <= 0 || p < 0 || p > 1) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    x = floor(x);
    if (x < 0.0) return 0;
    if (n <= x) return 1;
    return pbeta(1.0 - p, n - x, x + 1);
}


/* src/nmath/dbinom.c ------------------------------------------------------ */
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
 *    double dbinom(double x, double n, double p)
 *
 *  DESCRIPTION
 *
 *    The density of the binomial distribution.
 */

/* #include "Mathlib.h" */

double dbinom(double x, double n, double p)
{
#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (ISNAN(x) || ISNAN(n) || ISNAN(p)) return x + n + p;
#endif
    n = floor(n + 0.5);
    if(n <= 0 || p < 0 || p > 1) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    x = floor(x + 0.5);
    if (x < 0 || x > n)
	return 0;
    if (p == 0)
	return (x == 0) ? 1 : 0;
    if (p == 1)
	return (x == n) ? 1 : 0;
    return exp(lfastchoose(n, x) + log(p) * x + (n - x) * log(1 - p));
}




/* src/nmath/qbinom.c ------------------------------------------------------ */
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
 *    double qbinom(double x, double n, double p);
 *
 *  DESCRIPTION
 *
 *    The quantile function of the binomial distribution.
 *
 *  NOTES
 *
 *    The function uses the Cornish-Fisher Expansion to include
 *    a skewness correction to a normal approximation.  This gives
 *    an initial value which never seems to be off by more than
 *    1 or 2.  A search is then conducted of values close to
 *    this initial start point.
 */

/* #include "Mathlib.h" */

double qbinom(double x, double n, double p)
{
    double q, mu, sigma, gamma, z, y;

#ifdef IEEE_754
    if (ISNAN(x) || ISNAN(n) || ISNAN(p))
	return x + n + p;
    if(!FINITE (x) || !FINITE (n) || !FINITE (p)) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
#endif

    n = floor(n + 0.5);
    if (x < 0 || x > 1 || p <= 0 || p >= 1 || n <= 0) {
	ML_ERROR(ME_DOMAIN);
	return ML_NAN;
    }
    if (x == 0) return 0.0;
    if (x == 1) return n;
    q = 1 - p;
    mu = n * p;
    sigma = sqrt(n * p * q);
    gamma = (q-p)/sigma;
    z = qnorm(x, 0.0, 1.0);
    y = floor(mu + sigma * (z + gamma * (z*z - 1) / 6) + 0.5);

    z = pbinom(y, n, p);
    if(z >= x) {

	/* search to the left */

	for(;;) {
	    if((z = pbinom(y - 1, n, p)) < x)
		return y;
	    y = y - 1;
	}
    }
    else {

	/* search to the right */

	for(;;) {
	    if((z = pbinom(y + 1, n, p)) >= x)
		return y + 1;
	    y = y + 1;
	}
    }
}


/* src/nmath/bessel_i.c ---------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka and the R Core team.
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

/*  DESCRIPTION --> see below */


/* From http://www.netlib.org/specfun/ribesl	Fortran translated by f2c,...
 *	------------------------------=#----	Martin Maechler, ETH Zurich
 */
/* #include "Mathlib.h" */

static double exparg = 709.;/* maximal x for UNscaled answer, see below */

static
void I_bessel(double *x, double *alpha, long *nb,
	      long *ize, double *bi, long *ncalc)
{
/* -------------------------------------------------------------------

 This routine calculates Bessel functions I_(N+ALPHA) (X)
 for non-negative argument X, and non-negative order N+ALPHA,
 with or without exponential scaling.


 Explanation of variables in the calling sequence

 X     - Non-negative argument for which
	 I's or exponentially scaled I's (I*EXP(-X))
	 are to be calculated.	If I's are to be calculated,
	 X must be less than EXPARG (see below).
 ALPHA - Fractional part of order for which
	 I's or exponentially scaled I's (I*EXP(-X)) are
	 to be calculated.  0 <= ALPHA < 1.0.
 NB    - Number of functions to be calculated, NB > 0.
	 The first function calculated is of order ALPHA, and the
	 last is of order (NB - 1 + ALPHA).
 IZE   - Type.	IZE = 1 if unscaled I's are to be calculated,
		    = 2 if exponentially scaled I's are to be calculated.
 BI    - Output vector of length NB.	If the routine
	 terminates normally (NCALC=NB), the vector BI contains the
	 functions I(ALPHA,X) through I(NB-1+ALPHA,X), or the
	 corresponding exponentially scaled functions.
 NCALC - Output variable indicating possible errors.
	 Before using the vector BI, the user should check that
	 NCALC=NB, i.e., all orders have been calculated to
	 the desired accuracy.	See error returns below.


 *******************************************************************
 *******************************************************************

 Error returns

  In case of an error,	NCALC != NB, and not all I's are
  calculated to the desired accuracy.

  NCALC < 0:  An argument is out of range. For example,
     NB <= 0, IZE is not 1 or 2, or IZE=1 and ABS(X) >= EXPARG.
     In this case, the BI-vector is not calculated, and NCALC is
     set to MIN0(NB,0)-1 so that NCALC != NB.

  NB > NCALC > 0: Not all requested function values could
     be calculated accurately.	This usually occurs because NB is
     much larger than ABS(X).  In this case, BI[N] is calculated
     to the desired accuracy for N <= NCALC, but precision
     is lost for NCALC < N <= NB.  If BI[N] does not vanish
     for N > NCALC (because it is too small to be represented),
     and BI[N]/BI[NCALC] = 10**(-K), then only the first NSIG-K
     significant figures of BI[N] can be trusted.


 Intrinsic functions required are:

     DBLE, EXP, gamma_cody, INT, MAX, MIN, REAL, SQRT


 Acknowledgement

  This program is based on a program written by David J.
  Sookne (2) that computes values of the Bessel functions J or
  I of float argument and long order.  Modifications include
  the restriction of the computation to the I Bessel function
  of non-negative float argument, the extension of the computation
  to arbitrary positive order, the inclusion of optional
  exponential scaling, and the elimination of most underflow.
  An earlier version was published in (3).

 References: "A Note on Backward Recurrence Algorithms," Olver,
	      F. W. J., and Sookne, D. J., Math. Comp. 26, 1972,
	      pp 941-947.

	     "Bessel Functions of Real Argument and Integer Order,"
	      Sookne, D. J., NBS Jour. of Res. B. 77B, 1973, pp
	      125-132.

	     "ALGORITHM 597, Sequence of Modified Bessel Functions
	      of the First Kind," Cody, W. J., Trans. Math. Soft.,
	      1983, pp. 242-245.

  Latest modification: May 30, 1989

  Modified by: W. J. Cody and L. Stoltz
	       Applied Mathematics Division
	       Argonne National Laboratory
	       Argonne, IL  60439
*/

    /*-------------------------------------------------------------------
      Mathematical constants
      -------------------------------------------------------------------*/
    static double const__ = 1.585;

/* *******************************************************************

 Explanation of machine-dependent constants

   beta	  = Radix for the floating-point system
   minexp = Smallest representable power of beta
   maxexp = Smallest power of beta that overflows
   it	  = Number of bits in the mantissa of a working precision variable
   NSIG	  = Decimal significance desired.  Should be set to
	    INT(LOG10(2)*it+1).	 Setting NSIG lower will result
	    in decreased accuracy while setting NSIG higher will
	    increase CPU time without increasing accuracy.  The
	    truncation error is limited to a relative error of
	    T=.5*10**(-NSIG).
   ENTEN  = 10.0 ** K, where K is the largest long such that
	    ENTEN is machine-representable in working precision
   ENSIG  = 10.0 ** NSIG
   RTNSIG = 10.0 ** (-K) for the smallest long K such that
	    K >= NSIG/4
   ENMTEN = Smallest ABS(X) such that X/4 does not underflow
   XLARGE = Upper limit on the magnitude of X when IZE=2.  Bear
	    in mind that if ABS(X)=N, then at least N iterations
	    of the backward recursion will be executed.	 The value
	    of 10.0 ** 4 is used on every machine.
   EXPARG = Largest working precision argument that the library
	    EXP routine can handle and upper limit on the
	    magnitude of X when IZE=1; approximately
	    LOG(beta**maxexp)


     Approximate values for some important machines are:

			beta	   minexp      maxexp	    it

  CRAY-1	(S.P.)	  2	   -8193	8191	    48
  Cyber 180/855
    under NOS	(S.P.)	  2	    -975	1070	    48
  IEEE (IBM/XT,
    SUN, etc.)	(S.P.)	  2	    -126	 128	    24
  IEEE (IBM/XT,
    SUN, etc.)	(D.P.)	  2	   -1022	1024	    53
  IBM 3033	(D.P.)	 16	     -65	  63	    14
  VAX		(S.P.)	  2	    -128	 127	    24
  VAX D-Format	(D.P.)	  2	    -128	 127	    56
  VAX G-Format	(D.P.)	  2	   -1024	1023	    53


			NSIG	   ENTEN       ENSIG	  RTNSIG

 CRAY-1	       (S.P.)	 15	  1.0E+2465   1.0E+15	  1.0E-4
 Cyber 180/855
   under NOS   (S.P.)	 15	  1.0E+322    1.0E+15	  1.0E-4
 IEEE (IBM/XT,
   SUN, etc.)  (S.P.)	  8	  1.0E+38     1.0E+8	  1.0E-2
 IEEE (IBM/XT,
   SUN, etc.)  (D.P.)	 16	  1.0D+308    1.0D+16	  1.0D-4
 IBM 3033      (D.P.)	  5	  1.0D+75     1.0D+5	  1.0D-2
 VAX	       (S.P.)	  8	  1.0E+38     1.0E+8	  1.0E-2
 VAX D-Format  (D.P.)	 17	  1.0D+38     1.0D+17	  1.0D-5
 VAX G-Format  (D.P.)	 16	  1.0D+307    1.0D+16	  1.0D-4


			 ENMTEN	     XLARGE   EXPARG

 CRAY-1	       (S.P.)	1.84E-2466   1.0E+4    5677
 Cyber 180/855
   under NOS   (S.P.)	1.25E-293    1.0E+4	741
 IEEE (IBM/XT,
   SUN, etc.)  (S.P.)	4.70E-38     1.0E+4	 88
 IEEE (IBM/XT,
   SUN, etc.)  (D.P.)	8.90D-308    1.0D+4	709
 IBM 3033      (D.P.)	2.16D-78     1.0D+4	174
 VAX	       (S.P.)	1.17E-38     1.0E+4	 88
 VAX D-Format  (D.P.)	1.17D-38     1.0D+4	 88
 VAX G-Format  (D.P.)	2.22D-308    1.0D+4	709

 *******************************************************************
 -------------------------------------------------------------------
  Machine-dependent parameters
 -------------------------------------------------------------------
*/
    static long	   nsig =   16;
    static double ensig = 1e16;
    static double rtnsig = 1e-4;
    static double enmten = 8.9e-308;
    static double enten = 1e308;
    static double xlarge = 1e4;

#if 0
    extern double gamma_cody(double);/*--> ./gamma.c */

    /* Builtin functions */
    double pow_di(double *, long *);
#endif

    /* Local variables */
    long nend, intx, nbmx, k, l, n, nstart;
    double pold, test,	p, em, en, empal, emp2al, halfx,
	aa, bb, cc, psave, plast, tover, psavel, sum, nu, twonu;

    /*Parameter adjustments */
    --bi;
    nu = *alpha;
    twonu = nu + nu;

    /*-------------------------------------------------------------------
      Check for X, NB, OR IZE out of range.
      ------------------------------------------------------------------- */
    if (*nb > 0 && *x >= 0. &&	(0. <= nu && nu < 1.) &&
	(1 <= *ize && *ize <= 2) ) {

	*ncalc = *nb;
	if((*ize == 1 && *x > exparg) ||
	   (*ize == 2 && *x > xlarge)) {
	    ML_ERROR(ME_RANGE);
	    for(k=1; k <= *nb; k++)
		bi[k]=ML_POSINF;
	    return;
	}
	intx = (long) (*x);
	if (*x >= rtnsig) { /* "non-small" x */
/* -------------------------------------------------------------------
   Initialize the forward sweep, the P-sequence of Olver
   ------------------------------------------------------------------- */
	    nbmx = *nb - intx;
	    n = intx + 1;
	    en = (double) (n + n) + twonu;
	    plast = 1.;
	    p = en / *x;
	    /* ------------------------------------------------
	       Calculate general significance test
	       ------------------------------------------------ */
	    test = ensig + ensig;
	    if (intx << 1 > nsig * 5) {
		test = sqrt(test * p);
	    } else {
		test /= pow_di(&const__, &intx);
	    }
	    if (nbmx >= 3) {
		/* --------------------------------------------------
		   Calculate P-sequence until N = NB-1
		   Check for possible overflow.
		   ------------------------------------------------ */
		tover = enten / ensig;
		nstart = intx + 2;
		nend = *nb - 1;
		for (k = nstart; k <= nend; ++k) {
		    n = k;
		    en += 2.;
		    pold = plast;
		    plast = p;
		    p = en * plast / *x + pold;
		    if (p > tover) {
			/* ------------------------------------------------
			   To avoid overflow, divide P-sequence by TOVER.
			   Calculate P-sequence until ABS(P) > 1.
			   ---------------------------------------------- */
			tover = enten;
			p /= tover;
			plast /= tover;
			psave = p;
			psavel = plast;
			nstart = n + 1;
			do {
			    ++n;
			    en += 2.;
			    pold = plast;
			    plast = p;
			    p = en * plast / *x + pold;
			}
			while (p <= 1.);

			bb = en / *x;
			/* ------------------------------------------------
			   Calculate backward test, and find NCALC,
			   the highest N such that the test is passed.
			   ------------------------------------------------ */
			test = pold * plast / ensig;
			test *= .5 - .5 / (bb * bb);
			p = plast * tover;
			--n;
			en -= 2.;
			nend = imin2(*nb,n);
			for (l = nstart; l <= nend; ++l) {
			    *ncalc = l;
			    pold = psavel;
			    psavel = psave;
			    psave = en * psavel / *x + pold;
			    if (psave * psavel > test) {
				goto L90;
			    }
			}
			*ncalc = nend + 1;
L90:
			--(*ncalc);
			goto L120;
		    }
		}
		n = nend;
		en = (double)(n + n) + twonu;
		/*---------------------------------------------------
		  Calculate special significance test for NBMX > 2.
		  --------------------------------------------------- */
		test = fmax2(test,sqrt(plast * ensig) * sqrt(p + p));
	    }
	    /* --------------------------------------------------------
	       Calculate P-sequence until significance test passed.
	       -------------------------------------------------------- */
	    do {
		++n;
		en += 2.;
		pold = plast;
		plast = p;
		p = en * plast / *x + pold;
	    } while (p < test);

L120:
/* -------------------------------------------------------------------
 Initialize the backward recursion and the normalization sum.
 ------------------------------------------------------------------- */
	    ++n;
	    en += 2.;
	    bb = 0.;
	    aa = 1. / p;
	    em = (double) n - 1.;
	    empal = em + nu;
	    emp2al = em - 1. + twonu;
	    sum = aa * empal * emp2al / em;
	    nend = n - *nb;
	    if (nend < 0) {
		/* -----------------------------------------------------
		   N < NB, so store BI[N] and set higher orders to 0..
		   ----------------------------------------------------- */
		bi[n] = aa;
		nend = -nend;
		for (l = 1; l <= nend; ++l) {
		    bi[n + l] = 0.;
		}
	    } else {
		if (nend > 0) {
		    /* -----------------------------------------------------
		       Recur backward via difference equation,
		       calculating (but not storing) BI[N], until N = NB.
		       --------------------------------------------------- */
		    for (l = 1; l <= nend; ++l) {
			--n;
			en -= 2.;
			cc = bb;
			bb = aa;
			aa = en * bb / *x + cc;
			em -= 1.;
			emp2al -= 1.;
			if (n == 1) {
			    break;
			}
			if (n == 2) {
			    emp2al = 1.;
			}
			empal -= 1.;
			sum = (sum + aa * empal) * emp2al / em;
		    }
		}
		/* ---------------------------------------------------
		   Store BI[NB]
		   --------------------------------------------------- */
		bi[n] = aa;
		if (*nb <= 1) {
		    sum = sum + sum + aa;
		    goto L230;
		}
		/* -------------------------------------------------
		   Calculate and Store BI[NB-1]
		   ------------------------------------------------- */
		--n;
		en -= 2.;
		bi[n] = en * aa / *x + bb;
		if (n == 1) {
		    goto L220;
		}
		em -= 1.;
		if (n == 2)
		    emp2al = 1.;
		else
		    emp2al -= 1.;

		empal -= 1.;
		sum = (sum + bi[n] * empal) * emp2al / em;
	    }
	    nend = n - 2;
	    if (nend > 0) {
		/* --------------------------------------------
		   Calculate via difference equation
		   and store BI[N], until N = 2.
		   ------------------------------------------ */
		for (l = 1; l <= nend; ++l) {
		    --n;
		    en -= 2.;
		    bi[n] = en * bi[n + 1] / *x + bi[n + 2];
		    em -= 1.;
		    if (n == 2)
			emp2al = 1.;
		    else
			emp2al -= 1.;
		    empal -= 1.;
		    sum = (sum + bi[n] * empal) * emp2al / em;
		}
	    }
	    /* ----------------------------------------------
	       Calculate BI[1]
	       -------------------------------------------- */
	    bi[1] = 2. * empal * bi[2] / *x + bi[3];
L220:
	    sum = sum + sum + bi[1];

L230:
	    /* ---------------------------------------------------------
	       Normalize.  Divide all BI[N] by sum.
	       --------------------------------------------------------- */
	    if (nu != 0.)
		sum *= (gamma_cody(1. + nu) * pow(*x * .5, -nu));
	    if (*ize == 1)
		sum *= exp(-(*x));
	    aa = enmten;
	    if (sum > 1.)
		aa *= sum;
	    for (n = 1; n <= *nb; ++n) {
		if (bi[n] < aa)
		    bi[n] = 0.;
		else
		    bi[n] /= sum;
	    }
	    return;
	} else {
	    /* -----------------------------------------------------------
	       Two-term ascending series for small X.
	       -----------------------------------------------------------*/
	    aa = 1.;
	    empal = 1. + nu;
	    if (*x > enmten)
		halfx = .5 * *x;
	    else
		halfx = 0.;
	    if (nu != 0.)
		aa = pow(halfx, nu) / gamma_cody(empal);
	    if (*ize == 2)
		aa *= exp(-(*x));
	    if (*x + 1. > 1.)
		bb = halfx * halfx;
	    else
		bb = 0.;

	    bi[1] = aa + aa * bb / empal;
	    if (*x != 0. && bi[1] == 0.)
		*ncalc = 0;
	    if (*nb > 1) {
		if (*x == 0.) {
		    for (n = 2; n <= *nb; ++n) {
			bi[n] = 0.;
		    }
		} else {
		    /* -------------------------------------------------
		       Calculate higher-order functions.
		       ------------------------------------------------- */
		    cc = halfx;
		    tover = (enmten + enmten) / *x;
		    if (bb != 0.)
			tover = enmten / bb;
		    for (n = 2; n <= *nb; ++n) {
			aa /= empal;
			empal += 1.;
			aa *= cc;
			if (aa <= tover * empal)
			    bi[n] = aa = 0.;
			else
			    bi[n] = aa + aa * bb / empal;
			if (bi[n] == 0. && *ncalc > n)
			    *ncalc = n - 1;
		    }
		}
	    }
	}
    } else {
	*ncalc = imin2(*nb,0) - 1;
    }
}


double bessel_i(double x, double alpha, double expo)
{
    long nb, ncalc, ize;
    double *bi;
#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (ISNAN(x) || ISNAN(alpha)) return x + alpha;
#endif
    ize = (long)expo;
    nb = 1+ (long)floor(alpha);/* nb-1 <= alpha < nb */
    alpha -= (nb-1);
    bi = (double *) calloc(nb, sizeof(double));
    I_bessel(&x, &alpha, &nb, &ize, bi, &ncalc);
    if(ncalc != nb) {/* error input */
	if(ncalc < 0)
	    MATHLIB_WARNING4("bessel_i(%g): ncalc (=%d) != nb (=%d); alpha=%g."
			     " Arg. out of range?\n",
			     x, ncalc, nb, alpha);
	else
	    MATHLIB_WARNING2("bessel_i(%g,nu=%g): precision lost in result\n",
			     x, alpha+nb-1);
    }
    x = bi[nb-1];
    free(bi);
    return x;
}



/* src/nmath/bessel_k.c ---------------------------------------------------- */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka and the R Core team.
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

/*  DESCRIPTION --> see below */


/* From http://www.netlib.org/specfun/rkbesl	Fortran translated by f2c,...
 *	------------------------------=#----	Martin Maechler, ETH Zurich
 */
/* #include "Mathlib.h" */

static double xmax = 705.342;/* maximal x for UNscaled answer, see below */

static
void K_bessel(double *x, double *alpha, long *nb,
	      long *ize, double *bk, long *ncalc)
{
/*-------------------------------------------------------------------

  This routine calculates modified Bessel functions
  of the third kind, K_(N+ALPHA) (X), for non-negative
  argument X, and non-negative order N+ALPHA, with or without
  exponential scaling.

  Explanation of variables in the calling sequence

 X     - Non-negative argument for which
	 K's or exponentially scaled K's (K*EXP(X))
	 are to be calculated.	If K's are to be calculated,
	 X must not be greater than XMAX (see below).
 ALPHA - Fractional part of order for which
	 K's or exponentially scaled K's (K*EXP(X)) are
	 to be calculated.  0 <= ALPHA < 1.0.
 NB    - Number of functions to be calculated, NB > 0.
	 The first function calculated is of order ALPHA, and the
	 last is of order (NB - 1 + ALPHA).
 IZE   - Type.	IZE = 1 if unscaled K's are to be calculated,
		    = 2 if exponentially scaled K's are to be calculated.
 BK    - Output vector of length NB.	If the
	 routine terminates normally (NCALC=NB), the vector BK
	 contains the functions K(ALPHA,X), ... , K(NB-1+ALPHA,X),
	 or the corresponding exponentially scaled functions.
	 If (0 < NCALC < NB), BK(I) contains correct function
	 values for I <= NCALC, and contains the ratios
	 K(ALPHA+I-1,X)/K(ALPHA+I-2,X) for the rest of the array.
 NCALC - Output variable indicating possible errors.
	 Before using the vector BK, the user should check that
	 NCALC=NB, i.e., all orders have been calculated to
	 the desired accuracy.	See error returns below.


 *******************************************************************

 Error returns

  In case of an error, NCALC != NB, and not all K's are
  calculated to the desired accuracy.

  NCALC < -1:  An argument is out of range. For example,
	NB <= 0, IZE is not 1 or 2, or IZE=1 and ABS(X) >= XMAX.
	In this case, the B-vector is not calculated,
	and NCALC is set to MIN0(NB,0)-2	 so that NCALC != NB.
  NCALC = -1:  Either  K(ALPHA,X) >= XINF  or
	K(ALPHA+NB-1,X)/K(ALPHA+NB-2,X) >= XINF.	 In this case,
	the B-vector is not calculated.	Note that again
	NCALC != NB.

  0 < NCALC < NB: Not all requested function values could
	be calculated accurately.  BK(I) contains correct function
	values for I <= NCALC, and contains the ratios
	K(ALPHA+I-1,X)/K(ALPHA+I-2,X) for the rest of the array.


 Intrinsic functions required are:

     ABS, AINT, EXP, INT, LOG, MAX, MIN, SINH, SQRT


 Acknowledgement

	This program is based on a program written by J. B. Campbell
	(2) that computes values of the Bessel functions K of float
	argument and float order.  Modifications include the addition
	of non-scaled functions, parameterization of machine
	dependencies, and the use of more accurate approximations
	for SINH and SIN.

 References: "On Temme's Algorithm for the Modified Bessel
	      Functions of the Third Kind," Campbell, J. B.,
	      TOMS 6(4), Dec. 1980, pp. 581-586.

	     "A FORTRAN IV Subroutine for the Modified Bessel
	      Functions of the Third Kind of Real Order and Real
	      Argument," Campbell, J. B., Report NRC/ERB-925,
	      National Research Council, Canada.

  Latest modification: May 30, 1989

  Modified by: W. J. Cody and L. Stoltz
	       Applied Mathematics Division
	       Argonne National Laboratory
	       Argonne, IL  60439

 -------------------------------------------------------------------
*/

/*
 ---------------------------------------------------------------------
  Machine dependent parameters
 ---------------------------------------------------------------------
  Explanation of machine-dependent constants

   beta	  = Radix for the floating-point system
   minexp = Smallest representable power of beta
   maxexp = Smallest power of beta that overflows
   EPS	  = The smallest positive floating-point number such that 1.0+EPS > 1.0
   SQXMIN = Square root of beta**minexp
   XINF	  = Largest positive machine number; approximately  beta**maxexp
	    == DBL_MAX (defined in  #include <float.h>)
   XMIN	  = Smallest positive machine number; approximately beta**minexp

   XMAX	  = Upper limit on the magnitude of X when IZE=1;  Solution
	    to equation:
	       W(X) * (1-1/8X+9/128X**2) = beta**minexp
	    where  W(X) = EXP(-X)*SQRT(PI/2X)


     Approximate values for some important machines are:

			  beta	     minexp	 maxexp	     EPS

  CRAY-1	(S.P.)	    2	     -8193	  8191	  7.11E-15
  Cyber 180/185
    under NOS	(S.P.)	    2	      -975	  1070	  3.55E-15
  IEEE (IBM/XT,
    SUN, etc.)	(S.P.)	    2	      -126	   128	  1.19E-7
  IEEE (IBM/XT,
    SUN, etc.)	(D.P.)	    2	     -1022	  1024	  2.22D-16
  IBM 3033	(D.P.)	   16	       -65	    63	  2.22D-16
  VAX		(S.P.)	    2	      -128	   127	  5.96E-8
  VAX D-Format	(D.P.)	    2	      -128	   127	  1.39D-17
  VAX G-Format	(D.P.)	    2	     -1024	  1023	  1.11D-16


			 SQXMIN	      XINF	  XMIN	    XMAX

 CRAY-1	       (S.P.)  6.77E-1234  5.45E+2465  4.59E-2467 5674.858
 Cyber 180/855
   under NOS   (S.P.)  1.77E-147   1.26E+322   3.14E-294   672.788
 IEEE (IBM/XT,
   SUN, etc.)  (S.P.)  1.08E-19	   3.40E+38    1.18E-38	    85.337
 IEEE (IBM/XT,
   SUN, etc.)  (D.P.)  1.49D-154   1.79D+308   2.23D-308   705.342
 IBM 3033      (D.P.)  7.35D-40	   7.23D+75    5.40D-79	   177.852
 VAX	       (S.P.)  5.42E-20	   1.70E+38    2.94E-39	    86.715
 VAX D-Format  (D.P.)  5.42D-20	   1.70D+38    2.94D-39	    86.715
 VAX G-Format  (D.P.)  7.46D-155   8.98D+307   5.57D-309   706.728

 *******************************************************************
 */
    /*static double eps = 2.22e-16;*/
    /*static double xinf = 1.79e308;*/
    /*static double xmin = 2.23e-308;*/
    static double sqxmin = 1.49e-154;

    /*---------------------------------------------------------------------
     * Mathematical constants
     *	A = LOG(2) - Euler's constant
     *	D = SQRT(2/PI)
     ---------------------------------------------------------------------*/
    static double a = .11593151565841244881;

    /*---------------------------------------------------------------------
      P, Q - Approximation for LOG(GAMMA(1+ALPHA))/ALPHA + Euler's constant
      Coefficients converted from hex to decimal and modified
      by W. J. Cody, 2/26/82 */
    static double p[8] = { .805629875690432845,20.4045500205365151,
	    157.705605106676174,536.671116469207504,900.382759291288778,
	    730.923886650660393,229.299301509425145,.822467033424113231 };
    static double q[7] = { 29.4601986247850434,277.577868510221208,
	    1206.70325591027438,2762.91444159791519,3443.74050506564618,
	    2210.63190113378647,572.267338359892221 };
    /* R, S - Approximation for (1-ALPHA*PI/SIN(ALPHA*PI))/(2.D0*ALPHA) */
    static double r[5] = { -.48672575865218401848,13.079485869097804016,
	    -101.96490580880537526,347.65409106507813131,
	    3.495898124521934782e-4 };
    static double s[4] = { -25.579105509976461286,212.57260432226544008,
	    -610.69018684944109624,422.69668805777760407 };
    /* T    - Approximation for SINH(Y)/Y */
    static double t[6] = { 1.6125990452916363814e-10,
	    2.5051878502858255354e-8,2.7557319615147964774e-6,
	    1.9841269840928373686e-4,.0083333333333334751799,
	    .16666666666666666446 };
    /*---------------------------------------------------------------------*/
    static double estm[6] = { 52.0583,5.7607,2.7782,14.4303,185.3004, 9.3715 };
    static double estf[7] = { 41.8341,7.1075,6.4306,42.511,1.35633,84.5096,20.};

    /* Local variables */
    long iend, i, j, k, m, ii, mplus1;
    double x2by4, twox, c, blpha, ratio, wminf;
    double d1, d2, d3, f0, f1, f2, p0, q0, t1, t2, twonu;
    double dm, ex, bk1, bk2, nu;

    ii = 0;			/* -Wall */

    ex = *x;
    nu = *alpha;
    *ncalc = imin2(*nb,0) - 2;
    if (*nb > 0 && (0. <= nu && nu < 1.) && (1 <= *ize && *ize <= 2)) {
	if(ex <= 0 || (*ize == 1 && ex > xmax)) {
	    ML_ERROR(ME_RANGE);
	    *ncalc = *nb;
	    for(i=0; i < *nb; i++)
		bk[i] = ML_POSINF;
	    return;
	}
	k = 0;
	if (nu < sqxmin) {
	    nu = 0.;
	} else if (nu > .5) {
	    k = 1;
	    nu -= 1.;
	}
	twonu = nu + nu;
	iend = *nb + k - 1;
	c = nu * nu;
	d3 = -c;
	if (ex <= 1.) {
	    /* ------------------------------------------------------------
	       Calculation of P0 = GAMMA(1+ALPHA) * (2/X)**ALPHA
			      Q0 = GAMMA(1-ALPHA) * (X/2)**ALPHA
	       ------------------------------------------------------------ */
	    d1 = 0.; d2 = p[0];
	    t1 = 1.; t2 = q[0];
	    for (i = 2; i <= 7; i += 2) {
		d1 = c * d1 + p[i - 1];
		d2 = c * d2 + p[i];
		t1 = c * t1 + q[i - 1];
		t2 = c * t2 + q[i];
	    }
	    d1 = nu * d1;
	    t1 = nu * t1;
	    f1 = log(ex);
	    f0 = a + nu * (p[7] - nu * (d1 + d2) / (t1 + t2)) - f1;
	    q0 = exp(-nu * (a - nu * (p[7] + nu * (d1-d2) / (t1-t2)) - f1));
	    f1 = nu * f0;
	    p0 = exp(f1);
	    /* -----------------------------------------------------------
	       Calculation of F0 =
	       ----------------------------------------------------------- */
	    d1 = r[4];
	    t1 = 1.;
	    for (i = 0; i < 4; ++i) {
		d1 = c * d1 + r[i];
		t1 = c * t1 + s[i];
	    }
	    /* d2 := sinh(f1)/ nu = sinh(f1)/(f1/f0)
	     *	   = f0 * sinh(f1)/f1 */
	    if (fabs(f1) <= .5) {
		f1 *= f1;
		d2 = 0.;
		for (i = 0; i < 6; ++i) {
		    d2 = f1 * d2 + t[i];
		}
		d2 = f0 + f0 * f1 * d2;
	    } else {
		d2 = sinh(f1) / nu;
	    }
	    f0 = d2 - nu * d1 / (t1 * p0);
	    if (ex <= 1e-10) {
		/* ---------------------------------------------------------
		   X <= 1.0E-10
		   Calculation of K(ALPHA,X) and X*K(ALPHA+1,X)/K(ALPHA,X)
		   --------------------------------------------------------- */
		bk[0] = f0 + ex * f0;
		if (*ize == 1) {
		    bk[0] -= ex * bk[0];
		}
		ratio = p0 / f0;
		c = ex * DBL_MAX;
		if (k != 0) {
		    /* ---------------------------------------------------
		       Calculation of K(ALPHA,X)
		       and  X*K(ALPHA+1,X)/K(ALPHA,X),	ALPHA >= 1/2
		       --------------------------------------------------- */
		    *ncalc = -1;
		    if (bk[0] >= c / ratio) {
			return;
		    }
		    bk[0] = ratio * bk[0] / ex;
		    twonu += 2.;
		    ratio = twonu;
		}
		*ncalc = 1;
		if (*nb == 1)
		    return;

		/* -----------------------------------------------------
		   Calculate  K(ALPHA+L,X)/K(ALPHA+L-1,X),
		   L = 1, 2, ... , NB-1
		   ----------------------------------------------------- */
		*ncalc = -1;
		for (i = 1; i < *nb; ++i) {
		    if (ratio >= c)
			return;

		    bk[i] = ratio / ex;
		    twonu += 2.;
		    ratio = twonu;
		}
		*ncalc = 1;
		goto L420;
	    } else {
		/* ------------------------------------------------------
		   10^-10 < X <= 1.0
		   ------------------------------------------------------ */
		c = 1.;
		x2by4 = ex * ex / 4.;
		p0 = .5 * p0;
		q0 = .5 * q0;
		d1 = -1.;
		d2 = 0.;
		bk1 = 0.;
		bk2 = 0.;
		f1 = f0;
		f2 = p0;
		do {
		    d1 += 2.;
		    d2 += 1.;
		    d3 = d1 + d3;
		    c = x2by4 * c / d2;
		    f0 = (d2 * f0 + p0 + q0) / d3;
		    p0 /= d2 - nu;
		    q0 /= d2 + nu;
		    t1 = c * f0;
		    t2 = c * (p0 - d2 * f0);
		    bk1 += t1;
		    bk2 += t2;
		} while (fabs(t1 / (f1 + bk1)) > DBL_EPSILON ||
			 fabs(t2 / (f2 + bk2)) > DBL_EPSILON);
		bk1 = f1 + bk1;
		bk2 = 2. * (f2 + bk2) / ex;
		if (*ize == 2) {
		    d1 = exp(ex);
		    bk1 *= d1;
		    bk2 *= d1;
		}
		wminf = estf[0] * ex + estf[1];
	    }
	} else if (DBL_EPSILON * ex > 1.) {
	    /* -------------------------------------------------
	       X > 1./EPS
	       ------------------------------------------------- */
	    *ncalc = *nb;
	    bk1 = 1. / (M_SQRT_2dPI * sqrt(ex));
	    for (i = 0; i < *nb; ++i)
		bk[i] = bk1;
	    return;

	} else {
	    /* -------------------------------------------------------
	       X > 1.0
	       ------------------------------------------------------- */
	    twox = ex + ex;
	    blpha = 0.;
	    ratio = 0.;
	    if (ex <= 4.) {
		/* ----------------------------------------------------------
		   Calculation of K(ALPHA+1,X)/K(ALPHA,X),  1.0 <= X <= 4.0
		   ----------------------------------------------------------*/
		d2 = ftrunc(estm[0] / ex + estm[1]);
		m = (long) d2;
		d1 = d2 + d2;
		d2 -= .5;
		d2 *= d2;
		for (i = 2; i <= m; ++i) {
		    d1 -= 2.;
		    d2 -= d1;
		    ratio = (d3 + d2) / (twox + d1 - ratio);
		}
		/* -----------------------------------------------------------
		   Calculation of I(|ALPHA|,X) and I(|ALPHA|+1,X) by backward
		   recurrence and K(ALPHA,X) from the wronskian
		   -----------------------------------------------------------*/
		d2 = ftrunc(estm[2] * ex + estm[3]);
		m = (long) d2;
		c = fabs(nu);
		d3 = c + c;
		d1 = d3 - 1.;
		f1 = DBL_MIN;
		f0 = (2. * (c + d2) / ex + .5 * ex / (c + d2 + 1.)) * DBL_MIN;
		for (i = 3; i <= m; ++i) {
		    d2 -= 1.;
		    f2 = (d3 + d2 + d2) * f0;
		    blpha = (1. + d1 / d2) * (f2 + blpha);
		    f2 = f2 / ex + f1;
		    f1 = f0;
		    f0 = f2;
		}
		f1 = (d3 + 2.) * f0 / ex + f1;
		d1 = 0.;
		t1 = 1.;
		for (i = 1; i <= 7; ++i) {
		    d1 = c * d1 + p[i - 1];
		    t1 = c * t1 + q[i - 1];
		}
		p0 = exp(c * (a + c * (p[7] - c * d1 / t1) - log(ex))) / ex;
		f2 = (c + .5 - ratio) * f1 / ex;
		bk1 = p0 + (d3 * f0 - f2 + f0 + blpha) / (f2 + f1 + f0) * p0;
		if (*ize == 1) {
		    bk1 *= exp(-ex);
		}
		wminf = estf[2] * ex + estf[3];
	    } else {
		/* ---------------------------------------------------------
		   Calculation of K(ALPHA,X) and K(ALPHA+1,X)/K(ALPHA,X), by
		   backward recurrence, for  X > 4.0
		   ----------------------------------------------------------*/
		dm = ftrunc(estm[4] / ex + estm[5]);
		m = (long) dm;
		d2 = dm - .5;
		d2 *= d2;
		d1 = dm + dm;
		for (i = 2; i <= m; ++i) {
		    dm -= 1.;
		    d1 -= 2.;
		    d2 -= d1;
		    ratio = (d3 + d2) / (twox + d1 - ratio);
		    blpha = (ratio + ratio * blpha) / dm;
		}
		bk1 = 1. / ((M_SQRT_2dPI + M_SQRT_2dPI * blpha) * sqrt(ex));
		if (*ize == 1)
		    bk1 *= exp(-ex);
		wminf = estf[4] * (ex - fabs(ex - estf[6])) + estf[5];
	    }
	    /* ---------------------------------------------------------
	       Calculation of K(ALPHA+1,X)
	       from K(ALPHA,X) and  K(ALPHA+1,X)/K(ALPHA,X)
	       --------------------------------------------------------- */
	    bk2 = bk1 + bk1 * (nu + .5 - ratio) / ex;
	}
	/*--------------------------------------------------------------------
	  Calculation of 'NCALC', K(ALPHA+I,X),	I  =  0, 1, ... , NCALC-1,
	  &	  K(ALPHA+I,X)/K(ALPHA+I-1,X),	I = NCALC, NCALC+1, ... , NB-1
	  -------------------------------------------------------------------*/
	*ncalc = *nb;
	bk[0] = bk1;
	if (iend == 0)
	    return;

	j = 1 - k;
	if (j >= 0)
	    bk[j] = bk2;

	if (iend == 1)
	    return;

	m = imin2((long) (wminf - nu),iend);
	for (i = 2; i <= m; ++i) {
	    t1 = bk1;
	    bk1 = bk2;
	    twonu += 2.;
	    if (ex < 1.) {
		if (bk1 >= DBL_MAX / twonu * ex)
		    break;
	    } else {
		if (bk1 / ex >= DBL_MAX / twonu)
		    break;
	    }
	    bk2 = twonu / ex * bk1 + t1;
	    ii = i;
	    ++j;
	    if (j >= 0) {
		bk[j] = bk2;
	    }
	}

	m = ii;
	if (m == iend) {
	    return;
	}
	ratio = bk2 / bk1;
	mplus1 = m + 1;
	*ncalc = -1;
	for (i = mplus1; i <= iend; ++i) {
	    twonu += 2.;
	    ratio = twonu / ex + 1./ratio;
	    ++j;
	    if (j >= 1) {
		bk[j] = ratio;
	    } else {
		if (bk2 >= DBL_MAX / ratio)
		    return;

		bk2 *= ratio;
	    }
	}
	*ncalc = imax2(1, mplus1 - k);
	if (*ncalc == 1)
	    bk[0] = bk2;
	if (*nb == 1)
	    return;

L420:
	for (i = *ncalc; i < *nb; ++i) { /* i == *ncalc */
#ifndef IEEE_754
	    if (bk[i-1] >= DBL_MAX / bk[i])
		return;
#endif
	    bk[i] *= bk[i-1];
	    (*ncalc)++;
	}
    }
}

double bessel_k(double x, double alpha, double expo)
{
    long nb, ncalc, ize;
    double *bk;
#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (ISNAN(x) || ISNAN(alpha)) return x + alpha;
#endif
    ize = (long)expo;
    nb = 1+ (long)floor(fabs(alpha));/* nb-1 <= alpha < nb */
    alpha -= (nb-1);
    bk = (double *) calloc(nb, sizeof(double));
    K_bessel(&x, &alpha, &nb, &ize, bk, &ncalc);
    if(ncalc != nb) {/* error input */
      if(ncalc < 0)
	MATHLIB_WARNING4("bessel_k(%g): ncalc (=%d) != nb (=%d); alpha=%g. Arg. out of range?\n",
			 x, ncalc, nb, alpha);
      else
	MATHLIB_WARNING2("bessel_k(%g,nu=%g): precision lost in result\n",
			 x, alpha+nb-1);
    }
    x = bk[nb-1];
    free(bk);
    return x;
}


/* FIXME: we need something that catches partials and EAGAIN.  */
#define fullread read

#define RANDOM_DEVICE "dev/urandom"

/*
 * Conservative random number generator.  The result is (supposedly) uniform
 * and between 0 and 1.  (0 possible, 1 not.)  The result should have about
 * 64 bits randomness.
 */
double
random_01 (void)
{
	static int device_fd = -2;

	if (device_fd == -2)
		device_fd = open (RANDOM_DEVICE, O_RDONLY);

	if (device_fd >= 0) {
		int r1, r2;

		if (fullread (device_fd, &r1, sizeof (r1)) == sizeof (r1) &&
		    fullread (device_fd, &r2, sizeof (r2)) == sizeof (r2)) {
			r1 &= 2147483647;
			r2 &= 2147483647;
			return (r1 + (r2 / 2147483648.0)) / 2147483648.0;
		}

		/* It failed when it shouldn't.  Disable.  */
		g_warning ("Reading from %s failed; reverting to pseudo-random.",
			   RANDOM_DEVICE);
		close (device_fd);
		device_fd = -1;
	}

#ifdef HAVE_RANDOM
	{
		int r1, r2;

		r1 = random () & 2147483647;
		r2 = random () & 2147483647;

		return (r1 + (r2 / 2147483648.0)) / 2147483648.0;
	}
#elif defined (HAVE_DRAND48)
	return drand48 ();
#else
	{
		/*
		 * We try to work around lack of randomness in rand's
		 * lower bits.
		 */
		const int prime = 65537;
		int r1, r2, r3, r4;

		g_assert (RAND_MAX > ((1 << 12) - 1));

		r1 = (rand () ^ (rand () << 12)) % prime;
		r2 = (rand () ^ (rand () << 12)) % prime;
		r3 = (rand () ^ (rand () << 12)) % prime;
		r4 = (rand () ^ (rand () << 12)) % prime;

		return (r1 + (r2 + (r3 + r4 / (double)prime) / prime) / prime) / prime;
	}
#endif
}

/*
 * Generate a N(0,1) distributed number.
 */
double
random_normal (void)
{
	return qnorm (random_01 (), 0, 1);
}

/*
 * Generate a poisson distributed number.
 */
double
random_poisson (double lambda)
{
        double x = exp (-1 * lambda);
	double r = random_01 ();
	double t = x;
	double i = 0;

	while (r > t) {
	      x *= lambda / (i + 1);
	      i += 1;
	      t += x;
	}

	return i;
}

/*
 * Generate a binomial distributed number.
 */
double
random_binomial (double p, int trials)
{
        double x = pow (1 - p, trials);
	double r = random_01 ();
	double t = x;
	double i = 0;

	while (r > t) {
	      x *= (((trials - i) * p) / ((1 + i) * (1 - p)));
	      i += 1;
	      t += x;
	}

	return i;
}

/*
 * Generate a negative binomial distributed number.
 */
double
random_negbinom (double p, int f)
{
        double x = pow (p, f);
	double r = random_01 ();
	double t = x;
	double i = 0;

	while (r > t) {
	      x *= (((f + i) * (1 - p)) / (1 + i));
	      i += 1;
	      t += x;
	}

	return i;
}

/*
 * Generate an exponential distributed number.
 */
double
random_exponential (double b)
{
        return -1 * b * log (random_01 ());
}

/*
 * Generate a bernoulli distributed number.
 */
double
random_bernoulli (double p)
{
        double r = random_01 ();

	return (r <= p) ? 1.0 : 0.0;
}

/*
 * Generate 10^n being careful not to overflow
 */
gnum_float
gpow10 (int n)
{
	gnum_float res = 1.0;
	gnum_float p;
	const int maxn = 300;

	static const gnum_float fast[] = {
		1e-20, 1e-19, 1e-18, 1e-17, 1e-16, 1e-15, 1e-14, 1e-13, 1e-12, 1e-11,
		1e-10,  1e-9,  1e-8,  1e-7,  1e-6,  1e-5,  1e-4,  1e-3,  1e-2,  1e-1,
		1,
		  1e1,   1e2,   1e3,   1e4,   1e5,   1e6,   1e7,   1e8,   1e9,  1e10,
		 1e11,  1e12,  1e13,  1e14,  1e15,  1e16,  1e17,  1e18,  1e19,  1e20
	};

	if (n >= -20 && n <= 20)
		return (fast + 20)[n];

	if (n >= 0) {
		p = 10.0;
		n = (n > maxn) ? maxn : n;
	} else {
		p = 0.1;
		/* Note carefully that we avoid overflow.  */
		n = (n < -maxn) ? maxn : -n;
	}
	while (n > 0) {
		if (n & 1) res *= p;
		p *= p;
		n >>= 1;
	}
	return res;
}


/*
 * Euclid's Algorithm.	Assumes non-negative numbers.
 */
int
gcd (int a, int b)
{
	while (b != 0) {
		int r;

		r = a - (a / b) * b;	/* r = remainder from
					 * dividing a by b	*/
		a = b;
		b = r;
	}
	return a;
}


gnum_float
combin (int n, int k)
{
	if (n >= 15) {
		gnum_float res;

		res = exp (lgamma (n + 1) - lgamma (k + 1) - lgamma (n - k + 1));
		return floor (res + 0.5);  /* Round, just in case.  */
	} else {
		gnum_float res;

		res = fact (n) / fact (k) / fact (n - k);
		return res;
	}
}

gnum_float
fact (int n)
{
	if (n == 0)
		return 1;
	return (n * fact (n - 1));
}

/*
 ---------------------------------------------------------------------
  Matrix functions
 ---------------------------------------------------------------------
 */

/* This function implements the LU-Factorization method for solving
 * matrix determinants.  At first the given matrix, A, is converted to
 * a product of a lower triangular matrix, L, and an upper triangluar
 * matrix, U, where A = L*U.  The lower triangular matrix, L, contains
 * ones along the main diagonal.
 *
 * The determinant of the original matrix, A, is det(A)=det(L)*det(U).
 * The determinant of any triangular matrix is the product of the
 * elements along its main diagonal.  Since det(L) is always one, we
 * can simply write as follows: det(A) = det(U).  As you can see this
 * algorithm is quite efficient.
 *
 * (C) Copyright 1999 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 **/
gnum_float
mdeterm (gnum_float *A, int dim)
{
        int i, j, n;
	gnum_float product, sum;
	gnum_float *L, *U;

#define ARRAY(A,C,R) (*((A) + (R) + (C) * dim))

	L = g_new (gnum_float, dim * dim);
	U = g_new (gnum_float, dim * dim);

	/* Initialize the matrices with value zero, except fill the L's
	 * main diagonal with ones */
	for (i = 0; i < dim; i++)
	        for (n = 0; n < dim; n++) {
		        if (i == n)
		                ARRAY (L, i, n) = 1;
			else
		                ARRAY (L, i, n) = 0;
			ARRAY (U, i, n) = 0;
		}

	/* Determine L and U so that A=L*U */
	for (n = 0; n < dim; n++) {
	        for (i = n; i < dim; i++) {
		        sum = 0;
			for (j = 0; j < n; j++)
			        sum += ARRAY (U, j, i) * ARRAY (L, n, j);
			ARRAY (U, n, i) = ARRAY (A, n, i) - sum;
		}

		for (i = n + 1; i < dim; i++) {
		        sum = 0;
			for (j = 0; j < i - 1; j++)
			        sum += ARRAY (U, j, n) * ARRAY (L, i, j);
			ARRAY (L, i, n) = (ARRAY (A, i, n) - sum) /
			  ARRAY (U, n, n);
		}
	}

	/* Calculate det(U) */
	product = ARRAY (U, 0, 0);
	for (i = 1; i < dim; i++)
	        product *= ARRAY (U, i, i);

#undef ARRAY

	g_free (L);
	g_free (U);

	return product;
}

/* Inverts a given matrix (A).  If the inversion was succesfull, minverse
 * returns 0.  If there is no A^-1, the function returns 1.
 *
 * (C) Copyright 1999 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 */
int
minverse (gnum_float *A, int dim, gnum_float *res)
{
        int     i, n, r, cols, rows;
	gnum_float *array, pivot;

#define ARRAY(C,R) (*(array + (R) + (C) * rows))

	/* Initialize the matrix */
	cols = dim * 2;
	rows = dim;
	array = g_new (gnum_float, cols * rows);
	for (i = 0; i < cols; i++)
	        for (n = 0; n < rows; n++)
		        if (i < dim)
			        ARRAY (i, n) = A[n + i * rows];
			else if (i - dim == n)
			        ARRAY (i, n) = 1;
			else
			        ARRAY (i, n) = 0;

	/* Pivot top-down */
	for (r = 0; r < rows-1; r++) {
	        /* Select pivot row */
	        for (i = r; ARRAY (r, i) == 0; i++)
		        if (i == rows) {
			        g_free (array);
			        return 1;
			}
		if (i != r)
		        for (n = 0; n < cols; n++) {
			        gnum_float tmp = ARRAY (n, r);
				ARRAY (n, r) = ARRAY (n, i);
				ARRAY (n, i) = tmp;
			}

		for (i = r + 1; i < rows; i++) {
		        /* Calculate the pivot */
		        pivot = -ARRAY (r, i) / ARRAY (r, r);

			/* Add the pivot row */
			for (n = r; n < cols; n++)
			        ARRAY (n, i) += pivot * ARRAY (n, r);
		}
	}

	/* Pivot bottom-up */
	for (r = rows - 1; r>0; r--) {
	        for (i = r - 1; i >= 0; i--) {
		        /* Calculate the pivot */
		        pivot = -ARRAY (r, i) / ARRAY (r, r);

			/* Add the pivot row */
			for (n = 0; n < cols; n++)
			        ARRAY (n, i) += pivot * ARRAY (n, r);
		}
	}

	for (r = 0; r < rows; r++) {
	        pivot = ARRAY (r, r);
		for (i = 0; i < cols; i++)
		        ARRAY (i, r) /= pivot;
	}

	/* Fetch the results */
	for (i = 0; i < dim; i++)
	        for (n = 0; n < dim; n++)
		        res[n + i * rows] = ARRAY (i + dim, n);
#undef ARRAY
	g_free (array);

        return 0;
}


/* Calculates the product of two matrixes.
 */
void
mmult (gnum_float *A, gnum_float *B, int cols_a, int rows_a, int cols_b,
       gnum_float *product)
{
	gnum_float tmp;
        int     c, r, i;

	for (c = 0; c < cols_b; ++c) {
		for (r = 0; r < rows_a; ++r) {
		        tmp = 0;
			for (i = 0; i < cols_a; ++i)
				tmp += A[r + i * rows_a] * B[i + c * cols_a];
		        product[r + c * rows_a] = tmp;
		}
	}
}

/* Returns the transpose of a matrix.
 */
static void
mtranspose (gnum_float *A, int cols, int rows, gnum_float *M)
{
        int i, j;

	for (i = 0; i < cols; i++)
	        for (j = 0; j < rows; j++)
		        M[i + j * cols] = A[j + i * rows];
}

/* Solve a set of linear equations (do not try to swap rows).
 */
static int
mpivot (gnum_float *pivot_table, int cols, int rows)
{
        gnum_float pivot;
	int     i, j, k;

	/* Pivot top-down */
	for (j = 0; j < rows; j++) {
	        if (pivot_table[j + j * rows] == 0)
		        return 1;
		for (i = j + 1; i < rows; i++) {
		        pivot = pivot_table[i + j * rows] /
			        pivot_table[j + j * rows];
			for (k = j; k < cols; k++)
			        pivot_table[i + k * rows] -=
				        pivot * pivot_table[j + k * rows];
		}
	}

	/* Pivot bottom-up */
	for (j = rows - 1; j >= 0; j--) {
	        if (pivot_table[j + j * rows] == 0)
		        return 1;
		for (i = j - 1; i >= 0; i--) {
		        pivot = pivot_table[i + j * rows] /
			        pivot_table[j + j * rows];
			for (k = j; k < cols; k++)
			        pivot_table[i + k * rows] -=
				        pivot * pivot_table[j + k * rows];
		}
	}

	for (i = 0; i < rows; i++) {
	        pivot = pivot_table[i + i * rows];
		pivot_table[i + i * rows] /= pivot;
		pivot_table[i + (cols-1) * rows] /= pivot;
	}

	return 0;
}

/*
 ---------------------------------------------------------------------
  Primal Affine scaling

  (C) Copyright 2000 by Jukka-Pekka Iivonen (iivonen@iki.fi)
 ---------------------------------------------------------------------
 */

/* Creates a diagonal matrix from the squares of a given vector.
 * The result array (M) should have n * n elements.
 */
static void
vect_sqr_mdiag (gnum_float *v, int n, gnum_float *M)
{
        int i;

	for (i = 0; i < n * n; i++)
	        M[i] = 0;

	for (i = 0; i < n; i++)
	        M[i + i * n] = v[i] * v[i];
}

#include <stdio.h>

static void
display (gnum_float *M, int cols, int rows, const char *s)
{
        int i, j;

	printf ("\n%s:\n", s);
	for (i = 0; i < rows; i++) {
	        for (j = 0; j < cols; j++)
		         printf ("%8.4" GNUM_FORMAT_f " ", M[i + j * rows]);
		printf ("\n");
	}
	printf ("\n");
}

/* Solves the dual vector v from
 *   (A * D^2 * A_t) * v = A * D^2 * c
 *
 * After the call, D^2 is in `sqr_D' and v is in `v'.
 *
 * `wspace' should have
 *     (n_constraints * n_variables)        | for A * D^2
 *   + (n_constraints * n_constraints)      | for (A * D^2) * A_t
 *   + (n_constraints)                      | for (A * D^2) * c
 *   + (n_constraints + 1) * n_constraints  | for pivot table
 * elementes.
 */
static int
solve_dual_vector (gnum_float *A, gnum_float *c, gnum_float *x, gnum_float *A_t,
		   int n_constraints, int n_variables,
		   gnum_float *sqr_D, gnum_float *v,
		   gnum_float *wspace)
{
        gnum_float *Asqr_D;
	gnum_float *Asqr_DA_t;
	gnum_float *Asqr_Dc;
	gnum_float *pivot_table;
	int     s_ind = 0;
	int     i, j;

	Asqr_D = &wspace[s_ind];
	s_ind += n_variables * n_constraints;

	Asqr_DA_t = &wspace[s_ind];
	s_ind += n_constraints * n_constraints;

	Asqr_Dc = &wspace[s_ind];
	s_ind += n_constraints;

	pivot_table = &wspace[s_ind];
	s_ind += (n_constraints + 1) * n_constraints;

	vect_sqr_mdiag (x, n_variables, sqr_D);
	mmult (A, sqr_D, n_variables, n_constraints, n_variables, Asqr_D);

	mmult (Asqr_D, A_t, n_variables, n_constraints, n_constraints,
	       Asqr_DA_t);
	mmult (Asqr_D, c, n_variables, n_constraints, 1, Asqr_Dc);

	/* Create the pivot table for mpivot */
	for (i = 0; i < n_constraints; i++) {
	        for (j = 0; j < n_constraints; j++)
		        pivot_table[j + i * n_constraints] =
			        Asqr_DA_t[j + i * n_constraints];
		pivot_table[i + n_constraints * n_constraints] = Asqr_Dc[i];
	}

	i = mpivot (pivot_table, n_constraints + 1, n_constraints);
	if (i)
	        return i;

	for (i = 0; i < n_constraints; i++)
	        v[i] = pivot_table[i + n_constraints * n_constraints];

	return 0;
}

static void
create_step_vector (gnum_float *c, gnum_float *sqr_D, gnum_float *A_t, gnum_float *v,
		    int n_constraints, int n_variables, gboolean max_flag,
		    gnum_float *dx, gnum_float *wspace)
{
        gnum_float *A_tv;
	gnum_float *diff;
	int     s_ind = 0;
	int     i;

	A_tv = &wspace[s_ind];
	s_ind += n_variables;

	diff = &wspace[s_ind];
	s_ind += n_variables;

	mmult (A_t, v, n_constraints, n_variables, 1, A_tv);

	if (max_flag)
	        for (i = 0; i < n_variables; i++)
		        diff[i] = c[i] - A_tv[i];
	else
	        for (i = 0; i < n_variables; i++)
		        diff[i] = -(c[i] - A_tv[i]);

	mmult (sqr_D, diff, n_variables, n_variables, 1, dx);
}

static gnum_float
step_length (gnum_float *x, gnum_float *dx, int n_variables, gboolean *found)
{
        gnum_float min = 0, test;
	int     i, min_ind = -1;

	for (i = 0; i < n_variables; i++)
	        if (dx[i] < 0) {
		         test = -x[i] / dx[i];
			 if (min_ind < 0 || test < min) {
			         min_ind = i;
				 min = test;
			 }
		}

	if (min_ind == -1)
	        *found = FALSE;
	else
	        *found = TRUE;

	return min;
}

static gnum_float
affine_rdg (gnum_float *b, gnum_float *c, gnum_float *x, gnum_float *v,
	    int n_constraints, int n_variables, gnum_float *bv, gnum_float *cx)
{
	mmult (c, x, n_variables, 1, 1, cx);
	mmult (b, v, n_constraints, 1, 1, bv);

	return fabs (*cx - *bv) / (1.0 + fabs (*cx));
}


static gboolean
run_affine_scale (gnum_float *A, gnum_float *b, gnum_float *c, gnum_float *x,
		  gnum_float *A_t, int n_constraints, int n_variables,
		  gboolean max_flag, gnum_float e, int max_iter, gnum_float *wspace,
		  affscale_callback_fun_t fun, void *data)
{
        gboolean flag;
        gnum_float  *sqr_D;
	gnum_float  *v;
	gnum_float  *dx;
	gnum_float  step_len, rdg=1;
	gnum_float  bv, cx;
	int      s_ind = 0;
	int      i;
	int      iter;

	sqr_D = &wspace[s_ind];
	s_ind += n_variables * n_variables;

	v = &wspace[s_ind];
	s_ind += n_constraints;

	dx = &wspace[s_ind];
	s_ind += n_variables;

	for (iter = 0; rdg > e && iter < max_iter; iter++) {
	        solve_dual_vector (A, c, x, A_t,
				   n_constraints, n_variables, sqr_D, v,
				   &wspace[s_ind]);
		create_step_vector (c, sqr_D, A_t, v,
				    n_constraints, n_variables,
				    max_flag, dx, &wspace[s_ind]);

		step_len = step_length (x, dx, n_variables, &flag);

		if ( !flag)
		        break;

		for (i = 0; i < n_variables; i++)
		        x[i] += 0.81 * step_len * dx[i];

		rdg = affine_rdg (b, c, x, v, n_constraints, n_variables,
				  &bv, &cx);

		if (fun)
		        fun (iter, x, bv, cx, n_variables, data);
	}

	return flag && iter < max_iter;
}

/* Optimizes a given problem using affine scaling (primal) algorithm.
 */
gboolean
affine_scale (gnum_float *A, gnum_float *b, gnum_float *c, gnum_float *x,
	      int n_constraints, int n_variables, gboolean max_flag,
	      gnum_float e, int max_iter,
	      affscale_callback_fun_t fun, void *data)
{
        gboolean found;
        gnum_float  *wspace;
	gnum_float  *A_t;
	int      s_ind = 0;

        wspace = g_new (gnum_float,
			n_variables * n_constraints           /* A_t */
			+ n_variables * n_variables           /* sqr_D */
			+ n_constraints                       /* v */
			+ n_variables                         /* dx */

			+ n_variables * n_constraints         /* Asqr_D */
			+ n_constraints * n_constraints       /* Asqr_DA_t */
			+ n_constraints                       /* Asqr_Dc */
			+ (n_constraints + 1) * n_constraints /* pivot table */
			);

	A_t = &wspace[s_ind];
	s_ind += n_variables * n_constraints;

	mtranspose (A, n_variables, n_constraints, A_t);

        found = run_affine_scale (A, b, c, x, A_t, n_constraints, n_variables,
				  max_flag, e, max_iter, &wspace[s_ind],
				  fun, data);
	g_free (wspace);

	return found;
}

/* Gives a valid initial solution for a problem.
 */
gboolean
affine_init (gnum_float *A, gnum_float *b, gnum_float *c, int n_constraints,
	     int n_variables, gnum_float *x)
{
         gboolean found;
         gnum_float  *wspace;
	 gnum_float  *new_c;
	 gnum_float  *new_A;
	 gnum_float  *tmp;
	 gnum_float  *new_x;
	 int      i, j;
	 int      s_ind = 0;

	 wspace = g_new (gnum_float,
			 n_variables + 1                      /* new_c */
			 + n_constraints                      /* tmp */
			 + (n_variables + 1) * n_constraints  /* new_A */
			 + n_variables + 1                    /* new_x */
			 );

	 new_c = &wspace[s_ind];
	 s_ind += n_variables + 1;

	 tmp = &wspace[s_ind];
	 s_ind += n_constraints;

	 new_A = &wspace[s_ind];
	 s_ind += (n_variables + 1) * n_constraints;

	 new_x = &wspace[s_ind];
	 s_ind += n_variables + 1;

	 for (i = 0; i < n_variables; i++)
	         new_c[i] = c[i];
	 new_c[i] = -pow (2, 58);

	 for (i = 0; i < n_constraints; i++)
	         tmp[i] = 0;
	 for (i = 0; i < n_constraints; i++)
	         for (j = 0; j < n_variables; j++)
		          tmp[i] += A[i + j * n_constraints];

	 for (i = 0; i < n_variables; i++)
	         tmp[i] = b[i] - tmp[i];

	 for (i = 0; i < n_variables; i++)
	         for (j = 0; j < n_constraints; j++)
		         new_A[j + i * n_constraints] = A[j + i * n_constraints];
	 for (i = 0; i < n_constraints; i++)
	         new_A[i + n_variables * n_constraints] = tmp[i];

	 for (i = 0; i <= n_variables; i++)
	         new_x[i] = 1;

	 found = affine_scale (new_A, b, new_c, new_x,
			       n_constraints, n_variables + 1, TRUE,
			       0.01, 1000, NULL, NULL);

	 for (i = 0; i < n_variables; i++)
	         x[i] = new_x[i];

	 g_free (wspace);

	 return found;
}

/*
 ---------------------------------------------------------------------
  Branch-And-Bound

  (C) Copyright 2000 by Jukka-Pekka Iivonen (iivonen@iki.fi)
 ---------------------------------------------------------------------
 */

gboolean
branch_and_bound (gnum_float *A, gnum_float *b, gnum_float *c, gnum_float *xx,
		  int n_constraints, int n_variables, int n_original,
		  gboolean max_flag, gnum_float e, int max_iter,
		  gboolean *int_r,
		  affscale_callback_fun_t fun, void *data, gnum_float *best)
{
        gboolean found;
	gnum_float  *x, z;
	int      i;

	x = g_new (gnum_float, n_variables);

	display (A, n_variables, n_constraints, "A");
	display (b, n_constraints, 1, "b");
	display (c, n_variables, 1, "c");

	found = affine_init (A, b, c, n_constraints, n_variables, x);
	if (!found) {
	        g_free (x);
	        return FALSE;
	}

	found = affine_scale (A, b, c, x, n_constraints, n_variables, max_flag,
			      e, max_iter, NULL, NULL);

	if (!found) {
	        g_free (x);
		return FALSE;
	}

	z = 0;
	for (i = 0; i < n_variables; i++)
	        z += c[i] * x[i];

	if (max_flag) {
	        if (z < *best)
		        return FALSE;
	} else {
	        if (z > *best)
		        return FALSE;
	}

	for (i = 0; i < n_variables; i++)
	        if (int_r[i] && fabs (x[i] - rint (x[i])) > 0.0001) {
		        gnum_float  rhs_1, rhs_2;
			gnum_float  *lA, *rA;
			gnum_float  *lb, *rb;
			gnum_float  *lrc;
			gboolean f1, f2;
			int      l, k;

			rhs_1 = floor (x[i]);
			rhs_2 = ceil (x[i]);

			lA =  g_new (gnum_float,
				     (n_constraints + 1) * (n_variables + 1));
			lb =  g_new (gnum_float, n_constraints + 1);
			lrc = g_new (gnum_float, n_variables + 1);
			rA =  g_new (gnum_float,
				     (n_constraints + 1) * (n_variables + 1));
			rb =  g_new (gnum_float, n_constraints + 1);
			/* FIXME: int_r too */

			for (k = 0; k < n_variables; k++)
			        lrc[k] = c[k];
			lrc[k] = 0;

			for (k = 0; k < n_constraints; k++)
			        lb[k] = rb[k] = b[k];
			lb[k] = rhs_1;
			rb[k] = rhs_2;

			for (k = 0; k < n_variables; k++) {
			        for (l = 0; l < n_constraints; l++)
				        lA[l + k * (n_constraints + 1)] =
					  rA[l + k * (n_constraints + 1)] =
					  A[l + k * n_constraints];
				if (k == i)
				        lA[l + k * (n_constraints + 1)] =
					  rA[l + k * (n_constraints + 1)] = 1;
				else
				        lA[l + k * (n_constraints + 1)] =
					  rA[l + k * (n_constraints + 1)] = 0;
			}
			for (k = 0; k < n_constraints; k++)
			        lA[k + n_variables * (n_constraints + 1)] =
				  rA[k + n_variables * (n_constraints + 1)] = 0;
			lA[k + n_variables * (n_constraints + 1)] = 1;
			rA[k + n_variables * (n_constraints + 1)] = -1;

			f1 = branch_and_bound (lA, lb, lrc, xx,
					       n_constraints + 1, n_variables + 1,
					       n_original, TRUE, e, max_iter,
					       int_r, fun, data, best);

			f2 = branch_and_bound (rA, rb, lrc, xx,
					       n_constraints + 1, n_variables + 1,
					       n_original, TRUE, e, max_iter,
					       int_r, fun, data, best);

			g_free (lA);
			g_free (lb);
			g_free (rA);
			g_free (rb);
			g_free (lrc);

			return f1 || f2;
		} else if (int_r[i])
		        x[i] = rint (x[i]);

	if (max_flag) {
	        if (z > *best) {
		        *best = z;
			for (i = 0; i < n_original; i++)
			        xx[i] = x[i];
		}
	} else {
	        if (z < *best) {
		        *best = z;
			for (i = 0; i < n_original; i++)
			        xx[i] = x[i];
		}
	}

	g_free (x);
	return TRUE;
}

void
stern_brocot (float val, int max_denom, int *res_num, int *res_denom)
{
	int an = 0, ad = 1;
	int bn = 1, bd = 1;
	int n, d;
	float sp, delta;

	while ((d = ad + bd) <= max_denom) {
		sp = 1e-5 * d;	/* Quick and dirty,  do adaptive later */
		n = an + bn;
		delta = val * d - n;
		if (delta > sp)
		{
			an = n;
			ad = d;
		} else if (delta < -sp)
		{
			bn = n;
			bd = d;
		} else {
			*res_num = n;
			*res_denom = d;
			return;
		}
	}
	if (bd > max_denom || fabs (val * ad - an) < fabs (val * bd - bn)) {
		*res_num = an;
		*res_denom = ad;
	} else {
		*res_num = bn;
		*res_denom = bd;
	}
}

#ifdef STANDALONE
int main ()
{
  gnum_float A[] = { 11, 5, 6, 50, 1, 0, 0, 1 };
  gnum_float b[] = { 66, 225 };
  gnum_float c[] = {  1,  5, 0, 0 };
  gnum_float x[4];
  int     ind_row_added_r[] = { -1, -1 };
  int     ind_col_added_r[] = { -1, -1 };
  int     i;
  gnum_float best = 0;
  gboolean r_int[] = { TRUE, TRUE };

  int n_variables = 4;
  int n_constraints = 2;

  branch_and_bound (A, b, c, x, n_constraints, n_variables, n_variables, TRUE,
		    0.000001, 10000, r_int,
		    NULL, NULL, &best);
  printf ("=========================================================\n");
  printf ("optimal=%g\n", best);
  for (i = 0; i < n_variables; i++)
    printf ("%g\t", x[i]);
  printf ("\n");
}

#endif
