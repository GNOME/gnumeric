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

static int
gcd(int a, int b)
{
        int ri, ri_1, ri_2, qi;
 
	if (b == 0)
	        return a;
 
	qi = a/b;
	ri_2 = a - qi*b;
 
	if (ri_2 == 0)
	        return 1;
 
	qi = b/ri_2;
	ri = ri_1 = b - qi*ri_2;
 
	while (ri > 0) {
	        qi = ri_2/ri_1;
		ri = ri_2 - qi*ri_1;
		ri_2 = ri_1;
		ri_1 = ri;
	}
 
	return ri_2;
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

typedef struct {
        GSList              *list;
        criteria_test_fun_t fun;
        Value               *test_value;
        int                 num;
} math_criteria_t;

static int
callback_function_criteria (Sheet *sheet, int col, int row,
			    Cell *cell, void *user_data)
{
        math_criteria_t *mm = user_data;
	Value           *v;
	gpointer        p;

	if (cell == NULL || cell->value == NULL)
	        return TRUE;

        switch (cell->value->type) {
	case VALUE_INTEGER:
	        v = value_new_int (cell->value->v.v_int);
		break;
	case VALUE_FLOAT:
	        v = value_new_float (cell->value->v.v_float);
		break;
	case VALUE_STRING:
	        v = value_new_string (cell->value->v.str->str);
		break;
	default:
	        return TRUE;
	}

	if (mm->fun(v, mm->test_value)) {
	        p = g_new(Value, 1);
		*((Value **) p) = v;
		mm->list = g_slist_append(mm->list, p);
		mm->num++;
	} else
	        value_release(v);

	return TRUE;
}

static char *help_gcd = {
	N_("@FUNCTION=GCD\n"
	   "@SYNTAX=GCD(a,b)\n"

	   "@DESCRIPTION="
	   "GCD returns the greatest common divisor of two numbers. "
	   "\n"
	   "If any of the arguments is less than zero, GCD returns #NUM! "
	   "error. "
	   "\n"
	   "@SEEALSO=LCM")
};

static Value *
gnumeric_gcd (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
        float_t a, b;

	a = value_get_as_float (argv[0]);
	b = value_get_as_float (argv[1]);

        if (a < 0 || b < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}
	        
	return value_new_int (gcd(a, b));
}

static char *help_lcm = {
	N_("@FUNCTION=LCM\n"
	   "@SYNTAX=LCM(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "LCM returns the least common multiple of integers.  The least "
	   "common multiple is the smallest positive number that is a "
	   "multiple of all integer arguments given. "
	   "\n"
	   "If any of the arguments is less than one, LCM returns #NUM! "
	   "error. "
	   "\n"
	   "@SEEALSO=GCD")
};

static int
callback_function_lcm (Sheet *sheet, Value *value,
		       char **error_string, void *closure)
{
	Value *result = closure;

	switch (value->type){
	case VALUE_INTEGER:
	        if (value->v.v_int < 1)
		        return FALSE;
	        result->v.v_int /= gcd(result->v.v_int, value->v.v_int);
		result->v.v_int *= value->v.v_int;
		break;
	default:
	        return FALSE;
	}
	
	return TRUE;
}

static Value *
gnumeric_lcm (Sheet *tsheet, GList *expr_node_list,
	      int eval_col, int eval_row, char **error_string)
{
	Value *result;
	Sheet *sheet = (Sheet *) tsheet;

	result = g_new (Value, 1);
	result->type = VALUE_INTEGER;
	result->v.v_int = 1;

	if (function_iterate_argument_values (sheet, callback_function_lcm,
					      result, expr_node_list,
					      eval_col, eval_row, 
					      error_string) == FALSE) {
		*error_string = _("#NUM!");
		return NULL;
	}

	return result;
}

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
gnumeric_abs (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
	return value_new_float (fabs (value_get_as_float (argv [0])));
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
gnumeric_acos (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_float (argv [0]);
	if ((t < -1.0) || (t > 1.0)){
		*error_string = _("acos - domain error");
		return NULL;
	}
	return value_new_float (acos (t));
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
gnumeric_acosh (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_float (argv [0]);
	if (t < 1.0){
		*error_string = _("acosh - domain error");
		return NULL;
	}
	return value_new_float (acosh (t));
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
gnumeric_asin (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_float (argv [0]);
	if ((t < -1.0) || (t > 1.0)){
		*error_string = _("asin - domain error");
		return NULL;
	}
	return value_new_float (asin (t));
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
gnumeric_asinh (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	return value_new_float (asinh (value_get_as_float (argv [0])));
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
gnumeric_atan (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	return value_new_float (atan (value_get_as_float (argv [0])));
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
gnumeric_atanh (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_float (argv [0]);
	if ((t <= -1.0) || (t >= 1.0)){
		*error_string = _("atanh: domain error");
		return NULL;
	}
	return value_new_float (atanh (value_get_as_float (argv [0])));
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
gnumeric_atan2 (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	return value_new_float (atan2 (value_get_as_float (argv [0]),
				   value_get_as_float (argv [1])));
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
gnumeric_ceil (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	return value_new_float (ceil (value_get_as_float (argv [0])));
}

static char *help_countif = {
	N_("@FUNCTION=COUNTIF\n"
	   "@SYNTAX=COUNTIF(range,criteria)\n"

	   "@DESCRIPTION="
	   "COUNTIF function counts the number of cells in the given range "
	   "that meet the given criteria. "

	   "\n"
	   
	   "@SEEALSO=COUNT,SUMIF")
};

static Value *
gnumeric_countif (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
        Value           *range = argv[0];
	math_criteria_t items;
	int             ret;
	GSList          *list;

	items.num  = 0;
	items.list = NULL;

	if ((!VALUE_IS_NUMBER(argv[1]) && argv[1]->type != VALUE_STRING)
	    || (range->type != VALUE_CELLRANGE)) {
	        *error_string = _("#VALUE!");
		return NULL;
	}

	if (VALUE_IS_NUMBER(argv[1])) {
	        items.fun = (criteria_test_fun_t) criteria_test_equal;
		items.test_value = argv[1];
	} else
	        parse_criteria(argv[1]->v.str->str,
			       &items.fun, &items.test_value);

	ret = sheet_cell_foreach_range (
	  range->v.cell_range.cell_a.sheet, TRUE,
	  range->v.cell_range.cell_a.col, 
	  range->v.cell_range.cell_a.row,
	  range->v.cell_range.cell_b.col,
	  range->v.cell_range.cell_b.row,
	  callback_function_criteria,
	  &items);
	if (ret == FALSE) {
	        *error_string = _("#VALUE!");
		return NULL;
	}

        list = items.list;

	while (list != NULL) {
		g_free(list->data);
		list = list->next;
	}
	g_slist_free(items.list);

	return value_new_int (items.num);
}

static char *help_sumif = {
	N_("@FUNCTION=SUMIF\n"
	   "@SYNTAX=SUMIF(range,criteria)\n"

	   "@DESCRIPTION="
	   "SUMIF function sums the values in the given range that meet "
	   "the given criteria. "

	   "\n"
	   
	   "@SEEALSO=COUNTIF,SUM")
};

static Value *
gnumeric_sumif (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
        Value           *range = argv[0];
	math_criteria_t items;
	int             ret;
	float_t         sum;
	GSList          *list;

	items.num  = 0;
	items.list = NULL;

	if ((!VALUE_IS_NUMBER(argv[1]) && argv[1]->type != VALUE_STRING)
	    || (range->type != VALUE_CELLRANGE)) {
	        *error_string = _("#VALUE!");
		return NULL;
	}

	if (VALUE_IS_NUMBER(argv[1])) {
	        items.fun = (criteria_test_fun_t) criteria_test_equal;
		items.test_value = argv[1];
	} else
	        parse_criteria(argv[1]->v.str->str,
			       &items.fun, &items.test_value);

	ret = sheet_cell_foreach_range (
	  range->v.cell_range.cell_a.sheet, TRUE,
	  range->v.cell_range.cell_a.col, 
	  range->v.cell_range.cell_a.row,
	  range->v.cell_range.cell_b.col,
	  range->v.cell_range.cell_b.row,
	  callback_function_criteria,
	  &items);
	if (ret == FALSE) {
	        *error_string = _("#VALUE!");
		return NULL;
	}

        list = items.list;
	sum = 0;

	while (list != NULL) {
	        Value *v = *((Value **) list->data);

		if (v != NULL)
		       sum += value_get_as_float (v);
		g_free(list->data);
		list = list->next;
	}
	g_slist_free(items.list);

	return value_new_float (sum);
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
gnumeric_ceiling (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
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
	x = value_get_as_float (argv[0]);
	significance = value_get_as_float (argv[1]);
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

	return value_new_float (sign * ceiled * div / (k*10) -
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
gnumeric_cos (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
	return value_new_float (cos (value_get_as_float (argv [0])));
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
gnumeric_cosh (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	return value_new_float (cosh (value_get_as_float (argv [0])));
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
gnumeric_degrees (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	return value_new_float ((value_get_as_float (argv [0]) * 180.0) / M_PI);
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
gnumeric_exp (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
	return value_new_float (exp (value_get_as_float (argv [0])));
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
gnumeric_fact (struct FunctionDefinition *id,
	       Value *argv [], char **error_string)
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
		*error_string = _("#NUM!");
		return NULL;
	}

	if (i < 0){
		*error_string = _("#NUM!");
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
	if (n >= 15) {
		float_t res;

		res = exp (lgamma (n + 1) - lgamma (k + 1) - lgamma (n - k + 1));
		return floor (res + 0.5);  /* Round, just in case.  */
	} else {
		float_t res;

		res = fact (n) / fact (k) / fact (n - k);
		return res;
	}
}

static Value *
gnumeric_combin (struct FunctionDefinition *id,
		 Value *argv [], char **error_string)
{
	int n ,k;

	n = value_get_as_int (argv[0]);
	k = value_get_as_int (argv[1]);

	if (k >= 0 && n >= k)
		return value_new_float (combin (n ,k));

	*error_string = _("#NUM!");
	return NULL;
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
gnumeric_floor (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	return value_new_float (floor (value_get_as_float (argv [0])));
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
gnumeric_int (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_float (argv [0]);
	
	return value_new_float (t > 0.0 ? floor (t) : ceil (t));
}

static char *help_log = {
	N_("@FUNCTION=LOG\n"
	   "@SYNTAX=LOG(x[,base])\n"

	   "@DESCRIPTION="
	   "Computes the logarithm of x in the given base.  If no base is "
	   "given LOG returns the logarithm in base 10. "
	   "\n"
	   "@SEEALSO=LN, LOG2, LOG10")
};

static Value *
gnumeric_log (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
	float_t t, base;

	t = value_get_as_float (argv [0]);

	if (argv[1] == NULL)
	        base = 10;
	else
	        base = value_get_as_float (argv[1]);

	if (t <= 0.0) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	return value_new_float (log (t) / log (base));
}

static char *help_ln = {
	N_("@FUNCTION=LN\n"
	   "@SYNTAX=LN(x)\n"

	   "@DESCRIPTION="
	   "LN returns the natural logarithm of x. "
	   "\n"
	   "@SEEALSO=EXP, LOG2, LOG10")
};

static Value *
gnumeric_ln (struct FunctionDefinition *i,
	     Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_float (argv [0]);

	if (t <= 0.0){
		*error_string = _("#VALUE!");
		return NULL;
	}

	return value_new_float (log (t));
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
gnumeric_power (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	return value_new_float (pow(value_get_as_float (argv [0]),
				value_get_as_float (argv [1]))) ;
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
gnumeric_log2 (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_float (argv [0]);
	if (t <= 0.0){
		*error_string = _("log2: domain error");
		return NULL;
	}
	return value_new_float (log (t) / M_LN2);
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
gnumeric_log10 (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	float_t t;

	t = value_get_as_float (argv [0]);
	if (t <= 0.0){
		*error_string = _("log10: domain error");
		return NULL;
	}
	return value_new_float (log10 (t));
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
gnumeric_mod (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
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
	
	return value_new_int (a%b) ;
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
gnumeric_radians (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	return value_new_float ((value_get_as_float (argv [0]) * M_PI) / 180);
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
gnumeric_rand (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	return value_new_float (rand()/(RAND_MAX + 1.0)) ;
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
gnumeric_sin (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
	return value_new_float (sin (value_get_as_float (argv [0])));
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
gnumeric_sinh (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	return value_new_float (sinh (value_get_as_float (argv [0])));
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
gnumeric_sqrt (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	float_t x = value_get_as_float (argv[0]) ;
	if (x<0) {
		*error_string = _("#NUM!") ;
		return NULL ;
	}
	return value_new_float (sqrt(x)) ;
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
callback_function_sum (Sheet *sheet, Value *value,
		       char **error_string, void *closure)
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
gnumeric_sum (Sheet *sheet, GList *expr_node_list,
	      int eval_col, int eval_row, char **error_string)
{
	Value *result;

	result = g_new (Value, 1);
	result->type = VALUE_INTEGER;
	result->v.v_int = 0;
	
	function_iterate_argument_values (sheet, callback_function_sum, result, expr_node_list,
					  eval_col, eval_row, error_string);

	return result;
}

static char *help_suma = {
	N_("@FUNCTION=SUMA\n"
	   "@SYNTAX=SUMA(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "Computes the sum of all the values and cells referenced in the "
	   "argument list.  Numbers, text and logical values are included "
	   "in the calculation too.  If the cell contains text or the "
	   "argument evaluates to FALSE, it is counted as value zero (0). "
	   "If the argument evaluates to TRUE, it is counted as one (1). "
	   "Note that empty cells are not counted." 
	   "\n"

	   "@SEEALSO=AVERAGE, SUM, COUNT")
};

Value *
gnumeric_suma (Sheet *sheet, GList *expr_node_list,
	       int eval_col, int eval_row, char **error_string)
{
	Value *result;

	result = g_new (Value, 1);
	result->type = VALUE_INTEGER;
	result->v.v_int = 0;
	
	function_iterate_argument_values (sheet, callback_function_sum, 
					  result, expr_node_list,
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
callback_function_sumsq (Sheet *sheet, Value *value,
			 char **error_string, void *closure)
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
gnumeric_sumsq (Sheet *sheet, GList *expr_node_list, int eval_col,
		int eval_row, char **error_string)
{
        math_sumsq_t p;

	p.num = 0;
	p.sum = 0;

	function_iterate_argument_values (sheet, callback_function_sumsq,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);

	return value_new_float (p.sum);
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
gnumeric_multinomial (Sheet *sheet, GList *expr_node_list, int eval_col,
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

	return value_new_float (fact(p.sum) / p.product);
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
gnumeric_product (Sheet *sheet, GList *expr_node_list, int eval_col,
		  int eval_row, char **error_string)
{
        math_product_t p;

	p.num = 0;
	p.product = 1;

	function_iterate_argument_values (sheet, callback_function_product,
					  &p, expr_node_list,
					  eval_col, eval_row, error_string);

	return value_new_float (p.product);
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
gnumeric_tan (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
	return value_new_float (tan (value_get_as_float (argv [0])));
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
gnumeric_tanh (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	return value_new_float (tanh (value_get_as_float (argv [0])));
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
gnumeric_pi (struct FunctionDefinition *i,
	     Value *argv [], char **error_string)
{
	return value_new_float (M_PI);
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
gnumeric_trunc (Sheet *sheet, GList *expr_node_list, 
		int eval_col, int eval_row, char **error_string)
{
	Value *number;
	int args = g_list_length (expr_node_list);
	int decimals = 0;
	double v, integral, fraction;
	
	if (args < 1 || args > 2){
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	number = eval_expr (sheet, (ExprTree *) expr_node_list->data,
			    eval_col, eval_row, error_string);
	if (!number)
		return NULL;

	v = number->v.v_float;
	value_release (number);
	
	if (args == 2){
		Value *value;

		value = eval_expr (sheet, 
				   (ExprTree *) expr_node_list->next->data,
				   eval_col, eval_row, error_string);
		if (!value){
			return NULL;
		}
		
		decimals = value_get_as_int (value);
		value_release (value);
	}

	fraction = modf (v, &integral);
	if (decimals){
		double pot = pow (10, decimals);
		
		return value_new_float (integral + floor (fraction * pot) / pot);
	} else
		return value_new_float (integral);
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
gnumeric_even (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
        float_t number, ceiled;
	int     sign = 1;

	number = value_get_as_float (argv[0]);
	if (number < 0) {
	        sign = -1;
		number = -number;
	}
	ceiled = ceil(number);
	if (fmod(ceiled, 2) == 0)
	        if (number > ceiled)
		        return value_new_int ((int) (sign * (ceiled + 2)));
		else
		        return value_new_int ((int) (sign * ceiled));
	else
	        return value_new_int ((int) (sign * (ceiled + 1)));
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
gnumeric_odd (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
        float_t number, ceiled;
	int     sign = 1;

	number = value_get_as_float (argv[0]);
	if (number < 0) {
	        sign = -1;
		number = -number;
	}
	ceiled = ceil(number);
	if (fmod(ceiled, 2) == 1)
	        if (number > ceiled)
		        return value_new_int ((int) (sign * (ceiled + 2)));
		else
		        return value_new_int ((int) (sign * ceiled));
	else
	        return value_new_int ((int) (sign * (ceiled + 1)));
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
gnumeric_factdouble (struct FunctionDefinition *i,
		     Value *argv [], char **error_string)
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
	return value_new_int (product);
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
gnumeric_quotient (struct FunctionDefinition *i,
		   Value *argv [], char **error_string)
{
        float_t num, den;

	num = value_get_as_float (argv[0]);
	den = value_get_as_float (argv[1]);

	return value_new_int ((int) (num / den));
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
gnumeric_sign (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
        float_t n;

	n = value_get_as_float (argv[0]);

	if (n > 0)
	      return value_new_int (1);
	else if (n == 0)
	      return value_new_int (0);
	else
	      return value_new_int (-1);
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
gnumeric_sqrtpi (struct FunctionDefinition *i,
		 Value *argv [], char **error_string)
{
        float_t n;

	n = value_get_as_float (argv[0]);
	if (n < 0) {
		*error_string = _("#NUM!");
		return NULL;
	}

	return value_new_float (sqrt (M_PI * n));
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
gnumeric_randbetween (struct FunctionDefinition *i,
		      Value *argv [], char **error_string)
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

	return value_new_int (r % (top-bottom+1) + bottom);
}

static char *help_rounddown = {
	N_("@FUNCTION=ROUNDDOWN\n"
	   "@SYNTAX=ROUNDDOWN(number[,digits])\n"

	   "@DESCRIPTION="
	   "ROUNDDOWN function rounds a given number down, towards zero. "
	   "@number is the number you want rounded down and @digits is the "
	   "number of digits to which you want to round that number. "
	   "\n"
	   "If digits is greater than zero, number is rounded down to the "
	   "given number of digits. "
	   "If digits is zero or omitted, number is rounded down to the "
	   "nearest integer. "
	   "If digits is less than zero, number is rounded down to the left "
	   "of the decimal point. "
	   "\n"
	   "@SEEALSO=ROUNDUP")
};

static Value *
gnumeric_rounddown (struct FunctionDefinition *i,
		    Value *argv [], char **error_string)
{
        float_t number;
        int     digits, k, n;

	number = value_get_as_float (argv[0]);
	if (argv[1] == NULL)
	        digits = 0;
	else
	        digits = value_get_as_int (argv[1]);

	if (digits > 0) {
	        k=1;
		for (n=0; n<digits; n++)
		        k *= 10;
	        return value_new_float ((float_t) ((int) (number * k)) / k);
	} else if (digits == 0) {
	        return value_new_int ((int) number);
	} else {
	        k=1;
		for (n=0; n<-digits; n++)
		        k *= 10;
		return value_new_float ((float_t) ((int) (number / k)) * k);
	}
}

static char *help_round = {
	N_("@FUNCTION=ROUND\n"
	   "@SYNTAX=ROUND(number[,digits])\n"

	   "@DESCRIPTION="
	   "ROUND function rounds a given number. "
	   "@number is the number you want rounded and @digits is the "
	   "number of digits to which you want to round that number. "
	   "\n"
	   "If digits is greater than zero, number is rounded to the "
	   "given number of digits. "
	   "If digits is zero or omitted, number is rounded to the "
	   "nearest integer. "
	   "If digits is less than zero, number is rounded to the left "
	   "of the decimal point. "
	   "\n"
	   "@SEEALSO=ROUNDDOWN,ROUNDUP")
};

static Value *
gnumeric_round (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
        float_t number;
        int     digits, k, n;

	number = value_get_as_float (argv[0]);
	if (argv[1] == NULL)
	        digits = 0;
	else
	        digits = value_get_as_int (argv[1]);

	if (digits > 0) {
	        k=1;
		for (n=0; n<digits; n++)
		        k *= 10;
	        return value_new_float ( rint(number * k) / k);
	} else if (digits == 0) {
	        return value_new_int ((int) number);
	} else {
	        k=1;
		for (n=0; n<-digits; n++)
		        k *= 10;
		return value_new_float (rint(number / k) * k);
	}
}

static char *help_roundup = {
	N_("@FUNCTION=ROUNDUP\n"
	   "@SYNTAX=ROUNDUP(number[,digits])\n"

	   "@DESCRIPTION="
	   "ROUNDUP function rounds a given number up, away from zero. "
	   "@number is the number you want rounded up and @digits is the "
	   "number of digits to which you want to round that number. "
	   "\n"
	   "If digits is greater than zero, number is rounded up to the "
	   "given number of digits. "
	   "If digits is zero or omitted, number is rounded up to the "
	   "nearest integer. "
	   "If digits is less than zero, number is rounded up to the left "
	   "of the decimal point. "
	   "\n"
	   "@SEEALSO=ROUNDDOWN")
};

static Value *
gnumeric_roundup (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
        float_t number, sign;
        int     digits, k, n;

	number = value_get_as_float (argv[0]);
	if (argv[1] == NULL)
	        digits = 0;
	else
	        digits = value_get_as_int (argv[1]);

	sign = (number < 0) ? -1.0 : 1.0;

	if (digits > 0) {
	        k=1;
		for (n=0; n<digits; n++)
		        k *= 10;
	        return value_new_float (sign * (ceil (fabs(number) * k)) / k);
	} else if (digits == 0) {
	        return value_new_int (sign * ceil(fabs(number)));
	} else {
	        k=1;
		for (n=0; n<-digits; n++)
		        k *= 10;
		if (fabs(number) < k)
		        return value_new_float (0);
		return value_new_float (sign * (ceil (fabs(number) / k)) * k);
	}
}

static char *help_mround = {
	N_("@FUNCTION=MROUND\n"
	   "@SYNTAX=MROUND(number,multiple)\n"

	   "@DESCRIPTION="
	   "MROUND function rounds a given number to the desired multiple. "
	   "@number is the number you want rounded and @multiple is the "
	   "the multiple to which you wnat to round the number. "
	   "For example, MROUND(1.7, 0.2) equals 1.8. "
	   "\n"
	   "If the number and the multiple have different sign, MROUND "
	   "returns #NUM! error. "
	   "\n"
	   "@SEEALSO=ROUNDDOWN,ROUND,ROUNDUP")
};

static Value *
gnumeric_mround (struct FunctionDefinition *i,
		 Value *argv [], char **error_string)
{
        const float_t accuracy_limit = 0.0000003;
        float_t number, multiple;
	float_t div, mod;
	int     sign = 1;

	number = value_get_as_float (argv[0]);
	multiple = value_get_as_float (argv[1]);

	if ((number > 0 && multiple < 0) 
	    || (number < 0 && multiple > 0)) {
		*error_string = _("#NUM!");
		return NULL;
	}
	if (number < 0) {
	        sign = -1;
		number = -number;
		multiple = -multiple;
	}

	mod = fmod(number, multiple);
	div = number-mod;

        return value_new_float (sign * (
	  div + ((mod + accuracy_limit >= multiple/2) ? multiple : 0)));
}

static char *help_roman = {
	N_("@FUNCTION=ROMAN\n"
	   "@SYNTAX=ROMAN(number[,type])\n"

	   "@DESCRIPTION="
	   "ROMAN function returns an arabic number in the roman numeral "
	   "style, as text. @number is the number you want to convert and "
	   "@type is the type of roman numeral you want. "
	   "\n"
	   "If @type is 0 or it is omitted, ROMAN returns classic roman "
	   "numbers. "
	   "Types 1,2,3, and 4 are not implemented yet. "
	   "If @number is negative or greater than 3999, ROMAN returns "
	   "#VALUE! error. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_roman (struct FunctionDefinition *fd,
		Value *argv [], char **error_string)
{
	const char letter[] = { 'M', 'D', 'C', 'L', 'X', 'V', 'I' };
	const int  largest = 1000;

	static char buf[256];
	int n, form;
	int i, j, dec;

	dec = largest;
	n = value_get_as_int (argv[0]);
	if (argv[1] == NULL)
	        form = 0;
	else
	        form = value_get_as_int (argv[1]);

	if (n < 0 || n > 3999) {
		*error_string = _("#VALUE!") ;
		return NULL ;
	}

	if (n == 0)
	        return value_new_string ("");

	if (form < 0 || form > 4) {
		*error_string = _("#NUM!") ;
		return NULL ;
	}

	if (form > 0) {
		*error_string = _("#Unimplemented!") ;
		return NULL ;
	}

	for (i=j=0; dec > 1; dec/=10, j+=2) {
	        for ( ; n>0; i++) {
		        if (n >= dec) {
			        buf[i] = letter[j];
				n -= dec;
			} else if (n >= dec - dec/10) {
			        buf[i++] = letter[j+2];
				buf[i] = letter[j];
				n -= dec - dec/10;
			} else if (n >= dec/2) {
			        buf[i] = letter[j+1];
				n -= dec/2;
			} else if (n >= dec/2 - dec/10) {
			        buf[i++] = letter[j+2];
				buf[i] = letter[j+1];
				n -= dec/2 - dec/10;
			} else if (dec == 10) {
			        buf[i] = letter[j+2];
				n--;
			} else
			        break;
		}
	}
	buf[i] = '\0';

	return value_new_string (buf);
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
	
	return value_new_float (sum);
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
	
	return value_new_float (sum);
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
	
	return value_new_float (sum);
}

static char *help_subtotal = {
	N_("@FUNCTION=SUBTOTAL\n"
	   "@SYNTAX=SUMIF(function_nbr,ref1,ref2,...)\n"

	   "@DESCRIPTION="
	   "SUBTOTAL function returns a subtotal of given list of arguments. "
	   "@function_nbr is the number that specifies which function to use "
	   "in calculating the subtotal. "
	   "The following functions are available:\n"
	   "1   AVERAGE\n"
	   "2   COUNT\n"
	   "3   COUNTA\n"
	   "4   MAX\n"
	   "5   MIN\n"
	   "6   PRODUCT\n"
	   "7   STDEV\n"
	   "8   STDEVP\n"
	   "9   SUM\n"
	   "10   VAR\n"
	   "11   VARP\n"
	   "\n"	   
	   "@SEEALSO=COUNT,SUM")
};

static Value *
gnumeric_subtotal (Sheet *tsheet, GList *expr_node_list,
		   int eval_col, int eval_row, char **error_string)
{
        ExprTree *tree;
	Value    *val;
	int      fun_nbr;

	if (expr_node_list == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	
	tree = (ExprTree *) expr_node_list->data;
	if (tree == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}

	val = eval_expr (tsheet, tree, eval_col, eval_row, error_string);
	if (! VALUE_IS_NUMBER(val)) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	fun_nbr = value_get_as_int(val);
	if (fun_nbr < 1 || fun_nbr > 11) {
		*error_string = _("#NUM!");
		return NULL;
	}

	switch (fun_nbr) {
	case 1:
	        return gnumeric_average(tsheet, expr_node_list->next,
					eval_col, eval_row, error_string);
	case 2:
	        return gnumeric_count(tsheet, expr_node_list->next,
				      eval_col, eval_row, error_string);
	case 3:
	        return gnumeric_counta(tsheet, expr_node_list->next,
				       eval_col, eval_row, error_string);
	case 4:
	        return gnumeric_max(tsheet, expr_node_list->next,
				    eval_col, eval_row, error_string);
	case 5:
	        return gnumeric_min(tsheet, expr_node_list->next,
				    eval_col, eval_row, error_string);
	case 6:
	        return gnumeric_product(tsheet, expr_node_list->next,
					eval_col, eval_row, error_string);
	case 7:
	        return gnumeric_stdev(tsheet, expr_node_list->next,
				      eval_col, eval_row, error_string);
	case 8:
	        return gnumeric_stdevp(tsheet, expr_node_list->next,
				       eval_col, eval_row, error_string);
	case 9:
	        return gnumeric_sum(tsheet, expr_node_list->next,
				    eval_col, eval_row, error_string);
	case 10:
	        return gnumeric_var(tsheet, expr_node_list->next,
				    eval_col, eval_row, error_string);
	case 11:
	        return gnumeric_varp(tsheet, expr_node_list->next,
				     eval_col, eval_row, error_string);
	}

	return NULL;
}

static char *help_seriessum = {
	N_("@FUNCTION=SERIESSUM\n"
	   "@SYNTAX=SERIESSUM(x,n,m,coefficients)\n"

	   "@DESCRIPTION="
	   "SERIESSUM function returns the sum of a power series.  @x is "
	   "the base of the power serie, @n is the initial power to raise @x, "
	   "@m is the increment to the power for each term in the series, and "
	   "@coefficients is the coefficents by which each successive power "
	   "of @x is multiplied. "
	   "\n"	   
	   "@SEEALSO=COUNT,SUM")
};


typedef struct {
        float_t sum;
        float_t x;
        float_t n;
        float_t m;
} math_seriessum_t;

static int
callback_function_seriessum (Sheet *sheet, Value *value,
			     char **error_string, void *closure)
{
	math_seriessum_t *mm = closure;
	float_t          coefficient=0;

	switch (value->type){
	case VALUE_INTEGER:
	        coefficient = value->v.v_int;
		break;
	case VALUE_FLOAT:
	        coefficient = value->v.v_float;
		break;
	default:
	        return FALSE;
	}

	mm->sum += coefficient * pow(mm->x, mm->n);
	mm->n += mm->m;

	return TRUE;
}

static Value *
gnumeric_seriessum (Sheet *sheet, GList *expr_node_list,
		    int eval_col, int eval_row, char **error_string)
{
        math_seriessum_t p;
        ExprTree         *tree;
	Value            *val;
	float_t          x, n, m;

	if (expr_node_list == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}

	/* Get x */
	tree = (ExprTree *) expr_node_list->data;
	if (tree == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	val = eval_expr (sheet, tree, eval_col, eval_row, error_string);
	if (! VALUE_IS_NUMBER(val)) {
		*error_string = _("#VALUE!");
		return NULL;
	}
	x = value_get_as_int(val);
	expr_node_list = expr_node_list->next;

	/* Get n */
	tree = (ExprTree *) expr_node_list->data;
	if (tree == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	val = eval_expr (sheet, tree, eval_col, eval_row, error_string);
	if (! VALUE_IS_NUMBER(val)) {
		*error_string = _("#VALUE!");
		return NULL;
	}
	n = value_get_as_int(val);
	expr_node_list = expr_node_list->next;

	/* Get m */
	tree = (ExprTree *) expr_node_list->data;
	if (tree == NULL) {
		*error_string = _("#NUM!");
		return NULL;
	}
	val = eval_expr (sheet, tree, eval_col, eval_row, error_string);
	if (! VALUE_IS_NUMBER(val)) {
		*error_string = _("#VALUE!");
		return NULL;
	}
	m = value_get_as_int(val);
	expr_node_list = expr_node_list->next;

	p.n = n;
	p.m = m;
	p.x = x;
	p.sum = 0;

	if (function_iterate_argument_values (sheet,
					      callback_function_seriessum,
					      &p, expr_node_list,
					      eval_col, eval_row,
					      error_string) == FALSE) {
		*error_string = _("#VALUE!");
		return NULL;
	}

	return value_new_float (p.sum);
}


FunctionDefinition math_functions [] = {
	{ "abs",     "f",    "number",    &help_abs,   NULL, gnumeric_abs },
	{ "acos",    "f",    "number",    &help_acos,  NULL, gnumeric_acos },
	{ "acosh",   "f",    "number",    &help_acosh, NULL, gnumeric_acosh },
	{ "asin",    "f",    "number",    &help_asin,  NULL, gnumeric_asin },
	{ "asinh",   "f",    "number",    &help_asinh, NULL, gnumeric_asinh },
	{ "atan",    "f",    "number",    &help_atan,  NULL, gnumeric_atan },
	{ "atanh",   "f",    "number",    &help_atanh, NULL, gnumeric_atanh },
	{ "atan2",   "ff",   "xnum,ynum", &help_atan2, NULL, gnumeric_atan2 },
	{ "cos",     "f",    "number",    &help_cos,     NULL, gnumeric_cos },
	{ "cosh",    "f",    "number",    &help_cosh,    NULL, gnumeric_cosh },
	{ "countif", "r?",   "range,criteria", &help_countif,
	  NULL, gnumeric_countif },
	{ "ceil",    "f",    "number",    &help_ceil,    NULL, gnumeric_ceil },
	{ "ceiling", "ff",   "number,significance",    &help_ceiling,
	  NULL, gnumeric_ceiling },
	{ "degrees", "f",    "number",    &help_degrees,
	  NULL, gnumeric_degrees },
	{ "even",    "f",    "number",    &help_even,    NULL, gnumeric_even },
	{ "exp",     "f",    "number",    &help_exp,     NULL, gnumeric_exp },
	{ "fact",    "f",    "number",    &help_fact,    NULL, gnumeric_fact },
	{ "factdouble", "f", "number",    &help_factdouble,    
	  NULL, gnumeric_factdouble },
	{ "combin",  "ff",   "n,k",       &help_combin,
	  NULL, gnumeric_combin },
	{ "floor",   "f",    "number",    &help_floor,
	  NULL, gnumeric_floor },
	{ "gcd",     "ff",   "number1,number2", &help_gcd,
	  NULL, gnumeric_gcd },
	{ "int",     "f",    "number",    &help_int,     NULL, gnumeric_int },
	{ "lcm",     0,      "",          &help_lcm,     gnumeric_lcm, NULL },
	{ "ln",      "f",    "number",    &help_ln,      NULL, gnumeric_ln },
	{ "log",     "f|f",  "number[,base]", &help_log, NULL, gnumeric_log },
	{ "log2",    "f",    "number",    &help_log2,    NULL, gnumeric_log2 },
	{ "log10",   "f",    "number",    &help_log10,
	  NULL, gnumeric_log10 },
	{ "mod",     "ff",   "num,denom", &help_mod,
	  NULL, gnumeric_mod },
	{ "mround",  "ff", "number,multiple", &help_mround,
	  NULL, gnumeric_mround },
	{ "multinomial", 0,  "",          &help_multinomial,
	  gnumeric_multinomial, NULL },
	{ "odd" ,    "f",    "number",    &help_odd,     NULL, gnumeric_odd },
	{ "power",   "ff",   "x,y",       &help_power,
	  NULL, gnumeric_power },
	{ "product", 0,      "number",    &help_product,
	  gnumeric_product, NULL },
	{ "quotient" , "ff",  "num,den",  &help_quotient,
	  NULL, gnumeric_quotient},
	{ "radians", "f",    "number",    &help_radians,
	  NULL, gnumeric_radians },
	{ "rand",    "",     "",          &help_rand,    NULL, gnumeric_rand },
	{ "randbetween", "ff", "bottom,top", &help_randbetween,
	  NULL, gnumeric_randbetween },
	{ "roman",      "f|f", "number[,type]", &help_roman,
	  NULL, gnumeric_roman },
	{ "round",      "f|f", "number[,digits]", &help_round,
	  NULL, gnumeric_round },
	{ "rounddown",  "f|f", "number,digits", &help_rounddown,
	  NULL, gnumeric_rounddown },
	{ "roundup",    "f|f", "number,digits", &help_roundup,
	  NULL, gnumeric_roundup },
	{ "seriessum", 0,    "x,n,m,coefficients",   &help_seriessum,
	  gnumeric_seriessum, NULL },
	{ "sign",    "f",    "number",    &help_sign,    NULL, gnumeric_sign },
	{ "sin",     "f",    "number",    &help_sin,     NULL, gnumeric_sin },
	{ "sinh",    "f",    "number",    &help_sinh,    NULL, gnumeric_sinh },
	{ "sqrt",    "f",    "number",    &help_sqrt,    NULL, gnumeric_sqrt },
	{ "sqrtpi",  "f",    "number",    &help_sqrtpi,
	  NULL, gnumeric_sqrtpi},
	{ "subtotal", 0,     "function_nbr,ref1,ref2,...",  &help_subtotal,
	  gnumeric_subtotal, NULL },
	{ "sum",     0,      "number1,number2,...",    &help_sum,
	  gnumeric_sum, NULL },
	{ "suma",    0,      "number1,number2,...",    &help_suma,
	  gnumeric_suma, NULL },
	{ "sumif",   "r?",   "range,criteria", &help_sumif,
	  NULL, gnumeric_sumif },
	{ "sumsq",   0,      "number",    &help_sumsq,
	  gnumeric_sumsq, NULL },
	{ "sumx2my2", "AA", "array1,array2", &help_sumx2my2,
	  NULL, gnumeric_sumx2my2 },
	{ "sumx2py2", "AA", "array1,array2", &help_sumx2py2,
	  NULL, gnumeric_sumx2py2 },
	{ "sumxmy2",  "AA", "array1,array2", &help_sumxmy2,
	  NULL, gnumeric_sumxmy2 },
	{ "tan",     "f",    "number",    &help_tan,     NULL, gnumeric_tan },
	{ "tanh",    "f",    "number",    &help_tanh,    NULL, gnumeric_tanh },
	{ "trunc",   "f",    "number",    &help_trunc,
	  gnumeric_trunc, NULL },
	{ "pi",      "",     "",          &help_pi,      NULL, gnumeric_pi },
	{ NULL, NULL },
};
