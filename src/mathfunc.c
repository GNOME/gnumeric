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

#define M_LN_SQRT_2PI   GNUM_const(0.918938533204672741780329736406)  /* log(sqrt(2*pi)) */
#define M_SQRT_32       GNUM_const(5.656854249492380195206754896838)  /* sqrt(32) */
#define M_1_SQRT_2PI    GNUM_const(0.398942280401432677939946059934)  /* 1/sqrt(2pi) */
#define M_SQRT_2dPI     GNUM_const(0.797884560802865355879892119869)  /* sqrt(2/pi) */
#define M_2PIgnum       (2 * M_PIgnum)

/* Any better idea for a quick hack?  */
#define ML_NAN (-HUGE_VAL * 0.0)
#define ML_NEGINF (-HUGE_VAL)
#define ML_POSINF (HUGE_VAL)
#define ML_UNDERFLOW (GNUM_EPSILON * GNUM_EPSILON)
#define ML_VALID(_x) (!isnangnum (_x))
#define ML_ERROR(cause) /* Nothing */
#define MATHLIB_ERROR g_error
#define MATHLIB_WARNING g_warning
#define MATHLIB_WARNING2 g_warning
#define MATHLIB_WARNING4 g_warning
#define fmin2(_x,_y) MIN(_x, _y)
#define imin2(_x,_y) MIN(_x, _y)
#define fmax2(_x,_y) MAX(_x, _y)
#define imax2(_x,_y) MAX(_x, _y)

#define lgammafn(_x) lgamma (_x)
#define gammafn(_x) expgnum (lgammafn (_x))
#define gamma_cody(_x) gammafn (_x)

#define MATHLIB_STANDALONE
#define ML_ERR_return_NAN { return ML_NAN; }
static void pnorm_both(gnum_float x, gnum_float *cum, gnum_float *ccum, int i_tail, gboolean log_p);

/* MW ---------------------------------------------------------------------- */

/*
 * In preparation for truncation, make the value a tiny bit larger (seen
 * absolutely).  This makes ROUND (etc.) behave a little closer to what
 * people want, even if it is a bit bogus.
 */
gnum_float
gnumeric_add_epsilon (gnum_float x)
{
  if (!finitegnum (x) || x == 0)
    return x;
  else {
    int exp;
    gnum_float mant = frexpgnum (gnumabs (x), &exp);
    gnum_float absres = ldexpgnum (mant + GNUM_EPSILON, exp);
    return (x < 0) ? -absres : absres;
  }
}

gnum_float
gnumeric_sub_epsilon (gnum_float x)
{
  if (!finitegnum (x) || x == 0)
    return x;
  else {
    int exp;
    gnum_float mant = frexpgnum (gnumabs (x), &exp);
    gnum_float absres = ldexpgnum (mant - GNUM_EPSILON, exp);
    return (x < 0) ? -absres : absres;
  }
}

gnum_float
gnumeric_fake_floor (gnum_float x)
{
  return floorgnum (gnumeric_add_epsilon (x));
}

gnum_float
gnumeric_fake_ceil (gnum_float x)
{
  return ceilgnum (gnumeric_sub_epsilon (x));
}

gnum_float
gnumeric_fake_round (gnum_float x)
{
  return (x >= 0)
    ? gnumeric_fake_floor (x + 0.5)
    : -gnumeric_fake_floor (-x + 0.5);
}

gnum_float
gnumeric_fake_trunc (gnum_float x)
{
  return (x >= 0)
    ? gnumeric_fake_floor (x)
    : gnumeric_fake_ceil (x);
}

/* When R 1.5 comes out, their pweibull should be ok.  */
gnum_float
pweibull (gnum_float x, gnum_float shape, gnum_float scale)
{
    if (shape <= 0 || scale <= 0)
	    return ML_NAN;
    else if (x <= 0)
	    return 0;
    else
	    return -expm1gnum (-powgnum (x / scale, shape));
}

/* When R 1.5 comes out, their pexp should be ok.  */
gnum_float
pexp (gnum_float x, gnum_float scale)
{
	if (scale <= 0)
		return ML_NAN;
	else if (x <= 0)
		return 0;
	else
		return -expm1gnum (-x / scale);
}

/* ------------------------------------------------------------------------- */
/* --- BEGIN MAGIC R SOURCE MARKER --- */

/* The following source code was imported from the R project.  */
/* It was automatically transformed by tools/import-R.  */

/* Imported src/nmath/dpq.h from R.  */
	/* Utilities for `dpq' handling (density/probability/quantile) */

/* give_log in "d";  log_p in "p" & "q" : */
#define give_log log_p
							/* "DEFAULT" */
							/* --------- */
#define R_D__0	(log_p ? ML_NEGINF : 0.)		/* 0 */
#define R_D__1	(log_p ? 0. : 1.)			/* 1 */
#define R_DT_0	(lower_tail ? R_D__0 : R_D__1)		/* 0 */
#define R_DT_1	(lower_tail ? R_D__1 : R_D__0)		/* 1 */

#define R_D_Lval(p)	(lower_tail ? (p) : (1 - (p)))	/*  p  */
#define R_D_Cval(p)	(lower_tail ? (1 - (p)) : (p))	/*  1 - p */

#define R_D_val(x)	(log_p	? loggnum(x) : (x))		/*  x  in pF(x,..) */
#define R_D_qIv(p)	(log_p	? expgnum(p) : (p))		/*  p  in qF(p,..) */
#define R_D_exp(x)	(log_p	?  (x)	 : expgnum(x))	/* expgnum(x) */
#define R_D_log(p)	(log_p	?  (p)	 : loggnum(p))	/* loggnum(p) */

#define R_DT_val(x)	R_D_val(R_D_Lval(x))		/*  x  in pF */
#define R_DT_Cval(x)	R_D_val(R_D_Cval(x))		/*  1 - x */
#define R_DT_qIv(p)	R_D_Lval(R_D_qIv(p))		/*  p  in qF ! */
#define R_DT_CIv(p)	R_D_Cval(R_D_qIv(p))		/*  1 - p in qF */

#define R_DT_exp(x)	R_D_exp(R_D_Lval(x))		/* expgnum(x) */
#define R_DT_Cexp(x)	R_D_exp(R_D_Cval(x))		/* expgnum(1 - x) */

#define R_DT_log(p)	(lower_tail ? R_D_log(p) :		\
			 log1pgnum(- (log_p ? expgnum(p) : p)))/* loggnum(p)	in qF */

#define R_DT_Clog(p)	(lower_tail ?				\
			 log1pgnum(- (log_p ? expgnum(p) : p)) :	\
			 R_D_log(p))			/* loggnum(1 - p)	in qF */

#define R_Q_P01_check(p)			\
    if ((log_p	&& p > 0) ||			\
	(!log_p && (p < 0 || p > 1)) )		\
	ML_ERR_return_NAN


/* additions for density functions (C.Loader) */
#define R_D_fexp(f,x)     (give_log ? -0.5*loggnum(f)+(x) : expgnum(x)/sqrtgnum(f))
#define R_D_forceint(x)   floorgnum((x) + 0.5)
#define R_D_nonint(x) 	  (gnumabs((x) - floorgnum((x)+0.5)) > 1e-7)
#define R_D_notnnegint(x) (x < 0. || R_D_nonint(x))

#define R_D_nonint_check(x) 				\
   if(R_D_nonint(x)) {					\
	MATHLIB_WARNING("non-integer x = %" GNUM_FORMAT_f "", x);	\
	return R_D__0;					\
   }

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dnorm.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  SYNOPSIS
 *
 *	double dnorm4(double x, double mu, double sigma, int give_log)
 *	      {dnorm (..) is synonymous and preferred inside R}
 *
 *  DESCRIPTION
 *
 *	Compute the density of the normal distribution.
 */


gnum_float dnorm(gnum_float x, gnum_float mu, gnum_float sigma, gboolean give_log)
{
#ifdef IEEE_754
    if (isnangnum(x) || isnangnum(mu) || isnangnum(sigma))
	return x + mu + sigma;
#endif
    if (sigma <= 0) ML_ERR_return_NAN;

    x = (x - mu) / sigma;

    return (give_log ?
	    -(M_LN_SQRT_2PI  +	0.5 * x * x + loggnum(sigma)) :
	    M_1_SQRT_2PI * expgnum(-0.5 * x * x)  /	  sigma);
    /* M_1_SQRT_2PI = 1 / sqrtgnum(2 * pi) */
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pnorm.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  SYNOPSIS
 *
 *   #include "Rmath.h"
 *
 *   double pnorm5(double x, double mu, double sigma, int lower_tail,int log_p);
 *	   {pnorm (..) is synonymous and preferred inside R}
 *
 *   void   pnorm_both(double x, double *cum, double *ccum,
 *		       int i_tail, int log_p);
 *
 *  DESCRIPTION
 *
 *	The main computation evaluates near-minimax approximations derived
 *	from those in "Rational Chebyshev approximations for the error
 *	function" by W. J. Cody, Math. Comp., 1969, 631-637.  This
 *	transportable program uses rational functions that theoretically
 *	approximate the normal distribution function to at least 18
 *	significant decimal digits.  The accuracy achieved depends on the
 *	arithmetic system, the compiler, the intrinsic functions, and
 *	proper selection of the machine-dependent constants.
 *
 *  REFERENCE
 *
 *	Cody, W. D. (1993).
 *	ALGORITHM 715: SPECFUN - A Portable FORTRAN Package of
 *	Special Function Routines and Test Drivers".
 *	ACM Transactions on Mathematical Software. 19, 22-32.
 */

gnum_float pnorm(gnum_float x, gnum_float mu, gnum_float sigma, gboolean lower_tail, gboolean log_p)
{
    gnum_float p, cp;

    /* Note: The structure of these checks has been carefully thought through.
     * For example, if x == mu and sigma == 0, we still get the correct answer.
     */
#ifdef IEEE_754
    if(isnangnum(x) || isnangnum(mu) || isnangnum(sigma))
	return x + mu + sigma;
#endif
    if (sigma < 0) ML_ERR_return_NAN;

    x = (x - mu) / sigma;
    if(!finitegnum(x)) {
	if(isnangnum(x)) /* e.g. x=mu=Inf */ return(ML_NAN);
	if(x < 0) return R_DT_0;
	else return R_DT_1;
    }

    pnorm_both(x, &p, &cp, (lower_tail ? 0 : 1), log_p);

    return(lower_tail ? p : cp);
}

#define SIXTEN	16 /* Cutoff allowing exact "*" and "/" */

void pnorm_both(gnum_float x, gnum_float *cum, gnum_float *ccum, int i_tail, gboolean log_p)
{
/* i_tail in {0,1,2} means: "lower", "upper", or "both" :
   if(lower) return  *cum := P[X <= x]
   if(upper) return *ccum := P[X >  x] = 1 - P[X <= x]
*/
    const gnum_float a[5] = {
	GNUM_const(2.2352520354606839287),
	GNUM_const(161.02823106855587881),
	GNUM_const(1067.6894854603709582),
	GNUM_const(18154.981253343561249),
	GNUM_const(0.065682337918207449113)
    };
    const gnum_float b[4] = {
	GNUM_const(47.20258190468824187),
	GNUM_const(976.09855173777669322),
	GNUM_const(10260.932208618978205),
	GNUM_const(45507.789335026729956)
    };
    const gnum_float c[9] = {
	GNUM_const(0.39894151208813466764),
	GNUM_const(8.8831497943883759412),
	GNUM_const(93.506656132177855979),
	GNUM_const(597.27027639480026226),
	GNUM_const(2494.5375852903726711),
	GNUM_const(6848.1904505362823326),
	GNUM_const(11602.651437647350124),
	GNUM_const(9842.7148383839780218),
	GNUM_const(1.0765576773720192317e-8)
    };
    const gnum_float d[8] = {
	GNUM_const(22.266688044328115691),
	GNUM_const(235.38790178262499861),
	GNUM_const(1519.377599407554805),
	GNUM_const(6485.558298266760755),
	GNUM_const(18615.571640885098091),
	GNUM_const(34900.952721145977266),
	GNUM_const(38912.003286093271411),
	GNUM_const(19685.429676859990727)
    };
    const gnum_float p[6] = {
	GNUM_const(0.21589853405795699),
	GNUM_const(0.1274011611602473639),
	GNUM_const(0.022235277870649807),
	GNUM_const(0.001421619193227893466),
	GNUM_const(2.9112874951168792e-5),
	GNUM_const(0.02307344176494017303)
    };
    const gnum_float q[5] = {
	GNUM_const(1.28426009614491121),
	GNUM_const(0.468238212480865118),
	GNUM_const(0.0659881378689285515),
	GNUM_const(0.00378239633202758244),
	GNUM_const(7.29751555083966205e-5)
    };

    gnum_float xden, xnum, temp, del, eps, xsq, y;
#ifdef NO_DENORMS
    gnum_float min = GNUM_MIN;
#endif
    int i, lower, upper;

#ifdef IEEE_754
    if(isnangnum(x)) { *cum = *ccum = x; return; }
#endif

    /* Consider changing these : */
    eps = GNUM_EPSILON * 0.5;

    /* i_tail in {0,1,2} =^= {lower, upper, both} */
    lower = i_tail != 1;
    upper = i_tail != 0;

    y = gnumabs(x);
    if (y <= 0.67448975) { /* qnorm(3/4) = .6744.... -- earlier had 0.66291 */
	if (y > eps) {
	    xsq = x * x;
	    xnum = a[4] * xsq;
	    xden = xsq;
	    for (i = 0; i < 3; ++i) {
		xnum = (xnum + a[i]) * xsq;
		xden = (xden + b[i]) * xsq;
	    }
	} else xnum = xden = 0.0;

	temp = x * (xnum + a[3]) / (xden + b[3]);
	if(lower)  *cum = 0.5 + temp;
	if(upper) *ccum = 0.5 - temp;
	if(log_p) {
	    if(lower)  *cum = loggnum(*cum);
	    if(upper) *ccum = loggnum(*ccum);
	}
    }
    else if (y <= M_SQRT_32) {

	/* Evaluate pnorm for 0.674.. = qnorm(3/4) < |x| <= sqrtgnum(32) ~= 5.657 */

	xnum = c[8] * y;
	xden = y;
	for (i = 0; i < 7; ++i) {
	    xnum = (xnum + c[i]) * y;
	    xden = (xden + d[i]) * y;
	}
	temp = (xnum + c[7]) / (xden + d[7]);

#define do_del(X)							\
	xsq = floor(X * SIXTEN) / SIXTEN;				\
	del = (X - xsq) * (X + xsq);					\
	if(log_p) {							\
	    *cum = (-xsq * xsq * 0.5) + (-del * 0.5) + loggnum(temp);	\
	    if((lower && x > 0.) || (upper && x <= 0.))			\
		  *ccum = log1pgnum(-expgnum(-xsq * xsq * 0.5) * 		\
				expgnum(-del * 0.5) * temp);		\
	}								\
	else {								\
	    *cum = expgnum(-xsq * xsq * 0.5) * expgnum(-del * 0.5) * temp;	\
	    *ccum = 1.0 - *cum;						\
	}

#define swap_tail						\
	if (x > 0.) {/* swap  ccum <--> cum */			\
	    temp = *cum; if(lower) *cum = *ccum; *ccum = temp;	\
	}

	do_del(y);
	swap_tail;
    }
    else if((-37.5193 < x) || (x < 8.2924)) { /* originally had y < 50 */

	/* Evaluate pnorm for x in (-37.5, -5.657) union (5.657, 8.29) */

	xsq = 1.0 / (x * x);
	xnum = p[5] * xsq;
	xden = xsq;
	for (i = 0; i < 4; ++i) {
	    xnum = (xnum + p[i]) * xsq;
	    xden = (xden + q[i]) * xsq;
	}
	temp = xsq * (xnum + p[4]) / (xden + q[4]);
	temp = (M_1_SQRT_2PI - temp) / y;

	do_del(x);
	swap_tail;
    }
    else { /* x < -37.5193  OR	8.2924 < x */
	if(log_p) {/* be better than to just return loggnum(0) or loggnum(1) */
	    xsq = x*x;
	    if(xsq * GNUM_EPSILON < 1.)
		del = (1. - (1. - 5./(xsq+6.)) / (xsq+4.)) / (xsq+2.);
	    else
		del = 0.;
	    *cum = -.5*xsq - M_LN_SQRT_2PI - loggnum(y) + log1pgnum(del);
	    *ccum = -0.;/*loggnum(1)*/
	    swap_tail;

	} else {
	    if(x > 0) {	*cum = 1.; *ccum = 0.;	}
	    else {	*cum = 0.; *ccum = 1.;	}
	}
    }

#ifdef NO_DENORMS
    /* do not return "denormalized" -- needed ?? */
    if(log_p) {
	if(*cum > -min)	 *cum = -0.;
	if(*ccum > -min)*ccum = -0.;
    else {
	if(*cum < min)	 *cum = 0.;
	if(*ccum < min)	*ccum = 0.;
    }
#endif
    return;
}
/* Cleaning up done by tools/import-R:  */
#undef SIXTEN
#undef do_del
#undef swap_tail

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qnorm.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
 *  based on AS 111 (C) 1977 Royal Statistical Society
 *  and   on AS 241 (C) 1988 Royal Statistical Society
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  SYNOPSIS
 *
 *	double qnorm5(double p, double mu, double sigma,
 *		      int lower_tail, int log_p)
 *            {qnorm (..) is synonymous and preferred inside R}
 *
 *  DESCRIPTION
 *
 *	Compute the quantile function for the normal distribution.
 *
 *	For small to moderate probabilities, algorithm referenced
 *	below is used to obtain an initial approximation which is
 *	polished with a final Newton step.
 *
 *	For very large arguments, an algorithm of Wichura is used.
 *
 *  REFERENCE
 *
 *	Beasley, J. D. and S. G. Springer (1977).
 *	Algorithm AS 111: The percentage points of the normal distribution,
 *	Applied Statistics, 26, 118-121.
 *
 *      Wichura, M.J. (1988).
 *      Algorithm AS 241: The Percentage Points of the Normal Distribution.
 *      Applied Statistics, 37, 477-484.
 */


gnum_float qnorm(gnum_float p, gnum_float mu, gnum_float sigma, gboolean lower_tail, gboolean log_p)
{
    gnum_float p_, q, r, val;

#ifdef IEEE_754
    if (isnangnum(p) || isnangnum(mu) || isnangnum(sigma))
	return p + mu + sigma;
#endif
    if (p == R_DT_0)	return ML_NEGINF;
    if (p == R_DT_1)	return ML_POSINF;
    R_Q_P01_check(p);

    if(sigma  < 0)	ML_ERR_return_NAN;
    if(sigma == 0)	return mu;

    p_ = R_DT_qIv(p);/* real lower_tail prob. p */
    q = p_ - 0.5;

#ifdef DEBUG_qnorm
    REprintf("qnorm(p=%10.7" GNUM_FORMAT_g ", m=%" GNUM_FORMAT_g ", s=%" GNUM_FORMAT_g ", l.t.= %d, log= %d): q = %" GNUM_FORMAT_g "\n",
	     p,mu,sigma, lower_tail, log_p, q);
#endif


#ifdef OLD_qnorm
    /* --- use  AS 111 --- */
    if (gnumabs(q) <= 0.42) {

	/* 0.08 <= p <= 0.92 */

	r = q * q;
	val = q * (((GNUM_const(-25.44106049637) * r + GNUM_const(41.39119773534)) * r
		    - GNUM_const(18.61500062529)) * r + GNUM_const(2.50662823884))
	    / ((((GNUM_const(3.13082909833) * r - GNUM_const(21.06224101826)) * r
		 + GNUM_const(23.08336743743)) * r + GNUM_const(-8.47351093090)) * r + 1.0);
    }
    else {

	/* p < 0.08 or p > 0.92, set r = min(p, 1 - p) */

	if (q > 0)
	    r = R_DT_CIv(p);/* 1-p */
	else
	    r = p_;/* = R_DT_Iv(p) ^=  p */
#ifdef DEBUG_qnorm
	REprintf("\t 'middle p': r = %7" GNUM_FORMAT_g "\n", r);
#endif

	if(r > GNUM_EPSILON) {
	    r = sqrtgnum(- ((log_p &&
			 ((lower_tail && q <= 0) || (!lower_tail && q > 0))) ?
			p : /* else */ loggnum(r)));
#ifdef DEBUG_qnorm
	    REprintf("\t new r = %7" GNUM_FORMAT_g " ( =? sqrtgnum(- loggnum(r)) )\n", r);
#endif
	    val = (((GNUM_const(2.32121276858) * r + GNUM_const(4.85014127135)) * r
		    - GNUM_const(2.29796479134)) * r - GNUM_const(2.78718931138))
		/ ((GNUM_const(1.63706781897) * r + GNUM_const(3.54388924762)) * r + 1.0);
	    if (q < 0)
		val = -val;
	}
	else if(r >= GNUM_MIN) { /* r = p <= eps : Use Wichura */
	    val = -2 * (log_p ? R_D_Lval(p) : loggnum(R_D_Lval(p)));
	    r = loggnum(2 * M_PIgnum * val);
#ifdef DEBUG_qnorm
	    REprintf("\t GNUM_MIN <= r <= DBL_EPS: val = %" GNUM_FORMAT_g ", new r = %" GNUM_FORMAT_g "\n",
		     val, r);
#endif
	    p = val * val;
	    r = r/val + (2 - r)/p + (-14 + 6 * r - r * r)/(2 * p * val);
	    val = sqrtgnum(val * (1 - r));
	    if(q < 0.0)
		val = -val;
	    return mu + sigma * val;
	}
	else {
#ifdef DEBUG_qnorm
	    REprintf("\t r < GNUM_MIN : giving up (-> +- Inf \n");
#endif
	    ML_ERROR(ME_RANGE);
	    if(q < 0.0) return ML_NEGINF;
	    else	return ML_POSINF;
	}
    }
/* FIXME: This could be improved when log_p or !lower_tail ?
 *	  (using p, not p_ , and a different derivative )
 */
#ifdef DEBUG_qnorm
    REprintf("\t before final step: val = %7" GNUM_FORMAT_g "\n", val);
#endif
    /* Final Newton step: */
    val = val -
	(pnorm(val, 0., 1., /*lower*/TRUE, /*log*/FALSE) - p_) /
	 dnorm(val, 0., 1., /*log*/FALSE);

#else
/*-- use AS 241 --- */
/* gnum_float ppnd16_(gnum_float *p, long *ifault)*/
/*      ALGORITHM AS241  APPL. STATIST. (1988) VOL. 37, NO. 3

        Produces the normal deviate Z corresponding to a given lower
        tail area of P; Z is accurate to about 1 part in 10**16.

        (original fortran code used PARAMETER(..) for the coefficients
         and provided hash codes for checking them...)
*/
    if (gnumabs(q) <= .425) {/* 0.075 <= p <= 0.925 */
        r = .180625 - q * q;
	val =
            q * (((((((r * GNUM_const(2509.0809287301226727) +
                       GNUM_const(33430.575583588128105)) * r + GNUM_const(67265.770927008700853)) * r +
                     GNUM_const(45921.953931549871457)) * r + GNUM_const(13731.693765509461125)) * r +
                   GNUM_const(1971.5909503065514427)) * r + GNUM_const(133.14166789178437745)) * r +
                 GNUM_const(3.387132872796366608))
            / (((((((r * GNUM_const(5226.495278852854561) +
                     GNUM_const(28729.085735721942674)) * r + GNUM_const(39307.89580009271061)) * r +
                   GNUM_const(21213.794301586595867)) * r + GNUM_const(5394.1960214247511077)) * r +
                 GNUM_const(687.1870074920579083)) * r + GNUM_const(42.313330701600911252)) * r + 1.);
    }
    else { /* closer than 0.075 from {0,1} boundary */

	/* r = min(p, 1-p) < 0.075 */
	if (q > 0)
	    r = R_DT_CIv(p);/* 1-p */
	else
	    r = p_;/* = R_DT_Iv(p) ^=  p */

	r = sqrtgnum(- ((log_p &&
		     ((lower_tail && q <= 0) || (!lower_tail && q > 0))) ?
		    p : /* else */ loggnum(r)));
        /* r = sqrtgnum(-loggnum(r))  <==>  min(p, 1-p) = expgnum( - r^2 ) */
#ifdef DEBUG_qnorm
	REprintf("\t close to 0 or 1: r = %7" GNUM_FORMAT_g "\n", r);
#endif

        if (r <= 5.) { /* <==> min(p,1-p) >= expgnum(-25) ~= 1.3888e-11 */
            r += -1.6;
            val = (((((((r * GNUM_const(7.7454501427834140764e-4) +
                       GNUM_const(.0227238449892691845833)) * r + GNUM_const(.24178072517745061177)) *
                     r + GNUM_const(1.27045825245236838258)) * r +
                    GNUM_const(3.64784832476320460504)) * r + GNUM_const(5.7694972214606914055)) *
                  r + GNUM_const(4.6303378461565452959)) * r +
                 GNUM_const(1.42343711074968357734))
                / (((((((r *
                         GNUM_const(1.05075007164441684324e-9) + GNUM_const(5.475938084995344946e-4)) *
                        r + GNUM_const(.0151986665636164571966)) * r +
                       GNUM_const(.14810397642748007459)) * r + GNUM_const(.68976733498510000455)) *
                     r + GNUM_const(1.6763848301838038494)) * r +
                    GNUM_const(2.05319162663775882187)) * r + 1.);
        }
        else { /* very close to  0 or 1 */
            r += -5.;
            val = (((((((r * GNUM_const(2.01033439929228813265e-7) +
                       GNUM_const(2.71155556874348757815e-5)) * r +
                      GNUM_const(.0012426609473880784386)) * r + GNUM_const(.026532189526576123093)) *
                    r + GNUM_const(.29656057182850489123)) * r +
                   GNUM_const(1.7848265399172913358)) * r + GNUM_const(5.4637849111641143699)) *
                 r + GNUM_const(6.6579046435011037772))
                / (((((((r *
                         GNUM_const(2.04426310338993978564e-15) + GNUM_const(1.4215117583164458887e-7))*
                        r + GNUM_const(1.8463183175100546818e-5)) * r +
                       GNUM_const(7.868691311456132591e-4)) * r + GNUM_const(.0148753612908506148525))
                     * r + GNUM_const(.13692988092273580531)) * r +
                    GNUM_const(.59983220655588793769)) * r + 1.);
        }

	if(q < 0.0)
	    val = -val;
        /* return (q >= 0.)? r : -r ;*/
    }

#endif
/*-- Switch of AS 111 <-> AS 241 --- */

    return mu + sigma * val;
}




/* ------------------------------------------------------------------------ */
/* Imported src/nmath/plnorm.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  DESCRIPTION
 *
 *    The lognormal distribution function.
 */


gnum_float plnorm(gnum_float x, gnum_float logmean, gnum_float logsd, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (isnangnum(x) || isnangnum(logmean) || isnangnum(logsd))
	return x + logmean + logsd;
#endif
    if (logsd <= 0) ML_ERR_return_NAN;

    if (x > 0)
	return pnorm(loggnum(x), logmean, logsd, lower_tail, log_p);
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qlnorm.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  DESCRIPTION
 *
 *    This the lognormal quantile function.
 */


gnum_float qlnorm(gnum_float p, gnum_float logmean, gnum_float logsd, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (isnangnum(p) || isnangnum(logmean) || isnangnum(logsd))
	return p + logmean + logsd;
#endif
    R_Q_P01_check(p);

    if (p == R_DT_1)	return ML_POSINF;
    if (p == R_DT_0)	return 0;
    return expgnum(qnorm(p, logmean, logsd, lower_tail, log_p));
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/ppois.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  DESCRIPTION
 *
 *    The distribution function of the Poisson distribution.
 */


gnum_float ppois(gnum_float x, gnum_float lambda, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (isnangnum(x) || isnangnum(lambda))
	return x + lambda;
#endif
    if(lambda < 0.) ML_ERR_return_NAN;

    x = floorgnum(x + 1e-7);
    if (x < 0)		return R_DT_0;
    if (lambda == 0.)	return R_DT_1;
    if (!finitegnum(x))	return R_DT_1;

    return pgamma(lambda, x + 1, 1., !lower_tail, log_p);
}

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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
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


/* stirlerr(n) = loggnum(n!) - loggnum( sqrtgnum(2*pi*n)*(n/e)^n ) */

static gnum_float stirlerr(gnum_float n)
{

#define S0 GNUM_const(0.083333333333333333333)       /* 1/12 */
#define S1 GNUM_const(0.00277777777777777777778)     /* 1/360 */
#define S2 GNUM_const(0.00079365079365079365079365)  /* 1/1260 */
#define S3 GNUM_const(0.000595238095238095238095238) /* 1/1680 */
#define S4 GNUM_const(0.0008417508417508417508417508)/* 1/1188 */

/*
  error for 0, 0.5, 1.0, 1.5, ..., 14.5, 15.0.
*/
    const gnum_float sferr_halves[31] = {
	0.0, /* n=0 - wrong, place holder only */
	GNUM_const(0.1534264097200273452913848),  /* 0.5 */
	GNUM_const(0.0810614667953272582196702),  /* 1.0 */
	GNUM_const(0.0548141210519176538961390),  /* 1.5 */
	GNUM_const(0.0413406959554092940938221),  /* 2.0 */
	GNUM_const(0.03316287351993628748511048), /* 2.5 */
	GNUM_const(0.02767792568499833914878929), /* 3.0 */
	GNUM_const(0.02374616365629749597132920), /* 3.5 */
	GNUM_const(0.02079067210376509311152277), /* 4.0 */
	GNUM_const(0.01848845053267318523077934), /* 4.5 */
	GNUM_const(0.01664469118982119216319487), /* 5.0 */
	GNUM_const(0.01513497322191737887351255), /* 5.5 */
	GNUM_const(0.01387612882307074799874573), /* 6.0 */
	GNUM_const(0.01281046524292022692424986), /* 6.5 */
	GNUM_const(0.01189670994589177009505572), /* 7.0 */
	GNUM_const(0.01110455975820691732662991), /* 7.5 */
	GNUM_const(0.010411265261972096497478567), /* 8.0 */
	GNUM_const(0.009799416126158803298389475), /* 8.5 */
	GNUM_const(0.009255462182712732917728637), /* 9.0 */
	GNUM_const(0.008768700134139385462952823), /* 9.5 */
	GNUM_const(0.008330563433362871256469318), /* 10.0 */
	GNUM_const(0.007934114564314020547248100), /* 10.5 */
	GNUM_const(0.007573675487951840794972024), /* 11.0 */
	GNUM_const(0.007244554301320383179543912), /* 11.5 */
	GNUM_const(0.006942840107209529865664152), /* 12.0 */
	GNUM_const(0.006665247032707682442354394), /* 12.5 */
	GNUM_const(0.006408994188004207068439631), /* 13.0 */
	GNUM_const(0.006171712263039457647532867), /* 13.5 */
	GNUM_const(0.005951370112758847735624416), /* 14.0 */
	GNUM_const(0.005746216513010115682023589), /* 14.5 */
	GNUM_const(0.005554733551962801371038690)  /* 15.0 */
    };
    gnum_float nn;

    if (n <= 15.0) {
	nn = n + n;
	if (nn == (int)nn) return(sferr_halves[(int)nn]);
	return(lgammafn(n + 1.) - (n + 0.5)*loggnum(n) + n - M_LN_SQRT_2PI);
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
/* Imported src/nmath/bd0.c from R.  */
/*
 *  AUTHOR
 *	Catherine Loader, catherine@research.bell-labs.com.
 *	October 23, 2000.
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *
 *  DESCRIPTION
 *	Evaluates the "deviance part"
 *	bd0(x,M) :=  M * D0(x/M) = M*[ x/M * log(x/M) + 1 - (x/M) ] =
 *		  =  x * log(x/M) + M - x
 *	where M = E[X] = n*p (or = lambda), for	  x, M > 0
 *
 *	in a manner that should be stable (with small relative error)
 *	for all x and np. In particular for x/np close to 1, direct
 *	evaluation fails, and evaluation is based on the Taylor series
 *	of log((1+v)/(1-v)) with v = (x-np)/(x+np).
 */

static gnum_float bd0(gnum_float x, gnum_float np)
{
    gnum_float ej, s, s1, v;
    int j;

    if (gnumabs(x-np) < 0.1*(x+np)) {
	v = (x-np)/(x+np);
	s = (x-np)*v;/* s using v -- change by MM */
	ej = 2*x*v;
	v = v*v;
	for (j=1; ; j++) { /* Taylor series */
	    ej *= v;
	    s1 = s+ej/((j<<1)+1);
	    if (s1==s) /* last term was effectively 0 */
		return(s1);
	    s = s1;
	}
    }
    /* else:  | x - np |  is not too small */
    return(x*loggnum(x/np)+np-x);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dpois.c from R.  */
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *
 * DESCRIPTION
 *
 *    dpois() checks argument validity and calls dpois_raw().
 *
 *    dpois_raw() computes the Poisson probability  lb^x exp(-lb) / x!.
 *      This does not check that x is an integer, since dgamma() may
 *      call this with a fractional x argument. Any necessary argument
 *      checks should be done in the calling function.
 *
 */


static gnum_float dpois_raw(gnum_float x, gnum_float lambda, gboolean give_log)
{
    if (lambda == 0) return( (x == 0) ? R_D__1 : R_D__0 );
    if (x == 0) return( R_D_exp(-lambda) );
    if (x < 0)  return( R_D__0 );

    return(R_D_fexp( M_2PIgnum*x, -stirlerr(x)-bd0(x,lambda) ));
}

gnum_float dpois(gnum_float x, gnum_float lambda, gboolean give_log)
{
#ifdef IEEE_754
    if(isnangnum(x) || isnangnum(lambda))
        return x + lambda;
#endif

    if (lambda < 0) ML_ERR_return_NAN;
    R_D_nonint_check(x);
    if (x < 0 || !finitegnum(x)) return R_D__0;
    x = R_D_forceint(x);

    return( dpois_raw(x,lambda,give_log) );
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dgamma.c from R.  */
/*
 *  AUTHOR
 *    Catherine Loader, catherine@research.bell-labs.com.
 *    October 23, 2000.
 *
 *  Merge in to R:
 *	Copyright (C) 2000 The R Core Development Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *
 * DESCRIPTION
 *
 *   Computes the density of the gamma distribution,
 *
 *                   1/s (x/s)^{a-1} exp(-x/s)
 *        p(x;a,s) = -----------------------
 *                            (a-1)!
 *
 *   where `s' is the scale (= 1/lambda in other parametrizations)
 *     and `a' is the shape parameter ( = alpha in other contexts).
 *
 * The old (R 1.1.1) version of the code is available via `#define D_non_pois'
 */


gnum_float dgamma(gnum_float x, gnum_float shape, gnum_float scale, gboolean give_log)
{
#ifndef D_non_pois
    gnum_float pr;
#endif
#ifdef IEEE_754
    if (isnangnum(x) || isnangnum(shape) || isnangnum(scale))
        return x + shape + scale;
#endif
    if (shape <= 0 || scale <= 0) ML_ERR_return_NAN;
    if (x < 0)
	return R_D__0;
    if (x == 0) {
	if (shape < 1) ML_ERR_return_NAN;
	if (shape > 1) return R_D__0;
	/* else */
	return give_log ? -loggnum(scale) : 1 / scale;
    }

#ifdef D_non_pois

    x /= scale;
    return give_log ?
	   ((shape - 1) * loggnum(x) - lgammafn(shape) - x) - loggnum(scale) :
	expgnum((shape - 1) * loggnum(x) - lgammafn(shape) - x) / scale;

#else /* new dpois() based code */

    if (shape < 1) {
	pr = dpois_raw(shape, x/scale, give_log);
	return give_log ?  pr + loggnum(shape/x) : pr*shape/x;
    }
    /* else  shape >= 1 */
    pr = dpois_raw(shape-1, x/scale, give_log);
    return give_log ? pr - loggnum(scale) : pr/scale;
#endif
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pgamma.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998		Ross Ihaka
 *  Copyright (C) 1999-2000	The R Development Core Team
 *  based on AS 239 (C) 1988 Royal Statistical Society
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  SYNOPSIS
 *
 *	#include "Rmath.h"
 *	double pgamma(double x, double alph, double scale,
 *		      int lower_tail, int log_p)
 *
 *  DESCRIPTION
 *
 *	This function computes the distribution function for the
 *	gamma distribution with shape parameter alph and scale parameter
 *	scale.	This is also known as the incomplete gamma function.
 *	See Abramowitz and Stegun (6.5.1) for example.
 *
 *  NOTES
 *
 *	This function is an adaptation of Algorithm 239 from the
 *	Applied Statistics Series.  The algorithm is faster than
 *	those by W. Fullerton in the FNLIB library and also the
 *	TOMS 542 alorithm of W. Gautschi.  It provides comparable
 *	accuracy to those algorithms and is considerably simpler.
 *
 *  REFERENCES
 *
 *	Algorithm AS 239, Incomplete Gamma Function
 *	Applied Statistics 37, 1988.
 */

/*----------- DEBUGGING -------------
 *	make CFLAGS='-DDEBUG_p -g -I/usr/local/include -I../include'
 */


gnum_float pgamma(gnum_float x, gnum_float alph, gnum_float scale, gboolean lower_tail, gboolean log_p)
{
    const gnum_float
	xbig = 1.0e+8,
	xlarge = 1.0e+37,

#ifndef IEEE_754
	elimit = M_LN2gnum*(GNUM_MIN_EXP),/* will set expgnum(E) = 0 for E < elimit ! */
    /* was elimit = -88.0e0; */
#endif
	alphlimit = 1000.;/* normal approx. for alph > alphlimit */

    gnum_float pn1, pn2, pn3, pn4, pn5, pn6, arg, a, b, c, an, osum, sum;
    long n;
    int pearson;

    /* check that we have valid values for x and alph */

#ifdef IEEE_754
    if (isnangnum(x) || isnangnum(alph) || isnangnum(scale))
	return x + alph + scale;
#endif
#ifdef DEBUG_p
    REprintf("pgamma(x=%4" GNUM_FORMAT_g ", alph=%4" GNUM_FORMAT_g ", scale=%4" GNUM_FORMAT_g "): ",x,alph,scale);
#endif
    if(alph <= 0. || scale <= 0.)
	ML_ERR_return_NAN;

    x /= scale;
#ifdef DEBUG_p
    REprintf("-> x=%4" GNUM_FORMAT_g "; ",x);
#endif
#ifdef IEEE_754
    if (isnangnum(x)) /* eg. original x = scale = Inf */
	return x;
#endif
    if (x <= 0.)
	return R_DT_0;

    /* use a normal approximation if alph > alphlimit */

    if (alph > alphlimit) {
	pn1 = sqrtgnum(alph) * 3. * (powgnum(x/alph, 1./3.) + 1. / (9. * alph) - 1.);
	return pnorm(pn1, 0., 1., lower_tail, log_p);
    }

    /* if x is extremely large __compared to alph__ then return 1 */

    if (x > xbig * alph)
	return R_DT_1;

    if (x <= 1. || x < alph) {

	pearson = 1;/* use pearson's series expansion. */

	arg = alph * loggnum(x) - x - lgammafn(alph + 1.);
#ifdef DEBUG_p
	REprintf("Pearson  arg=%" GNUM_FORMAT_g " ", arg);
#endif
	c = 1.;
	sum = 1.;
	a = alph;
	do {
	    a += 1.;
	    c *= x / a;
	    sum += c;
	} while (c > GNUM_EPSILON);
	arg += loggnum(sum);
    }
    else { /* x >= max( 1, alph) */

	pearson = 0;/* use a continued fraction expansion */

	arg = alph * loggnum(x) - x - lgammafn(alph);
#ifdef DEBUG_p
	REprintf("Cont.Fract. arg=%" GNUM_FORMAT_g " ", arg);
#endif
	a = 1. - alph;
	b = a + x + 1.;
	pn1 = 1.;
	pn2 = x;
	pn3 = x + 1.;
	pn4 = x * b;
	sum = pn3 / pn4;
	for (n = 1; ; n++) {
	    a += 1.;/* =   n+1 -alph */
	    b += 2.;/* = 2(n+1)-alph+x */
	    an = a * n;
	    pn5 = b * pn3 - an * pn1;
	    pn6 = b * pn4 - an * pn2;
	    if (gnumabs(pn6) > 0.) {
		osum = sum;
		sum = pn5 / pn6;
		if (gnumabs(osum - sum) <= GNUM_EPSILON * fmin2(1., sum))
		    break;
	    }
	    pn1 = pn3;
	    pn2 = pn4;
	    pn3 = pn5;
	    pn4 = pn6;
	    if (gnumabs(pn5) >= xlarge) {

		/* re-scale the terms in continued fraction if they are large */
#ifdef DEBUG_p
		REprintf(" [r] ");
#endif
		pn1 /= xlarge;
		pn2 /= xlarge;
		pn3 /= xlarge;
		pn4 /= xlarge;
	    }
	}
	arg += loggnum(sum);
    }

#ifdef DEBUG_p
    REprintf("--> arg=%12" GNUM_FORMAT_g " (elimit=%" GNUM_FORMAT_g ")\n", arg, elimit);
#endif

    lower_tail = (lower_tail == pearson);

    if (log_p && lower_tail)
	return(arg);
    /* else */
#ifndef IEEE_754
    /* Underflow check :*/
    if (arg < elimit)
	sum = 0.;
    else
#endif
	sum = expgnum(arg);

    return (lower_tail) ? sum : R_D_val(1 - sum);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qgamma.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
 *  based on AS91 (C) 1979 Royal Statistical Society
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 *
 *  DESCRIPTION
 *
 *	Compute the quantile function of the gamma distribution.
 *
 *  NOTES
 *
 *	This function is based on the Applied Statistics
 *	Algorithm AS 91 ("ppchi2") and via pgamma(.) AS 239.
 *
 *  REFERENCES
 *
 *	Best, D. J. and D. E. Roberts (1975).
 *	Percentage Points of the Chi-Squared Distribution.
 *      Applied Statistics 24, page 385.  */


gnum_float qgamma(gnum_float p, gnum_float alpha, gnum_float scale, gboolean lower_tail, gboolean log_p)
/*			shape = alpha */
{
#define C7	4.67
#define C8	6.66
#define C9	6.73
#define C10	13.32

#define EPS1 1e-2
#define EPS2 5e-7/* final precision */
#define MAXIT 1000/* was 20 */

#define pMIN 1e-100    /* was 0.000002 = 2e-6 */
#define pMAX (1-1e-12)/* was 0.999998 = 1 - 2e-6 */

    const gnum_float
	i420  = 1./ 420.,
	i2520 = 1./ 2520.,
	i5040 = 1./ 5040;

    gnum_float p_, a, b, c, ch, g, p1, v;
    gnum_float p2, q, s1, s2, s3, s4, s5, s6, t, x;
    int i;

    /* test arguments and initialise */

#ifdef IEEE_754
    if (isnangnum(p) || isnangnum(alpha) || isnangnum(scale))
	return p + alpha + scale;
#endif
    R_Q_P01_check(p);
    if (alpha <= 0) ML_ERR_return_NAN;

    /* FIXME: This (cutoff to {0, +Inf}) is far from optimal when log_p: */
    p_ = R_DT_qIv(p);/* lower_tail prob (in any case) */
    if (/* 0 <= */ p_ < pMIN) return 0;
    if (/* 1 >= */ p_ > pMAX) return ML_POSINF;

    v = 2*alpha;

    c = alpha-1;
    g = lgammafn(alpha);/* loggnum Gamma(v/2) */


/*----- Phase I : Starting Approximation */

#ifdef DEBUG_qgamma
    REprintf("qgamma(p=%7" GNUM_FORMAT_g ", alpha=%7" GNUM_FORMAT_g ", scale=%7" GNUM_FORMAT_g ", l.t.=%2d, log_p=%2d): ",
	     p,alpha,scale, lower_tail, log_p);
#endif

    if(v < (-1.24)*R_DT_log(p)) {	/* for small chi-squared */

#ifdef DEBUG_qgamma
	REprintf(" small chi-sq.\n");
#endif
	/* FIXME: Improve this "if (log_p)" :
	 *	  (A*expgnum(b)) ^ 1/al */
	ch = powgnum(p_* alpha*expgnum(g+alpha*M_LN2gnum), 1/alpha);
	if(ch < EPS2) {/* Corrected according to AS 91; MM, May 25, 1999 */
	    goto END;
	}

    } else if(v > 0.32) {	/*  using Wilson and Hilferty estimate */

	x = qnorm(p, 0, 1, lower_tail, log_p);
	p1 = 0.222222/v;
	ch = v*powgnum(x*sqrtgnum(p1)+1-p1, 3);

#ifdef DEBUG_qgamma
	REprintf(" v > .32: Wilson-Hilferty; x = %7" GNUM_FORMAT_g "\n", x);
#endif
	/* starting approximation for p tending to 1 */

	if( ch > 2.2*v + 6 )
	    ch = -2*(R_DT_Clog(p) - c*loggnum(0.5*ch) + g);

    } else { /* for v <= 0.32 */

	ch = 0.4;
	a = R_DT_Clog(p) + g + c*M_LN2gnum;
#ifdef DEBUG_qgamma
	REprintf(" v <= .32: a = %7" GNUM_FORMAT_g "\n", a);
#endif
	do {
	    q = ch;
	    p1 = 1. / (1+ch*(C7+ch));
	    p2 = ch*(C9+ch*(C8+ch));
	    t = -0.5 +(C7+2*ch)*p1 - (C9+ch*(C10+3*ch))/p2;
	    ch -= (1- expgnum(a+0.5*ch)*p2*p1)/t;
	} while(gnumabs(q - ch) > EPS1*gnumabs(ch));
    }

#ifdef DEBUG_qgamma
    REprintf("\t==> ch = %10" GNUM_FORMAT_g ":", ch);
#endif

/*----- Phase II: Iteration
 *	Call pgamma() [AS 239]	and calculate seven term taylor series
 */
    for( i=1 ; i <= MAXIT ; i++ ) {
	q = ch;
	p1 = 0.5*ch;
	p2 = p_ - pgamma(p1, alpha, 1, /*lower_tail*/TRUE, /*log_p*/FALSE);
#ifdef IEEE_754
	if(!finitegnum(p2))
#else
	if(errno != 0)
#endif
		return ML_NAN;

	t = p2*expgnum(alpha*M_LN2gnum+g+p1-c*loggnum(ch));
	b = t/ch;
	a = 0.5*t - b*c;
	s1 = (210+a*(140+a*(105+a*(84+a*(70+60*a))))) * i420;
	s2 = (420+a*(735+a*(966+a*(1141+1278*a)))) * i2520;
	s3 = (210+a*(462+a*(707+932*a))) * i2520;
	s4 = (252+a*(672+1182*a)+c*(294+a*(889+1740*a))) * i5040;
	s5 = (84+2264*a+c*(1175+606*a)) * i2520;
	s6 = (120+c*(346+127*c)) * i5040;
	ch += t*(1+0.5*t*s1-b*c*(s1-b*(s2-b*(s3-b*(s4-b*(s5-b*s6))))));
	if(gnumabs(q - ch) < EPS2*ch)
	    goto END;
    }
    ML_ERROR(ME_PRECISION);/* no convergence in MAXIT iterations */
 END:
    return 0.5*scale*ch;
}
/* Cleaning up done by tools/import-R:  */
#undef C7
#undef C8
#undef C9
#undef C10
#undef EPS1
#undef EPS2
#undef MAXIT
#undef pMIN
#undef pMAX

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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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


int chebyshev_init(gnum_float *dos, int nos, gnum_float eta)
{
    int i, ii;
    gnum_float err;

    if (nos < 1)
	return 0;

    err = 0.0;
    i = 0;			/* just to avoid compiler warnings */
    for (ii=1; ii<=nos; ii++) {
	i = nos - ii;
	err += gnumabs(dos[i]);
	if (err > eta) {
	    return i;
	}
    }
    return i;
}


static gnum_float chebyshev_eval(gnum_float x, const gnum_float *a, const int n)
{
    gnum_float b0, b1, b2, twox;
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  SYNOPSIS
 *
 *    #include "Rmath.h"
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
 *    is faster and cleaner, but is only defined for half integers.
 */


static gnum_float lgammacor(gnum_float x)
{
    const gnum_float algmcs[15] = {
	GNUM_const(+.1666389480451863247205729650822e+0),
	GNUM_const(-.1384948176067563840732986059135e-4),
	GNUM_const(+.9810825646924729426157171547487e-8),
	GNUM_const(-.1809129475572494194263306266719e-10),
	GNUM_const(+.6221098041892605227126015543416e-13),
	GNUM_const(-.3399615005417721944303330599666e-15),
	GNUM_const(+.2683181998482698748957538846666e-17),
	GNUM_const(-.2868042435334643284144622399999e-19),
	GNUM_const(+.3962837061046434803679306666666e-21),
	GNUM_const(-.6831888753985766870111999999999e-23),
	GNUM_const(+.1429227355942498147573333333333e-24),
	GNUM_const(-.3547598158101070547199999999999e-26),
	GNUM_const(+.1025680058010470912000000000000e-27),
	GNUM_const(-.3401102254316748799999999999999e-29),
	GNUM_const(+.1276642195630062933333333333333e-30)
    };

    gnum_float tmp;

#ifdef NOMORE_FOR_THREADS
    static int nalgm = 0;
    static gnum_float xbig = 0, xmax = 0;

    /* Initialize machine dependent constants, the first time gamma() is called.
	FIXME for threads ! */
    if (nalgm == 0) {
	/* For IEEE gnum_float precision : nalgm = 5 */
	nalgm = chebyshev_init(algmcs, 15, GNUM_EPSILON/2);/*was d1mach(3)*/
	xbig = 1 / sqrtgnum(GNUM_EPSILON/2); /* ~ 94906265.6 for IEEE gnum_float */
	xmax = expgnum(fmin2(loggnum(GNUM_MAX / 12), -loggnum(12 * GNUM_MIN)));
	/*   = GNUM_MAX / 48 ~= 3.745e306 for IEEE gnum_float */
    }
#else
/* For IEEE gnum_float precision GNUM_EPSILON = 2^-52 = GNUM_const(2.220446049250313e-16) :
 *   xbig = 2 ^ 26.5
 *   xmax = GNUM_MAX / 48 =  2^1020 / 3 */
# define nalgm 5
# define xbig  94906265.62425156
# define xmax  GNUM_const(3.745194030963158e306)
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  SYNOPSIS
 *
 *    #include "Rmath.h"
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


static gnum_float lbeta(gnum_float a, gnum_float b)
{
    gnum_float corr, p, q;

    p = q = a;
    if(b < p) p = b;/* := min(a,b) */
    if(b > q) q = b;/* := max(a,b) */

#ifdef IEEE_754
    if(isnangnum(a) || isnangnum(b))
	return a + b;
#endif

    /* both arguments must be >= 0 */

    if (p < 0)
	ML_ERR_return_NAN
    else if (p == 0) {
	return ML_POSINF;
    }
    else if (!finitegnum(q)) {
	return ML_NEGINF;
    }

    if (p >= 10) {
	/* p and q are big. */
	corr = lgammacor(p) + lgammacor(q) - lgammacor(p + q);
	return loggnum(q) * -0.5 + M_LN_SQRT_2PI + corr
		+ (p - 0.5) * loggnum(p / (p + q)) + q * log1pgnum(-p / (p + q));
    }
    else if (q >= 10) {
	/* p is small, but q is big. */
	corr = lgammacor(q) - lgammacor(p + q);
	return lgammafn(p) + corr + p - p * loggnum(p + q)
		+ (q - 0.5) * log1pgnum(-p / (p + q));
    }
    else
	/* p and q are small: p <= q > 10. */
	return loggnum(gammafn(p) * (gammafn(q) / gammafn(p + q)));
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pbeta.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  SYNOPSIS
 *
 * #include "Rmath.h"
 *
 * double pbeta_raw(double x, double pin, double qin, int lower_tail)
 * double pbeta	   (double x, double pin, double qin, int lower_tail, int log_p)
 *
 *  DESCRIPTION
 *
 *	Returns distribution function of the beta distribution.
 *	( = The incomplete beta ratio I_x(p,q) ).
 *
 *  NOTES
 *
 *	This routine is a translation into C of a Fortran subroutine
 *	by W. Fullerton of Los Alamos Scientific Laboratory.
 *
 *  REFERENCE
 *
 *	Bosten and Battiste (1974).
 *	Remark on Algorithm 179, CACM 17, p153, (1974).
 */


/* This is called from	qbeta(.) in a root-finding loop --- be FAST! */

static gnum_float pbeta_raw(gnum_float x, gnum_float pin, gnum_float qin, gboolean lower_tail)
{
    gnum_float ans, c, finsum, p, ps, p1, q, term, xb, xi, y;
    int n, i, ib, swap_tail;

    const gnum_float eps = .5*GNUM_EPSILON;
    const gnum_float sml = GNUM_MIN;
    const gnum_float lneps = loggnum(eps);
    const gnum_float lnsml = loggnum(sml);

    /* swap tails if x is greater than the mean */
    if (pin / (pin + qin) < x) {
	swap_tail = 1;
	y = 1 - x;
	p = qin;
	q = pin;
    }
    else {
	swap_tail = 0;
	y = x;
	p = pin;
	q = qin;
    }

    if ((p + q) * y / (p + 1) < eps) {

	/* tail approximation */

	ans = 0;
	xb = p * loggnum(fmax2(y, sml)) - loggnum(p) - lbeta(p, q);
	if (xb > lnsml && y != 0)
	    ans = expgnum(xb);
	if (swap_tail == lower_tail)
	    ans = 1 - ans;
    }
    else {
	/*___ FIXME ___:  This takes forever (or ends wrongly)
	  when (one or) both p & q  are huge
	*/

	/* evaluate the infinite sum first.  term will equal */
	/* y^p / beta(ps, p) * (1 - ps)-sub-i * y^i / fac(i) */

	ps = q - floorgnum(q);
	if (ps == 0)
	    ps = 1;
	xb = p * loggnum(y) - lbeta(ps, p) - loggnum(p);
	ans = 0;
	if (xb >= lnsml) {
	    ans = expgnum(xb);
	    term = ans * p;
	    if (ps != 1) {
		n = fmax2(lneps/loggnum(y), 4.0);
		for(i=1 ; i <= n ; i++) {
		    xi = i;
		    term *= (xi - ps) * y / xi;
		    ans += term / (p + xi);
		}
	    }
	}

	/* now evaluate the finite sum, maybe. */

	if (q > 1) {
	    xb = p * loggnum(y) + q * loggnum(1 - y) - lbeta(p, q) - loggnum(q);
	    ib = fmax2(xb / lnsml, 0.0);
	    term = expgnum(xb - ib * lnsml);
	    c = 1 / (1 - y);
	    p1 = q * c / (p + q - 1);

	    finsum = 0;
	    n = q;
	    if (q == n)
		n--;
	    for(i=1 ; i<=n ; i++) {
		if (p1 <= 1 && term / eps <= finsum)
		    break;
		xi = i;
		term = (q - xi + 1) * c * term / (p + q - xi);
		if (term > 1) {
		    ib--;
		    term *= sml;
		}
		if (ib == 0)
		    finsum += term;
	    }
	    ans += finsum;
	}
	if (swap_tail == lower_tail)
	    ans = 1 - ans;
	ans = fmax2(fmin2(ans, 1.), 0.);
    }
    return ans;
} /* pbeta_raw() */

gnum_float pbeta(gnum_float x, gnum_float pin, gnum_float qin, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (isnangnum(x) || isnangnum(pin) || isnangnum(qin))
	return x + pin + qin;
#endif

    if (pin <= 0 || qin <= 0) ML_ERR_return_NAN;

    if (x <= 0)
	return R_DT_0;
    if (x >= 1)
	return R_DT_1;
    return R_D_val(pbeta_raw(x, pin, qin, lower_tail));
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qbeta.c from R.  */
/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 1998--2001  The R Development Core Team
 *  based on code (C) 1979 and later Royal Statistical Society
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *

 * Reference:
 * Cran, G. W., K. J. Martin and G. E. Thomas (1977).
 *	Remark AS R19 and Algorithm AS 109,
 *	Applied Statistics, 26(1), 111-114.
 * Remark AS R83 (v.39, 309-310) and the correction (v.40(1) p.236)
 *	have been incorporated in this version.
 */



/* set the exponent of accu to -2r-2 for r digits of accuracy */
#ifdef OLD
#define acu 1.0e-32
#define lower 0.0001
#define upper 0.9999

#else/*---- NEW ---- -- still fails for p = 1e11, q=.5*/

#define fpu 3e-308
/* acu_min:  Minimal value for accuracy 'acu' which will depend on (a,p);
	     acu_min >= fpu ! */
#define acu_min 1e-300
#define lower fpu
#define upper 1-2.22e-16

#endif

#define const1 2.30753
#define const2 0.27061
#define const3 0.99229
#define const4 0.04481

static volatile gnum_float xtrunc;/* not a real global .. delicate though! */

gnum_float qbeta(gnum_float alpha, gnum_float p, gnum_float q, gboolean lower_tail, gboolean log_p)
{
    int swap_tail, i_pb, i_inn;
    gnum_float a, adj, logbeta, g, h, pp, p_, prev, qq, r, s, t, tx, w, y, yprev;
    gnum_float acu;
    volatile gnum_float xinbta;

    /* define accuracy and initialize */

    xinbta = alpha;

    /* test for admissibility of parameters */

#ifdef IEEE_754
    if (isnangnum(p) || isnangnum(q) || isnangnum(alpha))
	return p + q + alpha;
#endif
    R_Q_P01_check(alpha);

    if(p < 0. || q < 0.) ML_ERR_return_NAN;

    p_ = R_DT_qIv(alpha);/* lower_tail prob (in any case) */

    if (p_ == 0. || p_ == 1.)
	return p_;

    logbeta = lbeta(p, q);

    /* change tail if necessary;  afterwards   0 < a <= 1/2	 */
    if (p_ <= 0.5) {
	a = p_;	pp = p; qq = q; swap_tail = 0;
    } else { /* change tail, swap  p <-> q :*/
	a = (!lower_tail && !log_p)? alpha : 1 - p_;
	pp = q; qq = p; swap_tail = 1;
    }

    /* calculate the initial approximation */

    r = sqrtgnum(-loggnum(a * a));
    y = r - (const1 + const2 * r) / (1. + (const3 + const4 * r) * r);
    if (pp > 1 && qq > 1) {
	r = (y * y - 3.) / 6.;
	s = 1. / (pp + pp - 1.);
	t = 1. / (qq + qq - 1.);
	h = 2. / (s + t);
	w = y * sqrtgnum(h + r) / h - (t - s) * (r + 5. / 6. - 2. / (3. * h));
	xinbta = pp / (pp + qq * expgnum(w + w));
    } else {
	r = qq + qq;
	t = 1. / (9. * qq);
	t = r * powgnum(1. - t + y * sqrtgnum(t), 3.0);
	if (t <= 0.)
	    xinbta = 1. - expgnum((loggnum((1. - a) * qq) + logbeta) / qq);
	else {
	    t = (4. * pp + r - 2.) / t;
	    if (t <= 1.)
		xinbta = expgnum((loggnum(a * pp) + logbeta) / pp);
	    else
		xinbta = 1. - 2. / (t + 1.);
	}
    }

    /* solve for x by a modified newton-raphson method, */
    /* using the function pbeta_raw */

    r = 1 - pp;
    t = 1 - qq;
    yprev = 0.;
    adj = 1;
    /* Sometimes the approximation is negative! */
    if (xinbta < lower)
	xinbta = 0.5;
    else if (xinbta > upper)
	xinbta = 0.5;

    /* Desired accuracy should depend on  (a,p)
     * This is from Remark .. on AS 109, adapted.
     * However, it's not clear if this is "optimal" for IEEE gnum_float prec.

     * acu = fmax2(acu_min, powgnum(10., -25. - 5./(pp * pp) - 1./(a * a)));

     * NEW: 'acu' accuracy NOT for squared adjustment, but simple;
     * ---- i.e.,  "new acu" = sqrtgnum(old acu)

     */
    acu = fmax2(acu_min, powgnum(10., -13 - 2.5/(pp * pp) - 0.5/(a * a)));
    tx = prev = 0.;	/* keep -Wall happy */

    for (i_pb=0; i_pb < 1000; i_pb++) {
	y = pbeta_raw(xinbta, pp, qq, /*lower_tail = */ TRUE);
	/* y = pbeta_raw2(xinbta, pp, qq, logbeta) -- to SAVE CPU; */
#ifdef IEEE_754
	if(!finitegnum(y))
#else
	    if (errno)
#endif
		ML_ERR_return_NAN;

	y = (y - a) *
	    expgnum(logbeta + r * loggnum(xinbta) + t * loggnum(1 - xinbta));
	if (y * yprev <= 0.)
	    prev = fmax2(gnumabs(adj),fpu);
	g = 1;
	for (i_inn=0; i_inn < 1000;i_inn++) {
	    adj = g * y;
	    if (gnumabs(adj) < prev) {
		tx = xinbta - adj; /* trial new x */
		if (tx >= 0. && tx <= 1) {
		    if (prev <= acu)	goto L_converged;
		    if (gnumabs(y) <= acu) goto L_converged;
		    if (tx != 0. && tx != 1)
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
	return 1 - xinbta;
    return xinbta;
}
/* Cleaning up done by tools/import-R:  */
#undef acu
#undef lower
#undef upper
#undef fpu
#undef acu_min
#undef lower
#undef upper
#undef const1
#undef const2
#undef const3
#undef const4

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pt.c from R.  */
/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 1995, 1996  Robert Gentleman and Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 */


gnum_float pt(gnum_float x, gnum_float n, gboolean lower_tail, gboolean log_p)
{
/* return  P[ T <= x ]	where
 * T ~ t_{n}  (t distrib. with n degrees of freedom).

 *	--> ./pnt.c for NON-central
 */
    gnum_float val;
#ifdef IEEE_754
    if (isnangnum(x) || isnangnum(n))
	return x + n;
#endif
    if (n <= 0.0) ML_ERR_return_NAN;

    if(!finitegnum(x))
	return (x < 0) ? R_DT_0 : R_DT_1;
    if(!finitegnum(n))
	return pnorm(x, 0.0, 1.0, lower_tail, log_p);
    if (n > 4e5) { /*-- Fixme(?): test should depend on `n' AND `x' ! */
	/* Approx. from	 Abramowitz & Stegun 26.7.8 (p.949) */
	val = 1./(4.*n);
	return pnorm(x*(1. - val)/sqrtgnum(1. + x*x*2.*val), 0.0, 1.0,
		     lower_tail, log_p);
    }

    val = pbeta(n / (n + x * x), n / 2.0, 0.5, /*lower_tail*/1, log_p);

    /* Use "1 - v"  if	lower_tail  and	 x > 0 (but not both):*/
    if(x <= 0.)
	lower_tail = !lower_tail;

    if(log_p) {
	if(lower_tail) return loggnum(1 - 0.5*expgnum(val));
	else return val - M_LN2gnum; /* = loggnum(.5* pbeta(....)) */
    }
    else {
	val /= 2.;
	return R_D_Cval(val);
    }
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qt.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  DESCRIPTION
 *
 *	The "Student" t distribution quantile function.
 *
 *  NOTES
 *
 *	This is a C translation of the Fortran routine given in:
 *	Algorithm 396: Student's t-quantiles by
 *	G.W. Hill CACM 13(10), 619-620, October 1970
 */


gnum_float qt(gnum_float p, gnum_float ndf, gboolean lower_tail, gboolean log_p)
{
    const gnum_float eps = 1.e-12;

    gnum_float a, b, c, d, p_, P, q, x, y;
    gboolean neg;

#ifdef IEEE_754
    if (isnangnum(p) || isnangnum(ndf))
	return p + ndf;
#endif
    if (p == R_DT_0) return ML_NEGINF;
    if (p == R_DT_1) return ML_POSINF;
    R_Q_P01_check(p);

    if (ndf < 1) /* FIXME:  not yet treated here */
	ML_ERR_return_NAN;

    /* FIXME: This test should depend on  ndf  AND p  !!
     * -----  and in fact should be replaced by
     * something like Abramowitz & Stegun 26.7.5 (p.949)
     */
    if (ndf > 1e20) return qnorm(p, 0., 1., lower_tail, log_p);

    p_ = R_D_qIv(p); /* note: expgnum(p) may underflow to 0; fix later */

    if((lower_tail && p_ > 0.5) || (!lower_tail && p_ < 0.5)) {
	neg = FALSE; P = 2 * R_D_Cval(p_);
    } else {
	neg = TRUE;  P = 2 * R_D_Lval(p_);
    } /* 0 <= P <= 1  in all cases */

    if (gnumabs(ndf - 2) < eps) {	/* df ~= 2 */
	if(P > 0)
	    q = sqrtgnum(2 / (P * (2 - P)) - 2);
	else { /* P = 0, but maybe = expgnum(p) ! */
	    if(log_p) q = M_SQRT2gnum * expgnum(- .5 * R_D_Lval(p));
	    else q = ML_POSINF;
	}
    }
    else if (ndf < 1 + eps) { /* df ~= 1  (df < 1 excluded above !) */
	if(P > 0)
	    q = - tan((P+1) * M_PI_2gnum);

	else { /* P = 0, but maybe p_ = expgnum(p) ! */
	    if(log_p) q = M_1_PI * expgnum(-R_D_Lval(p));/* cot(e) ~ 1/e */
	    else q = ML_POSINF;
	}
    }
    else {		/*-- usual case;  including, e.g.,  df = 1.1 */
	a = 1 / (ndf - 0.5);
	b = 48 / (a * a);
	c = ((20700 * a / b - 98) * a - 16) * a + 96.36;
	d = ((94.5 / (b + c) - 3) / b + 1) * sqrtgnum(a * M_PI_2gnum) * ndf;
	if(P > 0 || !log_p)
	    y = powgnum(d * P, 2 / ndf);
	else /* P = 0 && log_p;  P = 2*expgnum(p*) */
	    y = expgnum(2 / ndf * (loggnum(d) + M_LN2gnum + R_D_Lval(p)));

	if (y > 0.05 + a) {
	    /* Asymptotic inverse expansion about normal */
	    if(P > 0 || !log_p)
		x = qnorm(0.5 * P, 0., 1., /*lower_tail*/TRUE, /*log_p*/FALSE);
	    else /* P = 0 && log_p;  P = 2*expgnum(p') */
		x = qnorm( p,	   0., 1., lower_tail,	       /*log_p*/TRUE);

	    y = x * x;
	    if (ndf < 5)
		c += 0.3 * (ndf - 4.5) * (x + 0.6);
	    c = (((0.05 * d * x - 5) * x - 7) * x - 2) * x + b + c;
	    y = (((((0.4 * y + 6.3) * y + 36) * y + 94.5) / c
		  - y - 3) / b + 1) * x;
	    y = a * y * y;
	    /* FIXME: Following cutoff is machine-precision dependent
	       -----  Really, use stable impl. of expm1(y) == expgnum(y) - 1,
	              as it is in GNU's mathlib ..*/
	    if (1) /* was (y > 0.002) */
		y = expm1gnum(y);
	    else { /* Taylor of	 e^y -1 : */
		y = (0.5 * y + 1) * y;
	    }
	} else {
	    y = ((1 / (((ndf + 6) / (ndf * y) - 0.089 * d - 0.822)
		       * (ndf + 2) * 3) + 0.5 / (ndf + 4))
		 * y - 1) * (ndf + 1) / (ndf + 2) + 1 / y;
	}
	q = sqrtgnum(ndf * y);
    }
    if(neg) q = -q;
    return q;
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pf.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  DESCRIPTION
 *
 *    The distribution function of the F distribution.
 */


gnum_float pf(gnum_float x, gnum_float n1, gnum_float n2, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (isnangnum(x) || isnangnum(n1) || isnangnum(n2))
	return x + n2 + n1;
#endif
    if (n1 <= 0. || n2 <= 0.) ML_ERR_return_NAN;

    if (x <= 0.)
	return R_DT_0;

    /* fudge the extreme DF cases -- pbeta doesn't do this well */

    if (n2 > 4e5)
	return pchisq(x * n1, n1, lower_tail, log_p);

    if (n1 > 4e5)
	return pchisq(n2 / x , n2, !lower_tail, log_p);

    x = pbeta(n2 / (n2 + n1 * x), n2 / 2.0, n1 / 2.0,
	      !lower_tail, log_p);

    return ML_VALID(x) ? x : ML_NAN;
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qf.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  DESCRIPTION
 *
 *    The quantile function of the F distribution.
*/


gnum_float qf(gnum_float p, gnum_float n1, gnum_float n2, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (isnangnum(p) || isnangnum(n1) || isnangnum(n2))
	return p + n1 + n2;
#endif
    if (n1 <= 0. || n2 <= 0.) ML_ERR_return_NAN;

    R_Q_P01_check(p);
    if (p == R_DT_0)
	return 0;

    /* fudge the extreme DF cases -- qbeta doesn't do this well */

    if (n2 > 4e5)
	return qchisq(p, n1, lower_tail, log_p) / n1;

    if (n1 > 4e5)
	return 1/qchisq(p, n2, !lower_tail, log_p) * n2;

    p = (1. / qbeta(R_DT_CIv(p), n2/2, n1/2, TRUE, FALSE) - 1.) * (n2 / n1);
    return ML_VALID(p) ? p : ML_NAN;
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pchisq.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998	Ross Ihaka
 *  Copyright (C) 2000	The R Development Core Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 *
 *  DESCRIPTION
 *
 *     The distribution function of the chi-squared distribution.
 */


gnum_float pchisq(gnum_float x, gnum_float df, gboolean lower_tail, gboolean log_p)
{
    return pgamma(x, df / 2.0, 2.0, lower_tail, log_p);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qchisq.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  DESCRIPTION
 *
 *	The quantile function of the chi-squared distribution.
 */


gnum_float qchisq(gnum_float p, gnum_float df, gboolean lower_tail, gboolean log_p)
{
    return qgamma(p, 0.5 * df, 2.0, lower_tail, log_p);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dweibull.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  DESCRIPTION
 *
 *    The density function of the Weibull distribution.
 */


gnum_float dweibull(gnum_float x, gnum_float shape, gnum_float scale, gboolean give_log)
{
    gnum_float tmp1, tmp2;
#ifdef IEEE_754
    if (isnangnum(x) || isnangnum(shape) || isnangnum(scale))
	return x + shape + scale;
#endif
    if (shape <= 0 || scale <= 0) ML_ERR_return_NAN;

    if (x < 0) return R_D__0;
    if (!finitegnum(x)) return R_D__0;
    tmp1 = powgnum(x / scale, shape - 1);
    tmp2 = tmp1 * (x / scale);
    return  give_log ?
	-tmp2 + loggnum(shape * tmp1 / scale) :
	shape * tmp1 * expgnum(-tmp2) / scale;
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/pbinom.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  DESCRIPTION
 *
 *    The distribution function of the binomial distribution.
 */

gnum_float pbinom(gnum_float x, gnum_float n, gnum_float p, gboolean lower_tail, gboolean log_p)
{
#ifdef IEEE_754
    if (isnangnum(x) || isnangnum(n) || isnangnum(p))
	return x + n + p;
    if (!finitegnum(n) || !finitegnum(p)) ML_ERR_return_NAN;

#endif
    n = floorgnum(n + 0.5);
    if(n <= 0 || p < 0 || p > 1) ML_ERR_return_NAN;

    x = floorgnum(x);
    if (x < 0.0) return R_DT_0;
    if (n <= x) return R_DT_1;
    return pbeta(1 - p, n - x, x + 1, lower_tail, log_p);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dbinom.c from R.  */
/*
 * AUTHOR
 *   Catherine Loader, catherine@research.bell-labs.com.
 *   October 23, 2000.
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *
 * DESCRIPTION
 *
 *   To compute the binomial probability, call dbinom(x,n,p).
 *   This checks for argument validity, and calls dbinom_raw().
 *
 *   dbinom_raw() does the actual computation; note this is called by
 *   other functions in addition to dbinom()).
 *     (1) dbinom_raw() has both p and q arguments, when one may be represented
 *         more accurately than the other (in particular, in df()).
 *     (2) dbinom_raw() does NOT check that inputs x and n are integers. This
 *         should be done in the calling function, where necessary.
 *     (3) Also does not check for 0 <= p <= 1 and 0 <= q <= 1 or NaN's.
 *         Do this in the calling function.
 */


static gnum_float dbinom_raw(gnum_float x, gnum_float n, gnum_float p, gnum_float q, gboolean give_log)
{
    gnum_float f, lc;

    if (p == 0) return((x == 0) ? R_D__1 : R_D__0);
    if (q == 0) return((x == n) ? R_D__1 : R_D__0);

    if (x == 0) {
	if(n == 0) return R_D__1;
	lc = (p < 0.1) ? -bd0(n,n*q) - n*p : n*loggnum(q);
	return( R_D_exp(lc) );
    }
    if (x == n) {
	lc = (q < 0.1) ? -bd0(n,n*p) - n*q : n*loggnum(p);
	return( R_D_exp(lc) );
    }
    if (x < 0 || x > n) return( R_D__0 );

    lc = stirlerr(n) - stirlerr(x) - stirlerr(n-x) - bd0(x,n*p) - bd0(n-x,n*q);
    f = (M_2PIgnum*x*(n-x))/n;

    return R_D_fexp(f,lc);
}

gnum_float dbinom(gnum_float x, gnum_float n, gnum_float p, gboolean give_log)
{
#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (isnangnum(x) || isnangnum(n) || isnangnum(p)) return x + n + p;
#endif

  if (p < 0 || p > 1 || R_D_notnnegint(n)) ML_ERR_return_NAN;
  R_D_nonint_check(x);

  n = R_D_forceint(n);
  x = R_D_forceint(x);

  return dbinom_raw(x,n,p,1-p,give_log);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/qbinom.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 *
 *  DESCRIPTION
 *
 *	The quantile function of the binomial distribution.
 *
 *  METHOD
 *
 *	Uses the Cornish-Fisher Expansion to include a skewness
 *	correction to a normal approximation.  This gives an
 *	initial value which never seems to be off by more than
 *	1 or 2.	 A search is then conducted of values close to
 *	this initial start point.
 */


gnum_float qbinom(gnum_float p, gnum_float n, gnum_float pr, gboolean lower_tail, gboolean log_p)
{
    gnum_float q, mu, sigma, gamma, z, y;

#ifdef IEEE_754
    if (isnangnum(p) || isnangnum(n) || isnangnum(pr))
	return p + n + pr;
#endif
    if(!finitegnum(p) || !finitegnum(n) || !finitegnum(pr))
	ML_ERR_return_NAN;
    R_Q_P01_check(p);

    n = floorgnum(n + 0.5);
    if (pr <= 0 || pr >= 1 || n <= 0)
	ML_ERR_return_NAN;

    if (p == R_DT_0) return 0.;
    if (p == R_DT_1) return n;

    q = 1 - pr;
    mu = n * pr;
    sigma = sqrtgnum(n * pr * q);
    gamma = (q - pr) / sigma;

#ifdef DEBUG_qbinom
    REprintf("qbinom(p=%7" GNUM_FORMAT_g ", n=%" GNUM_FORMAT_g ", pr=%7" GNUM_FORMAT_g ", l.t.=%d, log=%d): sigm=%" GNUM_FORMAT_g ", gam=%" GNUM_FORMAT_g "\n",
	     p,n,pr, lower_tail, log_p, sigma, gamma);
#endif
    /* Note : "same" code in qpois.c, qbinom.c, qnbinom.c --
     * FIXME: This is far from optimal [cancellation for p ~= 1, etc]: */
    if(!lower_tail || log_p) {
	p = R_DT_qIv(p); /* need check again (cancellation!): */
	if (p == 0.) return 0.;
	if (p == 1.) return n;
    }
    /* temporary hack --- FIXME --- */
    if (p + 1.01*GNUM_EPSILON >= 1.) return n;

    /* y := approx.value (Cornish-Fisher expansion) :  */
    z = qnorm(p, 0., 1., /*lower_tail*/TRUE, /*log_p*/FALSE);
    y = floorgnum(mu + sigma * (z + gamma * (z*z - 1) / 6) + 0.5);
    if(y > n) /* way off */ y = n;

#ifdef DEBUG_qbinom
    REprintf("  new (p,1-p)=(%7" GNUM_FORMAT_g ",%7" GNUM_FORMAT_g "), z=qnorm(..)=%7" GNUM_FORMAT_g ", y=%5" GNUM_FORMAT_g "\n", p, 1-p, z, y);
#endif
    z = pbinom(y, n, pr, /*lower_tail*/TRUE, /*log_p*/FALSE);

    /* fuzz to ensure left continuity: */
    p *= 1 - 64*GNUM_EPSILON;

/*-- Fixme, here y can be way off --
  should use interval search instead of primitive stepping down or up */

#ifdef maybe_future
    if((lower_tail && z >= p) || (!lower_tail && z <= p)) {
#else
    if(z >= p) {
#endif
			/* search to the left */
#ifdef DEBUG_qbinom
	REprintf("\tnew z=%7" GNUM_FORMAT_g " >= p = %7" GNUM_FORMAT_g "  --> search to left (y--) ..\n", z,p);
#endif
	for(;;) {
	    if(y == 0 ||
	       (z = pbinom(y - 1, n, pr, /*l._t.*/TRUE, /*log_p*/FALSE)) < p)
		return y;
	    y = y - 1;
	}
    }
    else {		/* search to the right */
#ifdef DEBUG_qbinom
	REprintf("\tnew z=%7" GNUM_FORMAT_g " < p = %7" GNUM_FORMAT_g "  --> search to right (y++) ..\n", z,p);
#endif
	for(;;) {
	    y = y + 1;
	    if(y == n ||
	       (z = pbinom(y, n, pr, /*l._t.*/TRUE, /*log_p*/FALSE)) >= p)
		return y;
	}
    }
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/dexp.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  DESCRIPTION
 *
 *	The density of the exponential distribution.
 */


gnum_float dexp(gnum_float x, gnum_float scale, gboolean give_log)
{
#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (isnangnum(x) || isnangnum(scale)) return x + scale;
#endif
    if (scale <= 0.0) ML_ERR_return_NAN;

    if (x < 0.)
	return R_D__0;
    return (give_log ?
	    (-x / scale) - loggnum(scale) :
	    expgnum(-x / scale) / scale);
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/bessel.h from R.  */

/* Constants und Documentation that apply to several of the
 * ./bessel_[ijky].c  files */

/* *******************************************************************

 Explanation of machine-dependent constants

   beta	  = Radix for the floating-point system
   minexp = Smallest representable power of beta
   maxexp = Smallest power of beta that overflows
   it = p = Number of bits (base-beta digits) in the mantissa
	    (significand) of a working precision (floating-point) variable
   NSIG	  = Decimal significance desired.  Should be set to
	    INT(LOG10(2)*it+1).	 Setting NSIG lower will result
	    in decreased accuracy while setting NSIG higher will
	    increase CPU time without increasing accuracy.  The
	    truncation error is limited to a relative error of
	    T=.5*10^(-NSIG).
   ENTEN  = 10 ^ K, where K is the largest long such that
	    ENTEN is machine-representable in working precision
   ENSIG  = 10 ^ NSIG
   RTNSIG = 10 ^ (-K) for the smallest long K such that
	    K >= NSIG/4
   ENMTEN = Smallest ABS(X) such that X/4 does not underflow
   XINF	  = Largest positive machine number; approximately beta ^ maxexp
	    == GNUM_MAX (defined in  #include <float.h>)
   SQXMIN = Square root of beta ^ minexp = sqrtgnum(GNUM_MIN)

   EPS	  = The smallest positive floating-point number such that 1.0+EPS > 1.0
	  = beta ^ (-p)	 == GNUM_EPSILON


  For I :

   EXPARG = Largest working precision argument that the library
	    EXP routine can handle and upper limit on the
	    magnitude of X when IZE=1; approximately LOG(beta ^ maxexp)

  For I and J :

   xlrg_IJ = (was = XLARGE). Upper limit on the magnitude of X (when
	    IZE=2 for I()).  Bear in mind that if ABS(X)=N, then at least
	    N iterations of the backward recursion will be executed.
	    The value of 10 ^ 4 is used on every machine.

  For j :
   XMIN_J  = Smallest acceptable argument for RBESY; approximately
	    max(2*beta ^ minexp, 2/XINF), rounded up

  For Y :

   xlrg_Y =  (was = XLARGE). Upper bound on X;
	    approximately 1/DEL, because the sine and cosine functions
	    have lost about half of their precision at that point.

   EPS_SINC = Machine number below which singnum(x)/x = 1; approximately SQRT(EPS).
   THRESH = Lower bound for use of the asymptotic form;
	    approximately AINT(-LOG10(EPS/2.0))+1.0


  For K :

   xmax_k =  (was = XMAX). Upper limit on the magnitude of X when ize = 1;
	    i.e. maximal x for UNscaled answer.

	    Solution to equation:
	       W(X) * (1 -1/8 X + 9/128 X^2) = beta ^ minexp
	    where  W(X) = EXP(-X)*SQRT(PI/2X)

 --------------------------------------------------------------------

     Approximate values for some important machines are:

		  beta minexp maxexp it NSIG ENTEN ENSIG RTNSIG ENMTEN	 EXPARG
 IEEE (IBM/XT,
   SUN, etc.) (S.P.)  2	  -126	128  24	  8  1e38   1e8	  1e-2	4.70e-38     88
 IEEE	(...) (D.P.)  2	 -1022 1024  53	 16  1e308  1e16  1e-4	8.90e-308   709
 CRAY-1	      (S.P.)  2	 -8193 8191  48	 15  1e2465 1e15  1e-4	1.84e-2466 5677
 Cyber 180/855
   under NOS  (S.P.)  2	  -975 1070  48	 15  1e322  1e15  1e-4	1.25e-293   741
 IBM 3033     (D.P.) 16	   -65	 63  14	  5  1e75   1e5	  1e-2	2.16e-78    174
 VAX	      (S.P.)  2	  -128	127  24	  8  1e38   1e8	  1e-2	1.17e-38     88
 VAX D-Format (D.P.)  2	  -128	127  56	 17  1e38   1e17  1e-5	1.17e-38     88
 VAX G-Format (D.P.)  2	 -1024 1023  53	 16  1e307  1e16  1e-4	2.22e-308   709


And routine specific :

		    xlrg_IJ xlrg_Y xmax_k EPS_SINC XMIN_J    XINF   THRESH
 IEEE (IBM/XT,
   SUN, etc.) (S.P.)	1e4  1e4   85.337  1e-4	 2.36e-38   3.40e38	8.
 IEEE	(...) (D.P.)	1e4  1e8  705.342  1e-8	 4.46e-308  1.79e308   16.
 CRAY-1	      (S.P.)	1e4  2e7 5674.858  5e-8	 3.67e-2466 5.45e2465  15.
 Cyber 180/855
   under NOS  (S.P.)	1e4  2e7  672.788  5e-8	 6.28e-294  1.26e322   15.
 IBM 3033     (D.P.)	1e4  1e8  177.852  1e-8	 2.77e-76   7.23e75    17.
 VAX	      (S.P.)	1e4  1e4   86.715  1e-4	 1.18e-38   1.70e38	8.
 VAX e-Format (D.P.)	1e4  1e9   86.715  1e-9	 1.18e-38   1.70e38    17.
 VAX G-Format (D.P.)	1e4  1e8  706.728  1e-8	 2.23e-308  8.98e307   16.

*/
#define nsig_BESS	16
#define ensig_BESS	1e16
#define rtnsig_BESS	1e-4
#define enmten_BESS	8.9e-308
#define enten_BESS	1e308

#define exparg_BESS	709.
#define xlrg_BESS_IJ	1e4
#define xlrg_BESS_Y	1e8
#define thresh_BESS_Y	16.

#define xmax_BESS_K	705.342/* maximal x for UNscaled answer */


/* sqrtgnum(GNUM_MIN) =	1.491668e-154 */
#define sqxmin_BESS_K	1.49e-154

/* x < eps_sinc	 <==>  singnum(x)/x == 1 (particularly "==>");
  Linux (around 2001-02) gives GNUM_const(2.14946906753213e-08)
  Solaris 2.5.1		 gives GNUM_const(2.14911933289084e-08)
*/
#define M_eps_sinc	2.149e-8

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/bessel_i.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998-2001 Ross Ihaka and the R Development Core team.
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*  DESCRIPTION --> see below */


/* From http://www.netlib.org/specfun/ribesl	Fortran translated by f2c,...
 *	------------------------------=#----	Martin Maechler, ETH Zurich
 */

#ifndef MATHLIB_STANDALONE
#endif

static void I_bessel(gnum_float *x, gnum_float *alpha, long *nb,
		     long *ize, gnum_float *bi, long *ncalc);

gnum_float bessel_i(gnum_float x, gnum_float alpha, gnum_float expo)
{
    long nb, ncalc, ize;
    gnum_float *bi;
#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (isnangnum(x) || isnangnum(alpha)) return x + alpha;
#endif
    if (x < 0) {
	ML_ERROR(ME_RANGE);
	return ML_NAN;
    }
    ize = (long)expo;
    if (alpha < 0) {
	/* Using Abramowitz & Stegun  9.6.2
	 * this may not be quite optimal (CPU and accuracy wise) */
	return(bessel_i(x, -alpha, expo) +
	       bessel_k(x, -alpha, expo) * ((ize == 1)? 2. : 2.*expgnum(-x))/M_PIgnum
	       * singnum(-M_PIgnum * alpha));
    }
    nb = 1+ (long)floorgnum(alpha);/* nb-1 <= alpha < nb */
    alpha -= (nb-1);
#ifdef MATHLIB_STANDALONE
    bi = (gnum_float *) calloc(nb, sizeof(gnum_float));
    if (!bi) MATHLIB_ERROR("%s", "bessel_i allocation error");
#else
    bi = (gnum_float *) R_alloc(nb, sizeof(gnum_float));
#endif
    I_bessel(&x, &alpha, &nb, &ize, bi, &ncalc);
    if(ncalc != nb) {/* error input */
	if(ncalc < 0)
	    MATHLIB_WARNING4("bessel_i(%" GNUM_FORMAT_g "): ncalc (=%ld) != nb (=%ld); alpha=%" GNUM_FORMAT_g "."
			     " Arg. out of range?\n",
			     x, ncalc, nb, alpha);
	else
	    MATHLIB_WARNING2("bessel_i(%" GNUM_FORMAT_g ",nu=%" GNUM_FORMAT_g "): precision lost in result\n",
			     x, alpha+nb-1);
    }
    x = bi[nb-1];
#ifdef MATHLIB_STANDALONE
    free(bi);
#endif
    return x;
}

static void I_bessel(gnum_float *x, gnum_float *alpha, long *nb,
		     long *ize, gnum_float *bi, long *ncalc)
{
/* -------------------------------------------------------------------

 This routine calculates Bessel functions I_(N+ALPHA) (X)
 for non-negative argument X, and non-negative order N+ALPHA,
 with or without exponential scaling.


 Explanation of variables in the calling sequence

 X     - Non-negative argument for which
	 I's or exponentially scaled I's (I*EXP(-X))
	 are to be calculated.	If I's are to be calculated,
	 X must be less than EXPARG_BESS (see bessel.h).
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
     NB <= 0, IZE is not 1 or 2, or IZE=1 and ABS(X) >= EXPARG_BESS.
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
    const gnum_float const__ = 1.585;

    /* Local variables */
    long nend, intx, nbmx, k, l, n, nstart;
    gnum_float pold, test,	p, em, en, empal, emp2al, halfx,
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
	if((*ize == 1 && *x > exparg_BESS) ||
	   (*ize == 2 && *x > xlrg_BESS_IJ)) {
	    ML_ERROR(ME_RANGE);
	    for(k=1; k <= *nb; k++)
		bi[k]=ML_POSINF;
	    return;
	}
	intx = (long) (*x);/* --> we will probably fail when *x > LONG_MAX */
	if (*x >= rtnsig_BESS) { /* "non-small" x */
/* -------------------------------------------------------------------
   Initialize the forward sweep, the P-sequence of Olver
   ------------------------------------------------------------------- */
	    nbmx = *nb - intx;
	    n = intx + 1;
	    en = (gnum_float) (n + n) + twonu;
	    plast = 1.;
	    p = en / *x;
	    /* ------------------------------------------------
	       Calculate general significance test
	       ------------------------------------------------ */
	    test = ensig_BESS + ensig_BESS;
	    if (intx << 1 > nsig_BESS * 5) {
		test = sqrtgnum(test * p);
	    } else {
		test /= powgnum(const__, (gnum_float)intx);
	    }
	    if (nbmx >= 3) {
		/* --------------------------------------------------
		   Calculate P-sequence until N = NB-1
		   Check for possible overflow.
		   ------------------------------------------------ */
		tover = enten_BESS / ensig_BESS;
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
			tover = enten_BESS;
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
			test = pold * plast / ensig_BESS;
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
		en = (gnum_float)(n + n) + twonu;
		/*---------------------------------------------------
		  Calculate special significance test for NBMX > 2.
		  --------------------------------------------------- */
		test = fmax2(test,sqrtgnum(plast * ensig_BESS) * sqrtgnum(p + p));
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
	    em = (gnum_float) n - 1.;
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
		sum *= (gamma_cody(1. + nu) * powgnum(*x * .5, -nu));
	    if (*ize == 1)
		sum *= expgnum(-(*x));
	    aa = enmten_BESS;
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
	    if (*x > enmten_BESS)
		halfx = .5 * *x;
	    else
		halfx = 0.;
	    if (nu != 0.)
		aa = powgnum(halfx, nu) / gamma_cody(empal);
	    if (*ize == 2)
		aa *= expgnum(-(*x));
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
		    tover = (enmten_BESS + enmten_BESS) / *x;
		    if (bb != 0.)
			tover = enmten_BESS / bb;
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

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/bessel_k.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998-2001 Ross Ihaka and the R Development Core team.
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*  DESCRIPTION --> see below */


/* From http://www.netlib.org/specfun/rkbesl	Fortran translated by f2c,...
 *	------------------------------=#----	Martin Maechler, ETH Zurich
 */

#ifndef MATHLIB_STANDALONE
#endif

static void K_bessel(gnum_float *x, gnum_float *alpha, long *nb,
		     long *ize, gnum_float *bk, long *ncalc);

gnum_float bessel_k(gnum_float x, gnum_float alpha, gnum_float expo)
{
    long nb, ncalc, ize;
    gnum_float *bk;
#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (isnangnum(x) || isnangnum(alpha)) return x + alpha;
#endif
    if (x < 0) {
	ML_ERROR(ME_RANGE);
	return ML_NAN;
    }
    ize = (long)expo;
    if(alpha < 0)
	alpha = -alpha;
    nb = 1+ (long)floorgnum(alpha);/* nb-1 <= |alpha| < nb */
    alpha -= (nb-1);
#ifdef MATHLIB_STANDALONE
    bk = (gnum_float *) calloc(nb, sizeof(gnum_float));
    if (!bk) MATHLIB_ERROR("%s", "bessel_k allocation error");
#else
    bk = (gnum_float *) R_alloc(nb, sizeof(gnum_float));
#endif
    K_bessel(&x, &alpha, &nb, &ize, bk, &ncalc);
    if(ncalc != nb) {/* error input */
      if(ncalc < 0)
	MATHLIB_WARNING4("bessel_k(%" GNUM_FORMAT_g "): ncalc (=%ld) != nb (=%ld); alpha=%" GNUM_FORMAT_g ". Arg. out of range?\n",
			 x, ncalc, nb, alpha);
      else
	MATHLIB_WARNING2("bessel_k(%" GNUM_FORMAT_g ",nu=%" GNUM_FORMAT_g "): precision lost in result\n",
			 x, alpha+nb-1);
    }
    x = bk[nb-1];
#ifdef MATHLIB_STANDALONE
    free(bk);
#endif
    return x;
}

static void K_bessel(gnum_float *x, gnum_float *alpha, long *nb,
		     long *ize, gnum_float *bk, long *ncalc)
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
	 X must not be greater than XMAX_BESS_K.
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
	NB <= 0, IZE is not 1 or 2, or IZE=1 and ABS(X) >= XMAX_BESS_K.
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
    /*---------------------------------------------------------------------
     * Mathematical constants
     *	A = LOG(2) - Euler's constant
     *	D = SQRT(2/PI)
     ---------------------------------------------------------------------*/
    const gnum_float a = GNUM_const(.11593151565841244881);

    /*---------------------------------------------------------------------
      P, Q - Approximation for LOG(GAMMA(1+ALPHA))/ALPHA + Euler's constant
      Coefficients converted from hex to decimal and modified
      by W. J. Cody, 2/26/82 */
    const gnum_float p[8] = { GNUM_const(.805629875690432845),GNUM_const(20.4045500205365151),
	    GNUM_const(157.705605106676174),GNUM_const(536.671116469207504),GNUM_const(900.382759291288778),
	    GNUM_const(730.923886650660393),GNUM_const(229.299301509425145),GNUM_const(.822467033424113231) };
    const gnum_float q[7] = { GNUM_const(29.4601986247850434),GNUM_const(277.577868510221208),
	    GNUM_const(1206.70325591027438),GNUM_const(2762.91444159791519),GNUM_const(3443.74050506564618),
	    GNUM_const(2210.63190113378647),GNUM_const(572.267338359892221) };
    /* R, S - Approximation for (1-ALPHA*PI/SIN(ALPHA*PI))/(2.D0*ALPHA) */
    const gnum_float r[5] = { GNUM_const(-.48672575865218401848),GNUM_const(13.079485869097804016),
	    GNUM_const(-101.96490580880537526),GNUM_const(347.65409106507813131),
	    GNUM_const(3.495898124521934782e-4) };
    const gnum_float s[4] = { GNUM_const(-25.579105509976461286),GNUM_const(212.57260432226544008),
	    GNUM_const(-610.69018684944109624),GNUM_const(422.69668805777760407) };
    /* T    - Approximation for SINH(Y)/Y */
    const gnum_float t[6] = { GNUM_const(1.6125990452916363814e-10),
	    GNUM_const(2.5051878502858255354e-8),GNUM_const(2.7557319615147964774e-6),
	    GNUM_const(1.9841269840928373686e-4),GNUM_const(.0083333333333334751799),
	    GNUM_const(.16666666666666666446) };
    /*---------------------------------------------------------------------*/
    const gnum_float estm[6] = { 52.0583,5.7607,2.7782,14.4303,185.3004, 9.3715 };
    const gnum_float estf[7] = { 41.8341,7.1075,6.4306,42.511,1.35633,84.5096,20.};

    /* Local variables */
    long iend, i, j, k, m, ii, mplus1;
    gnum_float x2by4, twox, c, blpha, ratio, wminf;
    gnum_float d1, d2, d3, f0, f1, f2, p0, q0, t1, t2, twonu;
    gnum_float dm, ex, bk1, bk2, nu;

    ii = 0; /* -Wall */

    ex = *x;
    nu = *alpha;
    *ncalc = imin2(*nb,0) - 2;
    if (*nb > 0 && (0. <= nu && nu < 1.) && (1 <= *ize && *ize <= 2)) {
	if(ex <= 0 || (*ize == 1 && ex > xmax_BESS_K)) {
	    ML_ERROR(ME_RANGE);
	    *ncalc = *nb;
	    for(i=0; i < *nb; i++)
		bk[i] = ML_POSINF;
	    return;
	}
	k = 0;
	if (nu < sqxmin_BESS_K) {
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
	    f1 = loggnum(ex);
	    f0 = a + nu * (p[7] - nu * (d1 + d2) / (t1 + t2)) - f1;
	    q0 = expgnum(-nu * (a - nu * (p[7] + nu * (d1-d2) / (t1-t2)) - f1));
	    f1 = nu * f0;
	    p0 = expgnum(f1);
	    /* -----------------------------------------------------------
	       Calculation of F0 =
	       ----------------------------------------------------------- */
	    d1 = r[4];
	    t1 = 1.;
	    for (i = 0; i < 4; ++i) {
		d1 = c * d1 + r[i];
		t1 = c * t1 + s[i];
	    }
	    /* d2 := sinhgnum(f1)/ nu = sinhgnum(f1)/(f1/f0)
	     *	   = f0 * sinhgnum(f1)/f1 */
	    if (gnumabs(f1) <= .5) {
		f1 *= f1;
		d2 = 0.;
		for (i = 0; i < 6; ++i) {
		    d2 = f1 * d2 + t[i];
		}
		d2 = f0 + f0 * f1 * d2;
	    } else {
		d2 = sinhgnum(f1) / nu;
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
		c = ex * GNUM_MAX;
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
		} while (gnumabs(t1 / (f1 + bk1)) > GNUM_EPSILON ||
			 gnumabs(t2 / (f2 + bk2)) > GNUM_EPSILON);
		bk1 = f1 + bk1;
		bk2 = 2. * (f2 + bk2) / ex;
		if (*ize == 2) {
		    d1 = expgnum(ex);
		    bk1 *= d1;
		    bk2 *= d1;
		}
		wminf = estf[0] * ex + estf[1];
	    }
	} else if (GNUM_EPSILON * ex > 1.) {
	    /* -------------------------------------------------
	       X > 1./EPS
	       ------------------------------------------------- */
	    *ncalc = *nb;
	    bk1 = 1. / (M_SQRT_2dPI * sqrtgnum(ex));
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
		d2 = floor(estm[0] / ex + estm[1]);
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
		d2 = floor(estm[2] * ex + estm[3]);
		m = (long) d2;
		c = gnumabs(nu);
		d3 = c + c;
		d1 = d3 - 1.;
		f1 = GNUM_MIN;
		f0 = (2. * (c + d2) / ex + .5 * ex / (c + d2 + 1.)) * GNUM_MIN;
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
		p0 = expgnum(c * (a + c * (p[7] - c * d1 / t1) - loggnum(ex))) / ex;
		f2 = (c + .5 - ratio) * f1 / ex;
		bk1 = p0 + (d3 * f0 - f2 + f0 + blpha) / (f2 + f1 + f0) * p0;
		if (*ize == 1) {
		    bk1 *= expgnum(-ex);
		}
		wminf = estf[2] * ex + estf[3];
	    } else {
		/* ---------------------------------------------------------
		   Calculation of K(ALPHA,X) and K(ALPHA+1,X)/K(ALPHA,X), by
		   backward recurrence, for  X > 4.0
		   ----------------------------------------------------------*/
		dm = floor(estm[4] / ex + estm[5]);
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
		bk1 = 1. / ((M_SQRT_2dPI + M_SQRT_2dPI * blpha) * sqrtgnum(ex));
		if (*ize == 1)
		    bk1 *= expgnum(-ex);
		wminf = estf[4] * (ex - gnumabs(ex - estf[6])) + estf[5];
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
		if (bk1 >= GNUM_MAX / twonu * ex)
		    break;
	    } else {
		if (bk1 / ex >= GNUM_MAX / twonu)
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
		if (bk2 >= GNUM_MAX / ratio)
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
	    if (bk[i-1] >= GNUM_MAX / bk[i])
		return;
#endif
	    bk[i] *= bk[i-1];
	    (*ncalc)++;
	}
    }
}

/* ------------------------------------------------------------------------ */
/* --- END MAGIC R SOURCE MARKER --- */

/* FIXME: we need something that catches partials and EAGAIN.  */
#define fullread read

#define RANDOM_DEVICE "dev/urandom"

/*
 * Conservative random number generator.  The result is (supposedly) uniform
 * and between 0 and 1.  (0 possible, 1 not.)  The result should have about
 * 64 bits randomness.
 */
gnum_float
random_01 (void)
{
	static int device_fd = -2;

	if (device_fd == -2) {
		device_fd = open (RANDOM_DEVICE, O_RDONLY);
		/*
		 * We could check that we really have a device, but it hard
		 * to come up with a non-paranoid reason to.
		 */
	}

	if (device_fd >= 0) {
		unsigned data[sizeof (gnum_float)];

		if (fullread (device_fd, &data, sizeof (gnum_float)) == sizeof (gnum_float)) {
			gnum_float res = 0;
			size_t i;

			for (i = 0; i < sizeof (gnum_float); i++)
				res = (res + data[i]) / 256;
			return res;
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

		return (r1 + (r2 + (r3 + r4 / (gnum_float)prime) / prime) / prime) / prime;
	}
#endif
}

/*
 * Generate a N(0,1) distributed number.
 */
gnum_float
random_normal (void)
{
	return qnorm (random_01 (), 0, 1, TRUE, FALSE);
}

/*
 * Generate a poisson distributed number.
 */
gnum_float
random_poisson (gnum_float lambda)
{
        gnum_float x = expgnum (-lambda);
	gnum_float r = random_01 ();
	gnum_float t = x;
	gnum_float i = 0;

	/* FIXME: Looks dubious, performanc-wise.  */
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
gnum_float
random_binomial (gnum_float p, int trials)
{
        gnum_float x = powgnum (1 - p, trials);
	gnum_float r = random_01 ();
	gnum_float t = x;
	gnum_float i = 0;

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
gnum_float
random_negbinom (gnum_float p, int f)
{
        gnum_float x = powgnum (p, f);
	gnum_float r = random_01 ();
	gnum_float t = x;
	gnum_float i = 0;

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
gnum_float
random_exponential (gnum_float b)
{
        return -1 * b * loggnum (random_01 ());
}

/*
 * Generate a bernoulli distributed number.
 */
gnum_float
random_bernoulli (gnum_float p)
{
        gnum_float r = random_01 ();

	return (r <= p) ? 1.0 : 0.0;
}

/*
 * Generate 2^n being careful not to overflow
 */
gnum_float
gpow2 (int n)
{
	g_assert (FLT_RADIX == 2);

	if (n >= DBL_MIN_EXP && n <= DBL_MAX_EXP)
		return (gnum_float) ldexp (1.0, n);
	else {
		gnum_float tmp;

		tmp = gpow2 (n / 2);
		tmp *= tmp;
		if (n & 1)
			tmp *= 2;
		return tmp;
	}
}


/*
 * Generate 10^n being careful not to overflow
 */
gnum_float
gpow10 (int n)
{
	gnum_float res = 1.0;
	gnum_float p;
	const int maxn = GNUM_MAX_EXP;

	static const gnum_float fast[] = {
		GNUM_const (1e-20),
		GNUM_const (1e-19),
		GNUM_const (1e-18),
		GNUM_const (1e-17),
		GNUM_const (1e-16),
		GNUM_const (1e-15),
		GNUM_const (1e-14),
		GNUM_const (1e-13),
		GNUM_const (1e-12),
		GNUM_const (1e-11),
		GNUM_const (1e-10),
		GNUM_const (1e-9),
		GNUM_const (1e-8),
		GNUM_const (1e-7),
		GNUM_const (1e-6),
		GNUM_const (1e-5),
		GNUM_const (1e-4),
		GNUM_const (1e-3),
		GNUM_const (1e-2),
		GNUM_const (1e-1),
		GNUM_const (1),
		GNUM_const (1e1),
		GNUM_const (1e2),
		GNUM_const (1e3),
		GNUM_const (1e4),
		GNUM_const (1e5),
		GNUM_const (1e6),
		GNUM_const (1e7),
		GNUM_const (1e8),
		GNUM_const (1e9),
		GNUM_const (1e10),
		GNUM_const (1e11),
		GNUM_const (1e12),
		GNUM_const (1e13),
		GNUM_const (1e14),
		GNUM_const (1e15),
		GNUM_const (1e16),
		GNUM_const (1e17),
		GNUM_const (1e18),
		GNUM_const (1e19),
		GNUM_const (1e20)
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

		res = expgnum (lgamma (n + 1) - lgamma (k + 1) - lgamma (n - k + 1));
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
