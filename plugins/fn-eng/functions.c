/*
 * fn-eng.c:  Built in engineering functions and functions registration
 *
 * Author:
 *  Michael Meeks <michael@imaginator.com>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
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

#include <parse-util.h>
#include <complex.h>
#include <str.h>
#include <value.h>
#include <mathfunc.h>

#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgnome/gnome-i18n.h>

/**
 * FIXME: In the long term this needs optimising.
 **/
static Value *
val_to_base (FunctionEvalInfo *ei, Value **argv, int num_argv,
	     int src_base, int dest_base)
{
	Value *value;
	int max, places;
	char *err, buffer[40];
	const char *str;
	gnum_float v, b10;
	int digit;

	g_return_val_if_fail (src_base > 1 && src_base <= 36,
			      value_new_error (ei->pos, gnumeric_err_VALUE));
	g_return_val_if_fail (dest_base > 1 && dest_base <= 36,
			      value_new_error (ei->pos, gnumeric_err_VALUE));

	value = argv[0];
	if (VALUE_IS_EMPTY (value))
		return value_new_error (ei->pos, gnumeric_err_NUM);
	else if (VALUE_IS_EMPTY_OR_ERROR (value))
		return value_duplicate (value);

	places = (num_argv >= 2 && argv[1]) ? value_get_as_int (argv[1]) : 0;
	str = value_peek_string (value);

	v = strtol (str, &err, src_base);
	if (*err)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	b10 = powgnum (src_base, 10);
	if (v >= b10 / 2) /* N's complement */
		v = v - b10;

	if (dest_base == 10)
		return value_new_int (v);

	if (v < 0) {
		max = 10;
		v += powgnum (dest_base, max);
	} else {
		if (v == 0)
			max = 1;
		else
			max = (int)(loggnum (v + 0.5) / loggnum (dest_base)) + 1;
	}

	if (places > max)
		max = places;
	if (max >= (int)sizeof (buffer))
		return value_new_error (ei->pos, _("Unimplemented"));

	for (digit = max - 1; digit >= 0; digit--) {
		int thisdigit = fmod (v + 0.5, dest_base);
		v = floor ((v + 0.5) / dest_base);
		buffer[digit] = thisdigit["0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"];
	}
	buffer[max] = 0;
	return value_new_string (buffer);
}

/***************************************************************************/

static const char *help_bin2dec = {
	N_("@FUNCTION=BIN2DEC\n"
	   "@SYNTAX=BIN2DEC(x)\n"

	   "@DESCRIPTION="
	   "BIN2DEC function converts a binary number "
	   "in string or number to its decimal equivalent.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "BIN2DEC(101) equals 5.\n"
	   "\n"
	   "@SEEALSO=DEC2BIN, BIN2OCT, BIN2HEX")
};

static Value *
gnumeric_bin2dec (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 1, 2, 10);
}

/***************************************************************************/

static const char *help_bin2oct = {
	N_("@FUNCTION=BIN2OCT\n"
	   "@SYNTAX=BIN2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "BIN2OCT function converts a binary number to an octal number. "
	   "@places is an optional field, specifying to zero pad to that "
	   "number of spaces."
	   "\n"
	   "If @places is too small or negative #NUM! error is returned.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "BIN2OCT(110111) equals 67.\n"
	   "\n"
	   "@SEEALSO=OCT2BIN, BIN2DEC, BIN2HEX")
};

static Value *
gnumeric_bin2oct (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 2, 8);
}

/***************************************************************************/

static const char *help_bin2hex = {
	N_("@FUNCTION=BIN2HEX\n"
	   "@SYNTAX=BIN2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "BIN2HEX function converts a binary number to a "
	   "hexadecimal number.  @places is an optional field, specifying "
	   "to zero pad to that number of spaces."
	   "\n"
	   "If @places is too small or negative #NUM! error is returned.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "BIN2HEX(100111) equals 27.\n"
	   "\n"
	   "@SEEALSO=HEX2BIN, BIN2OCT, BIN2DEC")
};

static Value *
gnumeric_bin2hex (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 2, 16);
}

/***************************************************************************/

static const char *help_dec2bin = {
	N_("@FUNCTION=DEC2BIN\n"
	   "@SYNTAX=DEC2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "DEC2BIN function converts a decimal number to a binary number. "
	   "@places is an optional field, specifying to zero pad to that "
	   "number of spaces."
	   "\n"
	   "If @places is too small or negative #NUM! error is returned.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "DEC2BIN(42) equals 101010.\n"
	   "\n"
	   "@SEEALSO=BIN2DEC, DEC2OCT, DEC2HEX")
};

static Value *
gnumeric_dec2bin (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 10, 2);
}

/***************************************************************************/

static const char *help_dec2oct = {
	N_("@FUNCTION=DEC2OCT\n"
	   "@SYNTAX=DEC2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "DEC2OCT function converts a decimal number to an octal number. "
	   "@places is an optional field, specifying to zero pad to that "
	   "number of spaces."
	   "\n"
	   "If @places is too small or negative #NUM! error is returned.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "DEC2OCT(42) equals 52.\n"
	   "\n"
	   "@SEEALSO=OCT2DEC, DEC2BIN, DEC2HEX")
};

static Value *
gnumeric_dec2oct (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 10, 8);
}

/***************************************************************************/

static const char *help_dec2hex = {
	N_("@FUNCTION=DEC2HEX\n"
	   "@SYNTAX=DEC2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "DEC2HEX function converts a decimal number to a hexadecimal "
	   "number. @places is an optional field, specifying to zero pad "
	   "to that number of spaces."
	   "\n"
	   "If @places is too small or negative #NUM! error is returned.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "DEC2HEX(42) equals 2A.\n"
	   "\n"
	   "@SEEALSO=HEX2DEC, DEC2BIN, DEC2OCT")
};

static Value *
gnumeric_dec2hex (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 10, 16);
}

/***************************************************************************/

static const char *help_oct2dec = {
	N_("@FUNCTION=OCT2DEC\n"
	   "@SYNTAX=OCT2DEC(x)\n"

	   "@DESCRIPTION="
	   "OCT2DEC function converts an octal number "
	   "in a string or number to its decimal equivalent.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "OCT2DEC(\"124\") equals 84.\n"
	   "\n"
	   "@SEEALSO=DEC2OCT, OCT2BIN, OCT2HEX")
};

static Value *
gnumeric_oct2dec (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 1, 8, 10);
}

/***************************************************************************/

static const char *help_oct2bin = {
	N_("@FUNCTION=OCT2BIN\n"
	   "@SYNTAX=OCT2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "OCT2BIN function converts an octal number to a binary "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "If @places is too small or negative #NUM! error is returned."
	   "\n"
	   "@EXAMPLES=\n"
	   "OCT2BIN(\"213\") equals 10001011.\n"
	   "\n"
	   "@SEEALSO=BIN2OCT, OCT2DEC, OCT2HEX")
};

static Value *
gnumeric_oct2bin (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 8, 2);
}

/***************************************************************************/

static const char *help_oct2hex = {
	N_("@FUNCTION=OCT2HEX\n"
	   "@SYNTAX=OCT2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "OCT2HEX function converts an octal number to a hexadecimal "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "If @places is too small or negative #NUM! error is returned."
	   "\n"
	   "@EXAMPLES=\n"
	   "OCT2HEX(132) equals 5A.\n"
	   "\n"
	   "@SEEALSO=HEX2OCT, OCT2BIN, OCT2DEC")
};

static Value *
gnumeric_oct2hex (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 8, 16);
}

/***************************************************************************/

static const char *help_hex2bin = {
	N_("@FUNCTION=HEX2BIN\n"
	   "@SYNTAX=HEX2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "HEX2BIN function converts a hexadecimal number to a binary "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces."
	   "\n"
	   "If @places is too small or negative #NUM! error is returned.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "HEX2BIN(\"2A\") equals 101010.\n"
	   "\n"
	   "@SEEALSO=BIN2HEX, HEX2OCT, HEX2DEC")
};

static Value *
gnumeric_hex2bin (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 16, 2);
}

/***************************************************************************/

static const char *help_hex2oct = {
	N_("@FUNCTION=HEX2OCT\n"
	   "@SYNTAX=HEX2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "HEX2OCT function converts a hexadecimal number to an octal "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces."
	   "\n"
	   "If @places is too small or negative #NUM! error is returned.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "HEX2OCT(\"2A\") equals 52.\n"
	   "\n"
	   "@SEEALSO=OCT2HEX, HEX2BIN, HEX2DEC")
};

static Value *
gnumeric_hex2oct (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 16, 8);
}

/***************************************************************************/

static const char *help_hex2dec = {
	N_("@FUNCTION=HEX2DEC\n"
	   "@SYNTAX=HEX2DEC(x)\n"

	   "@DESCRIPTION="
	   "HEX2DEC function converts a hexadecimal number "
	   "to its decimal equivalent.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "HEX2DEC(\"2A\") equals 42.\n"
	   "\n"
	   "@SEEALSO=DEC2HEX, HEX2BIN, HEX2OCT")
};

static Value *
gnumeric_hex2dec (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 1, 16, 10);
}

/***************************************************************************/

static const char *help_besseli = {
	N_("@FUNCTION=BESSELI\n"
	   "@SYNTAX=BESSELI(x,y)\n"

	   "@DESCRIPTION="
	   "BESSELI function returns the Neumann, Weber or Bessel "
	   "function. "
	   "@x is where the function is evaluated. "
	   "@y is the order of the bessel function, if non-integer it is "
	   "truncated."
	   "\n"
	   "If @x or @y are not numeric a #VALUE! error is returned. "
	   "If @y < 0 a #NUM! error is returned.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "BESSELI(0.7,3) equals 0.007367374.\n"
	   "\n"
	   "@SEEALSO=BESSELJ,BESSELK,BESSELY")
};


static Value *
gnumeric_besseli (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x, order;

	x = value_get_as_float (argv[0]);	/* value to evaluate I_n at. */
	order = value_get_as_float (argv[1]);	/* the order */

	if (order < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (bessel_i (x, order, 1.0));
}

/***************************************************************************/

static const char *help_besselk = {
	N_("@FUNCTION=BESSELK\n"
	   "@SYNTAX=BESSELK(x,y)\n"

	   "@DESCRIPTION="
	   "BESSELK function returns the Neumann, Weber or Bessel "
	   "function. "
	   "@x is where the function is evaluated. "
	   "@y is the order of the bessel function, if non-integer it is "
	   "truncated."
	   "\n"
	   "If x or n are not numeric a #VALUE! error is returned. "
	   "If y < 0 a #NUM! error is returned.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "BESSELK(3,9) equals 397.95880.\n"
	   "\n"
	   "@SEEALSO=BESSELI,BESSELJ,BESSELY")
};

static Value *
gnumeric_besselk (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x, order;

	x = value_get_as_float (argv[0]);	/* value to evaluate K_n at. */
	order = value_get_as_float (argv[1]);	/* the order */

	if (order < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (bessel_k (x, order, 1.0));
}

/***************************************************************************/

static const char *help_besselj = {
	N_("@FUNCTION=BESSELJ\n"
	   "@SYNTAX=BESSELJ(x,y)\n"

	   "@DESCRIPTION="
	   "BESSELJ function returns the bessel function with "
	   "@x is where the function is evaluated. "
	   "@y is the order of the bessel function, if non-integer it is "
	   "truncated."
	   "\n"
	   "If @x or @y are not numeric a #VALUE! error is returned.  "
	   "If @y < 0 a #NUM! error is returned.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "BESSELJ(0.89,3) equals 0.013974004.\n"
	   "\n"
	   "@SEEALSO=BESSELJ,BESSELK,BESSELY")
};

static Value *
gnumeric_besselj (FunctionEvalInfo *ei, Value **argv)
{
	int x, y;

	x = value_get_as_int (argv[0]);
	y = value_get_as_int (argv[1]);

	if (y < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (jn (y, value_get_as_float (argv[0])));
}

/***************************************************************************/

static const char *help_bessely = {
	N_("@FUNCTION=BESSELY\n"
	   "@SYNTAX=BESSELY(x,y)\n"

	   "@DESCRIPTION="
	   "BESSELY function returns the Neumann, Weber or Bessel "
	   "function. "
	   "@x is where the function is evaluated. "
	   "@y is the order of the bessel function, if non-integer it is "
	   "truncated."
	   "\n"
	   "If x or n are not numeric a #VALUE! error is returned. "
	   "If n < 0 a #NUM! error is returned.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "BESSELY(4,2) equals 0.215903595.\n"
	   "\n"
	   "@SEEALSO=BESSELJ,BESSELK,BESSELY")
};

static Value *
gnumeric_bessely (FunctionEvalInfo *ei, Value **argv)
{
	int y;
	if (argv[0]->type != VALUE_INTEGER &&
	    argv[1]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT &&
	    argv[1]->type != VALUE_FLOAT)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if ((y = value_get_as_int (argv[1])) < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (yngnum (y, value_get_as_float (argv[0])));
}

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
gnumeric_improduct (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	Value *v;
        eng_imoper_t p;

	p.type = Improduct;
	p.imunit = 'j';
	complex_real (&p.res, 1);

        if ((v = function_iterate_argument_values (ei->pos,
						   &callback_function_imoper,
						   &p, expr_node_list,
						   TRUE, TRUE)) != NULL)
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
gnumeric_imsum (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	Value *v;
        eng_imoper_t p;

	p.type = Imsum;
	p.imunit = 'j';
	complex_real (&p.res, 0);

        if ((v = function_iterate_argument_values (ei->pos,
						   callback_function_imoper,
						   &p, expr_node_list,
						   TRUE, TRUE)) != NULL)
                return v;

	return value_new_complex (&p.res, p.imunit);
}

/***************************************************************************/

static const char *help_convert = {
	N_("@FUNCTION=CONVERT\n"
	   "@SYNTAX=CONVERT(number,from_unit,to_unit)\n"
	   "@DESCRIPTION="
	   "CONVERT returns a conversion from one measurement system to "
	   "another.  For example, you can convert a weight in pounds "
	   "to a weight in grams.  @number is the value you want to "
	   "convert, @from_unit specifies the unit of the @number, and "
	   "@to_unit is the unit for the result."
	   "\n"
	   "@from_unit and @to_unit can be any of the following:\n\n"
	   "Weight and mass:\n"
	   "'g'    Gram\n"
           "'sg'   Slug\n"
	   "'lbm'  Pound\n"
	   "'u'    U (atomic mass)\n"
	   "'ozm'  Ounce\n\n"
	   "Distance:\n"
	   "'m'    Meter\n"
	   "'mi'   Statute mile\n"
	   "'Nmi'  Nautical mile\n"
	   "'in'   Inch\n"
	   "'ft'   Foot\n"
	   "'yd'   Yard\n"
	   "'ang'  Angstrom\n"
	   "'Pica' Pica\n\n"
	   "Time:\n"
	   "'yr'   Year\n"
	   "'day'  Day\n"
	   "'hr'   Hour\n"
	   "'mn'   Minute\n"
	   "'sec'  Second\n\n"
	   "Pressure:\n"
	   "'Pa'   Pascal\n"
	   "'atm'  Atmosphere\n"
	   "'mmHg' mm of Mercury\n\n"
	   "Force:\n"
	   "'N'    Newton\n"
	   "'dyn'  Dyne\n"
	   "'lbf'  Pound force\n\n"
	   "Energy:\n"
	   "'J'    Joule\n"
	   "'e'    Erg\n"
	   "'c'    Thermodynamic calorie\n"
	   "'cal'  IT calorie\n"
	   "'eV'   Electron volt\n"
	   "'HPh'  Horsepower-hour\n"
	   "'Wh'   Watt-hour\n"
	   "'flb'  Foot-pound\n"
	   "'BTU'  BTU\n\n"
	   "Power:\n"
	   "'HP'   Horsepower\n"
	   "'W'    Watt\n"
	   "Magnetism:\n"
	   "'T'    Tesla\n"
	   "'ga'   Gauss\n\n"
	   "Temperature:\n"
	   "'C'    Degree Celsius\n"
	   "'F'    Degree Fahrenheit\n"
	   "'K'    Degree Kelvin\n\n"
	   "Liquid measure:\n"
	   "'tsp'  Teaspoon\n"
	   "'tbs'  Tablespoon\n"
	   "'oz'   Fluid ounce\n"
	   "'cup'  Cup\n"
	   "'pt'   Pint\n"
	   "'qt'   Quart\n"
	   "'gal'  Gallon\n"
	   "'l'    Liter\n\n"
	   "For metric units any of the following prefixes can be used:\n"
	   "'E'  exa    1E+18\n"
	   "'P'  peta   1E+15\n"
	   "'T'  tera   1E+12\n"
	   "'G'  giga   1E+09\n"
	   "'M'  mega   1E+06\n"
	   "'k'  kilo   1E+03\n"
	   "'h'  hecto  1E+02\n"
	   "'e'  dekao  1E+01\n"
	   "'d'  deci   1E-01\n"
	   "'c'  centi  1E-02\n"
	   "'m'  milli  1E-03\n"
	   "'u'  micro  1E-06\n"
	   "'n'  nano   1E-09\n"
	   "'p'  pico   1E-12\n"
	   "'f'  femto  1E-15\n"
	   "'a'  atto   1E-18\n"
	   "\n"
	   "If @from_unit and @to_unit are different types, CONVERT returns "
	   "#NUM! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "CONVERT(3,\"lbm\",\"g\") equals 1360.7769.\n"
	   "CONVERT(5.8,\"m\",\"in\") equals 228.3465.\n"
	   "CONVERT(7.9,\"cal\",\"J\") equals 33.07567.\n"
	   "\n"
	   "@SEEALSO=")
};

typedef struct {
        const char *str;
	gnum_float c;
} eng_convert_unit_t;


static gnum_float
get_constant_of_unit(const eng_convert_unit_t units[],
		     const eng_convert_unit_t prefixes[],
		     const char *unit_name,
		     gnum_float *c, gnum_float *prefix)
{
        int i;

	*prefix = 1;
	for (i = 0; units[i].str != NULL; i++)
	        if (strcmp (unit_name, units[i].str) == 0) {
		        *c = units[i].c;
			return 1;
		}

	if (prefixes != NULL)
	        for (i = 0; prefixes[i].str != NULL; i++)
		        if (strncmp (unit_name, prefixes[i].str, 1) == 0) {
			        *prefix = prefixes[i].c;
				unit_name++;
				break;
			}

	for (i = 0; units[i].str != NULL; i++)
	        if (strcmp (unit_name, units[i].str) == 0) {
		        *c = units[i].c;
			return 1;
		}

	return 0;
}

static Value *
convert (const eng_convert_unit_t units[],
	 const eng_convert_unit_t prefixes[],
	 const char *from_unit, const char *to_unit,
	 gnum_float n, Value **v, const EvalPos *ep)
{
        gnum_float from_c, from_prefix, to_c, to_prefix;

	if (get_constant_of_unit (units, prefixes, from_unit, &from_c,
				  &from_prefix)) {

	        if (!get_constant_of_unit (units, prefixes,
					   to_unit, &to_c, &to_prefix))
			return value_new_error (ep, gnumeric_err_NUM);

	        if (from_c == 0 || to_prefix == 0)
	                return value_new_error (ep, gnumeric_err_NUM);

		*v = value_new_float (((n * from_prefix) / from_c) *
				 to_c / to_prefix);
		return *v;
	}

	return NULL;
}

static Value *
gnumeric_convert (FunctionEvalInfo *ei, Value **argv)
{
        /* Weight and mass constants */
        #define one_g_to_sg     0.00006852205001
	#define one_g_to_lbm    0.002204622915
	#define one_g_to_u      6.02217e+23
        #define one_g_to_ozm    0.035273972

	/* Distance constants */
	#define one_m_to_mi     (one_m_to_yd / 1760)
	#define one_m_to_Nmi    (1 / GNUM_const (1852.0))
	#define one_m_to_in     (10000 / GNUM_const (254.0))
	#define one_m_to_ft     (one_m_to_in / 12)
	#define one_m_to_yd     (one_m_to_ft / 3)
	#define one_m_to_ang    10000000000.0
	#define one_m_to_Pica   2834.645669

	/* Time constants */
	#define one_yr_to_day   365.25
	#define one_yr_to_hr    (24 * one_yr_to_day)
	#define one_yr_to_mn    (60 * one_yr_to_hr)
	#define one_yr_to_sec   (60 * one_yr_to_mn)

	/* Pressure constants */
	#define one_Pa_to_atm   0.9869233e-5
	#define one_Pa_to_mmHg  0.00750061708

	/* Force constants */
	#define one_N_to_dyn    100000
	#define one_N_to_lbf    0.224808924

	/* Energy constants */
	#define one_J_to_e      9999995.193
	#define one_J_to_c      0.239006249
	#define one_J_to_cal    0.238846191
	#define one_J_to_eV     6.2146e+18
	#define one_J_to_HPh    3.72506e-7
	#define one_J_to_Wh     0.000277778
	#define one_J_to_flb    23.73042222
	#define one_J_to_BTU    0.000947815

	/* Power constants */
	#define one_HP_to_W     745.701

	/* Magnetism constants */
	#define one_T_to_ga     10000

	/* Temperature constants */
	#define C_K_offset      273.15

	/* Liquid measure constants */
	#define one_tsp_to_tbs  (GNUM_const (1.0) / 3)
	#define one_tsp_to_oz   (GNUM_const (1.0) / 6)
	#define one_tsp_to_cup  0.020833333
	#define one_tsp_to_pt   0.010416667
	#define one_tsp_to_qt   0.005208333
	#define one_tsp_to_gal  0.001302083
	#define one_tsp_to_l    0.004929994

	/* Prefixes */
	#define exa    GNUM_const (1e+18)
	#define peta   GNUM_const (1e+15)
	#define tera   GNUM_const (1e+12)
	#define giga   GNUM_const (1e+09)
	#define mega   GNUM_const (1e+06)
	#define kilo   GNUM_const (1e+03)
	#define hecto  GNUM_const (1e+02)
	#define dekao  GNUM_const (1e+01)
	#define deci   GNUM_const (1e-01)
	#define centi  GNUM_const (1e-02)
	#define milli  GNUM_const (1e-03)
	#define micro  GNUM_const (1e-06)
	#define nano   GNUM_const (1e-09)
	#define pico   GNUM_const (1e-12)
	#define femto  GNUM_const (1e-15)
	#define atto   GNUM_const (1e-18)

	static const eng_convert_unit_t weight_units[] = {
	        { "g",    1.0 },
		{ "sg",   one_g_to_sg },
		{ "lbm",  one_g_to_lbm },
		{ "u",    one_g_to_u },
		{ "ozm",  one_g_to_ozm },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t distance_units[] = {
	        { "m",    1.0 },
		{ "mi",   one_m_to_mi },
		{ "Nmi",  one_m_to_Nmi },
		{ "in",   one_m_to_in },
		{ "ft",   one_m_to_ft },
		{ "yd",   one_m_to_yd },
		{ "ang",  one_m_to_ang },
		{ "Pica", one_m_to_Pica },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t time_units[] = {
	        { "yr",   1.0 },
		{ "day",  one_yr_to_day },
		{ "hr",   one_yr_to_hr },
		{ "mn",   one_yr_to_mn },
		{ "sec",  one_yr_to_sec },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t pressure_units[] = {
	        { "Pa",   1.0 },
		{ "atm",  one_Pa_to_atm },
		{ "mmHg", one_Pa_to_mmHg },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t force_units[] = {
	        { "N",    1.0 },
		{ "dyn",  one_N_to_dyn },
		{ "lbf",  one_N_to_lbf },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t energy_units[] = {
	        { "J",    1.0 },
		{ "e",    one_J_to_e },
		{ "c",    one_J_to_c },
		{ "cal",  one_J_to_cal },
		{ "eV",   one_J_to_eV },
		{ "HPh",  one_J_to_HPh },
		{ "Wh",   one_J_to_Wh },
		{ "flb",  one_J_to_flb },
		{ "BTU",  one_J_to_BTU },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t power_units[] = {
	        { "HP",   1.0 },
		{ "W",    one_HP_to_W },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t magnetism_units[] = {
	        { "T",    1.0 },
		{ "ga",   one_T_to_ga },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t liquid_units[] = {
	        { "tsp",  1.0 },
		{ "tbs",  one_tsp_to_tbs },
		{ "oz",   one_tsp_to_oz },
		{ "cup",  one_tsp_to_cup },
		{ "pt",   one_tsp_to_pt },
		{ "qt",   one_tsp_to_qt },
		{ "gal",  one_tsp_to_gal },
		{ "l",    one_tsp_to_l },
		{ NULL,   0.0 }
	};

	static const eng_convert_unit_t prefixes[] = {
	        { "E", exa },
	        { "P", peta },
	        { "T", tera },
	        { "G", giga },
	        { "M", mega },
	        { "k", kilo },
	        { "h", hecto },
	        { "e", dekao },
	        { "d", deci },
	        { "c", centi },
	        { "m", milli },
	        { "u", micro },
	        { "n", nano },
	        { "p", pico },
	        { "f", femto },
	        { "a", atto },
		{ NULL,0.0 }
	};

	gnum_float n;
	const char *from_unit, *to_unit;
	Value *v;

	n = value_get_as_float (argv[0]);
	from_unit = value_peek_string (argv[1]);
	to_unit = value_peek_string (argv[2]);

	if (strcmp (from_unit, "C") == 0 && strcmp (to_unit, "F") == 0)
	        return value_new_float (n * 9 / 5 + 32);
	else if (strcmp (from_unit, "F") == 0 && strcmp (to_unit, "C") == 0)
	        return value_new_float ((n - 32) * 5 / 9);
	else if (strcmp (from_unit, "F") == 0 && strcmp (to_unit, "F") == 0)
	        return value_new_float (n);
	else if (strcmp (from_unit, "F") == 0 && strcmp (to_unit, "K") == 0)
	        return value_new_float ((n - 32) * 5 / 9 + C_K_offset);
	else if (strcmp (from_unit, "K") == 0 && strcmp (to_unit, "F") == 0)
	        return value_new_float ((n - C_K_offset) * 9 / 5 + 32);
	else if (strcmp (from_unit, "C") == 0 && strcmp (to_unit, "K") == 0)
	        return value_new_float (n + C_K_offset);
	else if (strcmp (from_unit, "K") == 0 && strcmp (to_unit, "C") == 0)
	        return value_new_float (n - C_K_offset);

	if (convert (weight_units, prefixes, from_unit, to_unit, n, &v,
		    ei->pos))
	        return v;
	if (convert (distance_units, prefixes, from_unit, to_unit, n, &v,
		    ei->pos))
	        return v;
	if (convert (time_units, NULL, from_unit, to_unit, n, &v,
		    ei->pos))
	        return v;
	if (convert (pressure_units, prefixes, from_unit, to_unit, n, &v,
		    ei->pos))
	        return v;
	if (convert (force_units, prefixes, from_unit, to_unit, n, &v,
		    ei->pos))
	        return v;
	if (convert (energy_units, prefixes, from_unit, to_unit, n, &v,
		    ei->pos))
	        return v;
	if (convert (power_units, prefixes, from_unit, to_unit, n, &v,
		    ei->pos))
	        return v;
	if (convert (magnetism_units, prefixes, from_unit, to_unit, n, &v,
		    ei->pos))
	        return v;
	if (convert (liquid_units, prefixes, from_unit, to_unit, n, &v,
		    ei->pos))
	        return v;
	if (convert (magnetism_units, prefixes, from_unit, to_unit, n, &v,
		    ei->pos))
	        return v;

	return value_new_error (ei->pos, gnumeric_err_NUM);
}

/***************************************************************************/

static const char *help_erf = {
	N_("@FUNCTION=ERF\n"
	   "@SYNTAX=ERF([lower limit,]upper_limit)\n"

	   "@DESCRIPTION="
	   "With a single argument ERF returns the error function, defined as "
	   "erf(x) = 2/sqrt(pi)* integral from 0 to x of exp(-t*t) dt. "
	   "If two arguments are supplied, they are the lower and upper "
	   "limits of the integral."
	   "\n"
	   "If either @lower_limit or @upper_limit is not numeric a "
	   "#VALUE! error is returned.\n"
	   "This function is upward-compatible with that in Excel. "
	   "(If two arguments are supplied, "
	   "Excel will not allow either to be negative.) "
	   "\n"
	   "@EXAMPLES=\n"
	   "ERF(0.4) equals 0.428392355.\n"
	   "ERF(1.6448536269515/SQRT(2)) equals 0.90.\n"
	   "\n"
	   "The second example shows that a random variable with a normal "
	   "distribution has a 90 percent chance of falling within "
	   "approximately 1.645 standard deviations of the mean."
	   "\n"
	   "@SEEALSO=ERFC")
};


static Value *
gnumeric_erf (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float ans, lower, upper;

	lower = value_get_as_float (argv[0]);
	ans = erfgnum (lower);

	if (argv[1]) {
		upper = value_get_as_float (argv[1]);
		ans = erfgnum (upper) - ans;
	}

	return value_new_float (ans);
}

/***************************************************************************/

static const char *help_erfc = {
	N_("@FUNCTION=ERFC\n"
	   "@SYNTAX=ERFC(x)\n"

	   "@DESCRIPTION="
	   "ERFC function returns the complementary "
	   "error function, defined as 1 - erf(x). "
	   "erfc(x) is calculated more accurately than 1 - erf(x) for "
	   "arguments larger than about 0.5."
	   "\n"
	   "If @x is not numeric a #VALUE! error is returned.  "
	   "\n"
	   "@EXAMPLES=\n"
	   "ERFC(6) equals 2.15197367e-17.\n"
	   "\n"
	   "@SEEALSO=ERF")
};

static Value *
gnumeric_erfc (FunctionEvalInfo *ei, Value **argv)
{
	gnum_float x;

	x = value_get_as_float (argv[0]);

	return value_new_float (erfcgnum (x));
}

/***************************************************************************/

static const char *help_delta = {
	N_("@FUNCTION=DELTA\n"
	   "@SYNTAX=DELTA(x[,y])\n"

	   "@DESCRIPTION="
	   "DELTA function tests for numerical equivalence of two "
	   "arguments, returning 1 in case of equality.  "
	   "@y is optional, and defaults to 0."
	   "\n"
	   "If either argument is non-numeric returns a #VALUE! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "DELTA(42.99,43) equals 0.\n"
	   "\n"
	   "@SEEALSO=EXACT,GESTEP")
};


static Value *
gnumeric_delta (FunctionEvalInfo *ei, Value **argv)
{
	Value *err = NULL;
	gboolean ans = FALSE;
	Value *vx, *vy;

	vx = argv[0];
	if (argv[1])
		vy = argv[1];
	else
		vy = value_new_int (0);

	/* Promote to the largest value */
	switch ((vx->type > vy->type) ? vx->type : vy->type) {
	case VALUE_BOOLEAN:
		/* Only happens when both are bool */
		ans = vx->v_bool.val == vy->v_bool.val;
		break;
	case VALUE_EMPTY:
	case VALUE_INTEGER:
		ans = value_get_as_int (vx) == value_get_as_int (vy);
		break;
	case VALUE_FLOAT:
		ans = value_get_as_float (vx) == value_get_as_float (vy);
		break;
	default:
		err = value_new_error (ei->pos, _("Impossible"));
	}
	if (!argv[1])
		value_release (vy);

	return (err != NULL) ? err : value_new_int (ans ? 1 : 0);
}

/***************************************************************************/

static const char *help_gestep = {
	N_("@FUNCTION=GESTEP\n"
	   "@SYNTAX=GESTEP(x[,y])\n"
	   "@DESCRIPTION="
	   "GESTEP function test for if @x is >= @y, returning 1 if it "
	   "is so, and 0 otherwise. @y is optional, and defaults to 0."
	   "\n"
	   "If either argument is non-numeric returns a #VALUE! error.\n"
	   "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "GESTEP(5,4) equals 1.\n"
	   "\n"
	   "@SEEALSO=DELTA")
};


static Value *
gnumeric_gestep (FunctionEvalInfo *ei, Value **argv)
{
	Value *err = NULL;
	gboolean ans = FALSE;
	Value *vx, *vy;

	vx = argv[0];
	if (argv[1])
		vy = argv[1];
	else
		vy = value_new_int (0);

	/* Promote to the largest value */
	switch ((vx->type > vy->type) ? vx->type : vy->type) {
	case VALUE_BOOLEAN:
		/* Only happens when both are bool */
		ans = vx->v_bool.val >= vy->v_bool.val;
		break;
	case VALUE_EMPTY:
	case VALUE_INTEGER:
		ans = value_get_as_int (vx) >= value_get_as_int (vy);
		break;
	case VALUE_FLOAT:
		ans = value_get_as_float (vx) >= value_get_as_float (vy);
		break;
	default:
		err = value_new_error (ei->pos, _("Impossible"));
	}

	if (!argv[1])
		value_release (vy);
	return (err != NULL) ? err : value_new_int (ans ? 1 : 0);
}

/***************************************************************************/

void eng_functions_init (void);
void
eng_functions_init (void)
{
	FunctionCategory *cat = function_get_category_with_translation ("Engineering", _("Engineering"));

	function_add_args  (
		cat, "besseli",    "ff",   "xnum,ynum",
		&help_besseli, gnumeric_besseli);
	function_add_args  (
		cat, "besselk",     "ff",   "xnum,ynum",
		&help_besselk, gnumeric_besselk);
	function_add_args  (
		cat, "besselj",     "ff",   "xnum,ynum",
		&help_besselj, gnumeric_besselj);
	function_add_args  (
		cat, "bessely",     "ff",   "xnum,ynum",
		&help_bessely, gnumeric_bessely);
	function_add_args  (
		cat, "bin2dec",     "?",    "number",
		&help_bin2dec, gnumeric_bin2dec);
	function_add_args  (
		cat, "bin2hex",     "?|f",  "xnum,ynum",
		&help_bin2hex, gnumeric_bin2hex);
	function_add_args  (
		cat, "bin2oct",     "?|f",  "xnum,ynum",
		&help_bin2oct, gnumeric_bin2oct);
	function_add_args  (
		cat, "complex",     "ff|s", "real,im[,suffix]",
		&help_complex, gnumeric_complex);
	function_add_args  (
		cat, "convert",     "fss",  "number,from_unit,to_unit",
		&help_convert, gnumeric_convert);
	function_add_args  (
		cat, "dec2bin",     "?|f",  "xnum,ynum",
		&help_dec2bin, gnumeric_dec2bin);
	function_add_args  (
		cat, "dec2oct",     "?|f",  "xnum,ynum",
		&help_dec2oct, gnumeric_dec2oct);
	function_add_args  (
		cat, "dec2hex",     "?|f",  "xnum,ynum",
		&help_dec2hex, gnumeric_dec2hex);
	function_add_args  (
		cat, "delta",       "f|f",  "xnum,ynum",
		&help_delta, gnumeric_delta);
	function_add_args  (
		cat, "erf",         "f|f",  "lower,upper",
		&help_erf, gnumeric_erf );
	function_add_args  (
		cat, "erfc",        "f",    "number",
		&help_erfc, gnumeric_erfc);
	function_add_args  (
		cat, "gestep",      "f|f",  "xnum,ynum",
		&help_gestep, gnumeric_gestep);
	function_add_args  (
		cat, "hex2bin",     "?|f",  "xnum,ynum",
		&help_hex2bin, gnumeric_hex2bin);
	function_add_args  (
		cat, "hex2dec",     "?",    "number",
		&help_hex2dec, gnumeric_hex2dec);
	function_add_args  (
		cat, "hex2oct",     "?|f",  "xnum,ynum",
		&help_hex2oct, gnumeric_hex2oct);
	function_add_args  (
		cat, "imabs",       "?",  "inumber",
		&help_imabs, gnumeric_imabs);
	function_add_args  (
		cat, "imaginary",   "?",  "inumber",
		&help_imaginary, gnumeric_imaginary);
	function_add_args  (
		cat, "imargument",  "?",  "inumber",
		&help_imargument, gnumeric_imargument);
	function_add_args  (
		cat, "imconjugate", "?",  "inumber",
		&help_imconjugate, gnumeric_imconjugate);
	function_add_args  (
		cat, "imcos",       "?",  "inumber",
		&help_imcos, gnumeric_imcos);
	function_add_args  (
		cat, "imtan",       "?",  "inumber",
		&help_imtan, gnumeric_imtan);
	function_add_args  (
		cat, "imdiv",       "??", "inumber,inumber",
		&help_imdiv, gnumeric_imdiv);
	function_add_args  (
		cat, "imexp",       "?",  "inumber",
		&help_imexp, gnumeric_imexp);
	function_add_args  (
		cat, "imln",        "?",  "inumber",
		&help_imln, gnumeric_imln);
	function_add_args  (
		cat, "imlog10",     "?",  "inumber",
		&help_imlog10, gnumeric_imlog10);
	function_add_args  (
		cat, "imlog2",      "?",  "inumber",
		&help_imlog2, gnumeric_imlog2);
	function_add_args  (
		cat, "impower",     "??", "inumber,inumber",
		&help_impower, gnumeric_impower);
	function_add_nodes (
		cat, "improduct", NULL, "inumber,inumber",
		&help_improduct, gnumeric_improduct);
	function_add_args  (
		cat, "imreal",      "?",  "inumber",
		&help_imreal, gnumeric_imreal);
	function_add_args  (
		cat, "imsin",       "?",  "inumber",
		&help_imsin, gnumeric_imsin);
	function_add_args  (
		cat, "imsqrt",      "?",  "inumber",
		&help_imsqrt, gnumeric_imsqrt);
	function_add_args  (
		cat, "imsub",       "??", "inumber,inumber",
		&help_imsub, gnumeric_imsub);
	function_add_nodes (
		cat, "imsum",       NULL, "inumber,inumber",
		&help_imsum, gnumeric_imsum);
	function_add_args  (
		cat, "oct2bin",     "?|f",  "xnum,ynum",
		&help_oct2bin, gnumeric_oct2bin);
	function_add_args  (
		cat, "oct2dec",     "?",    "number",
		&help_oct2dec, gnumeric_oct2dec);
	function_add_args  (
		cat, "oct2hex",     "?|f",  "xnum,ynum",
		&help_oct2hex, gnumeric_oct2hex);
}
