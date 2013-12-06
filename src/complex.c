/*
 * complex.c:  A quick library for complex math.
 *
 * Author:
 *  Morten Welinder <terra@gnome.org>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "complex.h"
#include "gutils.h"

#include <stdlib.h>
#include <errno.h>
#include <mathfunc.h>
#include <sf-gamma.h>

/* ------------------------------------------------------------------------- */

char *
complex_to_string (complex_t const *src, char imunit)
{
	char *re_buffer = NULL;
	char *im_buffer = NULL;
	char const *sign = "";
	char const *suffix = "";
	char *res;
	char suffix_buffer[2];
	const char *fmt = "%.*" GNM_FORMAT_g;
	static int digits = -1;

	if (digits == -1) {
		gnm_float l10 = gnm_log10 (FLT_RADIX);
		digits = (int)gnm_ceil (GNM_MANT_DIG * l10) +
			(l10 == (int)l10 ? 0 : 1);
	}

	if (src->re != 0 || src->im == 0) {
		/* We have a real part.  */
		re_buffer = g_strdup_printf (fmt, digits, src->re);
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
			im_buffer = g_strdup_printf (fmt, digits, src->im);
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

#define EAT_SPACES(src_) do {					\
	while (g_unichar_isspace (g_utf8_get_char (src_)))	\
		src_ = g_utf8_next_char (src_);			\
} while (0)

#define HANDLE_SIGN(src_,sign_) do {				\
	switch (*src_) {					\
	case '+': sign_ = +1; src_++; EAT_SPACES (src_); break;	\
	case '-': sign_ = -1; src_++; EAT_SPACES (src_); break;	\
	default: sign_ = 0; break;				\
	}							\
} while (0)

/**
 * complex_from_string:
 * @dst: return location
 * @src: string to parse
 * @imunit: (out): return location of imaginary unit.
 *
 * Returns: zero on success, -1 otherwise.
 *
 * This function differs from Excel's parsing in at least the following
 * ways:
 * (1) We allow spaces before the imaginary unit used with an impled "1".
 * Therefore we allow "+ i".
 * (2) We do not allow a thousands separator as in "1,000i".
 */
int
complex_from_string (complex_t *dst, char const *src, char *imunit)
{
	gnm_float x, y;
	char *end;
	int sign;

	EAT_SPACES (src);
	HANDLE_SIGN (src, sign);

	/* Case: "i", "+i", "-i", ...  */
	if (*src == 'i' || *src == 'j') {
		x = 1;
	} else {
		x = gnm_strto (src, &end);
		if (src == end || errno == ERANGE)
			return -1;
		src = end;
		EAT_SPACES (src);
	}
	if (sign < 0)
		x = 0 - x;

	/* Case: "42", "+42", "-42", ...  */
	if (*src == 0) {
		complex_real (dst, x);
		*imunit = 'i';
		return 0;
	}

	/* Case: "42i", "+42i", "-42i", "-i", "i", ...  */
	if (*src == 'i' || *src == 'j') {
		*imunit = *src++;
		EAT_SPACES (src);
		if (*src == 0) {
			complex_init (dst, 0, x);
			return 0;
		} else
			return -1;
	}

	HANDLE_SIGN (src, sign);
	if (!sign)
		return -1;

	if (*src == 'i' || *src == 'j') {
		y = 1;
	} else {
		y = gnm_strto (src, &end);
		if (src == end || errno == ERANGE)
			return -1;
		src = end;
		EAT_SPACES (src);
	}
	if (sign < 0)
		y = 0 - y;

	/* Case: "42+12i", "+42-12i", "-42-12i", "-42+i", "+42-i", ...  */
	if (*src == 'i' || *src == 'j') {
		*imunit = *src++;
		EAT_SPACES (src);
		if (*src == 0) {
			complex_init (dst, x, y);
			return 0;
		}
	}

	return -1;
}

/* ------------------------------------------------------------------------- */

int
complex_invalid_p (complex_t const *src)
{
	return !(gnm_finite (src->re) && gnm_finite (src->im));
}

/* ------------------------------------------------------------------------- */
