#include <gnumeric-config.h>
#include <sf-gamma.h>
#include <sf-trig.h>
#include <mathfunc.h>

#define ML_ERR_return_NAN { return gnm_nan; }
#define ML_UNDERFLOW (GNM_EPSILON * GNM_EPSILON)
#define ML_ERROR(cause) do { } while(0)

static int qgammaf (gnm_float x, GnmQuad *mant, int *exp2);


/* Compute  gnm_log(gamma(a+1))  accurately also for small a (0 < a < 0.5). */
gnm_float lgamma1p (gnm_float a)
{
    const gnm_float eulers_const =	 GNM_const(0.5772156649015328606065120900824024);

    /* coeffs[i] holds (zeta(i+2)-1)/(i+2) , i = 1:N, N = 40 : */
    const int N = 40;
    static const gnm_float coeffs[40] = {
	GNM_const(0.3224670334241132182362075833230126e-0),
	GNM_const(0.6735230105319809513324605383715000e-1),
	GNM_const(0.2058080842778454787900092413529198e-1),
	GNM_const(0.7385551028673985266273097291406834e-2),
	GNM_const(0.2890510330741523285752988298486755e-2),
	GNM_const(0.1192753911703260977113935692828109e-2),
	GNM_const(0.5096695247430424223356548135815582e-3),
	GNM_const(0.2231547584535793797614188036013401e-3),
	GNM_const(0.9945751278180853371459589003190170e-4),
	GNM_const(0.4492623673813314170020750240635786e-4),
	GNM_const(0.2050721277567069155316650397830591e-4),
	GNM_const(0.9439488275268395903987425104415055e-5),
	GNM_const(0.4374866789907487804181793223952411e-5),
	GNM_const(0.2039215753801366236781900709670839e-5),
	GNM_const(0.9551412130407419832857179772951265e-6),
	GNM_const(0.4492469198764566043294290331193655e-6),
	GNM_const(0.2120718480555466586923135901077628e-6),
	GNM_const(0.1004322482396809960872083050053344e-6),
	GNM_const(0.4769810169363980565760193417246730e-7),
	GNM_const(0.2271109460894316491031998116062124e-7),
	GNM_const(0.1083865921489695409107491757968159e-7),
	GNM_const(0.5183475041970046655121248647057669e-8),
	GNM_const(0.2483674543802478317185008663991718e-8),
	GNM_const(0.1192140140586091207442548202774640e-8),
	GNM_const(0.5731367241678862013330194857961011e-9),
	GNM_const(0.2759522885124233145178149692816341e-9),
	GNM_const(0.1330476437424448948149715720858008e-9),
	GNM_const(0.6422964563838100022082448087644648e-10),
	GNM_const(0.3104424774732227276239215783404066e-10),
	GNM_const(0.1502138408075414217093301048780668e-10),
	GNM_const(0.7275974480239079662504549924814047e-11),
	GNM_const(0.3527742476575915083615072228655483e-11),
	GNM_const(0.1711991790559617908601084114443031e-11),
	GNM_const(0.8315385841420284819798357793954418e-12),
	GNM_const(0.4042200525289440065536008957032895e-12),
	GNM_const(0.1966475631096616490411045679010286e-12),
	GNM_const(0.9573630387838555763782200936508615e-13),
	GNM_const(0.4664076026428374224576492565974577e-13),
	GNM_const(0.2273736960065972320633279596737272e-13),
	GNM_const(0.1109139947083452201658320007192334e-13)
    };

    const gnm_float c = GNM_const(0.2273736845824652515226821577978691e-12);/* zeta(N+2)-1 */
    gnm_float lgam;
    int i;

    if (gnm_abs (a) >= 0.5)
	return gnm_lgamma (a + 1);

    /* Abramowitz & Stegun 6.1.33,
     * also  http://functions.wolfram.com/06.11.06.0008.01 */
    lgam = c * gnm_logcf (-a / 2, N + 2, 1);
    for (i = N - 1; i >= 0; i--)
	lgam = coeffs[i] - a * lgam;

    return (a * lgam - eulers_const) * a - log1pmx (a);
} /* lgamma1p */

/* ------------------------------------------------------------------------ */

/* Imported src/nmath/stirlerr.c from R.  */
/*
 *  AUTHOR
 *    Catherine Loader, catherine@research.bell-labs.com.
 *    October 23, 2000.
 *
 *  Merge in to R:
 *	Copyright (C) 2000, The R Core Development Team
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
 *  USA.
 *
 *
 *  DESCRIPTION
 *
 *    Computes the log of the error term in Stirling's formula.
 *      For n > 15, uses the series 1/12n - 1/360n^3 + ...
 *      For n <=15, integers or half-integers, uses stored values.
 *      For other n < 15, uses lgamma directly (don't use this to
 *        write lgamma!)
 *
 * Merge in to R:
 * Copyright (C) 2000, The R Core Development Team
 * R has lgammafn, and lgamma is not part of ISO C
 */


/* stirlerr(n) = gnm_log(n!) - gnm_log( gnm_sqrt(2*pi*n)*(n/e)^n )
 *             = gnm_log Gamma(n+1) - 1/2 * [gnm_log(2*pi) + gnm_log(n)] - n*[gnm_log(n) - 1]
 *             = gnm_log Gamma(n+1) - (n + 1/2) * gnm_log(n) + n - gnm_log(2*pi)/2
 *
 * see also lgammacor() in ./lgammacor.c  which computes almost the same!
 */

gnm_float stirlerr(gnm_float n)
{

#define S0 GNM_const(0.083333333333333333333)       /* 1/12 */
#define S1 GNM_const(0.00277777777777777777778)     /* 1/360 */
#define S2 GNM_const(0.00079365079365079365079365)  /* 1/1260 */
#define S3 GNM_const(0.000595238095238095238095238) /* 1/1680 */
#define S4 GNM_const(0.0008417508417508417508417508)/* 1/1188 */

/*
  error for 0, 0.5, 1.0, 1.5, ..., 14.5, 15.0.
*/
    static const gnm_float sferr_halves[31] = {
	0.0, /* n=0 - wrong, place holder only */
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
    gnm_float nn;

    if (n <= 15.0) {
	nn = n + n;
	if (nn == (int)nn) return(sferr_halves[(int)nn]);
	return(lgamma1p (n ) - (n + 0.5)*gnm_log(n) + n - M_LN_SQRT_2PI);
    }

    nn = n*n;
    if (n>500) return((S0-S1/nn)/n);
    if (n> 80) return((S0-(S1-S2/nn)/nn)/n);
    if (n> 35) return((S0-(S1-(S2-S3/nn)/nn)/nn)/n);
    /* 15 < n <= 35 : */
    return((S0-(S1-(S2-(S3-S4/nn)/nn)/nn)/nn)/n);
}
/* Cleaning up done by tools/import-R:  */
#undef S0
#undef S1
#undef S2
#undef S3
#undef S4

/* ------------------------------------------------------------------------ */
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 *  02110-1301 USA.
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

    if (n < 1 || n > 1000) ML_ERR_return_NAN;

    if (x < -1.1 || x > 1.1) ML_ERR_return_NAN;

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

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/lgammacor.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000-2001 The R Development Core Team
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
 *  USA.
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

#ifdef NOMORE_FOR_THREADS
    static int nalgm = 0;
    static gnm_float xbig = 0, xmax = 0;

    /* Initialize machine dependent constants, the first time gamma() is called.
	FIXME for threads ! */
    if (nalgm == 0) {
	/* For IEEE gnm_float precision : nalgm = 5 */
	nalgm = chebyshev_init(algmcs, 15, GNM_EPSILON/2);/*was d1mach(3)*/
	xbig = 1 / gnm_sqrt(GNM_EPSILON/2); /* ~ 94906265.6 for IEEE gnm_float */
	xmax = gnm_exp(fmin2(gnm_log(GNM_MAX / 12), -gnm_log(12 * GNM_MIN)));
	/*   = GNM_MAX / 48 ~= 3.745e306 for IEEE gnm_float */
    }
#else
/* For IEEE gnm_float precision GNM_EPSILON = 2^-52 = GNM_const(2.220446049250313e-16) :
 *   xbig = 2 ^ 26.5
 *   xmax = GNM_MAX / 48 =  2^1020 / 3 */
# define nalgm 5
# define xbig  GNM_const(94906265.62425156)
# define xmax  GNM_const(3.745194030963158e306)
#endif

    if (x < 10)
	ML_ERR_return_NAN
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
/* Cleaning up done by tools/import-R:  */
#undef nalgm
#undef xbig
#undef xmax

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/lbeta.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
 *  USA.
 *
 *  SYNOPSIS
 *
 *    #include <Rmath.h>
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


gnm_float gnm_lbeta(gnm_float a, gnm_float b)
{
    gnm_float corr, p, q;

    p = q = a;
    if(b < p) p = b;/* := min(a,b) */
    if(b > q) q = b;/* := max(a,b) */

#ifdef IEEE_754
    if(gnm_isnan(a) || gnm_isnan(b))
	return a + b;
#endif

    /* both arguments must be >= 0 */

    if (p < 0)
	ML_ERR_return_NAN
    else if (p == 0) {
	return gnm_pinf;
    }
    else if (!gnm_finite(q)) {
	return gnm_ninf;
    }

    if (p >= 10) {
	/* p and q are big. */
	corr = lgammacor(p) + lgammacor(q) - lgammacor(p + q);
	return gnm_log(q) * -0.5 + M_LN_SQRT_2PI + corr
		+ (p - 0.5) * gnm_log(p / (p + q)) + q * gnm_log1p(-p / (p + q));
    }
    else if (q >= 10) {
	/* p is small, but q is big. */
	corr = lgammacor(q) - lgammacor(p + q);
	return gnm_lgamma(p) + corr + p - p * gnm_log(p + q)
		+ (q - 0.5) * gnm_log1p(-p / (p + q));
    }
    else
	/* p and q are small: p <= q < 10. */
	return gnm_lgamma (p) + gnm_lgamma (q) - gnm_lgamma (p + q);
}

/* ------------------------------------------------------------------------ */

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
	GnmQuad r;
	int e;

	switch (qgammaf (x, &r, &e)) {
	case 0: return ldexp (gnm_quad_value (&r), e);
	case 1: return gnm_pinf;
	default: return gnm_nan;
	}
}

/* ------------------------------------------------------------------------- */

/**
 * gnm_fact:
 * @x: number
 *
 * Returns: the factorial of @x, which must not be a negative integer.
 */
gnm_float
gnm_fact (gnm_float x)
{
	GnmQuad r;
	int e;

	switch (qfactf (x, &r, &e)) {
	case 0: return ldexp (gnm_quad_value (&r), e);
	case 1: return gnm_pinf;
	default: return gnm_nan;
	}
}

/* ------------------------------------------------------------------------- */

/* 0: ok, 1: overflow, 2: nan */
static int
qbetaf (gnm_float a, gnm_float b, GnmQuad *mant, int *exp2)
{
	GnmQuad ma, mb, mab;
	int ea, eb, eab;
	gnm_float ab = a + b;

	if (gnm_isnan (ab) ||
	    (a <= 0 && a == gnm_floor (a)) ||
	    (b <= 0 && b == gnm_floor (b)) ||
	    (ab <= 0 && ab == gnm_floor (ab)))
		return 2;

	if (!qgammaf (a, &ma, &ea) &&
	    !qgammaf (b, &mb, &eb) &&
	    !qgammaf (ab, &mab, &eab)) {
		void *state = gnm_quad_start ();
		gnm_quad_mul (&ma, &ma, &mb);
		gnm_quad_div (mant, &ma, &mab);
		gnm_quad_end (state);
		*exp2 = ea + eb - eab;
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
	case 0: return ldexp (gnm_quad_value (&r), e);
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
		return gnm_log (gnm_abs (m)) + e * M_LN2gnum;
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
 * x should be >20 and the result is, roughly, 1+1/(12x).
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
	GnmQuad zn;
	int i;

	gnm_quad_init (&zn, 1);
	*res = zn;

	for (i = 0; i < (int)G_N_ELEMENTS (num); i++) {
		GnmQuad t, c;
		gnm_quad_mul (&zn, &zn, x);
		gnm_quad_init (&c, den[i]);
		gnm_quad_mul (&t, &zn, &c);
		gnm_quad_init (&c, num[i]);
		gnm_quad_div (&t, &c, &t);
		gnm_quad_add (res, res, &t);
	}
}

/* ------------------------------------------------------------------------- */

static void
pochhammer_small_n (gnm_float x, gnm_float n, GnmQuad *res)
{
	GnmQuad qx, qn, qr, qs, f1, f2, f3, f4, f5;
	gnm_float r;
	gboolean debug = FALSE;

	g_return_if_fail (x >= 20);
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



/*
 * Pochhammer's symbol: (x)_n = Gamma(x+n)/Gamma(x).
 *
 * While n is often an integer, that is not a requirement.
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

		return gnm_ldexp (r, de);
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
	lr = ((x - 0.5) * gnm_log1p (n / x) +
	      n * gnm_log (x + n) -
	      n +
	      (lgammacor (x + n) - lgammacor (x)));
	return gnm_exp (lr);
}

/* ------------------------------------------------------------------------- */

static void
rescale_mant_exp (GnmQuad *mant, int *exp2)
{
	GnmQuad s;
	int e;

	(void)gnm_frexp (gnm_quad_value (mant), &e);
	*exp2 += e;
	gnm_quad_init (&s, gnm_ldexp (1.0, -e));
	gnm_quad_mul (mant, mant, &s);
}

/* Tabulate up to, but not including, this number.  */
#define QFACTI_LIMIT 10000

static gboolean
qfacti (int n, GnmQuad *mant, int *exp2)
{
	static GnmQuad mants[QFACTI_LIMIT];
	static int exp2s[QFACTI_LIMIT];
	static int init = 0;

	if (n < 0 || n >= QFACTI_LIMIT) {
		*mant = gnm_quad_zero;
		*exp2 = 0;
		return TRUE;
	}

	if (n >= init) {
		void *state = gnm_quad_start ();

		if (init == 0) {
			gnm_quad_init (&mants[0], 0.5);
			exp2s[0] = 1;
			init++;
		}

		while (n >= init) {
			GnmQuad m;

			gnm_quad_init (&m, init);
			gnm_quad_mul (&mants[init], &m, &mants[init - 1]);
			exp2s[init] = exp2s[init - 1];
			rescale_mant_exp (&mants[init], &exp2s[init]);

			init++;
		}

		gnm_quad_end (state);
	}

	*mant = mants[n];
	*exp2 = exp2s[n];
	return FALSE;
}

/* 0: ok, 1: overflow, 2: nan */
int
qfactf (gnm_float x, GnmQuad *mant, int *exp2)
{
	void *state;
	gboolean res = 0;

	if (gnm_isnan (x))
		return 2;

	if (x >= G_MAXINT / 2)
		return 1;

	if (x == gnm_floor (x)) {
		/* Integer or infinite.  */
		if (x < 0)
			return 2;

		if (!qfacti ((int)x, mant, exp2))
			return 0;
	}

	state = gnm_quad_start ();

	if (x < -1) {
		if (qfactf (-x - 1, mant, exp2))
			res = 1;
		else {
			GnmQuad b;

			gnm_quad_init (&b, -x);
			gnm_quad_sinpi (&b, &b);
			gnm_quad_mul (&b, &b, mant);
			gnm_quad_div (mant, &gnm_quad_pi, &b);
			*exp2 = -*exp2;
		}
	} else if (x >= QFACTI_LIMIT - 0.5) {
		/*
		 * Let y = x + 1 = m * 2^e; c = sqrt(2Pi).
		 *
		 * G(y) = c * y^(y-1/2) * exp(-y) * E(y)
		 *      = c * (y/e)^y / sqrt(y) * E(y)
		 */
		GnmQuad y, f1, f2, f3, f4;
		gnm_float ef2;
		gboolean debug = FALSE;

		if (debug) g_printerr ("x=%.20g\n", x);

		gnm_quad_init (&y, x + 1);
		*exp2 = 0;

		/* sqrt(2Pi) */
		gnm_quad_sqrt (&f1, &gnm_quad_2pi);
		if (debug) g_printerr ("f1=%.20g\n", gnm_quad_value (&f1));

		/* (y/e)^y */
		gnm_quad_div (&f2, &y, &gnm_quad_e);
		gnm_quad_pow (&f2, &ef2, &f2, &y);
		if (ef2 > G_MAXINT || ef2 < G_MININT)
			res = 1;
		else
			*exp2 += (int)ef2;
		if (debug) g_printerr ("f2=%.20g\n", gnm_quad_value (&f2));

		/* sqrt(y) */
		gnm_quad_sqrt (&f3, &y);
		if (debug) g_printerr ("f3=%.20g\n", gnm_quad_value (&f3));

		/* E(x) */
		gamma_error_factor (&f4, &y);
		if (debug) g_printerr ("f4=%.20g\n", gnm_quad_value (&f4));

		gnm_quad_mul (mant, &f1, &f2);
		gnm_quad_div (mant, mant, &f3);
		gnm_quad_mul (mant, mant, &f4);

		if (debug) g_printerr ("G(x+1)=%.20g * 2^%d %s\n", gnm_quad_value (mant), *exp2, res ? "overflow" : "");
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
		w = gnm_floor (x + 0.5);
		f = x - w;
		gnm_quad_init (&qx, x);

		gnm_quad_init (&s, 1);
		while (w < 20) {
			gnm_quad_add (&qx, &qx, &gnm_quad_one);
			w++;
			gnm_quad_mul (&s, &s, &qx);
		}

		if (qfacti ((int)w, &mFw, &eFw)) {
			res = 1;
		} else {
			GnmQuad r;

			pochhammer_small_n (w + 1, f, &r);
			gnm_quad_mul (mant, &mFw, &r);
			gnm_quad_div (mant, mant, &s);
			*exp2 = eFw;
		}
	}

	if (res == 0)
		rescale_mant_exp (mant, exp2);

	gnm_quad_end (state);
	return res;
}

/* 0: ok, 1: overflow, 2: nan */
static int
qgammaf (gnm_float x, GnmQuad *mant, int *exp2)
{
	if (x < -1.5 || x > 0.5)
		return qfactf (x - 1, mant, exp2);
	else if (gnm_isnan (x) || x == 0)
		return 2;
	else {
		void *state = gnm_quad_start ();
		GnmQuad qx;

		qfactf (x, mant, exp2);
		gnm_quad_init (&qx, x);
		gnm_quad_div (mant, mant, &qx);
		rescale_mant_exp (mant, exp2);
		gnm_quad_end (state);
		return 0;
	}
}

/* ------------------------------------------------------------------------- */

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
		c = gnm_ldexp (gnm_quad_value (&m1), e1 - e2 - e3);
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
 * implemetation will suffer significant cancellation errors.  The error
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

void
complex_gamma (complex_t *dst, complex_t const *src)
{
	if (complex_real_p (src)) {
		complex_init (dst, gnm_gamma (src->re), 0);
	} else if (src->re < 0) {
		/* Gamma(z) = pi / (sin(pi*z) * Gamma(-z+1)) */
		complex_t a, b, mz;

		complex_init (&mz, -src->re, -src->im);
		complex_fact (&a, &mz);

		complex_init (&b,
			      M_PIgnum * gnm_fmod (src->re, 2),
			      M_PIgnum * src->im);
		/* Hmm... sin overflows when b.im is large.  */
		complex_sin (&b, &b);

		complex_mul (&a, &a, &b);

		complex_init (&b, M_PIgnum, 0);

		complex_div (dst, &b, &a);
	} else {
		complex_t zmh, zmhd2, zmhpg, f, f2, p, q, pq;
		int i;

		i = G_N_ELEMENTS(lanczos_num) - 1;
		complex_init (&p, lanczos_num[i], 0);
		complex_init (&q, lanczos_denom[i], 0);
		while (--i >= 0) {
			complex_mul (&p, &p, src);
			p.re += lanczos_num[i];
			complex_mul (&q, &q, src);
			q.re += lanczos_denom[i];
		}
		complex_div (&pq, &p, &q);

		complex_init (&zmh, src->re - 0.5, src->im);
		complex_init (&zmhpg, zmh.re + lanczos_g, zmh.im);
		complex_init (&zmhd2, zmh.re * 0.5, zmh.im * 0.5);
		complex_pow (&f, &zmhpg, &zmhd2);

		zmh.re = -zmh.re; zmh.im = -zmh.im;
		complex_exp (&f2, &zmh);
		complex_mul (&f2, &f, &f2);
		complex_mul (&f2, &f2, &f);

		complex_mul (dst, &f2, &pq);
	}
}

/* ------------------------------------------------------------------------- */

void
complex_fact (complex_t *dst, complex_t const *src)
{
	if (complex_real_p (src)) {
		complex_init (dst, gnm_fact (src->re), 0);
	} else {
		/*
		 * This formula is valid for all arguments except zero
		 * which we conveniently handled above.
		 */
		complex_t gz;
		complex_gamma (&gz, src);
		complex_mul (dst, &gz, src);
	}
}

/* ------------------------------------------------------------------------- */

static void
igamma_cf (complex_t *dst, const complex_t *a, const complex_t *z)
{
	complex_t A0, A1, B0, B1;
	int i;
	const gboolean debug_cf = FALSE;

	complex_init (&A0, 1, 0);
	complex_init (&A1, 0, 0);
	complex_init (&B0, 0, 0);
	complex_init (&B1, 1, 0);

	for (i = 1; i < 100; i++) {
		complex_t ai, bi, t1, t2, c1, c2, A2, B2;
		gnm_float m;
		const gnm_float BIG = GNM_const(18446744073709551616.0);

		if (i == 1)
			complex_init (&ai, 1, 0);
		else if (i & 1) {
			gnm_float f = (i >> 1);
			complex_init (&ai, z->re * f, z->im * f);
		} else {
			complex_t f;
			complex_init (&f, -(a->re + ((i >> 1) - 1)), -a->im);
			complex_mul (&ai, &f, z);
		}
		complex_init (&bi, a->re + (i - 1), a->im);

		/* Update A. */
		complex_mul (&t1, &bi, &A1);
		complex_mul (&t2, &ai, &A0);
		complex_add (&A2, &t1, &t2);
		A0 = A1; A1 = A2;

		/* Update B. */
		complex_mul (&t1, &bi, &B1);
		complex_mul (&t2, &ai, &B0);
		complex_add (&B2, &t1, &t2);
		B0 = B1; B1 = B2;

		/* Rescale */
		m = gnm_abs (B1.re) + gnm_abs (B1.im);
		if (m >= BIG || m <= 1 / BIG) {
			int e;
			gnm_float s;
			(void)frexp (m, &e);
			s = ldexp (1, -e);
			A0.re *= s; A0.im *= s;
			A1.re *= s; A1.im *= s;
			B0.re *= s; B0.im *= s;
			B1.re *= s; B1.im *= s;
			if (debug_cf)
				g_printerr ("rescale\n");
		}

		/* Check for convergence */
		complex_mul (&t1, &A1, &B0);
		complex_mul (&t2, &A0, &B1);
		complex_sub (&c1, &t1, &t2);

		complex_mul (&c2, &B0, &B1);

		complex_div (&t1, &A1, &B1);
		if (debug_cf) {
			g_printerr ("  a : %.20g + %.20g I\n", ai.re, ai.im);
			g_printerr ("  b : %.20g + %.20g I\n", bi.re, bi.im);
			g_printerr ("  A : %.20g + %.20g I\n", A1.re, A1.im);
			g_printerr ("  B : %.20g + %.20g I\n", B1.re, B1.im);
			g_printerr ("%3d : %.20g + %.20g I\n", i, t1.re, t1.im);
		}

		if (complex_mod (&c1) <= complex_mod (&c2) * (16 * GNM_EPSILON))
			break;
	}

	if (i == 100) {
		/* Make the failure obvious. */
		dst->re = dst->im = gnm_nan;
		g_printerr ("igamma_cf not converged\n");
		return;
	}

	complex_div (dst, &A1, &B1);
}


void
complex_igamma (complex_t *dst, const complex_t *a, const complex_t *z,
		gboolean lower, gboolean regularized)
{
	complex_t res, f, mz;

	if (complex_zero_p (a)) {
		if (!lower && !regularized)
			complex_gamma (dst, z);
		else
			complex_init (dst, lower ? 0 : 1, 0);
		return;
	}

	if (complex_real_p (a) && a->re >= 0 &&
	    complex_real_p (z) && z->re >= 0) {
		complex_init (&res, pgamma (z->re, a->re, 1, lower, FALSE), 0);
		if (!regularized) {
			complex_t g;
			complex_gamma (&g, a);
			complex_mul (&res, &res, &g);
		}
		*dst = res;
		return;
	}

	igamma_cf (&res, a, z);

	/*
	 * FIXME: The following three blocks should be handled without
	 * creating big numbers.
	 */

       	mz.re = -z->re, mz.im = -z->im;
	complex_exp (&f, &mz);
	complex_mul (&res, &res, &f);
	complex_pow (&f, z, a);
	complex_mul (&res, &res, &f);

	if (!regularized && lower) {
		/* Nothing */
	} else {
		complex_t g;
		complex_gamma (&g, a);

		if (regularized) {
			complex_div (&res, &res, &g);
			if (!lower)
				res.re = 1 - res.re;
		} else {
			/* !lower here */
			complex_sub (&res, &g, &res);
		}
	}

	*dst = res;
}

/* ------------------------------------------------------------------------- */
