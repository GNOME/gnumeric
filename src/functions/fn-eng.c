/*
 * fn-eng.c:  Built in engineering functions and functions registration
 *
 * Author:
 *  Michael Meeks <michael@imaginator.com>
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

static char *help_bin2dec = {
	N_("@FUNCTION=BIN2DEC\n"
	   "@SYNTAX=BIN2DEC(x)\n"

	   "@DESCRIPTION="
	   "The BIN2DEC function converts a binary number "
	   "in string or number to its decimal equivalent."
	   "\n"

	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=DEC2BIN")
};

static Value *
gnumeric_bin2dec (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	int  result, v, n, bit;
	char *p;

	result = 0;
	switch (argv [0]->type){
	case VALUE_INTEGER:
		v = argv [0]->v.v_int;
		n = 0;
		for (n = 0; v; n++){
			bit = v % 10;
			v   = v / 10;
			result |= bit << n;
		}
		break;
		
	case VALUE_STRING:
		p = argv [0]->v.str->str;
		for (;*p; p++){
			if (!(*p == '0' || *p == '1')){
				*error_string = "#NUM!";
				return NULL;
			}
			result = result << 1 | (*p - '0');
		}
		break;
		
	default:
		*error_string = "#NUM!";
		return NULL;
	}
	return value_int (result);
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
	int y ;
	if (argv[0]->type != VALUE_INTEGER &&
	    argv[1]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT &&
	    argv[1]->type != VALUE_FLOAT) {
		*error_string = _("#VALUE!") ;
		return NULL ;
	}
	if ((y=value_get_as_int(argv[1]))<0) {
		*error_string = _("#NUM!") ;
		return NULL ;
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
	int y ;
	if (argv[0]->type != VALUE_INTEGER &&
	    argv[1]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT &&
	    argv[1]->type != VALUE_FLOAT) {
		*error_string = _("#VALUE!") ;
		return NULL ;
	}
	if ((y=value_get_as_int(argv[1]))<0) {
		*error_string = _("#NUM!") ;
		return NULL ;
	}
	return value_float (yn (y, value_get_as_double (argv [0])));
}

static char *help_erf = {
	N_("@FUNCTION=ERF\n"
	   "@SYNTAX=ERF(lower limit, upper_limit)\n"

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
gnumeric_erf (void *sheet, GList *l, int eval_col, int eval_row, char **error_string)
{
	float_t ans, lower, upper=0.0 ;
	int argc = g_list_length (l) ;
	Value *vlower, *vupper=0 ;

	if (argc < 1 || argc > 2 || !l->data) {
		*error_string = _("Invalid number of arguments") ;
		return NULL ;
	}

	vlower = eval_expr (sheet, l->data, eval_col, eval_row, error_string) ;
	if (vlower->type != VALUE_INTEGER &&
	    vlower->type != VALUE_FLOAT) {
		*error_string = _("#VALUE!") ;
		return NULL ;
	}
	lower = value_get_as_double(vlower) ;
	value_release (vlower) ;
	
	l = g_list_next(l) ;
	if (l && l->data) {
		vupper = eval_expr (sheet, l->data, eval_col, eval_row, error_string) ;
		if (vupper->type != VALUE_INTEGER &&
		    vupper->type != VALUE_FLOAT) {
			*error_string = _("#VALUE!") ;
			return NULL ;
		}
		upper = value_get_as_double(vupper) ;
		value_release (vupper) ;
	}
	
	if (lower < 0.0 || upper < 0.0) {
		*error_string = _("#NUM!") ;
		return NULL ;
	}
	       
	ans = erf(lower) ;
	if (vupper)
		ans = erf(upper) - ans ;
	
	return value_float (ans) ;
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
	float_t x ;
	if (argv[0]->type != VALUE_INTEGER &&
	    argv[0]->type != VALUE_FLOAT) {
		*error_string = _("#VALUE!") ;
		return NULL ;
	}
	if ((x=value_get_as_double(argv[0]))<0) {
		*error_string = _("#NUM!") ;
		return NULL ;
	}
	return value_float (erfc (x)) ;
}

static char *help_delta = {
	N_("@FUNCTION=DELTA\n"
	   "@SYNTAX=DELTA(x,y)\n"

	   "@DESCRIPTION="
	   "The DELTA function test for numerical eqivilance of two arguments returning 1 in equality "
	   "y is optional, and defaults to 0"
	   "\n"

	   "if either argument is non-numeric returns a #VALUE! error"
	   "\n"
	   "@SEEALSO=EXACT,GESTEP")
};


static Value *
gnumeric_delta (void *sheet, GList *l, int eval_col, int eval_row, char **error_string)
{
	int ans = 0 ;
	int argc = g_list_length (l) ;
	Value *vx, *vy ;

	if (argc < 1 || argc > 2 || !l->data) {
		*error_string = _("Invalid number of arguments") ;
		return NULL ;
	}

	vx = eval_expr (sheet, l->data, eval_col, eval_row, error_string) ;
	if (vx->type != VALUE_INTEGER &&
	    vx->type != VALUE_FLOAT) {
		*error_string = _("#VALUE!") ;
		return NULL ;
	}
	
	l = g_list_next(l) ;
	if (l && l->data) {
		vy = eval_expr (sheet, l->data, eval_col, eval_row, error_string) ;
		if (vy->type != VALUE_INTEGER &&
		    vy->type != VALUE_FLOAT) {
			*error_string = _("#VALUE!") ;
			return NULL ;
		}
	}
	else
		vy = value_int (0) ;
	switch (vx->type)
	{
	case VALUE_INTEGER:
		switch (vy->type)
		{
		case VALUE_INTEGER:
			if (vx->v.v_int == vy->v.v_int)
				ans = 1 ;
			break ;
		case VALUE_FLOAT:
			if (vy->v.v_float == (float_t)vx->v.v_int)
				ans = 1 ;
			break ;
		default:
			*error_string = _("Impossible") ;
			return NULL ;
		}
		break ;
	case VALUE_FLOAT:
		switch (vy->type)
		{
		case VALUE_INTEGER:
			if (vx->v.v_float == (float_t)vy->v.v_int)
				ans = 1 ;
			break ;
		case VALUE_FLOAT:
			if (vy->v.v_float == vx->v.v_float)
				ans = 1 ;
			break ;
		default:
			*error_string = _("Impossible") ;
			return NULL ;
		}
		break ;
	default:
		*error_string = _("Impossible") ;
		return NULL ;
	}
	       
	value_release (vx) ;
	value_release (vy) ;
	return value_int (ans) ;
}

FunctionDefinition eng_functions [] = {
	{ "bessely",   "ff",   "xnum,ynum",   &help_bessely, NULL, gnumeric_bessely },
	{ "besselj",   "ff",   "xnum,ynum",   &help_besselj, NULL, gnumeric_besselj },
	{ "bin2dec",   "?",    "number",      &help_bin2dec, NULL, gnumeric_bin2dec },
	{ "delta",     0,      "xnum,ynum",   &help_delta,   gnumeric_delta, NULL },
	{ "erf",       0,      "lower,upper", &help_erf,     gnumeric_erf, NULL },
	{ "erfc",      "f",    "number",      &help_erfc,    NULL, gnumeric_erfc },
	/* besseli */
	/* besselk */
	/* bessely */
	{ NULL, NULL },
};


/*
 * Mode, Median: Use large hash table :-)
 *
 * Engineering: Bessel functions: use C fns: j0, y0 etc.
 */
