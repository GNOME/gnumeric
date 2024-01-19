#include <gnumeric-config.h>
#include <sf-bessel.h>
#include <sf-gamma.h>
#include <sf-trig.h>
#include <mathfunc.h>

#define IEEE_754
#define MATHLIB_STANDALONE
#define ML_ERROR(cause) do { } while(0)
#define MATHLIB_ERROR(_a,_b) return gnm_nan;
#define M_SQRT_2dPI     GNM_const(0.797884560802865355879892119869)  /* sqrt(2/pi) */
#define MATHLIB_WARNING2 g_warning
#define MATHLIB_WARNING4 g_warning
#define ML_WARNING(typ,what) g_printerr("sf-bessel: trouble in %s\n", (what))

static gnm_float bessel_k(gnm_float x, gnm_float alpha, gnm_float expo);

static inline int imin2 (int x, int y) { return MIN (x, y); }
static inline int imax2 (int x, int y) { return MAX (x, y); }
static inline gnm_float fmax2 (gnm_float x, gnm_float y) { return MAX (x, y); }


/* ------------------------------------------------------------------------- */
/* --- BEGIN MAGIC R SOURCE MARKER --- */

// The following source code was imported from the R project.
// It was automatically transformed by tools/import-R.

/* Imported src/nmath/bessel.h from R.  */
/*
 *  R : A Computer Language for Statistical Data Analysis
 *  Copyright (C) 2001-2014  R Core Team
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
 */

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
   ENTEN  = 10 ^ K, where K is the largest int such that
	    ENTEN is machine-representable in working precision
   ENSIG  = 10 ^ NSIG
   RTNSIG = 10 ^ (-K) for the smallest int K such that K >= NSIG/4
   ENMTEN = Smallest ABS(X) such that X/4 does not underflow
   XINF	  = Largest positive machine number; approximately beta ^ maxexp
	    == DBL_MAX (defined in  #include <float.h>)
   SQXMIN = Square root of beta ^ minexp = sqrt(DBL_MIN)

   EPS	  = The smallest positive floating-point number such that 1.0+EPS > 1.0
	  = beta ^ (-p)	 == DBL_EPSILON


  For I :

   EXPARG = Largest working precision argument that the library
	    EXP routine can handle and upper limit on the
	    magnitude of X when IZE=1; approximately LOG(beta ^ maxexp)

  For I and J :

   xlrg_IJ = xlrg_BESS_IJ (was = XLARGE). Upper limit on the magnitude of X
	    (when IZE=2 for I()).  Bear in mind that if floor(abs(x)) =: N, then
	    at least N iterations of the backward recursion will be executed.
	    The value of 10 ^ 4 was used till Feb.2009, when it was increased
	    to 10 ^ 5 (= 1e5).

  For j :
   XMIN_J  = Smallest acceptable argument for RBESY; approximately
	    max(2*beta ^ minexp, 2/XINF), rounded up

  For Y :

   xlrg_Y =  (was = XLARGE). Upper bound on X;
	    approximately 1/DEL, because the sine and cosine functions
	    have lost about half of their precision at that point.

   EPS_SINC = Machine number below which sin(x)/x = 1; approximately SQRT(EPS).
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
#define ensig_BESS	GNM_const(1e16)
#define rtnsig_BESS	GNM_const(1e-4)
#define enmten_BESS	GNM_const(8.9e-308)
#define enten_BESS	GNM_const(1e308)

#define exparg_BESS	GNM_const(709.)
#define xlrg_BESS_IJ	GNM_const(1e5)
#define xlrg_BESS_Y	GNM_const(1e8)
#define thresh_BESS_Y	GNM_const(16.)

#define xmax_BESS_K	GNM_const(705.342)/* maximal x for UNscaled answer */


/* sqrt(DBL_MIN) =	1.491668e-154 */
#define sqxmin_BESS_K	GNM_const(1.49e-154)

/* x < eps_sinc	 <==>  sin(x)/x == 1 (particularly "==>");
  Linux (around 2001-02) gives 2.14946906753213e-08
  Solaris 2.5.1		 gives 2.14911933289084e-08
*/
#define M_eps_sinc	GNM_const(2.149e-8)

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/bessel_i.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998-2014 Ross Ihaka and the R Core team.
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
 */

/*  DESCRIPTION --> see below */


/* From http://www.netlib.org/specfun/ribesl	Fortran translated by f2c,...
 *	------------------------------=#----	Martin Maechler, ETH Zurich
 */

#ifndef MATHLIB_STANDALONE
#endif

#define min0(x, y) (((x) <= (y)) ? (x) : (y))

static void I_bessel(gnm_float *x, gnm_float *alpha, int *nb,
		     int *ize, gnm_float *bi, int *ncalc);

/* .Internal(besselI(*)) : */
static gnm_float bessel_i(gnm_float x, gnm_float alpha, gnm_float expo)
{
    int nb, ncalc, ize;
    gnm_float na, *bi;
#ifndef MATHLIB_STANDALONE
    const void *vmax;
#endif

#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(alpha)) return x + alpha;
#endif
    if (x < 0) {
	ML_WARNING(ME_RANGE, "bessel_i");
	return gnm_nan;
    }
    ize = (int)expo;
    na = gnm_floor(alpha);
    if (alpha < 0) {
	/* Using Abramowitz & Stegun  9.6.2 & 9.6.6
	 * this may not be quite optimal (CPU and accuracy wise) */
	return(bessel_i(x, -alpha, expo) +
	       ((alpha == na) ? /* sin(pi * alpha) = 0 */ 0 :
		bessel_k(x, -alpha, expo) *
		((ize == 1)? GNM_const(2.) : GNM_const(2.)*gnm_exp(GNM_const(-2.)*x))/M_PIgnum * gnm_sinpi(-alpha)));
    }
    nb = 1 + (int)na;/* nb-1 <= alpha < nb */
    alpha -= (gnm_float)(nb-1);
#ifdef MATHLIB_STANDALONE
    bi = (gnm_float *) calloc(nb, sizeof(gnm_float));
    if (!bi) MATHLIB_ERROR("%s", ("bessel_i allocation error"));
#else
    vmax = vmaxget();
    bi = (gnm_float *) R_alloc((size_t) nb, sizeof(gnm_float));
#endif
    I_bessel(&x, &alpha, &nb, &ize, bi, &ncalc);
    if(ncalc != nb) {/* error input */
	if(ncalc < 0)
	    MATHLIB_WARNING4(("bessel_i(%" GNM_FORMAT_g "): ncalc (=%d) != nb (=%d); alpha=%" GNM_FORMAT_g ". Arg. out of range?\n"),
			     x, ncalc, nb, alpha);
	else
	    MATHLIB_WARNING2(("bessel_i(%" GNM_FORMAT_g ",nu=%" GNM_FORMAT_g "): precision lost in result\n"),
			     x, alpha+(gnm_float)nb-1);
    }
    x = bi[nb-1];
#ifdef MATHLIB_STANDALONE
    free(bi);
#else
    vmaxset(vmax);
#endif
    return x;
}

/* modified version of bessel_i that accepts a work array instead of
   allocating one. */
/* Definition of function bessel_i_ex removed.  */

static void I_bessel(gnm_float *x, gnm_float *alpha, int *nb,
		     int *ize, gnm_float *bi, int *ncalc)
{
/* -------------------------------------------------------------------

 This routine calculates Bessel functions I_(N+ALPHA) (X)
 for non-negative argument X, and non-negative order N+ALPHA,
 with or without exponential scaling.


 Explanation of variables in the calling sequence

 X     - Non-negative argument for which
	 I's or exponentially scaled I's (I*EXP(-X))
	 are to be calculated.	If I's are to be calculated,
	 X must be less than exparg_BESS (IZE=1) or xlrg_BESS_IJ (IZE=2),
	 (see bessel.h).
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
    static const gnm_float const__ = GNM_const(1.585);

    /* Local variables */
    int nend, intx, nbmx, k, l, n, nstart;
    gnm_float pold, test,	p, em, en, empal, emp2al, halfx,
	aa, bb, cc, psave, plast, tover, psavel, sum, nu, twonu;

    /*Parameter adjustments */
    --bi;
    nu = *alpha;
    twonu = nu + nu;

    /*-------------------------------------------------------------------
      Check for X, NB, OR IZE out of range.
      ------------------------------------------------------------------- */
    if (*nb > 0 && *x >= 0 &&	(GNM_const(0.) <= nu && nu < 1) &&
	(1 <= *ize && *ize <= 2) ) {

	*ncalc = *nb;
	if(*ize == 1 && *x > exparg_BESS) {
	    for(k=1; k <= *nb; k++)
		bi[k]=gnm_pinf; /* the limit *is* = Inf */
	    return;
	}
	if(*ize == 2 && *x > xlrg_BESS_IJ) {
	    for(k=1; k <= *nb; k++)
		bi[k]= GNM_const(0.); /* The limit exp(-x) * I_nu(x) --> 0 : */
	    return;
	}
	intx = (int) (*x);/* fine, since *x <= xlrg_BESS_IJ <<< LONG_MAX */
	if (*x >= rtnsig_BESS) { /* "non-small" x ( >= 1e-4 ) */
/* -------------------------------------------------------------------
   Initialize the forward sweep, the P-sequence of Olver
   ------------------------------------------------------------------- */
	    nbmx = *nb - intx;
	    n = intx + 1;
	    en = (gnm_float) (n + n) + twonu;
	    plast = 1;
	    p = en / *x;
	    /* ------------------------------------------------
	       Calculate general significance test
	       ------------------------------------------------ */
	    test = ensig_BESS + ensig_BESS;
	    if (intx << 1 > nsig_BESS * 5) {
		test = gnm_sqrt(test * p);
	    } else {
		test /= gnm_pow(const__, intx);
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
		    en += 2;
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
			    en += 2;
			    pold = plast;
			    plast = p;
			    p = en * plast / *x + pold;
			}
			while (p <= 1);

			bb = en / *x;
			/* ------------------------------------------------
			   Calculate backward test, and find NCALC,
			   the highest N such that the test is passed.
			   ------------------------------------------------ */
			test = pold * plast / ensig_BESS;
			test *= GNM_const(.5) - GNM_const(.5) / (bb * bb);
			p = plast * tover;
			--n;
			en -= 2;
			nend = min0(*nb,n);
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
		en = (gnm_float)(n + n) + twonu;
		/*---------------------------------------------------
		  Calculate special significance test for NBMX > 2.
		  --------------------------------------------------- */
		test = fmax2(test,gnm_sqrt(plast * ensig_BESS) * gnm_sqrt(p + p));
	    }
	    /* --------------------------------------------------------
	       Calculate P-sequence until significance test passed.
	       -------------------------------------------------------- */
	    do {
		++n;
		en += 2;
		pold = plast;
		plast = p;
		p = en * plast / *x + pold;
	    } while (p < test);

L120:
/* -------------------------------------------------------------------
 Initialize the backward recursion and the normalization sum.
 ------------------------------------------------------------------- */
	    ++n;
	    en += 2;
	    bb = 0;
	    aa = GNM_const(1.) / p;
	    em = (gnm_float) n - GNM_const(1.);
	    empal = em + nu;
	    emp2al = em - GNM_const(1.) + twonu;
	    sum = aa * empal * emp2al / em;
	    nend = n - *nb;
	    if (nend < 0) {
		/* -----------------------------------------------------
		   N < NB, so store BI[N] and set higher orders to 0..
		   ----------------------------------------------------- */
		bi[n] = aa;
		nend = -nend;
		for (l = 1; l <= nend; ++l) {
		    bi[n + l] = 0;
		}
	    } else {
		if (nend > 0) {
		    /* -----------------------------------------------------
		       Recur backward via difference equation,
		       calculating (but not storing) BI[N], until N = NB.
		       --------------------------------------------------- */

		    for (l = 1; l <= nend; ++l) {
			--n;
			en -= 2;
			cc = bb;
			bb = aa;
			/* for x ~= 1500,  sum would overflow to 'inf' here,
			 * and the final bi[] /= sum would give 0 wrongly;
			 * RE-normalize (aa, sum) here -- no need to undo */
			if(nend > 100 && aa > GNM_const(1e200)) {
			    /* multiply by  2^-900 = 1.18e-271 */
			    cc	= ldexp(cc, -900);
			    bb	= ldexp(bb, -900);
			    sum = ldexp(sum,-900);
			}
			aa = en * bb / *x + cc;
			em -= 1;
			emp2al -= 1;
			if (n == 1) {
			    break;
			}
			if (n == 2) {
			    emp2al = 1;
			}
			empal -= 1;
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
		en -= 2;
		bi[n] = en * aa / *x + bb;
		if (n == 1) {
		    goto L220;
		}
		em -= 1;
		if (n == 2)
		    emp2al = 1;
		else
		    emp2al -= 1;

		empal -= 1;
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
		    en -= 2;
		    bi[n] = en * bi[n + 1] / *x + bi[n + 2];
		    em -= 1;
		    if (n == 2)
			emp2al = 1;
		    else
			emp2al -= 1;
		    empal -= 1;
		    sum = (sum + bi[n] * empal) * emp2al / em;
		}
	    }
	    /* ----------------------------------------------
	       Calculate BI[1]
	       -------------------------------------------- */
	    bi[1] = GNM_const(2.) * empal * bi[2] / *x + bi[3];
L220:
	    sum = sum + sum + bi[1];

L230:
	    /* ---------------------------------------------------------
	       Normalize.  Divide all BI[N] by sum.
	       --------------------------------------------------------- */
	    if (nu != 0)
		sum *= (gnm_gamma(GNM_const(1.) + nu) * gnm_pow(*x * GNM_const(.5), -nu));
	    if (*ize == 1)
		sum *= gnm_exp(-(*x));
	    aa = enmten_BESS;
	    if (sum > 1)
		aa *= sum;
	    for (n = 1; n <= *nb; ++n) {
		if (bi[n] < aa)
		    bi[n] = 0;
		else
		    bi[n] /= sum;
	    }
	    return;
	} else { /* small x  < 1e-4 */
	    /* -----------------------------------------------------------
	       Two-term ascending series for small X.
	       -----------------------------------------------------------*/
	    aa = 1;
	    empal = GNM_const(1.) + nu;
#ifdef IEEE_754
	    /* No need to check for underflow */
	    halfx = GNM_const(.5) * *x;
#else
	    if (*x > enmten_BESS) */
		halfx = GNM_const(.5) * *x;
	    else
		halfx = 0;
#endif
	    if (nu != 0)
		aa = gnm_pow(halfx, nu) / gnm_gamma(empal);
	    if (*ize == 2)
		aa *= gnm_exp(-(*x));
	    bb = halfx * halfx;
	    bi[1] = aa + aa * bb / empal;
	    if (*x != 0 && bi[1] == 0)
		*ncalc = 0;
	    if (*nb > 1) {
		if (*x == 0) {
		    for (n = 2; n <= *nb; ++n)
			bi[n] = 0;
		} else {
		    /* -------------------------------------------------
		       Calculate higher-order functions.
		       ------------------------------------------------- */
		    cc = halfx;
		    tover = (enmten_BESS + enmten_BESS) / *x;
		    if (bb != 0)
			tover = enmten_BESS / bb;
		    for (n = 2; n <= *nb; ++n) {
			aa /= empal;
			empal += 1;
			aa *= cc;
			if (aa <= tover * empal)
			    bi[n] = aa = 0;
			else
			    bi[n] = aa + aa * bb / empal;
			if (bi[n] == 0 && *ncalc > n)
			    *ncalc = n - 1;
		    }
		}
	    }
	}
    } else { /* argument out of range */
	*ncalc = min0(*nb,0) - 1;
    }
}
/* Cleaning up done by tools/import-R:  */
#undef min0

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/bessel_k.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998-2014 Ross Ihaka and the R Core team.
 *  Copyright (C) 2002-3    The R Foundation
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
 */

/*  DESCRIPTION --> see below */


/* From http://www.netlib.org/specfun/rkbesl	Fortran translated by f2c,...
 *	------------------------------=#----	Martin Maechler, ETH Zurich
 */

#ifndef MATHLIB_STANDALONE
#endif

#define min0(x, y) (((x) <= (y)) ? (x) : (y))
#define max0(x, y) (((x) <= (y)) ? (y) : (x))

static void K_bessel(gnm_float *x, gnm_float *alpha, int *nb,
		     int *ize, gnm_float *bk, int *ncalc);

static gnm_float bessel_k(gnm_float x, gnm_float alpha, gnm_float expo)
{
    int nb, ncalc, ize;
    gnm_float *bk;
#ifndef MATHLIB_STANDALONE
    const void *vmax;
#endif

#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(alpha)) return x + alpha;
#endif
    if (x < 0) {
	ML_WARNING(ME_RANGE, "bessel_k");
	return gnm_nan;
    }
    ize = (int)expo;
    if(alpha < 0)
	alpha = -alpha;
    nb = 1+ (int)gnm_floor(alpha);/* nb-1 <= |alpha| < nb */
    alpha -= (gnm_float)(nb-1);
#ifdef MATHLIB_STANDALONE
    bk = (gnm_float *) calloc(nb, sizeof(gnm_float));
    if (!bk) MATHLIB_ERROR("%s", ("bessel_k allocation error"));
#else
    vmax = vmaxget();
    bk = (gnm_float *) R_alloc((size_t) nb, sizeof(gnm_float));
#endif
    K_bessel(&x, &alpha, &nb, &ize, bk, &ncalc);
    if(ncalc != nb) {/* error input */
      if(ncalc < 0)
	MATHLIB_WARNING4(("bessel_k(%" GNM_FORMAT_g "): ncalc (=%d) != nb (=%d); alpha=%" GNM_FORMAT_g ". Arg. out of range?\n"),
			 x, ncalc, nb, alpha);
      else
	MATHLIB_WARNING2(("bessel_k(%" GNM_FORMAT_g ",nu=%" GNM_FORMAT_g "): precision lost in result\n"),
			 x, alpha+(gnm_float)nb-1);
    }
    x = bk[nb-1];
#ifdef MATHLIB_STANDALONE
    free(bk);
#else
    vmaxset(vmax);
#endif
    return x;
}

/* modified version of bessel_k that accepts a work array instead of
   allocating one. */
/* Definition of function bessel_k_ex removed.  */

static void K_bessel(gnm_float *x, gnm_float *alpha, int *nb,
		     int *ize, gnm_float *bk, int *ncalc)
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
    static const gnm_float a = GNM_const(.11593151565841244881);

    /*---------------------------------------------------------------------
      P, Q - Approximation for LOG(GAMMA(1+ALPHA))/ALPHA + Euler's constant
      Coefficients converted from hex to decimal and modified
      by W. J. Cody, 2/26/82 */
    static const gnm_float p[8] = { GNM_const(.805629875690432845),GNM_const(20.4045500205365151),
	    GNM_const(157.705605106676174),GNM_const(536.671116469207504),GNM_const(900.382759291288778),
	    GNM_const(730.923886650660393),GNM_const(229.299301509425145),GNM_const(.822467033424113231) };
    static const gnm_float q[7] = { GNM_const(29.4601986247850434),GNM_const(277.577868510221208),
	    GNM_const(1206.70325591027438),GNM_const(2762.91444159791519),GNM_const(3443.74050506564618),
	    GNM_const(2210.63190113378647),GNM_const(572.267338359892221) };
    /* R, S - Approximation for (1-ALPHA*PI/SIN(ALPHA*PI))/(2.D0*ALPHA) */
    static const gnm_float r[5] = { GNM_const(-.48672575865218401848),GNM_const(13.079485869097804016),
	    GNM_const(-101.96490580880537526),GNM_const(347.65409106507813131),
	    GNM_const(3.495898124521934782e-4) };
    static const gnm_float s[4] = { GNM_const(-25.579105509976461286),GNM_const(212.57260432226544008),
	    GNM_const(-610.69018684944109624),GNM_const(422.69668805777760407) };
    /* T    - Approximation for SINH(Y)/Y */
    static const gnm_float t[6] = { GNM_const(1.6125990452916363814e-10),
	    GNM_const(2.5051878502858255354e-8),GNM_const(2.7557319615147964774e-6),
	    GNM_const(1.9841269840928373686e-4),GNM_const(.0083333333333334751799),
	    GNM_const(.16666666666666666446) };
    /*---------------------------------------------------------------------*/
    static const gnm_float estm[6] = { GNM_const(52.0583),GNM_const(5.7607),GNM_const(2.7782),GNM_const(14.4303),GNM_const(185.3004), GNM_const(9.3715) };
    static const gnm_float estf[7] = { GNM_const(41.8341),GNM_const(7.1075),GNM_const(6.4306),GNM_const(42.511),GNM_const(1.35633),GNM_const(84.5096),GNM_const(20.)};

    /* Local variables */
    int iend, i, j, k, m, ii, mplus1;
    gnm_float x2by4, twox, c, blpha, ratio, wminf;
    gnm_float d1, d2, d3, f0, f1, f2, p0, q0, t1, t2, twonu;
    gnm_float dm, ex, bk1, bk2, nu;

    ii = 0; /* -Wall */

    ex = *x;
    nu = *alpha;
    *ncalc = min0(*nb,0) - 2;
    if (*nb > 0 && (GNM_const(0.) <= nu && nu < 1) && (1 <= *ize && *ize <= 2)) {
	if(ex <= 0 || (*ize == 1 && ex > xmax_BESS_K)) {
	    if(ex <= 0) {
		if(ex < 0) ML_WARNING(ME_RANGE, "K_bessel");
		for(i=0; i < *nb; i++)
		    bk[i] = gnm_pinf;
	    } else /* would only have underflow */
		for(i=0; i < *nb; i++)
		    bk[i] = 0;
	    *ncalc = *nb;
	    return;
	}
	k = 0;
	if (nu < sqxmin_BESS_K) {
	    nu = 0;
	} else if (nu > GNM_const(.5)) {
	    k = 1;
	    nu -= 1;
	}
	twonu = nu + nu;
	iend = *nb + k - 1;
	c = nu * nu;
	d3 = -c;
	if (ex <= 1) {
	    /* ------------------------------------------------------------
	       Calculation of P0 = GAMMA(1+ALPHA) * (2/X)**ALPHA
			      Q0 = GAMMA(1-ALPHA) * (X/2)**ALPHA
	       ------------------------------------------------------------ */
	    d1 = 0; d2 = p[0];
	    t1 = 1; t2 = q[0];
	    for (i = 2; i <= 7; i += 2) {
		d1 = c * d1 + p[i - 1];
		d2 = c * d2 + p[i];
		t1 = c * t1 + q[i - 1];
		t2 = c * t2 + q[i];
	    }
	    d1 = nu * d1;
	    t1 = nu * t1;
	    f1 = gnm_log(ex);
	    f0 = a + nu * (p[7] - nu * (d1 + d2) / (t1 + t2)) - f1;
	    q0 = gnm_exp(-nu * (a - nu * (p[7] + nu * (d1-d2) / (t1-t2)) - f1));
	    f1 = nu * f0;
	    p0 = gnm_exp(f1);
	    /* -----------------------------------------------------------
	       Calculation of F0 =
	       ----------------------------------------------------------- */
	    d1 = r[4];
	    t1 = 1;
	    for (i = 0; i < 4; ++i) {
		d1 = c * d1 + r[i];
		t1 = c * t1 + s[i];
	    }
	    /* d2 := sinh(f1)/ nu = sinh(f1)/(f1/f0)
	     *	   = f0 * sinh(f1)/f1 */
	    if (gnm_abs(f1) <= GNM_const(.5)) {
		f1 *= f1;
		d2 = 0;
		for (i = 0; i < 6; ++i) {
		    d2 = f1 * d2 + t[i];
		}
		d2 = f0 + f0 * f1 * d2;
	    } else {
		d2 = gnm_sinh(f1) / nu;
	    }
	    f0 = d2 - nu * d1 / (t1 * p0);
	    if (ex <= GNM_const(1e-10)) {
		/* ---------------------------------------------------------
		   X <= 1.0E-10
		   Calculation of K(ALPHA,X) and X*K(ALPHA+1,X)/K(ALPHA,X)
		   --------------------------------------------------------- */
		bk[0] = f0 + ex * f0;
		if (*ize == 1) {
		    bk[0] -= ex * bk[0];
		}
		ratio = p0 / f0;
		c = ex * GNM_MAX;
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
		    twonu += 2;
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
		    twonu += 2;
		    ratio = twonu;
		}
		*ncalc = 1;
		goto L420;
	    } else {
		/* ------------------------------------------------------
		   10^-10 < X <= 1.0
		   ------------------------------------------------------ */
		c = 1;
		x2by4 = ex * ex / GNM_const(4.);
		p0 = GNM_const(.5) * p0;
		q0 = GNM_const(.5) * q0;
		d1 = GNM_const(-1.);
		d2 = 0;
		bk1 = 0;
		bk2 = 0;
		f1 = f0;
		f2 = p0;
		do {
		    d1 += 2;
		    d2 += 1;
		    d3 = d1 + d3;
		    c = x2by4 * c / d2;
		    f0 = (d2 * f0 + p0 + q0) / d3;
		    p0 /= d2 - nu;
		    q0 /= d2 + nu;
		    t1 = c * f0;
		    t2 = c * (p0 - d2 * f0);
		    bk1 += t1;
		    bk2 += t2;
		} while (gnm_abs(t1 / (f1 + bk1)) > GNM_EPSILON ||
			 gnm_abs(t2 / (f2 + bk2)) > GNM_EPSILON);
		bk1 = f1 + bk1;
		bk2 = GNM_const(2.) * (f2 + bk2) / ex;
		if (*ize == 2) {
		    d1 = gnm_exp(ex);
		    bk1 *= d1;
		    bk2 *= d1;
		}
		wminf = estf[0] * ex + estf[1];
	    }
	} else if (GNM_EPSILON * ex > 1) {
	    /* -------------------------------------------------
	       X > 1./EPS
	       ------------------------------------------------- */
	    *ncalc = *nb;
	    bk1 = GNM_const(1.) / (M_SQRT_2dPI * gnm_sqrt(ex));
	    for (i = 0; i < *nb; ++i)
		bk[i] = bk1;
	    return;

	} else {
	    /* -------------------------------------------------------
	       X > 1.0
	       ------------------------------------------------------- */
	    twox = ex + ex;
	    blpha = 0;
	    ratio = 0;
	    if (ex <= 4) {
		/* ----------------------------------------------------------
		   Calculation of K(ALPHA+1,X)/K(ALPHA,X),  1.0 <= X <= 4.0
		   ----------------------------------------------------------*/
		d2 = gnm_trunc(estm[0] / ex + estm[1]);
		m = (int) d2;
		d1 = d2 + d2;
		d2 -= GNM_const(.5);
		d2 *= d2;
		for (i = 2; i <= m; ++i) {
		    d1 -= 2;
		    d2 -= d1;
		    ratio = (d3 + d2) / (twox + d1 - ratio);
		}
		/* -----------------------------------------------------------
		   Calculation of I(|ALPHA|,X) and I(|ALPHA|+1,X) by backward
		   recurrence and K(ALPHA,X) from the wronskian
		   -----------------------------------------------------------*/
		d2 = gnm_trunc(estm[2] * ex + estm[3]);
		m = (int) d2;
		c = gnm_abs(nu);
		d3 = c + c;
		d1 = d3 - GNM_const(1.);
		f1 = GNM_MIN;
		f0 = (GNM_const(2.) * (c + d2) / ex + GNM_const(.5) * ex / (c + d2 + GNM_const(1.))) * GNM_MIN;
		for (i = 3; i <= m; ++i) {
		    d2 -= 1;
		    f2 = (d3 + d2 + d2) * f0;
		    blpha = (GNM_const(1.) + d1 / d2) * (f2 + blpha);
		    f2 = f2 / ex + f1;
		    f1 = f0;
		    f0 = f2;
		}
		f1 = (d3 + GNM_const(2.)) * f0 / ex + f1;
		d1 = 0;
		t1 = 1;
		for (i = 1; i <= 7; ++i) {
		    d1 = c * d1 + p[i - 1];
		    t1 = c * t1 + q[i - 1];
		}
		p0 = gnm_exp(c * (a + c * (p[7] - c * d1 / t1) - gnm_log(ex))) / ex;
		f2 = (c + GNM_const(.5) - ratio) * f1 / ex;
		bk1 = p0 + (d3 * f0 - f2 + f0 + blpha) / (f2 + f1 + f0) * p0;
		if (*ize == 1) {
		    bk1 *= gnm_exp(-ex);
		}
		wminf = estf[2] * ex + estf[3];
	    } else {
		/* ---------------------------------------------------------
		   Calculation of K(ALPHA,X) and K(ALPHA+1,X)/K(ALPHA,X), by
		   backward recurrence, for  X > 4.0
		   ----------------------------------------------------------*/
		dm = gnm_trunc(estm[4] / ex + estm[5]);
		m = (int) dm;
		d2 = dm - GNM_const(.5);
		d2 *= d2;
		d1 = dm + dm;
		for (i = 2; i <= m; ++i) {
		    dm -= 1;
		    d1 -= 2;
		    d2 -= d1;
		    ratio = (d3 + d2) / (twox + d1 - ratio);
		    blpha = (ratio + ratio * blpha) / dm;
		}
		bk1 = GNM_const(1.) / ((M_SQRT_2dPI + M_SQRT_2dPI * blpha) * gnm_sqrt(ex));
		if (*ize == 1)
		    bk1 *= gnm_exp(-ex);
		wminf = estf[4] * (ex - gnm_abs(ex - estf[6])) + estf[5];
	    }
	    /* ---------------------------------------------------------
	       Calculation of K(ALPHA+1,X)
	       from K(ALPHA,X) and  K(ALPHA+1,X)/K(ALPHA,X)
	       --------------------------------------------------------- */
	    bk2 = bk1 + bk1 * (nu + GNM_const(.5) - ratio) / ex;
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

	m = min0((int) (wminf - nu),iend);
	for (i = 2; i <= m; ++i) {
	    t1 = bk1;
	    bk1 = bk2;
	    twonu += 2;
	    if (ex < 1) {
		if (bk1 >= GNM_MAX / twonu * ex)
		    break;
	    } else {
		if (bk1 / ex >= GNM_MAX / twonu)
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
	    twonu += 2;
	    ratio = twonu / ex + GNM_const(1.)/ratio;
	    ++j;
	    if (j >= 1) {
		bk[j] = ratio;
	    } else {
		if (bk2 >= GNM_MAX / ratio)
		    return;

		bk2 *= ratio;
	    }
	}
	*ncalc = max0(1, mplus1 - k);
	if (*ncalc == 1)
	    bk[0] = bk2;
	if (*nb == 1)
	    return;

L420:
	for (i = *ncalc; i < *nb; ++i) { /* i == *ncalc */
#ifndef IEEE_754
	    if (bk[i-1] >= GNM_MAX / bk[i])
		return;
#endif
	    bk[i] *= bk[i-1];
	    (*ncalc)++;
	}
    }
}
/* Cleaning up done by tools/import-R:  */
#undef max0
#undef min0

/* ------------------------------------------------------------------------ */
/* --- END MAGIC R SOURCE MARKER --- */

static gboolean
bessel_ij_series_domain (gnm_float x, gnm_float v)
{
	// The taylor series is valid for all possible values of x and v,
	// but it isn't efficient and practical for all.
	//
	// Since bessel_j is used for computing BesselY when v is not an
	// integer, we need the domain to be independent of the sign of v
	// for such v.
	//
	// That is a bit of a problem because when v < -0.5 and close
	// to an integer, |v+k| will get close to 0 and the terms will
	// suddenly jump in size.  The jump will not be larger than a
	// factor of 2^53, so as long as [-v]! is much larger than that
	// we do not have a problem.

	if (v < 0 && v == gnm_floor (v))
		return FALSE;

	// For non-negative v, the factorials will dominate after the
	// k'th term if x*x/4 < c*k(v+k).  Let's require two bit decreases
	// (c=0.25) from k=10.  We ignore the sign of v, see above.

	return (x * x / 4 < GNM_const(0.25) * 10 * (gnm_abs (v) + 10));
}


static GnmQuad
bessel_ij_series (gnm_float x, gnm_float v, gboolean qj)
{
	GnmQuad qv, qxh, qfv, qs, qt;
	int efv;
	void *state = gnm_quad_start ();
	gnm_float e, s;

	gnm_quad_init (&qxh, x / 2);
	gnm_quad_init (&qv, v);

	gnm_quad_pow (&qt, &e, &qxh, &qv);

	switch (qfactf (v, &qfv, &efv)) {
	case 0:
		gnm_quad_div (&qt, &qt, &qfv);
		e -= efv;
		break;
	case 1:
		qt = gnm_quad_zero;
		e = 0;
		break;
	default:
		gnm_quad_init (&qt, gnm_nan);
		e = 0;
		break;
	}

	qs = qt;
	s = gnm_quad_value (&qs);
	if (gnm_finite (s) && s != 0) {
		int k, mink = 5;
		GnmQuad qxh2;

		gnm_quad_mul (&qxh2, &qxh, &qxh);

		if (v < 0) {
			// Terms can get suddenly big for k ~ -v.
			gnm_float ltn0 = -v * (1 - M_LN2gnum + gnm_log (x / -v));
			if (ltn0 - (gnm_log (s) + e * M_LN2gnum) < gnm_log (GNM_EPSILON) - 10)
				mink = (int)(-v) + 5;
		}

		for (k = 1; k < 200; k++) {
			GnmQuad qa, qb;
			gnm_float t;

			gnm_quad_mul (&qt, &qt, &qxh2);
			gnm_quad_init (&qa, k);
			gnm_quad_add (&qb, &qv, &qa);
			gnm_quad_init (&qa, qj ? -k : k);
			gnm_quad_mul (&qa, &qa, &qb);
			gnm_quad_div (&qt, &qt, &qa);
			t = gnm_quad_value (&qt);
			if (t == 0)
				break;
			gnm_quad_add (&qs, &qs, &qt);
			s = gnm_quad_value (&qs);
			if (k >= mink &&
			    gnm_abs (t) <= GNM_EPSILON / (1 << 20) * gnm_abs (s))
				break;
		}
	}

	// We scale here at the end to avoid intermediate overflow
	// and underflow.

	// Clamp won't affect whether we get 0 or inf.
	e = CLAMP (e, G_MININT, G_MAXINT);
	gnm_quad_scalbn (&qs, &qs, (int)e);

	gnm_quad_end (state);

	return qs;
}

/* ------------------------------------------------------------------------ */

static const gnm_float legendre20_roots[(20 + 1) / 2] = {
	GNM_const(0.0765265211334973),
	GNM_const(0.2277858511416451),
	GNM_const(0.3737060887154195),
	GNM_const(0.5108670019508271),
	GNM_const(0.6360536807265150),
	GNM_const(0.7463319064601508),
	GNM_const(0.8391169718222188),
	GNM_const(0.9122344282513259),
	GNM_const(0.9639719272779138),
	GNM_const(0.9931285991850949)
};

static const gnm_float legendre20_wts[(20 + 1) / 2] = {
	GNM_const(0.1527533871307258),
	GNM_const(0.1491729864726037),
	GNM_const(0.1420961093183820),
	GNM_const(0.1316886384491766),
	GNM_const(0.1181945319615184),
	GNM_const(0.1019301198172404),
	GNM_const(0.0832767415767048),
	GNM_const(0.0626720483341091),
	GNM_const(0.0406014298003869),
	GNM_const(0.0176140071391521)
};

static const gnm_float legendre33_roots[(33 + 1) / 2] = {
	GNM_const(0.0000000000000000),
	GNM_const(0.0936310658547334),
	GNM_const(0.1864392988279916),
	GNM_const(0.2776090971524970),
	GNM_const(0.3663392577480734),
	GNM_const(0.4518500172724507),
	GNM_const(0.5333899047863476),
	GNM_const(0.6102423458363790),
	GNM_const(0.6817319599697428),
	GNM_const(0.7472304964495622),
	GNM_const(0.8061623562741665),
	GNM_const(0.8580096526765041),
	GNM_const(0.9023167677434336),
	GNM_const(0.9386943726111684),
	GNM_const(0.9668229096899927),
	GNM_const(0.9864557262306425),
	GNM_const(0.9974246942464552)
};

static const gnm_float legendre33_wts[(33 + 1) / 2] = {
	GNM_const(0.0937684461602100),
	GNM_const(0.0933564260655961),
	GNM_const(0.0921239866433168),
	GNM_const(0.0900819586606386),
	GNM_const(0.0872482876188443),
	GNM_const(0.0836478760670387),
	GNM_const(0.0793123647948867),
	GNM_const(0.0742798548439541),
	GNM_const(0.0685945728186567),
	GNM_const(0.0623064825303175),
	GNM_const(0.0554708466316636),
	GNM_const(0.0481477428187117),
	GNM_const(0.0404015413316696),
	GNM_const(0.0323003586323290),
	GNM_const(0.0239155481017495),
	GNM_const(0.0153217015129347),
	GNM_const(0.0066062278475874)
};

static const gnm_float legendre45_roots[(45 + 1) / 2] = {
	GNM_const(0.0000000000000000),
	GNM_const(0.0689869801631442),
	GNM_const(0.1376452059832530),
	GNM_const(0.2056474897832637),
	GNM_const(0.2726697697523776),
	GNM_const(0.3383926542506022),
	GNM_const(0.4025029438585419),
	GNM_const(0.4646951239196351),
	GNM_const(0.5246728204629161),
	GNM_const(0.5821502125693532),
	GNM_const(0.6368533944532233),
	GNM_const(0.6885216807712006),
	GNM_const(0.7369088489454904),
	GNM_const(0.7817843125939062),
	GNM_const(0.8229342205020863),
	GNM_const(0.8601624759606642),
	GNM_const(0.8932916717532418),
	GNM_const(0.9221639367190004),
	GNM_const(0.9466416909956291),
	GNM_const(0.9666083103968947),
	GNM_const(0.9819687150345405),
	GNM_const(0.9926499984472037),
	GNM_const(0.9986036451819367)
};

static const gnm_float legendre45_wts[(45 + 1) / 2] = {
	GNM_const(0.0690418248292320),
	GNM_const(0.0688773169776613),
	GNM_const(0.0683845773786697),
	GNM_const(0.0675659541636075),
	GNM_const(0.0664253484498425),
	GNM_const(0.0649681957507234),
	GNM_const(0.0632014400738199),
	GNM_const(0.0611335008310665),
	GNM_const(0.0587742327188417),
	GNM_const(0.0561348787597865),
	GNM_const(0.0532280167312690),
	GNM_const(0.0500674992379520),
	GNM_const(0.0466683877183734),
	GNM_const(0.0430468807091650),
	GNM_const(0.0392202367293025),
	GNM_const(0.0352066922016090),
	GNM_const(0.0310253749345155),
	GNM_const(0.0266962139675777),
	GNM_const(0.0222398475505787),
	GNM_const(0.0176775352579376),
	GNM_const(0.0130311049915828),
	GNM_const(0.0083231892962182),
	GNM_const(0.0035826631552836)
};

typedef gnm_complex (*ComplexIntegrand) (gnm_float x, const gnm_float *args);

static gnm_complex
complex_legendre_integral (size_t N,
			   gnm_float L, gnm_float H,
			   ComplexIntegrand f, const gnm_float *args)
{
	const gnm_float *roots;
	const gnm_float *wts;
	gnm_float m = (L + H) / 2;
	gnm_float s = (H - L) / 2;
	size_t i;
	gnm_complex I = GNM_C0;

	switch (N) {
	case 20:
		roots = legendre20_roots;
		wts = legendre20_wts;
		break;
	case 33:
		roots = legendre33_roots;
		wts = legendre33_wts;
		break;
	case 45:
		roots = legendre45_roots;
		wts = legendre45_wts;
		break;
	default:
		g_assert_not_reached ();
	}
	if (N & 1)
		g_assert (roots[0] == 0);

	for (i = 0; i < (N + 1) / 2; i++) {
		gnm_float r = roots[i];
		gnm_float w = wts[i];
		int neg;
		for (neg = 0; neg <= 1; neg++) {
			gnm_float u = neg ? m - s * r : m + s * r;
			gnm_complex dI = f (u, args);
			I = GNM_CADD (I, GNM_CSCALE (dI, w));
			if (i == 0 && (N & 1))
				break;
		}
	}
	return GNM_CSCALE (I, s);
}

// Trapezoid rule integraion for a complex function defined on a finite
// interval.  This breaks the interval into N uniform pieces.
static void
complex_trapezoid_integral (gnm_complex *res, size_t N,
			    gnm_float L, gnm_float H,
			    ComplexIntegrand f, const gnm_float *args)
{
	gnm_float s = (H - L) / N;
	size_t i;

	*res = GNM_C0;
	for (i = 0; i <= N; i++) {
		gnm_float u = L + i * s;
		gnm_complex dI = f (u, args);
		if (i == 0 || i == N)
			dI = GNM_CSCALE (dI, 0.5);
		*res = GNM_CADD (*res, dI);
	}
	*res = GNM_CSCALE (*res, s);
}

// Shrink integration range to exclude vanishing outer parts.
static void
complex_shink_integral_range (gnm_float *L, gnm_float *H, gnm_float refx,
			      ComplexIntegrand f,
			      const gnm_float *args)
{
	gnm_complex y;
	gnm_float refy, limL = refx, limH = refx;
	gboolean first;
	gboolean debug = FALSE;

	g_return_if_fail (*L <= *H);
	g_return_if_fail (*L <= refx && refx <= *H);

	y = f (refx, args);
	refy = GNM_CABS (y) * GNM_EPSILON;

	g_return_if_fail (!gnm_isnan (refy));

	if (debug)
		g_printerr ("Initial range: (%g,%g)  refx=%g  refy=%g\n",
			    *L, *H, refx, refy);

	first = TRUE;
	while (limL - *L > GNM_EPSILON) {
		gnm_float testx = first ? *L : (limL + *L) / 2;
		gnm_float testy;

		y = f (testx, args);
		testy = GNM_CABS (y);

		first = FALSE;
		if (testy <= refy) {
			*L = testx;
			if (testy >= refy / 16)
				break;
			continue;
		} else
			limL = testx;
	}

	first = TRUE;
	while (*H - limH > GNM_EPSILON) {
		gnm_float testx = first ? *H : (*H + limH) / 2;
		gnm_float testy;

		y = f (testx, args);
		testy = GNM_CABS (y);

		first = FALSE;
		if (testy <= refy) {
			*H = testx;
			if (testy >= refy / 16)
				break;
			continue;
		} else
			limH = testx;
	}

	if (debug)
		g_printerr ("Shrunk range: (%g,%g)\n", *L, *H);
}

typedef gnm_float (*Interpolant) (gnm_float x, const gnm_float *args);

static gnm_float
chebyshev_interpolant (size_t N, gnm_float L, gnm_float H, gnm_float x0,
		       Interpolant f, const gnm_float *args)
{
	size_t i, j, k;
	gnm_float *coeffs = g_new (gnm_float, N);
	gnm_float m = (L + H) / 2;
	gnm_float s = (H - L) / 2;
	gnm_float x0n = (x0 - m) / s;
	gnm_float res, dip1, dip2, di;

	for (j = 0; j < N; j++) {
		gnm_float cj = 0;
		for (k = 0; k < N; k++) {
			gnm_float zj = gnm_cospi ((k + GNM_const(0.5)) / N);
			gnm_float xj = m + s * zj;
			gnm_float fxj = f (xj, args);
			cj += fxj * gnm_cospi (j * (k + GNM_const(0.5)) / N);
		}
		coeffs[j] = 2 * cj / N;
	}

	dip1 = 0.0;
	di = 0.0;

	for (i = N - 1; i >= 1; i--) {
		dip2 = dip1;
		dip1 = di;
		di = 2 * x0n * dip1 - dip2 + coeffs[i];
	}
	res = x0n * di - dip1 + GNM_const(0.5) * coeffs[0];

	g_free (coeffs);

	if (0) g_printerr ("--> f(%g) = %.20g\n", x0, res);

	return res;
}


// Coefficient for the debye polynomial u_n
// Lowest coefficent will be for x^n
// Highest coefficent will be for x^(3n)
// Every second coefficent is zero and left out.
// The count of coefficents will thus be n+1.
static const gnm_float *
debye_u_coeffs (size_t n)
{
	static gnm_float **coeffs = NULL;
	static size_t nalloc = 0;

	if (n >= nalloc) {
		size_t i;
		coeffs = g_renew (gnm_float *, coeffs, n + 1);
		for (i = nalloc; i <= n; i++) {
			gnm_float *c = coeffs[i] = g_new0 (gnm_float, i + 1);
			gnm_float *l;
			size_t j;

			if (i == 0) {
				c[0] = 1;
				continue;
			} else if (i == 1) {
				c[0] = 1 / GNM_const(8.0);
				c[1] = -5 / GNM_const(24.0);
				continue;
			}

			l = coeffs[i - 1];

			for (j = i; j <= 3 * i; j += 2) {
				gnm_float k = 0;

				// .5tt u_[i-1]'
				if (j < 3 * i)
					k += GNM_const(0.5) * (j-1) * l[((j-1)-(i-1)) / 2];

				// -.5tttt u[i-1]'
				if (j > i)
					k -= GNM_const(0.5) * (j-3) * l[((j-3)-(i-1)) / 2];

				// 1/8*Int[u[i-1]]
				if (j < 3 * i)
					k += GNM_const(0.125) * l[((j-1)-(i-1)) / 2] / j;


				// -5/8*Int[tt*u[i-1]]
				if (j > i)
					k -= GNM_const(0.625) * l[((j-3)-(i-1)) / 2] / j;

				c[(j - i) / 2] = k;

				if (0)
					g_printerr ("Debye u_%d : %g * x^%d\n",
						    (int)i, k, (int)j);
			}
		}
		nalloc = n + 1;
	}

	return coeffs[n];
}

static gnm_complex
debye_u (size_t n, gnm_float p, gboolean qip)
{
	const gnm_float *coeffs = debye_u_coeffs (n);
	gnm_float pn = gnm_pow (p, n);
	gnm_float pp = qip ? -p * p : p * p;
	gnm_float s = 0;
	int i;

	for (i = 3 * n; i >= (int)n; i -= 2)
		s = s * pp + coeffs[(i - n) / 2];

	switch (qip ? n % 4 : 0) {
	case 0: return GNM_CREAL (s * pn);
	case 1: return GNM_CMAKE (0, s * pn);
	case 2: return GNM_CREAL (s * -pn);
	case 3: return GNM_CMAKE (0, s * -pn);
	default:
		g_assert_not_reached ();
	}
}


static gnm_float
gnm_sinv_m_v_cosv (gnm_float v, gnm_float sinv)
{
	// Deviation: Matviyenko uses direct formula for this for all v.

	if (v >= 1)
		return sinv - v * gnm_cos (v);
	else {
		gnm_float r = 0, t = -v;
		gnm_float vv = v * v;
		int i;

		for (i = 3; i < 100; i += 2) {
			t = -t * vv / (i * (i == 3 ? 1 : i - 3));
			r += t;
			if (gnm_abs (t) <= gnm_abs (r) * (GNM_EPSILON / 16))
				break;
		}

		if (0) {
			gnm_float ref = sinv - v * gnm_cos (v);
			g_printerr ("XXX: %g %g %g -- %g\n",
				    v, ref, r, r - ref);
		}

		return r;
	}
}

static gnm_float
gnm_sinhumu (gnm_float u)
{
	if (!gnm_finite (u))
		return u;
	else if (gnm_abs (u) >= 1)
		return gnm_sinh (u) - u;
	else {
		gnm_float uu = u * u;
		size_t i;
		gnm_float s = 0;
		gnm_float t = u;

		for (i = 3; i < 100; i += 2) {
			t *= uu / ((i - 1) * i);
			s += t;
			if (gnm_abs (t) <= gnm_abs (s) * (GNM_EPSILON / 16))
				break;
		}
		return s;
	}
}

/* ------------------------------------------------------------------------ */

static gnm_complex
debye_u_sum (gnm_float x, gnm_float nu,
	     size_t N, gboolean qalt, gboolean qip)
{
	size_t n;
	gnm_float f;
	gnm_float sqdiff = gnm_abs (x * x - nu * nu);
	gnm_float diff2 = gnm_sqrt (sqdiff);
	gnm_float p = nu / diff2;
	gnm_complex sum = GNM_C0;

	(void)debye_u_coeffs (N);

	f = 1;
	for (n = 0; n <= N; n++) {
		gnm_complex t;
		if (nu == 0) {
			// lim(p/nu,nu->0) = 1/x
			gnm_float q = debye_u_coeffs (n)[0] / gnm_pow (x, n);
			if (qip && (n & 2)) q = -q;
			if (qalt && (n & 1)) q = -q;
			if (qip && (n & 1))
				t = GNM_CMAKE (0, q);
			else
				t = GNM_CREAL (q);
		} else {
			t = debye_u (n, p, qip);
			t = GNM_CSCALE (t, f);
			f /= nu;
			if (qalt) f = -f;
		}
		sum = GNM_CADD (sum, t);
	}

	return sum;
}

static void
debye_29_eta1 (gnm_float x, gnm_float nu,
	       gnm_float *r1a, gnm_float *r1b, gnm_float *rpi)
{
	static const gnm_float c[] = {
		/*  2 */ 1 / (gnm_float)2,
		/*  4 */ 1 / (gnm_float)24,
		/*  6 */ 1 / (gnm_float)80,
		/*  8 */ 5 / (gnm_float)896,
		/* 10 */ 7 / (gnm_float)2304,
		/* 12 */ 21 / (gnm_float)11264,
		/* 14 */ 33 / (gnm_float)26624,
		/* 16 */ 143 / (gnm_float)163840,
		/* 18 */ 715 / (gnm_float)1114112,
		/* 20 */ 2431 / (gnm_float)4980736
	};

	gnm_float q = nu / x;

	// Deviation: we improve this formula for small nu / x by computing
	// eta1 in three parts such that eta1 = (*r1a + *r1b + Pi * *rpi)
	// with *r1a small and no rounding errors on the others.

	if (q < GNM_const(0.1)) {
		gnm_float r  = 0;
		gnm_float qq = q * q;
		unsigned ci;
		*rpi = -nu / 2 - GNM_const(0.25);

		for (ci = G_N_ELEMENTS(c); ci-- > 0; )
			r = r * qq + c[ci];
		*r1a = r * q * nu;
		*r1b = x;
	} else {
		*r1a = gnm_sqrt (x * x - nu * nu) - nu * gnm_acos (nu / x);
		*r1b = 0;
		*rpi = -0.25;
	}
}

static gnm_complex
debye_29 (gnm_float x, gnm_float nu, size_t N)
{
	gnm_float sqdiff = x * x - nu * nu;
	gnm_float eta1a, eta1b, eta1pi;
	gnm_float f1 = gnm_sqrt (2 / M_PIgnum) / gnm_pow (sqdiff, 0.25);
	gnm_complex sum, f12;

	debye_29_eta1 (x, nu, &eta1a, &eta1b, &eta1pi);

	f12 = GNM_CPOLAR (f1, eta1a);
	if (eta1b)
		f12 = GNM_CMUL (f12, GNM_CPOLAR (1, eta1b));
	f12 = GNM_CMUL (f12, GNM_CPOLARPI (1, eta1pi));
	sum = debye_u_sum (x, nu, N, TRUE, TRUE);
	return GNM_CMUL (f12, sum);
}

static gnm_float
debye_32 (gnm_float x, gnm_float nu, gnm_float eta2, size_t N)
{
	gnm_float sqdiff = nu * nu - x * x;
	gnm_float f = gnm_exp (-eta2) /
		(gnm_sqrt (2 * M_PIgnum) * gnm_pow (sqdiff, 0.25));
	gnm_float res;
	gnm_complex sum;

	sum = debye_u_sum (x, nu, N, FALSE, FALSE);
	res = f * sum.re;

	if (0)
		g_printerr ("D32(%g,%g) = %.20g\n", x, nu, res);

	return res;
}

static gnm_float
debye_33 (gnm_float x, gnm_float nu, gnm_float eta2, size_t N)
{
	gnm_float sqdiff = nu * nu - x * x;
	gnm_float f;
	gnm_float res;
	gnm_complex sum;
	gnm_float c = gnm_sqrt (2 / M_PIgnum);

	if (eta2 < gnm_log (GNM_MAX) - GNM_const(0.01))
		f = - c * gnm_exp (eta2) / gnm_pow (sqdiff, 0.25);
	else {
		// Near-overflow panic
		f = - gnm_exp (gnm_log (c) + eta2 - GNM_const(0.25) * gnm_log (sqdiff));
	}

	sum = debye_u_sum (x, nu, N, TRUE, FALSE);
	res = f * sum.re;

	if (0)
		g_printerr ("D33(%g,%g) = %.20g\n", x, nu, res);

	return res;
}

static gnm_float
integral_83_coshum1 (gnm_float d, gnm_float sinv, gnm_float cosv,
		     gnm_float sinbeta, gnm_float cosbeta)
{
	gnm_float r = 0, todd, teven, dd, cotv;
	int i;

	if (gnm_abs (d) > GNM_const(0.1))
		return (d * cosbeta - (sinv - sinbeta)) / sinv;

	cotv = cosv / sinv;
	dd = d * d;
	teven = 1;
	todd = d;
	for (i = 2; i < 100; i++) {
		gnm_float t;
		if (i & 1) {
			todd *= -dd / (i == 3 ? 3 : i * (i - 3));
			t = todd * cotv;
		} else {
			teven *= -dd / (i * (i - 3));
			t = teven;
		}
		r += t;
		if (gnm_abs (t) <= gnm_abs (r) * (GNM_EPSILON / 16))
			break;
	}

	if (0) {
		gnm_float ref = (d * cosbeta - (sinv - sinbeta)) / sinv;
		g_printerr ("coshum1(d=%g): %g %g\n", d, ref, r);
	}

	return r;
}

static gnm_float
integral_83_cosdiff (gnm_float d, gnm_float v,
		     gnm_float sinbeta, gnm_float cosbeta)
{
	gnm_float s = 0;
	gnm_float t = 1;
	size_t i;
	gboolean debug = FALSE;

	g_return_val_if_fail (gnm_abs (d) < 1, gnm_nan);

	for (i = 1; i < 100; i += 2) {
		t *= -d / i;
		s += sinbeta * t;
		t *= d / (i + 1);
		s += cosbeta * t;
		if (gnm_abs (t) <= gnm_abs (s) * (GNM_EPSILON / 16))
			break;
	}

	if (debug) {
		gnm_float ref = gnm_cos (v) - cosbeta;
		g_printerr ("cosdiff(d=%g): %g %g\n", d, ref, s);
	}

	return s;
}

static gnm_complex
integral_83_integrand (gnm_float v, gnm_float const *args)
{
	gnm_float x = args[0];
	gnm_float nu = args[1];
	gnm_float beta = args[2];
	gnm_float du_dv, phi1, xphi1;
	gnm_float sinv = gnm_sin (v);
	gboolean debug = FALSE;

	if (sinv <= 0) {
		// Either end
		du_dv = gnm_nan;
		phi1 = gnm_ninf;
	} else {
		gnm_float vmbeta = v - beta;
		gnm_float cosv = gnm_cos (v);
		gnm_float cosbeta = nu / x;
		gnm_float sinbeta = gnm_sqrt (1 - cosbeta * cosbeta);
		gnm_float coshum1 = integral_83_coshum1 (vmbeta, sinv, cosv,
							 sinbeta, cosbeta);
		gnm_float sinhu = gnm_sqrt (coshum1 * (coshum1 + 2));
		// Deviation: use coshum1 here to avoid cancellation
		gnm_float u = gnm_log1p (sinhu + coshum1);
		gnm_float du = (gnm_sin (v - beta) -
				(v - beta) * cosbeta * cosv);
		// Erratum: fix sign of u.  See Watson 8.32
		if (v < beta)
			u = -u, sinhu = -sinhu;
		if (gnm_abs (vmbeta) < GNM_const(0.1)) {
			phi1 = integral_83_cosdiff (vmbeta, v, sinbeta, cosbeta) * sinhu +
				gnm_sinhumu (u) * cosbeta;
		} else {
			phi1 = cosv * sinhu - cosbeta * u;
		}
		du_dv =  du ? du / (sinhu * sinv * sinv) : 0;
		if (debug) {
			g_printerr ("beta = %g\n", beta);
			g_printerr ("v-beta = %g\n", v-beta);
			g_printerr ("phi1 = %g\n", phi1);
			g_printerr ("du_dv = %g\n", du_dv);
			g_printerr ("coshum1 = %g\n", coshum1);
			g_printerr ("sinhu = %g\n", sinhu);
		}
	}

	xphi1 = x * phi1;
	if (xphi1 == gnm_ninf) {
		// "exp" wins.
		return GNM_C0;
	} else {
		gnm_float exphi1 = gnm_exp (xphi1);
		return GNM_CMAKE (du_dv * exphi1, exphi1);
	}
}

static gnm_complex
integral_83_alt_integrand (gnm_float t, gnm_float const *args)
{
	// v = t^vpow; dv/dt = vpow*t^(vpow-1)
	gnm_float vpow = args[3];
	return GNM_CSCALE (integral_83_integrand (gnm_pow (t, vpow), args),
			   vpow * gnm_pow (t, vpow - 1));
}

static gnm_complex
integral_83 (gnm_float x, gnm_float nu, size_t N, gnm_float vpow)
{
	// -i/Pi * exp(i*(x*sin(beta)-nu*beta)) *
	//    Integrate[(du/dv+i)*exp(x*phi1),{v,0,Pi}]
	//
	// beta = acos(nu/x)
	// u = acosh((sin(beta)+(v-beta)*cos(beta))/sin(v))
	// du/dv = (sin(v-beta)-(v-beta)*cos(beta)*cos(v)) /
	//            (sinh(u)*sin^2(v))
	// phi1 = cos(v)*sinh(u) - cos(beta)*u
	//
	// When vpow is not 1 we change variable as v=t^vpow numerically.
	// (I.e., the trapezoid points will be evenly spaced in t-space
	// instead of v-space.)

	// x >= 9 && nu < x - 1.5*crbt(x)

	gnm_complex I, f1;
	gnm_float beta = gnm_acos (nu / x);
	gnm_float xsinbeta = gnm_sqrt (x * x - nu * nu);
	gnm_float refx = beta;
	gnm_float L = 0;
	gnm_float H = M_PIgnum;
	gnm_float args[4] = { x, nu, beta, vpow };
	ComplexIntegrand integrand;

	complex_shink_integral_range (&L, &H, refx,
				      integral_83_integrand, args);

	if (vpow != 1) {
		L = gnm_pow (L, 1 / vpow);
		H = gnm_pow (H, 1 / vpow);
		integrand = integral_83_alt_integrand;
	} else {
		// We could use the indirect path above, but let's go direct
		integrand = integral_83_integrand;
	}

	complex_trapezoid_integral (&I, N, L, H, integrand, args);

	f1 = GNM_CPOLAR (1, xsinbeta - nu * beta);
	I = GNM_CMUL (I, f1);
	return GNM_CMUL (I, GNM_CMAKE (0, -1 / M_PIgnum));
}

static gnm_complex
integral_105_126_integrand (gnm_float u, gnm_float const *args)
{
	gnm_float x = args[0];
	gnm_float nu = args[1];
	return GNM_CREAL (gnm_exp (x * gnm_sinh (u) - nu * u));
}

static gnm_complex
integral_105_126 (gnm_float x, gnm_float nu, gboolean qH0)
{
	// -i/Pi * Integrate[Exp[x*Sinh[u]-nu*u],{u,-Infinity,H}]
	// where H is either 0 or alpha, see below.

	// Deviation: the analysis doesn't seem to consider the case where
	// nu < x which occurs for (126).  In that case the integrand takes
	// its maximum at 0.

	gnm_float args[2] = { x, nu };
	gnm_complex I;
	gnm_float refx = (nu < x) ? 0 : -gnm_acosh (nu / x);
	// For the nu~x case, we have sinh(u)-u = u^3/6 + ...
	gnm_float L = refx - MAX (gnm_cbrt (6 * 50 / ((nu + x) / 2)), 50 / MIN (nu, x));
	gnm_float H = qH0 ? 0 : -refx;

	complex_shink_integral_range (&L, &H, refx,
				      integral_105_126_integrand, args);

	I = complex_legendre_integral (45, L, H,
				       integral_105_126_integrand, args);

	return GNM_CMAKE (0, I.re / -M_PIgnum);
}

static gnm_complex
integral_106_integrand (gnm_float v, gnm_float const *args)
{
	gnm_float x = args[0];
	gnm_float nu = args[1];

	gnm_float sinv = gnm_sin (v);
	gnm_float coshalpha = nu / x;
	gnm_float coshu = coshalpha * (v ? (v / sinv) : 1);
	// FIXME: u and sinhu are dubious, numerically
	gnm_float u = gnm_acosh (coshu);
	gnm_float sinhu = gnm_sinh (u);
	gnm_float xphi3 = x * sinhu * gnm_cos (v) - nu * u;
	gnm_float exphi3 = gnm_exp (xphi3);

	gnm_float num = nu * gnm_sinv_m_v_cosv (v, sinv);
	gnm_float den = x * sinv * sinv * sinhu;
	gnm_float du_dv = v ? num / den : 0;

	return GNM_CMAKE (exphi3 * du_dv, exphi3);
}

static gnm_complex
integral_106 (gnm_float x, gnm_float nu)
{
	// -i/Pi * Integrate[Exp[x*phi3[v]]*(i+du/dv),{v,0,Pi}]
	//
	// alpha = acosh(nu/x)
	// u(v) = acosh(nu/x * v/sin(v))
	// du/dv = cosh(alpha)*(sin(v)-v*cos(v))/(sin^2(v)*sinh(u(v)))
	// phi3(v) = sinh(u)*cos(v) - cosh(alpha)*u(v)

	// Note: 2 < x < nu.

	gnm_complex I;
	gnm_float L = 0, H = M_PIgnum;
	gnm_float args[2] = { x, nu };

	complex_shink_integral_range (&L, &H, 0, integral_106_integrand, args);

	I = complex_legendre_integral (45, L, H,
				       integral_106_integrand, args);

	return GNM_CMUL (I, GNM_CMAKE (0, -1 / M_PIgnum));
}

static gnm_float
integral_127_u (gnm_float v)
{
	static const gnm_float c[] = {
		GNM_const(0.57735026918962576451),
		GNM_const(0.025660011963983367312),
		GNM_const(0.0014662863979419067035),
		GNM_const(0.000097752426529460446901),
		GNM_const(7.4525058224720927532e-6),
		GNM_const(6.1544207267743329429e-7),
		GNM_const(5.2905118464628039046e-8),
		GNM_const(4.6529126736818620163e-9),
		GNM_const(4.1606321535886269061e-10),
		GNM_const(3.7712142304302013266e-11),
		GNM_const(3.4567362099184451359e-12),
		GNM_const(3.1977726302920313260e-13),
		GNM_const(2.9808441172607163378e-14),
		GNM_const(2.7965280211260193677e-15)
	};
	unsigned ci;
	gnm_float vv, u = 0;

	if (v >= 1)
		return gnm_acosh (v / gnm_sin (v));

	// Above formula will suffer from cancellation
	vv = v * v;
	for (ci = G_N_ELEMENTS(c); ci-- > 0; )
		u = u * vv + c[ci];
	u *= v;

	if (0) {
		gnm_float ref = gnm_acosh (v / gnm_sin (v));
		g_printerr ("XXX: %g %g\n", ref, u);
	}

	return u;
}

static gnm_float
integral_127_u_m_sinhu_cos_v (gnm_float v, gnm_float u, gnm_float sinhu)
{
	static const gnm_float c[] = {
		/*  3 */ GNM_const(0.25660011963983367312),
		/*  5 */ GNM_const(0.0),
		/*  7 */ GNM_const(0.00097752426529460446901),
		/*  9 */ GNM_const(0.000072409204836637368075),
		/* 11 */ GNM_const(7.4478039260541292877e-6),
		/* 13 */ GNM_const(7.4130822294291683120e-7),
		/* 15 */ GNM_const(7.4423844019777464899e-8),
		/* 17 */ GNM_const(7.4866591579915856176e-9),
		/* 19 */ GNM_const(7.5416412192891756316e-10),
		/* 21 */ GNM_const(7.6048685642328096017e-11),
		/* 23 */ GNM_const(7.6748139912232122716e-12),
		/* 25 */ GNM_const(7.7502621827532506438e-13),
		/* 27 */ GNM_const(7.8302824791617646275e-14),
		/* 29 */ GNM_const(7.9141968028287716142e-15),
		/* 31 */ GNM_const(8.0015150114119176413e-16),
		/* 33 */ GNM_const(8.0918754232915038797e-17),
		/* 35 */ GNM_const(8.1850043476015809121e-18)
	};
	unsigned ci;
	gnm_float vv, r = 0;

	if (v >= 1)
		return u - sinhu * gnm_cos (v);

	// Above formula will suffer from cancellation
	vv = v * v;
	for (ci = G_N_ELEMENTS(c); ci-- > 0; )
		r = r * vv + c[ci];
	r *= v * vv;

	if (0) {
		gnm_float ref = u - sinhu * gnm_cos (v);
		g_printerr ("XXX: %g %g %g\n", ref, r, ref - r);
	}

	return r;
}

static gnm_complex
integral_127_integrand (gnm_float v, gnm_float const *args)
{
	gnm_float x = args[0];
	gnm_float nu = args[1];

	gnm_float u = integral_127_u (v);
	// Deviation: Matviyenko uses taylor expansion for sinh for u < 1.
	// There is no need, assuming a reasonable sinh implementation.
	gnm_float sinhu = gnm_sinh (u);
	gnm_float diff = integral_127_u_m_sinhu_cos_v (v, u, sinhu);
	gnm_float sinv = gnm_sin (v);
	gnm_float num = gnm_sinv_m_v_cosv (v, sinv);
	gnm_float den = sinv * sinv * sinhu;
	gnm_float du_dv = v ? num / den : 0;

	gnm_complex xphi4, exphi4, i_du_dv;

	xphi4 = GNM_CMAKE (x * -diff + (x - nu) * u, (x - nu) * v);
	exphi4 = GNM_CEXP (xphi4);
	i_du_dv = GNM_CMAKE (du_dv, 1);
	return GNM_CMUL (exphi4, i_du_dv);
}

static gnm_complex
integral_127 (gnm_float x, gnm_float nu)
{
	// -i/Pi * Integrate[Exp[x*phi4[v]]*(i+du/dv),{v,0,Pi}]
	//
	// tau = 1-nu/x
	// u(v) = acosh(v/sin(v))
	// du/dv = (sin(v)-v*cos(v))/(sin^2(v)*sinh(u(v)))
	// phi4(v) = sinh(u)*cos(v) - u(v) + tau(u(v) + i * v)

	gnm_complex I;
	gnm_float L = 0, H = M_PIgnum;
	gnm_float args[2] = { x, nu };

	complex_shink_integral_range (&L, &H, 0,
				      integral_127_integrand, args);

	I = complex_legendre_integral (33, L, H,
				       integral_127_integrand, args);

	return GNM_CMUL (I, GNM_CMAKE (0, -1 / M_PIgnum));
}

static void
jy_via_j_series (gnm_float x, gnm_float nu, gnm_float *pj, gnm_float *py)
{
	void *state = gnm_quad_start ();
	GnmQuad qnu, qJnu, qJmnu, qYnu, qCos, qSin, qInvSin;

	gnm_quad_init (&qnu, nu);
	gnm_quad_cospi (&qCos, &qnu);
	gnm_quad_sinpi (&qSin, &qnu);
	gnm_quad_div (&qInvSin, &gnm_quad_one, &qSin);

	qJnu = bessel_ij_series (x, nu, TRUE);
	*pj = gnm_quad_value (&qJnu);

	qJmnu = bessel_ij_series (x, -nu, TRUE);

	gnm_quad_mul (&qYnu, &qJnu, &qCos);
	gnm_quad_sub (&qYnu, &qYnu, &qJmnu);
	gnm_quad_mul (&qYnu, &qYnu, &qInvSin);

	*py = gnm_quad_value (&qYnu);

	gnm_quad_end (state);
}


static gnm_float
cb_y_helper (gnm_float nu, const gnm_float *args)
{
	gnm_float x = args[0];
	gnm_float Ynu;
	if (nu == gnm_floor (nu)) {
		g_return_val_if_fail (gnm_abs (nu) < G_MAXINT, gnm_nan);
		Ynu = gnm_yn ((int)nu, x);
	} else {
		gnm_float Jnu;
		jy_via_j_series (x, nu, &Jnu, &Ynu);
	}
	return Ynu;
}

static gnm_complex
hankel1_B1 (gnm_float x, gnm_float nu, size_t N)
{
	return debye_29 (x, nu, N);
}

static gnm_complex
hankel1_B2 (gnm_float x, gnm_float nu, size_t N)
{
	gnm_float q = nu / x;
	gnm_float d = gnm_sqrt (q * q - 1);
	gnm_float eta2 = nu * gnm_log (q + d) - gnm_sqrt (nu * nu - x * x);

	return GNM_CMAKE (debye_32 (x, nu, eta2, N),
			  debye_33 (x, nu, eta2, N));
}

static gnm_complex
hankel1_A1 (gnm_float x, gnm_float nu)
{
	gnm_float rnu = gnm_floor (nu + GNM_const(0.49)); // Close enough
	gnm_float Jnu, Ynu;
	gboolean use_yn = (gnm_abs (rnu) < 100000 - 1);

	if (gnm_abs (nu - rnu) > GNM_const(5e-4)) {
		jy_via_j_series (x, nu, &Jnu, &Ynu);
	} else if (use_yn && nu == rnu) {
		Jnu = gnm_jn ((int)rnu, x);
		Ynu = gnm_yn ((int)rnu, x);
	} else {
		GnmQuad qJnu = bessel_ij_series (x, nu, TRUE);
		size_t N = 6;
		gnm_float dnu = 1e-3;
		gnm_float args[1] = { x };
		gnm_float nul = rnu - dnu, nur = rnu + dnu;
		if (use_yn)
			N |= 1; // Odd, so we use rnu
		Ynu = chebyshev_interpolant (N, nul, nur, nu,
					     cb_y_helper, args);

		Jnu = gnm_quad_value (&qJnu);
	}

	return GNM_CMAKE (Jnu, Ynu);
}

static gnm_complex
hankel1_A2 (gnm_float x, gnm_float nu)
{
	return GNM_CADD (integral_105_126 (x, nu, FALSE),
			 integral_106 (x, nu));
}

static gnm_complex
hankel1_A3 (gnm_float x, gnm_float nu, gnm_float g)
{
	// Deviation: Matviyenko says to change variable to v = t^4 for g < 5.
	// That works wonders for BesselJ[9,12.5], but is too sudden for
	// BesselJ[1,10].  Instead, gradually move from power of 1 to power
	// of 4.  The was the power is lowered is ad hoc.
	//
	// Also, we up the number of points from 37 to 47.

	if (g > 5)
		return integral_83 (x, nu, 25, 1);
	else if (g > 4)
		return integral_83 (x, nu, 47, 2);
	else if (g > 3)
		return integral_83 (x, nu, 47, 3);
	else
		return integral_83 (x, nu, 47, 4);
}

static gnm_complex
hankel1_A4 (gnm_float x, gnm_float nu)
{
	// Deviation: when Matviyenko says that (126) is the same as (105)
	// with alpha=0, he is glossing over the finer points.  When he should
	// have said is that the alpha in the limit is replaced by 0 and
	// that the cosh(alpha) inside is replaced textually by nu/x.  (We may
	// have nu<x and alpha isn't even defined in that case.)
	return GNM_CADD (integral_105_126 (x, nu, TRUE),
			 integral_127 (x, nu));
}

/* ------------------------------------------------------------------------ */

// This follows "On the Evaluation of Bessel Functions" by Gregory Matviyenko.
// Research report YALEU/DCS/RR-903, Yale, Dept. of Comp. Sci., 1992.
//
// Note: there are a few deviations are fixes in this code.  These are marked
// with "deviation" or "erratum".

static gnm_complex
hankel1 (gnm_float x, gnm_float nu)
{
	gnm_float cbrtx, g;

	if (gnm_isnan (x) || gnm_isnan (nu))
		return GNM_CNAN;

	g_return_val_if_fail (x >= 0, GNM_CNAN);

	// Deviation: make this work for negative nu also.
	if (nu < 0) {
		gnm_complex Hmnu = hankel1 (x, -nu);
		if (0) g_printerr ("H_{%g,%g} = %.20g + %.20g * i\n", -nu, x, Hmnu.re, Hmnu.im);
		return GNM_CMUL (Hmnu, GNM_CPOLARPI (1, -nu));
	}

	cbrtx = gnm_cbrt (x);
	g = gnm_abs (x - nu) / cbrtx;

	if (x >= 17 && g >= GNM_const(6.5)) {
		// Algorithm B
		size_t N;
		if (g < 7)
			N = 17;
		else if (g < 10)
			N = 13;
		else if (g < 23)
			N = 9;
		else
			N = 5;

		if (nu < x)
			return hankel1_B1 (x, nu, N);
		else
			return hankel1_B2 (x, nu, N);
	} else {
		// Algorithm A
		// Deviation: we use the series on a wider domain as our
		// series code uses quad precision.
		if (bessel_ij_series_domain (x, nu))
			return hankel1_A1 (x, nu);
		else if (nu > x && g > GNM_const(1.5))
			return hankel1_A2 (x, nu);
		else if (x >= 9 && nu < x && g > GNM_const(1.5))
			return hankel1_A3 (x, nu, g);
		else
			return hankel1_A4 (x, nu);
	}
}

/* ------------------------------------------------------------------------ */

static gboolean
bessel_jy_phase_domain (gnm_float x, gnm_float nu)
{
	gnm_float anu = gnm_abs (nu);
	gnm_float ax = gnm_abs (x);

	if (anu < 2)
		return ax > 1000000;

	if (ax < 20)
		return anu < ax / 5;
	if (ax < 30)
		return anu < ax / 3;
	if (ax < 50)
		return anu < ax / 2;
	if (ax < 100)
		return anu < ax / GNM_const(1.5);
	if (ax < 250)
		return anu < ax / GNM_const(1.2);

	return anu < ax / GNM_const(1.1);
}


static gnm_float
gnm_bessel_M (gnm_float z, gnm_float nu)
{
	gnm_float s = 1;
	gnm_float tn_z2n = 1;
	gnm_float z2 = z * z;
	gnm_float nu2 = nu * nu;
	int n, NMAX = 400;
	gboolean debug = FALSE;

	if (debug) g_printerr ("M[%g,%g]\n", nu, z);

	// log2(1.1^400) = 55.00

	for (n = 1; n < NMAX; n++) {
		gnm_float nmh = n - 0.5;
		gnm_float f = (nu2 - nmh * nmh) * (nmh / n);
		gnm_float r = f / z2;
		if (gnm_abs (r) > 1) {
			if (debug) g_printerr ("Ratio %g\n", r);
			break;
		}
		tn_z2n *= r;
		s += tn_z2n;
		if (debug) g_printerr ("Step %d: %g (%g)\n", n, s, tn_z2n);
		if (gnm_abs (tn_z2n) < GNM_EPSILON * gnm_abs (s)) {
			if (debug) g_printerr ("Precision ok\n");
			break;
		}
	}

	return gnm_sqrt (s / (z * (M_PIgnum / 2)));
}


static void
gnm_quad_reduce_pi (GnmQuad *res, GnmQuad const *a, int p, int *pk)
{
	gnm_float k;
	GnmQuad qk, qa, qb, qtwop;
	unsigned ui;
	static const gnm_float pi_parts[] = {
#if GNM_RADIX == 2
		+0x1.921fb54442d18p+1,
		+0x1.1a62633145c04p-53,
		+0x1.707344a40938p-104,
		+0x1.114cf98e80414p-155,
		+0x1.bea63b139b224p-206,
		+0x1.14a08798e3404p-258,
		+0x1.bbdf2a33679a4p-311,
		+0x1.a431b302b0a6cp-362,
		+0x1.f25f14374fe1p-414,
		+0x1.ab6b6a8e122fp-465
#elif GNM_RADIX == 10
		GNM_const(3.141592653589793),
		GNM_const(0.2384626433832795e-15),
		GNM_const(0.0288419716939937e-31),
		GNM_const(0.5105820974944592e-47),
		GNM_const(0.3078164062862089e-63),
		GNM_const(0.9862803482534211e-79),
		GNM_const(0.7067982148086513e-95),
		GNM_const(0.2823066470938446e-111),
		GNM_const(0.0955058223172535e-127),
		GNM_const(0.9408128481117450e-143)
#else
#error "Code needs fixing"
#endif
	};

	if (a->h < 0) {
		GnmQuad ma;
		gnm_quad_negate (&ma, a);
		gnm_quad_reduce_pi (res, &ma, p, pk);
		gnm_quad_negate (res, res);
		*pk = (p >= 0) ? (-*pk) & ((1 << (p + 1)) - 1) : 0;
		return;
	}

	if (a->h > 1 / GNM_EPSILON)
		g_warning ("Reduced accuracy for very large trigonometric arguments");

	gnm_quad_div (&qk, a, &gnm_quad_pi);
	// This really needs to be 2^p scaling, ie., it is not radix related.
	gnm_quad_init (&qtwop, 1 << p);
	gnm_quad_mul (&qk, &qk, &qtwop);

	gnm_quad_add (&qk, &qk, &gnm_quad_half);
	gnm_quad_floor (&qk, &qk);

	k = gnm_quad_value (&qk);
	*pk = (int)(gnm_fmod (k, 1 << (1 + p)));

	k = gnm_ldexp (k, -p);
	qa = *a;
	for (ui = 0; ui < G_N_ELEMENTS(pi_parts); ui++) {
		gnm_quad_mul12 (&qb, pi_parts[ui], k);
		gnm_quad_sub (&qa, &qa, &qb);
	}

	*res = qa;
}


static gnm_float
gnm_bessel_phi (gnm_float z, gnm_float nu, int *n_pi_4)
{
	void *state = gnm_quad_start ();
	GnmQuad qs = gnm_quad_zero;
	GnmQuad tn_z2n[400], sn_z2n[400];
	GnmQuad qz, qnu, qzm2, qnu2, nuh, qrz;
	int n, N, NMAX = 400, j, dk;
	gnm_float rnu;
	gnm_float lt = GNM_MAX;
	gboolean debug = FALSE;

	if (debug) g_printerr ("Phi[%g,%g]\n", nu, z);

	gnm_quad_init (&qz, z);
	gnm_quad_init (&qnu, nu);

	// qzm2 = 1 / (z * z)
	gnm_quad_mul12 (&qzm2, z, z);
	gnm_quad_div (&qzm2, &gnm_quad_one, &qzm2);

	// qnu2 = nu * nu
	gnm_quad_mul12 (&qnu2, nu, nu);

	(void)gnm_frexp (z / nu, &N);
	N = gnm_ceil (GNM_MANT_DIG * gnm_log2 (GNM_RADIX) / MAX (1, N - 1)) + 1;
	N = MIN (N, (int)G_N_ELEMENTS (tn_z2n));

	tn_z2n[0] = sn_z2n[0] = gnm_quad_one;

	for (n = 1; n < NMAX; n++) {
		GnmQuad qnmh, qnmh2, qf, qd, qn;
		gnm_float lt2;

		gnm_quad_init (&qn, n);

		// qnmh = n - 0.5
		gnm_quad_init (&qnmh, n - 0.5);

		// qnmh2 = (n - 0.5)^2
		gnm_quad_mul (&qnmh2, &qnmh, &qnmh);

		// qf = (nu^2 - (n - 0.5)^2) * (n - 0.5) / n
		gnm_quad_sub (&qf, &qnu2, &qnmh2);
		gnm_quad_mul (&qf, &qf, &qnmh);
		gnm_quad_div (&qf, &qf, &qn);

		// tn_z2n[n] = tn_z2n[n-1] * f / z^2
		gnm_quad_mul (&tn_z2n[n], &tn_z2n[n - 1], &qf);
		gnm_quad_mul (&tn_z2n[n], &tn_z2n[n], &qzm2);

		sn_z2n[n] = gnm_quad_zero;
		for (j = 1; j <= n; j++) {
			GnmQuad qp;

			gnm_quad_mul (&qp, &tn_z2n[j], &sn_z2n[n - j]);
			gnm_quad_sub (&sn_z2n[n], &sn_z2n[n], &qp);
		}

		gnm_quad_init (&qd, 1 - 2 * n);
		gnm_quad_div (&qd, &sn_z2n[n], &qd);

		// Break out when the tn ratios go the wrong way.  The
		// sn ratios can have hickups.
		lt2 = gnm_abs (gnm_quad_value (&tn_z2n[n]));
		if (lt2 > lt) {
			if (debug) g_printerr ("t_n ratio %g\n", lt2 / lt);
			break;
		}
		lt = lt2;

		gnm_quad_add (&qs, &qs, &qd);
		if (debug) g_printerr ("Step %d: %g (%g)\n", n, gnm_quad_value (&qs), gnm_quad_value (&qd));

		if (gnm_abs (gnm_quad_value (&qd)) < GNM_EPSILON * GNM_EPSILON * gnm_abs (gnm_quad_value (&qs))) {
			if (debug) g_printerr ("Precision ok\n");
			break;
		}
	}
	gnm_quad_mul (&qs, &qz, &qs);

	// Add z
	gnm_quad_reduce_pi (&qrz, &qz, 2, n_pi_4);
	gnm_quad_add (&qs, &qs, &qrz);

	// Subtract Pi/4
	(*n_pi_4)--;

	// Subtract nu/2
	rnu = rint (-2 * nu);
	*n_pi_4 += (int)fmod (rnu, 8);
	gnm_quad_init (&nuh, (-2 * nu - rnu) / 4);
	gnm_quad_mul (&nuh, &nuh, &gnm_quad_pi);
	gnm_quad_add (&qs, &qs, &nuh);

	gnm_quad_reduce_pi (&qs, &qs, 2, &dk);
	*n_pi_4 += dk;

	*n_pi_4 &= 7;

	gnm_quad_end (state);

	return gnm_quad_value (&qs);
}

/* ------------------------------------------------------------------------ */

static gnm_float
cos_x_plus_n_pi_4 (gnm_float x, int n_pi_4)
{
	static const gnm_float SQH = M_SQRT2gnum / 2;

	switch (n_pi_4 & 7) {
	case 0: return gnm_cos (x);
	case 1: return (gnm_cos (x) - gnm_sin (x)) * SQH;
	case 2: return -gnm_sin (x);
	case 3: return (gnm_cos (x) + gnm_sin (x)) * -SQH;
	case 4: return -gnm_cos (x);
	case 5: return (gnm_sin (x) - gnm_cos (x)) * SQH;
	case 6: return gnm_sin (x);
	case 7: return (gnm_cos (x) + gnm_sin (x)) * SQH;
	default: g_assert_not_reached ();
	}
}

/* ------------------------------------------------------------------------ */

static gnm_float
gnm_bessel_j_phase (gnm_float x, gnm_float nu)
{
	int n_pi_4;
	gnm_float M = gnm_bessel_M (x, nu);
	gnm_float phi = gnm_bessel_phi (x, nu, &n_pi_4);

	if (0) g_printerr ("M=%g  phi=%.20g + %d * Pi/4\n", M, phi, n_pi_4);

	return M * cos_x_plus_n_pi_4 (phi, n_pi_4);
}

static gnm_float
gnm_bessel_y_phase (gnm_float x, gnm_float nu)
{
	int n_pi_4;
	gnm_float M = gnm_bessel_M (x, nu);
	gnm_float phi = gnm_bessel_phi (x, nu, &n_pi_4);
	// Adding 6 means we get sin instead.
	return M * cos_x_plus_n_pi_4 (phi, n_pi_4 + 6);
}

/* ------------------------------------------------------------------------ */

gnm_float
gnm_bessel_i (gnm_float x, gnm_float alpha)
{
	if (gnm_isnan (x) || gnm_isnan (alpha))
		return x + alpha;

	if (bessel_ij_series_domain (x, alpha)) {
		GnmQuad qi = bessel_ij_series (x, alpha, FALSE);
		return gnm_quad_value (&qi);
	}

	if (x < 0) {
		if (alpha != gnm_floor (alpha))
			return gnm_nan;

		return gnm_fmod (alpha, 2) == 0
			? bessel_i (-x, alpha, 1)  /* Even for even alpha */
			: 0 - bessel_i (-x, alpha, 1);  /* Odd for odd alpha */
	} else
		return bessel_i (x, alpha, 1);
}

gnm_float
gnm_bessel_j (gnm_float x, gnm_float alpha)
{
	if (gnm_isnan (x) || gnm_isnan (alpha))
		return x + alpha;

	if (x < 0) {
		if (alpha != gnm_floor (alpha))
			return gnm_nan;

		return gnm_fmod (alpha, 2) == 0
			? gnm_bessel_j (-x, alpha)  /* Even for even alpha */
			: 0 - gnm_bessel_j (-x, alpha);  /* Odd for odd alpha */
	} else if (bessel_jy_phase_domain (x, alpha)) {
		return gnm_bessel_j_phase (x, alpha);
	} else {
		return GNM_CRE (hankel1 (x, alpha));
	}
}

gnm_float
gnm_bessel_k (gnm_float x, gnm_float alpha)
{
	return bessel_k (x, alpha, 1);
}

gnm_float
gnm_bessel_y (gnm_float x, gnm_float alpha)
{
	if (gnm_isnan (x) || gnm_isnan (alpha))
		return x + alpha;

	if (x < 0) {
		if (alpha != gnm_floor (alpha))
			return gnm_nan;

		return gnm_fmod (alpha, 2) == 0
			? gnm_bessel_y (-x, alpha)  /* Even for even alpha */
			: 0 - gnm_bessel_y (-x, alpha);  /* Odd for odd alpha */
	} else if (bessel_jy_phase_domain (x, alpha)) {
		return gnm_bessel_y_phase (x, alpha);
	} else {
		return GNM_CIM (hankel1 (x, alpha));
	}
}

/* ------------------------------------------------------------------------- */
