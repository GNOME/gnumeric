/*
 * complex.c:  A quick library for complex math.
 *
 * Author:
 *  Morten Welinder <terra@gnome.org>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#define GNUMERIC_COMPLEX_IMPLEMENTATION
#include "complex.h"

#include <stdlib.h>
#include <errno.h>
#include <mathfunc.h>

/* ------------------------------------------------------------------------- */

char *
complex_to_string (complex_t const *src, char const *reformat,
		   char const *imformat, char imunit)
{
	char *re_buffer = NULL;
	char *im_buffer = NULL;
	char const *sign = "";
	char const *suffix = "";
	char *res;
	char suffix_buffer[2];

	if (src->re != 0 || src->im == 0) {
		/* We have a real part.  */
		re_buffer = g_strdup_printf (reformat, src->re);
	}

	if (src->im != 0) {
		/* We have an imaginary part.  */
		suffix = suffix_buffer;
		suffix_buffer[0] = imunit;
		suffix_buffer[1] = 0;
		if (src->im == 1) {
			if (re_buffer)
				sign = "+";
		} else if (src->im == -1) {
			sign = "-";
		} else {
			im_buffer = g_strdup_printf (imformat, src->im);
			if (re_buffer && *im_buffer != '-' && *im_buffer != '+')
				sign = (src->im >= 0) ? "+" : "-";
		}
	}

	res = g_strconcat (re_buffer ? re_buffer : "",
			   sign,
			   im_buffer ? im_buffer : "",
			   suffix,
			   NULL);

	g_free (re_buffer);
	g_free (im_buffer);

	return res;
}

/* ------------------------------------------------------------------------- */

static int
is_unit_imaginary (char const *src, gnm_float *im, char *imunit)
{
	if (*src == '-') {
		*im = -1.0;
		src++;
	} else {
		*im = +1.0;
		if (*src == '+') src++;
	}

	if ((*src == 'i' || *src == 'j') && src[1] == 0) {
		*imunit = *src;
		return 1;
	} else
		return 0;
}

int
complex_from_string (complex_t *dst, char const *src, char *imunit)
{
	gnm_float x, y;
	char *end;

	/* Case: "i", "+i", "-i", ...  */
	if (is_unit_imaginary (src, &dst->im, imunit)) {
		dst->re = 0;
		return 0;
	}

	x = gnm_strto (src, &end);
	if (src == end || errno == ERANGE)
		return -1;
	src = end;

	/* Case: "42", "+42", "-42", ...  */
	if (*src == 0) {
		complex_real (dst, x);
		*imunit = 'i';
		return 0;
	}

	/* Case: "42i", "+42i", "-42i", ...  */
	if ((*src == 'i' || *src == 'j') && src[1] == 0) {
		complex_init (dst, 0, x);
		*imunit = *src;
		return 0;
	}

	if (*src != '-' && *src != '+')
		return -1;

	/* Case: "42+i", "+42-i", "-42-i", ...  */
	if (is_unit_imaginary (src, &dst->im, imunit)) {
		dst->re = x;
		return 0;
	}

	y = gnm_strto (src, &end);
	if (src == end || errno == ERANGE)
		return -1;
	src = end;

	/* Case: "42+12i", "+42-12i", "-42-12i", ...  */
	if ((*src == 'i' || *src == 'j') && src[1] == 0) {
		complex_init (dst, x, y);
		*imunit = *src;
		return 0;
	}

	return -1;
}

/* ------------------------------------------------------------------------- */

void
complex_to_polar (gnm_float *mod, gnm_float *angle, complex_t const *src)
{
	*mod = complex_mod (src);
	*angle = complex_angle (src);
}

/* ------------------------------------------------------------------------- */

void
complex_from_polar (complex_t *dst, gnm_float mod, gnm_float angle)
{
	complex_init (dst, mod * gnm_cos (angle), mod * gnm_sin (angle));
}

/* ------------------------------------------------------------------------- */

void
complex_mul (complex_t *dst, complex_t const *a, complex_t const *b)
{
	complex_init (dst,
		      a->re * b->re - a->im * b->im,
		      a->re * b->im + a->im * b->re);
}

/* ------------------------------------------------------------------------- */

void
complex_div (complex_t *dst, complex_t const *a, complex_t const *b)
{
	gnm_float bmod = complex_mod (b);

	if (bmod >= GNM_const(1e10)) {
		/* Ok, it's big.  */
		gnm_float a_re = a->re / bmod;
		gnm_float a_im = a->im / bmod;
		gnm_float b_re = b->re / bmod;
		gnm_float b_im = b->im / bmod;
		complex_init (dst,
			      a_re * b_re + a_im * b_im,
			      a_im * b_re - a_re * b_im);
	} else {
		gnm_float bmodsqr = bmod * bmod;
		complex_init (dst,
			      (a->re * b->re + a->im * b->im) / bmodsqr,
			      (a->im * b->re - a->re * b->im) / bmodsqr);
	}
}

/* ------------------------------------------------------------------------- */

void
complex_sqrt (complex_t *dst, complex_t const *src)
{
	if (complex_real_p (src)) {
		if (src->re >= 0)
			complex_init (dst, gnm_sqrt (src->re), 0);
		else
			complex_init (dst, 0, gnm_sqrt (-src->re));
	} else
		complex_from_polar (dst,
				    gnm_sqrt (complex_mod (src)),
				    complex_angle (src) / 2);
}

/* ------------------------------------------------------------------------- */

/* Like complex_angle, but divide result by pi.  */
static gnm_float
complex_angle_pi (complex_t const *src)
{
	if (src->im == 0)
		return (src->re >= 0 ? 0 : -1);

	if (src->re == 0)
		return (src->im >= 0 ? 0.5 : -0.5);

	/* We could do quarters too */

	/* Fallback.  */
	return complex_angle (src) / M_PIgnum;
}


void
complex_pow (complex_t *dst, complex_t const *a, complex_t const *b)
{
	if (complex_zero_p (a) && complex_real_p (b)) {
		if (b->re <= 0)
			complex_invalid (dst);
		else
			complex_real (dst, 0);
	} else {
		gnm_float res_r, res_a1, res_a2, res_a2_pi, r, arg;
		complex_t F;

		complex_to_polar (&r, &arg, a);
		res_r = gnm_pow (r, b->re) * gnm_exp (-b->im * arg);
		res_a1 = b->im * gnm_log (r);
		res_a2 = b->re * arg;
		res_a2_pi = b->re * complex_angle_pi (a);

		res_a2_pi = gnm_fmod (res_a2_pi, 2);
		if (res_a2_pi < 0) res_a2_pi += 2;

		/*
		 * Problem: sometimes res_a2 is a nice fraction of pi.
		 * Actually adding it will introduce pointless rounding
		 * errors.
		 */
		if (res_a2_pi == 0.5) {
			res_a2 = 0;
			complex_init (&F, 0, 1);
		} else if (res_a2_pi == 1) {
			res_a2 = 0;
			complex_real (&F, -1);
		} else if (res_a2_pi == 1.5) {
			res_a2 = 0;
			complex_init (&F, 0, -1);
		} else
			complex_real (&F, 1);

		complex_from_polar (dst, res_r, res_a1 + res_a2);
		complex_mul (dst, dst, &F);
	}
}

/* ------------------------------------------------------------------------- */
