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
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>

#include <complex.h>
#include <parse-util.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <auto-format.h>
#include <libgnome/gnome-i18n.h>
#include <mathfunc.h>


#define GSL_REAL(x) (x)->re
#define GSL_IMAG(x) (x)->im

/**********************************************************************
 * Properties of complex numbers
 **********************************************************************/

static inline gnum_float
gsl_complex_abs (complex_t *z)
{                               /* return |z| */
        return hypot (GSL_REAL (z), GSL_IMAG (z));
}


/***********************************************************************
 * Complex arithmetic operators
 ***********************************************************************/

static inline void
gsl_complex_mul_imag (complex_t *a, gnum_float y, complex_t *res)
{                               /* z=a*iy */
        complex_init (res, -y * GSL_IMAG (a), y * GSL_REAL (a));
}

static inline void
gsl_complex_inverse (complex_t *a, complex_t *res)
{                               /* z=1/a */
        gnum_float s = 1.0 / gsl_complex_abs (a);

	complex_init (res, (GSL_REAL (a) * s) * s, -(GSL_IMAG (a) * s) * s);
}

/**********************************************************************
 * Inverse Complex Trigonometric Functions
 **********************************************************************/

static void
gsl_complex_arcsin_real (gnum_float a, complex_t *res)
{                               /* z = arcsin(a) */
         if (fabs (a) <= 1.0) {
	         complex_init (res, asin (a), 0.0);
	 } else {
	         if (a < 0.0) {
		         complex_init (res, -M_PI_2, acosh (-a));
		 } else {
		         complex_init (res, M_PI_2, -acosh (a));
		 }
	 }
}

void
gsl_complex_arcsin (complex_t *a, complex_t *res)
{                               /* z = arcsin(a) */
        gnum_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (I == 0) {
	        gsl_complex_arcsin_real (R, res);
	} else {
	        gnum_float x = fabs (R), y = fabs (I);
		gnum_float r = hypot (x + 1, y), s = hypot (x - 1, y);
		gnum_float A = 0.5 * (r + s);
		gnum_float B = x / A;
		gnum_float y2 = y * y;

		gnum_float real, imag;

		const gnum_float A_crossover = 1.5, B_crossover = 0.6417;

		if (B <= B_crossover) {
		        real = asin (B);
		} else {
		        if (x <= 1) {
			        gnum_float D = 0.5 * (A + x) *
				        (y2 / (r + x + 1) + (s + (1 - x)));
				real = atan (x / sqrt (D));
			} else {
			        gnum_float Apx = A + x;
				gnum_float D = 0.5 * (Apx / (r + x + 1)
						      + Apx / (s + (x - 1)));
				real = atan (x / (y * sqrt (D)));
			}
		}

		if (A <= A_crossover) {
		        gnum_float Am1;

			if (x < 1) {
			        Am1 = 0.5 * (y2 / (r + (x + 1)) + y2 /
					     (s + (1 - x)));
			} else {
			        Am1 = 0.5 * (y2 / (r + (x + 1)) +
					     (s + (x - 1)));
			}

			imag = log1p (Am1 + sqrt (Am1 * (A + 1)));
		} else {
		        imag = log (A + sqrt (A * A - 1));
		}

		complex_init (res, (R >= 0) ? real : -real, (I >= 0) ?
			      imag : -imag);
	}
}

static void
gsl_complex_arccos_real (gnum_float a, complex_t *res)
{                               /* z = arccos(a) */
        if (fabs (a) <= 1.0) {
	        complex_init (res, acos (a), 0);
	} else {
	        if (a < 0.0) {
		        complex_init (res, M_PI, -acosh (-a));
		} else {
		        complex_init (res, 0, acosh (a));
		}
	}
}

void
gsl_complex_arccos (complex_t *a, complex_t *res)
{                               /* z = arccos(a) */
        gnum_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (I == 0) {
	        gsl_complex_arccos_real (R, res);
	} else {
	        gnum_float x = fabs (R), y = fabs (I);
		gnum_float r = hypot (x + 1, y), s = hypot (x - 1, y);
		gnum_float A = 0.5 * (r + s);
		gnum_float B = x / A;
		gnum_float y2 = y * y;

		gnum_float real, imag;

		const gnum_float A_crossover = 1.5, B_crossover = 0.6417;

		if (B <= B_crossover) {
		        real = acos (B);
		} else {
		        if (x <= 1) {
			        gnum_float D = 0.5 * (A + x) *
				        (y2 / (r + x + 1) + (s + (1 - x)));
				real = atan (sqrt (D) / x);
			} else {
			        gnum_float Apx = A + x;
				gnum_float D = 0.5 * (Apx / (r + x + 1) + Apx /
						      (s + (x - 1)));
				real = atan ((y * sqrt (D)) / x);
			}
		}
		if (A <= A_crossover) {
		        gnum_float Am1;

			if (x < 1) {
			        Am1 = 0.5 * (y2 / (r + (x + 1)) + y2 /
					     (s + (1 - x)));
			} else {
			        Am1 = 0.5 * (y2 / (r + (x + 1)) +
					     (s + (x - 1)));
			}

			imag = log1p (Am1 + sqrt (Am1 * (A + 1)));
		} else {
		        imag = log (A + sqrt (A * A - 1));
		}

		complex_init (res, (R >= 0) ? real : M_PI - real, (I >= 0) ?
			      -imag : imag);
	}
}

void
gsl_complex_arctan (complex_t *a, complex_t *res)
{                               /* z = arctan(a) */
        gnum_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (I == 0) {
	        complex_init (res, atan (R), 0);
	} else {
	        /* FIXME: This is a naive implementation which does not fully
		 * take into account cancellation errors, overflow, underflow
		 * etc.  It would benefit from the Hull et al treatment. */

	        gnum_float r = hypot (R, I);

		gnum_float imag;

		gnum_float u = 2 * I / (1 + r * r);

		/* FIXME: the following cross-over should be optimized but 0.1
		 * seems to work ok */

		if (fabs (u) < 0.1) {
		        imag = 0.25 * (log1p (u) - log1p (-u));
		} else {
		        gnum_float A = hypot (R, I + 1);
			gnum_float B = hypot (R, I - 1);
			imag = 0.5 * log (A / B);
		}
		if (R == 0) {
		        if (I > 1) {
			        complex_init (res, M_PI_2, imag);
			} else if (I < -1) {
			        complex_init (res, -M_PI_2, imag);
			} else {
			        complex_init (res, 0, imag);
			}
		} else {
		        complex_init (res, 0.5 * atan2 (2 * R,
							((1 + r) * (1 - r))),
				      imag);
		}
	}
}

void
gsl_complex_arcsec (complex_t *a, complex_t *res)
{                               /* z = arcsec(a) */
        gsl_complex_inverse (a, res);
	gsl_complex_arccos (res, res);
}

void
gsl_complex_arccsc (complex_t *a, complex_t *res)
{                               /* z = arccsc(a) */
        gsl_complex_inverse (a, res);
	gsl_complex_arcsin (res, res);
}

void
gsl_complex_arccot (complex_t *a, complex_t *res)
{                               /* z = arccot(a) */
        if (GSL_REAL (a) == 0.0 && GSL_IMAG (a) == 0.0) {
	        complex_init (res, M_PI_2, 0);
	} else {
	        gsl_complex_inverse (a, res);
		gsl_complex_arctan (res, res);
	}
}

/**********************************************************************
 * Complex Hyperbolic Functions
 **********************************************************************/

void
gsl_complex_sinh (complex_t *a, complex_t *res)
{                               /* z = sinh(a) */
        gnum_float R = GSL_REAL (a), I = GSL_IMAG (a);

	complex_init (res, sinh (R) * cos (I), cosh (R) * sin (I));
}

void
gsl_complex_cosh (complex_t *a, complex_t *res)
{                               /* z = cosh(a) */
        gnum_float R = GSL_REAL (a), I = GSL_IMAG (a);

	complex_init (res, cosh (R) * cos (I), sinh (R) * sin (I));
}

void
gsl_complex_tanh (complex_t *a, complex_t *res)
{                               /* z = tanh(a) */
        gnum_float R = GSL_REAL (a), I = GSL_IMAG (a);

	if (fabs(R) < 1.0) {
	         gnum_float D = pow (cos (I), 2.0) + pow (sinh (R), 2.0);
      
		 complex_init (res, sinh (R) * cosh (R) / D,
			       0.5 * sin (2 * I) / D);
	} else {
	         gnum_float D = pow (cos (I), 2.0) + pow (sinh (R), 2.0);
		 gnum_float F = 1 + pow (cos (I) / sinh (R), 2.0);

		 complex_init (res, 1.0 / (tanh (R) * F),
			       0.5 * sin (2 * I) / D);
	}
}

void
gsl_complex_sech (complex_t *a, complex_t *res)
{                               /* z = sech(a) */
        gsl_complex_cosh (a, res);
	gsl_complex_inverse (res, res);
}

void
gsl_complex_csch (complex_t *a, complex_t *res)
{                               /* z = csch(a) */
        gsl_complex_sinh (a, res);
	gsl_complex_inverse (res, res);
}

void
gsl_complex_coth (complex_t *a, complex_t *res)
{                               /* z = coth(a) */
        gsl_complex_tanh (a, res);
	gsl_complex_inverse (res, res);
}

/**********************************************************************
 * Inverse Complex Hyperbolic Functions
 **********************************************************************/

void
gsl_complex_arcsinh (complex_t *a, complex_t *res)
{                               /* z = arcsinh(a) */
        gsl_complex_mul_imag (a, 1.0, res);
	gsl_complex_arcsin (res, res);
	gsl_complex_mul_imag (res, -1.0, res);
}

void
gsl_complex_arccosh (complex_t *a, complex_t *res)
{                               /* z = arccosh(a) */
        gsl_complex_arccos (a, res);
	gsl_complex_mul_imag (res, GSL_IMAG(res) > 0 ? -1.0 : 1.0, res);
}

static void
gsl_complex_arctanh_real (gnum_float a, complex_t *res)
{                               /* z = arctanh(a) */
        if (a > -1.0 && a < 1.0) {
	        complex_init (res, atanh (a), 0);
	} else {
	        complex_init (res, atanh (1 / a), (a < 0) ? M_PI_2 : -M_PI_2);
	}
}

void
gsl_complex_arctanh (complex_t *a, complex_t *res)
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
gsl_complex_arcsech (complex_t *a, complex_t *res)
{                               /* z = arcsech(a); */
        gsl_complex_inverse (a, res);
	gsl_complex_arccosh (res, res);
}

void
gsl_complex_arccsch (complex_t *a, complex_t *res)
{                               /* z = arccsch(a); */
        gsl_complex_inverse (a, res);
	gsl_complex_arcsinh (res, res);
}

void
gsl_complex_arccoth (complex_t *a, complex_t *res)
{                               /* z = arccoth(a); */
        gsl_complex_inverse (a, res);
	gsl_complex_arctanh (res, res);
}

