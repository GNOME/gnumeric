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
val_to_base (Value *value, Value *val_places,
	     int src_base, int dest_base, char **error_string)
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
		return (value_new_int (v));

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
gnumeric_bin2dec (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	return val_to_base (argv[0], NULL, 2, 10, error_string);
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
gnumeric_bin2oct (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 2, 8, error_string);
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
gnumeric_bin2hex (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 2, 16, error_string);
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
gnumeric_dec2bin (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 10, 2, error_string);
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
gnumeric_dec2oct (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 10, 8, error_string);
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
gnumeric_dec2hex (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
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
gnumeric_oct2dec (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	return val_to_base (argv[0], NULL, 8, 10, error_string);
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
gnumeric_oct2bin (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 8, 2, error_string);
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
gnumeric_oct2hex (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 8, 16, error_string);
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
gnumeric_hex2bin (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	return val_to_base (argv[0], argv[1], 16, 2, error_string);
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
gnumeric_hex2oct (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
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
gnumeric_hex2dec (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	return val_to_base (argv[0], NULL, 16, 10, error_string);
}

static char *help_besselj = {
	N_("@FUNCTION=BESSELJ\n"
	   "@SYNTAX=BESSELJ(x,y)\n"

	   "@DESCRIPTION="
	   "The BESSELJ function returns the bessel function with "
	   "x is where the function is evaluated. "
	   "y is the order of the bessel function, if non-integer it is "
	   "truncated. "
	   "\n"

	   "if x or n are not numeric a #VALUE! error is returned."
	   "if n < 0 a #NUM! error is returned." 
	   "\n"
	   "@SEEALSO=BESSELJ,BESSELK,BESSELY")
};

static Value *
gnumeric_besselj (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
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
	   "truncated. "
	   "\n"

	   "if x or n are not numeric a #VALUE! error is returned."
	   "if n < 0 a #NUM! error is returned." 
	   "\n"
	   "@SEEALSO=BESSELJ,BESSELK,BESSELY")
};

static Value *
gnumeric_bessely (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
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
	return value_new_float (yn (y, value_get_as_float (argv [0])));
}


static char *i_suffix = "i";
static char *j_suffix = "j";


/* Returns 1 if 'i' or '+i', -1 if '-i', and 0 otherwise.
 */
static int
is_unit_imaginary(char *inumber, char **suffix)
{
        int im = 0;

	/* Check if only 'i' or '-i' */
	if (strcmp(inumber, "i") == 0 || strcmp(inumber, "+i") == 0) {
	        im = 1;
		*suffix = i_suffix;
	}
	if (strcmp(inumber, "-i") == 0) {
	        im = -1;
		*suffix = i_suffix;
	}
	if (strcmp(inumber, "j") == 0 || strcmp(inumber, "+j") == 0) {
	        im = 1;
		*suffix = j_suffix;
	}
	if (strcmp(inumber, "-j") == 0) {
	        im = -1;
		*suffix = j_suffix;
	}        

	return im;
}

/* Converts a complex number string into its coefficients.  Returns 0 if ok,
 * 1 if an error occured.
 */
static int
get_real_and_imaginary(char *inumber, float_t *real, float_t *im, 
		       char **suffix)
{
        char *p;

	*real = 0;

	*im = is_unit_imaginary(inumber, suffix);
	if (*im)
	        return 0;

	/* Get the real coefficient */
	*real = strtod(inumber, &p);
	if (inumber == p)
	        return 1;

	/* Check if only imaginary coefficient */
	if (*p == 'i') {
	        if (*(p+1) == '\0') {
		        *im = *real;
			*real = 0;
			*suffix = i_suffix;
			return 0;
		} else
		        return 1;
	}
	if (*p == 'j') {
	        if (*(p+1) == '\0') {
		        *im = *real;
			*real = 0;
			*suffix = j_suffix;
			return 0;
		} else
		        return 1;
	}

	/* Get the imaginary coefficient */
	*im = is_unit_imaginary(p, suffix);
	if (*im)
	        return 0;

	inumber = p;
	*im = strtod(inumber, &p);
	if (inumber == p)
	        return 1;

	if (*p == 'i') {
	        if (*(p+1) == '\0') {
			*suffix = i_suffix;
			return 0;
		} else
		        return 1;
	}
	if (*p == 'j') {
	        if (*(p+1) == '\0') {
			*suffix = j_suffix;
			return 0;
		} else
		        return 1;
	}

        return 1;
}

static Value*
create_inumber (float_t real, float_t im, char *suffix)
{
	static char buf[256];

	if (im == 0)
	        return value_new_float (real);

	if (suffix == NULL)
	        suffix = "i";

	if (im == 1)
	        if (real == 0)
		        sprintf(buf, "%s", suffix);
		else
		        sprintf(buf, "%g+%s", real, suffix);
	else if (im == -1)
	        if (real == 0)
		        sprintf(buf, "-%s", suffix);
		else
		        sprintf(buf, "%g-%s", real, suffix);
	else if (real == 0)
	        sprintf(buf, "%g%s", im, suffix);
	else  
	        sprintf(buf, "%g%+g%s", real, im, suffix);

	return value_new_string (buf);
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

	r = value_get_as_float (argv[0]);
	i = value_get_as_float (argv[1]);

	if (argv[2] == NULL)
	        suffix = "i";
	else
	        suffix = argv[2]->v.str->str;

	if (strcmp(suffix, "i") != 0 &&
	    strcmp(suffix, "j") != 0) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	return create_inumber (r, i, suffix);
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

static Value *
gnumeric_imaginary (struct FunctionDefinition *fd, 
		    Value *argv [], char **error_string)
{
        float_t real, im;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0]))
	        return value_new_int (0);

	if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	if (get_real_and_imaginary(argv[0]->v.str->str, &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	return value_new_float (im);
}

static char *help_imreal = {
	N_("@FUNCTION=IMREAL\n"
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
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0]))
	        return value_new_float (value_get_as_float (argv[0]));

	if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	if (get_real_and_imaginary(argv[0]->v.str->str, &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	return value_new_float (real);
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
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_float (argv[0]);
	        return value_new_float (sqrt(real * real));
	}

	if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	if (get_real_and_imaginary(argv[0]->v.str->str, &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	return value_new_float (sqrt(real*real + im*im));
}

static char *help_imconjugate = {
	N_("@FUNCTION=IMCONJUGATE\n"
	   "@SYNTAX=IMCONJUGATE(inumber)\n"
	   "@DESCRIPTION="
	   "IMCONJUGATE returns the complex conjugate of a complex number. "
	   "\n"
	   "@SEEALSO=IMAGINARY,IMREAL")
};

static Value *
gnumeric_imconjugate (struct FunctionDefinition *fd, 
		      Value *argv [], char **error_string)
{
        float_t real, im;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_float (argv[0]);
	        return value_new_float (real);
	}

	if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	if (get_real_and_imaginary(argv[0]->v.str->str, &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	return create_inumber (real, -im, suffix);
}

static char *help_imcos = {
	N_("@FUNCTION=IMCOS\n"
	   "@SYNTAX=IMCOS(inumber)\n"
	   "@DESCRIPTION="
	   "IMCOS returns the cosine of a complex number. "
	   "\n"
	   "@SEEALSO=IMSIN")
};

static Value *
gnumeric_imcos (struct FunctionDefinition *fd, 
		Value *argv [], char **error_string)
{
        float_t real, im;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_float (argv[0]);
	        im = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	return create_inumber (cos(real)*cosh(im),
			       -sin(real)*sinh(im), suffix);
}

static char *help_imexp = {
	N_("@FUNCTION=IMEXP\n"
	   "@SYNTAX=IMEXP(inumber)\n"
	   "@DESCRIPTION="
	   "IMEXP returns the exponential of a complex number. "
	   "\n"
	   "@SEEALSO=IMLN")
};

static Value *
gnumeric_imexp (struct FunctionDefinition *fd, 
		Value *argv [], char **error_string)
{
        float_t real, im, e;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_float (argv[0]);
	        im = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	e = exp(real);

	return create_inumber (e * cos(im), e * sin(im), suffix);
}

static char *help_imargument = {
	N_("@FUNCTION=IMARGUMENT\n"
	   "@SYNTAX=IMARGUMENT(inumber)\n"
	   "@DESCRIPTION="
	   "IMARGUMENT returns the argument theta of a complex number. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_imargument (struct FunctionDefinition *fd, 
		     Value *argv [], char **error_string)
{
        float_t real, im, theta;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_float (argv[0]);
	        im = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	theta = atan(im / real);

	return value_new_float (theta);
}

static char *help_imln = {
	N_("@FUNCTION=IMLN\n"
	   "@SYNTAX=IMLN(inumber)\n"
	   "@DESCRIPTION="
	   "IMLN returns the natural logarithm of a complex number. "
	   "\n"
	   "@SEEALSO=IMEXP")
};

static void
complex_ln(float_t *real, float_t *im)
{
        float_t r, i;

        r = log(sqrt(*real * *real + *im * *im));
	i = atan(*im / *real);
	*real = r;
	*im = i;
}

static Value *
gnumeric_imln (struct FunctionDefinition *fd, 
	       Value *argv [], char **error_string)
{
        float_t real, im;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_float (argv[0]);
	        im = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	if (real == 0) {
		*error_string = _("#DIV/0!");
		return NULL;
	}
	
	complex_ln(&real, &im);

	return create_inumber (real, im, suffix);
}

static char *help_imlog2 = {
	N_("@FUNCTION=IMLOG2\n"
	   "@SYNTAX=IMLOG2(inumber)\n"
	   "@DESCRIPTION="
	   "IMLOG2 returns the logarithm of a complex number in base 2. "
	   "\n"
	   "@SEEALSO=IMLN,IMLOG10")
};

static Value *
gnumeric_imlog2 (struct FunctionDefinition *fd, 
		 Value *argv [], char **error_string)
{
        float_t real, im;
	float_t ln_2;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_float (argv[0]);
	        im = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	if (real == 0) {
		*error_string = _("#DIV/0!");
		return NULL;
	}
	
	complex_ln(&real, &im);
	ln_2 = log(2);

	return create_inumber (real/ln_2, im/ln_2, suffix);
}

static char *help_imlog10 = {
	N_("@FUNCTION=IMLOG10\n"
	   "@SYNTAX=IMLOG10(inumber)\n"
	   "@DESCRIPTION="
	   "IMLOG10 returns the logarithm of a complex number in base 10. "
	   "\n"
	   "@SEEALSO=IMLN,IMLOG2")
};

static Value *
gnumeric_imlog10 (struct FunctionDefinition *fd, 
		  Value *argv [], char **error_string)
{
        float_t real, im;
	float_t ln_10;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_float (argv[0]);
	        im = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	if (real == 0) {
		*error_string = _("#DIV/0!");
		return NULL;
	}
	
	complex_ln(&real, &im);
	ln_10 = log(10);

	return create_inumber (real/ln_10, im/ln_10, suffix);
}

static char *help_impower = {
	N_("@FUNCTION=IMPOWER\n"
	   "@SYNTAX=IMPOWER(inumber,number)\n"
	   "@DESCRIPTION="
	   "IMPOWER returns a complex number raised to a power.  @inumber is "
	   "the complex number to be raised to a power and @number is the "
	   "power to which you want to raise the complex number. "
	   "\n"
	   "@SEEALSO=IMEXP,IMLN")
};

static Value *
gnumeric_impower (struct FunctionDefinition *fd, 
		  Value *argv [], char **error_string)
{
        float_t real, im, n, r, theta, power;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_float (argv[0]);
	        im = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	n = value_get_as_float (argv[1]);
	
	if (real == 0) {
		*error_string = _("#DIV/0!");
		return NULL;
	}
	
	r = sqrt(real*real + im*im);
	theta = atan(im / real);
	power = pow(r, n);

	return create_inumber (power * cos(n*theta), power * sin(n*theta),
			       suffix);
}

static char *help_imdiv = {
	N_("@FUNCTION=IMDIV\n"
	   "@SYNTAX=IMDIV(inumber,inumber)\n"
	   "@DESCRIPTION="
	   "IMDIV returns the quotient of two complex numbers. "
	   "\n"
	   "@SEEALSO=IMPRODUCT")
};

static Value *
gnumeric_imdiv (struct FunctionDefinition *fd, 
		Value *argv [], char **error_string)
{
        float_t a, b, c, d, den;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        a = value_get_as_float (argv[0]);
	        b = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &a, &b, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	if (VALUE_IS_NUMBER(argv[1])) {
	        c = value_get_as_float (argv[1]);
	        d = 0;
	} else if (argv[1]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[1]->v.str->str,
					  &c, &d, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	den = c*c + d*d;

	if (den == 0) {
		*error_string = _("#DIV/0!");
		return NULL;
	}
	
	return create_inumber ((a*c+b*d) / den, (b*c-a*d) / den, suffix);
}

static char *help_imsin = {
	N_("@FUNCTION=IMSIN\n"
	   "@SYNTAX=IMSIN(inumber)\n"
	   "@DESCRIPTION="
	   "IMSIN returns the sine of a complex number. "
	   "\n"
	   "@SEEALSO=IMCOS")
};

static Value *
gnumeric_imsin (struct FunctionDefinition *fd, 
		Value *argv [], char **error_string)
{
        float_t real, im;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_float (argv[0]);
	        im = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	return create_inumber (sin(real)*cosh(im),
			       -cos(real)*sinh(im), suffix);
}

static char *help_imsqrt = {
	N_("@FUNCTION=IMSQRT\n"
	   "@SYNTAX=IMSQRT(inumber)\n"
	   "@DESCRIPTION="
	   "IMSQRT returns the square root of a complex number. "
	   "\n"
	   "@SEEALSO=IMEXP")
};

static Value *
gnumeric_imsqrt (struct FunctionDefinition *fd, 
		 Value *argv [], char **error_string)
{
        float_t real, im, r, theta;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        real = value_get_as_float (argv[0]);
	        im = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &real, &im, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	r = sqrt(sqrt(real*real + im*im));
	theta = atan(im / real) / 2;

	return create_inumber (r*cos(theta), r*sin(theta), suffix);
}

static char *help_imsub = {
	N_("@FUNCTION=IMSUB\n"
	   "@SYNTAX=IMSUB(inumber,inumber)\n"
	   "@DESCRIPTION="
	   "IMSUB returns the difference of two complex numbers. "
	   "\n"
	   "@SEEALSO=IMSUM")
};

static Value *
gnumeric_imsub (struct FunctionDefinition *fd, 
		Value *argv [], char **error_string)
{
        float_t a, b, c, d;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        a = value_get_as_float (argv[0]);
	        b = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &a, &b, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	if (VALUE_IS_NUMBER(argv[1])) {
	        c = value_get_as_float (argv[1]);
	        d = 0;
	} else if (argv[1]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[1]->v.str->str,
					  &c, &d, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	return create_inumber (a-c, b-d, suffix);
}

static char *help_improduct = {
	N_("@FUNCTION=IMPRODUCT\n"
	   "@SYNTAX=IMPRODUCT(inumber,inumber)\n"
	   "@DESCRIPTION="
	   "IMPRODUCT returns the product of two complex numbers. "
	   "\n"
	   "@SEEALSO=IMDIV")
};

static Value *
gnumeric_improduct (struct FunctionDefinition *fd, 
		    Value *argv [], char **error_string)
{
        float_t a, b, c, d;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        a = value_get_as_float (argv[0]);
	        b = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &a, &b, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	if (VALUE_IS_NUMBER(argv[1])) {
	        c = value_get_as_float (argv[1]);
	        d = 0;
	} else if (argv[1]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[1]->v.str->str,
					  &c, &d, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	return create_inumber (a*c-b*d, a*d+b*c, suffix);
}

static char *help_imsum = {
	N_("@FUNCTION=IMSUM\n"
	   "@SYNTAX=IMSUM(inumber,inumber)\n"
	   "@DESCRIPTION="
	   "IMSUM returns the sum of two complex numbers. "
	   "\n"
	   "@SEEALSO=IMSUB")
};

static Value *
gnumeric_imsum (struct FunctionDefinition *fd, 
		Value *argv [], char **error_string)
{
        float_t a, b, c, d;
	char    *suffix;

	if (VALUE_IS_NUMBER(argv[0])) {
	        a = value_get_as_float (argv[0]);
	        b = 0;
	} else if (argv[0]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[0]->v.str->str,
					  &a, &b, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	if (VALUE_IS_NUMBER(argv[1])) {
	        c = value_get_as_float (argv[1]);
	        d = 0;
	} else if (argv[1]->type != VALUE_STRING) {
		*error_string = _("#VALUE!");
		return NULL;
	} else if (get_real_and_imaginary(argv[1]->v.str->str,
					  &c, &d, &suffix)) {
		*error_string = _("#NUM!");
		return NULL;
	}

	return create_inumber (a+c, b+d, suffix);
}

static char *help_convert = {
	N_("@FUNCTION=CONVERT\n"
	   "@SYNTAX=CONVERT(number,from_unit,to_unit)\n"
	   "@DESCRIPTION="
	   "CONVERT returns a conversion from one measurement system to "
	   "another.  For example, you can convert a weight in pounds "
	   "to a weight in grams.  @number is the value you want to "
	   "convert, @from_unit specifies the unit of the number, and "
	   "@to_unit is the unit for the result. "
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
	float_t n, Value **v, char **error_string)
{
        float_t from_c, from_prefix, to_c, to_prefix;

	if (get_constant_of_unit(units, prefixes, from_unit, &from_c,
				 &from_prefix)) {
	        if (!get_constant_of_unit(units, prefixes,
					 to_unit, &to_c, &to_prefix)) {
		        *error_string = _("#NUM!");
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
gnumeric_convert (struct FunctionDefinition *fd, 
		  Value *argv [], char **error_string)
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
		    error_string))
	        return v;
	if (convert(distance_units, prefixes, from_unit, to_unit, n, &v,
		    error_string))
	        return v;
	if (convert(time_units, NULL, from_unit, to_unit, n, &v,
		    error_string))
	        return v;
	if (convert(pressure_units, prefixes, from_unit, to_unit, n, &v,
		    error_string))
	        return v;
	if (convert(force_units, prefixes, from_unit, to_unit, n, &v,
		    error_string))
	        return v;
	if (convert(energy_units, prefixes, from_unit, to_unit, n, &v,
		    error_string))
	        return v;
	if (convert(power_units, prefixes, from_unit, to_unit, n, &v,
		    error_string))
	        return v;
	if (convert(magnetism_units, prefixes, from_unit, to_unit, n, &v,
		    error_string))
	        return v;
	if (convert(liquid_units, prefixes, from_unit, to_unit, n, &v,
		    error_string))
	        return v;
	if (convert(magnetism_units, prefixes, from_unit, to_unit, n, &v,
		    error_string))
	        return v;

	*error_string = _("#NUM!");
	return NULL;
}

static char *help_erf = {
	N_("@FUNCTION=ERF\n"
	   "@SYNTAX=ERF(lower limit[,upper_limit])\n"

	   "@DESCRIPTION="
	   "The ERF function returns the integral of the error function "
	   "between the limits.  If the upper limit ommitted ERF returns "
	   "the integral between zero and the lower limit"
	   "\n"
	   "if either lower or upper are not numeric a #VALUE! error is "
	   "returned."
	   "if either lower or upper are < 0 a #NUM! error is returned."
	   "\n"
	   "@SEEALSO=ERFC")
};


static Value *
gnumeric_erf (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
	float_t ans, lower, upper=0.0;

	lower = value_get_as_float (argv[0]);
	if (argv[1])
		upper = value_get_as_float (argv[1]);
	
	if (lower < 0.0 || upper < 0.0){
		*error_string = _("#NUM!");
		return NULL;
	}
	       
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
	   "error function between the limits 0 and x. "
	   "\n"

	   "if x is not numeric a #VALUE! error is returned."
	   "if x < 0 a #NUM! error is returned."
	   "\n"
	   "@SEEALSO=ERF")
};

static Value *
gnumeric_erfc (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	float_t x;
	if ((x=value_get_as_float (argv[0]))<0){
		*error_string = _("#NUM!");
		return NULL;
	}
	return value_new_float (erfc (x));
}

static char *help_delta = {
	N_("@FUNCTION=DELTA\n"
	   "@SYNTAX=DELTA(x[,y])\n"

	   "@DESCRIPTION="
	   "The DELTA function test for numerical eqivilance of two "
	   "arguments returning 1 in equality "
	   "y is optional, and defaults to 0"
	   "\n"

	   "if either argument is non-numeric returns a #VALUE! error"
	   "\n"
	   "@SEEALSO=EXACT,GESTEP")
};


static Value *
gnumeric_delta (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
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
	return value_new_int (ans);
}

static char *help_gestep = {
	N_("@FUNCTION=GESTEP\n"
	   "@SYNTAX=GESTEP(x[,y])\n"

	   "@DESCRIPTION="
	   "The GESTEP function test for if x is >= y, returning 1 if it "
	   "is so, and 0 otherwise y is optional, and defaults to 0"
	   "\n"

	   "if either argument is non-numeric returns a #VALUE! error"
	   "\n"
	   "@SEEALSO=DELTA")
};


static Value *
gnumeric_gestep (struct FunctionDefinition *i,
		 Value *argv [], char **error_string)
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
	return value_new_int (ans);
}

FunctionDefinition eng_functions [] = {
	{ "bessely",     "ff",   "xnum,ynum",                &help_bessely,
	  NULL, gnumeric_bessely },
	{ "besselj",     "ff",   "xnum,ynum",                &help_besselj,
	  NULL, gnumeric_besselj },
	{ "bin2dec",     "?",    "number",                   &help_bin2dec,
	  NULL, gnumeric_bin2dec },
	{ "bin2hex",     "?|f",  "xnum,ynum",                &help_bin2hex,
	  NULL, gnumeric_bin2hex },
	{ "bin2oct",     "?|f",  "xnum,ynum",                &help_bin2oct,
	  NULL, gnumeric_bin2oct },
	{ "complex",     "ff|s", "real,im[,suffix]",         &help_complex,
	  NULL, gnumeric_complex },
	{ "convert",     "fss",  "number,from_unit,to_unit", &help_convert,
	  NULL, gnumeric_convert },
	{ "dec2bin",     "?|f",  "xnum,ynum",                &help_dec2bin,
	  NULL, gnumeric_dec2bin },
	{ "dec2oct",     "?|f",  "xnum,ynum",                &help_dec2oct,
	  NULL, gnumeric_dec2oct },
	{ "dec2hex",     "?|f",  "xnum,ynum",                &help_dec2hex,
	  NULL, gnumeric_dec2hex },
	{ "delta",       "f|f",  "xnum,ynum",                &help_delta,
	  NULL, gnumeric_delta },
	{ "erf",         "f|f",  "lower,upper",              &help_erf,
	  NULL, gnumeric_erf  },
	{ "erfc",        "f",    "number",                   &help_erfc,
	  NULL, gnumeric_erfc },
	{ "gestep",      "f|f",  "xnum,ynum",                &help_gestep,
	  NULL, gnumeric_gestep },
	{ "hex2bin",     "?|f",  "xnum,ynum",                &help_hex2bin,
	  NULL, gnumeric_hex2bin },
	{ "hex2dec",     "?",    "number",                   &help_hex2dec,
	  NULL, gnumeric_hex2dec },
	{ "hex2oct",     "?|f",  "xnum,ynum",                &help_hex2oct,
	  NULL, gnumeric_hex2oct },
	{ "imabs",       "?",  "inumber",                    &help_imabs,
	  NULL, gnumeric_imabs },
	{ "imaginary",   "?",  "inumber",                    &help_imaginary,
	  NULL, gnumeric_imaginary },
	{ "imargument",  "?",  "inumber",                    &help_imargument,
	  NULL, gnumeric_imargument },
	{ "imconjugate", "?",  "inumber",                    &help_imconjugate,
	  NULL, gnumeric_imconjugate },
	{ "imcos",       "?",  "inumber",                    &help_imcos,
	  NULL, gnumeric_imcos },
	{ "imdiv",       "??", "inumber,inumber",            &help_imdiv,
	  NULL, gnumeric_imdiv },
	{ "imexp",       "?",  "inumber",                    &help_imexp,
	  NULL, gnumeric_imexp },
	{ "imln",        "?",  "inumber",                    &help_imln,
	  NULL, gnumeric_imln },
	{ "imlog10",     "?",  "inumber",                    &help_imlog10,
	  NULL, gnumeric_imlog10 },
	{ "imlog2",      "?",  "inumber",                    &help_imlog2,
	  NULL, gnumeric_imlog2 },
	{ "impower",     "?f", "inumber,number",             &help_impower,
	  NULL, gnumeric_impower },
	{ "improduct",   "??", "inumber,inumber",            &help_improduct,
	  NULL, gnumeric_improduct },
	{ "imreal",      "?",  "inumber",                    &help_imreal,
	  NULL, gnumeric_imreal },
	{ "imsin",       "?",  "inumber",                    &help_imsin,
	  NULL, gnumeric_imsin },
	{ "imsqrt",      "?",  "inumber",                    &help_imsqrt,
	  NULL, gnumeric_imsqrt },
	{ "imsub",       "??", "inumber,inumber",            &help_imsub,
	  NULL, gnumeric_imsub },
	{ "imsum",       "??", "inumber,inumber",            &help_imsum,
	  NULL, gnumeric_imsum },
	{ "oct2bin",     "?|f",  "xnum,ynum",                &help_oct2bin,
	  NULL, gnumeric_oct2bin },
	{ "oct2dec",     "?",    "number",                   &help_oct2dec,
	  NULL, gnumeric_oct2dec },
	{ "oct2hex",     "?|f",  "xnum,ynum",                &help_oct2hex,
	  NULL, gnumeric_oct2hex },
	/* besseli */
	/* besselk */
	{ NULL, NULL },
};
