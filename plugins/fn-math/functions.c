/*
 * fn-math.c:  Built in mathematical functions and functions registration
 *
 * Authors:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
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

static char *help_abs = {
	N_("@FUNCTION=ABS\n"
	   "@SYNTAX=ABS(b1)\n"

	   "@DESCRIPTION=Implements the Absolute Value function:  the result is "
	   "to drop the negative sign (if present).  This can be done for "
	   "integers and floating point numbers."
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=CEIL, FLOOR")
};

static Value *
gnumeric_abs (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (fabs (value_get_as_double (argv [0])));
}

static char *help_acos = {
	N_("@FUNCTION=ACOS\n"
	   "@SYNTAX=ACOS(x)\n"

	   "@DESCRIPTION="
	   "The ACOS function calculates the arc cosine of x; that "
	   " is the value whose cosine is x.  If x  falls  outside  the "
	   " range -1 to 1, ACOS fails and returns the error 'acos - domain error'. "
	   " The value it returns is in radians. "
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=COS, SIN, DEGREES, RADIANS")
};

static Value *
gnumeric_acos (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_double (argv [0]);
	if ((t < -1.0) || (t > 1.0)){
		*error_string = _("acos - domain error");
		return NULL;
	}
	return value_float (acos (t));
}

static char *help_acosh = {
	N_("@FUNCTION=ACOSH\n"
	   "@SYNTAX=ACOSH(x)\n"

	   "@DESCRIPTION="
	   "The ACOSH  function  calculates  the inverse hyperbolic "
	   "cosine of x; that is the value whose hyperbolic cosine is "
	   "x. If x is less than 1.0, acosh() returns the error "
	   " 'acosh - domain error'"
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=ACOS, DEGREES, RADIANS ")
};

static Value *
gnumeric_acosh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_double (argv [0]);
	if (t < 1.0){
		*error_string = _("acosh - domain error");
		return NULL;
	}
	return value_float (acosh (t));
}

static char *help_and = {
	N_("@FUNCTION=AND\n"
	   "@SYNTAX=AND(b1, b2, ...)\n"

	   "@DESCRIPTION=Implements the logical AND function: the result is TRUE "
	   "if all of the expression evaluates to TRUE, otherwise it returns "
	   "FALSE.\n"

	   "b1, trough bN are expressions that should evaluate to TRUE or FALSE. "
	   "If an integer or floating point value is provided zero is considered "
	   "FALSE and anything else is TRUE.\n"
	   
	   "If the values contain strings or empty cells those values are "
	   "ignored.  If no logical values are provided, then the error '#VALUE!' "
	   "is returned. "
	   "\n"
	   "@SEEALSO=OR, NOT")
};

static int
callback_function_and (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	Value *result = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		if (value->v.v_int == 0){
			result->v.v_int = 0;
			return FALSE;
		} else
			result->v.v_int = 1;
		break;

	case VALUE_FLOAT:
		if (value->v.v_float == 0.0){
			result->v.v_int = 0;
			return FALSE;
		} else
			result->v.v_int = 1;

	default:
		/* ignore strings */
		break;
	}
	
	return TRUE;
}

static Value *
gnumeric_and (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *result;
	Sheet *sheet = (Sheet *) tsheet;

	result = g_new (Value, 1);
	result->type = VALUE_INTEGER;
	result->v.v_int = -1;

	function_iterate_argument_values (sheet, callback_function_and,
					  result, expr_node_list,
					  eval_col, eval_row, error_string);

	/* See if there was any value worth using */
	if (result->v.v_int == -1){
		value_release (result);
		*error_string = _("#VALUE");
		return NULL;
	}
	return result;
}

static char *help_asin = {
	N_("@FUNCTION=ASIN\n"
	   "@SYNTAX=ASIN(x)\n"

	   "@DESCRIPTION="
	   "The ASIN function calculates the arc sine of x; that is "
	   "the value whose sine is x. If x falls outside  the  range "
	   "-1 to 1, ASIN fails and returns the error 'asin - domain error'   "
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=SIN, COS, ASINH, DEGREES, RADIANS")
};

static Value *
gnumeric_asin (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_double (argv [0]);
	if ((t < -1.0) || (t > 1.0)){
		*error_string = _("asin - domain error");
		return NULL;
	}
	return value_float (asin (t));
}

static char *help_asinh = {
	N_("@FUNCTION=ASINH\n"
	   "@SYNTAX=ASINH(x)\n"

	   "@DESCRIPTION="
	   "The ASINH  function  calculates  the inverse hyperbolic "
	   " sine of x; that is the value whose hyperbolic sine is x. "
	   "\n"

	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=ASIN, SIN, COS, DEGREES, RADIANS")
};

static Value *
gnumeric_asinh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (asinh (value_get_as_double (argv [0])));
}

static char *help_atan = {
	N_("@FUNCTION=ATAN\n"
	   "@SYNTAX=ATAN(x)\n"

	   "@DESCRIPTION="
	   "The ATAN function calculates the arc tangent of x; that "
	   " is the value whose tangent is x."
	   "Return value is in radians."
	   "\n"

	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"

	   "@SEEALSO=TAN, COS, SIN, DEGREES, RADIANS")
};

static Value *
gnumeric_atan (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (atan (value_get_as_double (argv [0])));
}

static char *help_atanh = {
	N_("@FUNCTION=ATANH\n"
	   "@SYNTAX=ATANH(x)\n"

	   "@DESCRIPTION="
	   "The  ATANH  function  calculates  the inverse hyperbolic "
	   "tangent of x; that is the value whose  hyperbolic  tangent "
	   "is  x.   If  the  absolute value of x is greater than 1.0, "
	   " ATANH returns an error of 'atanh: domain error'      "
	   "\n"

	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=ATAN, TAN, SIN, COS, DEGREES, RADIANS")
};

static Value *
gnumeric_atanh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_double (argv [0]);
	if ((t <= -1.0) || (t >= 1.0)){
		*error_string = _("atanh: domain error");
		return NULL;
	}
	return value_float (atanh (value_get_as_double (argv [0])));
}

static char *help_atan2 = {
	N_("@FUNCTION=ATAN2\n"
	   "@SYNTAX=ATAN2(b1,b2)\n"

	   "@DESCRIPTION="
	   "The ATAN2 function calculates the arc tangent of the two "
	   "variables b1 and b2.  It is similar to calculating  the  arc "
	   "tangent  of b2 / b1, except that the signs of both arguments "
	   "are used to determine the quadrant of the result. "
	   "The result is in Radians."
	   "\n"

	   "Performing this function on a string or empty cell simply does nothing. "
	   "\n"
	   "@SEEALSO=ATAN, ATANH, COS, SIN, DEGREES, RADIANS")
};

static Value *
gnumeric_atan2 (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (atan2 (value_get_as_double (argv [0]),
				   value_get_as_double (argv [1])));
}

static char *help_ceil = {
	N_("@FUNCTION=CEIL\n"
	   "@SYNTAX=CEIL(x)\n"

	   "@DESCRIPTION=The CEIL function rounds x up to the next nearest "
	   "integer.\n"

	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   
	   "@SEEALSO=ABS, FLOOR, INT")
};

static Value *
gnumeric_ceil (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (ceil (value_get_as_double (argv [0])));
}

static char *help_ceiling = {
	N_("@FUNCTION=CEILING\n"
	   "@SYNTAX=CEILING(x,significance)\n"

	   "@DESCRIPTION=The CEILING function rounds x up to the nearest "
	   "multiple of significance. "
	   "\n"

	   "If x or significance is non-numeric CEILING returns #VALUE! error. "
	   "If n and significance have different signs CEILING returns #NUM! error. "
	   "\n"
	   
	   "@SEEALSO=CEIL")
};

static Value *
gnumeric_ceiling (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        float_t k=1;
	float_t div, mod, ceiled;
        float_t x, significance;
	int     n, sign=1;

	if (!VALUE_IS_NUMBER(argv[0]) ||
	    !VALUE_IS_NUMBER(argv[1])) {
                *error_string = _("#VALUE!") ;
                return NULL;
	}
	x = value_get_as_double(argv[0]);
	significance = value_get_as_double(argv[1]);
	if ((x < 0.0 && significance > 0.0) || 
	    (x > 0.0 && significance < 0.0)) {
                *error_string = _("#NUM!") ;
                return NULL;
	}
	if (significance < 0) {
	        sign=-1;
		x = -x;
		significance = -significance;
	}
	/* Find significance level */
	for (n=0; n<12; n++) {
	        ceiled = ceil (significance * k);
		if (fabs (ceiled - (significance * k)) < significance/2)
		        break;
		k *= 10;
	}
	ceiled *= 10;

	div = ceil ((x * k * 10) / ceiled);
	mod = ((x * k * 10) / ceiled) - div;

	return value_float (sign * ceiled * div / (k*10) -
			    sign * significance * (mod > 0));
}

static char *help_cos = {
	N_("@FUNCTION=COS\n"
	   "@SYNTAX=COS(x)\n"

	   "@DESCRIPTION="
	   "The  COS  function  returns  the cosine of x, where x is "
           "given in radians.  "
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=COSH, SIN, SINH, TAN, TANH, RADIANS, DEGREES")
};

static Value *
gnumeric_cos (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (cos (value_get_as_double (argv [0])));
}

static char *help_cosh = {
	N_("@FUNCTION=COSH\n"
	   "@SYNTAX=COSH(x)\n"

	   "@DESCRIPTION="
	   "The COSH  function  returns the hyperbolic cosine of x, "
	   " which is defined mathematically as (exp(x) + exp(-x)) / 2.   "
	   " x is in radians. "
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=COS, SIN, SINH, TAN, TANH, RADIANS, DEGREES, EXP")
};

static Value *
gnumeric_cosh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (cosh (value_get_as_double (argv [0])));
}

static char *help_degrees = {
	N_("@FUNCTION=DEGREES\n"
	   "@SYNTAX=DEGREES(x)\n"

	   "@DESCRIPTION="
	   "Computes the number of degrees equivalent to "
	   " x radians."
	   "\n"

	   "Performing this function on a string or empty cell simply does nothing. "
	   "\n"
	   
	   "@SEEALSO=RADIANS, PI")
};

static Value *
gnumeric_degrees (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float ((value_get_as_double (argv [0]) * 180.0) / M_PI);
}

static char *help_exp = {
	N_("@FUNCTION=EXP\n"
	   "@SYNTAX=EXP(x)\n"

	   "@DESCRIPTION="
	   "Computes the value of e(the base of natural logarithmns) raised "
	   "to the power of x. "
	   "\n"
	   "Performing this function on a string or empty cell returns an error."
	   "\n"
	   "@SEEALSO=LOG, LOG2, LOG10")
};

static Value *
gnumeric_exp (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (exp (value_get_as_double (argv [0])));
}

float_t
fact (int n)
{
	if (n == 0)
		return 1;
	return (n * fact (n - 1));
}

static char *help_fact = {
	N_("@FUNCTION=FACT\n"
	   "@SYNTAX=FACT(x)\n"

	   "@DESCRIPTION="
	   "Computes the factorial of x. ie, x!"
	   "\n"
	   "Performing this function on a string or empty cell returns an error"
	   "\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_fact (struct FunctionDefinition *id, Value *argv [], char **error_string)
{
	Value *res;
	float i;

	switch (argv [0]->type){
	case VALUE_FLOAT:
		i = argv [0]->v.v_float;
		break;
	case VALUE_INTEGER:
		i = argv [0]->v.v_int;
		break;
	default:
		*error_string = "#NUM!";
		return NULL;
	}

	if (i < 0){
		*error_string = "#NUM!";
		return NULL;
	}
	
	res = g_new (Value, 1);
	if (i > 12){
		res->type = VALUE_FLOAT;
		res->v.v_float = exp (lgamma (i + 1));
	} else {
		res->type = VALUE_INTEGER;
		res->v.v_int = fact ((int)i);
	}
	return res;
}

static char *help_combin = {
	N_("@FUNCTION=COMBIN\n"
	   "@SYNTAX=COMBIN(n,k)\n"

	   "@DESCRIPTION="
	   "Computes the number of combinations."
	   "\n"
	   "Performing this function on a non-integer or a negative number "
           "returns an error. Also if n is less than k returns an error."
	   "\n"
	   "\n"
	   "@SEEALSO=")
};

float_t
combin (int n, int k)
{
	return fact(n) / (fact(k) * fact(n-k));
}

static Value *
gnumeric_combin (struct FunctionDefinition *id, Value *argv [], char **error_string)
{
	Value *res;
	float_t n, k;

	if (argv [0]->type == VALUE_INTEGER &&
	    argv [1]->type == VALUE_INTEGER &&
	    argv[0]->v.v_int >= argv[1]->v.v_int){
		n = argv [0]->v.v_int;
		k = argv [1]->v.v_int;
	} else {
		*error_string = "#NUM!";
		return NULL;
	}
	
	res = g_new (Value, 1);
	res->type = VALUE_INTEGER;
	res->v.v_int = combin ((int)n, (int)k);
	return res;
}

static char *help_floor = {
	N_("@FUNCTION=FLOOR\n"
	   "@SYNTAX=FLOOR(x)\n"

	   "@DESCRIPTION=The FLOOR function rounds x down to the next nearest "
	   "integer."
	   "\n"

	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=CEIL, ABS, INT")
};

static Value *
gnumeric_floor (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (floor (value_get_as_double (argv [0])));
}

static char *help_int = {
	N_("@FUNCTION=INT\n"
	   "@SYNTAX=INT(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "The INT function round b1 now to the nearest int. "
	   "Where 'nearest' implies being closer to zero. "
	   "Equivalent to FLOOR(b1) for b1 >0, amd CEIL(b1) "
	   "for b1 < 0. " 
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   ""
	   "\n"
	   "@SEEALSO=FLOOR, CEIL, ABS")
};

static Value *
gnumeric_int (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_double (argv [0]);
	
	return value_float (t > 0.0 ? floor (t) : ceil (t));
}

static char *help_log = {
	N_("@FUNCTION=LOG\n"
	   "@SYNTAX=LOG(x)\n"

	   "@DESCRIPTION="
	   "Computes the natural logarithm  of x. "
	   "\n"
	   "Performing this function on a string or empty cell returns an error. "
	   "\n"
	   "@SEEALSO=EXP, LOG2, LOG10")
};

static Value *
gnumeric_log (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_double (argv [0]);
	if (t <= 0.0){
		*error_string = _("log: domain error");
		return NULL;
	}
	return value_float (log (t));
}

static char *help_power = {
	N_("@FUNCTION=POWER\n"
	   "@SYNTAX=POWER(x,y)\n"

	   "@DESCRIPTION="
	   "Returns the value of x raised to the power y"
	   "\n"
	   "Performing this function on a string or empty cell returns an error. "
	   "\n"
	   "@SEEALSO=EXP")
};

static Value *
gnumeric_power (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (pow(value_get_as_double (argv [0]),
				value_get_as_double (argv [1]))) ;
}

static char *help_log2 = {
	N_("@FUNCTION=LOG2\n"
	   "@SYNTAX=LOG2(x)\n"

	   "@DESCRIPTION="
	   "Computes the base-2 logarithm  of x. "
	   "\n"
	   "Performing this function on a string or empty cell returns an error. "
	   "\n"
	   "@SEEALSO=EXP, LOG10, LOG")
};

static Value *
gnumeric_log2 (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_double (argv [0]);
	if (t <= 0.0){
		*error_string = _("log2: domain error");
		return NULL;
	}
	return value_float (log (t) / M_LN2);
}

static char *help_log10 = {
	N_("@FUNCTION=LOG10\n"
	   "@SYNTAX=LOG10(x)\n"

	   "@DESCRIPTION="
	   "Computes the base-10 logarithm  of x. "
	   "\n"

	   "Performing this function on a string or empty cell returns an error. "
	   "\n"
	   "@SEEALSO=EXP, LOG2, LOG")
};

static Value *
gnumeric_log10 (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_double (argv [0]);
	if (t <= 0.0){
		*error_string = _("log10: domain error");
		return NULL;
	}
	return value_float (log10 (t));
}

static char *help_mod = {
	N_("@FUNCTION=MOD\n"
	   "@SYNTAX=MOD(number,divisor)\n"

	   "@DESCRIPTION="
	   "Implements modulo arithmetic."
	   "Returns the remainder when divisor is divided into abs(number)."
	   "\n"
	   "Returns #DIV/0! if divisor is zero."
	   "@SEEALSO=INT,FLOOR,CEIL")
};

static Value *
gnumeric_mod (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	int a,b;
	
	a = value_get_as_int (argv[0]) ;
	b = value_get_as_int (argv[1]) ;
	/* Obscure handling of C's mod function */
	if (a<0) a = -a ;
	if (a<0) { /* -0 */
		*error_string = _("#NUM!") ;
		return NULL ;
	}
	if (b<0) {
		a = -a ;
		b = -b ;
	}
	if (b<0) { /* -0 */
		*error_string = _("#NUM!") ;
		return NULL ;
	}
	if (b==0) {
		*error_string = _("#DIV/0!") ;
		return NULL ;
	}
	
	return value_int(a%b) ;
}

static char *help_not = {
	N_("@FUNCTION=NOT\n"
	   "@SYNTAX=NOT(number)\n"

	   "@DESCRIPTION="
	   "Implements the logical NOT function: the result is TRUE if the "
	   "number is zero;  othewise the result is FALSE.\n\n"

	   "@SEEALSO=AND, OR")
};

static Value *
gnumeric_not (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	int b;
	
	b = value_get_as_int (argv [0]);
	
	return value_int (!b);
}

static char *help_or = {
	N_("@FUNCTION=OR\n"
	   "@SYNTAX=OR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "Implements the logical OR function: the result is TRUE if any of the "
	   "values evaluated to TRUE.\n"
	   "b1, trough bN are expressions that should evaluate to TRUE or FALSE. "
	   "If an integer or floating point value is provided zero is considered "
	   "FALSE and anything else is TRUE.\n"
	   "If the values contain strings or empty cells those values are "
	   "ignored.  If no logical values are provided, then the error '#VALUE!'"
	   "is returned.\n"

	   "@SEEALSO=AND, NOT")
};

static int
callback_function_or (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	Value *result = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		if (value->v.v_int != 0){
			result->v.v_int = 1;
			return FALSE;
		} else
			result->v.v_int = 0;
		break;

	case VALUE_FLOAT:
		if (value->v.v_float != 0.0){
			result->v.v_int = 1;
			return FALSE;
		} else
			result->v.v_int = 0;

	default:
		/* ignore strings */
		break;
	}
	
	return TRUE;
}

static Value *
gnumeric_or (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *result;
	Sheet *sheet = (Sheet *) tsheet;

	result = g_new (Value, 1);
	result->type = VALUE_INTEGER;
	result->v.v_int = -1;

	function_iterate_argument_values (sheet, callback_function_or,
					  result, expr_node_list,
					  eval_col, eval_row, error_string);

	/* See if there was any value worth using */
	if (result->v.v_int == -1){
		value_release (result);
		*error_string = _("#VALUE");
		return NULL;
	}
	return result;
}

static char *help_radians = {
	N_("@FUNCTION=RADIANS\n"
	   "@SYNTAX=RADIANS(x)\n"

	   "@DESCRIPTION="
	   "Computes the number of radians equivalent to  "
	   "x degrees. "
	   "\n"

	   "Performing this function on a string or empty cell simply does nothing. "
	   "\n"
	   
	   "@SEEALSO=PI,DEGREES")
};

static Value *
gnumeric_radians (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float ((value_get_as_double (argv [0]) * M_PI) / 180);
}

static char *help_rand = {
	N_("@FUNCTION=RAND\n"
	   "@SYNTAX=RAND()\n"

	   "@DESCRIPTION="
	   "Returns a random number greater than or equal to 0 and less than 1."
	   "\n"
	   "\n"
	   
	   "@SEEALSO=")
};

static Value *
gnumeric_rand (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (rand()/(RAND_MAX + 1.0)) ;
}

static char *help_sin = {
	N_("@FUNCTION=SIN\n"
	   "@SYNTAX=SIN(x)\n"

	   "@DESCRIPTION="
	   "The SIN function returns the sine of x, where x is given "
           " in radians. "
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=COS, COSH, SINH, TAN, TANH, RADIANS, DEGREES")
};

static Value *
gnumeric_sin (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (sin (value_get_as_double (argv [0])));
}

static char *help_sinh = {
	N_("@FUNCTION=SINH\n"
	   "@SYNTAX=SINH(x)\n"

	   "@DESCRIPTION="
	   "The SINH  function  returns  the  hyperbolic sine of x, "
	   "which is defined mathematically as (exp(x) - exp(-x)) / 2. "
	   " x is in radians. "
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=SIN, COS, COSH, TAN, TANH, DEGREES, RADIANS, EXP")
};

static Value *
gnumeric_sinh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (sinh (value_get_as_double (argv [0])));
}

static char *help_sqrt = {
	N_("@FUNCTION=SQRT\n"
	   "@SYNTAX=SQRT(x)\n"

	   "@DESCRIPTION="
	   "The SQRT  function  returns  the  square root of x, "
	   "\n"
	   "If x is negative returns #NUM!."
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=POW")
};

static Value *
gnumeric_sqrt (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	float_t x = value_get_as_double (argv[0]) ;
	if (x<0) {
		*error_string = _("#NUM!") ;
		return NULL ;
	}
	return value_float (sqrt(x)) ;
}

static char *help_sum = {
	N_("@FUNCTION=SUM\n"
	   "@SYNTAX=SUM(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "Computes the sum of all the values and cells referenced in the "
	   "argument list. " 
	   "\n"

	   "@SEEALSO=AVERAGE, COUNT")
};

static int
callback_function_sum (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	Value *result = (Value *) closure;

	switch (value->type){
	case VALUE_INTEGER:
		if (result->type == VALUE_INTEGER){
			if ((result->v.v_int > 0) && (value->v.v_int > 0)){
				int sum = result->v.v_int + value->v.v_int;

				if (sum < result->v.v_int){
					double n = result->v.v_int + value->v.v_int;
					
					result->type = VALUE_FLOAT;
					result->v.v_float = n;
				} else
					result->v.v_int = sum;
			} else if ((result->v.v_int < 0) && (value->v.v_int < 0)){
				int sum = result->v.v_int + value->v.v_int;

				if (sum > result->v.v_int){
					double n = result->v.v_int + value->v.v_int;
					
					result->type = VALUE_FLOAT;
					result->v.v_float = n;
				} else {
					result->v.v_int = sum;
				}
			} else {
				result->v.v_int += value->v.v_int;
			}
		} else
			result->v.v_float += value->v.v_int;
		break;
		
	case VALUE_FLOAT:
		if (result->type == VALUE_FLOAT)
			result->v.v_float += value->v.v_float;
		else {
			double v = result->v.v_int;

			/* cast to float */
			
			result->type = VALUE_FLOAT;
			result->v.v_float = v + value->v.v_float;
		}
		break;

	case VALUE_STRING:
		break;
		
	default:
		g_warning ("Unimplemented value->type in callback_function_sum : %s (%d)",
			   (value->type == VALUE_CELLRANGE) ? "CELLRANGE" :
			   (value->type == VALUE_ARRAY) ? "ARRAY" :
			   "UNKOWN!", value->type);
		break;
	}		
	return TRUE;
}

Value *
gnumeric_sum (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *result;
	Sheet *sheet = (Sheet *) tsheet;

	result = g_new (Value, 1);
	result->type = VALUE_INTEGER;
	result->v.v_int = 0;
	
	function_iterate_argument_values (sheet, callback_function_sum, result, expr_node_list,
					  eval_col, eval_row, error_string);

	return result;
}

static char *help_sumsq = {
	N_("@FUNCTION=SUMSQ\n"
	   "@SYNTAX=SUMSQ(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "SUMSQ returns the sum of the squares of all the values and "
	   "cells referenced in the argument list. " 
	   "\n"

	   "@SEEALSO=SUM, COUNT")
};

typedef struct {
	guint32 num;
	float_t sum;
} math_sumsq_t;

static int
callback_function_sumsq (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	math_sumsq_t *mm = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		mm->num++;
		mm->sum += value->v.v_int * value->v.v_int;
		break;
	case VALUE_FLOAT:
		mm->num++;
		mm->sum += value->v.v_float * value->v.v_float;
		break;
	default:
		/* ignore strings */
		break;
	}
	return TRUE;
}

static Value *
gnumeric_sumsq (void *sheet, GList *expr_node_list, int eval_col,
		int eval_row, char **error_string)
{
        math_sumsq_t p;

	p.num = 0;
	p.sum = 0;

	function_iterate_argument_values (sheet, callback_function_sumsq,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);

	return value_float (p.sum);
}

static char *help_multinomial = {
	N_("@FUNCTION=MULTINOMIAL\n"
	   "@SYNTAX=MULTINOMIAL(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "MULTINOMIAL returns the ratio of the factorial of a sum of "
	   "values to the product of factorials. "
	   "\n"

	   "@SEEALSO=SUM")
};

typedef struct {
	guint32 num;
	int     sum;
        int     product;
} math_multinomial_t;

static int
callback_function_multinomial (Sheet *sheet, Value *value,
			       char **error_string, void *closure)
{
	math_multinomial_t *mm = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
	        mm->product *= fact(value->v.v_int);
		mm->sum += value->v.v_int;
		mm->num++;
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

static Value *
gnumeric_multinomial (void *sheet, GList *expr_node_list, int eval_col,
		      int eval_row, char **error_string)
{
        math_multinomial_t p;

	p.num = 0;
	p.sum = 0;
	p.product = 1;

	if (function_iterate_argument_values (sheet,
					      callback_function_multinomial,
					      &p, expr_node_list,
					      eval_col, eval_row,
					      error_string) == FALSE) {
	        *error_string = _("#VALUE!");
		return NULL;
	}

	return value_float (fact(p.sum) / p.product);
}

static char *help_product = {
	N_("@FUNCTION=PRODUCT\n"
	   "@SYNTAX=PRODUCT(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "PRODUCT returns the product of all the values and cells "
	   "referenced in the argument list. " 
	   "\n"

	   "@SEEALSO=SUM, COUNT")
};

typedef struct {
	guint32 num;
	float_t product;
} math_product_t;

static int
callback_function_product (Sheet *sheet, Value *value,
			   char **error_string, void *closure)
{
	math_product_t *mm = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		mm->num++;
		mm->product *= value->v.v_int;
		break;
	case VALUE_FLOAT:
		mm->num++;
		mm->product *= value->v.v_float;
		break;
	default:
		/* ignore strings */
		break;
	}
	return TRUE;
}

static Value *
gnumeric_product (void *sheet, GList *expr_node_list, int eval_col,
		  int eval_row, char **error_string)
{
        math_product_t p;

	p.num = 0;
	p.product = 1;

	function_iterate_argument_values (sheet, callback_function_product,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);

	return value_float (p.product);
}

static char *help_tan = {
	N_("@FUNCTION=TAN\n"
	   "@SYNTAX=TAN(x)\n"

	   "@DESCRIPTION="
	   "The TAN function  returns the tangent of x, where x is "
	   "given in radians. "
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=TANH, COS, COSH, SIN, SINH, DEGREES, RADIANS")
};

static Value *
gnumeric_tan (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (tan (value_get_as_double (argv [0])));
}

static char *help_tanh = {
	N_("@FUNCTION=TANH\n"
	   "@SYNTAX=TANH(x)\n"

	   "@DESCRIPTION="
	   " The TANH function returns the hyperbolic tangent of x, "
	   " which is defined mathematically as sinh(x) / cosh(x). "
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=TAN, SIN, SINH, COS, COSH, DEGREES, RADIANS")
};

static Value *
gnumeric_tanh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (tanh (value_get_as_double (argv [0])));
}

static char *help_pi = {
	N_("@FUNCTION=PI\n"
	   "@SYNTAX=PI()\n"

	   "@DESCRIPTION=The PI functions returns the value of Pi "
	   "as defined by M_PI."
	   "\n"

	   "This function is called with no arguments."
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_pi (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	return value_float (M_PI);
}

static char *help_trunc = {
	N_("@FUNCTION=TRUNC\n"
	   "@SYNTAX=TRUNC(number[,digits])\n"

	   "@DESCRIPTION=The TRUNC function returns the value of number "
	   "truncated to the number of digits specified.  If digits is omited "
	   "then digits defaults to zero."
	   "\n"

	   "\n"
	   "@SEEALSO=")
};
static Value *
gnumeric_trunc (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *number;
	int args = g_list_length (expr_node_list);
	int decimals = 0;
	double v, integral, fraction;
	
	if (args < 1 || args > 2){
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	number = eval_expr (tsheet, (ExprTree *) expr_node_list->data, eval_col, eval_row, error_string);
	if (!number)
		return NULL;

	v = number->v.v_float;
	value_release (number);
	
	if (args == 2){
		Value *value;

		value = eval_expr (tsheet, (ExprTree *) expr_node_list->next->data, eval_col, eval_row, error_string);
		if (!value){
			return NULL;
		}
		
		decimals = value_get_as_int (value);
		value_release (value);
	}

	fraction = modf (v, &integral);
	if (decimals){
		double pot = pow (10, decimals);
		
		return value_float (integral + floor (fraction * pot) / pot);
	} else
		return value_float (integral);
}

static char *help_even = {
	N_("@FUNCTION=EVEN\n"
	   "@SYNTAX=EVEN(number)\n"

	   "@DESCRIPTION=EVEN function returns the number rounded up to the "
	   "nearest even integer. "
	   "\n"
	   "@SEEALSO=ODD")
};

static Value *
gnumeric_even (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        float_t number, ceiled;
	int     sign = 1;

	number = value_get_as_double (argv[0]);
	if (number < 0) {
	        sign = -1;
		number = -number;
	}
	ceiled = ceil(number);
	if (fmod(ceiled, 2) == 0)
	        if (number > ceiled)
		        return value_int ((int) (sign * (ceiled + 2)));
		else
		        return value_int ((int) (sign * ceiled));
	else
	        return value_int ((int) (sign * (ceiled + 1)));
}

static char *help_odd = {
	N_("@FUNCTION=ODD\n"
	   "@SYNTAX=ODD(number)\n"

	   "@DESCRIPTION=ODD function returns the number rounded up to the "
	   "nearest odd integer. "
	   "\n"
	   "@SEEALSO=EVEN")
};

static Value *
gnumeric_odd (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        float_t number, ceiled;
	int     sign = 1;

	number = value_get_as_double (argv[0]);
	if (number < 0) {
	        sign = -1;
		number = -number;
	}
	ceiled = ceil(number);
	if (fmod(ceiled, 2) == 1)
	        if (number > ceiled)
		        return value_int ((int) (sign * (ceiled + 2)));
		else
		        return value_int ((int) (sign * ceiled));
	else
	        return value_int ((int) (sign * (ceiled + 1)));
}

static char *help_factdouble = {
	N_("@FUNCTION=FACTDOUBLE\n"
	   "@SYNTAX=FACTDOUBLE(number)\n"

	   "@DESCRIPTION=FACTDOUBLE function returns the double factorial "
	   "of a number. "
	   "\n"
	   "If @number is not an integer, it is truncated. "
	   "If @number is negative FACTDOUBLE returns #NUM! error. "
	   "\n"
	   "@SEEALSO=FACT")
};

static Value *
gnumeric_factdouble (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        int number;
	int n;
	int product = 1;

	number = value_get_as_int (argv[0]);
	if (number < 0) {
		*error_string = _("#NUM!") ;
		return NULL ;
	}
	for (n=number; n > 0; n-=2)
	        product *= n;
	return value_int (product);
}

static char *help_quotient = {
	N_("@FUNCTION=QUOTIENT\n"
	   "@SYNTAX=QUOTIENT(num,den)\n"

	   "@DESCRIPTION=QUOTIENT function returns the integer portion "
	   "of a division. @num is the divided and @den is the divisor. "
	   "\n"
	   "@SEEALSO=MOD")
};

static Value *
gnumeric_quotient (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        float_t num, den;

	num = value_get_as_double (argv[0]);
	den = value_get_as_double (argv[1]);

	return value_int ((int) (num / den));
}

static char *help_sign = {
	N_("@FUNCTION=SIGN\n"
	   "@SYNTAX=SIGN(num)\n"

	   "@DESCRIPTION=SIGN function returns 1 if the number is positive, "
	   "zero if the number is 0, and -1 if the number is negative. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_sign (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        float_t n;

	n = value_get_as_double (argv[0]);

	if (n > 0)
	      return value_int (1);
	else if (n == 0)
	      return value_int (0);
	else
	      return value_int (-1);
}

static char *help_sqrtpi = {
	N_("@FUNCTION=SQRTPI\n"
	   "@SYNTAX=SQRTPI(number)\n"

	   "@DESCRIPTION=SQRTPI function returns the square root of a number "
	   "multiplied by pi. "
	   "\n"
	   "@SEEALSO=PI")
};

static Value *
gnumeric_sqrtpi (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        float_t n;

	n = value_get_as_double (argv[0]);
	if (n < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}

	return value_float (sqrt (M_PI * n));
}


static char *help_randbetween = {
	N_("@FUNCTION=RANDBETWEEN\n"
	   "@SYNTAX=RANDBETWEEN(bottom,top)\n"

	   "@DESCRIPTION=RANDBETWEEN function returns a random integer number "
	   "between @bottom and @top. "
	   "\n"
	   "If @bottom or @top is non-integer it is truncated. "
	   "If @bottom > @top RANDBETWEEN returns #NUM! error. "
	   "\n"
	   "@SEEALSO=RAND")
};


static Value *
gnumeric_randbetween (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
        int bottom, top;
	int r = rand();

	bottom = value_get_as_int (argv[0]);
	top = value_get_as_int (argv[1]);
	if (bottom > top) {
		*error_string = _("#NUM!") ;
		return NULL ;
	}
	if (top - bottom > RAND_MAX) {
		*error_string = _("#N/A!") ;
		return NULL ;
	}

	return value_int (r % (top-bottom+1) + bottom);
}

typedef struct {
        GSList *list;
        int    num;
} math_sums_t;

static int
callback_function_sumxy (Sheet *sheet, int col, int row,
			 Cell *cell, void *user_data)
{
        math_sums_t *mm = user_data;
        float_t     x;
	gpointer    p;

	if (cell == NULL || cell->value == NULL)
	        return TRUE;

        switch (cell->value->type) {
	case VALUE_INTEGER:
	        x = cell->value->v.v_int;
		break;
	case VALUE_FLOAT:
	        x = cell->value->v.v_float;
		break;
	default:
	        return TRUE;
	}

	p = g_new(float_t, 1);
	*((float_t *) p) = x;
	mm->list = g_slist_append(mm->list, p);
	mm->num++;

	return TRUE;
}

static char *help_sumx2my2 = {
	N_("@FUNCTION=SUMX2MY2\n"
	   "@SYNTAX=SUMX2MY2(array1,array2)\n"

	   "@DESCRIPTION="
	   "SUMX2MY2 function returns the sum of the difference of squares "
	   "of corresponding values in two arrays. @array1 is the first "
	   "array or range of data points and @array2 is the second array "
	   "or range of data points. The equation of SUMX2MY2 is "
	   "SUM (x^2-y^2). "
	   "\n"
           "Strings and empty cells are simply ignored."
           "\n"
	   "If @array1 and @array2 have different number of data points, "
	   "SUMX2MY2 returns #N/A! error. "
	   "\n"
	   "@SEEALSO=SUMSQ")
};

static Value *
gnumeric_sumx2my2 (struct FunctionDefinition *i,
		   Value *argv [], char **error_string)
{
        Value       *values_x = argv[0];
        Value       *values_y = argv[1];
	math_sums_t items_x, items_y;
	int         ret;
	float_t     sum;
	GSList      *list1, *list2;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (values_x->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  values_x->v.cell_range.cell_a.sheet, TRUE,
		  values_x->v.cell_range.cell_a.col, 
		  values_x->v.cell_range.cell_a.row,
		  values_x->v.cell_range.cell_b.col,
		  values_x->v.cell_range.cell_b.row,
		  callback_function_sumxy,
		  &items_x);
		if (ret == FALSE) {
		        *error_string = _("#VALUE!");
			return NULL;
		}
	} else {
		*error_string = _("Array version not implemented!");
		return NULL;
	}
	
        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  values_y->v.cell_range.cell_a.sheet, TRUE,
		  values_y->v.cell_range.cell_a.col, 
		  values_y->v.cell_range.cell_a.row,
		  values_y->v.cell_range.cell_b.col,
		  values_y->v.cell_range.cell_b.row,
		  callback_function_sumxy,
		  &items_y);
		if (ret == FALSE) {
		        *error_string = _("#VALUE!");
			return NULL;
		}
	} else {
		*error_string = _("Array version not implemented!");
		return NULL;
	}

	if (items_x.num != items_y.num) {
	        *error_string = _("#N/A!");
		return NULL;
	}

	list1 = items_x.list;
	list2 = items_y.list;
	sum = 0;
	while (list1 != NULL) {
	        float_t  x, y;

		x = *((float_t *) list1->data);
		y = *((float_t *) list2->data);
		sum += x*x - y*y;
		g_free(list1->data);
		g_free(list2->data);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free(items_x.list);
	g_slist_free(items_y.list);
	
	return value_float (sum);
}

static char *help_sumx2py2 = {
	N_("@FUNCTION=SUMX2PY2\n"
	   "@SYNTAX=SUMX2PY2(array1,array2)\n"

	   "@DESCRIPTION="
	   "SUMX2PY2 function returns the sum of the sum of squares "
	   "of corresponding values in two arrays. @array1 is the first "
	   "array or range of data points and @array2 is the second array "
	   "or range of data points. The equation of SUMX2PY2 is "
	   "SUM (x^2+y^2). "
	   "\n"
           "Strings and empty cells are simply ignored."
           "\n"
	   "If @array1 and @array2 have different number of data points, "
	   "SUMX2PY2 returns #N/A! error. "
	   "\n"
	   "@SEEALSO=SUMSQ")
};

static Value *
gnumeric_sumx2py2 (struct FunctionDefinition *i,
		   Value *argv [], char **error_string)
{
        Value       *values_x = argv[0];
        Value       *values_y = argv[1];
	math_sums_t items_x, items_y;
	int         ret;
	float_t     sum;
	GSList      *list1, *list2;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (values_x->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  values_x->v.cell_range.cell_a.sheet, TRUE,
		  values_x->v.cell_range.cell_a.col, 
		  values_x->v.cell_range.cell_a.row,
		  values_x->v.cell_range.cell_b.col,
		  values_x->v.cell_range.cell_b.row,
		  callback_function_sumxy,
		  &items_x);
		if (ret == FALSE) {
		        *error_string = _("#VALUE!");
			return NULL;
		}
	} else {
		*error_string = _("Array version not implemented!");
		return NULL;
	}
	
        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  values_y->v.cell_range.cell_a.sheet, TRUE,
		  values_y->v.cell_range.cell_a.col, 
		  values_y->v.cell_range.cell_a.row,
		  values_y->v.cell_range.cell_b.col,
		  values_y->v.cell_range.cell_b.row,
		  callback_function_sumxy,
		  &items_y);
		if (ret == FALSE) {
		        *error_string = _("#VALUE!");
			return NULL;
		}
	} else {
		*error_string = _("Array version not implemented!");
		return NULL;
	}

	if (items_x.num != items_y.num) {
	        *error_string = _("#N/A!");
		return NULL;
	}

	list1 = items_x.list;
	list2 = items_y.list;
	sum = 0;
	while (list1 != NULL) {
	        float_t  x, y;

		x = *((float_t *) list1->data);
		y = *((float_t *) list2->data);
		sum += x*x + y*y;
		g_free(list1->data);
		g_free(list2->data);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free(items_x.list);
	g_slist_free(items_y.list);
	
	return value_float (sum);
}

static char *help_sumxmy2 = {
	N_("@FUNCTION=SUMXMY2\n"
	   "@SYNTAX=SUMXMY2(array1,array2)\n"

	   "@DESCRIPTION="
	   "SUMXMY2 function returns the sum of squares of differences "
	   "of corresponding values in two arrays. @array1 is the first "
	   "array or range of data points and @array2 is the second array "
	   "or range of data points. The equation of SUMXMY2 is "
	   "SUM (x-y)^2. "
	   "\n"
           "Strings and empty cells are simply ignored."
           "\n"
	   "If @array1 and @array2 have different number of data points, "
	   "SUMXMY2 returns #N/A! error. "
	   "\n"
	   "@SEEALSO=SUMSQ")
};

static Value *
gnumeric_sumxmy2 (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
        Value       *values_x = argv[0];
        Value       *values_y = argv[1];
	math_sums_t items_x, items_y;
	int         ret;
	float_t     sum;
	GSList      *list1, *list2;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (values_x->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  values_x->v.cell_range.cell_a.sheet, TRUE,
		  values_x->v.cell_range.cell_a.col, 
		  values_x->v.cell_range.cell_a.row,
		  values_x->v.cell_range.cell_b.col,
		  values_x->v.cell_range.cell_b.row,
		  callback_function_sumxy,
		  &items_x);
		if (ret == FALSE) {
		        *error_string = _("#VALUE!");
			return NULL;
		}
	} else {
		*error_string = _("Array version not implemented!");
		return NULL;
	}
	
        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  values_y->v.cell_range.cell_a.sheet, TRUE,
		  values_y->v.cell_range.cell_a.col, 
		  values_y->v.cell_range.cell_a.row,
		  values_y->v.cell_range.cell_b.col,
		  values_y->v.cell_range.cell_b.row,
		  callback_function_sumxy,
		  &items_y);
		if (ret == FALSE) {
		        *error_string = _("#VALUE!");
			return NULL;
		}
	} else {
		*error_string = _("Array version not implemented!");
		return NULL;
	}

	if (items_x.num != items_y.num) {
	        *error_string = _("#N/A!");
		return NULL;
	}

	list1 = items_x.list;
	list2 = items_y.list;
	sum = 0;
	while (list1 != NULL) {
	        float_t  x, y;

		x = *((float_t *) list1->data);
		y = *((float_t *) list2->data);
		sum += (x-y) * (x-y);
		g_free(list1->data);
		g_free(list2->data);
		list1 = list1->next;
		list2 = list2->next;
	}

	g_slist_free(items_x.list);
	g_slist_free(items_y.list);
	
	return value_float (sum);
}

FunctionDefinition math_functions [] = {
	{ "abs",     "f",    "number",    &help_abs,   NULL, gnumeric_abs },
	{ "acos",    "f",    "number",    &help_acos,  NULL, gnumeric_acos },
	{ "acosh",   "f",    "number",    &help_acosh, NULL, gnumeric_acosh },
	{ "and",     0,      "",          &help_and,   gnumeric_and, NULL },
	{ "asin",    "f",    "number",    &help_asin,  NULL, gnumeric_asin },
	{ "asinh",   "f",    "number",    &help_asinh, NULL, gnumeric_asinh },
	{ "atan",    "f",    "number",    &help_atan,  NULL, gnumeric_atan },
	{ "atanh",   "f",    "number",    &help_atanh, NULL, gnumeric_atanh },
	{ "atan2",   "ff",   "xnum,ynum", &help_atan2, NULL, gnumeric_atan2 },
	/* avedev */
	{ "cos",     "f",    "number",    &help_cos,     NULL, gnumeric_cos },
	{ "cosh",    "f",    "number",    &help_cosh,    NULL, gnumeric_cosh },
	{ "ceil",    "f",    "number",    &help_ceil,    NULL, gnumeric_ceil },
	{ "ceiling", "ff",   "number,significance",    &help_ceiling, NULL, gnumeric_ceiling },
	{ "degrees", "f",    "number",    &help_degrees, NULL, gnumeric_degrees },
	{ "even",    "f",    "number",    &help_even,    NULL, gnumeric_even },
	{ "exp",     "f",    "number",    &help_exp,     NULL, gnumeric_exp },
	{ "fact",    "f",    "number",    &help_fact,    NULL, gnumeric_fact },
	{ "factdouble", "f", "number",    &help_factdouble,    NULL, gnumeric_factdouble },
	{ "combin",  "ff",   "n,k",       &help_combin,  NULL, gnumeric_combin },
	{ "floor",   "f",    "number",    &help_floor,   NULL, gnumeric_floor },
	{ "int",     "f",    "number",    &help_int,     NULL, gnumeric_int },
	{ "log",     "f",    "number",    &help_log,     NULL, gnumeric_log },
	{ "log2",    "f",    "number",    &help_log2,    NULL, gnumeric_log2 },
	{ "log10",   "f",    "number",    &help_log10,   NULL, gnumeric_log10 },
	{ "mod",     "ff",   "num,denom", &help_mod,     NULL, gnumeric_mod },
	{ "multinomial", 0,  "",          &help_multinomial, gnumeric_multinomial, NULL },
	{ "not",     "f",    "number",    &help_not,     NULL, gnumeric_not },
	{ "odd" ,    "f",    "number",    &help_odd,     NULL, gnumeric_odd },
	{ "or",      0,      "",          &help_or,      gnumeric_or, NULL },
	{ "power",   "ff",   "x,y",       &help_power,   NULL, gnumeric_power },
	{ "product", 0,      "number",    &help_product, gnumeric_product, NULL },
	{ "quotient" , "ff",  "num,den",  &help_quotient, NULL, gnumeric_quotient},
	{ "radians", "f",    "number",    &help_radians, NULL, gnumeric_radians },
	{ "rand",    "",     "",          &help_rand,    NULL, gnumeric_rand },
	{ "randbetween", "ff", "bottom,top", &help_randbetween, NULL, gnumeric_randbetween },
	{ "sign",    "f",    "number",    &help_sign,    NULL, gnumeric_sign },
	{ "sin",     "f",    "number",    &help_sin,     NULL, gnumeric_sin },
	{ "sinh",    "f",    "number",    &help_sinh,    NULL, gnumeric_sinh },
	{ "sqrt",    "f",    "number",    &help_sqrt,    NULL, gnumeric_sqrt },
	{ "sqrtpi",  "f",    "number",    &help_sqrtpi,  NULL, gnumeric_sqrtpi},
	{ "sum",     0,      "number",    &help_sum,     gnumeric_sum, NULL },
	{ "sumsq",   0,      "number",    &help_sumsq,   gnumeric_sumsq, NULL },
	{ "sumx2my2", "AA", "array1,array2", &help_sumx2my2, NULL, gnumeric_sumx2my2 },
	{ "sumx2py2", "AA", "array1,array2", &help_sumx2py2, NULL, gnumeric_sumx2py2 },
	{ "sumxmy2",  "AA", "array1,array2", &help_sumxmy2, NULL, gnumeric_sumxmy2 },
	{ "tan",     "f",    "number",    &help_tan,     NULL, gnumeric_tan },
	{ "tanh",    "f",    "number",    &help_tanh,    NULL, gnumeric_tanh },
	{ "trunc",   "f",    "number",    &help_trunc,   gnumeric_trunc, NULL },
	{ "pi",      "",     "",          &help_pi,      NULL, gnumeric_pi },
	{ NULL, NULL },
};
