/*
 * fn-eng.c:  Built in engineering functions and functions registration
 *
 * Author:
 *  Michael Meeks <michael@imaginator.com>
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "numbers.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
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
val_to_base (Value *value, Value *val_places, int src_base, int dest_base, char **error_string)
{
	int lp, max, bit, neg, places;
	char *p, *ans;
	char *err="\0", buffer[40], *str;
	double v;

	if (src_base<=1 || dest_base<=1){
		*error_string = _("Base error");
		return NULL;
	}

	if (val_places){
		if (val_places->type != VALUE_INTEGER &&
		    val_places->type != VALUE_FLOAT){
			*error_string = _("#VALUE!");
			return NULL;
		}
		places = value_get_as_int (val_places);
	}
	else
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
		*error_string = _("#NUM!");
		return NULL;
	}

	v = strtol (str, &err, src_base);
	if (*err){
		*error_string = _("#NUM!");
		return NULL;
	}

	if (v >= (pow (src_base, 10)/2.0)) /* N's complement */
		v = -v;

	if (dest_base == 10)
		return (value_int(v));

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
	if (max > 15){
		*error_string = _("Unimplemented");
		return NULL;
	}

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
			else {
				*error_string = _("#NUM!");
				return NULL;
			}
		}
	}
	ans[max] = '\0';
	return value_str(ans);
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
gnumeric_bin2dec (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], NULL, 2, 10, error_string);
}

static char *help_bin2oct = {
	N_("@FUNCTION=BIN2OCT\n"
	   "@SYNTAX=BIN2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "The BIN2OCT function converts a binary number to an octal number."
	   "places is an optional field, specifying to zero pad to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=OCT2BIN")
};

static Value *
gnumeric_bin2oct (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 2, 8, error_string);
}

static char *help_bin2hex = {
	N_("@FUNCTION=BIN2HEX\n"
	   "@SYNTAX=BIN2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "The BIN2HEX function converts a binary number to a hexadecimal number."
	   "places is an optional field, specifying to zero pad to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=HEX2BIN")
};

static Value *
gnumeric_bin2hex (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 2, 16, error_string);
}

static char *help_dec2bin = {
	N_("@FUNCTION=DEC2BIN\n"
	   "@SYNTAX=DEC2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "The DEC2BIN function converts a binary number to an octal number."
	   "places is an optional field, specifying to zero pad to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=BIN2DEC")
};

static Value *
gnumeric_dec2bin (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 10, 2, error_string);
}

static char *help_dec2oct = {
	N_("@FUNCTION=DEC2OCT\n"
	   "@SYNTAX=DEC2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "The DEC2OCT function converts a binary number to an octal number."
	   "places is an optional field, specifying to zero pad to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=OCT2DEC")
};

static Value *
gnumeric_dec2oct (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 10, 8, error_string);
}

static char *help_dec2hex = {
	N_("@FUNCTION=DEC2HEX\n"
	   "@SYNTAX=DEC2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "The DEC2HEX function converts a binary number to an octal number."
	   "places is an optional field, specifying to zero pad to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=HEX2DEC")
};

static Value *
gnumeric_dec2hex (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 10, 16, error_string);
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
gnumeric_oct2dec (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], NULL, 8, 10, error_string);
}

static char *help_oct2bin = {
	N_("@FUNCTION=OCT2BIN\n"
	   "@SYNTAX=OCT2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "The OCT2BIN function converts a binary number to a hexadecimal number."
	   "places is an optional field, specifying to zero pad to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=BIN2OCT")
};

static Value *
gnumeric_oct2bin (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 8, 2, error_string);
}

static char *help_oct2hex = {
	N_("@FUNCTION=OCT2HEX\n"
	   "@SYNTAX=OCT2HEX(number[,places])\n"

	   "@DESCRIPTION="
	   "The OCT2HEX function converts a binary number to a hexadecimal number."
	   "places is an optional field, specifying to zero pad to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=OCT2HEX")
};

static Value *
gnumeric_oct2hex (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 8, 16, error_string);
}

static char *help_hex2bin = {
	N_("@FUNCTION=HEX2BIN\n"
	   "@SYNTAX=HEX2BIN(number[,places])\n"

	   "@DESCRIPTION="
	   "The HEX2BIN function converts a binary number to a hexadecimal number."
	   "places is an optional field, specifying to zero pad to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=BIN2HEX")
};

static Value *
gnumeric_hex2bin (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 16, 2, error_string);
}

static char *help_hex2oct = {
	N_("@FUNCTION=HEX2OCT\n"
	   "@SYNTAX=HEX2OCT(number[,places])\n"

	   "@DESCRIPTION="
	   "The HEX2OCT function converts a binary number to a hexadecimal number."
	   "places is an optional field, specifying to zero pad to that number of spaces."
	   "\n"
	   "if places is too small or negative #NUM! error is returned."
	   "\n"
	   "@SEEALSO=BIN2HEX")
};

static Value *
gnumeric_hex2oct (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 16, 8, error_string);
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
gnumeric_hex2dec (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], NULL, 16, 10, error_string);
}

static char *help_besselj = {
	N_("@FUNCTION=BESSELJ\n"
	   "@SYNTAX=BESSELJ(x,y)\n"

	   "@DESCRIPTION="
	   "The BESSELJ function returns the bessel function with "
	   "x is where the function is evaluated. "
	   "y is the order of the bessel function, if non-integer it is truncated. "
	   "\n"

	   "if x or n are not numeric a #VALUE! error is returned."
	   "if n < 0 a #NUM! error is returned." 
	   "\n"
	   "@SEEALSO=BESSELJ,BESSELK,BESSELY")
};

static Value *
gnumeric_besselj (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	int y;
	if (argv[0]->type != VALUE_INTEGER &&
	    argv[1]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT &&
	    argv[1]->type != VALUE_FLOAT){
		*error_string = _("#VALUE!");
		return NULL;
	}
	if ((y=value_get_as_int(argv[1]))<0){
		*error_string = _("#NUM!");
		return NULL;
	}
	return value_float (jn (y, value_get_as_double (argv [0])));
}

static char *help_bessely = {
	N_("@FUNCTION=BESSELY\n"
	   "@SYNTAX=BESSELY(x,y)\n"

	   "@DESCRIPTION="
	   "The BESSELY function returns the Neumann, Weber or Bessel function. "
	   "x is where the function is evaluated. "
	   "y is the order of the bessel function, if non-integer it is truncated. "
	   "\n"

	   "if x or n are not numeric a #VALUE! error is returned."
	   "if n < 0 a #NUM! error is returned." 
	   "\n"
	   "@SEEALSO=BESSELJ,BESSELK,BESSELY")
};

static Value *
gnumeric_bessely (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	int y;
	if (argv[0]->type != VALUE_INTEGER &&
	    argv[1]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT &&
	    argv[1]->type != VALUE_FLOAT){
		*error_string = _("#VALUE!");
		return NULL;
	}
	if ((y=value_get_as_int(argv[1]))<0){
		*error_string = _("#NUM!");
		return NULL;
	}
	return value_float (yn (y, value_get_as_double (argv [0])));
}

static char *help_complex = {
	N_("@FUNCTION=COMPLEX\n"
	   "@SYNTAX=COMPLEX(real,im[,suffix])\n"

	   "@DESCRIPTION="
	   "COMPLEX returns a complex number of the form x + yi. "
	   "@real is the real and @im is the imaginary coefficient of "
	   "the complex number.  @suffix is the suffix for the imaginary "
	   "coefficient.  If it is omitted, COMPLEX uses 'i' by default. "
	   "\n"
	   "If @suffix is neither 'i' nor 'j', COMPLEX returns #VALUE! "
	   "error. "
	   "@SEEALSO=")
};

static Value *
gnumeric_complex (struct FunctionDefinition *fd, 
		  Value *argv [], char **error_string)
{
        float_t     r, i;
	char        *suffix;
	static char buf[256];

	r = value_get_as_double (argv[0]);
	i = value_get_as_double (argv[1]);

	if (argv[2] == NULL)
	        suffix = "i";
	else
	        suffix = argv[2]->v.str->str;

	if (strcmp(suffix, "i") != 0 &&
	    strcmp(suffix, "j") != 0) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	if (i == 0)
	        return value_float (r);

	if (i == 1)
	        if (r == 0)
		        sprintf(buf, "%s", suffix);
		else
		        sprintf(buf, "%g+%s", r, suffix);
	else if (i == -1)
	        if (r == 0)
		        sprintf(buf, "-%s", suffix);
		else
		        sprintf(buf, "%g-%s", r, suffix);
	else if (r == 0)
	        sprintf(buf, "%g%s", i, suffix);
	else  
	        sprintf(buf, "%g%+g%s", r, i, suffix);

	return value_str (buf);
}


static char *help_imaginary = {
	N_("@FUNCTION=IMAGINARY\n"
	   "@SYNTAX=IMAGINARY(inumber)\n"
	   "@DESCRIPTION="
	   "IMAGINARY returns the imaginary coefficient of a complex "
	   "number. "
	   "\n"
	   "@SEEALSO=IMREAL")
};

static int
get_real_and_imaginary(char *inumber, float_t *real, float_t *im)
{
        /* FIXME: exponents */
        char *p;
	char buf[256];
	int  i=0;

	*im = 0;
	for (p=inumber; *p; p++)
	        if (isdigit(*p) || *p=='.')
		        buf[i++] = *p;
		else
		        break;
	buf[i] = '\0';
	*real = atof(buf);
	for (i=0; *p; p++)
	        if (isdigit(*p) || *p=='+' || *p=='-' || *p=='.')
		        buf[i++] = *p;
		else
		        break;
	buf[i] = '\0';
	if (i > 0)
	        *im = atof(buf);

        return 0;
}

static Value *
gnumeric_imaginary (struct FunctionDefinition *fd, 
		    Value *argv [], char **error_string)
{
        float_t real, im;

	if (VALUE_IS_NUMBER(argv[0]))
	        return value_int (0);

	if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	if (get_real_and_imaginary(argv[0]->v.str->str, &real, &im)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	return value_float (im);
}

static char *help_imreal = {
	N_("@FUNCTION=IMAGINARY\n"
	   "@SYNTAX=IMREAL(inumber)\n"
	   "@DESCRIPTION="
	   "IMREAL returns the real coefficient of a complex number. "
	   "\n"
	   "@SEEALSO=IMAGINARY")
};


static Value *
gnumeric_imreal (struct FunctionDefinition *fd, 
		 Value *argv [], char **error_string)
{
        float_t real, im;

	if (VALUE_IS_NUMBER(argv[0]))
	        return value_float (value_get_as_double (argv[0]));

	if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	if (get_real_and_imaginary(argv[0]->v.str->str, &real, &im)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	return value_float (real);
}

static char *help_imabs = {
	N_("@FUNCTION=IMABS\n"
	   "@SYNTAX=IMABS(inumber)\n"
	   "@DESCRIPTION="
	   "IMABS returns the absolute value of a complex number. "
	   "\n"
	   "@SEEALSO=IMAGINARY,IMREAL")
};


static Value *
gnumeric_imabs (struct FunctionDefinition *fd, 
		Value *argv [], char **error_string)
{
        float_t real, im;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_double (argv[0]);
	        return value_float (sqrt(real * real));
	}

	if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	if (get_real_and_imaginary(argv[0]->v.str->str, &real, &im)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	return value_float (sqrt(real*real + im*im));
}

static char *help_erf = {
	N_("@FUNCTION=ERF\n"
	   "@SYNTAX=ERF(lower limit[,upper_limit])\n"

	   "@DESCRIPTION="
	   "The ERF function returns the integral of the error function between the limits. "
	   "If the upper limit ommitted ERF returns the integral between zero and the lower limit"
	   "\n"

	   "if either lower or upper are not numeric a #VALUE! error is returned."
	   "if either lower or upper are < 0 a #NUM! error is returned."
	   "\n"
	   "@SEEALSO=ERFC")
};


static Value *
gnumeric_erf (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t ans, lower, upper=0.0;

	lower = value_get_as_double(argv[0]);
	if (argv[1])
		upper = value_get_as_double(argv[1]);
	
	if (lower < 0.0 || upper < 0.0){
		*error_string = _("#NUM!");
		return NULL;
	}
	       
	ans = erf(lower);
	if (argv[1])
		ans = erf(upper) - ans;
	
	return value_float (ans);
}

static char *help_erfc = {
	N_("@FUNCTION=ERFC\n"
	   "@SYNTAX=ERFC(x)\n"

	   "@DESCRIPTION="
	   "The ERFC function returns the integral of the complimentary error function between the limits 0 and x. "
	   "\n"

	   "if x is not numeric a #VALUE! error is returned."
	   "if x < 0 a #NUM! error is returned."
	   "\n"
	   "@SEEALSO=ERF")
};

static Value *
gnumeric_erfc (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t x;
	if ((x=value_get_as_double(argv[0]))<0){
		*error_string = _("#NUM!");
		return NULL;
	}
	return value_float (erfc (x));
}

static char *help_delta = {
	N_("@FUNCTION=DELTA\n"
	   "@SYNTAX=DELTA(x[,y])\n"

	   "@DESCRIPTION="
	   "The DELTA function test for numerical eqivilance of two arguments returning 1 in equality "
	   "y is optional, and defaults to 0"
	   "\n"

	   "if either argument is non-numeric returns a #VALUE! error"
	   "\n"
	   "@SEEALSO=EXACT,GESTEP")
};


static Value *
gnumeric_delta (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	int ans = 0;
	Value *vx, *vy;

	vx = argv[0];
	if (argv[1])
		vy = argv[1];
	else
		vy = value_int(0);

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
			*error_string = _("Impossible");
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
			*error_string = _("Impossible");
			return NULL;
		}
		break;
	default:
		*error_string = _("Impossible");
		return NULL;
	}
	       
	if (!argv[1])
		value_release (vy);
	return value_int (ans);
}

static char *help_gestep = {
	N_("@FUNCTION=GESTEP\n"
	   "@SYNTAX=GESTEP(x[,y])\n"

	   "@DESCRIPTION="
	   "The GESTEP function test for if x is >= y, returning 1 if it is so, and 0 otherwise "
	   "y is optional, and defaults to 0"
	   "\n"

	   "if either argument is non-numeric returns a #VALUE! error"
	   "\n"
	   "@SEEALSO=DELTA")
};


static Value *
gnumeric_gestep (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	int ans = 0;
	Value *vx, *vy;

	vx = argv[0];
	if (argv[1])
		vy = argv[1];
	else
		vy = value_int (0);

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
			*error_string = _("Impossible");
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
			*error_string = _("Impossible");
			return NULL;
		}
		break;
	default:
		*error_string = _("Impossible");
		return NULL;
	}
	       
	if (!argv[1])
		value_release (vy);
	return value_int (ans);
}

FunctionDefinition eng_functions [] = {
	{ "bessely",   "ff",   "xnum,ynum",   &help_bessely,
	  NULL, gnumeric_bessely },
	{ "besselj",   "ff",   "xnum,ynum",   &help_besselj,
	  NULL, gnumeric_besselj },
	{ "bin2dec",   "?",    "number",      &help_bin2dec,
	  NULL, gnumeric_bin2dec },
	{ "bin2hex",   "?|f",  "xnum,ynum",   &help_bin2hex,
	  NULL, gnumeric_bin2hex },
	{ "bin2oct",   "?|f",  "xnum,ynum",   &help_bin2oct,
	  NULL, gnumeric_bin2oct },
	{ "complex",   "ff|s", "real,im[,suffix]", &help_complex,
	  NULL, gnumeric_complex },
	{ "dec2bin",   "?|f",  "xnum,ynum",   &help_dec2bin,
	  NULL, gnumeric_dec2bin },
	{ "dec2oct",   "?|f",  "xnum,ynum",   &help_dec2oct,
	  NULL, gnumeric_dec2oct },
	{ "dec2hex",   "?|f",  "xnum,ynum",   &help_dec2hex,
	  NULL, gnumeric_dec2hex },
	{ "delta",     "f|f",  "xnum,ynum",   &help_delta,
	  NULL, gnumeric_delta },
	{ "erf",       "f|f",  "lower,upper", &help_erf,
	  NULL, gnumeric_erf  },
	{ "erfc",      "f",    "number",      &help_erfc,
	  NULL, gnumeric_erfc },
	{ "gestep",    "f|f",  "xnum,ynum",   &help_gestep,
	  NULL, gnumeric_gestep },
	{ "hex2bin",   "?|f",  "xnum,ynum",   &help_hex2bin,
	  NULL, gnumeric_hex2bin },
	{ "hex2dec",   "?",    "number",      &help_hex2dec,
	  NULL, gnumeric_hex2dec },
	{ "hex2oct",   "?|f",  "xnum,ynum",   &help_hex2oct,
	  NULL, gnumeric_hex2oct },
	{ "imabs",     "?",  "inumber",       &help_imabs,
	  NULL, gnumeric_imabs },
	{ "imaginary", "?",  "inumber",       &help_imaginary,
	  NULL, gnumeric_imaginary },
	{ "imreal",    "?",  "inumber",       &help_imreal,
	  NULL, gnumeric_imreal },
	{ "oct2bin",   "?|f",  "xnum,ynum",   &help_oct2bin,
	  NULL, gnumeric_oct2bin },
	{ "oct2dec",   "?",    "number",      &help_oct2dec,
	  NULL, gnumeric_oct2dec },
	{ "oct2hex",   "?|f",  "xnum,ynum",   &help_oct2hex,
	  NULL, gnumeric_oct2hex },
	/* besseli */
	/* besselk */
	{ NULL, NULL },
};
