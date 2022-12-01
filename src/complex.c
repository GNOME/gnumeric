/*
 * complex.c:  A quick library for complex math.
 *
 * Author:
 *  Morten Welinder <terra@gnome.org>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <complex.h>
#include <gutils.h>

#include <stdlib.h>
#include <errno.h>
#include <mathfunc.h>
#include <sf-gamma.h>

/* ------------------------------------------------------------------------- */

char *
gnm_complex_to_string (gnm_complex const *src, char imunit)
{
	GString *res = g_string_new (NULL);
	gboolean have_real;

	have_real = src->re != 0 || src->im == 0;
	if (have_real) {
		// We have an real part.
		go_dtoa (res, "!^" GNM_FORMAT_G, src->re);
	}

	if (src->im != 0) {
		// We have an imaginary part.
		if (src->im == 1) {
			if (have_real)
				g_string_append_c (res, '+');
		} else if (src->im == -1) {
			g_string_append_c (res, '-');
		} else {
			size_t olen = res->len;
			go_dtoa (res, "!^" GNM_FORMAT_G, src->im);
			if (have_real &&
			    res->str[olen] != '-' && res->str[olen] != '+')
				g_string_insert_c (res, olen,
						   (src->im >= 0 ? '+' : '-'));
		}
		g_string_append_c (res, imunit);
	}

	return g_string_free (res, FALSE);
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
 * gnm_complex_from_string:
 * @dst: return location
 * @src: string to parse
 * @imunit: (out): return location of imaginary unit.
 *
 * Returns: zero on success, -1 otherwise.
 *
 * This function differs from Excel's parsing in at least the following
 * ways:
 * (1) We allow spaces before the imaginary unit used with an implied "1".
 * Therefore we allow "+ i".
 * (2) We do not allow a thousands separator as in "1,000i".
 */
int
gnm_complex_from_string (gnm_complex *dst, char const *src, char *imunit)
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
		*dst = GNM_CREAL (x);
		*imunit = 'i';
		return 0;
	}

	/* Case: "42i", "+42i", "-42i", "-i", "i", ...  */
	if (*src == 'i' || *src == 'j') {
		*imunit = *src++;
		EAT_SPACES (src);
		if (*src == 0) {
			*dst = GNM_CMAKE (0, x);
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
			*dst = GNM_CMAKE (x, y);
			return 0;
		}
	}

	return -1;
}

/* ------------------------------------------------------------------------- */

int
gnm_complex_invalid_p (gnm_complex const *src)
{
	return !(gnm_finite (src->re) && gnm_finite (src->im));
}

/* ------------------------------------------------------------------------- */
