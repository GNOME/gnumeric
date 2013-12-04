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

void
complex_gamma (complex_t *dst, complex_t const *src)
{
	if (complex_real_p (src)) {
		complex_init (dst, gnm_gamma (src->re), 0);
	} else {
		complex_t z = *src, f;

		complex_fact (&f, src);
		complex_div (dst, &f, &z);
	}
}

/* ------------------------------------------------------------------------- */

void
complex_fact (complex_t *dst, complex_t const *src)
{
	if (complex_real_p (src)) {
		complex_init (dst, gnm_fact (src->re), 0);
	} else if (src->re < 0) {
		/* Fact(z) = -pi / (sin(pi*z) * Gamma(-z)) */
		complex_t a, b, mz;

		complex_init (&mz, -src->re, -src->im);
		complex_gamma (&a, &mz);

		complex_init (&b,
			      M_PIgnum * gnm_fmod (src->re, 2),
			      M_PIgnum * src->im);
		/* Hmm... sin overflows when b.im is large.  */
		complex_sin (&b, &b);

		complex_mul (&a, &a, &b);

		complex_init (&b, -M_PIgnum, 0);

		complex_div (dst, &b, &a);
	} else {
		static const gnm_float c[] = {
			GNM_const (0.99999999999999709182),
			GNM_const (57.156235665862923517),
			GNM_const (-59.597960355475491248),
			GNM_const (14.136097974741747174),
			GNM_const (-0.49191381609762019978),
			GNM_const (0.33994649984811888699e-4),
			GNM_const (0.46523628927048575665e-4),
			GNM_const (-0.98374475304879564677e-4),
			GNM_const (0.15808870322491248884e-3),
			GNM_const (-0.21026444172410488319e-3),
			GNM_const (0.21743961811521264320e-3),
			GNM_const (-0.16431810653676389022e-3),
			GNM_const (0.84418223983852743293e-4),
			GNM_const (-0.26190838401581408670e-4),
			GNM_const (0.36899182659531622704e-5)
		};
		const gnm_float g = GNM_const(607.0) / 128;
		const gnm_float sqrt2pi =
			GNM_const (2.506628274631000502415765284811045253006986740609938316629923);
		complex_t zph, zpghde, s, f;
		int i;

		complex_init (&zph, src->re + 0.5, src->im);
		complex_init (&zpghde,
			      (src->re + g + 0.5) / M_Egnum,
			      src->im / M_Egnum);
		complex_init (&s, 0, 0);

		for (i = G_N_ELEMENTS(c) - 1; i >= 1; i--) {
			complex_t d, q;
			complex_init (&d, src->re + i, src->im);
			complex_init (&q, c[i], 0);
			complex_div (&q, &q, &d);
			complex_add (&s, &s, &q);
		}
		s.re += c[0];

		complex_init (&f, sqrt2pi * gnm_exp (-g), 0);
		complex_mul (&s, &s, &f);
		complex_pow (&f, &zpghde, &zph);
		complex_mul (&s, &s, &f);

		*dst = s;
	}
}

/* ------------------------------------------------------------------------- */
