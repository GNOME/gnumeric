/*
 * fn-math.c:  Built in mathematical functions and functions registration
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

/* Some forward declarations */
static Value *gnumeric_sum         (void *tsheet, GList *expr_node_list,
				    int eval_col, int eval_row,
				    char **error_string);

static Value *gnumeric_count       (void *tsheet, GList *expr_node_list,
				    int eval_col, int eval_row,
				    char **error_string);

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

static char *help_average = {
	N_("@FUNCTION=AVERAGE\n"
	   "@SYNTAX=AVERAGE(value1, value2,...)\n"

	   "@DESCRIPTION="
	   "Computes the average of all the values and cells referenced in the "
	   "argument list.  This is equivalent to the sum of the arguments divided "
	   "by the count of the arguments."
	   "\n"
	   "@SEEALSO=SUM, COUNT")
};

static Value *
gnumeric_average (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *result;
	Value *sum, *count;
	double c;
	
	sum = gnumeric_sum (tsheet, expr_node_list, eval_col, eval_row, error_string);
	if (!sum)
		return NULL;
	
	count = gnumeric_count (tsheet, expr_node_list, eval_col, eval_row, error_string);
	if (!count){
		value_release (sum);
		return NULL;
	}

	c = value_get_as_double (count);
	
	if (c == 0.0){
		*error_string = "Division by zero";
		value_release (sum);
		return NULL;
	}
	
	result = value_float (value_get_as_double (sum) / c);

	value_release (count);
	value_release (sum);
	
	return result;
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

static int
callback_function_count (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	Value *result = (Value *) closure;

	switch (value->type){
	case VALUE_INTEGER:
		result->v.v_int++;
		break;
		
	case VALUE_FLOAT:
		result->v.v_int++;
		break;
		
	default:
		break;
	}		
	return TRUE;
}

static char *help_count = {
	N_("@FUNCTION=COUNT\n"
	   "@SYNTAX=COUNT(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "Returns the total number of arguments passed."
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=AVERAGE")
};

static Value *
gnumeric_count (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *result;
	Sheet *sheet = (Sheet *) tsheet;

	result = g_new (Value, 1);
	result->type = VALUE_INTEGER;
	result->v.v_int = 0;
	
	function_iterate_argument_values (sheet, callback_function_count, result, expr_node_list,
					  eval_col, eval_row, error_string);

	return result;
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

static float_t
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

static char *help_min = {
	N_("@FUNCTION=MIN\n"
	   "@SYNTAX=MIN(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "MIN returns the value of the element of the values passed "
	   "that has the smallest value. With negative numbers considered "
	   "smaller than positive numbers."
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=MAX,ABS")
};

static char *help_max = {
	N_("@FUNCTION=MAX\n"
	   "@SYNTAX=MAX(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "MAX returns the value of the element of the values passed "
	   "that has the largest value. With negative numbers considered "
	   "smaller than positive numbers."
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=MIN,ABS")
};

enum {
	OPER_MIN,
	OPER_MAX
};

typedef struct {
	int   operation;
	int   found;
	Value *result;
} min_max_closure_t;

static int
callback_function_min_max (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	min_max_closure_t *mm = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		if (mm->found){
			if (mm->operation == OPER_MIN){
				if (value->v.v_int < mm->result->v.v_float)
					mm->result->v.v_float = value->v.v_int;
			} else {
				if (value->v.v_int > mm->result->v.v_float)
					mm->result->v.v_float = value->v.v_int;
			}
		} else {
			mm->found = 1;
			mm->result->v.v_float = value->v.v_int;
		}
		break;

	case VALUE_FLOAT:
		if (mm->found){
			if (mm->operation == OPER_MIN){
				if (value->v.v_float < mm->result->v.v_float)
					mm->result->v.v_float = value->v.v_float;
			} else {
				if (value->v.v_float > mm->result->v.v_float)
					mm->result->v.v_float = value->v.v_float;
			}
		} else {
			mm->found = 1;
			mm->result->v.v_float = value->v.v_float;
		}

	default:
		/* ignore strings */
	}
	
	return TRUE;
}

static Value *
gnumeric_min (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	min_max_closure_t closure;
	Sheet *sheet = (Sheet *) tsheet;

	closure.operation = OPER_MIN;
	closure.found  = 0;
	closure.result = g_new (Value, 1);
	closure.result->type = VALUE_FLOAT;
	closure.result->v.v_float = 0;

	function_iterate_argument_values (sheet, callback_function_min_max,
					  &closure, expr_node_list,
					  eval_col, eval_row, error_string);

	return 	closure.result;
}

static Value *
gnumeric_max (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	min_max_closure_t closure;
	Sheet *sheet = (Sheet *) tsheet;

	closure.operation = OPER_MAX;
	closure.found  = 0;
	closure.result = g_new (Value, 1);
	closure.result->type = VALUE_FLOAT;
	closure.result->v.v_float = 0;

	function_iterate_argument_values (sheet, callback_function_min_max,
					  &closure, expr_node_list,
					  eval_col, eval_row, error_string);
	return 	closure.result;
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
		if (result->type == VALUE_INTEGER)
			result->v.v_int += value->v.v_int;
		else
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

static Value *
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
	{ "average", 0,      "",          &help_average, gnumeric_average, NULL },
	/* besseli */
	/* besselj */
	/* besselk */
	/* bessely */
	{ "bin2dec", "?",    "number",    &help_bin2dec, NULL, gnumeric_bin2dec },
	{ "cos",     "f",    "number",    &help_cos,     NULL, gnumeric_cos },
	{ "cosh",    "f",    "number",    &help_cosh,    NULL, gnumeric_cosh },
	{ "count",   0,      "",          &help_count,   gnumeric_count, NULL },
	{ "ceil",    "f",    "number",    &help_ceil,    NULL, gnumeric_ceil },
	{ "degrees", "f",    "number",    &help_degrees, NULL, gnumeric_degrees },
	{ "exp",     "f",    "number",    &help_exp,     NULL, gnumeric_exp },
	{ "fact",    "f",    "number",    &help_fact,    NULL, gnumeric_fact },
	{ "floor",   "f",    "number",    &help_floor,   NULL, gnumeric_floor },
	{ "int",     "f",    "number",    &help_int,     NULL, gnumeric_int },
	{ "log",     "f",    "number",    &help_log,     NULL, gnumeric_log },
	{ "log2",    "f",    "number",    &help_log2,    NULL, gnumeric_log2 },
	{ "log10",   "f",    "number",    &help_log10,   NULL, gnumeric_log10 },
	{ "min",     0,      "",          &help_min,     gnumeric_min, NULL },
	{ "max",     0,      "",          &help_max,     gnumeric_max, NULL },
	{ "not",     "f",    "number",    &help_not,     NULL, gnumeric_not },
	{ "or",      0,      "",          &help_or,      gnumeric_or, NULL },
	{ "radians", "f",    "number",    &help_radians, NULL, gnumeric_radians },
	{ "sin",     "f",    "number",    &help_sin,     NULL, gnumeric_sin },
	{ "sinh",    "f",    "number",    &help_sinh,    NULL, gnumeric_sinh },
	{ "sum",     0,      "number",    &help_sum,     gnumeric_sum, NULL },
	{ "tan",     "f",    "number",    &help_tan,     NULL, gnumeric_tan },
	{ "tanh",    "f",    "number",    &help_tanh,    NULL, gnumeric_tanh },
	{ "trunc",   "f",    "number",    &help_trunc,   gnumeric_trunc, NULL },
	{ "pi",      "",     "",          &help_pi,      NULL, gnumeric_pi },
	{ NULL, NULL },
};


