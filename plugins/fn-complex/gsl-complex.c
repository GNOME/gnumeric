/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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


#define GSL_REAL(x) (x)->re
#define GSL_IMAG(x) (x)->im

/***********************************************************************
 * Complex arithmetic operators
 ***********************************************************************/

static inline void
gsl_complex_mul_imag (complex_t const *a, gnm_float y, complex_t *res)
{                               /* z=a*iy */
        complex_init (res, -y * GSL_IMAG (a), y * GSL_REAL (a));
}

void
gsl_complex_inverse (complex_t const *a, complex_t *res)
{                               /* z=1/a */
        gnm_float s = 1.0 / complex_mod (a);

	complex_init (res, (GSL_REAL (a) * s) * s, -(GSL_IMAG (a) * s) * s);
}

void
gsl_complex_negative (complex_t const *a, complex_t *res)
{                               /* z=1/a */
	complex_init (res, -GSL_REAL (a), -GSL_IMAG (a));
}

/**********************************************************************
 * Inverse Complex Trigonometric Functions
 **********************************************************************/

static void
gsl_complex_arcsin_real (gnm_float a, complex_t *res)
{                               /* z = arcsin(a) */
         if (gnm_abs (a) <= 1.0) {
	         complex_init (res, gnm_asin (a), 0.0);
	 } else {
	         if (a < 0.0) {
		         complex_init (res, -M_PI_2gnum, gnm_acosh (-a));
		 } else {
		         complex_init (res, M_PI_2gnum, -gnm_acosh (a));
		 }
	 }
}

void
gsl_complex_arcsin (complex_t const *a, complex_t *res)
{                               /* z = arcsin(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (I == 0) {
	        gsl_complex_arcsin_real (R, res);
	} else {
	        gnm_float x = gnm_abs (R), y = gnm_abs (I);
		gnm_float r = gnm_hypot (x + 1, y);
		gnm_float s = gnm_hypot (x - 1, y);
		gnm_float A = 0.5 * (r + s);
		gnm_float B = x / A;
		gnm_float y2 = y * y;

		gnm_float real, imag;

		const gnm_float A_crossover = 1.5, B_crossover = 0.6417;

		if (B <= B_crossover) {
		        real = gnm_asin (B);
		} else {
		        if (x <= 1) {
			        gnm_float D = 0.5 * (A + x) *
				        (y2 / (r + x + 1) + (s + (1 - x)));
				real = gnm_atan (x / gnm_sqrt (D));
			} else {
			        gnm_float Apx = A + x;
				gnm_float D = 0.5 * (Apx / (r + x + 1)
						      + Apx / (s + (x - 1)));
				real = gnm_atan (x / (y * gnm_sqrt (D)));
			}
		}

		if (A <= A_crossover) {
		        gnm_float Am1;

			if (x < 1) {
			        Am1 = 0.5 * (y2 / (r + (x + 1)) + y2 /
					     (s + (1 - x)));
			} else {
			        Am1 = 0.5 * (y2 / (r + (x + 1)) +
					     (s + (x - 1)));
			}

			imag = gnm_log1p (Am1 + gnm_sqrt (Am1 * (A + 1)));
		} else {
		        imag = gnm_log (A + gnm_sqrt (A * A - 1));
		}

		complex_init (res, (R >= 0) ? real : -real, (I >= 0) ?
			      imag : -imag);
	}
}

static void
gsl_complex_arccos_real (gnm_float a, complex_t *res)
{                               /* z = arccos(a) */
        if (gnm_abs (a) <= 1.0) {
	        complex_init (res, gnm_acos (a), 0);
	} else {
	        if (a < 0.0) {
		        complex_init (res, M_PIgnum, -gnm_acosh (-a));
		} else {
		        complex_init (res, 0, gnm_acosh (a));
		}
	}
}

void
gsl_complex_arccos (complex_t const *a, complex_t *res)
{                               /* z = arccos(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (I == 0) {
	        gsl_complex_arccos_real (R, res);
	} else {
	        gnm_float x = gnm_abs (R);
		gnm_float y = gnm_abs (I);
		gnm_float r = gnm_hypot (x + 1, y);
		gnm_float s = gnm_hypot (x - 1, y);
		gnm_float A = 0.5 * (r + s);
		gnm_float B = x / A;
		gnm_float y2 = y * y;

		gnm_float real, imag;

		const gnm_float A_crossover = 1.5;
		const gnm_float B_crossover = 0.6417;

		if (B <= B_crossover) {
		        real = gnm_acos (B);
		} else {
		        if (x <= 1) {
			        gnm_float D = 0.5 * (A + x) *
				        (y2 / (r + x + 1) + (s + (1 - x)));
				real = gnm_atan (gnm_sqrt (D) / x);
			} else {
			        gnm_float Apx = A + x;
				gnm_float D = 0.5 * (Apx / (r + x + 1) + Apx /
						      (s + (x - 1)));
				real = gnm_atan ((y * gnm_sqrt (D)) / x);
			}
		}
		if (A <= A_crossover) {
		        gnm_float Am1;

			if (x < 1) {
			        Am1 = 0.5 * (y2 / (r + (x + 1)) + y2 /
					     (s + (1 - x)));
			} else {
			        Am1 = 0.5 * (y2 / (r + (x + 1)) +
					     (s + (x - 1)));
			}

			imag = gnm_log1p (Am1 + gnm_sqrt (Am1 * (A + 1)));
		} else {
		        imag = gnm_log (A + gnm_sqrt (A * A - 1));
		}

		complex_init (res, (R >= 0) ? real : M_PIgnum - real, (I >= 0) ?
			      -imag : imag);
	}
}

void
gsl_complex_arctan (complex_t const *a, complex_t *res)
{                               /* z = arctan(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (I == 0) {
	        complex_init (res, gnm_atan (R), 0);
	} else {
	        /* FIXME: This is a naive implementation which does not fully
		 * take into account cancellation errors, overflow, underflow
		 * etc.  It would benefit from the Hull et al treatment. */

	        gnm_float r = gnm_hypot (R, I);

		gnm_float imag;

		gnm_float u = 2 * I / (1 + r * r);

		/* FIXME: the following cross-over should be optimized but 0.1
		 * seems to work ok */

		if (gnm_abs (u) < 0.1) {
		        imag = 0.25 * (gnm_log1p (u) - gnm_log1p (-u));
		} else {
		        gnm_float A = gnm_hypot (R, I + 1);
			gnm_float B = gnm_hypot (R, I - 1);
			imag = 0.5 * gnm_log (A / B);
		}
		if (R == 0) {
		        if (I > 1) {
			        complex_init (res, M_PI_2gnum, imag);
			} else if (I < -1) {
			        complex_init (res, -M_PI_2gnum, imag);
			} else {
			        complex_init (res, 0, imag);
			}
		} else {
		        complex_init (res, 0.5 * gnm_atan2 (2 * R,
							    ((1 + r) * (1 - r))),
				      imag);
		}
	}
}

void
gsl_complex_arcsec (complex_t const *a, complex_t *res)
{                               /* z = arcsec(a) */
        gsl_complex_inverse (a, res);
	gsl_complex_arccos (res, res);
}

void
gsl_complex_arccsc (complex_t const *a, complex_t *res)
{                               /* z = arccsc(a) */
        gsl_complex_inverse (a, res);
	gsl_complex_arcsin (res, res);
}

void
gsl_complex_arccot (complex_t const *a, complex_t *res)
{                               /* z = arccot(a) */
        if (GSL_REAL (a) == 0.0 && GSL_IMAG (a) == 0.0) {
	        complex_init (res, M_PI_2gnum, 0);
	} else {
	        gsl_complex_inverse (a, res);
		gsl_complex_arctan (res, res);
	}
}

/**********************************************************************
 * Complex Hyperbolic Functions
 **********************************************************************/

void
gsl_complex_sinh (complex_t const *a, complex_t *res)
{                               /* z = sinh(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	complex_init (res, gnm_sinh (R) * gnm_cos (I), cosh (R) * gnm_sin (I));
}

void
gsl_complex_cosh (complex_t const *a, complex_t *res)
{                               /* z = cosh(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	complex_init (res, cosh (R) * gnm_cos (I), gnm_sinh (R) * gnm_sin (I));
}

void
gsl_complex_tanh (complex_t const *a, complex_t *res)
{                               /* z = tanh(a) */
        gnm_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (gnm_abs (R) < 1.0) {
	         gnm_float D =
			 gnm_pow (gnm_cos (I), 2.0) +
			 gnm_pow (gnm_sinh (R), 2.0);

		 complex_init (res, gnm_sinh (R) * cosh (R) / D,
			       0.5 * gnm_sin (2 * I) / D);
	} else {
	         gnm_float D =
			 gnm_pow (gnm_cos (I), 2.0) +
			 gnm_pow (gnm_sinh (R), 2.0);
		 gnm_float F = 1 + gnm_pow (gnm_cos (I) / gnm_sinh (R), 2.0);

		 complex_init (res, 1.0 / (gnm_tanh (R) * F),
			       0.5 * gnm_sin (2 * I) / D);
	}
}

void
gsl_complex_sech (complex_t const *a, complex_t *res)
{                               /* z = sech(a) */
        gsl_complex_cosh (a, res);
	gsl_complex_inverse (res, res);
}

void
gsl_complex_csch (complex_t const *a, complex_t *res)
{                               /* z = csch(a) */
        gsl_complex_sinh (a, res);
	gsl_complex_inverse (res, res);
}

void
gsl_complex_coth (complex_t const *a, complex_t *res)
{                               /* z = coth(a) */
        gsl_complex_tanh (a, res);
	gsl_complex_inverse (res, res);
}

/**********************************************************************
 * Inverse Complex Hyperbolic Functions
 **********************************************************************/

void
gsl_complex_arcsinh (complex_t const *a, complex_t *res)
{                               /* z = arcsinh(a) */
        gsl_complex_mul_imag (a, 1.0, res);
	gsl_complex_arcsin (res, res);
	gsl_complex_mul_imag (res, -1.0, res);
}

void
gsl_complex_arccosh (complex_t const *a, complex_t *res)
{                               /* z = arccosh(a) */
        gsl_complex_arccos (a, res);
	gsl_complex_mul_imag (res, GSL_IMAG (res) > 0 ? -1.0 : 1.0, res);
}

static void
gsl_complex_arctanh_real (gnm_float a, complex_t *res)
{                               /* z = arctanh(a) */
        if (a > -1.0 && a < 1.0) {
	        complex_init (res, gnm_atanh (a), 0);
	} else {
	        complex_init (res, gnm_atanh (1 / a),
			      (a < 0) ? M_PI_2gnum : -M_PI_2gnum);
	}
}

void
gsl_complex_arctanh (complex_t const *a, complex_t *res)
{                               /* z = arctanh(a) */
        if (GSL_IMAG (a) == 0.0) {
	        gsl_complex_arctanh_real (GSL_REAL (a), res);
	} else {
	         gsl_complex_mul_imag (a, 1.0, res);
		 gsl_complex_arctan (res, res);
		 gsl_complex_mul_imag (res, -1.0, res);
	}
}

void
gsl_complex_arcsech (complex_t const *a, complex_t *res)
{                               /* z = arcsech(a); */
        gsl_complex_inverse (a, res);
	gsl_complex_arccosh (res, res);
}

void
gsl_complex_arccsch (complex_t const *a, complex_t *res)
{                               /* z = arccsch(a); */
        gsl_complex_inverse (a, res);
	gsl_complex_arcsinh (res, res);
}

void
gsl_complex_arccoth (complex_t const *a, complex_t *res)
{                               /* z = arccoth(a); */
        gsl_complex_inverse (a, res);
	gsl_complex_arctanh (res, res);
}
