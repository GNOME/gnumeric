/* complex/math.c (from the GSL 1.1.1)
 *
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 Jorma Olavi Tähtinen, Brian Gough
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* Basic complex arithmetic functions

 * Original version by Jorma Olavi Tähtinen <jotahtin@cc.hut.fi>
 *
 * Modified for GSL by Brian Gough, 3/2000
 */

/* The following references describe the methods used in these
 * functions,
 *
 *   T. E. Hull and Thomas F. Fairgrieve and Ping Tak Peter Tang,
 *   "Implementing Complex Elementary Functions Using Exception
 *   Handling", ACM Transactions on Mathematical Software, Volume 20
 *   (1994), pp 215-244, Corrigenda, p553
 *
 *   Hull et al, "Implementing the complex arcsin and arccosine
 *   functions using exception handling", ACM Transactions on
 *   Mathematical Software, Volume 23 (1997) pp 299-335
 *
 *   Abramowitz and Stegun, Handbook of Mathematical Functions, "Inverse
 *   Circular Functions in Terms of Real and Imaginary Parts", Formulas
 *   4.4.37, 4.4.38, 4.4.39
 */

/*
 * Gnumeric specific modifications written by Jukka-Pekka Iivonen
 * (jiivonen@hutcs.cs.hut.fi)
 *
 * long double modifications by Morten Welinder.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <func.h>

#include <complex.h>
#include "gsl-complex.h"
#include <parse-util.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <mathfunc.h>
#include <sf-trig.h>


#define GSL_REAL(x) GNM_CRE(*(x))
#define GSL_IMAG(x) GNM_CIM(*(x))

/***********************************************************************
 * Complex arithmetic operators
 ***********************************************************************/

static inline void
gsl_complex_mul_imag (gnm_complex const *a, gnm_float y, gnm_complex *res)
{                               /* z=a*iy */
        *res = GNM_CMAKE (-y * GSL_IMAG (a), y * GSL_REAL (a));
}

void
gsl_complex_inverse (gnm_complex const *a, gnm_complex *res)
{                               /* z=1/a */
        gnm_float s = 1 / GNM_CABS (*a);

	*res = GNM_CMAKE ((GSL_REAL (a) * s) * s, -(GSL_IMAG (a) * s) * s);
}

/**********************************************************************
 * Inverse Complex Trigonometric Functions
 **********************************************************************/

static void
gsl_complex_arcsin_real (gnm_float a, gnm_complex *res)
{                               /* z = arcsin(a) */
         if (gnm_abs (a) <= 1) {
	         *res = GNM_CMAKE (gnm_asin (a), 0.0);
	 } else {
	         if (a < 0) {
		         *res = GNM_CMAKE (-M_PI_2gnum, gnm_acosh (-a));
		 } else {
		         *res = GNM_CMAKE (M_PI_2gnum, -gnm_acosh (a));
		 }
	 }
}

void
gsl_complex_arcsin (gnm_complex const *a, gnm_complex *res)
{                               /* z = arcsin(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (I == 0) {
	        gsl_complex_arcsin_real (R, res);
	} else {
	        gnm_float x = gnm_abs (R), y = gnm_abs (I);
		gnm_float r = gnm_hypot (x + 1, y);
		gnm_float s = gnm_hypot (x - 1, y);
		gnm_float A = GNM_const(0.5) * (r + s);
		gnm_float B = x / A;
		gnm_float y2 = y * y;

		gnm_float real, imag;

		const gnm_float A_crossover = 1.5, B_crossover = 0.6417;

		if (B <= B_crossover) {
		        real = gnm_asin (B);
		} else {
		        if (x <= 1) {
			        gnm_float D = GNM_const(0.5) * (A + x) *
				        (y2 / (r + x + 1) + (s + (1 - x)));
				real = gnm_atan (x / gnm_sqrt (D));
			} else {
			        gnm_float Apx = A + x;
				gnm_float D = GNM_const(0.5) * (Apx / (r + x + 1)
						      + Apx / (s + (x - 1)));
				real = gnm_atan (x / (y * gnm_sqrt (D)));
			}
		}

		if (A <= A_crossover) {
		        gnm_float Am1;

			if (x < 1) {
			        Am1 = GNM_const(0.5) * (y2 / (r + (x + 1)) + y2 /
					     (s + (1 - x)));
			} else {
			        Am1 = GNM_const(0.5) * (y2 / (r + (x + 1)) +
					     (s + (x - 1)));
			}

			imag = gnm_log1p (Am1 + gnm_sqrt (Am1 * (A + 1)));
		} else {
		        imag = gnm_log (A + gnm_sqrt (A * A - 1));
		}

		*res = GNM_CMAKE ((R >= 0) ? real : -real, (I >= 0) ?
			      imag : -imag);
	}
}

static void
gsl_complex_arccos_real (gnm_float a, gnm_complex *res)
{                               /* z = arccos(a) */
        if (gnm_abs (a) <= 1) {
	        *res = GNM_CMAKE (gnm_acos (a), 0);
	} else {
	        if (a < 0) {
		        *res = GNM_CMAKE (M_PIgnum, -gnm_acosh (-a));
		} else {
		        *res = GNM_CMAKE (0, gnm_acosh (a));
		}
	}
}

void
gsl_complex_arccos (gnm_complex const *a, gnm_complex *res)
{                               /* z = arccos(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (I == 0) {
	        gsl_complex_arccos_real (R, res);
	} else {
	        gnm_float x = gnm_abs (R);
		gnm_float y = gnm_abs (I);
		gnm_float r = gnm_hypot (x + 1, y);
		gnm_float s = gnm_hypot (x - 1, y);
		gnm_float A = GNM_const(0.5) * (r + s);
		gnm_float B = x / A;
		gnm_float y2 = y * y;

		gnm_float real, imag;

		const gnm_float A_crossover = 1.5;
		const gnm_float B_crossover = 0.6417;

		if (B <= B_crossover) {
		        real = gnm_acos (B);
		} else {
		        if (x <= 1) {
			        gnm_float D = GNM_const(0.5) * (A + x) *
				        (y2 / (r + x + 1) + (s + (1 - x)));
				real = gnm_atan (gnm_sqrt (D) / x);
			} else {
			        gnm_float Apx = A + x;
				gnm_float D = GNM_const(0.5) * (Apx / (r + x + 1) + Apx /
						      (s + (x - 1)));
				real = gnm_atan ((y * gnm_sqrt (D)) / x);
			}
		}
		if (A <= A_crossover) {
		        gnm_float Am1;

			if (x < 1) {
			        Am1 = GNM_const(0.5) * (y2 / (r + (x + 1)) + y2 /
					     (s + (1 - x)));
			} else {
			        Am1 = GNM_const(0.5) * (y2 / (r + (x + 1)) +
					     (s + (x - 1)));
			}

			imag = gnm_log1p (Am1 + gnm_sqrt (Am1 * (A + 1)));
		} else {
		        imag = gnm_log (A + gnm_sqrt (A * A - 1));
		}

		*res = GNM_CMAKE ((R >= 0) ? real : M_PIgnum - real, (I >= 0) ?
			      -imag : imag);
	}
}

void
gsl_complex_arctan (gnm_complex const *a, gnm_complex *res)
{                               /* z = arctan(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (I == 0) {
	        *res = GNM_CMAKE (gnm_atan (R), 0);
	} else {
	        /* FIXME: This is a naive implementation which does not fully
		 * take into account cancellation errors, overflow, underflow
		 * etc.  It would benefit from the Hull et al treatment. */

	        gnm_float r = gnm_hypot (R, I);

		gnm_float imag;

		gnm_float u = 2 * I / (1 + r * r);

		/* FIXME: the following cross-over should be optimized but 0.1
		 * seems to work ok */

		if (gnm_abs (u) < GNM_const(0.1)) {
		        imag = GNM_const(0.25) * (gnm_log1p (u) - gnm_log1p (-u));
		} else {
		        gnm_float A = gnm_hypot (R, I + 1);
			gnm_float B = gnm_hypot (R, I - 1);
			imag = GNM_const(0.5) * gnm_log (A / B);
		}
		if (R == 0) {
		        if (I > 1) {
			        *res = GNM_CMAKE (M_PI_2gnum, imag);
			} else if (I < -1) {
			        *res = GNM_CMAKE (-M_PI_2gnum, imag);
			} else {
			        *res = GNM_CMAKE (0, imag);
			}
		} else {
		        *res = GNM_CMAKE (GNM_const(0.5) * gnm_atan2 (2 * R,
							    ((1 + r) * (1 - r))),
				      imag);
		}
	}
}

void
gsl_complex_arcsec (gnm_complex const *a, gnm_complex *res)
{                               /* z = arcsec(a) */
        gsl_complex_inverse (a, res);
	gsl_complex_arccos (res, res);
}

void
gsl_complex_arccsc (gnm_complex const *a, gnm_complex *res)
{                               /* z = arccsc(a) */
        gsl_complex_inverse (a, res);
	gsl_complex_arcsin (res, res);
}

void
gsl_complex_arccot (gnm_complex const *a, gnm_complex *res)
{                               /* z = arccot(a) */
        if (GSL_REAL (a) == 0 && GSL_IMAG (a) == 0) {
	        *res = GNM_CMAKE (M_PI_2gnum, 0);
	} else {
	        gsl_complex_inverse (a, res);
		gsl_complex_arctan (res, res);
	}
}

/**********************************************************************
 * Complex Hyperbolic Functions
 **********************************************************************/

void
gsl_complex_sinh (gnm_complex const *a, gnm_complex *res)
{                               /* z = sinh(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	*res = GNM_CMAKE (gnm_sinh (R) * gnm_cos (I), gnm_cosh (R) * gnm_sin (I));
}

void
gsl_complex_cosh (gnm_complex const *a, gnm_complex *res)
{                               /* z = cosh(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	*res = GNM_CMAKE (gnm_cosh (R) * gnm_cos (I), gnm_sinh (R) * gnm_sin (I));
}

void
gsl_complex_tanh (gnm_complex const *a, gnm_complex *res)
{                               /* z = tanh(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (gnm_abs (R) < 1) {
	         gnm_float D =
			 gnm_pow (gnm_cos (I), 2.0) +
			 gnm_pow (gnm_sinh (R), 2.0);

		 *res = GNM_CMAKE (gnm_sinh (R) * gnm_cosh (R) / D,
			       GNM_const(0.5) * gnm_sin (2 * I) / D);
	} else {
	         gnm_float D =
			 gnm_pow (gnm_cos (I), 2.0) +
			 gnm_pow (gnm_sinh (R), 2.0);
		 gnm_float F = 1 + gnm_pow (gnm_cos (I) / gnm_sinh (R), 2.0);

		 *res = GNM_CMAKE (1 / (gnm_tanh (R) * F),
			       GNM_const(0.5) * gnm_sin (2 * I) / D);
	}
}

void
gsl_complex_sech (gnm_complex const *a, gnm_complex *res)
{                               /* z = sech(a) */
        gsl_complex_cosh (a, res);
	gsl_complex_inverse (res, res);
}

void
gsl_complex_csch (gnm_complex const *a, gnm_complex *res)
{                               /* z = csch(a) */
        gsl_complex_sinh (a, res);
	gsl_complex_inverse (res, res);
}

void
gsl_complex_coth (gnm_complex const *a, gnm_complex *res)
{                               /* z = coth(a) */
        gsl_complex_tanh (a, res);
	gsl_complex_inverse (res, res);
}

/**********************************************************************
 * Inverse Complex Hyperbolic Functions
 **********************************************************************/

void
gsl_complex_arcsinh (gnm_complex const *a, gnm_complex *res)
{                               /* z = arcsinh(a) */
        gsl_complex_mul_imag (a, 1.0, res);
	gsl_complex_arcsin (res, res);
	gsl_complex_mul_imag (res, -1.0, res);
}

void
gsl_complex_arccosh (gnm_complex const *a, gnm_complex *res)
{                               /* z = arccosh(a) */
	if (GSL_IMAG (a) == 0 && GSL_REAL (a) == 1)
		*res = GNM_C0;
	else {
		gsl_complex_arccos (a, res);
		gsl_complex_mul_imag (res, GSL_IMAG (res) > 0 ? -1 : 1, res);
	}
}

static void
gsl_complex_arctanh_real (gnm_float a, gnm_complex *res)
{                               /* z = arctanh(a) */
        if (a > -1 && a < 1) {
	        *res = GNM_CMAKE (gnm_atanh (a), 0);
	} else {
	        *res = GNM_CMAKE (gnm_acoth (a),
			      (a < 0) ? M_PI_2gnum : -M_PI_2gnum);
	}
}

void
gsl_complex_arctanh (gnm_complex const *a, gnm_complex *res)
{                               /* z = arctanh(a) */
        if (GSL_IMAG (a) == 0) {
	        gsl_complex_arctanh_real (GSL_REAL (a), res);
	} else {
	         gsl_complex_mul_imag (a, 1.0, res);
		 gsl_complex_arctan (res, res);
		 gsl_complex_mul_imag (res, -1.0, res);
	}
}

void
gsl_complex_arcsech (gnm_complex const *a, gnm_complex *res)
{                               /* z = arcsech(a); */
        gsl_complex_inverse (a, res);
	gsl_complex_arccosh (res, res);
}

void
gsl_complex_arccsch (gnm_complex const *a, gnm_complex *res)
{                               /* z = arccsch(a); */
        gsl_complex_inverse (a, res);
	gsl_complex_arcsinh (res, res);
}

void
gsl_complex_arccoth (gnm_complex const *a, gnm_complex *res)
{                               /* z = arccoth(a); */
        gsl_complex_inverse (a, res);
	gsl_complex_arctanh (res, res);
}
