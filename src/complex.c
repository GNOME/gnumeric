/*
 * complex.c:  A quick library for complex math.
 *
 * Author:
 *  Morten Welinder <terra@diku.dk>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#define GNUMERIC_COMPLEX_IMPLEMENTATION
#include "complex.h"

#include <stdlib.h>
#include <errno.h>


/* ------------------------------------------------------------------------- */

char *
complex_to_string (const complex_t *src, const char *reformat,
		   const char *imformat, char imunit)
{
	char *re_buffer = NULL;
	char *im_buffer = NULL;
	char *sign = "";
	char *suffix = "";
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

	if (re_buffer) g_free (re_buffer);
	if (im_buffer) g_free (im_buffer);

	return res;
}

/* ------------------------------------------------------------------------- */

static int
is_unit_imaginary (const char *src, gnum_float *im, char *imunit)
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
complex_from_string (complex_t *dst, const char *src, char *imunit)
{
	gnum_float x, y;
	char *end;

	/* Case: "i", "+i", "-i", ...  */
	if (is_unit_imaginary (src, &dst->im, imunit)) {
		dst->re = 0;
		return 0;
	}

	errno = 0;
	x = strtognum (src, &end);
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

	/* Case: "42+i", "+42-i", "-42-i", ...  */
	if (is_unit_imaginary (src, &dst->im, imunit)) {
		dst->re = x;
		return 0;
	}

	y = strtognum (src, &end);
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
