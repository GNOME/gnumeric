/*
 * fn-math.c:  Built in mathematical functions and functions registration
 *
 * Authors:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 */
#include <config.h>
#include <math.h>
#include "gnumeric.h"
#include "utils.h"
#include "func.h"
#include "mathfunc.h"
#include "collect.h"

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
gcd (int a, int b)
{
	/* Euclid's Algorithm.	Assumes non-negative numbers.  */

	while (b != 0) {
		int r;

		r = a - (a / b) * b;	/* r = remainder from
					 * dividing a by b	*/
		a = b;
		b = r;
	}
	return a;
}

static float_t
gpow10 (int n)
{
	float_t res = 1.0;
	float_t p;
	const int maxn = 300;

	if (n >= 0) {
		p = 10.0;
		n = (n > maxn) ? maxn : n;
	} else {
		p = 0.1;
		/* Note carefully that we avoid overflow.  */
		n = (n < -maxn) ? maxn : -n;
	}
	while (n > 0) {
		if (n & 1) res *= p;
		p *= p;
		n >>= 1;
	}
	return res;
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

static int
callback_function_makeslist (const EvalPosition *ep, Value *value,
			     ErrorMessage *error, void *closure)
{
        math_sums_t *mm = closure;
        float_t     x;
	gpointer    p;

        switch (value->type) {
	case VALUE_INTEGER:
	        x = value->v.v_int;
		break;
	case VALUE_FLOAT:
	        x = value->v.v_float;
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
		mm->list = g_slist_append (mm->list, v);
		mm->num++;
	} else
	        value_release(v);

	return TRUE;
}

static char *help_gcd = {
	N_("@FUNCTION=GCD\n"
	   "@SYNTAX=GCD(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "GCD returns the greatest common divisor of given numbers. "
	   "\n"
	   "If any of the arguments is less than zero, GCD returns #NUM! "
	   "error. "
	   "\n"
	   "@SEEALSO=LCM")
};

static Value *
gnumeric_gcd (FunctionEvalInfo *ei, GList *expr_node_list)
{
        math_sums_t p;
	GSList      *current;
        int         a, b, old_gcd, new_gcd;

	p.num  = 0;
	p.list = NULL;
	if (function_iterate_argument_values (&ei->pos, 
					      callback_function_makeslist,
					      &p, expr_node_list,
					      ei->error, TRUE) == FALSE) {
		if (error_message_is_set (ei->error))
			return function_error (ei, gnumeric_err_NUM);
	}

	if (p.list == NULL || p.list->next == NULL)
		return function_error (ei, gnumeric_err_NUM);

try_again:
	a = *((float_t *) p.list->data);
	current=p.list->next;
	b = *((float_t *) current->data);
	old_gcd = gcd(a, b);

	for (current=current->next; current!=NULL; current=current->next) {
	        b = *((float_t *) current->data);
		new_gcd = gcd(a, b);
		if (old_gcd != new_gcd) {
		        GSList *tmp;
			for (tmp=p.list; tmp!=NULL; tmp=tmp->next) {
			        b = *((float_t *) current->data);
				if (b % old_gcd == 0)
				        *((float_t *) current->data) = 
					  b / old_gcd;
			}
			goto try_again;
		}
	}

	for (current=p.list; current!=NULL; current=current->next)
	        g_free(current->data);
	g_slist_free(p.list);

	return value_new_int (old_gcd);
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
callback_function_lcm (const EvalPosition *ep, Value *value,
		       ErrorMessage *error, void *closure)
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
gnumeric_lcm (FunctionEvalInfo *ei, GList *nodes)
{
	Value *result;

	result = g_new (Value, 1);
	result->type = VALUE_INTEGER;
	result->v.v_int = 1;

	if (function_iterate_argument_values (&ei->pos, callback_function_lcm,
					      result, nodes,
					      ei->error, TRUE) == FALSE) {
		if (error_message_is_set (ei->error))
			return function_error (ei, gnumeric_err_NUM);
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
gnumeric_abs (FunctionEvalInfo *ei, Value **args)
{
	return value_new_float (fabs (value_get_as_float (args [0])));
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
gnumeric_acos (FunctionEvalInfo *ei, Value **args)
{
	float_t t;

	t = value_get_as_float (args [0]);
	if ((t < -1.0) || (t > 1.0))
		return function_error (ei, _("acos - domain error"));

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
gnumeric_acosh (FunctionEvalInfo *ei, Value **args)
{
	float_t t;

	t = value_get_as_float (args [0]);
	if (t < 1.0)
		return function_error (ei, _("acosh - domain error"));

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
gnumeric_asin (FunctionEvalInfo *ei, Value **args)
{
	float_t t;

	t = value_get_as_float (args [0]);
	if ((t < -1.0) || (t > 1.0))
		return function_error (ei, _("asin - domain error"));

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
gnumeric_asinh (FunctionEvalInfo *ei, Value **args)
{
	return value_new_float (asinh (value_get_as_float (args [0])));
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
gnumeric_atan (FunctionEvalInfo *ei, Value **args)
{
	return value_new_float (atan (value_get_as_float (args [0])));
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
gnumeric_atanh (FunctionEvalInfo *ei, Value **args)
{
	float_t t;

	t = value_get_as_float (args [0]);
	if ((t <= -1.0) || (t >= 1.0))
		return function_error (ei, _("atanh: domain error"));

	return value_new_float (atanh (value_get_as_float (args [0])));
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
gnumeric_atan2 (FunctionEvalInfo *ei, Value **args)
{
	return value_new_float (atan2 (value_get_as_float (args [1]),
				       value_get_as_float (args [0])));
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
gnumeric_ceil (FunctionEvalInfo *ei, Value **args)
{
	return value_new_float (ceil (value_get_as_float (args [0])));
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
gnumeric_countif (FunctionEvalInfo *ei, Value **argv)
{
        Value           *range = argv[0];
	Value           *tmpvalue = NULL;

	math_criteria_t items;
	int             ret;
	GSList          *list;

	items.num  = 0;
	items.list = NULL;

	if ((!VALUE_IS_NUMBER(argv[1]) && argv[1]->type != VALUE_STRING)
	    || (range->type != VALUE_CELLRANGE))
	        return function_error (ei, gnumeric_err_VALUE);

	if (VALUE_IS_NUMBER(argv[1])) {
	        items.fun = (criteria_test_fun_t) criteria_test_equal;
		items.test_value = argv[1];
	} else {
	        parse_criteria(argv[1]->v.str->str,
			       &items.fun, &items.test_value);
		tmpvalue = items.test_value;
	}

	ret = sheet_cell_foreach_range (
		range->v.cell_range.cell_a.sheet, TRUE,
		range->v.cell_range.cell_a.col,
		range->v.cell_range.cell_a.row,
		range->v.cell_range.cell_b.col,
		range->v.cell_range.cell_b.row,
		callback_function_criteria,
		&items);

	if (tmpvalue)
		value_release (tmpvalue);

	if (ret == FALSE)
	        return function_error (ei, gnumeric_err_VALUE);

        list = items.list;

	while (list != NULL) {
		value_release (list->data);
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
gnumeric_sumif (FunctionEvalInfo *ei, Value **argv)
{
        Value           *range = argv[0];
	Value           *tmpvalue = NULL;

	math_criteria_t items;
	int             ret;
	float_t         sum;
	GSList          *list;

	items.num  = 0;
	items.list = NULL;

	if ((!VALUE_IS_NUMBER(argv[1]) && argv[1]->type != VALUE_STRING)
	    || (range->type != VALUE_CELLRANGE))
	        return function_error (ei, gnumeric_err_VALUE);

	if (VALUE_IS_NUMBER(argv[1])) {
	        items.fun = (criteria_test_fun_t) criteria_test_equal;
		items.test_value = argv[1];
	} else {
	        parse_criteria(argv[1]->v.str->str,
			       &items.fun, &items.test_value);
		tmpvalue = items.test_value;
	}

	ret = sheet_cell_foreach_range (
	  range->v.cell_range.cell_a.sheet, TRUE,
	  range->v.cell_range.cell_a.col,
	  range->v.cell_range.cell_a.row,
	  range->v.cell_range.cell_b.col,
	  range->v.cell_range.cell_b.row,
	  callback_function_criteria,
	  &items);

	if (tmpvalue)
		value_release (tmpvalue);

	if (ret == FALSE)
	        return function_error (ei, gnumeric_err_VALUE);

        list = items.list;
	sum = 0;

	while (list != NULL) {
	        Value *v = list->data;

		if (v != NULL)
		       sum += value_get_as_float (v);
		value_release (v);
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
gnumeric_ceiling (FunctionEvalInfo *ei, Value **argv)
{
        float_t number, s;

	number = value_get_as_float (argv[0]);
	if (argv[1] == NULL)
	        s = (number >= 0) ? 1.0 : -1.0;
	else {
	        s = value_get_as_float (argv[1]);
	}

	if (s == 0 || number / s < 0)
		return function_error (ei, gnumeric_err_NUM);

	return value_new_float (ceil (number / s) * s);
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
gnumeric_cos (FunctionEvalInfo *ei, Value **argv)
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
gnumeric_cosh (FunctionEvalInfo *ei, Value **argv)
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
gnumeric_degrees (FunctionEvalInfo *ei, Value **argv)
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
gnumeric_exp (FunctionEvalInfo *ei, Value **argv)
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
gnumeric_fact (FunctionEvalInfo *ei, Value **argv)
{
	float_t x;
	gboolean x_is_integer;

	if (!VALUE_IS_NUMBER (argv[0]))
		return function_error (ei, gnumeric_err_NUM);

	x = value_get_as_float (argv[0]);
	if (x < 0)
		return function_error (ei, gnumeric_err_NUM);

	x_is_integer = (x == floor (x));

	if (x > 12 || !x_is_integer) {
		float_t res = exp (lgamma (x + 1));
		if (x_is_integer)
			res = floor (res + 0.5);  /* Round, just in case.  */
		return value_new_float (res);
	} else
		return value_new_int (fact (x));
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
gnumeric_combin (FunctionEvalInfo *ei, Value **argv)
{
	int n ,k;

	n = value_get_as_int (argv[0]);
	k = value_get_as_int (argv[1]);

	if (k >= 0 && n >= k)
		return value_new_float (combin (n ,k));

	return function_error (ei, gnumeric_err_NUM);
}

static char *help_floor = {
	N_("@FUNCTION=FLOOR\n"
	   "@SYNTAX=FLOOR(x,significance)\n"

	   "@DESCRIPTION=The FLOOR function rounds x down to the next nearest "
	   "multiple of @significance.  @significance defaults to 1."
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=CEIL, ABS, INT")
};

static Value *
gnumeric_floor (FunctionEvalInfo *ei, Value **argv)
{
        float_t number, s;

	number = value_get_as_float (argv[0]);
	if (argv[1] == NULL)
	        s = (number >= 0) ? 1.0 : -1.0;
	else {
	        s = value_get_as_float (argv[1]);
	}

	if (s == 0 || number / s < 0)
		return function_error (ei, gnumeric_err_NUM);

	return value_new_float (floor (number / s) * s);
}

static char *help_int = {
	N_("@FUNCTION=INT\n"
	   "@SYNTAX=INT(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "The INT function round b1 now to the nearest int. "
	   "Where 'nearest' implies being closer to zero. "
	   "Equivalent to FLOOR(b1) for b1 >= 0, amd CEIL(b1) "
	   "for b1 < 0. "
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   ""
	   "\n"
	   "@SEEALSO=FLOOR, CEIL, ABS")
};

static Value *
gnumeric_int (FunctionEvalInfo *ei, Value **argv)
{
	float_t t;

	/* FIXME: What about strings and empty cells?  */
	t = value_get_as_float (argv [0]);

	return value_new_float (floor (value_get_as_float (argv [0])));
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
gnumeric_log (FunctionEvalInfo *ei, Value **argv)
{
	float_t t, base;

	t = value_get_as_float (argv [0]);

	if (argv[1] == NULL)
	        base = 10;
	else
	        base = value_get_as_float (argv[1]);

	if (t <= 0.0)
		return function_error (ei, gnumeric_err_VALUE);

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
gnumeric_ln (FunctionEvalInfo *ei, Value **argv)
{
	float_t t;

	t = value_get_as_float (argv [0]);

	if (t <= 0.0)
		return function_error (ei, gnumeric_err_VALUE);

	return value_new_float (log (t));
}

static char *help_power = {
	N_("@FUNCTION=POWER\n"
	   "@SYNTAX=POWER(x,y)\n"

	   "@DESCRIPTION="
	   "Returns the value of x raised to the power y."
	   "\n"
	   "Performing this function on a string or empty cell returns an error."
	   "\n"
	   "@SEEALSO=EXP")
};

static Value *
gnumeric_power (FunctionEvalInfo *ei, Value **argv)
{
	float_t x, y;

	x = value_get_as_float (argv [0]);
	y = value_get_as_float (argv [1]);

	if ((x > 0) || (x == 0 && y > 0) || (x < 0 && y == floor (y)))
		return value_new_float (pow (x, y));

	/* FIXME: What is supposed to happen for x=y=0?  */
	return function_error (ei, gnumeric_err_VALUE);
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
gnumeric_log2 (FunctionEvalInfo *ei, Value **argv)
{
	float_t t;

	t = value_get_as_float (argv [0]);
	if (t <= 0.0)
		return function_error (ei, _("log2: domain error"));

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
gnumeric_log10 (FunctionEvalInfo *ei, Value **argv)
{
	float_t t;

	t = value_get_as_float (argv [0]);
	if (t <= 0.0)
		return function_error (ei, _("log10: domain error"));

	return value_new_float (log10 (t));
}

static char *help_mod = {
	N_("@FUNCTION=MOD\n"
	   "@SYNTAX=MOD(number,divisor)\n"

	   "@DESCRIPTION="
	   "Implements modulo arithmetic."
	   "Returns the remainder when @divisor is divided into @number."
	   "\n"
	   "Returns #DIV/0! if divisor is zero."
	   "@SEEALSO=INT,FLOOR,CEIL")
};

static Value *
gnumeric_mod (FunctionEvalInfo *ei, Value **argv)
{
	int a,b;

	a = value_get_as_int (argv[0]);
	b = value_get_as_int (argv[1]);

	if (b == 0)
		return function_error (ei, gnumeric_err_NUM);

	if (b < 0) {
		a = -a;
		b = -b;
		/* FIXME: check for overflow.  */
	}

	if (a >= 0)
		return value_new_int (a % b);
	else {
		int invres = (-a) % b;
		return value_new_int (invres == 0 ? 0 : b - invres);
	}
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
gnumeric_radians (FunctionEvalInfo *ei, Value **argv)
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
gnumeric_rand (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (random_01 ());
}

static char *help_sin = {
	N_("@FUNCTION=SIN\n"
	   "@SYNTAX=SIN(x)\n"

	   "@DESCRIPTION="
	   "The SIN function returns the sine of x, where x is given "
           " in radians."
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=COS, COSH, SINH, TAN, TANH, RADIANS, DEGREES")
};

static Value *
gnumeric_sin (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (sin (value_get_as_float (argv [0])));
}

static char *help_sinh = {
	N_("@FUNCTION=SINH\n"
	   "@SYNTAX=SINH(x)\n"

	   "@DESCRIPTION="
	   "The SINH function returns the hyperbolic sine of @x, "
	   "which is defined mathematically as (exp(x) - exp(-x)) / 2."
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=SIN, COS, COSH, TAN, TANH, DEGREES, RADIANS, EXP")
};

static Value *
gnumeric_sinh (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (sinh (value_get_as_float (argv [0])));
}

static char *help_sqrt = {
	N_("@FUNCTION=SQRT\n"
	   "@SYNTAX=SQRT(x)\n"

	   "@DESCRIPTION="
	   "The SQRT function returns the square root of @x."
	   "\n"
	   "If x is negative returns #NUM!."
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=POW")
};

static Value *
gnumeric_sqrt (FunctionEvalInfo *ei, Value **argv)
{
	float_t x = value_get_as_float (argv[0]);
	if (x < 0)
		return function_error (ei, gnumeric_err_NUM);

	return value_new_float (sqrt(x));
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

Value *
gnumeric_sum (FunctionEvalInfo *ei, GList *nodes)
{
	return float_range_function (nodes, ei,
				     range_sum,
				     COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS,
				     gnumeric_err_VALUE, ei->error);
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
gnumeric_suma (FunctionEvalInfo *ei, GList *nodes)
{
	return float_range_function (nodes, ei,
				     range_sum,
				     COLLECT_ZERO_STRINGS | COLLECT_ZEROONE_BOOLS,
				     gnumeric_err_VALUE, ei->error);
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


static Value *
gnumeric_sumsq (FunctionEvalInfo *ei, GList *nodes)
{
	return float_range_function (nodes, ei,
				     range_sumsq,
				     COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS,
				     gnumeric_err_VALUE, ei->error);
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
callback_function_multinomial (const EvalPosition *ep, Value *value,
			       ErrorMessage *error, void *closure)
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
gnumeric_multinomial (FunctionEvalInfo *ei, GList *nodes)
{
        math_multinomial_t p;

	p.num = 0;
	p.sum = 0;
	p.product = 1;

	if (function_iterate_argument_values (&ei->pos,
					      callback_function_multinomial,
					      &p, nodes,
					      ei->error, TRUE) == FALSE)
	        return function_error (ei, gnumeric_err_VALUE);

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

static Value *
gnumeric_product (FunctionEvalInfo *ei, GList *nodes)
{
	return float_range_function (nodes, ei,
				     range_product,
				     COLLECT_IGNORE_STRINGS | COLLECT_IGNORE_BOOLS,
				     gnumeric_err_VALUE, ei->error);
}

static char *help_tan = {
	N_("@FUNCTION=TAN\n"
	   "@SYNTAX=TAN(x)\n"

	   "@DESCRIPTION="
	   "The TAN function  returns the tangent of @x, where @x is "
	   "given in radians."
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing. "
	   "This function only takes one argument."
	   "\n"
	   "@SEEALSO=TANH, COS, COSH, SIN, SINH, DEGREES, RADIANS")
};

static Value *
gnumeric_tan (FunctionEvalInfo *ei, Value **argv)
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
gnumeric_tanh (FunctionEvalInfo *ei, Value **argv)
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
gnumeric_pi (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (M_PI);
}

static char *help_trunc = {
	N_("@FUNCTION=TRUNC\n"
	   "@SYNTAX=TRUNC(number[,digits])\n"

	   "@DESCRIPTION=The TRUNC function returns the value of @number "
	   "truncated to the number of digits specified.  If @digits is omitted "
	   "then @digits defaults to zero."
	   "\n"

	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_trunc (FunctionEvalInfo *ei, Value **argv)
{
        float_t number, p10;
        int digits;

	number = value_get_as_float (argv[0]);
	if (argv[1] == NULL)
	        digits = 0;
	else
	        digits = value_get_as_int (argv[1]);

	p10 = gpow10 (digits);
	if (number < 0)
		return value_new_float (-floor (-number * p10) / p10);
	else
		return value_new_float (floor (number * p10) / p10);
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
gnumeric_even (FunctionEvalInfo *ei, Value **argv)
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
gnumeric_odd (FunctionEvalInfo *ei, Value **argv)
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
gnumeric_factdouble (FunctionEvalInfo *ei, Value **argv)

{
        int number;
	int n;
	float_t product = 1;

	number = value_get_as_int (argv[0]);
	if (number < 0)
		return function_error (ei, gnumeric_err_NUM );

	for (n = number; n > 0; n-= 2)
	        product *= n;

	return value_new_float (product);
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
gnumeric_quotient (FunctionEvalInfo *ei, Value **argv)
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
gnumeric_sign (FunctionEvalInfo *ei, Value **argv)
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
gnumeric_sqrtpi (FunctionEvalInfo *ei, Value **argv)
{
        float_t n;

	n = value_get_as_float (argv[0]);
	if (n < 0)
		return function_error (ei, gnumeric_err_NUM);

	return value_new_float (sqrt (M_PI * n));
}

static char *help_randbetween = {
	N_("@FUNCTION=RANDBETWEEN\n"
	   "@SYNTAX=RANDBETWEEN(bottom,top)\n"

	   "@DESCRIPTION=RANDBETWEEN function returns a random integer number "
	   "between @bottom and @top.\n"
	   "If @bottom or @top is non-integer, they are truncated. "
	   "If @bottom > @top, RANDBETWEEN returns #NUM! error.\n"
	   "@SEEALSO=RAND")
};

static Value *
gnumeric_randbetween (FunctionEvalInfo *ei, Value **argv)
{
        int bottom, top;
	double r;

	bottom = value_get_as_int (argv[0]);
	top    = value_get_as_int (argv[1]);
	if (bottom > top)
		return function_error (ei, gnumeric_err_NUM );

	r = bottom + floor ((top + 1.0 - bottom) * random_01 ());
	return value_new_int ((int)r);
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
gnumeric_rounddown (FunctionEvalInfo *ei, Value **argv)
{
        float_t number, p10;
        int digits;

	number = value_get_as_float (argv[0]);
	if (argv[1] == NULL)
	        digits = 0;
	else
	        digits = value_get_as_int (argv[1]);

	p10 = gpow10 (digits);
	if (number < 0)
		return value_new_float (-ceil (-number * p10) / p10);
	else
		return value_new_float (floor (number * p10) / p10);
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
gnumeric_round (FunctionEvalInfo *ei, Value **argv)
{
        float_t number, p10;
        int     digits;

	number = value_get_as_float (argv[0]);
	if (argv[1] == NULL)
	        digits = 0;
	else
	        digits = value_get_as_int (argv[1]);

	p10 = gpow10 (digits);
	return value_new_float (rint (number * p10) / p10);
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
gnumeric_roundup (FunctionEvalInfo *ei, Value **argv)
{
        float_t number, p10;
        int digits;

	number = value_get_as_float (argv[0]);
	if (argv[1] == NULL)
	        digits = 0;
	else
	        digits = value_get_as_int (argv[1]);

	p10 = gpow10 (digits);
	if (number < 0)
		return value_new_float (-floor (-number * p10) / p10);
	else
		return value_new_float (ceil (number * p10) / p10);
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
gnumeric_mround (FunctionEvalInfo *ei, Value **argv)
{
        const float_t accuracy_limit = 0.0000003;
        float_t number, multiple;
	float_t div, mod;
	int     sign = 1;

	number = value_get_as_float (argv[0]);
	multiple = value_get_as_float (argv[1]);

	if ((number > 0 && multiple < 0)
	    || (number < 0 && multiple > 0))
		return function_error (ei, gnumeric_err_NUM);

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
gnumeric_roman (FunctionEvalInfo *ei, Value **argv)
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

	if (n < 0 || n > 3999)
		return function_error (ei, gnumeric_err_VALUE );

	if (n == 0)
		return value_new_string ("");

	if (form < 0 || form > 4)
		return function_error (ei, gnumeric_err_NUM );

	if (form > 0)
		return function_error (ei, _("#Unimplemented!") );

	for (i = j = 0; dec > 1; dec /= 10, j+=2){
	        for (; n > 0; i++){
		        if (n >= dec){
			        buf[i] = letter [j];
				n -= dec;
			} else if (n >= dec - dec/10){
			        buf [i++] = letter [j+2];
				buf [i] = letter [j];
				n -= dec - dec/10;
			} else if (n >= dec/2){
			        buf [i] = letter [j+1];
				n -= dec/2;
			} else if (n >= dec/2 - dec/10){
			        buf [i++] = letter [j+2];
				buf [i] = letter [j+1];
				n -= dec/2 - dec/10;
			} else if (dec == 10){
			        buf [i] = letter [j+2];
				n--;
			} else
			        break;
		}
	}
	buf [i] = '\0';

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
gnumeric_sumx2my2 (FunctionEvalInfo *ei, Value **argv)
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

		if (ret == FALSE)
		        return function_error (ei, gnumeric_err_VALUE);
	} else
		return function_error (ei, _("Array version not implemented!"));

        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  values_y->v.cell_range.cell_a.sheet, TRUE,
		  values_y->v.cell_range.cell_a.col,
		  values_y->v.cell_range.cell_a.row,
		  values_y->v.cell_range.cell_b.col,
		  values_y->v.cell_range.cell_b.row,
		  callback_function_sumxy,
		  &items_y);
		if (ret == FALSE)
		        return function_error (ei, gnumeric_err_VALUE);
	} else
		return function_error (ei, _("Array version not implemented!"));

	if (items_x.num != items_y.num)
		return function_error (ei, gnumeric_err_NA);

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
gnumeric_sumx2py2 (FunctionEvalInfo *ei, Value **argv)
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
		if (ret == FALSE)
		        return function_error (ei, gnumeric_err_VALUE);
	} else
		return function_error (ei, _("Array version not implemented!"));

        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  values_y->v.cell_range.cell_a.sheet, TRUE,
		  values_y->v.cell_range.cell_a.col,
		  values_y->v.cell_range.cell_a.row,
		  values_y->v.cell_range.cell_b.col,
		  values_y->v.cell_range.cell_b.row,
		  callback_function_sumxy,
		  &items_y);
		if (ret == FALSE)
		        return function_error (ei, gnumeric_err_VALUE);
	} else
		return function_error (ei, _("Array version not implemented!"));

	if (items_x.num != items_y.num)
		return function_error (ei, gnumeric_err_NA);

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
gnumeric_sumxmy2 (FunctionEvalInfo *ei, Value **argv)
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
		if (ret == FALSE)
		        return function_error (ei, gnumeric_err_VALUE);
	} else
		return function_error (ei, _("Array version not implemented!"));

        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
		  values_y->v.cell_range.cell_a.sheet, TRUE,
		  values_y->v.cell_range.cell_a.col,
		  values_y->v.cell_range.cell_a.row,
		  values_y->v.cell_range.cell_b.col,
		  values_y->v.cell_range.cell_b.row,
		  callback_function_sumxy,
		  &items_y);
		if (ret == FALSE)
		        return function_error (ei, gnumeric_err_VALUE);
	} else
		return function_error (ei, _("Array version not implemented!"));

	if (items_x.num != items_y.num)
	        return function_error (ei, gnumeric_err_NA);

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
gnumeric_subtotal (FunctionEvalInfo *ei, GList *expr_node_list)
{
        ExprTree *tree;
	Value    *val;
	int      fun_nbr;

	if (expr_node_list == NULL)
		return function_error (ei, gnumeric_err_NUM);

	tree = (ExprTree *) expr_node_list->data;
	if (tree == NULL)
		return function_error (ei, gnumeric_err_NUM);

	val = eval_expr (ei, tree);
	if (!val) return NULL;
	if (!VALUE_IS_NUMBER (val)) {
		value_release (val);
		return function_error (ei, gnumeric_err_VALUE);
	}

	fun_nbr = value_get_as_int(val);
	if (fun_nbr < 1 || fun_nbr > 11)
		return function_error (ei, gnumeric_err_NUM);

	/* Skip the first node */
	expr_node_list = expr_node_list->next;

	switch (fun_nbr) {
	case 1:
	        return gnumeric_average(ei, expr_node_list);
	case 2:
	        return gnumeric_count(ei, expr_node_list);
	case 3:
		return gnumeric_counta(ei, expr_node_list);
	case 4:
		return gnumeric_max(ei, expr_node_list);
	case 5:
		return gnumeric_min(ei, expr_node_list);
	case 6:
	        return gnumeric_product(ei, expr_node_list);
	case 7:
	        return gnumeric_stdev(ei, expr_node_list);
	case 8:
	        return gnumeric_stdevp(ei, expr_node_list);
	case 9:
	        return gnumeric_sum(ei, expr_node_list);
	case 10:
	        return gnumeric_var(ei, expr_node_list);
	case 11:
	        return gnumeric_varp(ei, expr_node_list);
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
callback_function_seriessum (const EvalPosition *ep, Value *value,
			     ErrorMessage *error, void *closure)
{
	math_seriessum_t *mm = closure;
	float_t coefficient;

	if (!VALUE_IS_NUMBER (value))
		return FALSE;

	coefficient = value_get_as_float (value);

	mm->sum += coefficient * pow(mm->x, mm->n);
	mm->n += mm->m;

	return TRUE;
}

static Value *
gnumeric_seriessum (FunctionEvalInfo *ei, GList *nodes)
{
        math_seriessum_t p;
        ExprTree         *tree;
	Value            *val;
	float_t          x, n, m;

	if (nodes == NULL)
		return function_error (ei, gnumeric_err_NUM);

	/* Get x */
	tree = (ExprTree *) nodes->data;
	if (tree == NULL)
		return function_error (ei, gnumeric_err_NUM);

	val = eval_expr (ei, tree);
	if (!val) return NULL;
	if (! VALUE_IS_NUMBER(val)) {
		value_release (val);
		return function_error (ei, gnumeric_err_VALUE);
	}

	x = value_get_as_float (val);
	value_release (val);
	nodes = nodes->next;

	/* Get n */
	tree = (ExprTree *) nodes->data;
	if (tree == NULL)
		return function_error (ei, gnumeric_err_NUM);

	val = eval_expr (ei, tree);
	if (!val) return NULL;
	if (! VALUE_IS_NUMBER(val)) {
		value_release (val);
		return function_error (ei, gnumeric_err_VALUE);
	}

	n = value_get_as_int(val);
	value_release (val);
	nodes = nodes->next;

	/* Get m */
	tree = (ExprTree *) nodes->data;
	if (tree == NULL)
		return function_error (ei, gnumeric_err_NUM);

	val = eval_expr (ei, tree);
	if (!val) return NULL;
	if (! VALUE_IS_NUMBER(val)) {
		value_release (val);
		return function_error (ei, gnumeric_err_VALUE);
	}

	m = value_get_as_float(val);
	nodes = nodes->next;

	p.n = n;
	p.m = m;
	p.x = x;
	p.sum = 0;

	if (function_iterate_argument_values (&ei->pos,
					      callback_function_seriessum,
					      &p, nodes,
					      ei->error, TRUE) == FALSE)
		return function_error (ei, gnumeric_err_VALUE);

	return value_new_float (p.sum);
}

static char *help_sumproduct = {
	N_("@FUNCTION=SUMPRODUCT\n"
	   "@SYNTAX=SUMPRODUCT(range1,range2,...)\n"
	   "@DESCRIPTION="
	   "SUMPRODUCT function multiplies corresponding data entries in the "
	   "given arrays or ranges, and then returns the sum of those "
	   "products.  If an array entry is not numeric, the value zero is "
	   "used instead. "
	   "\n"
	   "If array or range arguments do not have the same dimentions, "
	   "SUMPRODUCT returns #VALUE! error. "
	   "\n"
	   "@SEEALSO=SUM,PRODUCT")
};

typedef struct {
        GSList   *components;
        GSList   *current;
        gboolean first;
} math_sumproduct_t;

static int
callback_function_sumproduct (const EvalPosition *ep, Value *value,
			      ErrorMessage *error, void *closure)
{
	math_sumproduct_t *mm = closure;
	float_t           x;

	if (!VALUE_IS_NUMBER (value))
		x = 0;
	else
	        x = value_get_as_float (value);

	if (mm->first) {
	        gpointer p;
		p = g_new(float_t, 1);
		*((float_t *) p) = x;
		mm->components = g_slist_append(mm->components, p);
	} else {
	        if (mm->current == NULL)
		        return FALSE;
		*((float_t *) mm->current->data) *= x;
		mm->current = mm->current->next;
	}

	return TRUE;
}

static Value *
gnumeric_sumproduct (FunctionEvalInfo *ei, GList *expr_node_list)
{
        math_sumproduct_t p;
	GSList            *current;
	float_t           sum;
	int               result=1;

	if (expr_node_list == NULL)
		return function_error (ei, gnumeric_err_NUM);

	p.components = NULL;
	p.first      = TRUE;

	for ( ; result && expr_node_list;
	      expr_node_list = expr_node_list->next) {
		ExprTree *tree = (ExprTree *) expr_node_list->data;
		Value    *val;

		val = eval_expr (ei, tree);

		if (val) {
		        p.current = p.components;
			result = function_iterate_do_value (
				&ei->pos, callback_function_sumproduct, &p,
				val, ei->error, TRUE);

			value_release (val);
		} else
		        break;
		p.first = FALSE;
	}

	sum = 0;
	current = p.components;
	while (current != NULL) {
	        gpointer p = current->data;
	        sum += *((float_t *) p);
		g_free(current->data);
	        current = current->next;
	}
	
	g_slist_free(p.components);

	if (expr_node_list)
		return function_error (ei, gnumeric_err_VALUE);

	return value_new_float (sum);
}

void math_functions_init()
{
	FunctionCategory *cat = function_get_category (_("Maths / Trig."));

	function_add_args  (cat, "abs",     "f",    "number",    &help_abs,   gnumeric_abs);
	function_add_args  (cat, "acos",    "f",    "number",    &help_acos,  gnumeric_acos);
	function_add_args  (cat, "acosh",   "f",    "number",    &help_acosh, gnumeric_acosh);
	function_add_args  (cat, "asin",    "f",    "number",    &help_asin,  gnumeric_asin);
	function_add_args  (cat, "asinh",   "f",    "number",    &help_asinh, gnumeric_asinh);
	function_add_args  (cat, "atan",    "f",    "number",    &help_atan,  gnumeric_atan);
	function_add_args  (cat, "atanh",   "f",    "number",    &help_atanh, gnumeric_atanh);
	function_add_args  (cat, "atan2",   "ff",   "xnum,ynum", &help_atan2, gnumeric_atan2);
	function_add_args  (cat, "cos",     "f",    "number",    &help_cos,     gnumeric_cos);
	function_add_args  (cat, "cosh",    "f",    "number",    &help_cosh,    gnumeric_cosh);
	function_add_args  (cat, "countif", "r?",   "range,criteria", &help_countif,
			    gnumeric_countif);
	function_add_args  (cat, "ceil",    "f",    "number",    &help_ceil,    gnumeric_ceil);
	function_add_args  (cat, "ceiling", "ff",   "number,significance",    &help_ceiling,
			    gnumeric_ceiling);
	function_add_args  (cat, "degrees", "f",    "number",    &help_degrees,
			    gnumeric_degrees);
	function_add_args  (cat, "even",    "f",    "number",    &help_even,    gnumeric_even);
	function_add_args  (cat, "exp",     "f",    "number",    &help_exp,     gnumeric_exp);
	function_add_args  (cat, "fact",    "f",    "number",    &help_fact,    gnumeric_fact);
	function_add_args  (cat, "factdouble", "f", "number",    &help_factdouble,
			    gnumeric_factdouble);
	function_add_args  (cat, "combin",  "ff",   "n,k",       &help_combin,
			    gnumeric_combin);
	function_add_args  (cat, "floor",   "f|f",  "number",    &help_floor,
			    gnumeric_floor);
	function_add_nodes (cat, "gcd",     "ff",   "number1,number2", &help_gcd,
			    gnumeric_gcd);
	function_add_args  (cat, "int",     "f",    "number",    &help_int,      gnumeric_int);
	function_add_nodes (cat, "lcm",     0,      "",          &help_lcm,      gnumeric_lcm);
	function_add_args  (cat, "ln",      "f",    "number",    &help_ln,       gnumeric_ln);
	function_add_args  (cat, "log",     "f|f",  "number[,base]", &help_log,  gnumeric_log);
	function_add_args  (cat, "log2",    "f",    "number",    &help_log2,     gnumeric_log2);
	function_add_args  (cat, "log10",   "f",    "number",    &help_log10,
			    gnumeric_log10);
	function_add_args  (cat, "mod",     "ff",   "num,denom", &help_mod,
			    gnumeric_mod);
	function_add_args  (cat, "mround",  "ff", "number,multiple", &help_mround,
			    gnumeric_mround);
	function_add_nodes (cat, "multinomial", 0,  "",          &help_multinomial,
			    gnumeric_multinomial);
	function_add_args  (cat, "odd" ,    "f",    "number",    &help_odd,     gnumeric_odd);
	function_add_args  (cat, "power",   "ff",   "x,y",       &help_power,
			    gnumeric_power);
	function_add_nodes (cat, "product", 0,      "number",    &help_product,
			    gnumeric_product);
	function_add_args  (cat, "quotient" , "ff",  "num,den",  &help_quotient,
			    gnumeric_quotient);
	function_add_args  (cat, "radians", "f",    "number",    &help_radians,
			    gnumeric_radians);
	function_add_args  (cat, "rand",    "",     "",          &help_rand,    gnumeric_rand);
	function_add_args  (cat, "randbetween", "ff", "bottom,top", &help_randbetween,
			    gnumeric_randbetween);
	function_add_args  (cat, "roman",      "f|f", "number[,type]", &help_roman,
			    gnumeric_roman);
	function_add_args  (cat, "round",      "f|f", "number[,digits]", &help_round,
			    gnumeric_round);
	function_add_args  (cat, "rounddown",  "f|f", "number,digits", &help_rounddown,
			    gnumeric_rounddown);
	function_add_args  (cat, "roundup",    "f|f", "number,digits", &help_roundup,
			    gnumeric_roundup);
	function_add_nodes (cat, "seriessum", 0,    "x,n,m,coefficients",   &help_seriessum,
			    gnumeric_seriessum);
	function_add_args  (cat, "sign",    "f",    "number",    &help_sign,    gnumeric_sign);
	function_add_args  (cat, "sin",     "f",    "number",    &help_sin,     gnumeric_sin);
	function_add_args  (cat, "sinh",    "f",    "number",    &help_sinh,    gnumeric_sinh);
	function_add_args  (cat, "sqrt",    "f",    "number",    &help_sqrt,    gnumeric_sqrt);
	function_add_args  (cat, "sqrtpi",  "f",    "number",    &help_sqrtpi,  gnumeric_sqrtpi);
	function_add_nodes (cat, "subtotal", 0,     "function_nbr,ref1,ref2,...",  &help_subtotal,
			    gnumeric_subtotal);
	function_add_nodes (cat, "sum",     0,      "number1,number2,...",    &help_sum,
			    gnumeric_sum);
	function_add_nodes (cat, "suma",    0,      "number1,number2,...",    &help_suma,
			    gnumeric_suma);
	function_add_args  (cat, "sumif",   "r?",   "range,criteria", &help_sumif, gnumeric_sumif);
	function_add_nodes (cat, "sumproduct",   0, "range1,range2,...",
			    &help_sumproduct, gnumeric_sumproduct);
	function_add_nodes (cat, "sumsq",   0,      "number",    &help_sumsq,
			    gnumeric_sumsq);
	function_add_args  (cat, "sumx2my2", "AA", "array1,array2", &help_sumx2my2, gnumeric_sumx2my2);
	function_add_args  (cat, "sumx2py2", "AA", "array1,array2", &help_sumx2py2, gnumeric_sumx2py2);
	function_add_args  (cat, "sumxmy2",  "AA", "array1,array2", &help_sumxmy2,  gnumeric_sumxmy2);
	function_add_args  (cat, "tan",     "f",    "number",    &help_tan,     gnumeric_tan);
	function_add_args  (cat, "tanh",    "f",    "number",    &help_tanh,    gnumeric_tanh);
	function_add_args  (cat, "trunc",   "f|f",  "number,digits",    &help_trunc,   gnumeric_trunc);
	function_add_args  (cat, "pi",      "",     "",          &help_pi,      gnumeric_pi);
}
