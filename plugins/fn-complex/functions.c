/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-complex.c:  Built in complex number functions and functions registration
 *
 * Authors:
 *   Michael Meeks <michael@imaginator.com>
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Morten Welinder (terra@diku.dk)
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
#include <func.h>

#include <complex.h>
#include <parse-util.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <auto-format.h>
#include <libgnome/gnome-i18n.h>
#include <mathfunc.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"
#include "gsl-complex.h"


GNUMERIC_MODULE_PLUGIN_INFO_DECL;


/* Converts a complex number string into its coefficients.  Returns 0 if ok,
 * 1 if an error occurred.
 */
static int
value_get_as_complex (Value *val, complex_t *res, char *imunit)
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

static Value *
value_new_complex (const complex_t *c, char imunit)
{
	if (complex_real_p (c))
		return value_new_float (c->re);
	else {
		char *s, f[5 + 4 * sizeof (int) + strlen (GNUM_FORMAT_g)];
		Value *res;
		sprintf (f, "%%.%d" GNUM_FORMAT_g, GNUM_DIG);
		s = complex_to_string (c, f, f, imunit);
		res = value_new_string (s);
		g_free (s);
		return res;
	}
}

/***************************************************************************/

static const char *help_complex = {
	N_("@FUNCTION=COMPLEX\n"
	   "@SYNTAX=COMPLEX(real,im[,suffix])\n"

	   "@DESCRIPTION="
	   "COMPLEX returns a complex number of the form x + yi. "
	   "@real is the real and @im is the imaginary coefficient of "
	   "the complex number.  @suffix is the suffix for the imaginary "
	   "coefficient.  If it is omitted, COMPLEX uses 'i' by default."
	   "\n"
	   "If @suffix is neither 'i' nor 'j', COMPLEX returns #VALUE! "
	   "error.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "COMPLEX(1,-1) equals 1-i.\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_complex (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c;
	const char *suffix;

	complex_init (&c,
		      value_get_as_float (argv[0]),
		      value_get_as_float (argv[1]));
	suffix = argv[2] ? value_peek_string (argv[2]) : "i";

	if (strcmp (suffix, "i") != 0 && strcmp (suffix, "j") != 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_complex (&c, *suffix);
}

/***************************************************************************/

static const char *help_imaginary = {
	N_("@FUNCTION=IMAGINARY\n"
	   "@SYNTAX=IMAGINARY(inumber)\n"
	   "@DESCRIPTION="
	   "IMAGINARY returns the imaginary coefficient of a complex "
	   "number.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "IMAGINARY(\"132-j\") equals -1.\n"
	   "\n"
	   "@SEEALSO=IMREAL")
};

static Value *
gnumeric_imaginary (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c;
	char imunit;

	if (VALUE_IS_NUMBER (argv[0]))
	        return value_new_float (0.0);

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_float (c.im);
}

/***************************************************************************/

static const char *help_imabs = {
	N_("@FUNCTION=IMABS\n"
	   "@SYNTAX=IMABS(inumber)\n"
	   "@DESCRIPTION="
	   "IMABS returns the absolute value of a complex number.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "IMABS(\"2-j\") equals 2.23606798.\n"
	   "\n"
	   "@SEEALSO=IMAGINARY,IMREAL")
};


static Value *
gnumeric_imabs (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
	  return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (argv[0]->type != VALUE_STRING)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_float (complex_mod (&c));
}

/***************************************************************************/

static const char *help_imreal = {
	N_("@FUNCTION=IMREAL\n"
	   "@SYNTAX=IMREAL(inumber)\n"
	   "@DESCRIPTION="
	   "IMREAL returns the real coefficient of a complex number.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "imreal(\"132-j\") equals 132.\n"
	   "\n"
	   "@SEEALSO=IMAGINARY")
};


static Value *
gnumeric_imreal (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c;
	char imunit;

	if (VALUE_IS_NUMBER (argv[0]))
		return value_duplicate (argv[0]);

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_float (c.re);
}

/***************************************************************************/

static const char *help_imconjugate = {
	N_("@FUNCTION=IMCONJUGATE\n"
	   "@SYNTAX=IMCONJUGATE(inumber)\n"
	   "@DESCRIPTION="
	   "IMCONJUGATE returns the complex conjugate of a complex number.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCONJUGATE(\"1-j\") equals 1+j.\n"
	   "\n"
	   "@SEEALSO=IMAGINARY,IMREAL")
};

static Value *
gnumeric_imconjugate (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (argv[0]->type != VALUE_STRING)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	complex_conj (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imcos = {
	N_("@FUNCTION=IMCOS\n"
	   "@SYNTAX=IMCOS(inumber)\n"
	   "@DESCRIPTION="
	   "IMCOS returns the cosine of a complex number.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCOS(\"1+j\") equals 0.833730-0.988898j.\n"
	   "\n"
	   "@SEEALSO=IMSIN,IMTAN")
};

static Value *
gnumeric_imcos (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	complex_cos (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imtan = {
	N_("@FUNCTION=IMTAN\n"
	   "@SYNTAX=IMTAN(inumber)\n"
	   "@DESCRIPTION="
	   "IMTAN returns the tangent of a complex number.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=IMSIN,IMCOS")
};

static Value *
gnumeric_imtan (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	complex_tan (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imexp = {
	N_("@FUNCTION=IMEXP\n"
	   "@SYNTAX=IMEXP(inumber)\n"
	   "@DESCRIPTION="
	   "IMEXP returns the exponential of a complex number.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "IMEXP(\"2-j\") equals 3.992324-6.217676j.\n"
	   "\n"
	   "@SEEALSO=IMLN")
};

static Value *
gnumeric_imexp (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	complex_exp (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imargument = {
	N_("@FUNCTION=IMARGUMENT\n"
	   "@SYNTAX=IMARGUMENT(inumber)\n"
	   "@DESCRIPTION="
	   "IMARGUMENT returns the argument theta of a complex number.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARGUMENT(\"2-j\") equals -0.463647609.\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_imargument (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_float (complex_angle (&c));
}

/***************************************************************************/

static const char *help_imln = {
	N_("@FUNCTION=IMLN\n"
	   "@SYNTAX=IMLN(inumber)\n"
	   "@DESCRIPTION="
	   "IMLN returns the natural logarithm of a complex number. (The "
	   "result will have an imaginary part between -pi and +pi.  The "
	   "natural logarithm is not uniquely defined on complex numbers. "
	   "You may need to add or subtract an even multiple of pi to the "
	   "imaginary part.)\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "IMLN(\"3-j\") equals 1.15129-0.32175j.\n"
	   "\n"
	   "@SEEALSO=IMEXP,IMLOG2,IMLOG10")
};

static Value *
gnumeric_imln (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	complex_ln (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imlog2 = {
	N_("@FUNCTION=IMLOG2\n"
	   "@SYNTAX=IMLOG2(inumber)\n"
	   "@DESCRIPTION="
	   "IMLOG2 returns the logarithm of a complex number in base 2.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "IMLOG2(\"3-j\") equals 1.66096-0.46419j.\n"
	   "\n"
	   "@SEEALSO=IMLN,IMLOG10")
};

static Value *
gnumeric_imlog2 (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	complex_ln (&res, &c);
	complex_scale_real (&res, 1 / M_LN2gnum);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imlog10 = {
	N_("@FUNCTION=IMLOG10\n"
	   "@SYNTAX=IMLOG10(inumber)\n"
	   "@DESCRIPTION="
	   "IMLOG10 returns the logarithm of a complex number in base 10.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "IMLOG10(\"3-j\") equals 0.5-0.13973j.\n"
	   "\n"
	   "@SEEALSO=IMLN,IMLOG2")
};

static Value *
gnumeric_imlog10 (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	complex_ln (&res, &c);
	complex_scale_real (&res, 1 / M_LN10gnum);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_impower = {
	N_("@FUNCTION=IMPOWER\n"
	   "@SYNTAX=IMPOWER(inumber,number)\n"
	   "@DESCRIPTION="
	   "IMPOWER returns a complex number raised to a power.  @inumber is "
	   "the complex number to be raised to a power and @number is the "
	   "power to which you want to raise the complex number.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "IMPOWER(\"4-j\",2) equals 15-8j.\n"
	   "\n"
	   "@SEEALSO=IMSQRT")
};

static Value *
gnumeric_impower (FunctionEvalInfo *ei, Value **argv)
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (complex_real_p (&a) && a.re <= 0 && !complex_real_p (&b))
		return value_new_error (ei->pos, gnumeric_err_DIV0);

	complex_pow (&res, &a, &b);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imdiv = {
	N_("@FUNCTION=IMDIV\n"
	   "@SYNTAX=IMDIV(inumber,inumber)\n"
	   "@DESCRIPTION="
	   "IMDIV returns the quotient of two complex numbers.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "IMDIV(\"2-j\",\"2+j\") equals 0.6-0.8j.\n"
	   "\n"
	   "@SEEALSO=IMPRODUCT")
};

static Value *
gnumeric_imdiv (FunctionEvalInfo *ei, Value **argv)
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (complex_zero_p (&b))
		return value_new_error (ei->pos, gnumeric_err_DIV0);

	complex_div (&res, &a, &b);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imsin = {
	N_("@FUNCTION=IMSIN\n"
	   "@SYNTAX=IMSIN(inumber)\n"
	   "@DESCRIPTION="
	   "IMSIN returns the sine of a complex number.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSIN(\"1+j\") equals 1.29846+0.63496j.\n"
	   "\n"
	   "@SEEALSO=IMCOS,IMTAN")
};

static Value *
gnumeric_imsin (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	complex_sin (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imsinh = {
	N_("@FUNCTION=IMSINH\n"
	   "@SYNTAX=IMSINH(z)\n"
	   "@DESCRIPTION="
	   "IMSINH returns the complex hyperbolic sine of the complex "
	   "number z, sinh(z) = (exp(z) - exp(-z))/2.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSINH(\"1+j\") equals 0.63496+1.29846j.\n"
	   "\n"
	   "@SEEALSO=IMCOSH,IMTANH")
};

static Value *
gnumeric_imsinh (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_sinh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imcosh = {
	N_("@FUNCTION=IMCOSH\n"
	   "@SYNTAX=IMCOSH(z)\n"
	   "@DESCRIPTION="
	   "IMCOSH returns the complex hyperbolic cosine of the complex "
	   "number z, cosh(z) = (exp(z) + exp(-z))/2.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCOSH(\"1+j\") equals 0.83373+0.988898j.\n"
	   "\n"
	   "@SEEALSO=IMSINH,IMTANH")
};

static Value *
gnumeric_imcosh (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_cosh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imtanh = {
	N_("@FUNCTION=IMTANH\n"
	   "@SYNTAX=IMTANH(z)\n"
	   "@DESCRIPTION="
	   "IMTANH returns the complex hyperbolic tangent of the complex "
	   "number z, tanh(z) = sinh(z)/cosh(z).\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMTANH(\"1+j\") equals 1.083923+0.2717526j.\n"
	   "\n"
	   "@SEEALSO=IMSINH,IMCOSH")
};

static Value *
gnumeric_imtanh (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_tanh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imsech = {
	N_("@FUNCTION=IMSECH\n"
	   "@SYNTAX=IMSECH(z)\n"
	   "@DESCRIPTION="
	   "IMSECH returns the complex hyperbolic secant of the complex "
	   "number z, sech(z) = 1/cosh(z).\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSECH(\"1+j\") equals 0.498337-0.5910838j.\n"
	   "\n"
	   "@SEEALSO=IMCSCH,IMCOTH")
};

static Value *
gnumeric_imsech (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_sech (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imcsch = {
	N_("@FUNCTION=IMCSCH\n"
	   "@SYNTAX=IMCSCH(z)\n"
	   "@DESCRIPTION="
	   "IMCSCH returns the complex hyperbolic cosecant of the "
	   "complex number z, csch(z) = 1/sinh(z).\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCSCH(\"1+j\") equals 0.303931-0.621518j.\n"
	   "\n"
	   "@SEEALSO=IMSECH,IMCOTH")
};

static Value *
gnumeric_imcsch (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_csch (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imcoth = {
	N_("@FUNCTION=IMCOTH\n"
	   "@SYNTAX=IMCOTH(z)\n"
	   "@DESCRIPTION="
	   "IMCOTH returns the complex hyperbolic cotangent of the complex "
	   "number z, coth(z) = 1/tanh(z).\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMCOTH(\"1+j\") equals 0.868014-0.217622j.\n"
	   "\n"
	   "@SEEALSO=IMSECH,IMCSCH")
};

static Value *
gnumeric_imcoth (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_coth (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarcsin = {
	N_("@FUNCTION=IMARCSIN\n"
	   "@SYNTAX=IMARCSIN(z)\n"
	   "@DESCRIPTION="
	   "IMARCSIN returns the complex arcsine of the complex number z, "
	   "arcsin(z). The branch cuts are on the real axis, less than -1 "
	   "and greater than 1.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCSIN(\"1+j\") equals 0.6662394+1.061275j.\n"
	   "\n"
	   "@SEEALSO=IMARCCOS,IMARCTAN")
};

static Value *
gnumeric_imarcsin (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arcsin (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarccos = {
	N_("@FUNCTION=IMARCCOS\n"
	   "@SYNTAX=IMARCCOS(z)\n"
	   "@DESCRIPTION="
	   "IMARCCOS returns the complex arccosine of the complex number z, "
	   "arccos(z). The branch cuts are on the real axis, less than -1 "
	   "and greater than 1.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCOS(\"1+j\") equals 0.9045569-1.061275j.\n"
	   "\n"
	   "@SEEALSO=IMARCSIN,IMARCTAN")
};

static Value *
gnumeric_imarccos (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arccos (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarctan = {
	N_("@FUNCTION=IMARCTAN\n"
	   "@SYNTAX=IMARCTAN(z)\n"
	   "@DESCRIPTION="
	   "IMARCTAN returns the complex arctangent of the complex number "
	   "z, arctan(z). The branch cuts are on the imaginary axis, "
	   "below -i and above i.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCTAN(\"1+j\") equals 1.0172220+0.4023595j.\n"
	   "\n"
	   "@SEEALSO=IMARCSIN,IMARCCOS")
};

static Value *
gnumeric_imarctan (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arctan (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarcsec = {
	N_("@FUNCTION=IMARCSEC\n"
	   "@SYNTAX=IMARCSEC(z)\n"
	   "@DESCRIPTION="
	   "IMARCSEC returns the complex arcsecant of the complex number "
	   "z, arcsec(z) = arccos(1/z).\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCSEC(\"1+j\") equals 1.1185179+0.5306375j.\n"
	   "\n"
	   "@SEEALSO=IMARCCSC,IMARCOT")
};

static Value *
gnumeric_imarcsec (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arcsec (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarccsc = {
	N_("@FUNCTION=IMARCCSC\n"
	   "@SYNTAX=IMARCCSC(z)\n"
	   "@DESCRIPTION="
	   "IMARCCSC returns the complex arccosecant of the complex "
	   "number z, arccsc(z) = arcsin(1/z).\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCSC(\"1+j\") equals 0.45227845-0.5306375j.\n"
	   "\n"
	   "@SEEALSO=IMARCSEC,IMARCOT")
};

static Value *
gnumeric_imarccsc (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arccsc (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarccot = {
	N_("@FUNCTION=IMARCCOT\n"
	   "@SYNTAX=IMARCCOT(z)\n"
	   "@DESCRIPTION="
	   "IMARCCOT returns the complex arccotangent of the complex "
	   "number z, arccot(z) = arctan(1/z).\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCOT(\"1+j\") equals 0.553574+0.4023595j.\n"
	   "\n"
	   "@SEEALSO=IMARCSEC,IMARCSC")
};

static Value *
gnumeric_imarccot (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arccot (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarcsinh = {
	N_("@FUNCTION=IMARCSINH\n"
	   "@SYNTAX=IMARCSINH(z)\n"
	   "@DESCRIPTION="
	   "IMARCSINH returns the complex hyperbolic arcsine of the "
	   "complex number z, arcsinh(z). The branch cuts are on the "
	   "imaginary axis, below -i and above i.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCSINH(\"1+j\") equals 1.061275+0.6662394j.\n"
	   "\n"
	   "@SEEALSO=IMARCCOSH,IMARCTANH")
};

static Value *
gnumeric_imarcsinh (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arcsinh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarccosh = {
	N_("@FUNCTION=IMARCCOSH\n"
	   "@SYNTAX=IMARCCOSH(z)\n"
	   "@DESCRIPTION="
	   "IMARCCOSH returns the complex hyperbolic arccosine of the "
	   "complex number z, arccosh(z). The branch cut is on the real "
	   "axis, less than 1.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCOSH(\"1+j\") equals 1.06127506+0.904557j.\n"
	   "\n"
	   "@SEEALSO=IMARCSINH,IMARCTANH")
};

static Value *
gnumeric_imarccosh (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arccosh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarctanh = {
	N_("@FUNCTION=IMARCTANH\n"
	   "@SYNTAX=IMARCTANH(z)\n"
	   "@DESCRIPTION="
	   "IMARCTANH returns the complex hyperbolic arctangent of the "
	   "complex number z, arctanh(z). The branch cuts are on the "
	   "real axis, less than -1 and greater than 1.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCTANH(\"1+j\") equals 0.4023595+1.0172220j.\n"
	   "\n"
	   "@SEEALSO=IMARCSINH,IMARCCOSH")
};

static Value *
gnumeric_imarctanh (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arctanh (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarcsech = {
	N_("@FUNCTION=IMARCSECH\n"
	   "@SYNTAX=IMARCSECH(z)\n"
	   "@DESCRIPTION="
	   "IMARCSECH returns the complex hyperbolic arcsecant of the "
	   "complex number z, arcsech(z) = arccosh(1/z).\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCSECH(\"1+j\") equals 0.5306375-1.118518j.\n"
	   "\n"
	   "@SEEALSO=IMARCCSCH,IMARCOTH")
};

static Value *
gnumeric_imarcsech (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arcsech (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarccsch = {
	N_("@FUNCTION=IMARCCSCH\n"
	   "@SYNTAX=IMARCCSCH(z)\n"
	   "@DESCRIPTION="
	   "IMARCCSCH returns the complex hyperbolic arccosecant of the "
	   "complex number z, arccsch(z) = arcsin(1/z).\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCSCH(\"1+j\") equals 0.5306375-0.452278j.\n"
	   "\n"
	   "@SEEALSO=IMARCSECH,IMARCOTH")
};

static Value *
gnumeric_imarccsch (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arccsch (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imarccoth = {
	N_("@FUNCTION=IMARCCOTH\n"
	   "@SYNTAX=IMARCCOTH(z)\n"
	   "@DESCRIPTION="
	   "IMARCCOTH returns the complex hyperbolic arccotangent of the "
	   "complex number z, arccoth(z) = arctanh(1/z).\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IMARCCOTH(\"1+j\") equals 0.40235948-0.5535744j.\n"
	   "\n"
	   "@SEEALSO=IMARCSECH,IMARCSCH")
};

static Value *
gnumeric_imarccoth (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char      imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	gsl_complex_arccoth (&c, &res);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imsqrt = {
	N_("@FUNCTION=IMSQRT\n"
	   "@SYNTAX=IMSQRT(inumber)\n"
	   "@DESCRIPTION="
	   "IMSQRT returns the square root of a complex number.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSQRT(\"1+j\") equals 1.09868+0.4550899j.\n"
	   "\n"
	   "@SEEALSO=IMPOWER")
};

static Value *
gnumeric_imsqrt (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	complex_sqrt (&res, &c);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_imsub = {
	N_("@FUNCTION=IMSUB\n"
	   "@SYNTAX=IMSUB(inumber,inumber)\n"
	   "@DESCRIPTION="
	   "IMSUB returns the difference of two complex numbers.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSUB(\"3-j\",\"2+j\") equals 1-2j.\n"
	   "\n"
	   "@SEEALSO=IMSUM")
};

static Value *
gnumeric_imsub (FunctionEvalInfo *ei, Value **argv)
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	complex_sub (&res, &a, &b);
	return value_new_complex (&res, imunit);
}

/***************************************************************************/

static const char *help_improduct = {
	N_("@FUNCTION=IMPRODUCT\n"
	   "@SYNTAX=IMPRODUCT(inumber1[,inumber2,...])\n"
	   "@DESCRIPTION="
	   "IMPRODUCT returns the product of given complex numbers.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "IMPRODUCT(\"2-j\",\"4-2j\") equals 6-8j.\n"
	   "\n"
	   "@SEEALSO=IMDIV")
};


typedef enum {
        Improduct, Imsum
} eng_imoper_type_t;

typedef struct {
	complex_t         res;
        char              imunit;
        eng_imoper_type_t type;
} eng_imoper_t;

static Value *
callback_function_imoper (const EvalPos *ep, Value *value, void *closure)
{
        eng_imoper_t *result = closure;
	complex_t c;
	char *imptr, dummy;

	imptr = VALUE_IS_NUMBER (value) ? &dummy : &result->imunit;
	if (value_get_as_complex (value, &c, imptr))
		return value_new_error (ep, gnumeric_err_VALUE);

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

static Value *
gnumeric_improduct (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	Value *v;
        eng_imoper_t p;

	p.type = Improduct;
	p.imunit = 'j';
	complex_real (&p.res, 1);

        if ((v = function_iterate_argument_values (ei->pos,
			&callback_function_imoper, &p, expr_node_list,
			TRUE, CELL_ITER_IGNORE_BLANK)) != NULL)
                return v;

	return value_new_complex (&p.res, p.imunit);
}

/***************************************************************************/

static const char *help_imsum = {
	N_("@FUNCTION=IMSUM\n"
	   "@SYNTAX=IMSUM(inumber,inumber)\n"
	   "@DESCRIPTION="
	   "IMSUM returns the sum of two complex numbers.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "IMSUM(\"2-4j\",\"9-j\") equals 11-5j.\n"
	   "\n"
	   "@SEEALSO=IMSUB")
};

static Value *
gnumeric_imsum (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	Value *v;
        eng_imoper_t p;

	p.type = Imsum;
	p.imunit = 'j';
	complex_real (&p.res, 0);

        if ((v = function_iterate_argument_values (ei->pos,
			callback_function_imoper, &p, expr_node_list,
			TRUE, CELL_ITER_IGNORE_BLANK)) != NULL)
                return v;

	return value_new_complex (&p.res, p.imunit);
}

/***************************************************************************/

const ModulePluginFunctionInfo complex_functions[] = {
        { "complex",     "ff|s", "real,im[,suffix]", &help_complex,
	  gnumeric_complex, NULL, NULL, NULL },
        { "imabs",       "?",    "inumber", &help_imabs,
	  gnumeric_imabs, NULL, NULL, NULL },
        { "imaginary",   "?",    "inumber", &help_imaginary,
	  gnumeric_imaginary, NULL, NULL, NULL },
        { "imargument",  "?",    "inumber", &help_imargument,
	  gnumeric_imargument, NULL, NULL, NULL },
        { "imconjugate", "?",    "inumber", &help_imconjugate,
	  gnumeric_imconjugate, NULL, NULL, NULL },
        { "imcos",       "?",    "inumber", &help_imcos,
	  gnumeric_imcos, NULL, NULL, NULL },
        { "imtan",       "?",    "inumber", &help_imtan,
	  gnumeric_imtan, NULL, NULL, NULL },
        { "imdiv",       "??",   "inumber,inumber", &help_imdiv,
	  gnumeric_imdiv, NULL, NULL, NULL },
        { "imexp",       "?",    "inumber", &help_imexp,
	  gnumeric_imexp, NULL, NULL, NULL },
        { "imln",        "?",    "inumber", &help_imln,
	  gnumeric_imln, NULL, NULL, NULL },
        { "imlog10",     "?",    "inumber", &help_imlog10,
	  gnumeric_imlog10, NULL, NULL, NULL },
        { "imlog2",      "?",    "inumber", &help_imlog2,
	  gnumeric_imlog2, NULL, NULL, NULL },
        { "impower",     "??",   "inumber,inumber", &help_impower,
	  gnumeric_impower, NULL, NULL, NULL },
        { "improduct",   NULL,   "inumber,inumber", &help_improduct,
	  NULL, gnumeric_improduct, NULL, NULL },
        { "imreal",      "?",    "inumber", &help_imreal,
	  gnumeric_imreal, NULL, NULL, NULL },
        { "imsin",       "?",    "inumber", &help_imsin,
	  gnumeric_imsin, NULL, NULL, NULL },
        { "imsqrt",      "?",    "inumber", &help_imsqrt,
	  gnumeric_imsqrt, NULL, NULL, NULL },
        { "imsub",       "??",   "inumber,inumber", &help_imsub,
	  gnumeric_imsub, NULL, NULL, NULL },
        { "imsum",       NULL,   "inumber,inumber", &help_imsum,
	  NULL, gnumeric_imsum, NULL, NULL },
        { "imsinh",       "?",   "inumber", &help_imsinh,
	  gnumeric_imsinh, NULL, NULL, NULL },
        { "imcosh",       "?",   "inumber", &help_imcosh,
	  gnumeric_imcosh, NULL, NULL, NULL },
        { "imtanh",       "?",   "inumber", &help_imtanh,
	  gnumeric_imtanh, NULL, NULL, NULL },
        { "imsech",       "?",   "inumber", &help_imsech,
	  gnumeric_imsech, NULL, NULL, NULL },
        { "imcsch",       "?",   "inumber", &help_imcsch,
	  gnumeric_imcsch, NULL, NULL, NULL },
        { "imcoth",       "?",   "inumber", &help_imcoth,
	  gnumeric_imcoth, NULL, NULL, NULL },
        { "imarcsin",     "?",   "inumber", &help_imarcsin,
	  gnumeric_imarcsin, NULL, NULL, NULL },
        { "imarccos",     "?",   "inumber", &help_imarccos,
	  gnumeric_imarccos, NULL, NULL, NULL },
        { "imarctan",     "?",   "inumber", &help_imarctan,
	  gnumeric_imarctan, NULL, NULL, NULL },
        { "imarcsec",     "?",   "inumber", &help_imarcsec,
	  gnumeric_imarcsec, NULL, NULL, NULL },
        { "imarccsc",     "?",   "inumber", &help_imarccsc,
	  gnumeric_imarccsc, NULL, NULL, NULL },
        { "imarccot",     "?",   "inumber", &help_imarccot,
	  gnumeric_imarccot, NULL, NULL, NULL },
        { "imarcsinh",    "?",   "inumber", &help_imarcsinh,
	  gnumeric_imarcsinh, NULL, NULL, NULL },
        { "imarccosh",    "?",   "inumber", &help_imarccosh,
	  gnumeric_imarccosh, NULL, NULL, NULL },
        { "imarctanh",    "?",   "inumber", &help_imarctanh,
	  gnumeric_imarctanh, NULL, NULL, NULL },
        { "imarcsech",    "?",   "inumber", &help_imarcsech,
	  gnumeric_imarcsech, NULL, NULL, NULL },
        { "imarccsch",    "?",   "inumber", &help_imarccsch,
	  gnumeric_imarccsch, NULL, NULL, NULL },
        { "imarccoth",    "?",   "inumber", &help_imarccoth,
	  gnumeric_imarccoth, NULL, NULL, NULL },
        {NULL}
};

/* FIXME: Should be merged into the above.  */
static const struct {
	const char *func;
	AutoFormatTypes typ;
} af_info[] = {
	{ NULL, AF_UNKNOWN }
};

void
plugin_init (void)
{
	int i;
	for (i = 0; af_info[i].func; i++)
		auto_format_function_result_by_name (af_info[i].func,
						     af_info[i].typ);
}

void
plugin_cleanup (void)
{
	int i;
	for (i = 0; af_info[i].func; i++)
		auto_format_function_result_remove (af_info[i].func);
}
