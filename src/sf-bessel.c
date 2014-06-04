#include <gnumeric-config.h>
#include <sf-bessel.h>
#include <sf-gamma.h>
#include <sf-trig.h>
#include <mathfunc.h>

#define MATHLIB_STANDALONE
#define ML_ERROR(cause) do { } while(0)
#define MATHLIB_ERROR(_a,_b) return gnm_nan;
#define M_SQRT_2dPI     GNM_const(0.797884560802865355879892119869)  /* sqrt(2/pi) */
#define MATHLIB_WARNING2 g_warning
#define MATHLIB_WARNING4 g_warning

static gboolean bessel_j_series_domain (gnm_float x, gnm_float v);
static gnm_float bessel_j_series (gnm_float x, gnm_float alpha);

static gnm_float bessel_y_ex(gnm_float x, gnm_float alpha, gnm_float *by);
static gnm_float bessel_k(gnm_float x, gnm_float alpha, gnm_float expo);
static gnm_float bessel_y(gnm_float x, gnm_float alpha);

static inline int imin2 (int x, int y) { return MIN (x, y); }
static inline int imax2 (int x, int y) { return MAX (x, y); }
static inline gnm_float fmax2 (gnm_float x, gnm_float y) { return MAX (x, y); }

/* ------------------------------------------------------------------------- */

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
	    == GNM_MAX (defined in  #include <float.h>)
   SQXMIN = Square root of beta ^ minexp = gnm_sqrt(GNM_MIN)

   EPS	  = The smallest positive floating-point number such that 1.0+EPS > 1.0
	  = beta ^ (-p)	 == GNM_EPSILON


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

   EPS_SINC = Machine number below which gnm_sin(x)/x = 1; approximately SQRT(EPS).
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
#define xlrg_BESS_IJ	1e5
#define xlrg_BESS_Y	1e8
#define thresh_BESS_Y	16.

#define xmax_BESS_K	705.342/* maximal x for UNscaled answer */


/* gnm_sqrt(GNM_MIN) =	GNM_const(1.491668e-154) */
#define sqxmin_BESS_K	1.49e-154

/* x < eps_sinc	 <==>  gnm_sin(x)/x == 1 (particularly "==>");
  Linux (around 2001-02) gives GNM_const(2.14946906753213e-08)
  Solaris 2.5.1		 gives GNM_const(2.14911933289084e-08)
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
 *  USA.
 */

/*  DESCRIPTION --> see below */


/* From http://www.netlib.org/specfun/ribesl	Fortran translated by f2c,...
 *	------------------------------=#----	Martin Maechler, ETH Zurich
 */

#ifndef MATHLIB_STANDALONE
#endif

static void I_bessel(gnm_float *x, gnm_float *alpha, long *nb,
		     long *ize, gnm_float *bi, long *ncalc);

static gnm_float bessel_i(gnm_float x, gnm_float alpha, gnm_float expo)
{
    long nb, ncalc, ize;
    gnm_float *bi;
#ifndef MATHLIB_STANDALONE
    char *vmax;
#endif

#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(alpha)) return x + alpha;
#endif
    if (x < 0) {
	ML_ERROR(ME_RANGE);
	return gnm_nan;
    }
    ize = (long)expo;
    if (alpha < 0) {
	/* Using Abramowitz & Stegun  9.6.2
	 * this may not be quite optimal (CPU and accuracy wise) */
	return(bessel_i(x, -alpha, expo) +
	       bessel_k(x, -alpha, expo) * ((ize == 1)? 2. : 2.*gnm_exp(-x))/M_PIgnum
	       * gnm_sinpi(-alpha));
    }
    nb = 1+ (long)gnm_floor(alpha);/* nb-1 <= alpha < nb */
    alpha -= (nb-1);
#ifdef MATHLIB_STANDALONE
    bi = (gnm_float *) calloc(nb, sizeof(gnm_float));
    if (!bi) MATHLIB_ERROR("%s", ("bessel_i allocation error"));
#else
    vmax = vmaxget();
    bi = (gnm_float *) R_alloc(nb, sizeof(gnm_float));
#endif
    I_bessel(&x, &alpha, &nb, &ize, bi, &ncalc);
    if(ncalc != nb) {/* error input */
	if(ncalc < 0)
	    MATHLIB_WARNING4(("bessel_i(%" GNM_FORMAT_g "): ncalc (=%ld) != nb (=%ld); alpha=%" GNM_FORMAT_g ". Arg. out of range?\n"),
			     x, ncalc, nb, alpha);
	else
	    MATHLIB_WARNING2(("bessel_i(%" GNM_FORMAT_g ",nu=%" GNM_FORMAT_g "): precision lost in result\n"),
			     x, alpha+nb-1);
    }
    x = bi[nb-1];
#ifdef MATHLIB_STANDALONE
    free(bi);
#else
    vmaxset(vmax);
#endif
    return x;
}

static void I_bessel(gnm_float *x, gnm_float *alpha, long *nb,
		     long *ize, gnm_float *bi, long *ncalc)
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
    const gnm_float const__ = 1.585;

    /* Local variables */
    long nend, intx, nbmx, k, l, n, nstart;
    gnm_float pold, test,	p, em, en, empal, emp2al, halfx,
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
		bi[k]=gnm_pinf;
	    return;
	}
	intx = (long) (*x);/* --> we will probably fail when *x > LONG_MAX */
	if (*x >= rtnsig_BESS) { /* "non-small" x */
/* -------------------------------------------------------------------
   Initialize the forward sweep, the P-sequence of Olver
   ------------------------------------------------------------------- */
	    nbmx = *nb - intx;
	    n = intx + 1;
	    en = (gnm_float) (n + n) + twonu;
	    plast = 1.;
	    p = en / *x;
	    /* ------------------------------------------------
	       Calculate general significance test
	       ------------------------------------------------ */
	    test = ensig_BESS + ensig_BESS;
	    if (intx << 1 > nsig_BESS * 5) {
		test = gnm_sqrt(test * p);
	    } else {
		test /= gnm_pow(const__, (gnm_float)intx);
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
	    em = (gnm_float) n - 1.;
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
		sum *= (gnm_exp(lgamma1p (nu)) * gnm_pow(*x * .5, -nu));
	    if (*ize == 1)
		sum *= gnm_exp(-(*x));
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
		aa = gnm_pow(halfx, nu) / gnm_exp(lgamma1p(nu));
	    if (*ize == 2)
		aa *= gnm_exp(-(*x));
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
/* Imported src/nmath/bessel_j.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998-2012 Ross Ihaka and the R Core team.
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
 *  http://www.r-project.org/Licenses/
 */

/*  DESCRIPTION --> see below */


/* From http://www.netlib.org/specfun/rjbesl	Fortran translated by f2c,...
 *	------------------------------=#----	Martin Maechler, ETH Zurich
 * Additional code for nu == alpha < 0  MM
 */

#ifndef MATHLIB_STANDALONE
#endif

#define min0(x, y) (((x) <= (y)) ? (x) : (y))

static void J_bessel(gnm_float *x, gnm_float *alpha, long *nb,
		     gnm_float *b, long *ncalc);

static gnm_float bessel_j(gnm_float x, gnm_float alpha)
{
    long nb, ncalc;
    gnm_float na, *bj;
#ifndef MATHLIB_STANDALONE
    const void *vmax;
#endif

#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(alpha)) return x + alpha;
#endif
    if (x < 0) {
	ML_ERROR(ME_RANGE);
	return gnm_nan;
    }
    na = gnm_floor(alpha);
    if (alpha < 0) {
	/* Using Abramowitz & Stegun  9.1.2
	 * this may not be quite optimal (CPU and accuracy wise) */
	return(bessel_j(x, -alpha) * gnm_cospi(alpha) +
	       ((alpha == na) ? 0 :
	       bessel_y(x, -alpha) * gnm_sinpi(alpha)));
    }
    if (bessel_j_series_domain (x, alpha))
	    return bessel_j_series (x, alpha);

    nb = 1 + (long)na; /* nb-1 <= alpha < nb */
    alpha -= (gnm_float)(nb-1);
#ifdef MATHLIB_STANDALONE
    bj = (gnm_float *) calloc(nb, sizeof(gnm_float));
    if (!bj) MATHLIB_ERROR("%s", ("bessel_j allocation error"));
#else
    vmax = vmaxget();
    bj = (gnm_float *) R_alloc((size_t) nb, sizeof(gnm_float));
#endif
    J_bessel(&x, &alpha, &nb, bj, &ncalc);
    if(ncalc != nb) {/* error input */
      if(ncalc < 0)
	MATHLIB_WARNING4(("bessel_j(%" GNM_FORMAT_g "): ncalc (=%ld) != nb (=%ld); alpha=%" GNM_FORMAT_g ". Arg. out of range?\n"),
			 x, ncalc, nb, alpha);
      else
	MATHLIB_WARNING2(("bessel_j(%" GNM_FORMAT_g ",nu=%" GNM_FORMAT_g "): precision lost in result\n"),
			 x, alpha+(gnm_float)nb-1);
    }
    x = bj[nb-1];
#ifdef MATHLIB_STANDALONE
    free(bj);
#else
    vmaxset(vmax);
#endif
    return x;
}

/* modified version of bessel_j that accepts a work array instead of
   allocating one. */
static gnm_float bessel_j_ex(gnm_float x, gnm_float alpha, gnm_float *bj)
{
    long nb, ncalc;
    gnm_float na;

#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(alpha)) return x + alpha;
#endif
    if (x < 0) {
	ML_ERROR(ME_RANGE);
	return gnm_nan;
    }
    na = gnm_floor(alpha);
    if (alpha < 0) {
	/* Using Abramowitz & Stegun  9.1.2
	 * this may not be quite optimal (CPU and accuracy wise) */
	return(bessel_j_ex(x, -alpha, bj) * gnm_cospi(alpha) +
	       ((alpha == na) ? 0 :
		bessel_y_ex(x, -alpha, bj) * gnm_sinpi(alpha)));
    }
    nb = 1 + (long)na; /* nb-1 <= alpha < nb */
    alpha -= (gnm_float)(nb-1);
    J_bessel(&x, &alpha, &nb, bj, &ncalc);
    if(ncalc != nb) {/* error input */
      if(ncalc < 0)
	MATHLIB_WARNING4(("bessel_j(%" GNM_FORMAT_g "): ncalc (=%ld) != nb (=%ld); alpha=%" GNM_FORMAT_g ". Arg. out of range?\n"),
			 x, ncalc, nb, alpha);
      else
	MATHLIB_WARNING2(("bessel_j(%" GNM_FORMAT_g ",nu=%" GNM_FORMAT_g "): precision lost in result\n"),
			 x, alpha+(gnm_float)nb-1);
    }
    x = bj[nb-1];
    return x;
}

static void J_bessel(gnm_float *x, gnm_float *alpha, long *nb,
		     gnm_float *b, long *ncalc)
{
/*
 Calculates Bessel functions J_{n+alpha} (x)
 for non-negative argument x, and non-negative order n+alpha, n = 0,1,..,nb-1.

  Explanation of variables in the calling sequence.

 X     - Non-negative argument for which J's are to be calculated.
 ALPHA - Fractional part of order for which
	 J's are to be calculated.  0 <= ALPHA < 1.
 NB    - Number of functions to be calculated, NB >= 1.
	 The first function calculated is of order ALPHA, and the
	 last is of order (NB - 1 + ALPHA).
 B     - Output vector of length NB.  If RJBESL
	 terminates normally (NCALC=NB), the vector B contains the
	 functions J/ALPHA/(X) through J/NB-1+ALPHA/(X).
 NCALC - Output variable indicating possible errors.
	 Before using the vector B, the user should check that
	 NCALC=NB, i.e., all orders have been calculated to
	 the desired accuracy.	See the following

	 ****************************************************************

 Error return codes

    In case of an error,  NCALC != NB, and not all J's are
    calculated to the desired accuracy.

    NCALC < 0:	An argument is out of range. For example,
       NBES <= 0, ALPHA < 0 or > 1, or X is too large.
       In this case, b[1] is set to zero, the remainder of the
       B-vector is not calculated, and NCALC is set to
       MIN(NB,0)-1 so that NCALC != NB.

    NB > NCALC > 0: Not all requested function values could
       be calculated accurately.  This usually occurs because NB is
       much larger than ABS(X).	 In this case, b[N] is calculated
       to the desired accuracy for N <= NCALC, but precision
       is lost for NCALC < N <= NB.  If b[N] does not vanish
       for N > NCALC (because it is too small to be represented),
       and b[N]/b[NCALC] = 10^(-K), then only the first NSIG - K
       significant figures of b[N] can be trusted.


  Acknowledgement

	This program is based on a program written by David J. Sookne
	(2) that computes values of the Bessel functions J or I of float
	argument and long order.  Modifications include the restriction
	of the computation to the J Bessel function of non-negative float
	argument, the extension of the computation to arbitrary positive
	order, and the elimination of most underflow.

  References:

	Olver, F.W.J., and Sookne, D.J. (1972)
	"A Note on Backward Recurrence Algorithms";
	Math. Comp. 26, 941-947.

	Sookne, D.J. (1973)
	"Bessel Functions of Real Argument and Integer Order";
	NBS Jour. of Res. B. 77B, 125-132.

  Latest modification: March 19, 1990

  Author: W. J. Cody
	  Applied Mathematics Division
	  Argonne National Laboratory
	  Argonne, IL  60439
 *******************************************************************
 */

/* ---------------------------------------------------------------------
  Mathematical constants

   PI2	  = 2 / PI
   TWOPI1 = first few significant digits of 2 * PI
   TWOPI2 = (2*PI - TWOPI1) to working precision, i.e.,
	    TWOPI1 + TWOPI2 = 2 * PI to extra precision.
 --------------------------------------------------------------------- */
    const static gnm_float pi2 = GNM_const(.636619772367581343075535);
    const static gnm_float twopi1 = GNM_const(6.28125);
    const static gnm_float twopi2 =  GNM_const(.001935307179586476925286767);

/*---------------------------------------------------------------------
 *  Factorial(N)
 *--------------------------------------------------------------------- */
/* removed array fact */

    /* Local variables */
    long nend, intx, nbmx, i, j, k, l, m, n, nstart;

    gnm_float nu, twonu, capp, capq, pold, vcos, test, vsin;
    gnm_float p, s, t, z, alpem, halfx, aa, bb, cc, psave, plast;
    gnm_float tover, t1, alp2em, em, en, xc, xk, xm, psavel, gnu, xin, sum;


    /* Parameter adjustment */
    --b;

    nu = *alpha;
    twonu = nu + nu;

    /*-------------------------------------------------------------------
      Check for out of range arguments.
      -------------------------------------------------------------------*/
    if (*nb > 0 && *x >= 0. && 0. <= nu && nu < 1.) {

	*ncalc = *nb;
	if(*x > xlrg_BESS_IJ) {
	    ML_ERROR(ME_RANGE);
	    /* indeed, the limit is 0,
	     * but the cutoff happens too early */
	    for(i=1; i <= *nb; i++)
		b[i] = 0.; /*was gnm_pinf (really nonsense) */
	    return;
	}
	intx = (long) (*x);
	/* Initialize result array to zero. */
	for (i = 1; i <= *nb; ++i)
	    b[i] = 0.;

	/*===================================================================
	  Branch into  3 cases :
	  1) use 2-term ascending series for small X
	  2) use asymptotic form for large X when NB is not too large
	  3) use recursion otherwise
	  ===================================================================*/

	if (*x < rtnsig_BESS) {
	  /* ---------------------------------------------------------------
	     Two-term ascending series for small X.
	     --------------------------------------------------------------- */
	    alpem = 1. + nu;

	    halfx = (*x > enmten_BESS) ? .5 * *x :  0.;
	    aa	  = (nu != 0.)	  ? gnm_pow(halfx, nu) / (nu * gnm_gamma(nu)) : 1.;
	    bb	  = (*x + 1. > 1.)? -halfx * halfx : 0.;
	    b[1] = aa + aa * bb / alpem;
	    if (*x != 0. && b[1] == 0.)
		*ncalc = 0;

	    if (*nb != 1) {
		if (*x <= 0.) {
		    for (n = 2; n <= *nb; ++n)
			b[n] = 0.;
		}
		else {
		    /* ----------------------------------------------
		       Calculate higher order functions.
		       ---------------------------------------------- */
		    if (bb == 0.)
			tover = (enmten_BESS + enmten_BESS) / *x;
		    else
			tover = enmten_BESS / bb;
		    cc = halfx;
		    for (n = 2; n <= *nb; ++n) {
			aa /= alpem;
			alpem += 1.;
			aa *= cc;
			if (aa <= tover * alpem)
			    aa = 0.;

			b[n] = aa + aa * bb / alpem;
			if (b[n] == 0. && *ncalc > n)
			    *ncalc = n - 1;
		    }
		}
	    }
	} else if (*x > 25. && *nb <= intx + 1) {
	    /* ------------------------------------------------------------
	       Asymptotic series for X > 25 (and not too large nb)
	       ------------------------------------------------------------ */
	    xc = gnm_sqrt(pi2 / *x);
	    xin = 1 / (64 * *x * *x);
	    if (*x >= 130.)	m = 4;
	    else if (*x >= 35.) m = 8;
	    else		m = 11;
	    xm = 4. * (gnm_float) m;
	    /* ------------------------------------------------
	       Argument reduction for SIN and COS routines.
	       ------------------------------------------------ */
	    t = gnm_trunc(*x / (twopi1 + twopi2) + .5);
	    z = (*x - t * twopi1) - t * twopi2 - (nu + .5) / pi2;
	    vsin = gnm_sin(z);
	    vcos = gnm_cos(z);
	    gnu = twonu;
	    for (i = 1; i <= 2; ++i) {
		s = (xm - 1. - gnu) * (xm - 1. + gnu) * xin * .5;
		t = (gnu - (xm - 3.)) * (gnu + (xm - 3.));
		t1= (gnu - (xm + 1.)) * (gnu + (xm + 1.));
		k = m + m;
		capp = s * t / gnm_fact(k);
		capq = s * t1/ gnm_fact(k + 1);
		xk = xm;
		for (; k >= 4; k -= 2) {/* k + 2(j-2) == 2m */
		    xk -= 4.;
		    s = (xk - 1. - gnu) * (xk - 1. + gnu);
		    t1 = t;
		    t = (gnu - (xk - 3.)) * (gnu + (xk - 3.));
		    capp = (capp + 1. / gnm_fact(k - 2)) * s * t  * xin;
		    capq = (capq + 1. / gnm_fact(k - 1)) * s * t1 * xin;

		}
		capp += 1.;
		capq = (capq + 1.) * (gnu * gnu - 1.) * (.125 / *x);
		b[i] = xc * (capp * vcos - capq * vsin);
		if (*nb == 1)
		    return;

		/* vsin <--> vcos */ t = vsin; vsin = -vcos; vcos = t;
		gnu += 2.;
	    }
	    /* -----------------------------------------------
	       If  NB > 2, compute J(X,ORDER+I)	for I = 2, NB-1
	       ----------------------------------------------- */
	    if (*nb > 2)
		for (gnu = twonu + 2., j = 3; j <= *nb; j++, gnu += 2.)
		    b[j] = gnu * b[j - 1] / *x - b[j - 2];
	}
	else {
	    /* rtnsig_BESS <= x && ( x <= 25 || intx+1 < *nb ) :
	       --------------------------------------------------------
	       Use recurrence to generate results.
	       First initialize the calculation of P*S.
	       -------------------------------------------------------- */
	    nbmx = *nb - intx;
	    n = intx + 1;
	    en = (gnm_float)(n + n) + twonu;
	    plast = 1.;
	    p = en / *x;
	    /* ---------------------------------------------------
	       Calculate general significance test.
	       --------------------------------------------------- */
	    test = ensig_BESS + ensig_BESS;
	    if (nbmx >= 3) {
		/* ------------------------------------------------------------
		   Calculate P*S until N = NB-1.  Check for possible overflow.
		   ---------------------------------------------------------- */
		tover = enten_BESS / ensig_BESS;
		nstart = intx + 2;
		nend = *nb - 1;
		en = (gnm_float) (nstart + nstart) - 2. + twonu;
		for (k = nstart; k <= nend; ++k) {
		    n = k;
		    en += 2.;
		    pold = plast;
		    plast = p;
		    p = en * plast / *x - pold;
		    if (p > tover) {
			/* -------------------------------------------
			   To avoid overflow, divide P*S by TOVER.
			   Calculate P*S until ABS(P) > 1.
			   -------------------------------------------*/
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
			    p = en * plast / *x - pold;
			} while (p <= 1.);

			bb = en / *x;
			/* -----------------------------------------------
			   Calculate backward test and find NCALC,
			   the highest N such that the test is passed.
			   ----------------------------------------------- */
			test = pold * plast * (.5 - .5 / (bb * bb));
			test /= ensig_BESS;
			p = plast * tover;
			--n;
			en -= 2.;
			nend = min0(*nb,n);
			for (l = nstart; l <= nend; ++l) {
			    pold = psavel;
			    psavel = psave;
			    psave = en * psavel / *x - pold;
			    if (psave * psavel > test) {
				*ncalc = l - 1;
				goto L190;
			    }
			}
			*ncalc = nend;
			goto L190;
		    }
		}
		n = nend;
		en = (gnm_float) (n + n) + twonu;
		/* -----------------------------------------------------
		   Calculate special significance test for NBMX > 2.
		   -----------------------------------------------------*/
		test = fmax2(test, gnm_sqrt(plast * ensig_BESS) * gnm_sqrt(p + p));
	    }
	    /* ------------------------------------------------
	       Calculate P*S until significance test passes. */
	    do {
		++n;
		en += 2.;
		pold = plast;
		plast = p;
		p = en * plast / *x - pold;
	    } while (p < test);

L190:
	    /*---------------------------------------------------------------
	      Initialize the backward recursion and the normalization sum.
	      --------------------------------------------------------------- */
	    ++n;
	    en += 2.;
	    bb = 0.;
	    aa = 1. / p;
	    m = n / 2;
	    em = (gnm_float)m;
	    m = (n << 1) - (m << 2);/* = 2 n - 4 (n/2)
				       = 0 for even, 2 for odd n */
	    if (m == 0)
		sum = 0.;
	    else {
		alpem = em - 1. + nu;
		alp2em = em + em + nu;
		sum = aa * alpem * alp2em / em;
	    }
	    nend = n - *nb;
	    /* if (nend > 0) */
	    /* --------------------------------------------------------
	       Recur backward via difference equation, calculating
	       (but not storing) b[N], until N = NB.
	       -------------------------------------------------------- */
	    for (l = 1; l <= nend; ++l) {
		--n;
		en -= 2.;
		cc = bb;
		bb = aa;
		aa = en * bb / *x - cc;
		m = m ? 0 : 2; /* m = 2 - m failed on gcc4-20041019 */
		if (m != 0) {
		    em -= 1.;
		    alp2em = em + em + nu;
		    if (n == 1)
			break;

		    alpem = em - 1. + nu;
		    if (alpem == 0.)
			alpem = 1.;
		    sum = (sum + aa * alp2em) * alpem / em;
		}
	    }
	    /*--------------------------------------------------
	      Store b[NB].
	      --------------------------------------------------*/
	    b[n] = aa;
	    if (nend >= 0) {
		if (*nb <= 1) {
		    if (nu + 1. == 1.)
			alp2em = 1.;
		    else
			alp2em = nu;
		    sum += b[1] * alp2em;
		    goto L250;
		}
		else {/*-- nb >= 2 : ---------------------------
			Calculate and store b[NB-1].
			----------------------------------------*/
		    --n;
		    en -= 2.;
		    b[n] = en * aa / *x - bb;
		    if (n == 1)
			goto L240;

		    m = m ? 0 : 2; /* m = 2 - m failed on gcc4-20041019 */
		    if (m != 0) {
			em -= 1.;
			alp2em = em + em + nu;
			alpem = em - 1. + nu;
			if (alpem == 0.)
			    alpem = 1.;
			sum = (sum + b[n] * alp2em) * alpem / em;
		    }
		}
	    }

	    /* if (n - 2 != 0) */
	    /* --------------------------------------------------------
	       Calculate via difference equation and store b[N],
	       until N = 2.
	       -------------------------------------------------------- */
	    for (n = n-1; n >= 2; n--) {
		en -= 2.;
		b[n] = en * b[n + 1] / *x - b[n + 2];
		m = m ? 0 : 2; /* m = 2 - m failed on gcc4-20041019 */
		if (m != 0) {
		    em -= 1.;
		    alp2em = em + em + nu;
		    alpem = em - 1. + nu;
		    if (alpem == 0.)
			alpem = 1.;
		    sum = (sum + b[n] * alp2em) * alpem / em;
		}
	    }
	    /* ---------------------------------------
	       Calculate b[1].
	       -----------------------------------------*/
	    b[1] = 2. * (nu + 1.) * b[2] / *x - b[3];

L240:
	    em -= 1.;
	    alp2em = em + em + nu;
	    if (alp2em == 0.)
		alp2em = 1.;
	    sum += b[1] * alp2em;

L250:
	    /* ---------------------------------------------------
	       Normalize.  Divide all b[N] by sum.
	       ---------------------------------------------------*/
/*	    if (nu + 1. != 1.) poor test */
	    if(gnm_abs(nu) > 1e-15)
		sum *= (gnm_gamma(nu) * gnm_pow(.5* *x, -nu));

	    aa = enmten_BESS;
	    if (sum > 1.)
		aa *= sum;
	    for (n = 1; n <= *nb; ++n) {
		if (gnm_abs(b[n]) < aa)
		    b[n] = 0.;
		else
		    b[n] /= sum;
	    }
	}

    }
    else {
      /* Error return -- X, NB, or ALPHA is out of range : */
	b[1] = 0.;
	*ncalc = min0(*nb,0) - 1;
    }
}
/* Cleaning up done by tools/import-R:  */
#undef min0

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/bessel_k.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998-2001 Ross Ihaka and the R Development Core team.
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
 *  USA.
 */

/*  DESCRIPTION --> see below */


/* From http://www.netlib.org/specfun/rkbesl	Fortran translated by f2c,...
 *	------------------------------=#----	Martin Maechler, ETH Zurich
 */

#ifndef MATHLIB_STANDALONE
#endif

static void K_bessel(gnm_float *x, gnm_float *alpha, long *nb,
		     long *ize, gnm_float *bk, long *ncalc);

static gnm_float bessel_k(gnm_float x, gnm_float alpha, gnm_float expo)
{
    long nb, ncalc, ize;
    gnm_float *bk;
#ifndef MATHLIB_STANDALONE
    char *vmax;
#endif

#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(alpha)) return x + alpha;
#endif
    if (x < 0) {
	ML_ERROR(ME_RANGE);
	return gnm_nan;
    }
    ize = (long)expo;
    if(alpha < 0)
	alpha = -alpha;
    nb = 1+ (long)gnm_floor(alpha);/* nb-1 <= |alpha| < nb */
    alpha -= (nb-1);
#ifdef MATHLIB_STANDALONE
    bk = (gnm_float *) calloc(nb, sizeof(gnm_float));
    if (!bk) MATHLIB_ERROR("%s", ("bessel_k allocation error"));
#else
    vmax = vmaxget();
    bk = (gnm_float *) R_alloc(nb, sizeof(gnm_float));
#endif
    K_bessel(&x, &alpha, &nb, &ize, bk, &ncalc);
    if(ncalc != nb) {/* error input */
      if(ncalc < 0)
	MATHLIB_WARNING4(("bessel_k(%" GNM_FORMAT_g "): ncalc (=%ld) != nb (=%ld); alpha=%" GNM_FORMAT_g ". Arg. out of range?\n"),
			 x, ncalc, nb, alpha);
      else
	MATHLIB_WARNING2(("bessel_k(%" GNM_FORMAT_g ",nu=%" GNM_FORMAT_g "): precision lost in result\n"),
			 x, alpha+nb-1);
    }
    x = bk[nb-1];
#ifdef MATHLIB_STANDALONE
    free(bk);
#else
    vmaxset(vmax);
#endif
    return x;
}

static void K_bessel(gnm_float *x, gnm_float *alpha, long *nb,
		     long *ize, gnm_float *bk, long *ncalc)
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
    const gnm_float a = GNM_const(.11593151565841244881);

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
    static const gnm_float estm[6] = { 52.0583,5.7607,2.7782,14.4303,185.3004, 9.3715 };
    static const gnm_float estf[7] = { 41.8341,7.1075,6.4306,42.511,GNM_const(1.35633),84.5096,20.};

    /* Local variables */
    long iend, i, j, k, m, ii, mplus1;
    gnm_float x2by4, twox, c, blpha, ratio, wminf;
    gnm_float d1, d2, d3, f0, f1, f2, p0, q0, t1, t2, twonu;
    gnm_float dm, ex, bk1, bk2, nu;

    ii = 0; /* -Wall */

    ex = *x;
    nu = *alpha;
    *ncalc = imin2(*nb,0) - 2;
    if (*nb > 0 && (0. <= nu && nu < 1.) && (1 <= *ize && *ize <= 2)) {
	if(ex <= 0 || (*ize == 1 && ex > xmax_BESS_K)) {
	    if(ex <= 0) {
		ML_ERROR(ME_RANGE);
		for(i=0; i < *nb; i++)
		    bk[i] = gnm_pinf;
	    } else /* would only have underflow */
		for(i=0; i < *nb; i++)
		    bk[i] = 0.;
	    *ncalc = *nb;
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
	    f1 = gnm_log(ex);
	    f0 = a + nu * (p[7] - nu * (d1 + d2) / (t1 + t2)) - f1;
	    q0 = gnm_exp(-nu * (a - nu * (p[7] + nu * (d1-d2) / (t1-t2)) - f1));
	    f1 = nu * f0;
	    p0 = gnm_exp(f1);
	    /* -----------------------------------------------------------
	       Calculation of F0 =
	       ----------------------------------------------------------- */
	    d1 = r[4];
	    t1 = 1.;
	    for (i = 0; i < 4; ++i) {
		d1 = c * d1 + r[i];
		t1 = c * t1 + s[i];
	    }
	    /* d2 := gnm_sinh(f1)/ nu = gnm_sinh(f1)/(f1/f0)
	     *	   = f0 * gnm_sinh(f1)/f1 */
	    if (gnm_abs(f1) <= .5) {
		f1 *= f1;
		d2 = 0.;
		for (i = 0; i < 6; ++i) {
		    d2 = f1 * d2 + t[i];
		}
		d2 = f0 + f0 * f1 * d2;
	    } else {
		d2 = gnm_sinh(f1) / nu;
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
		} while (gnm_abs(t1 / (f1 + bk1)) > GNM_EPSILON ||
			 gnm_abs(t2 / (f2 + bk2)) > GNM_EPSILON);
		bk1 = f1 + bk1;
		bk2 = 2. * (f2 + bk2) / ex;
		if (*ize == 2) {
		    d1 = gnm_exp(ex);
		    bk1 *= d1;
		    bk2 *= d1;
		}
		wminf = estf[0] * ex + estf[1];
	    }
	} else if (GNM_EPSILON * ex > 1.) {
	    /* -------------------------------------------------
	       X > 1./EPS
	       ------------------------------------------------- */
	    *ncalc = *nb;
	    bk1 = 1. / (M_SQRT_2dPI * gnm_sqrt(ex));
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
		d2 = gnm_trunc(estm[0] / ex + estm[1]);
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
		d2 = gnm_trunc(estm[2] * ex + estm[3]);
		m = (long) d2;
		c = gnm_abs(nu);
		d3 = c + c;
		d1 = d3 - 1.;
		f1 = GNM_MIN;
		f0 = (2. * (c + d2) / ex + .5 * ex / (c + d2 + 1.)) * GNM_MIN;
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
		p0 = gnm_exp(c * (a + c * (p[7] - c * d1 / t1) - gnm_log(ex))) / ex;
		f2 = (c + .5 - ratio) * f1 / ex;
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
		bk1 = 1. / ((M_SQRT_2dPI + M_SQRT_2dPI * blpha) * gnm_sqrt(ex));
		if (*ize == 1)
		    bk1 *= gnm_exp(-ex);
		wminf = estf[4] * (ex - gnm_abs(ex - estf[6])) + estf[5];
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
	    twonu += 2.;
	    ratio = twonu / ex + 1./ratio;
	    ++j;
	    if (j >= 1) {
		bk[j] = ratio;
	    } else {
		if (bk2 >= GNM_MAX / ratio)
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
	    if (bk[i-1] >= GNM_MAX / bk[i])
		return;
#endif
	    bk[i] *= bk[i-1];
	    (*ncalc)++;
	}
    }
}

/* ------------------------------------------------------------------------ */
/* Imported src/nmath/bessel_y.c from R.  */
/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998-2012 Ross Ihaka and the R Core team.
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
 *  http://www.r-project.org/Licenses/
 */

/*  DESCRIPTION --> see below */


/* From http://www.netlib.org/specfun/rybesl	Fortran translated by f2c,...
 *	------------------------------=#----	Martin Maechler, ETH Zurich
 */

#ifndef MATHLIB_STANDALONE
#endif

#define min0(x, y) (((x) <= (y)) ? (x) : (y))

static void Y_bessel(gnm_float *x, gnm_float *alpha, long *nb,
		     gnm_float *by, long *ncalc);

static gnm_float bessel_y(gnm_float x, gnm_float alpha)
{
    long nb, ncalc;
    gnm_float na, *by;
#ifndef MATHLIB_STANDALONE
    const void *vmax;
#endif

#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(alpha)) return x + alpha;
#endif
    if (x < 0) {
	ML_ERROR(ME_RANGE);
	return gnm_nan;
    }
    na = gnm_floor(alpha);

    if (alpha != na &&
	bessel_j_series_domain (x, alpha) &&
	bessel_j_series_domain (x, -alpha)) {
	    gnm_float s = gnm_sinpi (alpha);
	    gnm_float c = gnm_cospi (alpha);
	    return ((c ? bessel_j_series (x, alpha) * c : 0) - bessel_j_series (x, -alpha)) / s;
    }

    if (alpha < 0) {
	/* Using Abramowitz & Stegun  9.1.2
	 * this may not be quite optimal (CPU and accuracy wise) */
	return(bessel_y(x, -alpha) * gnm_cospi(alpha) -
	       ((alpha == na) ? 0 :
		bessel_j(x, -alpha) * gnm_sinpi(alpha)));
    }
    nb = 1+ (long)na;/* nb-1 <= alpha < nb */
    alpha -= (gnm_float)(nb-1);
#ifdef MATHLIB_STANDALONE
    by = (gnm_float *) calloc(nb, sizeof(gnm_float));
    if (!by) MATHLIB_ERROR("%s", ("bessel_y allocation error"));
#else
    vmax = vmaxget();
    by = (gnm_float *) R_alloc((size_t) nb, sizeof(gnm_float));
#endif
    Y_bessel(&x, &alpha, &nb, by, &ncalc);
    if(ncalc != nb) {/* error input */
	if(ncalc == -1) {
            free(by);
	    return gnm_pinf;
	} else if(ncalc < -1)
	    MATHLIB_WARNING4(("bessel_y(%" GNM_FORMAT_g "): ncalc (=%ld) != nb (=%ld); alpha=%" GNM_FORMAT_g ". Arg. out of range?\n"),
			     x, ncalc, nb, alpha);
	else /* ncalc >= 0 */
	    MATHLIB_WARNING2(("bessel_y(%" GNM_FORMAT_g ",nu=%" GNM_FORMAT_g "): precision lost in result\n"),
			     x, alpha+(gnm_float)nb-1);
    }
    x = by[nb-1];
#ifdef MATHLIB_STANDALONE
    free(by);
#else
    vmaxset(vmax);
#endif
    return x;
}

/* modified version of bessel_y that accepts a work array instead of
   allocating one. */
static gnm_float bessel_y_ex(gnm_float x, gnm_float alpha, gnm_float *by)
{
    long nb, ncalc;
    gnm_float na;

#ifdef IEEE_754
    /* NaNs propagated correctly */
    if (gnm_isnan(x) || gnm_isnan(alpha)) return x + alpha;
#endif
    if (x < 0) {
	ML_ERROR(ME_RANGE);
	return gnm_nan;
    }
    na = gnm_floor(alpha);
    if (alpha < 0) {
	/* Using Abramowitz & Stegun  9.1.2
	 * this may not be quite optimal (CPU and accuracy wise) */
	return(bessel_y_ex(x, -alpha, by) * gnm_cospi(alpha) -
	       ((alpha == na) ? 0 :
		bessel_j_ex(x, -alpha, by) * gnm_sinpi(alpha)));
    }
    nb = 1+ (long)na;/* nb-1 <= alpha < nb */
    alpha -= (gnm_float)(nb-1);
    Y_bessel(&x, &alpha, &nb, by, &ncalc);
    if(ncalc != nb) {/* error input */
	if(ncalc == -1)
	    return gnm_pinf;
	else if(ncalc < -1)
	    MATHLIB_WARNING4(("bessel_y(%" GNM_FORMAT_g "): ncalc (=%ld) != nb (=%ld); alpha=%" GNM_FORMAT_g ". Arg. out of range?\n"),
			     x, ncalc, nb, alpha);
	else /* ncalc >= 0 */
	    MATHLIB_WARNING2(("bessel_y(%" GNM_FORMAT_g ",nu=%" GNM_FORMAT_g "): precision lost in result\n"),
			     x, alpha+(gnm_float)nb-1);
    }
    x = by[nb-1];
    return x;
}

static void Y_bessel(gnm_float *x, gnm_float *alpha, long *nb,
		     gnm_float *by, long *ncalc)
{
/* ----------------------------------------------------------------------

 This routine calculates Bessel functions Y_(N+ALPHA) (X)
 for non-negative argument X, and non-negative order N+ALPHA.


 Explanation of variables in the calling sequence

 X     - Non-negative argument for which
	 Y's are to be calculated.
 ALPHA - Fractional part of order for which
	 Y's are to be calculated.  0 <= ALPHA < 1.0.
 NB    - Number of functions to be calculated, NB > 0.
	 The first function calculated is of order ALPHA, and the
	 last is of order (NB - 1 + ALPHA).
 BY    - Output vector of length NB.	If the
	 routine terminates normally (NCALC=NB), the vector BY
	 contains the functions Y(ALPHA,X), ... , Y(NB-1+ALPHA,X),
	 If (0 < NCALC < NB), BY(I) contains correct function
	 values for I <= NCALC, and contains the ratios
	 Y(ALPHA+I-1,X)/Y(ALPHA+I-2,X) for the rest of the array.
 NCALC - Output variable indicating possible errors.
	 Before using the vector BY, the user should check that
	 NCALC=NB, i.e., all orders have been calculated to
	 the desired accuracy.	See error returns below.


 *******************************************************************

 Error returns

  In case of an error, NCALC != NB, and not all Y's are
  calculated to the desired accuracy.

  NCALC < -1:  An argument is out of range. For example,
	NB <= 0, IZE is not 1 or 2, or IZE=1 and ABS(X) >=
	XMAX.  In this case, BY[0] = 0.0, the remainder of the
	BY-vector is not calculated, and NCALC is set to
	MIN0(NB,0)-2  so that NCALC != NB.
  NCALC = -1:  Y(ALPHA,X) >= XINF.  The requested function
	values are set to 0.0.
  1 < NCALC < NB: Not all requested function values could
	be calculated accurately.  BY(I) contains correct function
	values for I <= NCALC, and the remaining NB-NCALC
	array elements contain 0.0.


 Intrinsic functions required are:

     DBLE, EXP, INT, MAX, MIN, REAL, SQRT


 Acknowledgement

	This program draws heavily on Temme's Algol program for Y(a,x)
	and Y(a+1,x) and on Campbell's programs for Y_nu(x).	Temme's
	scheme is used for  x < THRESH, and Campbell's scheme is used
	in the asymptotic region.  Segments of code from both sources
	have been translated into Fortran 77, merged, and heavily modified.
	Modifications include parameterization of machine dependencies,
	use of a new approximation for ln(gamma(x)), and built-in
	protection against over/underflow.

 References: "Bessel functions J_nu(x) and Y_nu(x) of float
	      order and float argument," Campbell, J. B.,
	      Comp. Phy. Comm. 18, 1979, pp. 133-142.

	     "On the numerical evaluation of the ordinary
	      Bessel function of the second kind," Temme,
	      N. M., J. Comput. Phys. 21, 1976, pp. 343-350.

  Latest modification: March 19, 1990

  Modified by: W. J. Cody
	       Applied Mathematics Division
	       Argonne National Laboratory
	       Argonne, IL  60439
 ----------------------------------------------------------------------*/


/* ----------------------------------------------------------------------
  Mathematical constants
    FIVPI = 5*PI
    PIM5 = 5*PI - 15
 ----------------------------------------------------------------------*/
    const static gnm_float fivpi = GNM_const(15.707963267948966192);
    const static gnm_float pim5	=   GNM_const(.70796326794896619231);

    /*----------------------------------------------------------------------
      Coefficients for Chebyshev polynomial expansion of
      1/gamma(1-x), abs(x) <= .5
      ----------------------------------------------------------------------*/
    const static gnm_float ch[21] = { GNM_const(-6.7735241822398840964e-24),
	    GNM_const(-6.1455180116049879894e-23),GNM_const(2.9017595056104745456e-21),
	    GNM_const(1.3639417919073099464e-19),GNM_const(2.3826220476859635824e-18),
	    GNM_const(-9.0642907957550702534e-18),GNM_const(-1.4943667065169001769e-15),
	    GNM_const(-3.3919078305362211264e-14),GNM_const(-1.7023776642512729175e-13),
	    GNM_const(9.1609750938768647911e-12),GNM_const(2.4230957900482704055e-10),
	    GNM_const(1.7451364971382984243e-9),GNM_const(-3.3126119768180852711e-8),
	    GNM_const(-8.6592079961391259661e-7),GNM_const(-4.9717367041957398581e-6),
	    GNM_const(7.6309597585908126618e-5),GNM_const(.0012719271366545622927),
	    GNM_const(.0017063050710955562222),GNM_const(-.07685284084478667369),
	    GNM_const(-.28387654227602353814),GNM_const(.92187029365045265648) };

    /* Local variables */
    long i, k, na;

    gnm_float alfa, div, ddiv, even, gamma, term, cosmu, sinmu,
	b, c, d, e, f, g, h, p, q, r, s, d1, d2, q0, pa,pa1, qa,qa1,
	en, en1, nu, ex,  ya,ya1, twobyx, den, odd, aye, dmu, x2, xna;

    en1 = ya = ya1 = 0;		/* -Wall */

    ex = *x;
    nu = *alpha;
    if (*nb > 0 && 0. <= nu && nu < 1.) {
	if(ex < GNM_MIN || ex > xlrg_BESS_Y) {
	    /* Warning is not really appropriate, give
	     * proper limit:
	     * ML_ERROR(ME_RANGE); */
	    *ncalc = *nb;
	    if(ex > xlrg_BESS_Y)  by[0]= 0.; /*was gnm_pinf */
	    else if(ex < GNM_MIN) by[0]=gnm_ninf;
	    for(i=0; i < *nb; i++)
		by[i] = by[0];
	    return;
	}
	xna = gnm_trunc(nu + .5);
	na = (long) xna;
	if (na == 1) {/* <==>  .5 <= *alpha < 1	 <==>  -5. <= nu < 0 */
	    nu -= xna;
	}
	if (nu == -.5) {
	    p = M_SQRT_2dPI / gnm_sqrt(ex);
	    ya = p * gnm_sin(ex);
	    ya1 = -p * gnm_cos(ex);
	} else if (ex < 3.) {
	    /* -------------------------------------------------------------
	       Use Temme's scheme for small X
	       ------------------------------------------------------------- */
	    b = ex * .5;
	    d = -gnm_log(b);
	    f = nu * d;
	    e = gnm_pow(b, -nu);
	    if (gnm_abs(nu) < M_eps_sinc)
		c = M_1_PI;
	    else
		c = nu / gnm_sin(nu * M_PIgnum);

	    /* ------------------------------------------------------------
	       Computation of gnm_sinh(f)/f
	       ------------------------------------------------------------ */
	    if (gnm_abs(f) < 1.) {
		x2 = f * f;
		en = 19.;
		s = 1.;
		for (i = 1; i <= 9; ++i) {
		    s = s * x2 / en / (en - 1.) + 1.;
		    en -= 2.;
		}
	    } else {
		s = (e - GNM_const(1.) / e) * .5 / f;
	    }
	    /* --------------------------------------------------------
	       Computation of 1/gamma(1-a) using Chebyshev polynomials */
	    x2 = nu * nu * 8.;
	    aye = ch[0];
	    even = 0.;
	    alfa = ch[1];
	    odd = 0.;
	    for (i = 3; i <= 19; i += 2) {
		even = -(aye + aye + even);
		aye = -even * x2 - aye + ch[i - 1];
		odd = -(alfa + alfa + odd);
		alfa = -odd * x2 - alfa + ch[i];
	    }
	    even = (even * .5 + aye) * x2 - aye + ch[20];
	    odd = (odd + alfa) * 2.;
	    gamma = odd * nu + even;
	    /* End of computation of 1/gamma(1-a)
	       ----------------------------------------------------------- */
	    g = e * gamma;
	    e = (e + GNM_const(1.) / e) * .5;
	    f = 2. * c * (odd * e + even * s * d);
	    e = nu * nu;
	    p = g * c;
	    q = M_1_PI / g;
	    c = nu * M_PI_2gnum;
	    if (gnm_abs(c) < M_eps_sinc)
		r = 1.;
	    else
		r = gnm_sin(c) / c;

	    r = M_PIgnum * c * r * r;
	    c = 1.;
	    d = -b * b;
	    h = 0.;
	    ya = f + r * q;
	    ya1 = p;
	    en = 1.;

	    while (gnm_abs(g / (1. + gnm_abs(ya))) +
		   gnm_abs(h / (1. + gnm_abs(ya1))) > GNM_EPSILON) {
		f = (f * en + p + q) / (en * en - e);
		c *= (d / en);
		p /= en - nu;
		q /= en + nu;
		g = c * (f + r * q);
		h = c * p - en * g;
		ya += g;
		ya1+= h;
		en += 1.;
	    }
	    ya = -ya;
	    ya1 = -ya1 / b;
	} else if (ex < thresh_BESS_Y) {
	    /* --------------------------------------------------------------
	       Use Temme's scheme for moderate X :  3 <= x < 16
	       -------------------------------------------------------------- */
	    c = (.5 - nu) * (.5 + nu);
	    b = ex + ex;
	    e = ex * M_1_PI * gnm_cos(nu * M_PIgnum) / GNM_EPSILON;
	    e *= e;
	    p = 1.;
	    q = -ex;
	    r = 1. + ex * ex;
	    s = r;
	    en = 2.;
	    while (r * en * en < e) {
		en1 = en + 1.;
		d = (en - 1. + c / en) / s;
		p = (en + en - p * d) / en1;
		q = (-b + q * d) / en1;
		s = p * p + q * q;
		r *= s;
		en = en1;
	    }
	    f = p / s;
	    p = f;
	    g = -q / s;
	    q = g;
L220:
	    en -= 1.;
	    if (en > 0.) {
		r = en1 * (2. - p) - 2.;
		s = b + en1 * q;
		d = (en - 1. + c / en) / (r * r + s * s);
		p = d * r;
		q = d * s;
		e = f + 1.;
		f = p * e - g * q;
		g = q * e + p * g;
		en1 = en;
		goto L220;
	    }
	    f = 1. + f;
	    d = f * f + g * g;
	    pa = f / d;
	    qa = -g / d;
	    d = nu + .5 - p;
	    q += ex;
	    pa1 = (pa * q - qa * d) / ex;
	    qa1 = (qa * q + pa * d) / ex;
	    b = ex - M_PI_2gnum * (nu + .5);
	    c = gnm_cos(b);
	    s = gnm_sin(b);
	    d = M_SQRT_2dPI / gnm_sqrt(ex);
	    ya = d * (pa * s + qa * c);
	    ya1 = d * (qa1 * s - pa1 * c);
	} else { /* x > thresh_BESS_Y */
	    /* ----------------------------------------------------------
	       Use Campbell's asymptotic scheme.
	       ---------------------------------------------------------- */
	    na = 0;
	    d1 = gnm_trunc(ex / fivpi);
	    i = (long) d1;
	    dmu = ex - 15. * d1 - d1 * pim5 - (*alpha + .5) * M_PI_2gnum;
	    if (i - (i / 2 << 1) == 0) {
		cosmu = gnm_cos(dmu);
		sinmu = gnm_sin(dmu);
	    } else {
		cosmu = -gnm_cos(dmu);
		sinmu = -gnm_sin(dmu);
	    }
	    ddiv = 8. * ex;
	    dmu = *alpha;
	    den = gnm_sqrt(ex);
	    for (k = 1; k <= 2; ++k) {
		p = cosmu;
		cosmu = sinmu;
		sinmu = -p;
		d1 = (2. * dmu - 1.) * (2. * dmu + 1.);
		d2 = 0.;
		div = ddiv;
		p = 0.;
		q = 0.;
		q0 = d1 / div;
		term = q0;
		for (i = 2; i <= 20; ++i) {
		    d2 += 8.;
		    d1 -= d2;
		    div += ddiv;
		    term = -term * d1 / div;
		    p += term;
		    d2 += 8.;
		    d1 -= d2;
		    div += ddiv;
		    term *= (d1 / div);
		    q += term;
		    if (gnm_abs(term) <= GNM_EPSILON) {
			break;
		    }
		}
		p += 1.;
		q += q0;
		if (k == 1)
		    ya = M_SQRT_2dPI * (p * cosmu - q * sinmu) / den;
		else
		    ya1 = M_SQRT_2dPI * (p * cosmu - q * sinmu) / den;
		dmu += 1.;
	    }
	}
	if (na == 1) {
	    h = 2. * (nu + 1.) / ex;
	    if (h > 1.) {
		if (gnm_abs(ya1) > GNM_MAX / h) {
		    h = 0.;
		    ya = 0.;
		}
	    }
	    h = h * ya1 - ya;
	    ya = ya1;
	    ya1 = h;
	}

	/* ---------------------------------------------------------------
	   Now have first one or two Y's
	   --------------------------------------------------------------- */
	by[0] = ya;
	*ncalc = 1;
	if(*nb > 1) {
	    by[1] = ya1;
	    if (ya1 != 0.) {
		aye = 1. + *alpha;
		twobyx = GNM_const(2.) / ex;
		*ncalc = 2;
		for (i = 2; i < *nb; ++i) {
		    if (twobyx < 1.) {
			if (gnm_abs(by[i - 1]) * twobyx >= GNM_MAX / aye)
			    goto L450;
		    } else {
			if (gnm_abs(by[i - 1]) >= GNM_MAX / aye / twobyx)
			    goto L450;
		    }
		    by[i] = twobyx * aye * by[i - 1] - by[i - 2];
		    aye += 1.;
		    ++(*ncalc);
		}
	    }
	}
L450:
	for (i = *ncalc; i < *nb; ++i)
	    by[i] = gnm_ninf;/* was 0 */

    } else {
	by[0] = 0.;
	*ncalc = min0(*nb,0) - 1;
    }
}

/* Cleaning up done by tools/import-R:  */
#undef min0

/* ------------------------------------------------------------------------ */

static gboolean
bessel_j_series_domain (gnm_float x, gnm_float v)
{
	/*
	 * The series is valid for all possible values of x and v,
	 * but it isn't efficient for all.
	 */

	if (v <= -0.999) {
		/*
		 * For negative v we must ensure that nothing crazy
		 * happens when |v+k| reaches its minimum.
		 */
		gnm_float rv = gnm_floor (v + 0.5);
		if (gnm_abs (v - rv) * v * v < 1)
			return FALSE;

		/*
		 * The above condition ensure that at most one term
		 * increases relative to the prior term and that the
		 * next term undoes all of that increase.
		 */
	}

	/* For small x, the factorials will dominate quickly.  */
	if (gnm_abs (x) < 4)
		return TRUE;

	/*
	 * The factorials also dominate immediately if x*x/4<|v|.
	 * We must not go too far beyond that.
	 */
	if (x * x / 16 > gnm_abs (v))
		return FALSE;

	return TRUE;
}


static gnm_float
bessel_j_series (gnm_float x, gnm_float v)
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
		int k;
		GnmQuad qxh2;

		gnm_quad_mul (&qxh2, &qxh, &qxh);

		for (k = 1; k < 200; k++) {
			GnmQuad qa, qb;
			gnm_float t;

			gnm_quad_mul (&qt, &qt, &qxh2);
			gnm_quad_init (&qa, -k);
			gnm_quad_sub (&qb, &qv, &qa);
			gnm_quad_mul (&qa, &qa, &qb);
			gnm_quad_div (&qt, &qt, &qa);
			t = gnm_quad_value (&qt);
			if (t == 0)
				break;
			gnm_quad_add (&qs, &qs, &qt);
			s = gnm_quad_value (&qs);
			if (k > 5 &&
			    gnm_abs (t) <= GNM_EPSILON / 1024 * gnm_abs (s) &&
			    gnm_abs (k + v) > 2)
				break;
		}
	}

	s = gnm_ldexp (s, (int)CLAMP (e, G_MININT, G_MAXINT));

	gnm_quad_end (state);

	return s;
}

/* ------------------------------------------------------------------------ */

gnm_float
gnm_bessel_i (gnm_float x, gnm_float alpha)
{
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
	if (x < 0) {
		if (alpha != gnm_floor (alpha))
			return gnm_nan;

		return gnm_fmod (alpha, 2) == 0
			? bessel_j (-x, alpha)  /* Even for even alpha */
			: 0 - bessel_j (-x, alpha);  /* Odd for odd alpha */
	} else
		return bessel_j (x, alpha);
}

gnm_float
gnm_bessel_k (gnm_float x, gnm_float alpha)
{
	return bessel_k (x, alpha, 1);
}

gnm_float
gnm_bessel_y (gnm_float x, gnm_float alpha)
{
	return bessel_y (x, alpha);
}

/* ------------------------------------------------------------------------- */
