/*
 * fn-eng.c:  Built in engineering functions and functions registration
 *
 * Author:
 *  Michael Meeks <michael@imaginator.com>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */
#include <config.h>
#include <gnome.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include "complex.h"
#include "gnumeric.h"
#include "utils.h"
#include "func.h"

#if 0
/* help template */
static char *help_ = {
	N_("@FUNCTION=NAME\n"
	   "@SYNTAX=(b1, b2, ...)\n"

	   "@DESCRIPTION"
	   ""
	   "\n"

	   ""
	   ""
	   "\n"
	   
	   ""
	   ""
	   ""
	   ""
	   "@SEEALSO=")
};

#endif

/**
 * FIXME: In the long term this needs optimising.
 **/
static Value *
val_to_base (FunctionEvalInfo *ei, Value **argv, int num_argv,
	     int src_base, int dest_base)
{
	Value *value, *val_places;
	int lp, max, bit, neg, places;
	char *p, *ans;
	char *err="\0", buffer[40], *str;
	double v;
	
	value = argv[0];
	if (num_argv > 1)
		val_places = argv[1];
	else
		val_places = NULL;

	if (src_base<=1 || dest_base<=1)
		return function_error (ei, _("Base error"));

	if (val_places) {
		if (val_places->type != VALUE_INTEGER &&
		    val_places->type != VALUE_FLOAT)
			return function_error (ei, gnumeric_err_VALUE);

		places = value_get_as_int (val_places);
	} else
		places = 0;

/*	printf ("Type: %d\n", value->type); */
	switch (value->type){
	case VALUE_STRING:
		str = value->v.str->str;
		break;
	case VALUE_INTEGER:
		snprintf (buffer, sizeof (buffer)-1, "%d", value->v.v_int);
		str = buffer;
		break;
	case VALUE_FLOAT:
		snprintf (buffer, sizeof (buffer)-1, "%8.0f", value->v.v_float);
		str = buffer;
		break;
	default:
		return function_error (ei, gnumeric_err_NUM);
	}

	v = strtol (str, &err, src_base);
	if (*err)
		return function_error (ei, gnumeric_err_NUM);

	if (v >= (pow (src_base, 10)/2.0)) /* N's complement */
		v = -v;

	if (dest_base == 10)
		return value_new_int (v);

	if (v<0){
		neg = 1;
		v = -v;
	}
	else
		neg = 0;
	
	if (neg) /* Pad the number */
		max = 10;
	else {
		if (v==0)
			max = 1;
		else
			max = (int)(log(v)/log(dest_base)) + 1;
	}

	if (places>max)
		max = places;
	if (max > 15)
		return function_error (ei, _("Unimplemented"));

	ans = buffer;
	p = &ans[max-1];
	for (lp = 0; lp < max; lp++){
		bit = ((int)v) % dest_base;
		v   = fabs (v / (double)dest_base);
		if (neg)
			bit = dest_base-bit-1;
		if (bit>=0 && bit <= 9)
			*p-- = '0'+bit;
		else
			*p-- = 'A'+bit-10;

		if (places>0 && lp>=places){
			if (v == 0)
				break;
			else
				return function_error (ei, gnumeric_err_NUM);
		}
	}
	ans[max] = '\0';

	return value_new_string (ans);
}

static char *help_bin2dec = {
	N_("@FUNCTION=BIN2DEC\n"
	   "@SYNTAX=BIN2DEC(x)\n"

	   "@DESCRIPTION="
	   "The BIN2DEC function converts a binary number "
	   "in string or number to its decimal equivalent."
	   "\n"
	   "\n"
	   "@SEEALSO=DEC2BIN")
};

static Value *
gnumeric_bin2dec (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 1, 2, 10);
}

static char *help_bin2oct = {
	N_("@FUNCTION=BIN2OCT\n"
	   "@SYNTAX=BIN2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "The BIN2OCT function converts a binary number to an octal number."
	   "places is an optional field, specifying to zero pad to that "
	   "number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=OCT2BIN")
};

static Value *
gnumeric_bin2oct (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 2, 8);
}

static char *help_bin2hex = {
	N_("@FUNCTION=BIN2HEX\n"
	   "@SYNTAX=BIN2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "The BIN2HEX function converts a binary number to a "
	   "hexadecimal number.  @places is an optional field, specifying "
	   "to zero pad to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=HEX2BIN")
};

static Value *
gnumeric_bin2hex (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 2, 16);
}

static char *help_dec2bin = {
	N_("@FUNCTION=DEC2BIN\n"
	   "@SYNTAX=DEC2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "The DEC2BIN function converts a binary number to an octal number."
	   "places is an optional field, specifying to zero pad to that "
	   "number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=BIN2DEC")
};

static Value *
gnumeric_dec2bin (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 10, 2);
}

static char *help_dec2oct = {
	N_("@FUNCTION=DEC2OCT\n"
	   "@SYNTAX=DEC2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "The DEC2OCT function converts a binary number to an octal number."
	   "places is an optional field, specifying to zero pad to that "
	   "number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=OCT2DEC")
};

static Value *
gnumeric_dec2oct (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 10, 8);
}

static char *help_dec2hex = {
	N_("@FUNCTION=DEC2HEX\n"
	   "@SYNTAX=DEC2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "The DEC2HEX function converts a binary number to an octal number."
	   "places is an optional field, specifying to zero pad to that "
	   "number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=HEX2DEC")
};

static Value *
gnumeric_dec2hex (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 10, 16);
}

static char *help_oct2dec = {
	N_("@FUNCTION=OCT2DEC\n"
	   "@SYNTAX=OCT2DEC(x)\n"

	   "@DESCRIPTION="
	   "The OCT2DEC function converts an octal number "
	   "in a string or number to its decimal equivalent."
	   "\n"
	   "\n"
	   "@SEEALSO=DEC2OCT")
};

static Value *
gnumeric_oct2dec (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 1, 8, 10);
}

static char *help_oct2bin = {
	N_("@FUNCTION=OCT2BIN\n"
	   "@SYNTAX=OCT2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "The OCT2BIN function converts a binary number to a hexadecimal "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=BIN2OCT")
};

static Value *
gnumeric_oct2bin (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 8, 2);
}

static char *help_oct2hex = {
	N_("@FUNCTION=OCT2HEX\n"
	   "@SYNTAX=OCT2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "The OCT2HEX function converts a binary number to a hexadecimal "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=OCT2HEX")
};

static Value *
gnumeric_oct2hex (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 8, 16);
}

static char *help_hex2bin = {
	N_("@FUNCTION=HEX2BIN\n"
	   "@SYNTAX=HEX2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "The HEX2BIN function converts a binary number to a hexadecimal "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=BIN2HEX")
};

static Value *
gnumeric_hex2bin (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 16, 2);
}

static char *help_hex2oct = {
	N_("@FUNCTION=HEX2OCT\n"
	   "@SYNTAX=HEX2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "The HEX2OCT function converts a binary number to a hexadecimal "
	   "number.  @places is an optional field, specifying to zero pad "
	   "to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=BIN2HEX")
};

static Value *
gnumeric_hex2oct (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 2, 16, 8);
}

static char *help_hex2dec = {
	N_("@FUNCTION=HEX2DEC\n"
	   "@SYNTAX=HEX2DEC(x)\n"

	   "@DESCRIPTION="
	   "The HEX2DEC function converts a binary number "
	   "in string or number to its decimal equivalent."
	   "\n"
	   "\n"
	   "@SEEALSO=DEC2HEX")
};

static Value *
gnumeric_hex2dec (FunctionEvalInfo *ei, Value **argv)
{
	return val_to_base (ei, argv, 1, 16, 10);
}

static char *help_besselj = {
	N_("@FUNCTION=BESSELJ\n"
	   "@SYNTAX=BESSELJ(x,y)\n"

	   "@DESCRIPTION="
	   "The BESSELJ function returns the bessel function with "
	   "x is where the function is evaluated. "
	   "y is the order of the bessel function, if non-integer it is "
	   "truncated."
	   "\n"

	   "if x or n are not numeric a #VALUE! error is returned."
	   "if n < 0 a #NUM! error is returned." 
	   "\n"
	   "@SEEALSO=BESSELJ,BESSELK,BESSELY")
};

static Value *
gnumeric_besselj (FunctionEvalInfo *ei, Value **argv)
{
	int y;
	if (argv[0]->type != VALUE_INTEGER &&
	    argv[1]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT &&
	    argv[1]->type != VALUE_FLOAT)
		return function_error (ei, gnumeric_err_VALUE);

	if ((y=value_get_as_int(argv[1]))<0)
		return function_error (ei, gnumeric_err_NUM);

	return value_new_float (jn (y, value_get_as_float (argv [0])));
}

static char *help_bessely = {
	N_("@FUNCTION=BESSELY\n"
	   "@SYNTAX=BESSELY(x,y)\n"

	   "@DESCRIPTION="
	   "The BESSELY function returns the Neumann, Weber or Bessel "
	   "function. "
	   "@x is where the function is evaluated. "
	   "@y is the order of the bessel function, if non-integer it is "
	   "truncated."
	   "\n"

	   "if x or n are not numeric a #VALUE! error is returned."
	   "if n < 0 a #NUM! error is returned." 
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
		return function_error (ei, gnumeric_err_VALUE);

	if ((y=value_get_as_int(argv[1]))<0)
		return function_error (ei, gnumeric_err_NUM);

	return value_new_float (yn (y, value_get_as_float (argv[0])));
}

/* Converts a complex number string into its coefficients.  Returns 0 if ok,
 * 1 if an error occured.
 */
static int
value_get_as_complex (Value *val, complex_t *res, char *imunit)
{
	if (VALUE_IS_NUMBER (val)) {
		complex_real (res, value_get_as_float (val));
		*imunit = 'i';
		return 0;
	} else {
		char *s;
		int err;

		s = value_get_as_string (val);
		err = complex_from_string (res, s, imunit);
		g_free (s);
		return err;
	}
}

static Value *
value_new_complex (const complex_t *c, char imunit)
{
	if (complex_real_p (c))
		return value_new_float (c->re);
	else {
		char *s, f[5 + 4 * sizeof (int)];
		Value *res;
		sprintf (f, "%%.%dg", DBL_DIG);
		s = complex_to_string (c, f, f, imunit);
		res = value_new_string (s);
		g_free (s);
		return res;
	}
}

static char *help_complex = {
	N_("@FUNCTION=COMPLEX\n"
	   "@SYNTAX=COMPLEX(real,im[,suffix])\n"

	   "@DESCRIPTION="
	   "COMPLEX returns a complex number of the form x + yi. "
	   "@real is the real and @im is the imaginary coefficient of "
	   "the complex number.  @suffix is the suffix for the imaginary "
	   "coefficient.  If it is omitted, COMPLEX uses 'i' by default."
	   "\n"
	   "If @suffix is neither 'i' nor 'j', COMPLEX returns #VALUE! "
	   "error. "
	   "@SEEALSO=")
};

static Value *
gnumeric_complex (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c;
	char *suffix;

	complex_init (&c,
		      value_get_as_float (argv[0]),
		      value_get_as_float (argv[1]));

	if (argv[2] == NULL)
	        suffix = "i";
	else
	        suffix = argv[2]->v.str->str;

	if (strcmp(suffix, "i") != 0 &&
	    strcmp(suffix, "j") != 0)
		return function_error (ei, gnumeric_err_VALUE);

	return value_new_complex (&c, *suffix);
}

static char *help_imaginary = {
	N_("@FUNCTION=IMAGINARY\n"
	   "@SYNTAX=IMAGINARY(inumber)\n"
	   "@DESCRIPTION="
	   "IMAGINARY returns the imaginary coefficient of a complex "
	   "number."
	   "\n"
	   "@SEEALSO=IMREAL")
};

static Value *
gnumeric_imaginary (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c;
	char imunit;

	if (VALUE_IS_NUMBER(argv[0]))
	        return value_new_float (0.0);

	if (value_get_as_complex (argv[0], &c, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	return value_new_float (c.im);
}

static char *help_imreal = {
	N_("@FUNCTION=IMREAL\n"
	   "@SYNTAX=IMREAL(inumber)\n"
	   "@DESCRIPTION="
	   "IMREAL returns the real coefficient of a complex number."
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
		return function_error (ei, gnumeric_err_VALUE);

	return value_new_float (c.re);
}

static char *help_imabs = {
	N_("@FUNCTION=IMABS\n"
	   "@SYNTAX=IMABS(inumber)\n"
	   "@DESCRIPTION="
	   "IMABS returns the absolute value of a complex number."
	   "\n"
	   "@SEEALSO=IMAGINARY,IMREAL")
};


static Value *
gnumeric_imabs (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t c;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
	  return function_error (ei, gnumeric_err_VALUE);

	if (argv[0]->type != VALUE_STRING)
		return function_error (ei, gnumeric_err_VALUE);

	return value_new_float (complex_mod (&c));
}

static char *help_imconjugate = {
	N_("@FUNCTION=IMCONJUGATE\n"
	   "@SYNTAX=IMCONJUGATE(inumber)\n"
	   "@DESCRIPTION="
	   "IMCONJUGATE returns the complex conjugate of a complex number."
	   "\n"
	   "@SEEALSO=IMAGINARY,IMREAL")
};

static Value *
gnumeric_imconjugate (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	if (argv[0]->type != VALUE_STRING)
		return function_error (ei, gnumeric_err_VALUE);

	complex_conj (&res, &c);
	return value_new_complex (&res, imunit);
}

static char *help_imcos = {
	N_("@FUNCTION=IMCOS\n"
	   "@SYNTAX=IMCOS(inumber)\n"
	   "@DESCRIPTION="
	   "IMCOS returns the cosine of a complex number."
	   "\n"
	   "@SEEALSO=IMSIN")
};

static Value *
gnumeric_imcos (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	complex_cos (&res, &c);
	return value_new_complex (&res, imunit);
}

static char *help_imtan = {
	N_("@FUNCTION=IMTAN\n"
	   "@SYNTAX=IMTAN(inumber)\n"
	   "@DESCRIPTION="
	   "IMCOS returns the tangent of a complex number."
	   "\n"
	   "@SEEALSO=IMTAN")
};

static Value *
gnumeric_imtan (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	complex_tan (&res, &c);
	return value_new_complex (&res, imunit);
}

static char *help_imexp = {
	N_("@FUNCTION=IMEXP\n"
	   "@SYNTAX=IMEXP(inumber)\n"
	   "@DESCRIPTION="
	   "IMEXP returns the exponential of a complex number."
	   "\n"
	   "@SEEALSO=IMLN")
};

static Value *
gnumeric_imexp (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	complex_exp (&res, &c);
	return value_new_complex (&res, imunit);
}

static char *help_imargument = {
	N_("@FUNCTION=IMARGUMENT\n"
	   "@SYNTAX=IMARGUMENT(inumber)\n"
	   "@DESCRIPTION="
	   "IMARGUMENT returns the argument theta of a complex number."
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_imargument (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t c;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	return value_new_float (complex_angle (&c));
}

static char *help_imln = {
	N_("@FUNCTION=IMLN\n"
	   "@SYNTAX=IMLN(inumber)\n"
	   "@DESCRIPTION="
	   "IMLN returns the natural logarithm of a complex number. (The result "
	   "will have an imaginary part between -pi an +pi.  The natural "
	   "logarithm is not uniquely defined on complex numbers.  You may need "
	   "to add or subtract an even multiple of pi to the imaginary part.)"
	   "\n"
	   "@SEEALSO=IMEXP")
};

static Value *
gnumeric_imln (FunctionEvalInfo *ei, Value **argv)
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	complex_ln (&res, &c);
	return value_new_complex (&res, imunit);
}

static char *help_imlog2 = {
	N_("@FUNCTION=IMLOG2\n"
	   "@SYNTAX=IMLOG2(inumber)\n"
	   "@DESCRIPTION="
	   "IMLOG2 returns the logarithm of a complex number in base 2."
	   "\n"
	   "@SEEALSO=IMLN,IMLOG10")
};

static Value *
gnumeric_imlog2 (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	complex_ln (&res, &c);
	res.re /= M_LN2;
	res.im /= M_LN2;
	return value_new_complex (&res, imunit);
}

static char *help_imlog10 = {
	N_("@FUNCTION=IMLOG10\n"
	   "@SYNTAX=IMLOG10(inumber)\n"
	   "@DESCRIPTION="
	   "IMLOG10 returns the logarithm of a complex number in base 10."
	   "\n"
	   "@SEEALSO=IMLN,IMLOG2")
};

static Value *
gnumeric_imlog10 (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	complex_ln (&res, &c);
	res.re /= M_LN10;
	res.im /= M_LN10;
	return value_new_complex (&res, imunit);
}

static char *help_impower = {
	N_("@FUNCTION=IMPOWER\n"
	   "@SYNTAX=IMPOWER(inumber,number)\n"
	   "@DESCRIPTION="
	   "IMPOWER returns a complex number raised to a power.  @inumber is "
	   "the complex number to be raised to a power and @number is the "
	   "power to which you want to raise the complex number."
	   "\n"
	   "@SEEALSO=IMEXP,IMLN")
};

static Value *
gnumeric_impower (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	if (complex_real_p (&a) && a.re <= 0  && !complex_real_p (&b))
		return function_error (ei, gnumeric_err_DIV0);

	complex_pow (&res, &a, &b);
	return value_new_complex (&res, imunit);
}

static char *help_imdiv = {
	N_("@FUNCTION=IMDIV\n"
	   "@SYNTAX=IMDIV(inumber,inumber)\n"
	   "@DESCRIPTION="
	   "IMDIV returns the quotient of two complex numbers."
	   "\n"
	   "@SEEALSO=IMPRODUCT")
};

static Value *
gnumeric_imdiv (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	if (complex_zero_p (&b))
		return function_error (ei, gnumeric_err_DIV0);

	complex_div (&res, &a, &b);
	return value_new_complex (&res, imunit);
}

static char *help_imsin = {
	N_("@FUNCTION=IMSIN\n"
	   "@SYNTAX=IMSIN(inumber)\n"
	   "@DESCRIPTION="
	   "IMSIN returns the sine of a complex number."
	   "\n"
	   "@SEEALSO=IMCOS")
};

static Value *
gnumeric_imsin (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	complex_sin (&res, &c);
	return value_new_complex (&res, imunit);
}

static char *help_imsqrt = {
	N_("@FUNCTION=IMSQRT\n"
	   "@SYNTAX=IMSQRT(inumber)\n"
	   "@DESCRIPTION="
	   "IMSQRT returns the square root of a complex number."
	   "\n"
	   "@SEEALSO=IMEXP")
};

static Value *
gnumeric_imsqrt (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t c, res;
	char imunit;

	if (value_get_as_complex (argv[0], &c, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	complex_sqrt (&res, &c);
	return value_new_complex (&res, imunit);
}

static char *help_imsub = {
	N_("@FUNCTION=IMSUB\n"
	   "@SYNTAX=IMSUB(inumber,inumber)\n"
	   "@DESCRIPTION="
	   "IMSUB returns the difference of two complex numbers."
	   "\n"
	   "@SEEALSO=IMSUM")
};

static Value *
gnumeric_imsub (FunctionEvalInfo *ei, Value **argv) 
{
	complex_t a, b, res;
	char imunit;

	if (value_get_as_complex (argv[0], &a, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	if (value_get_as_complex (argv[1], &b, &imunit))
		return function_error (ei, gnumeric_err_VALUE);

	complex_sub (&res, &a, &b);
	return value_new_complex (&res, imunit);
}

static char *help_improduct = {
	N_("@FUNCTION=IMPRODUCT\n"
	   "@SYNTAX=IMPRODUCT(inumber1[,inumber2,...])\n"
	   "@DESCRIPTION="
	   "IMPRODUCT returns the product of given complex numbers."
	   "\n"
	   "@SEEALSO=IMDIV")
};


typedef enum {
        Improduct, Imsum
} eng_imoper_type_t;

typedef struct {
	complex_t         res;
        char              imunit;;
        eng_imoper_type_t type;
} eng_imoper_t;

static int
callback_function_imoper (const EvalPosition *ep, Value *value,
			  ErrorMessage *error, void *closure)
{
        eng_imoper_t *result = closure;
	complex_t c;
	char *imptr, dummy;

	imptr = VALUE_IS_NUMBER (value) ? &dummy : &result->imunit;
	if (value_get_as_complex (value, &c, imptr)) {
		error_message_set (error, gnumeric_err_VALUE);
		return FALSE;
	}

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

        return TRUE;
}

static Value *
gnumeric_improduct (FunctionEvalInfo *ei, GList *expr_node_list)
{
        eng_imoper_t p;

	p.type = Improduct;
	p.imunit = 'j';
	complex_real (&p.res, 1);

        if (function_iterate_argument_values (&ei->pos, callback_function_imoper,
                                              &p, expr_node_list,
                                              ei->error, TRUE) == FALSE) {
		/* Handler or iterator sets error_string.  */
                return NULL;
        }

	return value_new_complex (&p.res, p.imunit);
}

static char *help_imsum = {
	N_("@FUNCTION=IMSUM\n"
	   "@SYNTAX=IMSUM(inumber,inumber)\n"
	   "@DESCRIPTION="
	   "IMSUM returns the sum of two complex numbers."
	   "\n"
	   "@SEEALSO=IMSUB")
};

static Value *
gnumeric_imsum (FunctionEvalInfo *ei, GList *expr_node_list) 
{
        eng_imoper_t p;

	p.type = Imsum;
	p.imunit = 'j';
	complex_real (&p.res, 0);

        if (function_iterate_argument_values (&ei->pos, callback_function_imoper,
                                              &p, expr_node_list,
                                              ei->error, TRUE) == FALSE) {
		/* Handler or iterator sets error_string.  */
                return NULL;
        }

	return value_new_complex (&p.res, p.imunit);
}

static char *help_convert = {
	N_("@FUNCTION=CONVERT\n"
	   "@SYNTAX=CONVERT(number,from_unit,to_unit)\n"
	   "@DESCRIPTION="
	   "CONVERT returns a conversion from one measurement system to "
	   "another.  For example, you can convert a weight in pounds "
	   "to a weight in grams.  @number is the value you want to "
	   "convert, @from_unit specifies the unit of the number, and "
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
	   "#NUM! error. "
	   "@SEEALSO=")
};

typedef struct {
        char          *str;
         float_t c;
} eng_convert_unit_t;


static float_t
get_constant_of_unit(eng_convert_unit_t units[], 
		     eng_convert_unit_t prefixes[],
		     char *unit_name, float_t *c, float_t *prefix)
{
        int     i;

	*prefix = 1;
	for (i=0; units[i].str != NULL; i++)
	        if (strcmp(unit_name, units[i].str) == 0) {
		        *c = units[i].c;
			return 1;
		}

	if (prefixes != NULL)
	        for (i=0; prefixes[i].str != NULL; i++)
		        if (strncmp(unit_name, prefixes[i].str, 1) == 0) {
			        *prefix = prefixes[i].c;
				unit_name++;
				break;
			}
	
	for (i=0; units[i].str != NULL; i++)
	        if (strcmp(unit_name, units[i].str) == 0) {
		        *c = units[i].c;
			return 1;
		}

	return 0;
}

static int
convert(eng_convert_unit_t units[],
	eng_convert_unit_t prefixes[],
	char *from_unit, char *to_unit,
	float_t n, Value **v, ErrorMessage *error)
{
        float_t from_c, from_prefix, to_c, to_prefix;

	if (get_constant_of_unit(units, prefixes, from_unit, &from_c,
				 &from_prefix)) {
	        if (!get_constant_of_unit(units, prefixes,
					 to_unit, &to_c, &to_prefix)) {
			error_message_set (error, gnumeric_err_NUM);
			*v = NULL;
			return 1;
		}
		*v = value_new_float (((n*from_prefix) / from_c) * 
				 to_c / to_prefix);
		return 1;
	}

	return 0;
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
	#define one_m_to_mi     0.0006213711922
	#define one_m_to_Nmi    0.0005399568035
	#define one_m_to_in     39.37007874
	#define one_m_to_ft     3.280839895
	#define one_m_to_yd     1.093613298
	#define one_m_to_ang    10000000000.0
	#define one_m_to_Pica   2834.645669

	/* Time constants */
	#define one_yr_to_day   365.25
	#define one_yr_to_hr    8766
	#define one_yr_to_mn    525960
	#define one_yr_to_sec   31557600

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
	#define one_C_to_K      274.15

	/* Liquid measure constants */
	#define one_tsp_to_tbs  0.33333333333
	#define one_tsp_to_oz   0.166666667
	#define one_tsp_to_cup  0.020833333
	#define one_tsp_to_pt   0.010416667
	#define one_tsp_to_qt   0.005208333
	#define one_tsp_to_gal  0.001302083
	#define one_tsp_to_l    0.004929994

	/* Prefixes */
	#define exa    1e+18
	#define peta   1e+15
	#define tera   1e+12
	#define giga   1e+09
	#define mega   1e+06
	#define kilo   1e+03
	#define hecto  1e+02
	#define dekao  1e+01
	#define deci   1e-01
	#define centi  1e-02
	#define milli  1e-03
	#define micro  1e-06
	#define nano   1e-09
	#define pico   1e-12
	#define femto  1e-15
	#define atto   1e-18

	static eng_convert_unit_t weight_units[] = {
	        { "g",    1.0 },
		{ "sg",   one_g_to_sg, },
		{ "lbm",  one_g_to_lbm },
		{ "u",    one_g_to_u },
		{ "ozm",  one_g_to_ozm },
		{ NULL,   0.0 }
	};

	static eng_convert_unit_t distance_units[] = {
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

	static eng_convert_unit_t time_units[] = {
	        { "yr",   1.0 },
		{ "day",  one_yr_to_day },
		{ "hr",   one_yr_to_hr },
		{ "mn",   one_yr_to_mn },
		{ "sec",  one_yr_to_sec },
		{ NULL,   0.0 }
	};

	static eng_convert_unit_t pressure_units[] = {
	        { "Pa",   1.0 },
		{ "atm",  one_Pa_to_atm },
		{ "mmHg", one_Pa_to_mmHg },
		{ NULL,   0.0 }
	};

	static eng_convert_unit_t force_units[] = {
	        { "N",    1.0 },
		{ "dyn",  one_N_to_dyn },
		{ "lbf",  one_N_to_lbf },
		{ NULL,   0.0 }
	};

	static eng_convert_unit_t energy_units[] = {
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

	static eng_convert_unit_t power_units[] = {
	        { "HP",   1.0 },
		{ "W",    one_HP_to_W },
		{ NULL,   0.0 }
	};
	
	static eng_convert_unit_t magnetism_units[] = {
	        { "T",    1.0 },
		{ "ga",   one_T_to_ga },
		{ NULL,   0.0 }
	};

	static eng_convert_unit_t liquid_units[] = {
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

	static eng_convert_unit_t prefixes[] = {
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

	float_t n;
	char    *from_unit, *to_unit;
	Value   *v;

	n = value_get_as_float (argv[0]);
	from_unit = argv[1]->v.str->str;
	to_unit = argv[2]->v.str->str;

	if (strcmp(from_unit, "C") == 0 && strcmp(to_unit, "F") == 0)
	        return value_new_float (1.8*n+32);
	else if (strcmp(from_unit, "F") == 0 && strcmp(to_unit, "C") == 0)
	        return value_new_float ((n-32)/1.8);
	else if (strcmp(from_unit, "F") == 0 && strcmp(to_unit, "F") == 0)
	        return value_new_float (n);
	else if (strcmp(from_unit, "F") == 0 && strcmp(to_unit, "K") == 0)
	        return value_new_float ((n-32)/1.8 + one_C_to_K-1);
	else if (strcmp(from_unit, "K") == 0 && strcmp(to_unit, "F") == 0)
	        return value_new_float (1.8*(n-one_C_to_K+1)+32);
	else if (strcmp(from_unit, "C") == 0 && strcmp(to_unit, "K") == 0)
	        return value_new_float (n + one_C_to_K-1);
	else if (strcmp(from_unit, "K") == 0 && strcmp(to_unit, "C") == 0)
	        return value_new_float (n - one_C_to_K+1);

	if (convert(weight_units, prefixes, from_unit, to_unit, n, &v,
		    ei->error))
	        return v;
	if (convert(distance_units, prefixes, from_unit, to_unit, n, &v,
		    ei->error))
	        return v;
	if (convert(time_units, NULL, from_unit, to_unit, n, &v,
		    ei->error))
	        return v;
	if (convert(pressure_units, prefixes, from_unit, to_unit, n, &v,
		    ei->error))
	        return v;
	if (convert(force_units, prefixes, from_unit, to_unit, n, &v,
		    ei->error))
	        return v;
	if (convert(energy_units, prefixes, from_unit, to_unit, n, &v,
		    ei->error))
	        return v;
	if (convert(power_units, prefixes, from_unit, to_unit, n, &v,
		    ei->error))
	        return v;
	if (convert(magnetism_units, prefixes, from_unit, to_unit, n, &v,
		    ei->error))
	        return v;
	if (convert(liquid_units, prefixes, from_unit, to_unit, n, &v,
		    ei->error))
	        return v;
	if (convert(magnetism_units, prefixes, from_unit, to_unit, n, &v,
		    ei->error))
	        return v;

	return function_error (ei, gnumeric_err_NUM);
}

static char *help_erf = {
	N_("@FUNCTION=ERF\n"
	   "@SYNTAX=ERF(lower limit[,upper_limit])\n"

	   "@DESCRIPTION="
	   "The ERF function returns the integral of the error function "
	   "between the limits.  If the upper limit ommitted ERF returns "
	   "the integral between zero and the lower limit."
	   "\n"
	   "If either lower or upper are not numeric a #VALUE! error is "
	   "returned.  "
	   "If either lower or upper are < 0 a #NUM! error is returned."
	   "\n"
	   "@SEEALSO=ERFC")
};


static Value *
gnumeric_erf (FunctionEvalInfo *ei, Value **argv)
{
	float_t ans, lower, upper=0.0;

	lower = value_get_as_float (argv[0]);
	if (argv[1])
		upper = value_get_as_float (argv[1]);

	if (lower < 0.0 || upper < 0.0)
		return function_error (ei, gnumeric_err_NUM);

	ans = erf(lower);

	if (argv[1])
	        ans = erf(upper) - ans;
	
	return value_new_float (ans);
}

static char *help_erfc = {
	N_("@FUNCTION=ERFC\n"
	   "@SYNTAX=ERFC(x)\n"

	   "@DESCRIPTION="
	   "The ERFC function returns the integral of the complimentary "
	   "error function between the limits 0 and x."
	   "\n"

	   "If x is not numeric a #VALUE! error is returned.  "
	   "If x < 0 a #NUM! error is returned."
	   "\n"
	   "@SEEALSO=ERF")
};

static Value *
gnumeric_erfc (FunctionEvalInfo *ei, Value **argv)
{
	float_t x;

	if ((x=value_get_as_float (argv[0]))<0)
		return function_error (ei, gnumeric_err_NUM);

	return value_new_float (erfc (x));
}

static char *help_delta = {
	N_("@FUNCTION=DELTA\n"
	   "@SYNTAX=DELTA(x[,y])\n"

	   "@DESCRIPTION="
	   "The DELTA function test for numerical eqivilance of two "
	   "arguments returning 1 in equality "
	   "y is optional, and defaults to 0."
	   "\n"

	   "If either argument is non-numeric returns a #VALUE! error."
	   "\n"
	   "@SEEALSO=EXACT,GESTEP")
};


static Value *
gnumeric_delta (FunctionEvalInfo *ei, Value **argv)
{
	int ans = 0;
	Value *vx, *vy;

	vx = argv[0];
	if (argv[1])
		vy = argv[1];
	else
		vy = value_new_int (0);

	switch (vx->type)
	{
	case VALUE_INTEGER:
		switch (vy->type)
		{
		case VALUE_INTEGER:
			if (vx->v.v_int == vy->v.v_int)
				ans = 1;
			break;
		case VALUE_FLOAT:
			if (vy->v.v_float == (float_t)vx->v.v_int)
				ans = 1;
			break;
		default:
			return function_error (ei, _("Impossible"));
			return NULL;
		}
		break;
	case VALUE_FLOAT:
		switch (vy->type)
		{
		case VALUE_INTEGER:
			if (vx->v.v_float == (float_t)vy->v.v_int)
				ans = 1;
			break;
		case VALUE_FLOAT:
			if (vy->v.v_float == vx->v.v_float)
				ans = 1;
			break;
		default:
			return function_error (ei, _("Impossible"));
			return NULL;
		}
		break;
	default:
		return function_error (ei, _("Impossible"));
		return NULL;
	}
	       
	if (!argv[1])
		value_release (vy);

	return value_new_int (ans);
}

static char *help_gestep = {
	N_("@FUNCTION=GESTEP\n"
	   "@SYNTAX=GESTEP(x[,y])\n"

	   "@DESCRIPTION="
	   "The GESTEP function test for if x is >= y, returning 1 if it "
	   "is so, and 0 otherwise y is optional, and defaults to 0."
	   "\n"

	   "If either argument is non-numeric returns a #VALUE! error."
	   "\n"
	   "@SEEALSO=DELTA")
};


static Value *
gnumeric_gestep (FunctionEvalInfo *ei, Value **argv)
{
	int ans = 0;
	Value *vx, *vy;

	vx = argv[0];
	if (argv[1])
		vy = argv[1];
	else
		vy = value_new_int (0);

	switch (vx->type)
	{
	case VALUE_INTEGER:
		switch (vy->type)
		{
		case VALUE_INTEGER:
			if (vx->v.v_int >= vy->v.v_int)
				ans = 1;
			break;
		case VALUE_FLOAT:
			if (vy->v.v_float < (float_t)vx->v.v_int)
				ans = 1;
			break;
		default:
			return function_error (ei, _("Impossible"));
			return NULL;
		}
		break;
	case VALUE_FLOAT:
		switch (vy->type)
		{
		case VALUE_INTEGER:
			if (vx->v.v_float >= (float_t)vy->v.v_int)
				ans = 1;
			break;
		case VALUE_FLOAT:
			if (vy->v.v_float < vx->v.v_float)
				ans = 1;
			break;
		default:
			return function_error (ei, _("Impossible"));
			return NULL;
		}
		break;
	default:
		return function_error (ei, _("Impossible"));
		return NULL;
	}
	       
	if (!argv[1])
		value_release (vy);
	return value_new_int (ans);
}

void eng_functions_init()
{
	FunctionCategory *cat = function_get_category (_("Engineering"));

	function_add_args  (cat, "bessely",     "ff",   "xnum,ynum",                &help_bessely,
			   gnumeric_bessely);
	function_add_args  (cat, "besselj",     "ff",   "xnum,ynum",                &help_besselj,
			   gnumeric_besselj);
	function_add_args  (cat, "bin2dec",     "?",    "number",                   &help_bin2dec,
			   gnumeric_bin2dec);
	function_add_args  (cat, "bin2hex",     "?|f",  "xnum,ynum",                &help_bin2hex,
			   gnumeric_bin2hex);
	function_add_args  (cat, "bin2oct",     "?|f",  "xnum,ynum",                &help_bin2oct,
			   gnumeric_bin2oct);
	function_add_args  (cat, "complex",     "ff|s", "real,im[,suffix]",         &help_complex,
			   gnumeric_complex);
	function_add_args  (cat, "convert",     "fss",  "number,from_unit,to_unit", &help_convert,
			   gnumeric_convert);
	function_add_args  (cat, "dec2bin",     "?|f",  "xnum,ynum",                &help_dec2bin,
			   gnumeric_dec2bin);
	function_add_args  (cat, "dec2oct",     "?|f",  "xnum,ynum",                &help_dec2oct,
			   gnumeric_dec2oct);
	function_add_args  (cat, "dec2hex",     "?|f",  "xnum,ynum",                &help_dec2hex,
			   gnumeric_dec2hex);
	function_add_args  (cat, "delta",       "f|f",  "xnum,ynum",                &help_delta,
			   gnumeric_delta);
	function_add_args  (cat, "erf",         "f|f",  "lower,upper",              &help_erf,
			   gnumeric_erf );
	function_add_args  (cat, "erfc",        "f",    "number",                   &help_erfc,
			   gnumeric_erfc);
	function_add_args  (cat, "gestep",      "f|f",  "xnum,ynum",                &help_gestep,
			   gnumeric_gestep);
	function_add_args  (cat, "hex2bin",     "?|f",  "xnum,ynum",                &help_hex2bin,
			   gnumeric_hex2bin);
	function_add_args  (cat, "hex2dec",     "?",    "number",                   &help_hex2dec,
			   gnumeric_hex2dec);
	function_add_args  (cat, "hex2oct",     "?|f",  "xnum,ynum",                &help_hex2oct,
			   gnumeric_hex2oct);
	function_add_args  (cat, "imabs",       "?",  "inumber",                    &help_imabs,
			   gnumeric_imabs);
	function_add_args  (cat, "imaginary",   "?",  "inumber",                    &help_imaginary,
			   gnumeric_imaginary);
	function_add_args  (cat, "imargument",  "?",  "inumber",                    &help_imargument,
			   gnumeric_imargument);
	function_add_args  (cat, "imconjugate", "?",  "inumber",                    &help_imconjugate,
			   gnumeric_imconjugate);
	function_add_args  (cat, "imcos",       "?",  "inumber",                    &help_imcos,
			   gnumeric_imcos);
	function_add_args  (cat, "imdiv",       "??", "inumber,inumber",            &help_imdiv,
			   gnumeric_imdiv);
	function_add_args  (cat, "imexp",       "?",  "inumber",                    &help_imexp,
			   gnumeric_imexp);
	function_add_args  (cat, "imln",        "?",  "inumber",                    &help_imln,
			   gnumeric_imln);
	function_add_args  (cat, "imlog10",     "?",  "inumber",                    &help_imlog10,
			   gnumeric_imlog10);
	function_add_args  (cat, "imlog2",      "?",  "inumber",                    &help_imlog2,
			   gnumeric_imlog2);
	function_add_args  (cat, "impower",     "?f", "inumber,number",             &help_impower,
			   gnumeric_impower);
	function_add_nodes (cat, "improduct",   "??", "inumber,inumber",            &help_improduct,
			   gnumeric_improduct);
	function_add_args  (cat, "imreal",      "?",  "inumber",                    &help_imreal,
			   gnumeric_imreal);
	function_add_args  (cat, "imsin",       "?",  "inumber",                    &help_imsin,
			   gnumeric_imsin);
	function_add_args  (cat, "imsqrt",      "?",  "inumber",                    &help_imsqrt,
			   gnumeric_imsqrt);
	function_add_args  (cat, "imsub",       "??", "inumber,inumber",            &help_imsub,
			   gnumeric_imsub);
	function_add_nodes (cat, "imsum",       "??", "inumber,inumber",            &help_imsum,
			   gnumeric_imsum);
	function_add_args  (cat, "oct2bin",     "?|f",  "xnum,ynum",                &help_oct2bin,
			   gnumeric_oct2bin);
	function_add_args  (cat, "oct2dec",     "?",    "number",                   &help_oct2dec,
			   gnumeric_oct2dec);
	function_add_args  (cat, "oct2hex",     "?|f",  "xnum,ynum",                &help_oct2hex,
			   gnumeric_oct2hex);
}
