/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-complex.c:  Built in complex number functions and functions registration
 *
 * Authors:
 *   Michael Meeks <michael@ximian.com>
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <string.h>
#include <stdlib.h>
#include <func.h>

#include <complex.h>
#include <sf-gamma.h>
#include <parse-util.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <mathfunc.h>
#include <gnm-i18n.h>

#include <goffice/goffice.h>
#include <gnm-plugin.h>
#include "gsl-complex.h"


GNM_PLUGIN_MODULE_HEADER;


/* Converts a complex number string into its coefficients.  Returns 0 if ok,
 * 1 if an error occurred.
 */
static int
value_get_as_complex (GnmValue const *val, complex_t *res, char *imunit)
{
	if (VALUE_IS_NUMBER (val)) {
		complex_real (res, value_get_as_float (val));
		*imunit = 'i';
		return 0;
	} else {
		return complex_from_string (res,
					    value_peek_string (val),
					    imunit);
	}
}

static GnmValue *
value_new_complex (complex_t const *c, char imunit)
{
	if (complex_invalid_p (c))
		return value_new_error_NUM (NULL);
	else if (complex_real_p (c))
		return value_new_float (c->re);
	else
		return value_new_string_nocopy (complex_to_string (c, imunit));
}

/***************************************************************************/

static GnmFuncHelp const help_complex[] = {
	{ GNM_FUNC_HELP_NAME, F_("COMPLEX:a complex number of the form @{x} + @{y}@{i}") },
	{ GNM_FUNC_HELP_ARG, F_("x:real part") },
	{ GNM_FUNC_HELP_ARG, F_("y:imaginary part") },
	{ GNM_FUNC_HELP_ARG, F_("i:the suffix for the complex number, either \"i\" or \"j\"; defaults to \"i\"") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{i} is neither \"i\" nor \"j\", COMPLEX returns #VALUE!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=COMPLEX(1,-1)" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_complex (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c;
	char const *suffix;

	complex_init (&c,
		      value_get_as_float (argv[0]),
		      value_get_as_float (argv[1]));
	suffix = argv[2] ? value_peek_string (argv[2]) : "i";

	if (strcmp (suffix, "i") != 0 && strcmp (suffix, "j") != 0)
		return value_new_error_VALUE (ei->pos);

	return value_new_complex (&c, *suffix);
}

/***************************************************************************/

static GnmFuncHelp const help_imaginary[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMAGINARY:the imaginary part of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMAGINARY(\"132-j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMREAL" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imaginary (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c;
	char imunit;

	if (VALUE_IS_NUMBER (argv[0]))
	        return value_new_float (0.0);

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	return value_new_float (c.im);
}

/***************************************************************************/

static GnmFuncHelp const help_imabs[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMABS:the absolute value of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMABS(\"2-j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMAGINARY,IMREAL" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imabs (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	return value_new_float (complex_mod (&c));
}

/***************************************************************************/

static GnmFuncHelp const help_imreal[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMREAL:the real part of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMREAL(\"132-j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMAGINARY" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imreal (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c;
	char imunit;

	if (VALUE_IS_NUMBER (argv[0]))
		return value_dup (argv[0]);

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	return value_new_float (c.re);
}

/***************************************************************************/

static GnmFuncHelp const help_imconjugate[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMCONJUGATE:the complex conjugate of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMCONJUGATE(\"1-j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMAGINARY,IMREAL" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imconjugate (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_conj (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_iminv[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMINV:the reciprocal, or inverse, of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMINV(\"1-j\")" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_iminv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_inverse (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imneg[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMNEG:the negative of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMNEG(\"1-j\")" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imneg (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_negative (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcos[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMCOS:the cosine of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMCOS(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMSIN,IMTAN" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imcos (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_cos (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imtan[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMTAN:the tangent of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMTAN(\"2-j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMSIN,IMCOS" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imtan (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_tan (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsec[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMSEC:the secant of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("IMSEC(@{z}) = 1/IMCOS(@{z}).") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMSEC(\"2-j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMCSC,IMCOT" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imsec (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_cos (&res, &c);
	gsl_complex_inverse (&res, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcsc[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMCSC:the cosecant of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("IMCSC(@{z}) = 1/IMSIN(@{z}).") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMCSC(\"2-j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMSEC,IMCOT" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imcsc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_sin (&res, &c);
	gsl_complex_inverse (&res, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcot[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMCOT:the cotangent of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("IMCOT(@{z}) = IMCOS(@{z})/IMSIN(@{z}).") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMCOT(\"2-i\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMCOT(\"2+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMSEC,IMCSC" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imcot (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_tan (&res, &c);
	gsl_complex_inverse (&res, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imexp[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMEXP:the exponential of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMEXP(\"2-i\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMEXP(\"2+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMLN" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imexp (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_exp (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imargument[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARGUMENT:the argument theta of the complex number @{z} ") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The argument theta of a complex number is its angle in radians from the real axis.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARGUMENT(\"2-j\")" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imargument (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	return value_new_float (complex_angle (&c));
}

/***************************************************************************/

static GnmFuncHelp const help_imln[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMLN:the natural logarithm of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The result will have an imaginary part between -\xcf\x80 and +\xcf\x80.\n"
					"The natural logarithm is not uniquely defined on complex numbers. "
					"You may need to add or subtract an even multiple of \xcf\x80 to the imaginary part.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMLN(\"3-j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMEXP,IMLOG2,IMLOG10" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imln (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_ln (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imlog2[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMLOG2:the base-2 logarithm of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMLOG2(\"3-j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMLN,IMLOG10" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imlog2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_ln (&res, &c);
	complex_scale_real (&res, 1 / M_LN2gnum);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imlog10[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMLOG10:the base-10 logarithm of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMLOG10(\"3-j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMLN,IMLOG2" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imlog10 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_ln (&res, &c);
	complex_scale_real (&res, 1 / M_LN10gnum);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_impower[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMPOWER:the complex number @{z1} raised to the @{z2}th power") },
	{ GNM_FUNC_HELP_ARG, F_("z1:a complex number") },
	{ GNM_FUNC_HELP_ARG, F_("z2:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z1} or @{z2} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMPOWER(\"4-j\",2)" },
	{ GNM_FUNC_HELP_SEEALSO, "IMSQRT" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_impower (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return value_new_error_NUM (ei->pos);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return value_new_error_NUM (ei->pos);

	if (complex_zero_p (&a) && complex_zero_p (&b))
		return value_new_error_DIV0 (ei->pos);

	complex_pow (&res, &a, &b);
	if (complex_invalid_p (&res))
		return value_new_error_NUM (ei->pos);

	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imdiv[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMDIV:the quotient of two complex numbers @{z1}/@{z2}") },
	{ GNM_FUNC_HELP_ARG, F_("z1:a complex number") },
	{ GNM_FUNC_HELP_ARG, F_("z2:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z1} or @{z2} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMDIV(\"2-j\",\"2+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMPRODUCT" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imdiv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return value_new_error_NUM (ei->pos);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return value_new_error_NUM (ei->pos);

	if (complex_zero_p (&b))
		return value_new_error_DIV0 (ei->pos);

	complex_div (&res, &a, &b);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsin[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMSIN:the sine of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMSIN(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMCOS,IMTAN" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imsin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_sin (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsinh[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMSINH:the hyperbolic sine of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMSINH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMCOSH,IMTANH" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imsinh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_sinh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcosh[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMCOSH:the hyperbolic cosine of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMCOSH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMSINH,IMTANH" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imcosh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_cosh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imtanh[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMTANH:the hyperbolic tangent of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMTANH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMSINH,IMCOSH" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imtanh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_tanh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsech[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMSECH:the hyperbolic secant of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMSECH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMCSCH,IMCOTH" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imsech (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_sech (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcsch[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMCSCH:the hyperbolic cosecant of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMCSCH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMSECH,IMCOTH" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imcsch (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_csch (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcoth[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMCOTH:the hyperbolic cotangent of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMCOTH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMSECH,IMCSCH" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imcoth (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_coth (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarcsin[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCSIN:the complex arcsine of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("IMARCSIN returns the complex arcsine of the complex number "
	   "@{z}. The branch cuts are on the real axis, less than -1 and greater than 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCSIN(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCCOS,IMARCTAN" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imarcsin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arcsin (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccos[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCCOS:the complex arccosine of the complex number ") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("IMARCCOS returns the complex arccosine of the complex number "
	   "@{z}. The branch cuts are on the real axis, less than -1 and greater than 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCCOS(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCSIN,IMARCTAN" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imarccos (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arccos (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarctan[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCTAN:the complex arctangent of the complex number ") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("IMARCTAN returns the complex arctangent of the complex number "
	   "@{z}. The branch cuts are on the imaginary axis, below -i and above i.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCTAN(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCSIN,IMARCCOS" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imarctan (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arctan (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarcsec[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCSEC:the complex arcsecant of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCSEC(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCCSC,IMARCCOT" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imarcsec (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arcsec (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccsc[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCCSC:the complex arccosecant of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCCSC(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCSEC,IMARCCOT" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imarccsc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arccsc (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccot[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCCOT:the complex arccotangent of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCCOT(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCSEC,IMARCCSC" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imarccot (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arccot (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarcsinh[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCSINH:the complex hyperbolic arcsine of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("IMARCSINH returns the complex hyperbolic arcsine of the complex number @{z}. "
					" The branch cuts are on the imaginary axis, below -i and above i.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCSINH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCCOSH,IMARCTANH" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imarcsinh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arcsinh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccosh[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCCOSH:the complex hyperbolic arccosine of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("IMARCCOSH returns the complex hyperbolic arccosine of the "
					"complex number @{z}. The branch cut is on the real "
					"axis, less than 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCCOSH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCSINH,IMARCTANH" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imarccosh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arccosh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarctanh[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCTANH:the complex hyperbolic arctangent of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("IMARCTANH returns the complex hyperbolic arctangent of the "
					"complex number @{z}. The branch cuts are on the "
					"real axis, less than -1 and greater than 1.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCTANH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCSINH,IMARCCOSH" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imarctanh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arctanh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarcsech[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCSECH:the complex hyperbolic arcsecant of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCSECH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCCSCH,IMARCCOTH" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_imarcsech (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arcsech (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccsch[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCCSCH:the complex hyperbolic arccosecant of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCCSCH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCSECH,IMARCCOTH" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imarccsch (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arccsch (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccoth[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMARCCOTH:the complex hyperbolic arccotangent of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMARCCOTH(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMARCSECH,IMARCCSCH" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imarccoth (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	gsl_complex_arccoth (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsqrt[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMSQRT:the square root of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMSQRT(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMPOWER" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imsqrt (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_sqrt (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imfact[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMFACT:the factorial of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMFACT(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMGAMMA" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imfact (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_fact (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imgamma[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMGAMMA:the gamma function of the complex number @{z}") },
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMGAMMA(\"1+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMGAMMA" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imgamma (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_gamma (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imigamma[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMIGAMMA:the incomplete Gamma function")},
	{ GNM_FUNC_HELP_ARG, F_("a:a complex number")},
	{ GNM_FUNC_HELP_ARG, F_("z:a complex number")},
	{ GNM_FUNC_HELP_ARG, F_("lower:if true (the default), the lower incomplete gamma function, otherwise the upper incomplete gamma function")},
	{ GNM_FUNC_HELP_ARG, F_("regularize:if true (the default), the regularized version of the incomplete gamma function")},
	{ GNM_FUNC_HELP_NOTE, F_("The regularized incomplete gamma function is the unregularized incomplete gamma function divided by gamma(@{a})") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMIGAMMA(2.5,-1.8,TRUE,TRUE)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMIGAMMA(2.5,-1.8,TRUE,TRUE)" },
	{ GNM_FUNC_HELP_SEEALSO, "GAMMA,IMIGAMMA"},
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imigamma (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t res, a, z;
	char imunit;
	gboolean lower = argv[2] ? value_get_as_checked_bool (argv[2]) : TRUE;
	gboolean reg = argv[3] ? value_get_as_checked_bool (argv[3]) : TRUE;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return value_new_error_NUM (ei->pos);
	if (value_get_as_complex (argv[1], &z, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_igamma (&res, &a, &z, lower, reg);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsub[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMSUB:the difference of two complex numbers") },
	{ GNM_FUNC_HELP_ARG, F_("z1:a complex number") },
	{ GNM_FUNC_HELP_ARG, F_("z2:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{z1} or @{z2} is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMSUB(\"3-j\",\"2+j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMSUM" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imsub (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return value_new_error_NUM (ei->pos);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return value_new_error_NUM (ei->pos);

	complex_sub (&res, &a, &b);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_improduct[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMPRODUCT:the product of the given complex numbers") },
	{ GNM_FUNC_HELP_ARG, F_("z1:a complex number") },
	{ GNM_FUNC_HELP_ARG, F_("z2:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If any of @{z1}, @{z2},... is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMPRODUCT(\"2-j\",\"4-2j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMDIV" },
	{ GNM_FUNC_HELP_END}
};


typedef enum {
	Improduct, Imsum
} eng_imoper_type_t;

typedef struct {
	complex_t         res;
	char              imunit;
	eng_imoper_type_t type;
} eng_imoper_t;

static GnmValue *
callback_function_imoper (GnmEvalPos const *ep, GnmValue const *value, void *closure)
{
	eng_imoper_t *result = closure;
	complex_t c;
	char *imptr, dummy;

	imptr = VALUE_IS_NUMBER (value) ? &dummy : &result->imunit;
	if (value_get_as_complex (value, &c, imptr))
		return value_new_error_NUM (ep);

	switch (result->type) {
	case Improduct:
		complex_mul (&result->res, &result->res, &c);
	        break;
	case Imsum:
		complex_add (&result->res, &result->res, &c);
	        break;
	default:
		abort ();
	}

	return NULL;
}

static GnmValue *
gnumeric_improduct (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmValue *v;
	eng_imoper_t p;

	p.type = Improduct;
	p.imunit = 'j';
	complex_real (&p.res, 1);

	v = function_iterate_argument_values
		(ei->pos, callback_function_imoper, &p,
		 argc, argv, TRUE, CELL_ITER_IGNORE_BLANK);

	if (v != NULL)
		return v;

	return value_new_complex (&p.res, p.imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsum[] = {
	{ GNM_FUNC_HELP_NAME, F_("IMSUM:the sum of the given complex numbers") },
	{ GNM_FUNC_HELP_ARG, F_("z1:a complex number") },
	{ GNM_FUNC_HELP_ARG, F_("z2:a complex number") },
	{ GNM_FUNC_HELP_NOTE, F_("If any of @{z1}, @{z2},... is not a valid complex number, #VALUE! is returned.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=IMSUM(\"2-4j\",\"9-j\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IMSUB" },
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_imsum (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmValue *v;
	eng_imoper_t p;

	p.type = Imsum;
	p.imunit = 'j';
	complex_real (&p.res, 0);

	v = function_iterate_argument_values
		(ei->pos, callback_function_imoper, &p,
		 argc, argv, TRUE, CELL_ITER_IGNORE_BLANK);

	if (v != NULL)
		return v;

	return value_new_complex (&p.res, p.imunit);
}

/***************************************************************************/

GnmFuncDescriptor const complex_functions[] = {
	{ "complex",     "ff|s",  help_complex,
	  gnumeric_complex, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imabs",       "S",     help_imabs,
	  gnumeric_imabs, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imaginary",   "S",     help_imaginary,
	  gnumeric_imaginary, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imargument",  "S",     help_imargument,
	  gnumeric_imargument, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imconjugate", "S",     help_imconjugate,
	  gnumeric_imconjugate, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imcos",       "S",     help_imcos,
	  gnumeric_imcos, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imdiv",       "SS",    help_imdiv,
	  gnumeric_imdiv, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imexp",       "S",     help_imexp,
	  gnumeric_imexp, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imln",        "S",     help_imln,
	  gnumeric_imln, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imlog10",     "S",     help_imlog10,
	  gnumeric_imlog10, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imlog2",      "S",     help_imlog2,
	  gnumeric_imlog2, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "impower",     "SS",    help_impower,
	  gnumeric_impower, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imreal",      "S",     help_imreal,
	  gnumeric_imreal, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imsin",       "S",     help_imsin,
	  gnumeric_imsin, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imsqrt",      "S",     help_imsqrt,
	  gnumeric_imsqrt, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imsub",       "SS",    help_imsub,
	  gnumeric_imsub, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imsum",       NULL,    help_imsum,
	  NULL, gnumeric_imsum, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "iminv",   "S",     help_iminv,
	  gnumeric_iminv, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "imneg",   "S",     help_imneg,
	  gnumeric_imneg, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imtan",       "S",     help_imtan,
	  gnumeric_imtan, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "improduct",   NULL,    help_improduct,
	  NULL, gnumeric_improduct, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imsec",       "S",     help_imsec,
	  gnumeric_imsec, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imcsc",       "S",     help_imcsc,
	  gnumeric_imcsc, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imcot",       "S",     help_imcot,
	  gnumeric_imcot, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imsinh",       "S",    help_imsinh,
	  gnumeric_imsinh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imcosh",       "S",    help_imcosh,
	  gnumeric_imcosh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imtanh",       "S",    help_imtanh,
	  gnumeric_imtanh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imsech",       "S",    help_imsech,
	  gnumeric_imsech, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imcsch",       "S",    help_imcsch,
	  gnumeric_imcsch, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imcoth",       "S",    help_imcoth,
	  gnumeric_imcoth, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarcsin",     "S",    help_imarcsin,
	  gnumeric_imarcsin, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarccos",     "S",    help_imarccos,
	  gnumeric_imarccos, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarctan",     "S",    help_imarctan,
	  gnumeric_imarctan, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarcsec",     "S",    help_imarcsec,
	  gnumeric_imarcsec, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarccsc",     "S",    help_imarccsc,
	  gnumeric_imarccsc, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarccot",     "S",    help_imarccot,
	  gnumeric_imarccot, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarcsinh",    "S",    help_imarcsinh,
	  gnumeric_imarcsinh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarccosh",    "S",    help_imarccosh,
	  gnumeric_imarccosh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarctanh",    "S",    help_imarctanh,
	  gnumeric_imarctanh, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarcsech",    "S",    help_imarcsech,
	  gnumeric_imarcsech, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarccsch",    "S",    help_imarccsch,
	  gnumeric_imarccsch, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "imarccoth",    "S",    help_imarccoth,
	  gnumeric_imarccoth, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

	{ "imfact",       "S",    help_imfact,
	  gnumeric_imfact, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "imgamma",      "S",    help_imgamma,
	  gnumeric_imgamma, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "imigamma",     "SS|bb",  help_imigamma,
	  gnumeric_imigamma, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

	{NULL}
};
