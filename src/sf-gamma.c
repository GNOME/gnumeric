#include <gnumeric-config.h>
#include <sf-gamma.h>
#include <sf-trig.h>
#include <mathfunc.h>

#define IEEE_754
#define ML_ERR_return_NAN { return gnm_nan; }
#define ML_WARN_return_NAN { return gnm_nan; }
#define ML_UNDERFLOW (GNM_EPSILON * GNM_EPSILON)
#define ML_ERROR(cause) do { } while(0)
#define ML_WARNING(typ,what) g_printerr("sf-gamma: trouble in %s\n", (what))

static int qgammaf (gnm_float x, GnmQuad *mant, int *expb);
static void pochhammer_small_n (gnm_float x, gnm_float n, GnmQuad *res);

/* ------------------------------------------------------------------------- */
/* --- BEGIN MAGIC R SOURCE MARKER --- */

// The following source code was imported from the R project.
// It was automatically transformed by tools/import-R.

/* Imported src/nmath/chebyshev.c from R.  */
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
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  SYNOPSIS
 *
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


/* NaNs propagated correctly */


/* Definition of function chebyshev_init removed.  */


static gnm_float chebyshev_eval(gnm_float x, const gnm_float *a, const int n)
{
    gnm_float b0, b1, b2, twox;
    int i;

    if (n < 1 || n > 1000) ML_WARN_return_NAN;

    if (x < GNM_const(-1.1) || x > GNM_const(1.1)) ML_WARN_return_NAN;

    twox = x * 2;
    b2 = b1 = 0;
    b0 = 0;
    for (i = 1; i <= n; i++) {
	b2 = b1;
	b1 = b0;
	b0 = twox * b1 - b2 + a[n - i];
    }
    return (b0 - b2) * GNM_const(0.5);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/lgammacor.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-2001 The R Core Team
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
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  SYNOPSIS
 *
 *    #include <Rmath.h>
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
 *
 *  SEE ALSO
 *
 *    Loader(1999)'s stirlerr() {in ./stirlerr.c} is *very* similar in spirit,
 *    is faster and cleaner, but is only defined "fast" for half integers.
 */


static gnm_float lgammacor(gnm_float x)
{
    static const gnm_float algmcs[15] = {
	GNM_const(+.1666389480451863247205729650822e+0),
	GNM_const(-.1384948176067563840732986059135e-4),
	GNM_const(+.9810825646924729426157171547487e-8),
	GNM_const(-.1809129475572494194263306266719e-10),
	GNM_const(+.6221098041892605227126015543416e-13),
	GNM_const(-.3399615005417721944303330599666e-15),
	GNM_const(+.2683181998482698748957538846666e-17),
	GNM_const(-.2868042435334643284144622399999e-19),
	GNM_const(+.3962837061046434803679306666666e-21),
	GNM_const(-.6831888753985766870111999999999e-23),
	GNM_const(+.1429227355942498147573333333333e-24),
	GNM_const(-.3547598158101070547199999999999e-26),
	GNM_const(+.1025680058010470912000000000000e-27),
	GNM_const(-.3401102254316748799999999999999e-29),
	GNM_const(+.1276642195630062933333333333333e-30)
    };

    gnm_float tmp;

/* For IEEE double precision DBL_EPSILON = 2^-52 = 2.220446049250313e-16 :
 *   xbig = 2 ^ 26.5
 *   xmax = DBL_MAX / 48 =  2^1020 / 3 */
#define nalgm 5
#define xbig  GNM_const(94906265.62425156)
#define xmax  GNM_const(3.745194030963158e306)

    if (x < 10)
	ML_WARN_return_NAN
    else if (x >= xmax) {
	ML_WARNING(ME_UNDERFLOW, "lgammacor");
	/* allow to underflow below */
    }
    else if (x < xbig) {
	tmp = 10 / x;
	return chebyshev_eval(tmp * tmp * 2 - 1, algmcs, nalgm) / x;
    }
    return 1 / (x * 12);
}
/* Cleaning up done by tools/import-R:  */
#undef nalgm
#undef xbig
#undef xmax

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/lbeta.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-12 The R Core Team
 *  Copyright (C) 2003 The R Foundation
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
 *  along with this program; if not, a copy is available at
 *  https://www.R-project.org/Licenses/
 *
 *  SYNOPSIS
 *
 *    #include <Rmath.h>
 *    double lbeta(double a, double b);
 *
 *  DESCRIPTION
 *
 *    This function returns the value of the log beta function
 *
 *	log B(a,b) = log G(a) + log G(b) - log G(a+b)
 *
 *  NOTES
 *
 *    This routine is a translation into C of a Fortran subroutine
 *    by W. Fullerton of Los Alamos Scientific Laboratory.
 */


gnm_float gnm_lbeta(gnm_float a, gnm_float b)
{
    gnm_float corr, p, q;

#ifdef IEEE_754
    if(gnm_isnan(a) || gnm_isnan(b))
	return a + b;
#endif
    p = q = a;
    if(b < p) p = b;/* := min(a,b) */
    if(b > q) q = b;/* := max(a,b) */

    /* both arguments must be >= 0 */
    if (p < 0)
	ML_WARN_return_NAN
    else if (p == 0) {
	return gnm_pinf;
    }
    else if (!gnm_finite(q)) { /* q == +Inf */
	return gnm_ninf;
    }

    if (p >= 10) {
	/* p and q are big. */
	corr = lgammacor(p) + lgammacor(q) - lgammacor(p + q);
	return gnm_log(q) * GNM_const(-0.5) + M_LN_SQRT_2PI + corr
		+ (p - GNM_const(0.5)) * gnm_log(p / (p + q)) + q * gnm_log1p(-p / (p + q));
    }
    else if (q >= 10) {
	/* p is small, but q is big. */
	corr = lgammacor(q) - lgammacor(p + q);
	return gnm_lgamma(p) + corr + p - p * gnm_log(p + q)
		+ (q - GNM_const(0.5)) * gnm_log1p(-p / (p + q));
    }
    else {
	/* p and q are small: p <= q < 10. */
	/* R change for very small args */
	if (p < GNM_const(1e-306)) return gnm_lgamma(p) + (gnm_lgamma(q) - gnm_lgamma(p+q));
	else return gnm_lgamma (p) + gnm_lgamma (q) - gnm_lgamma (p + q);
    }
}

/* ------------------------------------------------------------------------ */
/* --- END MAGIC R SOURCE MARKER --- */

// dstirlerr(x) = stirlerr(x+1) - stirlerr(x)
// but computed independently.
static gnm_float
dstirlerr (gnm_float x)
{
	gnm_float xph = x + GNM_const(0.5);
	if (x < GNM_const(0.5))
		return 1 - xph * gnm_log1p (1 / x);
	if (x < 2)
		return -1 / (2 * x) - xph * log1pmx (1 / x);
	return -(x + 2) / (12 * x * x * x) - xph * gnm_taylor_log1p (1 / x, 4);
}


/* Parts from src/nmath/stirlerr.c from R.
 * Copyright (C) 2000, The R Core Team
 *
 * stirlerr(n) = log(n!) - log( sqrt(2*pi*n)*(n/e)^n )
 *             = log(n!) - (n + 1/2) * log(n) + n - log(2*pi)/2
 */
gnm_float
stirlerr (gnm_float n)
{
	gnm_float S0 = GNM_const(1.) / 12;
	gnm_float S1 = GNM_const(1.) / 360;
	gnm_float S2 = GNM_const(1.) / 1260;
	gnm_float S3 = GNM_const(1.) / 1680;
	gnm_float S4 = GNM_const(1.) / 1188;
	gnm_float S5 = GNM_const(691.) / 360360;
	gnm_float S6 = GNM_const(1.) / 156;
	gnm_float S7 = GNM_const(3617.) / 122400;
	gnm_float S8 = GNM_const(43867.) / 244188;

	static const gnm_float sferr_halves[31] = {
		(gnm_float)NAN,                          /* 0.0 */
		GNM_const(0.1534264097200273452913848),  /* 0.5 */
		GNM_const(0.0810614667953272582196702),  /* 1.0 */
		GNM_const(0.0548141210519176538961390),  /* 1.5 */
		GNM_const(0.0413406959554092940938221),  /* 2.0 */
		GNM_const(0.03316287351993628748511048), /* 2.5 */
		GNM_const(0.02767792568499833914878929), /* 3.0 */
		GNM_const(0.02374616365629749597132920), /* 3.5 */
		GNM_const(0.02079067210376509311152277), /* 4.0 */
		GNM_const(0.01848845053267318523077934), /* 4.5 */
		GNM_const(0.01664469118982119216319487), /* 5.0 */
		GNM_const(0.01513497322191737887351255), /* 5.5 */
		GNM_const(0.01387612882307074799874573), /* 6.0 */
		GNM_const(0.01281046524292022692424986), /* 6.5 */
		GNM_const(0.01189670994589177009505572), /* 7.0 */
		GNM_const(0.01110455975820691732662991), /* 7.5 */
		GNM_const(0.010411265261972096497478567), /* 8.0 */
		GNM_const(0.009799416126158803298389475), /* 8.5 */
		GNM_const(0.009255462182712732917728637), /* 9.0 */
		GNM_const(0.008768700134139385462952823), /* 9.5 */
		GNM_const(0.008330563433362871256469318), /* 10.0 */
		GNM_const(0.007934114564314020547248100), /* 10.5 */
		GNM_const(0.007573675487951840794972024), /* 11.0 */
		GNM_const(0.007244554301320383179543912), /* 11.5 */
		GNM_const(0.006942840107209529865664152), /* 12.0 */
		GNM_const(0.006665247032707682442354394), /* 12.5 */
		GNM_const(0.006408994188004207068439631), /* 13.0 */
		GNM_const(0.006171712263039457647532867), /* 13.5 */
		GNM_const(0.005951370112758847735624416), /* 14.0 */
		GNM_const(0.005746216513010115682023589), /* 14.5 */
		GNM_const(0.005554733551962801371038690)  /* 15.0 */
	};
	gnm_float nn, y;

	if (!(n > 0))
		return gnm_nan;

	if (n <= (G_N_ELEMENTS (sferr_halves) - 1) / 2) {
		gnm_float nn = n + n;
		if (nn == (int)nn) return (sferr_halves[(int)nn]);
	}

	nn = n * n;

	// These breakpoints are selected to give a relative error of 2^-53.
	if (n > GNM_const(3043.)) return (S0-S1/nn)/n;
	if (n > GNM_const(200.2)) return (S0-(S1-S2/nn)/nn)/n;
	if (n > GNM_const(55.57)) return (S0-(S1-(S2-S3/nn)/nn)/nn)/n;
	if (n > GNM_const(27.01)) return (S0-(S1-(S2-(S3-S4/nn)/nn)/nn)/nn)/n;
	if (n > GNM_const(17.23)) return (S0-(S1-(S2-(S3-(S4-S5/nn)/nn)/nn)/nn)/nn)/n;
	if (n > GNM_const(12.77)) return (S0-(S1-(S2-(S3-(S4-(S5-S6/nn)/nn)/nn)/nn)/nn)/nn)/n;
	if (n > GNM_const(10.38)) return (S0-(S1-(S2-(S3-(S4-(S5-(S6-S7/nn)/nn)/nn)/nn)/nn)/nn)/nn)/n;
	if (n > GNM_const(8.946)) return (S0-(S1-(S2-(S3-(S4-(S5-(S6-(S7-S8/nn)/nn)/nn)/nn)/nn)/nn)/nn)/nn)/n;

	// Adding a boat-load of more terms could get us down to 6-ish,
	// but no further.  The series is not convergent.


	y = 0;
	while (n < 9) {
		y -= dstirlerr (n);
		n++;
	}
	return y + stirlerr (n);
}



gnm_float
gnm_gammax (gnm_float x, int *expb)
{
	GnmQuad r;
	(void) qgammaf (x, &r, expb);
	return gnm_quad_value (&r);
}

/**
 * gnm_gamma:
 * @x: a number
 *
 * Returns: the Gamma function evaluated at @x for positive or
 * non-integer @x.
 */
gnm_float
gnm_gamma (gnm_float x)
{
	int e;
	gnm_float r = gnm_gammax (x, &e);
	return gnm_scalbn (r, e);
}

/* ------------------------------------------------------------------------- */

gnm_float
gnm_factx (gnm_float x, int *expb)
{
	GnmQuad r;
	(void)qfactf (x, &r, expb);
	return gnm_quad_value (&r);
}

/**
 * gnm_fact:
 * @x: number
 *
 * Returns: the factorial of @x, which must not be a negative integer.
 */
gnm_float
gnm_fact (gnm_float x)
{
	int e;
	gnm_float r = gnm_factx (x, &e);
	return gnm_scalbn (r, e);
}

/* ------------------------------------------------------------------------- */

/* 0: ok, 1: overflow, 2: nan */
static int
qbetaf (gnm_float a, gnm_float b, GnmQuad *mant, int *expb)
{
	GnmQuad ma, mb, mab;
	int ea, eb, eab;
	gnm_float ab = a + b;

	if (gnm_isnan (ab) ||
	    (a <= 0 && a == gnm_floor (a)) ||
	    (b <= 0 && b == gnm_floor (b)))
		return 2;

	if (ab <= 0 && ab == gnm_floor (ab)) {
		gnm_quad_init (mant, 0);
		*expb = 0;
		return 0;
	}

	if (b > a) {
		gnm_float s = a;
		a = b;
		b = s;
	}

	if (a > 1 && gnm_abs (b) < 1) {
		void *state;
		if (qgammaf (b, &mb, &eb))
			return 1;
		state = gnm_quad_start ();
		pochhammer_small_n (a, b, &ma);
		gnm_quad_div (mant, &mb, &ma);
		gnm_quad_end (state);
		*expb = eb;
		return 0;
	}

	if (!qgammaf (a, &ma, &ea) &&
	    !qgammaf (b, &mb, &eb) &&
	    !qgammaf (ab, &mab, &eab)) {
		void *state = gnm_quad_start ();
		gnm_quad_mul (&ma, &ma, &mb);
		gnm_quad_div (mant, &ma, &mab);
		gnm_quad_end (state);
		*expb = ea + eb - eab;
		return 0;
	} else
		return 1;
}

/**
 * gnm_beta:
 * @a: a number
 * @b: a number
 *
 * Returns: the Beta function evaluated at @a and @b.
 */
gnm_float
gnm_beta (gnm_float a, gnm_float b)
{
	GnmQuad r;
	int e;

	switch (qbetaf (a, b, &r, &e)) {
	case 0: return gnm_scalbn (gnm_quad_value (&r), e);
	case 1: return gnm_pinf;
	default: return gnm_nan;
	}
}

/**
 * gnm_lbeta3:
 * @a: a number
 * @b: a number
 * @sign: (out): the sign
 *
 * Returns: the logarithm of the absolute value of the Beta function
 * evaluated at @a and @b.  The sign will be stored in @sign as -1 or
 * +1.  This function is useful because the result of the beta
 * function can be too large for doubles.
 */
gnm_float
gnm_lbeta3 (gnm_float a, gnm_float b, int *sign)
{
	int sign_a, sign_b, sign_ab;
	gnm_float ab = a + b;
	gnm_float res_a, res_b, res_ab;
	GnmQuad r;
	int e;

	switch (qbetaf (a, b, &r, &e)) {
	case 0: {
		gnm_float m = gnm_quad_value (&r);
		*sign = (m >= 0 ? +1 : -1);
		return gnm_log (gnm_abs (m)) + e * (GNM_RADIX == 2 ? M_LN2gnum : M_LN10gnum);
	}
	case 1:
		/* Overflow */
		break;
	default:
		*sign = 1;
		return gnm_nan;
	}

	if (a > 0 && b > 0) {
		*sign = 1;
		return gnm_lbeta (a, b);
	}

	/* This is awful */
	res_a = gnm_lgamma_r (a, &sign_a);
	res_b = gnm_lgamma_r (b, &sign_b);
	res_ab = gnm_lgamma_r (ab, &sign_ab);

	*sign = sign_a * sign_b * sign_ab;
	return res_a + res_b - res_ab;
}

/* ------------------------------------------------------------------------- */
/*
 * This computes the E(x) such that
 *
 *   Gamma(x) = sqrt(2Pi) * x^(x-1/2) * exp(-x) * E(x)
 *
 * x should be >0 and the result is, roughly, 1+1/(12x).
 */
static void
gamma_error_factor (GnmQuad *res, const GnmQuad *x)
{
	gnm_float num[] = {
		GNM_const(1.),
		GNM_const(1.),
		GNM_const(-139.),
		GNM_const(-571.),
		GNM_const(163879.),
		GNM_const(5246819.),
		GNM_const(-534703531.),
		GNM_const(-4483131259.),
		GNM_const(432261921612371.)
	};
	gnm_float den[] = {
		GNM_const(12.),
		GNM_const(288.),
		GNM_const(51840.),
		GNM_const(2488320.),
		GNM_const(209018880.),
		GNM_const(75246796800.),
		GNM_const(902961561600.),
		GNM_const(86684309913600.),
		GNM_const(514904800886784000.)
	};
	GnmQuad zn, xpn;
	int i;
	gnm_float xval = gnm_quad_value (x);
	int n;

	g_return_if_fail (xval > 0);

	// We want x >= 20 for the asymptotic expansion
	n = (xval < 20) ? (int)gnm_floor (21 - xval) : 0;
	gnm_quad_init (&xpn, n);
	gnm_quad_add (&xpn, &xpn, x);

	gnm_quad_init (&zn, 1);
	*res = zn;

	for (i = 0; i < (int)G_N_ELEMENTS (num); i++) {
		GnmQuad t, c;
		gnm_quad_mul (&zn, &zn, &xpn);
		gnm_quad_init (&c, den[i]);
		gnm_quad_mul (&t, &zn, &c);
		gnm_quad_init (&c, num[i]);
		gnm_quad_div (&t, &c, &t);
		gnm_quad_add (res, res, &t);
	}

	if (n > 0) {
		int i;
		GnmQuad en, xxn, xph;

		// Gamma(x) = sqrt(2Pi) * x^(x-1/2) * exp(-x) * E(x)
		// Gamma(x+n) = sqrt(2Pi) * (x+n)^(x+n-1/2) * exp(-x-n) * E(x+n)

		// E(x+n) / E(x) =
		// Gamma(x+n)/Gamma(x) * (x^(x-1/2) * exp(-x)) / ((x+n)^(x+n-1/2) * exp(-x-n)) =
		// (x*(x+1)*...*(x+n-1)) * exp(n) * (x/(x+n))^(x-1/2) / (x+n)^n =
		// ((x+1)*...*(x+n-1)) * exp(n) * (x/(x+n))^(x+1/2) / (x+n)^(n-1) =
		// ((x+1)/(x+n)*...*(x+n-1)/(x+n)) * exp(n) * (x/(x+n))^(x+1/2)

		for (i = 1; i < n; i++) {
			// *= (x+i)/(x+n)
			GnmQuad xpi;
			gnm_quad_init (&xpi, i);
			gnm_quad_add (&xpi, &xpi, x);
			gnm_quad_div (res, res, &xpi);
			gnm_quad_mul (res, res, &xpn);
		}

		// /= exp(n)
		gnm_quad_init (&en, n);
		gnm_quad_exp (&en, NULL, &en);
		gnm_quad_div (res, res, &en);

		// /= (x/(x+n))^(x+1/2)
		gnm_quad_init (&xph, 0.5);
		gnm_quad_add (&xph, &xph, x);
		gnm_quad_div (&xxn, x, &xpn);
		gnm_quad_pow (&xxn, NULL, &xxn, &xph);
		gnm_quad_div (res, res, &xxn);
	}
}

/* ------------------------------------------------------------------------- */

static void
pochhammer_small_n (gnm_float x, gnm_float n, GnmQuad *res)
{
	GnmQuad qx, qn, qr, qs, f1, f2, f3, f4, f5;
	gnm_float r;
	gboolean debug = FALSE;

	g_return_if_fail (x >= 1);
	g_return_if_fail (gnm_abs (n) <= 1);

	/*
	 * G(x)   = c * x^(x-1/2) * exp(-x) * E(x)
	 * G(x+n) = c * (x+n)^(x+n-1/2) * exp(-(x+n)) * E(x+n)
	 *        = c * (x+n)^(x-1/2) * (x+n)^n * exp(-x) * exp(-n) * E(x+n)
	 *
	 * G(x+n)/G(x)
	 * = (1+n/x)^(x-1/2) * (x+n)^n * exp(-n) * E(x+n)/E(x)
	 * = (1+n/x)^x / sqrt(1+n/x) * (x+n)^n * exp(-n) * E(x+n)/E(x)
	 * = exp(x*log(1+n/x) - n) / sqrt(1+n/x) * (x+n)^n * E(x+n)/E(x)
	 * = exp(x*log1p(n/x) - n) / sqrt(1+n/x) * (x+n)^n * E(x+n)/E(x)
	 * = exp(x*(log1pmx(n/x)+n/x) - n) / sqrt(1+n/x) * (x+n)^n * E(x+n)/E(x)
	 * = exp(x*log1pmx(n/x) + n - n) / sqrt(1+n/x) * (x+n)^n * E(x+n)/E(x)
	 * = exp(x*log1pmx(n/x)) / sqrt(1+n/x) * (x+n)^n * E(x+n)/E(x)
	 */

	gnm_quad_init (&qx, x);
	gnm_quad_init (&qn, n);

	gnm_quad_div (&qr, &qn, &qx);
	r = gnm_quad_value (&qr);

	gnm_quad_add (&qs, &qx, &qn);

	/* exp(x*log1pmx(n/x)) */
	gnm_quad_mul12 (&f1, log1pmx (r), x);  /* sub-opt */
	gnm_quad_exp (&f1, NULL, &f1);
	if (debug) g_printerr ("f1=%.20g\n", gnm_quad_value (&f1));

	/* sqrt(1+n/x) */
	gnm_quad_add (&f2, &gnm_quad_one, &qr);
	gnm_quad_sqrt (&f2, &f2);
	if (debug) g_printerr ("f2=%.20g\n", gnm_quad_value (&f2));

	/* (x+n)^n */
	gnm_quad_pow (&f3, NULL, &qs, &qn);
	if (debug) g_printerr ("f3=%.20g\n", gnm_quad_value (&f3));

	/* E(x+n) */
	gamma_error_factor (&f4, &qs);
	if (debug) g_printerr ("f4=%.20g\n", gnm_quad_value (&f4));

	/* E(x) */
	gamma_error_factor (&f5, &qx);
	if (debug) g_printerr ("f5=%.20g\n", gnm_quad_value (&f5));

	gnm_quad_div (res, &f1, &f2);
	gnm_quad_mul (res, res, &f3);
	gnm_quad_mul (res, res, &f4);
	gnm_quad_div (res, res, &f5);
}

static gnm_float
pochhammer_naive (gnm_float x, int n)
{
	void *state = gnm_quad_start ();
	GnmQuad qp, qx;
	gnm_float r;

	qp = gnm_quad_one;
	gnm_quad_init (&qx, x);
	while (n-- > 0) {
		gnm_quad_mul (&qp, &qp, &qx);
		gnm_quad_add (&qx, &qx, &gnm_quad_one);
	}
	r = gnm_quad_value (&qp);
	gnm_quad_end (state);

	return r;
}

/**
 * pochhammer:
 * @x: a real number
 * @n: a real number, often an integer
 *
 * This function computes Pochhammer's symbol at @x and @n, i.e.,
 * Gamma(@x+@n)/Gamma(@x).  This is well defined unless @x or @x+@n is a
 * non-negative integer.  The ratio has a removable singlularity at @n=0
 * and the result is 1.
 *
 * Returns: Pochhammer's symbol (@x)_@n.
 */
gnm_float
pochhammer (gnm_float x, gnm_float n)
{
	gnm_float rn, rx, lr;
	GnmQuad m1, m2;
	int e1, e2;

	if (gnm_isnan (x) || gnm_isnan (n))
		return gnm_nan;

	if (n == 0)
		return 1;

	rx = gnm_floor (x);
	rn = gnm_floor (n);

	/*
	 * Use naive multiplication when n is a small integer.
	 * We don't want to use this if x is also an integer
	 * (but we might do so below if x is insanely large).
	 */
	if (n == rn && x != rx && n >= 0 && n < 40)
		return pochhammer_naive (x, (int)n);

	if (!qfactf (x + n - 1, &m1, &e1) &&
	    !qfactf (x - 1, &m2, &e2)) {
		void *state = gnm_quad_start ();
		int de = e1 - e2;
		GnmQuad qr;
		gnm_float r;

		gnm_quad_div (&qr, &m1, &m2);
		r = gnm_quad_value (&qr);
		gnm_quad_end (state);

		return gnm_scalbn (r, de);
	}

	if (x == rx && x <= 0) {
		if (n != rn)
			return 0;
		if (x == 0)
			return (n > 0)
				? 0
				: ((gnm_fmod (-n, 2) == 0 ? +1 : -1) /
				   gnm_fact (-n));
		if (n > -x)
			return gnm_nan;
	}

	/*
	 * We have left the common cases.  One of x+n and x is
	 * insanely big, possibly both.
	 */

	if (gnm_abs (x) < 1)
		return gnm_pinf;

	if (n < 0)
		return 1 / pochhammer (x + n, -n);

	if (n == rn && n >= 0 && n < 100)
		return pochhammer_naive (x, (int)n);

	if (gnm_abs (n) < 1) {
		/* x is big.  */
		void *state = gnm_quad_start ();
		GnmQuad qr;
		gnm_float r;
		pochhammer_small_n (x, n, &qr);
		r = gnm_quad_value (&qr);
		gnm_quad_end (state);
		return r;
	}

	/* Panic mode.  */
	g_printerr ("x=%.20g  n=%.20g\n", x, n);
	lr = ((x - GNM_const(0.5)) * gnm_log1p (n / x) +
	      n * gnm_log (x + n) -
	      n +
	      (lgammacor (x + n) - lgammacor (x)));
	return gnm_exp (lr);
}

/* ------------------------------------------------------------------------- */

static void
rescale_mant_exp (GnmQuad *mant, int *expb)
{
	int e;
	(void)gnm_unscalbn (gnm_quad_value (mant), &e);
	*expb += e;
	gnm_quad_scalbn (mant, mant, -e);
}

/* Tabulate up to, but not including, this number.  */
#define QFACTI_LIMIT 10000

static gboolean
qfacti (int n, GnmQuad *mant, int *expb)
{
	static GnmQuad mants[QFACTI_LIMIT];
	static int expbs[QFACTI_LIMIT];
	static int init = 0;

	if (n < 0 || n >= QFACTI_LIMIT) {
		*mant = gnm_quad_zero;
		*expb = 0;
		return TRUE;
	}

	if (n >= init) {
		void *state = gnm_quad_start ();

		if (init == 0) {
			gnm_quad_init (&mants[0], GNM_const(1.) / GNM_RADIX);
			expbs[0] = 1;
			init++;
		}

		while (n >= init) {
			GnmQuad m;

			gnm_quad_init (&m, init);
			gnm_quad_mul (&mants[init], &m, &mants[init - 1]);
			expbs[init] = expbs[init - 1];
			rescale_mant_exp (&mants[init], &expbs[init]);

			init++;
		}

		gnm_quad_end (state);
	}

	*mant = mants[n];
	*expb = expbs[n];
	return FALSE;
}

/* 0: ok, 1: overflow, 2: nan */
int
qfactf (gnm_float x, GnmQuad *mant, int *expb)
{
	void *state;
	gboolean res = 0;

	*expb = 0;

	if (gnm_isnan (x) || (x < 0 && x == gnm_floor (x))) {
		mant->h = mant->l = gnm_nan;
		return 2;
	}

	if (x >= G_MAXINT / 2) {
		mant->h = mant->l = gnm_pinf;
		return 1;
	}

	if (x == gnm_floor (x)) {
		/* 0, 1, 2, ...  */
		if (!qfacti ((int)x, mant, expb))
			return 0;
	}

	state = gnm_quad_start ();

	if (x < -1) {
		if (qfactf (-x - 1, mant, expb))
			res = 1;
		else {
			GnmQuad b;

			gnm_quad_init (&b, -x);
			gnm_quad_sinpi (&b, &b);
			gnm_quad_mul (&b, &b, mant);
			gnm_quad_div (mant, &gnm_quad_pi, &b);
			*expb = -*expb;
		}
	} else if (x >= QFACTI_LIMIT - GNM_const(0.5)) {
		/*
		 * Let y = x + 1 = m * 2^e; c = sqrt(2Pi).
		 *
		 * G(y) = c * y^(y-1/2) * exp(-y) * E(y)
		 *      = c * (y/e)^y / sqrt(y) * E(y)
		 */
		GnmQuad y, f1, f2, f3, f4;
		gnm_float ef2;
		gboolean debug = FALSE;

		if (debug) g_printerr ("x=%.20" GNM_FORMAT_g "\n", x);

		gnm_quad_init (&y, x + 1);
		*expb = 0;

		/* sqrt(2Pi) */
		gnm_quad_sqrt (&f1, &gnm_quad_2pi);
		if (debug) g_printerr ("f1=%.20" GNM_FORMAT_g " + %.20" GNM_FORMAT_g "\n",
				       f1.h, f1.l);

		/* (y/e)^y */
		gnm_quad_div (&f2, &y, &gnm_quad_e);
		gnm_quad_pow (&f2, &ef2, &f2, &y);
		if (debug) g_printerr ("f2=(%.20" GNM_FORMAT_g " + %.20" GNM_FORMAT_g ") * B^%d\n",
				       f2.h, f2.l, (int)ef2);
		if (ef2 > G_MAXINT / 2 || ef2 < G_MININT / 2) {
			res = 1;
			f2.h = f2.l = gnm_pinf;
		} else
			*expb += (int)ef2;

		/* sqrt(y) */
		gnm_quad_sqrt (&f3, &y);
		if (debug) g_printerr ("f3=%.20" GNM_FORMAT_g " + %.20" GNM_FORMAT_g "\n",
				       f3.h, f3.l);

		/* E(x) */
		gamma_error_factor (&f4, &y);
		if (debug) g_printerr ("f4=%.20" GNM_FORMAT_g " + %.20" GNM_FORMAT_g "\n",
				       f4.h, f4.l);

		gnm_quad_mul (mant, &f1, &f2);
		gnm_quad_div (mant, mant, &f3);
		gnm_quad_mul (mant, mant, &f4);

		if (debug) g_printerr ("G(x+1)=(%.20" GNM_FORMAT_g " + %.20" GNM_FORMAT_g ") * B^%d %s\n",
				       mant->h, mant->l, *expb, res ? "overflow" : "");
	} else {
		GnmQuad s, qx, mFw;
		gnm_float w, f;
		int eFw;

		/*
		 * w integer, |f|<=0.5, x=w+f.
		 *
		 * Do this before we do the stepping below which would kill
		 * up to 4 bits of accuracy of f.
		 */
		w = gnm_round (x);
		f = x - w;
		gnm_quad_init (&qx, x);

		gnm_quad_init (&s, 1);
		while (w < 20) {
			gnm_quad_add (&qx, &qx, &gnm_quad_one);
			w++;
			gnm_quad_mul (&s, &s, &qx);
		}

		if (qfacti ((int)w, &mFw, &eFw)) {
			mant->h = mant->l = gnm_pinf;
			res = 1;
		} else {
			GnmQuad r;

			pochhammer_small_n (w + 1, f, &r);
			gnm_quad_mul (mant, &mFw, &r);
			gnm_quad_div (mant, mant, &s);
			*expb = eFw;
		}
	}

	if (res == 0)
		rescale_mant_exp (mant, expb);

	gnm_quad_end (state);
	return res;
}

/* 0: ok, 1: overflow, 2: nan */
static int
qgammaf (gnm_float x, GnmQuad *mant, int *expb)
{
	if (x < GNM_const(-1.5) || x > GNM_const(0.5))
		return qfactf (x - 1, mant, expb);
	else if (gnm_isnan (x) || x == gnm_floor (x)) {
		*expb = 0;
		mant->h = mant->l = gnm_nan;
		return 2;
	} else {
		void *state = gnm_quad_start ();
		GnmQuad qx;

		qfactf (x, mant, expb);
		gnm_quad_init (&qx, x);
		gnm_quad_div (mant, mant, &qx);
		rescale_mant_exp (mant, expb);
		gnm_quad_end (state);
		return 0;
	}
}

/* ------------------------------------------------------------------------- */

/**
 * combin:
 * @n: total number of items
 * @k: number of items to pick
 *
 * Returns: the binomial coefficient of @n and @k.
 */
gnm_float
combin (gnm_float n, gnm_float k)
{
	GnmQuad m1, m2, m3;
	int e1, e2, e3;
	gboolean ok;

	if (k < 0 || k > n || n != gnm_floor (n) || k != gnm_floor (k))
		return gnm_nan;

	k = MIN (k, n - k);
	if (k == 0)
		return 1;
	if (k == 1)
		return n;

	ok = (n < G_MAXINT &&
	      !qfactf (n, &m1, &e1) &&
	      !qfactf (k, &m2, &e2) &&
	      !qfactf (n - k, &m3, &e3));

	if (ok) {
		void *state = gnm_quad_start ();
		gnm_float c;
		gnm_quad_mul (&m2, &m2, &m3);
		gnm_quad_div (&m1, &m1, &m2);
		c = gnm_scalbn (gnm_quad_value (&m1), e1 - e2 - e3);
		gnm_quad_end (state);
		return c;
	}

	if (k < 100) {
		void *state = gnm_quad_start ();
		GnmQuad p, a, b;
		gnm_float c;
		int i;

		gnm_quad_init (&p, 1);
		for (i = 0; i < k; i++) {
			gnm_quad_init (&a, n - i);
			gnm_quad_mul (&p, &p, &a);

			gnm_quad_init (&b, i + 1);
			gnm_quad_div (&p, &p, &b);
		}

		c = gnm_quad_value (&p);
		gnm_quad_end (state);
		return c;
	}

	return pochhammer (n - k + 1, k) / gnm_fact (k);
}

gnm_float
permut (gnm_float n, gnm_float k)
{
	if (k < 0 || k > n || n != gnm_floor (n) || k != gnm_floor (k))
		return gnm_nan;

	return pochhammer (n - k + 1, k);
}

gnm_float
gnm_fact2 (int x)
{
	static gnm_float table[400];
	static gboolean init = FALSE;

	if (x < 0)
		return gnm_nan;
	else if (x >= (int)G_N_ELEMENTS (table)) {
		int n = x / 2;
		if (x & 1) {
			int e1, e2;
			gnm_float res = gnm_factx (x, &e1) / gnm_factx (n, &e2);
#if GNM_RADIX == 2
			return gnm_ldexp (res, e1 - e2 - n);
#else
			return gnm_scalbn (gnm_ldexp (res, -n), e1 - e2);
#endif
		} else
			return gnm_ldexp (gnm_fact (n), n);
	}

	if (!init) {
		void *state = gnm_quad_start ();
		GnmQuad p[2];

		gnm_quad_init (&p[0], 1);
		gnm_quad_init (&p[1], 1);
		table[0] = table[1] = 1;

		for (unsigned i = 2; i < G_N_ELEMENTS (table); i++) {
			GnmQuad qi;
			gnm_quad_init (&qi, i);
			gnm_quad_mul (&p[i & 1], &p[i & 1], &qi);
			table[i] = gnm_quad_value (&p[i & 1]);
			if (isnan (table[i]))
				table[i] = gnm_pinf;
		}

		gnm_quad_end (state);
	}

	return table[x];
}


/* ------------------------------------------------------------------------- */

#ifdef GNM_SUPPLIES_LGAMMA
/* Avoid using signgam.  It may be missing in system libraries.  */
int signgam;

double
lgamma (double x)
{
	return lgamma_r (x, &signgam);
}
#endif

#ifdef GNM_SUPPLIES_LGAMMA_R
double
lgamma_r (double x, int *signp)
{
	*signp = +1;

	if (gnm_isnan (x))
		return gnm_nan;

	if (x > 0) {
		gnm_float f = 1;

		while (x < 10) {
			f *= x;
			x++;
		}

		return (M_LN_SQRT_2PI + (x - 0.5) * gnm_log(x) -
			x + lgammacor(x)) - gnm_log (f);
	} else {
		gnm_float axm2 = gnm_fmod (-x, 2.0);
		gnm_float y = gnm_sinpi (axm2) / M_PIgnum;

		*signp = axm2 > 1.0 ? +1 : -1;

		return y == 0
			? gnm_nan
			: - gnm_log (gnm_abs (y)) - lgamma1p (-x);
	}
}
#endif

/* ------------------------------------------------------------------------- */


static const gnm_float lanczos_g = GNM_const (808618867.0) / 134217728;

/*
 * This Mathematica sniplet computes the Lanczos gamma coefficients:
 *
 * Dr[k_]:=DiagonalMatrix[Join[{1},Array[-Binomial[2*#-1,#]*#&,k]]]
 * c[k_]:= Array[If[#1+#2==2,1/2,If[#1>=#2,(-1)^(#1+#2)*4^(#2-1)*(#1-1)*(#1+#2-3)!/(#1-#2)!/(2*#2-2)!,0]]&,{k+1,k+1}]
 * Dc[k_]:=DiagonalMatrix[Array[2*(2*#-3)!!&,k+1]]
 * B[k_]:=Array[If[#1==1,1,If[#1<=#2,(-1)^(#2-#1)*Binomial[#1+#2-3,#1+#1-3],0]]&,{k+1,k+1}]
 * M[k_]:=(Dr[k].B[k]).(c[k].Dc[k])
 * f[g_,k_]:=Array[Sqrt[2]*(E/(2*(#-1+g)+1))^(#-1/2)&,k+1]
 * a[g_,k_]:=M[k].f[g,k]*Exp[g]/Sqrt[2*Pi]
 *
 * The result of a[g,k] will contain both positive and negative constants.
 * Most people using the Lanczos series do not understand that a naive
 * implementation will suffer significant cancellation errors.  The error
 * estimates assume the series is computed without loss!
 *
 * Following Boost we multiply the entire partial fraction by Pochhammer[z+1,k]
 * That gives us a polynomium with positive coefficient.  For kicks we toss
 * the constant factor back in.
 *
 * b[g_,k_]:=Sum[a[g,k][[i+1]]/If[i==0,1,(z+i)],{i,0,k}]*Pochhammer[z+1,k]
 * c13b:=Block[{$MaxExtraPrecision=500},FullSimplify[N[b[808618867/134217728,12]*Sqrt[2*Pi]/Exp[808618867/134217728],300]]]
 *
 * Finally we recast that in terms of gamma's argument:
 *
 * N[CoefficientList[c13b /. z->(zp-1),{zp}],50]
 *
 * Enter complex numbers, exit simplicity.  The error bounds for the
 * Lanczos approximation are bounds for the absolute value of the result.
 * The relative error on one of the coordinates can be much higher.
 */
static const gnm_float lanczos_num[] = {
	GNM_const(56906521.913471563880907910335591226868592353221448),
	GNM_const(103794043.11634454519062710536160702385539539810110),
	GNM_const(86363131.288138591455469272889778684223419113014358),
	GNM_const(43338889.324676138347737237405905333160850993321475),
	GNM_const(14605578.087685068084141699827913592185707234229516),
	GNM_const(3481712.1549806459088207101896477455646802362321652),
	GNM_const(601859.61716810987866702265336993523025071425740828),
	GNM_const(75999.293040145426498753034435989091370919973262979),
	GNM_const(6955.9996025153761403563101155151989875259157712039),
	GNM_const(449.94455690631681194468586076509884096232715968614),
	GNM_const(19.519927882476174828478609662356521362076846583112),
	GNM_const(0.50984166556566761881251786448046945099926051133936),
	GNM_const(0.0060618423462489065257837539645559368832224636654970)
};

/* CoefficientList[Pochhammer[z,12],z] */
static const guint32 lanczos_denom[G_N_ELEMENTS(lanczos_num)] = {
	0, 39916800, 120543840, 150917976, 105258076, 45995730,
	13339535, 2637558, 357423, 32670, 1925, 66, 1
};

/**
 * gnm_complex_gamma:
 * @z: a complex number
 * @expb: (out) (allow-none): Return location for power-of-base
 *
 * Returns: (transfer full): the Gamma function evaluated at @z.
 */
gnm_complex
gnm_complex_gamma (gnm_complex z, int *expb)
{
	if (expb)
		*expb = 0;

	if (GNM_CREALP (z)) {
		return GNM_CREAL (expb ? gnm_gammax (z.re, expb) : gnm_gamma (z.re));
	} else if (z.re < 0) {
		/* Gamma(z) = pi / (sin(pi*z) * Gamma(-z+1)) */
		gnm_complex b = GNM_CMAKE (M_PIgnum * gnm_fmod (z.re, 2),
					   M_PIgnum * z.im);
		/* Hmm... sin overflows when b.im is large.  */
		gnm_complex res = GNM_CDIV (GNM_CREAL (M_PIgnum),
					    GNM_CMUL (gnm_complex_fact (GNM_CNEG (z), expb),
						      GNM_CSIN (b)));
		if (expb)
			*expb = -*expb;
		return res;
	} else {
		gnm_complex zmh, f, p, q;
		int i;

		i = G_N_ELEMENTS(lanczos_num) - 1;
		p = GNM_CREAL (lanczos_num[i]);
		q = GNM_CREAL (lanczos_denom[i]);
		while (--i >= 0) {
			p = GNM_CMUL (p, z);
			p.re += lanczos_num[i];
			q = GNM_CMUL (q, z);
			q.re += lanczos_denom[i];
		}

		zmh = GNM_CMAKE (z.re - GNM_const(0.5), z.im);
		f = GNM_CPOW (GNM_CADD (zmh, GNM_CREAL (lanczos_g)),
			      GNM_CSCALE (zmh, 0.5));

		return GNM_CMUL4 (f, GNM_CEXP (GNM_CNEG (zmh)), f, GNM_CDIV (p, q));
	}
}

/* ------------------------------------------------------------------------- */

/**
 * gnm_complex_fact:
 * @z: a complex number
 * @expb: (out) (allow-none): Return location for power-of-base.
 *
 * Returns: (transfer full): the factorial function evaluated at @z.
 */
gnm_complex
gnm_complex_fact (gnm_complex z, int *expb)
{
	if (expb)
		*expb = 0;

	if (GNM_CREALP (z)) {
		return GNM_CREAL (expb ? gnm_factx (z.re, expb) : gnm_fact (z.re));
	} else {
		// This formula is valid for all arguments except zero
		// which we conveniently handled above.
		return GNM_CMUL (gnm_complex_gamma (z, expb), z);
	}
}

/* ------------------------------------------------------------------------- */

// D(a,z) := z^a * exp(-z) / Gamma (a + 1)
static gnm_complex
complex_temme_D (gnm_complex a, gnm_complex z)
{
	gnm_complex t;

	// The idea here is to control intermediate sizes and to avoid
	// accuracy problems caused by exp and pow.  For now, do neither.

	t = GNM_CDIV (GNM_CPOW (z, a), GNM_CEXP (z));
	return GNM_CDIV (t, gnm_complex_fact (a, NULL));
}


typedef void (*GnmComplexContinuedFraction) (gnm_complex *ai, gnm_complex *bi,
					     size_t i, const gnm_complex *args);

static gboolean
gnm_complex_continued_fraction (gnm_complex *dst, size_t N,
				GnmComplexContinuedFraction cf,
				gnm_complex const *args)
{
	gnm_complex A0, A1, B0, B1;
	size_t i;
	const gboolean debug_cf = FALSE;

	A0 = B1 = GNM_C1;
	A1 = B0 = GNM_C0;

	for (i = 1; i < N; i++) {
		gnm_complex ai, bi, t1, t2, c1, c2, A2, B2;
		gnm_float m;
		const gnm_float BIG = GNM_const(18446744073709551616.0);

		cf (&ai, &bi, i, args);

		/* Update A. */
		t1 = GNM_CMUL (bi, A1);
		t2 = GNM_CMUL (ai, A0);
		A2 = GNM_CADD (t1, t2);
		A0 = A1; A1 = A2;

		/* Update B. */
		t1 = GNM_CMUL (bi, B1);
		t2 = GNM_CMUL (ai, B0);
		B2 = GNM_CADD (t1, t2);
		B0 = B1; B1 = B2;

		/* Rescale */
		m = gnm_abs (B1.re) + gnm_abs (B1.im);
		if (m >= BIG || m <= 1 / BIG) {
			int e;
			gnm_float s;

			if (m == 0)
				return FALSE;

			(void)gnm_unscalbn (m, &e);
			if (debug_cf)
				g_printerr ("rescale by %d^%d\n", GNM_RADIX, -e);

			s = gnm_scalbn (1, -e);
			A0 = GNM_CSCALE (A0, s);
			A1 = GNM_CSCALE (A1, s);
			B0 = GNM_CSCALE (B0, s);
			B1 = GNM_CSCALE (B1, s);
		}

		/* Check for convergence */
		t1 = GNM_CMUL (A1, B0);
		t2 = GNM_CMUL (A0, B1);
		c1 = GNM_CSUB (t1, t2);

		c2 = GNM_CMUL (B0, B1);

		t1 = GNM_CDIV (A1, B1);
		if (debug_cf) {
			g_printerr ("  a : %.20g + %.20g I\n", ai.re, ai.im);
			g_printerr ("  b : %.20g + %.20g I\n", bi.re, bi.im);
			g_printerr ("  A : %.20g + %.20g I\n", A1.re, A1.im);
			g_printerr ("  B : %.20g + %.20g I\n", B1.re, B1.im);
			g_printerr ("%3zd : %.20g + %.20g I\n", i, t1.re, t1.im);
		}

		if (GNM_CABS (c1) <= GNM_CABS (c2) * (GNM_EPSILON /2))
			break;
	}

	if (i == N) {
		g_printerr ("continued fraction failed to converge.\n");
		// Make the failure obvious.
		*dst = GNM_CNAN;
		return FALSE;
	}

	*dst = GNM_CDIV (A1, B1);
	return TRUE;
}

static void
igamma_lower_coefs (gnm_complex *ai, gnm_complex *bi, size_t i,
		    gnm_complex const *args)
{
	gnm_complex const *a = args + 0;
	gnm_complex const *z = args + 1;

	if (i == 1)
		*ai = GNM_C1;
	else if (i & 1) {
		*ai = GNM_CSCALE (*z, i >> 1);
	} else {
		gnm_complex f = GNM_CMAKE (-(a->re + ((i >> 1) - 1)), -a->im);
		*ai = GNM_CMUL (f, *z);
	}

	*bi = GNM_CMAKE (a->re + (i - 1), a->im);
}

static gboolean
igamma_lower_cf (gnm_complex *dst, const gnm_complex *a, const gnm_complex *z)
{
	gnm_complex args[2] = { *a, *z };
	gnm_complex res;

	if (!gnm_complex_continued_fraction (&res, 100, igamma_lower_coefs, args))
		return FALSE;

	// FIXME: The following should be handled without creating big numbers.
	*dst = GNM_CMUL3 (res, GNM_CEXP (GNM_CNEG (*z)), GNM_CPOW (*z, *a));

	return TRUE;
}

static gboolean
igamma_upper_asymp (gnm_complex *dst, const gnm_complex *a, const gnm_complex *z)
{
	gnm_float am = GNM_CABS (*a);
	gnm_float zm = GNM_CABS (*z);
	gnm_float n0;
	gnm_complex s, t;
	gboolean debug = FALSE;
	size_t i;

	if (am >= zm)
		return FALSE;

	// Things start to diverge here
	n0 = a->re + gnm_sqrt (zm * zm - a->im * a->im);
	if (debug)
		g_printerr ("n0=%g\n", n0);
	(void)n0;

	// FIXME: Verify this condition for whether we have enough
	// precision at term n0
	if (2 * zm < GNM_MANT_DIG * M_LN2gnum)
		return FALSE;

	s = GNM_C0;

	t = complex_temme_D (GNM_CSUB (*a, GNM_C1), *z);

	for (i = 0; i < 100; i++) {
		s = GNM_CADD (s, t);

		if (debug) {
			g_printerr ("%3zd: t=%.20g + %.20g * I\n", i, t.re, t.im);
			g_printerr ("   : s=%.20g + %.20g * I\n", s.re, s.im);
		}

		if (GNM_CABS (t) <= GNM_CABS (s) * GNM_EPSILON) {
			if (debug)
				g_printerr ("igamma_upper_asymp converged.\n");
			*dst = s;
			return TRUE;
		}

		t = GNM_CDIV (t, *z);
		t = GNM_CMUL (t, GNM_CSUB (*a, GNM_CREAL (i + 1)));
	}

	if (debug)
		g_printerr ("igamma_upper_asymp failed to converge.\n");

	return FALSE;
}

static void
fixup_upper_real (gnm_complex *res, gnm_complex a, gnm_complex z)
{
	// This assumes we have an upper gamma regularized result.
	//
	// It appears that upper algorithms have trouble with negative real z
	// (for example, such z being outside the allowed domain) in some cases.

	if (GNM_CREALP (z) && z.re < 0 &&
	    GNM_CREALP (a) && a.re != gnm_floor (a.re)) {
		// Everything in the lower power series is real except z^a
		// which is not.  So...
		// 1. Flip to lower gamma
		// 2. Assume modulus is correct
		// 3. Use exact angle for lower gamma
		// 4. Flip back to upper gamma
		gnm_complex lres = GNM_CSUB (GNM_C1, *res);
		*res = GNM_CPOLARPI (GNM_CABS (lres), a.re);
		*res = GNM_CSUB (GNM_C1, *res);
	}
}

/**
 * gnm_complex_igamma:
 * @a: a complex number
 * @z: a complex number
 * @lower: determines if upper or lower incomplete gamma is desired.
 * @regularized: determines if the result should be normalized by Gamma(@a).
 *
 * Returns: (transfer full): the incomplete gamma function evaluated at
 * @a and @z.
 */
gnm_complex
gnm_complex_igamma (gnm_complex a, gnm_complex z,
		    gboolean lower, gboolean regularized)
{
	gnm_complex res, ga;
	gboolean have_lower, have_regularized;
	gboolean have_ga = FALSE;

	if (regularized && GNM_CREALP (a) &&
	    a.re <= 0 && a.re == gnm_floor (a.re)) {
		res = GNM_C0;
		have_lower = FALSE;
		have_regularized = TRUE;
		goto fixup;
	}

	if (GNM_CREALP (a) && a.re >= 0 &&
	    GNM_CREALP (z) && z.re >= 0) {
		res = GNM_CREAL (pgamma (z.re, a.re, 1, lower, FALSE));
		have_lower = lower;
		have_regularized = TRUE;
		goto fixup;
	}

	if (igamma_upper_asymp (&res, &a, &z)) {
		have_lower = FALSE;
		have_regularized = TRUE;
		fixup_upper_real (&res, a, z);
		goto fixup;
	}

	if (igamma_lower_cf (&res, &a, &z)) {
		have_lower = TRUE;
		have_regularized = FALSE;
		goto fixup;
	}

	// Failure of all sub-methods.
	return GNM_CNAN;

fixup:
	// Fixup to the desired form as needed.  This is not ideal.
	// 1. Regularizing here is too late due to overflow.
	// 2. Upper/lower switch can have cancellation
	if (regularized != have_regularized) {
		ga = gnm_complex_gamma (a, NULL);
		have_ga = TRUE;

		if (have_regularized)
			res = GNM_CMUL (res, ga);
		else
			res = GNM_CDIV (res, ga);
		have_regularized = TRUE;
	}

	if (lower != have_lower) {
		if (have_regularized) {
			res = GNM_CSUB (GNM_C1, res);
		} else {
			if (!have_ga)
				ga = gnm_complex_gamma (a, NULL);
			res = GNM_CSUB (ga, res);
		}
	}

	return res;
}

/* ------------------------------------------------------------------------- */

static gnm_float
gnm_digamma_series_1 (gnm_float x)
{
	static const gnm_float ctr = 3414350731.0 / 4294967296.0; // ~ x0-2/3
	// Taylor series data for digamma(x)*x around ctr
	// (Multiplying by x eliminates the pole at 0 and improves convergence)

	// There are more terms here than will be used in practice
	static const gnm_float c[] = {
		GNM_const(-1.393604931385844667205297), // cst
		GNM_const(+0.7838726021041081531302582), // z
		GNM_const(+1.820471535319717826256316), // z^2
		GNM_const(+0.2300704039473615371242174), // z^3
		GNM_const(-0.03648708728785595477443336), // z^4
		GNM_const(+0.008663338335810582341288719), // z^5
		GNM_const(-0.002436194723850649598022839), // z^6
		GNM_const(+0.0007486622557872255311371203), // z^7
		GNM_const(-0.0002423133587459245107417307), // z^8
		GNM_const(+0.00008100113830883611703726430), // z^9
		GNM_const(-0.00002765115168760370451893173), // z^10
		GNM_const(+9.572584786684540889574004e-6), // z^11
		GNM_const(-3.345885770126257344664911e-6), // z^12
		GNM_const(+1.177300128979172845825083e-6), // z^13
		GNM_const(-4.161969426343619044066147e-7), // z^14
		GNM_const(+1.476236789046367348142744e-7), // z^15
		GNM_const(-5.248645227284800471117817e-8), // z^16
		GNM_const(+1.869315129102582931045594e-8), // z^17
		GNM_const(-6.665853754670506957976488e-9), // z^18
		GNM_const(+2.379136739280974154742874e-9), // z^19
		GNM_const(-8.497029470698846358950073e-10), // z^20
		GNM_const(+3.036142118559307675850845e-10), // z^21
		GNM_const(-1.085246878064370064490199e-10), // z^22
		GNM_const(+3.880126402094332901095669e-11), // z^23
		GNM_const(-1.387536654151506108828032e-11), // z^24
		GNM_const(+4.962524563617018345793409e-12), // z^25
		GNM_const(-1.775025683975804156201718e-12), // z^26
		GNM_const(+6.349488874733389900536889e-13), // z^27
		GNM_const(-2.271415182435993612263917e-13), // z^28
		GNM_const(+8.125903477897147090860925e-14), // z^29
		GNM_const(-2.907097355266920392577544e-14), // z^30
		GNM_const(+1.040056414044071726030447e-14), // z^31
		GNM_const(-3.721012573246943428604950e-15), // z^32
		GNM_const(+1.331283261904345080561599e-15), // z^33
		GNM_const(-4.763032612601286523705145e-16), // z^34
		GNM_const(+1.704116960031678756548478e-16), // z^35
		GNM_const(-6.097015154289962965498327e-17), // z^36
		GNM_const(+2.181406718966981191594648e-17), // z^37
		GNM_const(-7.804716283314031896188832e-18), // z^38
		GNM_const(+2.792404989185037140120149e-18), // z^39
		GNM_const(-9.990800520119412094515637e-19)  // z^40
	};

	gnm_float sum, xn, eps, dx;
	unsigned ui;

	dx = xn = x - ctr;
	sum = c[0] + c[1] * xn;
	eps = gnm_abs (GNM_EPSILON / 2 * sum);
	for (ui = 2; ui < G_N_ELEMENTS (c); ui++) {
		gnm_float t;
		xn *= dx;
		t = c[ui] * xn;
		sum += t;
		if (gnm_abs (t) < eps)
			break;
	}

	return sum / x / (x + 1);
}

static gnm_float
gnm_digamma_series_2 (gnm_float x, gnm_float dx)
{
	// Taylor series data for digamma(x)*x around x0.
	// (Multiplying by x eliminates the pole at 0 and improves convergence)

	// There are more terms here than will be used in practice
	static const gnm_float c[] = {
		0,
		GNM_const(1.414380859739958132208209), // z
		GNM_const(+0.3205153650531439606356288), // z^2
		GNM_const(-0.06493160890417499678330267), // z^3
		GNM_const(+0.01887583274794994723362426), // z^4
		GNM_const(-0.006343606951359283604253287), // z^5
		GNM_const(+0.002294851106215796610898052), // z^6
		GNM_const(-0.0008656448634441624396007814), // z^7
		GNM_const(+0.0003349197451448133179202073), // z^8
		GNM_const(-0.0001316774179498895538138516), // z^9
		GNM_const(+0.00005231455693269487786690492), // z^10
		GNM_const(-0.00002092930898551028581067484), // z^11
		GNM_const(+8.412567983061925868991692e-6), // z^12
		GNM_const(-3.392327126020536111624551e-6), // z^13
		GNM_const(+1.370973972130058130320036e-6), // z^14
		GNM_const(-5.549180707134621401005220e-7), // z^15
		GNM_const(+2.248510299244865219544966e-7), // z^16
		GNM_const(-9.117750735408115351181446e-8), // z^17
		GNM_const(+3.699221275229322519704744e-8), // z^18
		GNM_const(-1.501394539608077112213162e-8), // z^19
		GNM_const(+6.095280485458728954145874e-9), // z^20
		GNM_const(-2.474989843290409518138793e-9), // z^21
		GNM_const(+1.005102611341640470198718e-9), // z^22
		GNM_const(-4.082140595549856261286344e-10), // z^23
		GNM_const(+1.658037290401667848672372e-10), // z^24
		GNM_const(-6.734743373121082302361713e-11), // z^25
		GNM_const(+2.735661184007454408449954e-11), // z^26
		GNM_const(-1.111255343693481217856139e-11), // z^27
		GNM_const(+4.514116174157725512713376e-12), // z^28
		GNM_const(-1.833735949521707719130688e-12), // z^29
		GNM_const(+7.449112972569399411235872e-13), // z^30
		GNM_const(-3.026041976266472126189062e-13), // z^31
		GNM_const(+1.229269770874759794761121e-13), // z^32
		GNM_const(-4.993680849210878859449551e-14), // z^33
		GNM_const(+2.028594795343119634764731e-14), // z^34
		GNM_const(-8.240821397432176895280819e-15), // z^35
		GNM_const(+3.347697235347764148196203e-15), // z^36
		GNM_const(-1.359947630194034569577449e-15), // z^37
		GNM_const(+5.524569438901596063753430e-16), // z^38
		GNM_const(-2.244268739259513574290477e-16), // z^39
		GNM_const(+9.116988470680108150341624e-17)  // z^40
	};
	gnm_float sum, xn, eps;
	unsigned ui;

	xn = dx;
	sum = c[1] * xn;
	eps = gnm_abs (GNM_EPSILON * sum);
	for (ui = 2; ui < G_N_ELEMENTS (c); ui++) {
		gnm_float t;
		xn *= dx;
		t = c[ui] * xn;
		sum += t;
		if (gnm_abs (t) < eps)
			break;
	}

	return sum / x;
}

static gnm_float
gnm_digamma_series_3 (gnm_float x)
{
	static const gnm_float ctr = 9140973792.0 / 4294967296.0; // ~ x0+2/3

	// Taylor series data for digamma(x)*x*(x+1) around ctr.
	// (Multiplying by x eliminates the pole at 0 and improves convergence;
	// multiplying by x+1 removes trouble caused by the pole at -1.)
	//
	// There are more terms here than will be used in practice
	static const gnm_float c[] = {
		GNM_const(1.069187202106379964561108), // cst
		GNM_const(+1.772667605096075412537626), // z
		GNM_const(+0.2272125634616216308385530), // z^2
		GNM_const(-0.03340833758699978856544311), // z^3
		GNM_const(+0.007175553429733710899335576), // z^4
		GNM_const(-0.001806192980500979068857208), // z^5
		GNM_const(+0.0004945960000406938148418368), // z^6
		GNM_const(-0.0001423916069504330801643716), // z^7
		GNM_const(+0.00004231966722000581929615164), // z^8
		GNM_const(-0.00001284637919029649494826060), // z^9
		GNM_const(+3.956444268156385801727645e-6), // z^10
		GNM_const(-1.230919658902018354780620e-6), // z^11
		GNM_const(+3.857326410290438339904409e-7), // z^12
		GNM_const(-1.215068812516640310282077e-7), // z^13
		GNM_const(+3.842015503145882562666222e-8), // z^14
		GNM_const(-1.218213493657190927765958e-8), // z^15
		GNM_const(+3.870598142893619365308165e-9), // z^16
		GNM_const(-1.231667143855504792729306e-9), // z^17
		GNM_const(+3.923744199538871225509428e-10), // z^18
		GNM_const(-1.251053017217116281525239e-10), // z^19
		GNM_const(+3.991408929102272214329984e-11), // z^20
		GNM_const(-1.274041466704381992529912e-11), // z^21
		GNM_const(+4.068145278333075741751660e-12), // z^22
		GNM_const(-1.299351027914282051289942e-12), // z^23
		GNM_const(+4.150924791093867196981568e-13), // z^24
		GNM_const(-1.326263739748920828936488e-13), // z^25
		GNM_const(+4.238042170781100294818004e-14), // z^26
		GNM_const(-1.354374285142548716401069e-14), // z^27
		GNM_const(+4.328534597139797982202326e-15), // z^28
		GNM_const(-1.383454394822643441972787e-15), // z^29
		GNM_const(+4.421862837726160406096067e-16), // z^30
		GNM_const(-1.413377420676461469423437e-16), // z^31
		GNM_const(+4.517732012277313050103063e-17), // z^32
		GNM_const(-1.444075584733824688998366e-17), // z^33
		GNM_const(+4.615989316796428759128748e-18), // z^34
		GNM_const(-1.475515451985640283943034e-18), // z^35
		GNM_const(+4.716565090265976036820537e-19), // z^36
		GNM_const(-1.507683748197167538907172e-19), // z^37
		GNM_const(+4.819438643661667878185773e-20), // z^38
		GNM_const(-1.540579118701962073182260e-20), // z^39
		GNM_const(+4.924618369274725064707054e-21) // z^40
	};

	gnm_float sum, xn, eps, dx;
	unsigned ui;

	dx = xn = x - ctr;
	sum = c[0] + c[1] * xn;
	eps = gnm_abs (GNM_EPSILON / 2 * sum);
	for (ui = 2; ui < G_N_ELEMENTS (c); ui++) {
		gnm_float t;
		xn *= dx;
		t = c[ui] * xn;
		sum += t;
		if (gnm_abs (t) < eps)
			break;
	}

	return sum / x;
}

static gnm_float
gnm_digamma_asymp (gnm_float x)
{
	// Use asympototic series for exp(digamma(x+1/2))
	// The use of exp here makes for less cancellation.  Note, that the
	// asympototic series for plain digamma has a log(x) term, so we
	// need the log call anyway.  The use of +1/2 makes all the even
	// powers go away.

	// There are more terms here than will be used in practice
	static const gnm_float c[] = {
		1, // x
		GNM_const(+0.04166666666666666666666667), // x^-1
		GNM_const(-0.006423611111111111111111111), // x^-3
		GNM_const(+0.003552482914462081128747795), // x^-5
		GNM_const(-0.003953557448973030570252792), // x^-7
		GNM_const(+0.007365033269308668975914346), // x^-9
		GNM_const(-0.02073467582436813806307797), // x^-11
		GNM_const(+0.08238185223878776450850024), // x^-13
		GNM_const(-0.4396044368600812717750832), // x^-15
		GNM_const(+3.034822873180573561262723), // x^-17
		GNM_const(-26.32566091447594628148156)  // x^-19
	};

	gnm_float z = x - GNM_const(0.5), zm2 = 1 / (z * z), zn = z;
	gnm_float eps = GNM_EPSILON * z;
	gnm_float sum = z;
	unsigned ui;

	for (ui = 1; ui < G_N_ELEMENTS (c); ui++) {
		gnm_float t;
		zn *= zm2;
		t = c[ui] * zn;
		sum += t;
		if (gnm_abs (t) < eps)
			break;
	}

	return gnm_log (sum);
}


/**
 * gnm_digamma:
 * @x: a number
 *
 * Returns: the digamma function evaluated at @x.
 */
gnm_float
gnm_digamma (gnm_float x)
{
	// x0 = x0a + x0b is the positive root
#if GNM_RADIX == 2
	gnm_float x0a = GNM_const(1.4616321449683622457627052426687441766262054443359375);
	gnm_float x0b = GNM_const(9.549995429965697715184199075967050885129598840859878644035380181024307499273372559036557380022743e-17);
#elif GNM_RADIX == 10
	gnm_float x0a = GNM_const(1.461632144968362);
	gnm_float x0b = GNM_const(0.341262659542326e-15);
#else
#error "Code needs fixing"
#endif
	if (gnm_isnan (x))
		return x;

	if (x <= 0) {
		if (x == gnm_floor (x))
			return gnm_nan; // Including infinite

		// Reflection.  Not ideal near zeros
		return gnm_digamma (1 - x) - M_PIgnum * gnm_cotpi (x);
	}

	if (x < x0a - 1)
		// Single step up.  No cancellation as digamma is negative
		// at x+1.
		return gnm_digamma (x + 1) - 1 / x;

	if (x < x0a - GNM_const(1.0) / 3)
		// Series for range [0.46;1.13]
		return gnm_digamma_series_1 (x);

	if (x < x0a + GNM_const(1.0) / 3)
		// Series for range [1.13;1.79] around x0
		// Take extra care to compute the difference to x0 with a high-
		// precision version of x0
		return gnm_digamma_series_2 (x, (x - x0a) - x0b);

	if (x < x0a + 1)
		// Series for range [1.79;2.46]
		return gnm_digamma_series_3 (x);

	if (x < 20) {
		// Step down to series 2 or 3.  All terms are positive so no
		// cancellation.
		gnm_float sum = 0;
		while (x > x0a + 1) {
			x--;
			sum += 1 / x;
		}
		return sum + gnm_digamma (x);
	}

	return gnm_digamma_asymp (x);
}

/* ------------------------------------------------------------------------- */
