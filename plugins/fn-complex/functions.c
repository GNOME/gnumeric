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
#include <parse-util.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <mathfunc.h>
#include <gnm-i18n.h>

#include <goffice/app/go-plugin.h>
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
	if (complex_real_p (c))
		return value_new_float (c->re);
	else {
		char f[5 + 4 * sizeof (int) + sizeof (GNM_FORMAT_g)];
		sprintf (f, "%%.%d" GNM_FORMAT_g, GNM_DIG);
		return value_new_string_nocopy (complex_to_string (c, f, f, imunit));
	}
}

/***************************************************************************/

static GnmFuncHelp const help_complex[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=COMPLEX\n"
	   "@SYNTAX=COMPLEX(real,im[,suffix])\n"

	   "@DESCRIPTION="
	   "COMPLEX returns a complex number of the form x + yi.\n\n"
	   "@real is the real and @im is the imaginary part of "
	   "the complex number.  @suffix is the suffix for the imaginary "
	   "part.  If it is omitted, COMPLEX uses 'i' by default.\n"
	   "\n"
	   "* If @suffix is neither 'i' nor 'j', COMPLEX returns #VALUE! "
	   "error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "COMPLEX(1,-1) equals 1-i.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMAGINARY\n"
	   "@SYNTAX=IMAGINARY(inumber)\n"
	   "@DESCRIPTION="
	   "IMAGINARY returns the imaginary part of a complex "
	   "number.\n\n"
	   "* If @inumber is not a valid complex number, IMAGINARY returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMAGINARY(\"132-j\") equals -1.\n"
	   "\n"
	   "@SEEALSO=IMREAL")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imaginary (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c;
	char imunit;

	if (VALUE_IS_NUMBER (argv[0]))
	        return value_new_float (0.0);

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	return value_new_float (c.im);
}

/***************************************************************************/

static GnmFuncHelp const help_imabs[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMABS\n"
	   "@SYNTAX=IMABS(inumber)\n"
	   "@DESCRIPTION="
	   "IMABS returns the absolute value of a complex number.\n\n"
	   "* If @inumber is not a valid complex number, IMABS returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMABS(\"2-j\") equals 2.23606798.\n"
	   "\n"
	   "@SEEALSO=IMAGINARY,IMREAL")
	},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_imabs (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	return value_new_float (complex_mod (&c));
}

/***************************************************************************/

static GnmFuncHelp const help_imreal[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMREAL\n"
	   "@SYNTAX=IMREAL(inumber)\n"
	   "@DESCRIPTION="
	   "IMREAL returns the real part of a complex number.\n\n"
	   "* If @inumber is not a valid complex number, IMREAL returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "imreal(\"132-j\") equals 132.\n"
	   "\n"
	   "@SEEALSO=IMAGINARY")
	},
	{ GNM_FUNC_HELP_END }
};


static GnmValue *
gnumeric_imreal (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c;
	char imunit;

	if (VALUE_IS_NUMBER (argv[0]))
		return value_dup (argv[0]);

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	return value_new_float (c.re);
}

/***************************************************************************/

static GnmFuncHelp const help_imconjugate[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMCONJUGATE\n"
	   "@SYNTAX=IMCONJUGATE(inumber)\n"
	   "@DESCRIPTION="
	   "IMCONJUGATE returns the complex conjugate of a complex number.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMCONJUGATE returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCONJUGATE(\"1-j\") equals 1+j.\n"
	   "\n"
	   "@SEEALSO=IMAGINARY,IMREAL")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imconjugate (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_conj (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_iminv[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMINV\n"
	   "@SYNTAX=IMINV(inumber)\n"
	   "@DESCRIPTION="
	   "IMINV returns the inverse, or reciprocal, of the complex "
	   "number z (@inumber), where\n\n\t1/z = (x - i y)/(x^2 + y^2).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMINV returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMINV(\"1-j\") equals 0.5+0.5j.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_iminv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_inverse (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imneg[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMNEG\n"
	   "@SYNTAX=IMNEG(inumber)\n"
	   "@DESCRIPTION="
	   "IMNEG returns the negative of the complex number z (@inumber), "
	   "where\n\n\t-z = (-x) + i(-y).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMNEG returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMNEG(\"1-j\") equals -1+j.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imneg (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_negative (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcos[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMCOS\n"
	   "@SYNTAX=IMCOS(inumber)\n"
	   "@DESCRIPTION="
	   "IMCOS returns the cosine of a complex number.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMCOS returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCOS(\"1+j\") equals 0.833730-0.988898j.\n"
	   "\n"
	   "@SEEALSO=IMSIN,IMTAN")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imcos (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_cos (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imtan[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMTAN\n"
	   "@SYNTAX=IMTAN(inumber)\n"
	   "@DESCRIPTION="
	   "IMTAN returns the tangent of a complex number.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMTAN returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMTAN(\"2-j\") equals -0.2434582-1.1667363j.\n"
	   "\n"
	   "@SEEALSO=IMSIN,IMCOS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imtan (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_tan (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsec[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMSEC\n"
	   "@SYNTAX=IMSEC(inumber)\n"
	   "@DESCRIPTION="
	   "IMSEC returns the complex secant of the complex number z "
	   "(@inumber), where\n\n\t"
	   "sec(z) = 1/cos(z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMSEC returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSEC(\"2-j\") equals -0.413149-0.687527j.\n"
	   "\n"
	   "@SEEALSO=IMCSC,IMCOT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imsec (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_cos (&res, &c);
	gsl_complex_inverse (&res, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcsc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMCSC\n"
	   "@SYNTAX=IMCSC(inumber)\n"
	   "@DESCRIPTION="
	   "IMCSC returns the complex cosecant of the complex number z "
	   "(@inumber), where\n\n\t"
	   "csc(z) = 1/sin(z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMCSC returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCSC(\"2-j\") equals 0.635494-0.221501j.\n"
	   "\n"
	   "@SEEALSO=IMSEC,IMCOT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imcsc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_sin (&res, &c);
	gsl_complex_inverse (&res, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcot[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMCOT\n"
	   "@SYNTAX=IMCOT(inumber)\n"
	   "@DESCRIPTION="
	   "IMCOT returns the complex cotangent of the complex number z "
	   "(@inumber), where\n\n\t"
	   "cot(z) = 1/tan(z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMCOT returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCOT(\"2-j\") equals -0.171384+0.821330j.\n"
	   "\n"
	   "@SEEALSO=IMSEC,IMCSC")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imcot (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_tan (&res, &c);
	gsl_complex_inverse (&res, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imexp[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMEXP\n"
	   "@SYNTAX=IMEXP(inumber)\n"
	   "@DESCRIPTION="
	   "IMEXP returns the exponential of a complex number.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMEXP returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMEXP(\"2-j\") equals 3.992324-6.217676j.\n"
	   "\n"
	   "@SEEALSO=IMLN")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imexp (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_exp (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imargument[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARGUMENT\n"
	   "@SYNTAX=IMARGUMENT(inumber)\n"
	   "@DESCRIPTION="
	   "IMARGUMENT returns the argument theta of a complex number, "
	   "i.e. the angle in radians from the real axis to the "
	   "representation of the number in polar coordinates.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARGUMENT returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARGUMENT(\"2-j\") equals -0.463647609.\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imargument (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	return value_new_float (complex_angle (&c));
}

/***************************************************************************/

static GnmFuncHelp const help_imln[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMLN\n"
	   "@SYNTAX=IMLN(inumber)\n"
	   "@DESCRIPTION="
	   "IMLN returns the natural logarithm of a complex number.\n\n"
	   "The "
	   "result will have an imaginary part between -pi and +pi.  The "
	   "natural logarithm is not uniquely defined on complex numbers. "
	   "You may need to add or subtract an even multiple of pi to the "
	   "imaginary part.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMLN returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMLN(\"3-j\") equals 1.15129-0.32175j.\n"
	   "\n"
	   "@SEEALSO=IMEXP,IMLOG2,IMLOG10")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imln (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_ln (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imlog2[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMLOG2\n"
	   "@SYNTAX=IMLOG2(inumber)\n"
	   "@DESCRIPTION="
	   "IMLOG2 returns the logarithm of a complex number in base 2.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMLOG2 returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMLOG2(\"3-j\") equals 1.66096-0.46419j.\n"
	   "\n"
	   "@SEEALSO=IMLN,IMLOG10")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imlog2 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_ln (&res, &c);
	complex_scale_real (&res, 1 / M_LN2gnum);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imlog10[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMLOG10\n"
	   "@SYNTAX=IMLOG10(inumber)\n"
	   "@DESCRIPTION="
	   "IMLOG10 returns the logarithm of a complex number in base 10.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMLOG10 returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMLOG10(\"3-j\") equals 0.5-0.13973j.\n"
	   "\n"
	   "@SEEALSO=IMLN,IMLOG2")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imlog10 (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_ln (&res, &c);
	complex_scale_real (&res, 1 / M_LN10gnum);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_impower[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMPOWER\n"
	   "@SYNTAX=IMPOWER(inumber1,inumber2)\n"
	   "@DESCRIPTION="
	   "IMPOWER returns a complex number raised to a power.  @inumber1 is "
	   "the complex number to be raised to a power and @inumber2 is the "
	   "power to which you want to raise it.\n"
	   "\n"
	   "* If @inumber1 or @inumber2 are not valid complex numbers, "
	   "IMPOWER returns #VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMPOWER(\"4-j\",2) equals 15-8j.\n"
	   "\n"
	   "@SEEALSO=IMSQRT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_impower (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return value_new_error_VALUE (ei->pos);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return value_new_error_VALUE (ei->pos);

	if (complex_real_p (&a) && a.re <= 0 && !complex_real_p (&b))
		return value_new_error_DIV0 (ei->pos);

	complex_pow (&res, &a, &b);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imdiv[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMDIV\n"
	   "@SYNTAX=IMDIV(inumber1,inumber2)\n"
	   "@DESCRIPTION="
	   "IMDIV returns the quotient of two complex numbers.\n"
	   "\n"
	   "* If @inumber1 or @inumber2 are not valid complex numbers, "
	   "IMDIV returns #VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMDIV(\"2-j\",\"2+j\") equals 0.6-0.8j.\n"
	   "\n"
	   "@SEEALSO=IMPRODUCT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imdiv (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return value_new_error_VALUE (ei->pos);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return value_new_error_VALUE (ei->pos);

	if (complex_zero_p (&b))
		return value_new_error_DIV0 (ei->pos);

	complex_div (&res, &a, &b);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsin[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMSIN\n"
	   "@SYNTAX=IMSIN(inumber)\n"
	   "@DESCRIPTION="
	   "IMSIN returns the sine of a complex number.\n\n"
	   "* If @inumber is not a valid complex number, IMSIN returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSIN(\"1+j\") equals 1.29846+0.63496j.\n"
	   "\n"
	   "@SEEALSO=IMCOS,IMTAN")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imsin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_sin (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsinh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMSINH\n"
	   "@SYNTAX=IMSINH(inumber)\n"
	   "@DESCRIPTION="
	   "IMSINH returns the complex hyperbolic sine of the complex "
	   "number z (@inumber), where\n\n"
	   "\tsinh(z) = (exp(z) - exp(-z))/2.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMSINH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSINH(\"1+j\") equals 0.63496+1.29846j.\n"
	   "\n"
	   "@SEEALSO=IMCOSH,IMTANH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imsinh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_sinh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcosh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMCOSH\n"
	   "@SYNTAX=IMCOSH(inumber)\n"
	   "@DESCRIPTION="
	   "IMCOSH returns the complex hyperbolic cosine of the complex "
	   "number z (@inumber), where\n\n\tcosh(z) = (exp(z) + exp(-z))/2.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMCOSH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCOSH(\"1+j\") equals 0.83373+0.988898j.\n"
	   "\n"
	   "@SEEALSO=IMSINH,IMTANH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imcosh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_cosh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imtanh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMTANH\n"
	   "@SYNTAX=IMTANH(inumber)\n"
	   "@DESCRIPTION="
	   "IMTANH returns the complex hyperbolic tangent of the complex "
	   "number z (@inumber), where\n\n\ttanh(z) = sinh(z)/cosh(z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMTANH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMTANH(\"1+j\") equals 1.083923+0.2717526j.\n"
	   "\n"
	   "@SEEALSO=IMSINH,IMCOSH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imtanh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_tanh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsech[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMSECH\n"
	   "@SYNTAX=IMSECH(inumber)\n"
	   "@DESCRIPTION="
	   "IMSECH returns the complex hyperbolic secant of the complex "
	   "number z (@inumber), where\n\n\tsech(z) = 1/cosh(z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMSECH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSECH(\"1+j\") equals 0.498337-0.5910838j.\n"
	   "\n"
	   "@SEEALSO=IMCSCH,IMCOTH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imsech (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_sech (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcsch[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMCSCH\n"
	   "@SYNTAX=IMCSCH(inumber)\n"
	   "@DESCRIPTION="
	   "IMCSCH returns the complex hyperbolic cosecant of the "
	   "complex number z (@inumber), where\n\n\tcsch(z) = 1/sinh(z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMCSCH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCSCH(\"1+j\") equals 0.303931-0.621518j.\n"
	   "\n"
	   "@SEEALSO=IMSECH,IMCOTH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imcsch (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_csch (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imcoth[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMCOTH\n"
	   "@SYNTAX=IMCOTH(inumber)\n"
	   "@DESCRIPTION="
	   "IMCOTH returns the complex hyperbolic cotangent of the complex "
	   "number z (@inumber) where,\n\n\tcoth(z) = 1/tanh(z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMCOTH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCOTH(\"1+j\") equals 0.868014-0.217622j.\n"
	   "\n"
	   "@SEEALSO=IMSECH,IMCSCH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imcoth (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_coth (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarcsin[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCSIN\n"
	   "@SYNTAX=IMARCSIN(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCSIN returns the complex arcsine of the complex number "
	   "@inumber. The branch cuts are on the real axis, less than -1 "
	   "and greater than 1.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCSIN returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCSIN(\"1+j\") equals 0.6662394+1.061275j.\n"
	   "\n"
	   "@SEEALSO=IMARCCOS,IMARCTAN")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarcsin (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arcsin (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccos[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCCOS\n"
	   "@SYNTAX=IMARCCOS(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCCOS returns the complex arccosine of the complex number "
	   "@inumber. The branch cuts are on the real axis, less than -1 "
	   "and greater than 1.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCCOS returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCOS(\"1+j\") equals 0.9045569-1.061275j.\n"
	   "\n"
	   "@SEEALSO=IMARCSIN,IMARCTAN")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarccos (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arccos (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarctan[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCTAN\n"
	   "@SYNTAX=IMARCTAN(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCTAN returns the complex arctangent of the complex number "
	   "@inumber. The branch cuts are on the imaginary axis, "
	   "below -i and above i.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCTAN returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCTAN(\"1+j\") equals 1.0172220+0.4023595j.\n"
	   "\n"
	   "@SEEALSO=IMARCSIN,IMARCCOS")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarctan (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arctan (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarcsec[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCSEC\n"
	   "@SYNTAX=IMARCSEC(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCSEC returns the complex arcsecant of the complex number "
	   "z (@inumber), where\n\n\tarcsec(z) = arccos(1/z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCSEC returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCSEC(\"1+j\") equals 1.1185179+0.5306375j.\n"
	   "\n"
	   "@SEEALSO=IMARCCSC,IMARCCOT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarcsec (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arcsec (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccsc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCCSC\n"
	   "@SYNTAX=IMARCCSC(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCCSC returns the complex arccosecant of the complex "
	   "number z (@inumber), where\n\n\tarccsc(z) = arcsin(1/z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCCSC returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCSC(\"1+j\") equals 0.45227845-0.5306375j.\n"
	   "\n"
	   "@SEEALSO=IMARCSEC,IMARCCOT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarccsc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arccsc (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccot[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCCOT\n"
	   "@SYNTAX=IMARCCOT(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCCOT returns the complex arccotangent of the complex "
	   "number z (@inumber), where\n\n\tarccot(z) = arctan(1/z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCCOT returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCOT(\"1+j\") equals 0.553574+0.4023595j.\n"
	   "\n"
	   "@SEEALSO=IMARCSEC,IMARCCSC")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarccot (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arccot (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarcsinh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCSINH\n"
	   "@SYNTAX=IMARCSINH(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCSINH returns the complex hyperbolic arcsine of the "
	   "complex number @inumber. The branch cuts are on the "
	   "imaginary axis, below -i and above i.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCSINH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCSINH(\"1+j\") equals 1.061275+0.6662394j.\n"
	   "\n"
	   "@SEEALSO=IMARCCOSH,IMARCTANH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarcsinh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arcsinh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccosh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCCOSH\n"
	   "@SYNTAX=IMARCCOSH(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCCOSH returns the complex hyperbolic arccosine of the "
	   "complex number @inumber. The branch cut is on the real "
	   "axis, less than 1.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCCOSH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCOSH(\"1+j\") equals 1.06127506+0.904557j.\n"
	   "\n"
	   "@SEEALSO=IMARCSINH,IMARCTANH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarccosh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arccosh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarctanh[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCTANH\n"
	   "@SYNTAX=IMARCTANH(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCTANH returns the complex hyperbolic arctangent of the "
	   "complex number @inumber. The branch cuts are on the "
	   "real axis, less than -1 and greater than 1.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCTANH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCTANH(\"1+j\") equals 0.4023595+1.0172220j.\n"
	   "\n"
	   "@SEEALSO=IMARCSINH,IMARCCOSH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarctanh (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arctanh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarcsech[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCSECH\n"
	   "@SYNTAX=IMARCSECH(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCSECH returns the complex hyperbolic arcsecant of the "
	   "complex number z (@inumber), where\n\n\t"
	   "arcsech(z) = arccosh(1/z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCSECH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCSECH(\"1+j\") equals 0.5306375-1.118518j.\n"
	   "\n"
	   "@SEEALSO=IMARCCSCH,IMARCCOTH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarcsech (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arcsech (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccsch[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCCSCH\n"
	   "@SYNTAX=IMARCCSCH(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCCSCH returns the complex hyperbolic arccosecant of the "
	   "complex number z (@inumber), where\n\n\tarccsch(z) = arcsinh(1/z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCCSCH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCSCH(\"1+j\") equals 0.5306375-0.452278j.\n"
	   "\n"
	   "@SEEALSO=IMARCSECH,IMARCCOTH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarccsch (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arccsch (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imarccoth[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMARCCOTH\n"
	   "@SYNTAX=IMARCCOTH(inumber)\n"
	   "@DESCRIPTION="
	   "IMARCCOTH returns the complex hyperbolic arccotangent of the "
	   "complex number z (@inumber), where\n\n\t"
	   "arccoth(z) = arctanh(1/z).\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMARCCOTH returns "
	   "#VALUE! error.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCOTH(\"1+j\") equals 0.40235948-0.5535744j.\n"
	   "\n"
	   "@SEEALSO=IMARCSECH,IMARCCSCH")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imarccoth (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	gsl_complex_arccoth (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsqrt[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMSQRT\n"
	   "@SYNTAX=IMSQRT(inumber)\n"
	   "@DESCRIPTION="
	   "IMSQRT returns the square root of a complex number.\n"
	   "\n"
	   "* If @inumber is not a valid complex number, IMSQRT returns "
	   "#VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSQRT(\"1+j\") equals 1.09868+0.4550899j.\n"
	   "\n"
	   "@SEEALSO=IMPOWER")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imsqrt (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_sqrt (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_imsub[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMSUB\n"
	   "@SYNTAX=IMSUB(inumber1,inumber2)\n"
	   "@DESCRIPTION="
	   "IMSUB returns the difference of two complex numbers.\n"
	   "\n"
	   "* If @inumber1 or @inumber2 are not valid complex numbers, "
	   "IMSUB returns #VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSUB(\"3-j\",\"2+j\") equals 1-2j.\n"
	   "\n"
	   "@SEEALSO=IMSUM")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_imsub (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return value_new_error_VALUE (ei->pos);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return value_new_error_VALUE (ei->pos);

	complex_sub (&res, &a, &b);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static GnmFuncHelp const help_improduct[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMPRODUCT\n"
	   "@SYNTAX=IMPRODUCT(inumber1[,inumber2,...])\n"
	   "@DESCRIPTION="
	   "IMPRODUCT returns the product of given complex numbers.\n"
	   "\n"
	   "* If any of the @inumbers are not valid complex numbers, "
	   "IMPRODUCT returns #VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMPRODUCT(\"2-j\",\"4-2j\") equals 6-8j.\n"
	   "\n"
	   "@SEEALSO=IMDIV")
	},
	{ GNM_FUNC_HELP_END }
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
		return value_new_error_VALUE (ep);

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
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IMSUM\n"
	   "@SYNTAX=IMSUM(inumber1,inumber2)\n"
	   "@DESCRIPTION="
	   "IMSUM returns the sum of two complex numbers.\n"
	   "\n"
	   "* If @inumber1 or @inumber2 are not valid complex numbers, "
	   "IMSUM returns #VALUE! error.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSUM(\"2-4j\",\"9-j\") equals 11-5j.\n"
	   "\n"
	   "@SEEALSO=IMSUB")
	},
	{ GNM_FUNC_HELP_END }
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
        { "complex",     "ff|s", N_("real,im,suffix"), help_complex,
	  gnumeric_complex, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imabs",       "S",    N_("inumber"), help_imabs,
	  gnumeric_imabs, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imaginary",   "S",    N_("inumber"), help_imaginary,
	  gnumeric_imaginary, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imargument",  "S",    N_("inumber"), help_imargument,
	  gnumeric_imargument, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imconjugate", "S",    N_("inumber"), help_imconjugate,
	  gnumeric_imconjugate, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imcos",       "S",    N_("inumber"), help_imcos,
	  gnumeric_imcos, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imdiv",       "SS",   N_("inumber,inumber"), help_imdiv,
	  gnumeric_imdiv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imexp",       "S",    N_("inumber"), help_imexp,
	  gnumeric_imexp, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imln",        "S",    N_("inumber"), help_imln,
	  gnumeric_imln, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imlog10",     "S",    N_("inumber"), help_imlog10,
	  gnumeric_imlog10, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imlog2",      "S",    N_("inumber"), help_imlog2,
	  gnumeric_imlog2, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "impower",     "SS",   N_("inumber,inumber"), help_impower,
	  gnumeric_impower, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imreal",      "S",    N_("inumber"), help_imreal,
	  gnumeric_imreal, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imsin",       "S",    N_("inumber"), help_imsin,
	  gnumeric_imsin, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imsqrt",      "S",    N_("inumber"), help_imsqrt,
	  gnumeric_imsqrt, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imsub",       "SS",   N_("inumber,inumber"), help_imsub,
	  gnumeric_imsub, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imsum",       NULL,   N_("inumber,inumber"), help_imsum,
	  NULL, gnumeric_imsum, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        { "iminv",   "S",    N_("inumber"), help_iminv,
	  gnumeric_iminv, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "imneg",   "S",    N_("inumber"), help_imneg,
	  gnumeric_imneg, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imtan",       "S",    N_("inumber"), help_imtan,
	  gnumeric_imtan, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "improduct",   NULL,   N_("inumber,inumber"), help_improduct,
	  NULL, gnumeric_improduct, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imsec",       "S",    N_("inumber"), help_imsec,
	  gnumeric_imsec, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imcsc",       "S",    N_("inumber"), help_imcsc,
	  gnumeric_imcsc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imcot",       "S",    N_("inumber"), help_imcot,
	  gnumeric_imcot, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imsinh",       "S",   N_("inumber"), help_imsinh,
	  gnumeric_imsinh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imcosh",       "S",   N_("inumber"), help_imcosh,
	  gnumeric_imcosh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imtanh",       "S",   N_("inumber"), help_imtanh,
	  gnumeric_imtanh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imsech",       "S",   N_("inumber"), help_imsech,
	  gnumeric_imsech, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imcsch",       "S",   N_("inumber"), help_imcsch,
	  gnumeric_imcsch, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imcoth",       "S",   N_("inumber"), help_imcoth,
	  gnumeric_imcoth, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarcsin",     "S",   N_("inumber"), help_imarcsin,
	  gnumeric_imarcsin, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarccos",     "S",   N_("inumber"), help_imarccos,
	  gnumeric_imarccos, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarctan",     "S",   N_("inumber"), help_imarctan,
	  gnumeric_imarctan, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarcsec",     "S",   N_("inumber"), help_imarcsec,
	  gnumeric_imarcsec, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarccsc",     "S",   N_("inumber"), help_imarccsc,
	  gnumeric_imarccsc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarccot",     "S",   N_("inumber"), help_imarccot,
	  gnumeric_imarccot, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarcsinh",    "S",   N_("inumber"), help_imarcsinh,
	  gnumeric_imarcsinh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarccosh",    "S",   N_("inumber"), help_imarccosh,
	  gnumeric_imarccosh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarctanh",    "S",   N_("inumber"), help_imarctanh,
	  gnumeric_imarctanh, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarcsech",    "S",   N_("inumber"), help_imarcsech,
	  gnumeric_imarcsech, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarccsch",    "S",   N_("inumber"), help_imarccsch,
	  gnumeric_imarccsch, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "imarccoth",    "S",   N_("inumber"), help_imarccoth,
	  gnumeric_imarccoth, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        {NULL}
};
