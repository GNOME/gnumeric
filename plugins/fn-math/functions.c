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
#include "auto-format.h"

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


typedef struct {
        GSList *list;
        int    num;
} math_sums_t;

static Value *
callback_function_sumxy (Sheet *sheet, int col, int row,
			 Cell *cell, void *user_data)
{
        math_sums_t *mm = user_data;
        float_t     x;
	gpointer    p;

	if (cell == NULL || cell->value == NULL)
	        return NULL;

        switch (cell->value->type) {
	case VALUE_ERROR:
		return value_terminate();

	case VALUE_BOOLEAN:
	        x = cell->value->v.v_bool ? 1 : 0;
		break;
	case VALUE_INTEGER:
	        x = cell->value->v.v_int;
		break;
	case VALUE_FLOAT:
	        x = cell->value->v.v_float;
		break;
	case VALUE_EMPTY:
	default:
	        return NULL;
	}

	p = g_new(float_t, 1);
	*((float_t *) p) = x;
	mm->list = g_slist_append(mm->list, p);
	mm->num++;

	return NULL;
}

typedef struct {
        GSList              *list;
        criteria_test_fun_t fun;
        Value               *test_value;
        int                 num;
        int                 total_num;
        gboolean            actual_range;
        float_t             sum;
        GSList              *current;
} math_criteria_t;

static Value *
callback_function_criteria (Sheet *sheet, int col, int row,
			    Cell *cell, void *user_data)
{
        math_criteria_t *mm = user_data;
	Value           *v;
	gpointer        n;

	mm->total_num++;
	if (cell == NULL || cell->value == NULL)
	        return NULL;

        switch (cell->value->type) {
	case VALUE_BOOLEAN:
	        v = value_new_bool (cell->value->v.v_bool);
		break;
	case VALUE_INTEGER:
	        v = value_new_int (cell->value->v.v_int);
		break;
	case VALUE_FLOAT:
	        v = value_new_float (cell->value->v.v_float);
		break;
	case VALUE_STRING:
	        v = value_new_string (cell->value->v.str->str);
		break;
	case VALUE_EMPTY:
	default:
	        return NULL;
	}

	if (mm->fun(v, mm->test_value)) {
	        if (mm->actual_range) {
			n = g_new (int, 1);
			*((int *) n) = mm->total_num;
		        mm->list = g_slist_append (mm->list, n);
			/* FIXME: is this right?  -- MW.  */
			value_release(v);
		} else
		        mm->list = g_slist_append (mm->list, v);
		mm->num++;
	} else
	        value_release(v);

	return NULL;
}

/***************************************************************************/

static char *help_gcd = {
	N_("@FUNCTION=GCD\n"
	   "@SYNTAX=GCD(number1,number2,...)\n"

	   "@DESCRIPTION="
	   "GCD returns the greatest common divisor of given numbers. "
	   "\n"
	   "If any of the arguments is less than zero, GCD returns #NUM! "
	   "error. "
	   "If any of the arguments is non-integer, it is truncated. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "GCD(470,770) equals to 10.\n"
	   "GCD(470,770,1495) equals to 5.\n"
	   "\n"
	   "@SEEALSO=LCM")
};

static int
range_gcd (const float_t *xs, int n, float_t *res)
{
	if (n > 0) {
		int i;
		int gcd_so_far = 0;

		for (i = 0; i < n; i++) {
			if (xs[i] <= 0)
				return 1;
			else
				gcd_so_far = gcd ((int)(floor (xs[i])),
						  gcd_so_far);
		}
		*res = gcd_so_far;
		return 0;
	} else
		return 1;
}

static Value *
gnumeric_gcd (FunctionEvalInfo *ei, GList *nodes)
{
	return float_range_function (nodes, ei,
				     range_gcd,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS,
				     gnumeric_err_NUM);
}

/***************************************************************************/

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
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "LCM(2,13) equlas to 26.\n"
	   "LCM(4,7,5) equals to 140.\n"
	   "\n"
	   "@SEEALSO=GCD")
};

static Value *
callback_function_lcm (const EvalPosition *ep, Value *value, void *closure)
{
	Value *result = closure;

	switch (value->type){
	case VALUE_INTEGER:
	        if (value->v.v_int < 1)
		        return value_terminate();
	        result->v.v_int /= gcd(result->v.v_int, value->v.v_int);
		result->v.v_int *= value->v.v_int;
		break;
	case VALUE_EMPTY:
	default:
	        return value_terminate();
	}

	return NULL;
}

static Value *
gnumeric_lcm (FunctionEvalInfo *ei, GList *nodes)
{
	Value *result;

	result = g_new (Value, 1);
	result->type = VALUE_INTEGER;
	result->v.v_int = 1;

	if (function_iterate_argument_values (ei->pos, callback_function_lcm,
					      result, nodes, TRUE) != NULL)
	    return value_new_error (ei->pos, gnumeric_err_NUM);

	return result;
}

/***************************************************************************/

static char *help_abs = {
	N_("@FUNCTION=ABS\n"
	   "@SYNTAX=ABS(b1)\n"

	   "@DESCRIPTION="
	   "ABS implements the Absolute Value function:  the result is "
	   "to drop the negative sign (if present).  This can be done for "
	   "integers and floating point numbers. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ABS(7) equals 7.\n"
	   "ABS(-3.14) equals 3.14.\n"
	   "\n"
	   "@SEEALSO=CEIL, FLOOR")
};

static Value *
gnumeric_abs (FunctionEvalInfo *ei, Value **args)
{
	return value_new_float (fabs (value_get_as_float (args [0])));
}

/***************************************************************************/

static char *help_acos = {
	N_("@FUNCTION=ACOS\n"
	   "@SYNTAX=ACOS(x)\n"

	   "@DESCRIPTION="
	   "ACOS function calculates the arc cosine of @x; that "
	   "is the value whose cosine is @x.  If @x  falls  outside  the "
	   "range -1 to 1, ACOS fails and returns the NUM! error. "
	   "The value it returns is in radians. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ACOS(0.1) equals 1.470629.\n"
	   "ACOS(-0.1) equals 1.670964.\n"
	   "\n"
	   "@SEEALSO=COS, SIN, DEGREES, RADIANS")
};

static Value *
gnumeric_acos (FunctionEvalInfo *ei, Value **args)
{
	float_t t;

	t = value_get_as_float (args [0]);
	if ((t < -1.0) || (t > 1.0))
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (acos (t));
}

/***************************************************************************/

static char *help_acosh = {
	N_("@FUNCTION=ACOSH\n"
	   "@SYNTAX=ACOSH(x)\n"

	   "@DESCRIPTION="
	   "ACOSH  function  calculates  the inverse hyperbolic "
	   "cosine of @x; that is the value whose hyperbolic cosine is "
	   "@x. If @x is less than 1.0, acosh() returns the NUM! error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ACOSH(2) equals 1.31696.\n"
	   "ACOSH(5.3) equals 2.35183.\n"
	   "\n"
	   "@SEEALSO=ACOS, ASINH, DEGREES, RADIANS ")
};

static Value *
gnumeric_acosh (FunctionEvalInfo *ei, Value **args)
{
	float_t t;

	t = value_get_as_float (args [0]);
	if (t < 1.0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (acosh (t));
}

/***************************************************************************/

static char *help_asin = {
	N_("@FUNCTION=ASIN\n"
	   "@SYNTAX=ASIN(x)\n"

	   "@DESCRIPTION="
	   "ASIN function calculates the arc sine of @x; that is "
	   "the value whose sine is @x. If @x falls outside  the  range "
	   "-1 to 1, ASIN fails and returns the NUM! error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ASIN(0.5) equals 0.523599.\n"
	   "ASIN(1) equals 1.570797.\n"
	   "\n"
	   "@SEEALSO=SIN, COS, ASINH, DEGREES, RADIANS")
};

static Value *
gnumeric_asin (FunctionEvalInfo *ei, Value **args)
{
	float_t t;

	t = value_get_as_float (args [0]);
	if ((t < -1.0) || (t > 1.0))
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (asin (t));
}

/***************************************************************************/

static char *help_asinh = {
	N_("@FUNCTION=ASINH\n"
	   "@SYNTAX=ASINH(x)\n"

	   "@DESCRIPTION="
	   "ASINH function calculates the inverse hyperbolic sine of @x; "
	   "that is the value whose hyperbolic sine is @x. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ASINH(0.5) equals 0.481212. "
	   "ASINH(1.0) equals 0.881374."
	   "\n"
	   "@SEEALSO=ASIN, ACOSH, SIN, COS, DEGREES, RADIANS")
};

static Value *
gnumeric_asinh (FunctionEvalInfo *ei, Value **args)
{
	return value_new_float (asinh (value_get_as_float (args [0])));
}

/***************************************************************************/

static char *help_atan = {
	N_("@FUNCTION=ATAN\n"
	   "@SYNTAX=ATAN(x)\n"

	   "@DESCRIPTION="
	   "ATAN function calculates the arc tangent of @x; that "
	   "is the value whose tangent is @x. "
	   "Return value is in radians. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TAN, COS, SIN, DEGREES, RADIANS")
};

static Value *
gnumeric_atan (FunctionEvalInfo *ei, Value **args)
{
	return value_new_float (atan (value_get_as_float (args [0])));
}

/***************************************************************************/

static char *help_atanh = {
	N_("@FUNCTION=ATANH\n"
	   "@SYNTAX=ATANH(x)\n"

	   "@DESCRIPTION="
	   "ATANH function calculates the inverse hyperbolic tangent "
	   "of @x; that is the value whose hyperbolic tangent is @x. "
	   "If the absolute value of @x is greater than 1.0, ATANH "
	   "returns NUM! error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ATANH(0.5) equals 0.549306. "
	   "ATANH(0.8) equals 1.098612."
	   "\n"
	   "@SEEALSO=ATAN, TAN, SIN, COS, DEGREES, RADIANS")
};

static Value *
gnumeric_atanh (FunctionEvalInfo *ei, Value **args)
{
	float_t t;

	t = value_get_as_float (args [0]);
	if ((t <= -1.0) || (t >= 1.0))
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (atanh (value_get_as_float (args [0])));
}

/***************************************************************************/

static char *help_atan2 = {
	N_("@FUNCTION=ATAN2\n"
	   "@SYNTAX=ATAN2(b1,b2)\n"

	   "@DESCRIPTION="
	   "ATAN2 function calculates the arc tangent of the two "
	   "variables @b1 and @b2.  It is similar to calculating the arc "
	   "tangent of @b2 / @b1, except that the signs of both arguments "
	   "are used to determine the quadrant of the result. "
	   "The result is in radians. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ATAN2(0.5,1.0) equals 1.107149. "
	   "ATAN2(-0.5,2.0) equals 1.815775."
	   "\n"
	   "@SEEALSO=ATAN, ATANH, COS, SIN, DEGREES, RADIANS")
};

static Value *
gnumeric_atan2 (FunctionEvalInfo *ei, Value **args)
{
	return value_new_float (atan2 (value_get_as_float (args [1]),
				       value_get_as_float (args [0])));
}

/***************************************************************************/

static char *help_ceil = {
	N_("@FUNCTION=CEIL\n"
	   "@SYNTAX=CEIL(x)\n"

	   "@DESCRIPTION="
	   "CEIL function rounds @x up to the next nearest integer.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "CEIL(0.4) equals 1. "
	   "CEIL(-1.1) equals -1. "
	   "CEIL(-2.9) equals -2."
	   "\n"
	   "@SEEALSO=ABS, FLOOR, INT")
};

static Value *
gnumeric_ceil (FunctionEvalInfo *ei, Value **args)
{
	return value_new_float (ceil (value_get_as_float (args [0])));
}

/***************************************************************************/

static char *help_countif = {
	N_("@FUNCTION=COUNTIF\n"
	   "@SYNTAX=COUNTIF(range,criteria)\n"

	   "@DESCRIPTION="
	   "COUNTIF function counts the number of cells in the given @range "
	   "that meet the given @criteria. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "23, 27, 28, 33, and 39.  Then\n"
	   "COUNTIF(A1:A5,\"<=28\") equals 3.\n"
	   "COUNTIF(A1:A5,\"<28\") equals 2.\n"
	   "COUNTIF(A1:A5,\"28\") equals 1.\n"
	   "COUNTIF(A1:A5,\">28\") equals 2.\n"
	   "\n"
	   "@SEEALSO=COUNT,SUMIF")
};

static Value *
gnumeric_countif (FunctionEvalInfo *ei, Value **argv)
{
        Value           *range = argv[0];
	Value           *tmpval = NULL;
	Sheet           *sheet;

	math_criteria_t  items;
	Value           *ret;
	GSList          *list;

	items.num  = 0;
	items.total_num = 0;
	items.list = NULL;
	items.actual_range = FALSE;

	if ((!VALUE_IS_NUMBER(argv[1]) && argv[1]->type != VALUE_STRING)
	    || (range->type != VALUE_CELLRANGE))
	        return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (VALUE_IS_NUMBER(argv[1])) {
	        items.fun = (criteria_test_fun_t) criteria_test_equal;
		items.test_value = argv[1];
	} else {
	        parse_criteria(argv[1]->v.str->str,
			       &items.fun, &items.test_value);
		tmpval = items.test_value;
	}

	sheet = eval_sheet (range->v.cell_range.cell_a.sheet, ei->pos->sheet);
	ret = sheet_cell_foreach_range ( sheet,
		FALSE, /* Ignore empty cells */
		range->v.cell_range.cell_a.col,
		range->v.cell_range.cell_a.row,
		range->v.cell_range.cell_b.col,
		range->v.cell_range.cell_b.row,
		callback_function_criteria,
		&items);

	if (tmpval)
		value_release (tmpval);

	if (ret != NULL)
	        return value_new_error (ei->pos, gnumeric_err_VALUE);

        list = items.list;

	while (list != NULL) {
		value_release (list->data);
		list = list->next;
	}
	g_slist_free(items.list);

	return value_new_int (items.num);
}

/***************************************************************************/

static char *help_sumif = {
	N_("@FUNCTION=SUMIF\n"
	   "@SYNTAX=SUMIF(range,criteria[,actual_range])\n"

	   "@DESCRIPTION="
	   "SUMIF function sums the values in the given @range that meet "
	   "the given @criteria.  If @actual_range is given, SUMIF sums "
	   "the values in the @actual_range whose corresponding components "
	   "in @range meet the given @criteria. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "23, 27, 28, 33, and 39.  Then\n"
	   "SUMIF(A1:A5,\"<=28\") equals 78.\n"
	   "SUMIF(A1:A5,\"<28\") equals 50.\n"
	   "In addition, if the cells B1, B2, ..., B5 hold numbers "
	   "5, 3, 2, 6, and 7 then:\n"
	   "SUMIF(A1:A5,\"<=27\",B1:B5) equals 8.\n"
	   "\n"
	   "@SEEALSO=COUNTIF, SUM")
};

static Value *
callback_function_sumif (Sheet *sheet, int col, int row,
			 Cell *cell, void *user_data)
{
        math_criteria_t *mm = user_data;
	float_t         v;
	int             num;

	mm->total_num++;
	if (cell == NULL || cell->value == NULL)
	        return NULL;

        switch (cell->value->type) {
	case VALUE_INTEGER:
	        v = cell->value->v.v_int;
		break;
	case VALUE_FLOAT:
	        v = cell->value->v.v_float;
		break;
	case VALUE_STRING:
	        v = 0;
		break;

	case VALUE_BOOLEAN:
	        v = cell->value->v.v_bool ? 1 : 0;
		break;

	case VALUE_ERROR:
		return value_terminate();

	case VALUE_EMPTY:
	default:
	        return NULL;
	}

	if (mm->current == NULL)
	        return NULL;

	num = *((int*) mm->current->data);

	if (mm->total_num == num) {
	        mm->sum += v;
		g_free(mm->current->data);
		mm->current=mm->current->next;
	}

	return NULL;
}

static Value *
gnumeric_sumif (FunctionEvalInfo *ei, Value **argv)
{
        Value          *range = argv[0];
	Value          *actual_range = argv[2];
	Value          *tmpval = NULL;

	math_criteria_t items;
	Value          *ret;
	float_t         sum;
	GSList         *list;

	items.num  = 0;
	items.total_num = 0;
	items.list = NULL;

	if ((!VALUE_IS_NUMBER(argv[1]) && argv[1]->type != VALUE_STRING)
	    || (range->type != VALUE_CELLRANGE))
	        return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (VALUE_IS_NUMBER(argv[1])) {
	        items.fun = (criteria_test_fun_t) criteria_test_equal;
		items.test_value = argv[1];
	} else {
	        parse_criteria(argv[1]->v.str->str,
			       &items.fun, &items.test_value);
		tmpval = items.test_value;
	}

	if (actual_range != NULL)
	        items.actual_range = TRUE;
	else
	        items.actual_range = FALSE;

	ret = sheet_cell_foreach_range (
		eval_sheet (range->v.cell_range.cell_a.sheet, ei->pos->sheet),
		/* Do not ignore empty cells if there is an actual range */
		actual_range == NULL,

		range->v.cell_range.cell_a.col,
		range->v.cell_range.cell_a.row,
		range->v.cell_range.cell_b.col,
		range->v.cell_range.cell_b.row,
		callback_function_criteria,
		&items);

	if (tmpval)
		value_release (tmpval);

	if (ret != NULL)
	        return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (actual_range == NULL) {
	        list = items.list;
		sum = 0;

		while (list != NULL) {
		        Value *v = list->data;

			if (v != NULL)
			        sum += value_get_as_float (v);
			value_release (v);
			list = list->next;
		}
	} else {
	      items.current = items.list;
	      items.sum = items.total_num = 0;
 	      ret = sheet_cell_foreach_range (
			eval_sheet (actual_range->v.cell_range.cell_a.sheet, ei->pos->sheet),
			/* Empty cells too.  Criteria and results must align */
			FALSE,

			actual_range->v.cell_range.cell_a.col,
			actual_range->v.cell_range.cell_a.row,
			actual_range->v.cell_range.cell_b.col,
			actual_range->v.cell_range.cell_b.row,
			callback_function_sumif,
			&items);
	      sum = items.sum;
	}

	g_slist_free(items.list);
		
	return value_new_float (sum);
}

/***************************************************************************/

static char *help_ceiling = {
	N_("@FUNCTION=CEILING\n"
	   "@SYNTAX=CEILING(x,significance)\n"

	   "@DESCRIPTION="
	   "CEILING function rounds @x up to the nearest multiple of "
	   "significance. "
	   "\n"
	   "If @x or @significance is non-numeric CEILING returns "
	   "#VALUE! error. "
	   "If @x and @significance have different signs CEILING returns "
	   "#NUM! error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "CEILING(2.43,1) equals 3. "
	   "CEILING(123.123,3) equals 126."
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
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (ceil (number / s) * s);
}

/***************************************************************************/

static char *help_cos = {
	N_("@FUNCTION=COS\n"
	   "@SYNTAX=COS(x)\n"

	   "@DESCRIPTION="
	   "COS function returns the cosine of @x, where @x is given "
           "in radians. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "COS(0.5) equals 0.877583.\n"
	   "COS(1) equals 0.540302.\n"
	   "\n"
	   "@SEEALSO=COSH, SIN, SINH, TAN, TANH, RADIANS, DEGREES")
};

static Value *
gnumeric_cos (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (cos (value_get_as_float (argv [0])));
}

/***************************************************************************/

static char *help_cosh = {
	N_("@FUNCTION=COSH\n"
	   "@SYNTAX=COSH(x)\n"

	   "@DESCRIPTION="
	   "COSH function returns the hyperbolic cosine of @x, which "
	   "is defined mathematically as (exp(@x) + exp(-@x)) / 2.   "
	   "@x is in radians. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "COSH(0.5) equals 1.127626. "
	   "COSH(1) equals 1.543081."
	   "\n"
	   "@SEEALSO=COS, SIN, SINH, TAN, TANH, RADIANS, DEGREES, EXP")
};

static Value *
gnumeric_cosh (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (cosh (value_get_as_float (argv [0])));
}

/***************************************************************************/

static char *help_degrees = {
	N_("@FUNCTION=DEGREES\n"
	   "@SYNTAX=DEGREES(x)\n"

	   "@DESCRIPTION="
	   "DEGREES computes the number of degrees equivalent to @x radians. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "DEGREES(2.5) equals 143.2394.\n"
	   "\n"
	   "@SEEALSO=RADIANS, PI")
};

static Value *
gnumeric_degrees (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float ((value_get_as_float (argv [0]) * 180.0) / M_PI);
}

/***************************************************************************/

static char *help_exp = {
	N_("@FUNCTION=EXP\n"
	   "@SYNTAX=EXP(x)\n"

	   "@DESCRIPTION="
	   "EXP computes the value of e (the base of natural logarithmns) "
	   "raised to the power of @x. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "EXP(2) equals 7.389056.\n"
	   "\n"
	   "@SEEALSO=LOG, LOG2, LOG10")
};

static Value *
gnumeric_exp (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (exp (value_get_as_float (argv [0])));
}

/***************************************************************************/

static char *help_fact = {
	N_("@FUNCTION=FACT\n"
	   "@SYNTAX=FACT(x)\n"

	   "@DESCRIPTION="
	   "FACT computes the factorial of @x. ie, @x!"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "FACT(3) equals 6.\n"
	   "FACT(9) equals 362880.\n"
	   "\n"
	   "@SEEALSO=")
};

float_t
fact (int n)
{
	if (n == 0)
		return 1;
	return (n * fact (n - 1));
}

static Value *
gnumeric_fact (FunctionEvalInfo *ei, Value **argv)
{
	float_t x;
	gboolean x_is_integer;

	if (!VALUE_IS_NUMBER (argv[0]))
		return value_new_error (ei->pos, gnumeric_err_NUM);

	x = value_get_as_float (argv[0]);
	if (x < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	x_is_integer = (x == floor (x));

	if (x > 12 || !x_is_integer) {
		float_t res = exp (lgamma (x + 1));
		if (x_is_integer)
			res = floor (res + 0.5);  /* Round, just in case.  */
		return value_new_float (res);
	} else
		return value_new_int (fact (x));
}

/***************************************************************************/

static char *help_combin = {
	N_("@FUNCTION=COMBIN\n"
	   "@SYNTAX=COMBIN(n,k)\n"

	   "@DESCRIPTION="
	   "COMBIN computes the number of combinations. "
	   "\n"
	   "Performing this function on a non-integer or a negative number "
           "returns an error.  Also if @n is less than @k returns an error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "COMBIN(8,6) equals 28.\n"
	   "COMBIN(6,2) equals 15.\n"
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

	return value_new_error (ei->pos, gnumeric_err_NUM);
}

/***************************************************************************/

static char *help_floor = {
	N_("@FUNCTION=FLOOR\n"
	   "@SYNTAX=FLOOR(x,significance)\n"

	   "@DESCRIPTION="
	   "FLOOR function rounds @x down to the next nearest multiple "
	   "of @significance.  @significance defaults to 1. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "FLOOR(0.5) equals 0.\n"
	   "FLOOR(5,2) equals 4.\n"
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
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (floor (number / s) * s);
}

/***************************************************************************/

static char *help_int = {
	N_("@FUNCTION=INT\n"
	   "@SYNTAX=INT(a)\n"

	   "@DESCRIPTION="
	   "INT function rounds @a now to the nearest integer "
	   "where `nearest' implies being closer to zero. "
	   "INT is equivalent to FLOOR(a) for @a >= 0, and CEIL(a) "
	   "for @a < 0. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "INT(7.2) equals 7.\n"
	   "INT(-5.5) equals -6.\n"
	   "\n"
	   "@SEEALSO=FLOOR, CEIL, ABS")
};

static Value *
gnumeric_int (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (floor (value_get_as_float (argv [0])));
}

/***************************************************************************/

static char *help_log = {
	N_("@FUNCTION=LOG\n"
	   "@SYNTAX=LOG(x[,base])\n"

	   "@DESCRIPTION="
	   "LOG computes the logarithm of @x in the given base @base.  "
	   "If no @base is given LOG returns the logarithm in base 10. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "LOG(2) equals 0.30103.\n"
	   "LOG(8192,2) equals 13.\n"
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
	else {
	        base = value_get_as_float (argv[1]);
		if (base <= 1)
			return value_new_error (ei->pos, gnumeric_err_NUM);
	}

	if (t <= 0.0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (log (t) / log (base));
}

/***************************************************************************/

static char *help_ln = {
	N_("@FUNCTION=LN\n"
	   "@SYNTAX=LN(x)\n"

	   "@DESCRIPTION="
	   "LN returns the natural logarithm of @x. "
	   "If @x <= 0, LN returns #NUM! error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "LN(7) equals 1.94591.\n"
	   "\n"
	   "@SEEALSO=EXP, LOG2, LOG10")
};

static Value *
gnumeric_ln (FunctionEvalInfo *ei, Value **argv)
{
	float_t t;

	t = value_get_as_float (argv [0]);

	if (t <= 0.0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (log (t));
}

/***************************************************************************/

static char *help_power = {
	N_("@FUNCTION=POWER\n"
	   "@SYNTAX=POWER(x,y)\n"

	   "@DESCRIPTION="
	   "POWER returns the value of @x raised to the power @y. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "POWER(2,7) equals 128.\n"
	   "POWER(3,3.141) equals 31.523749.\n"
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

	if (x == 0 && y != 0)
		return value_new_error (ei->pos, gnumeric_err_DIV0);
	else
		return value_new_error (ei->pos, gnumeric_err_NUM);
}

/***************************************************************************/

static char *help_log2 = {
	N_("@FUNCTION=LOG2\n"
	   "@SYNTAX=LOG2(x)\n"

	   "@DESCRIPTION="
	   "LOG2 computes the base-2 logarithm of @x. "
	   "If @x <= 0, LOG2 returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "LOG2(1024) equals 10.\n"
	   "\n"
	   "@SEEALSO=EXP, LOG10, LOG")
};

static Value *
gnumeric_log2 (FunctionEvalInfo *ei, Value **argv)
{
	float_t t;

	t = value_get_as_float (argv [0]);
	if (t <= 0.0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (log (t) / M_LN2);
}

/***************************************************************************/

static char *help_log10 = {
	N_("@FUNCTION=LOG10\n"
	   "@SYNTAX=LOG10(x)\n"

	   "@DESCRIPTION="
	   "LOG10 computes the base-10 logarithm of @x. "
	   "If @x <= 0, LOG10 returns #NUM! error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "LOG10(7) equals 0.845098.\n"
	   "\n"
	   "@SEEALSO=EXP, LOG2, LOG")
};

static Value *
gnumeric_log10 (FunctionEvalInfo *ei, Value **argv)
{
	float_t t;

	t = value_get_as_float (argv [0]);
	if (t <= 0.0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (log10 (t));
}

/***************************************************************************/

static char *help_mod = {
	N_("@FUNCTION=MOD\n"
	   "@SYNTAX=MOD(number,divisor)\n"

	   "@DESCRIPTION="
	   "MOD function returns the remainder when @divisor is divided "
	   "into @number. "
	   "This function is Excel compatible. "
	   "\n"
	   "MOD returns #DIV/0! if divisor is zero.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "MOD(23,7) equals 2.\n"
	   "\n"
	   "@SEEALSO=INT,FLOOR,CEIL")
};

static Value *
gnumeric_mod (FunctionEvalInfo *ei, Value **argv)
{
	int a, b;

	a = value_get_as_int (argv[0]);
	b = value_get_as_int (argv[1]);

	if (b == 0)
		return value_new_error (ei->pos, gnumeric_err_DIV0);
	else if (b > 0) {
		if (a >= 0)
			return value_new_int (a % b);
		else {
			int c = (-a) % b;
			return value_new_int (c ? b - c : 0);
		}
	} else {
		b = -b;
		a = -a;
		if (a >= 0)
			return value_new_int (-(a % b));
		else {
			int c = (-a) % b;
			return value_new_int (c ? c - b : 0);
		}
	}
}

/***************************************************************************/

static char *help_radians = {
	N_("@FUNCTION=RADIANS\n"
	   "@SYNTAX=RADIANS(x)\n"

	   "@DESCRIPTION="
	   "RADIANS computes the number of radians equivalent to @x degrees. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "RADIANS(180) equals 3.14159.\n"
	   "\n"
	   "@SEEALSO=PI,DEGREES")
};

static Value *
gnumeric_radians (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float ((value_get_as_float (argv [0]) * M_PI) / 180);
}

/***************************************************************************/

static char *help_rand = {
	N_("@FUNCTION=RAND\n"
	   "@SYNTAX=RAND()\n"

	   "@DESCRIPTION="
	   "RAND returns a random number between zero and one ([0..1]). "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "RAND() returns a random number greater than zero but less "
	   "than one.\n"
	   "\n"
	   "@SEEALSO=RANDBETWEEN")
};

static Value *
gnumeric_rand (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (random_01 ());
}

/***************************************************************************/

static char *help_sin = {
	N_("@FUNCTION=SIN\n"
	   "@SYNTAX=SIN(x)\n"

	   "@DESCRIPTION="
	   "SIN function returns the sine of @x, where @x is given "
           "in radians. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "SIN(0.5) equals 0.479426.\n"
	   "\n"
	   "@SEEALSO=COS, COSH, SINH, TAN, TANH, RADIANS, DEGREES")
};

static Value *
gnumeric_sin (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (sin (value_get_as_float (argv [0])));
}

/***************************************************************************/

static char *help_sinh = {
	N_("@FUNCTION=SINH\n"
	   "@SYNTAX=SINH(x)\n"

	   "@DESCRIPTION="
	   "SINH function returns the hyperbolic sine of @x, "
	   "which is defined mathematically as (exp(@x) - exp(-@x)) / 2. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "SINH(0.5) equals 0.521095.\n"
	   "\n"
	   "@SEEALSO=SIN, COS, COSH, TAN, TANH, DEGREES, RADIANS, EXP")
};

static Value *
gnumeric_sinh (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (sinh (value_get_as_float (argv [0])));
}

/***************************************************************************/

static char *help_sqrt = {
	N_("@FUNCTION=SQRT\n"
	   "@SYNTAX=SQRT(x)\n"

	   "@DESCRIPTION="
	   "SQRT function returns the square root of @x. "
	   "This function is Excel compatible. "
	   "\n"
	   "If @x is negative, SQRT returns #NUM! error. "
	   "\n"
	   "@EXAMPLES=\n"
	   "SQRT(2) equals 1.4142136.\n"
	   "\n"
	   "@SEEALSO=POW")
};

static Value *
gnumeric_sqrt (FunctionEvalInfo *ei, Value **argv)
{
	float_t x = value_get_as_float (argv[0]);
	if (x < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (sqrt(x));
}

/***************************************************************************/

static char *help_sum = {
	N_("@FUNCTION=SUM\n"
	   "@SYNTAX=SUM(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "SUM computes the sum of all the values and cells referenced "
	   "in the argument list. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43.  Then\n"
	   "SUM(A1:A5) equals 107.\n"
	   "\n"
	   "@SEEALSO=AVERAGE, COUNT")
};

static Value *
gnumeric_sum (FunctionEvalInfo *ei, GList *nodes)
{
	return float_range_function (nodes, ei,
				     range_sum,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_suma = {
	N_("@FUNCTION=SUMA\n"
	   "@SYNTAX=SUMA(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "SUMA computes the sum of all the values and cells referenced "
	   "in the argument list.  Numbers, text and logical values are "
	   "included in the calculation too.  If the cell contains text or "
	   "the argument evaluates to FALSE, it is counted as value zero (0). "
	   "If the argument evaluates to TRUE, it is counted as one (1). "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43.  Then\n"
	   "SUMA(A1:A5) equals 107.\n"
	   "\n"
	   "@SEEALSO=AVERAGE, SUM, COUNT")
};

static Value *
gnumeric_suma (FunctionEvalInfo *ei, GList *nodes)
{
	return float_range_function (nodes, ei,
				     range_sum,
				     COLLECT_ZERO_STRINGS |
				     COLLECT_ZEROONE_BOOLS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_sumsq = {
	N_("@FUNCTION=SUMSQ\n"
	   "@SYNTAX=SUMSQ(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "SUMSQ returns the sum of the squares of all the values and "
	   "cells referenced in the argument list. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43.  Then\n"
	   "SUMSQ(A1:A5) equals 2925.\n"
	   "\n"
	   "@SEEALSO=SUM, COUNT")
};


static Value *
gnumeric_sumsq (FunctionEvalInfo *ei, GList *nodes)
{
	return float_range_function (nodes, ei,
				     range_sumsq,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_multinomial = {
	N_("@FUNCTION=MULTINOMIAL\n"
	   "@SYNTAX=MULTINOMIAL(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "MULTINOMIAL returns the ratio of the factorial of a sum of "
	   "values to the product of factorials. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "MULTINOMIAL(2,3,4) equals 1260.\n"
	   "\n"
	   "@SEEALSO=SUM")
};

typedef struct {
	guint32 num;
	int     sum;
        int     product;
} math_multinomial_t;

static Value *
callback_function_multinomial (const EvalPosition *ep, Value *value,
			       void *closure)
{
	math_multinomial_t *mm = closure;

	switch (value->type){
	case VALUE_INTEGER:
	        mm->product *= fact(value->v.v_int);
		mm->sum += value->v.v_int;
		mm->num++;
		break;
	case VALUE_EMPTY:
	default:
		return value_terminate();
	}
	return NULL;
}

static Value *
gnumeric_multinomial (FunctionEvalInfo *ei, GList *nodes)
{
        math_multinomial_t p;

	p.num = 0;
	p.sum = 0;
	p.product = 1;

	if (function_iterate_argument_values (ei->pos,
					      callback_function_multinomial,
					      &p, nodes,
					      TRUE) != NULL)
	        return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_float (fact(p.sum) / p.product);
}

/***************************************************************************/

static char *help_product = {
	N_("@FUNCTION=PRODUCT\n"
	   "@SYNTAX=PRODUCT(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "PRODUCT returns the product of all the values and cells "
	   "referenced in the argument list. "
	   "This function is Excel compatible.  In particular, this means "
	   "that if all cells are empty, the result will be 0."
	   "\n"
	   "@EXAMPLES=\n"
	   "PRODUCT(2,5,9) equals 90.\n"
	   "\n"
	   "@SEEALSO=SUM, COUNT, G_PRODUCT")
};

static int
range_bogusproduct (const float_t *xs, int n, float_t *res)
{
	if (n == 0) {
		*res = 0;  /* Severe Excel brain damange.  */
		return 0;
	} else
		return range_product (xs, n, res);
}

static Value *
gnumeric_product (FunctionEvalInfo *ei, GList *nodes)
{
	return float_range_function (nodes, ei,
				     range_bogusproduct,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS,
				     gnumeric_err_VALUE);
}


static char *help_g_product = {
	N_("@FUNCTION=G_PRODUCT\n"
	   "@SYNTAX=G_PRODUCT(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "PRODUCT returns the product of all the values and cells "
	   "referenced in the argument list.  Empty cells are ignored "
	   "and the empty product in 1."
	   "\n"
	   "@EXAMPLES=\n"
	   "G_PRODUCT(2,5,9) equals 90.\n"
	   "\n"
	   "@SEEALSO=SUM, COUNT")
};

static Value *
gnumeric_g_product (FunctionEvalInfo *ei, GList *nodes)
{
	return float_range_function (nodes, ei,
				     range_product,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static char *help_tan = {
	N_("@FUNCTION=TAN\n"
	   "@SYNTAX=TAN(x)\n"

	   "@DESCRIPTION="
	   "TAN function returns the tangent of @x, where @x is "
	   "given in radians. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "TAN(3) equals -0.1425465.\n"
	   "\n"
	   "@SEEALSO=TANH, COS, COSH, SIN, SINH, DEGREES, RADIANS")
};

static Value *
gnumeric_tan (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (tan (value_get_as_float (argv [0])));
}

/***************************************************************************/

static char *help_tanh = {
	N_("@FUNCTION=TANH\n"
	   "@SYNTAX=TANH(x)\n"

	   "@DESCRIPTION="
	   "The TANH function returns the hyperbolic tangent of @x, "
	   "which is defined mathematically as sinh(@x) / cosh(@x). "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "TANH(2) equals 0.96402758.\n"
	   "\n"
	   "@SEEALSO=TAN, SIN, SINH, COS, COSH, DEGREES, RADIANS")
};

static Value *
gnumeric_tanh (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (tanh (value_get_as_float (argv [0])));
}

/***************************************************************************/

static char *help_pi = {
	N_("@FUNCTION=PI\n"
	   "@SYNTAX=PI()\n"

	   "@DESCRIPTION="
	   "PI functions returns the value of Pi. "
	   "\n"
	   "This function is called with no arguments. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "PI() equals 3.141593.\n"
	   "\n"
	   "@SEEALSO=SQRTPI")
};

static Value *
gnumeric_pi (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_float (M_PI);
}

/***************************************************************************/

static char *help_trunc = {
	N_("@FUNCTION=TRUNC\n"
	   "@SYNTAX=TRUNC(number[,digits])\n"

	   "@DESCRIPTION="
	   "TRUNC function returns the value of @number "
	   "truncated to the number of digits specified. "
	   "If @digits is omitted then @digits defaults to zero. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "TRUNC(3.12) equals 3.\n"
	   "TRUNC(4.15,1) equals 4.1.\n"
	   "\n"
	   "@SEEALSO=INT")
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

/***************************************************************************/

static char *help_even = {
	N_("@FUNCTION=EVEN\n"
	   "@SYNTAX=EVEN(number)\n"

	   "@DESCRIPTION="
	   "EVEN function returns the number rounded up to the "
	   "nearest even integer. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "EVEN(5.4) equals 6.\n"
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

/***************************************************************************/

static char *help_odd = {
	N_("@FUNCTION=ODD\n"
	   "@SYNTAX=ODD(number)\n"

	   "@DESCRIPTION="
	   "ODD function returns the @number rounded up to the "
	   "nearest odd integer. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ODD(4.4) equals 5.\n"
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

/***************************************************************************/

static char *help_factdouble = {
	N_("@FUNCTION=FACTDOUBLE\n"
	   "@SYNTAX=FACTDOUBLE(number)\n"

	   "@DESCRIPTION="
	   "FACTDOUBLE function returns the double factorial "
	   "of a @number. "
	   "\n"
	   "If @number is not an integer, it is truncated. "
	   "If @number is negative FACTDOUBLE returns #NUM! error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "FACTDOUBLE(5) equals 15.\n"
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
		return value_new_error (ei->pos, gnumeric_err_NUM );

	for (n = number; n > 0; n-= 2)
	        product *= n;

	return value_new_float (product);
}

/***************************************************************************/

static char *help_quotient = {
	N_("@FUNCTION=QUOTIENT\n"
	   "@SYNTAX=QUOTIENT(num,den)\n"

	   "@DESCRIPTION="
	   "QUOTIENT function returns the integer portion "
	   "of a division. @num is the divided and @den is the divisor. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "QUOTIENT(23,5) equals 4.\n"
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

/***************************************************************************/

static char *help_sign = {
	N_("@FUNCTION=SIGN\n"
	   "@SYNTAX=SIGN(number)\n"

	   "@DESCRIPTION="
	   "SIGN function returns 1 if the @number is positive, "
	   "zero if the @number is 0, and -1 if the @number is negative. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "SIGN(3) equals 1.\n"
	   "SIGN(-3) equals -1.\n"
	   "SIGN(0) equals 0.\n"
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

/***************************************************************************/

static char *help_sqrtpi = {
	N_("@FUNCTION=SQRTPI\n"
	   "@SYNTAX=SQRTPI(number)\n"

	   "@DESCRIPTION="
	   "SQRTPI function returns the square root of a @number "
	   "multiplied by pi. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "SQRTPI(2) equals 2.506628275.\n"
	   "\n"
	   "@SEEALSO=PI")
};

static Value *
gnumeric_sqrtpi (FunctionEvalInfo *ei, Value **argv)
{
        float_t n;

	n = value_get_as_float (argv[0]);
	if (n < 0)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	return value_new_float (sqrt (M_PI * n));
}

/***************************************************************************/

static char *help_randbetween = {
	N_("@FUNCTION=RANDBETWEEN\n"
	   "@SYNTAX=RANDBETWEEN(bottom,top)\n"

	   "@DESCRIPTION="
	   "RANDBETWEEN function returns a random integer number "
	   "between @bottom and @top.\n"
	   "If @bottom or @top is non-integer, they are truncated. "
	   "If @bottom > @top, RANDBETWEEN returns #NUM! error.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "RANDBETWEEN(3,7).\n"
	   "\n"
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
		return value_new_error (ei->pos, gnumeric_err_NUM );

	r = bottom + floor ((top + 1.0 - bottom) * random_01 ());
	return value_new_int ((int)r);
}

/***************************************************************************/

static char *help_rounddown = {
	N_("@FUNCTION=ROUNDDOWN\n"
	   "@SYNTAX=ROUNDDOWN(number[,digits])\n"

	   "@DESCRIPTION="
	   "ROUNDDOWN function rounds a given @number down, towards zero. "
	   "@number is the number you want rounded down and @digits is the "
	   "number of digits to which you want to round that number. "
	   "\n"
	   "If @digits is greater than zero, @number is rounded down to the "
	   "given number of digits. "
	   "If @digits is zero or omitted, @number is rounded down to the "
	   "nearest integer. "
	   "If @digits is less than zero, @number is rounded down to the left "
	   "of the decimal point. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ROUNDDOWN(5.5) equals 5.\n"
	   "ROUNDDOWN(-3.3) equals -4.\n"
	   "ROUNDDOWN(1501.15,1) equals 1501.1.\n"
	   "ROUNDDOWN(1501.15,-2) equals 1500.0.\n"
	   "\n"
	   "@SEEALSO=ROUND,ROUNDUP")
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

/***************************************************************************/

static char *help_round = {
	N_("@FUNCTION=ROUND\n"
	   "@SYNTAX=ROUND(number[,digits])\n"

	   "@DESCRIPTION="
	   "ROUND function rounds a given number. "
	   "@number is the number you want rounded and @digits is the "
	   "number of digits to which you want to round that number. "
	   "\n"
	   "If @digits is greater than zero, @number is rounded to the "
	   "given number of digits. "
	   "If @digits is zero or omitted, @number is rounded to the "
	   "nearest integer. "
	   "If @digits is less than zero, @number is rounded to the left "
	   "of the decimal point. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ROUND(5.5) equals 6.\n"
	   "ROUND(-3.3) equals -3.\n"
	   "ROUND(1501.15,1) equals 1501.2.\n"
	   "ROUND(1501.15,-2) equals 1500.0.\n"
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

/***************************************************************************/

static char *help_roundup = {
	N_("@FUNCTION=ROUNDUP\n"
	   "@SYNTAX=ROUNDUP(number[,digits])\n"

	   "@DESCRIPTION="
	   "ROUNDUP function rounds a given number up, away from zero. "
	   "@number is the number you want rounded up and @digits is the "
	   "number of digits to which you want to round that number. "
	   "\n"
	   "If @digits is greater than zero, @number is rounded up to the "
	   "given number of digits. "
	   "If @digits is zero or omitted, @number is rounded up to the "
	   "nearest integer. "
	   "If @digits is less than zero, @number is rounded up to the left "
	   "of the decimal point. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ROUNDUP(5.5) equals 6.\n"
	   "ROUNDUP(-3.3) equals -3.\n"
	   "ROUNDUP(1501.15,1) equals 1501.2.\n"
	   "ROUNDUP(1501.15,-2) equals 1600.0.\n"
	   "\n"
	   "@SEEALSO=ROUND,ROUNDDOWN")
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

/***************************************************************************/

static char *help_mround = {
	N_("@FUNCTION=MROUND\n"
	   "@SYNTAX=MROUND(number,multiple)\n"

	   "@DESCRIPTION="
	   "MROUND function rounds a given number to the desired multiple. "
	   "@number is the number you want rounded and @multiple is the "
	   "the multiple to which you want to round the number. "
	   "\n"
	   "If @number and @multiple have different sign, MROUND "
	   "returns #NUM! error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "MROUND(1.7,0.2) equals 1.8.\n"
	   "MROUND(321.123,0.12) equals 321.12.\n"
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
		return value_new_error (ei->pos, gnumeric_err_NUM);

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

/***************************************************************************/

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
	   "Type 1 is more concise than classic type, type 2 is more concise "
	   "than type 1, and type 3 is more concise than type 2.  Type 4 "
	   "is simplified type. "
	   "\n"
	   "If @number is negative or greater than 3999, ROMAN returns "
	   "#VALUE! error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "ROMAN(999) equals CMXCIX.\n"
	   "ROMAN(999,1) equals LMVLIV.\n"
	   "ROMAN(999,2) equals XMIX.\n"
	   "ROMAN(999,3) equals VMIV.\n"
	   "ROMAN(999,4) equals IM.\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_roman (FunctionEvalInfo *ei, Value **argv)
{
	const char letter[] = { 'M', 'D', 'C', 'L', 'X', 'V', 'I' };
	const int  largest = 1000;

	static char buf[256];
	char        *p;

	int n, form;
	int i, j, dec;

	dec = largest;
	n = value_get_as_int (argv[0]);
	if (argv[1] == NULL)
	        form = 0;
	else
	        form = value_get_as_int (argv[1]);

	if (n < 0 || n > 3999)
		return value_new_error (ei->pos, gnumeric_err_VALUE );

	if (n == 0)
		return value_new_string ("");

	if (form < 0 || form > 4)
		return value_new_error (ei->pos, gnumeric_err_NUM );

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


	if (form > 0) {
	        /* Replace ``XLV'' with ``VL'' */
	        if ((p = strstr(buf, "XLV")) != NULL) {
		        *p++ = 'V';
			*p++ = 'L';
			for ( ; *p; p++)
			        *p = *(p+1);
		} 
	        /* Replace ``XCV'' with ``VC'' */
	        if ((p = strstr(buf, "XCV")) != NULL) {
		        *p++ = 'V';
			*p++ = 'C';
			for ( ; *p; p++)
			        *p = *(p+1);
		} 
	        /* Replace ``CDL'' with ``LD'' */
	        if ((p = strstr(buf, "CDL")) != NULL) {
		        *p++ = 'L';
			*p++ = 'D';
			for ( ; *p; p++)
			        *p = *(p+1);
		} 
	        /* Replace ``CML'' with ``LM'' */
	        if ((p = strstr(buf, "CML")) != NULL) {
		        *p++ = 'L';
			*p++ = 'M';
			for ( ; *p; p++)
			        *p = *(p+1);
		} 
	        /* Replace ``CMVC'' with ``LMVL'' */
	        if ((p = strstr(buf, "CMVC")) != NULL) {
		        *p++ = 'L';
			*p++ = 'M';
			*p++ = 'V';
			*p++ = 'L';
		} 
	}
	if (form == 1) {
	        /* Replace ``CDXC'' with ``LDXL'' */
	        if ((p = strstr(buf, "CDXC")) != NULL) {
		        *p++ = 'L';
			*p++ = 'D';
			*p++ = 'X';
			*p++ = 'L';
		} 
	        /* Replace ``CDVC'' with ``LDVL'' */
	        if ((p = strstr(buf, "CDVC")) != NULL) {
		        *p++ = 'L';
			*p++ = 'D';
			*p++ = 'V';
			*p++ = 'L';
		} 
	        /* Replace ``CMXC'' with ``LMXL'' */
	        if ((p = strstr(buf, "CMXC")) != NULL) {
		        *p++ = 'L';
			*p++ = 'M';
			*p++ = 'X';
			*p++ = 'L';
		} 
	        /* Replace ``XCIX'' with ``VCIV'' */
	        if ((p = strstr(buf, "XCIX")) != NULL) {
		        *p++ = 'V';
			*p++ = 'C';
			*p++ = 'I';
			*p++ = 'V';
		} 
	        /* Replace ``XLIX'' with ``VLIV'' */
	        if ((p = strstr(buf, "XLIX")) != NULL) {
		        *p++ = 'V';
			*p++ = 'L';
			*p++ = 'I';
			*p++ = 'V';
		} 
	}
	if (form > 1) {
	        /* Replace ``XLIX'' with ``IL'' */
	        if ((p = strstr(buf, "XLIX")) != NULL) {
		        *p++ = 'I';
			*p++ = 'L';
			for ( ; *p; p++)
			        *p = *(p+2);
		} 
	        /* Replace ``XCIX'' with ``IC'' */
	        if ((p = strstr(buf, "XCIX")) != NULL) {
		        *p++ = 'I';
			*p++ = 'C';
			for ( ; *p; p++)
			        *p = *(p+2);
		} 
	        /* Replace ``CDXC'' with ``XD'' */
	        if ((p = strstr(buf, "CDXC")) != NULL) {
		        *p++ = 'X';
			*p++ = 'D';
			for ( ; *p; p++)
			        *p = *(p+2);
		} 
	        /* Replace ``CDVC'' with ``XDV'' */
	        if ((p = strstr(buf, "CDVC")) != NULL) {
		        *p++ = 'X';
			*p++ = 'D';
			*p++ = 'V';
			for ( ; *p; p++)
			        *p = *(p+1);
		} 
	        /* Replace ``CDIC'' with ``XDIX'' */
	        if ((p = strstr(buf, "CDIC")) != NULL) {
		        *p++ = 'X';
			*p++ = 'D';
			*p++ = 'I';
			*p++ = 'X';
		} 
	        /* Replace ``LMVL'' with ``XMV'' */
	        if ((p = strstr(buf, "LMVL")) != NULL) {
		        *p++ = 'X';
			*p++ = 'M';
			*p++ = 'V';
			for ( ; *p; p++)
			        *p = *(p+1);
		} 
	        /* Replace ``CMIC'' with ``XMIX'' */
	        if ((p = strstr(buf, "CMIC")) != NULL) {
		        *p++ = 'X';
			*p++ = 'M';
			*p++ = 'I';
			*p++ = 'X';
		} 
	        /* Replace ``CMXC'' with ``XM'' */
	        if ((p = strstr(buf, "CMXC")) != NULL) {
		        *p++ = 'X';
			*p++ = 'M';
			for ( ; *p; p++)
			        *p = *(p+2);
		} 
	}
	if (form > 2) {
	        /* Replace ``XDV'' with ``VD'' */
	        if ((p = strstr(buf, "XDV")) != NULL) {
		        *p++ = 'V';
			*p++ = 'D';
			for ( ; *p; p++)
			        *p = *(p+1);
		} 
	        /* Replace ``XDIX'' with ``VDIV'' */
	        if ((p = strstr(buf, "XDIX")) != NULL) {
		        *p++ = 'V';
			*p++ = 'D';
			*p++ = 'I';
			*p++ = 'V';
		} 
	        /* Replace ``XMV'' with ``VM'' */
	        if ((p = strstr(buf, "XMV")) != NULL) {
		        *p++ = 'V';
			*p++ = 'M';
			for ( ; *p; p++)
			        *p = *(p+1);
		} 
	        /* Replace ``XMIX'' with ``VMIV'' */
	        if ((p = strstr(buf, "XMIX")) != NULL) {
		        *p++ = 'V';
			*p++ = 'M';
			*p++ = 'I';
			*p++ = 'V';
		} 
	}
	if (form == 4) {
	        /* Replace ``VDIV'' with ``ID'' */
	        if ((p = strstr(buf, "VDIV")) != NULL) {
		        *p++ = 'I';
			*p++ = 'D';
			for ( ; *p; p++)
			        *p = *(p+2);
		} 
	        /* Replace ``VMIV'' with ``IM'' */
	        if ((p = strstr(buf, "VMIV")) != NULL) {
		        *p++ = 'I';
			*p++ = 'M';
			for ( ; *p; p++)
			        *p = *(p+2);
		} 
	}

	return value_new_string (buf);
}

/***************************************************************************/

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
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold "
	   "numbers 13, 22, 31, 33, and 39.  Then\n"
	   "SUMX2MY2(A1:A5,B1:B5) equals -1299.\n"
	   "\n"
	   "@SEEALSO=SUMSQ,SUMX2PY2")
};

static Value *
gnumeric_sumx2my2 (FunctionEvalInfo *ei, Value **argv)
{
        Value      *values_x = argv[0];
        Value      *values_y = argv[1];
	math_sums_t items_x, items_y;
	Value      *ret;
	float_t     sum;
	GSList     *list1, *list2;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (values_x->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet),
			TRUE,
			values_x->v.cell_range.cell_a.col,
			values_x->v.cell_range.cell_a.row,
			values_x->v.cell_range.cell_b.col,
			values_x->v.cell_range.cell_b.row,
			callback_function_sumxy,
			&items_x);
		
		if (ret != NULL)
		        return value_new_error (ei->pos, gnumeric_err_VALUE);
	} else
		return value_new_error (ei->pos,
					_("Array version not implemented!"));

        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet), TRUE,
			values_y->v.cell_range.cell_a.col,
			values_y->v.cell_range.cell_a.row,
			values_y->v.cell_range.cell_b.col,
			values_y->v.cell_range.cell_b.row,
			callback_function_sumxy,
			&items_y);
		if (ret != NULL)
		        return value_new_error (ei->pos, gnumeric_err_VALUE);
	} else
		return value_new_error (ei->pos,
					_("Array version not implemented!"));

	if (items_x.num != items_y.num)
		return value_new_error (ei->pos, gnumeric_err_NA);

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

/***************************************************************************/

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
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold "
	   "numbers 13, 22, 31, 33, and 39.  Then\n"
	   "SUMX2PY2(A1:A5,B1:B5) equals 7149.\n"
	   "\n"
	   "@SEEALSO=SUMSQ,SUMX2MY2")
};

static Value *
gnumeric_sumx2py2 (FunctionEvalInfo *ei, Value **argv)
{
        Value      *values_x = argv[0];
        Value      *values_y = argv[1];
	math_sums_t items_x, items_y;
	Value      *ret;
	float_t     sum;
	GSList     *list1, *list2;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (values_x->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet),
			TRUE,
			values_x->v.cell_range.cell_a.col,
			values_x->v.cell_range.cell_a.row,
			values_x->v.cell_range.cell_b.col,
			values_x->v.cell_range.cell_b.row,
			callback_function_sumxy,
			&items_x);
		if (ret != NULL)
		        return value_new_error (ei->pos, gnumeric_err_VALUE);
	} else
		return value_new_error (ei->pos,
					_("Array version not implemented!"));

        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet), TRUE,
			values_y->v.cell_range.cell_a.col,
			values_y->v.cell_range.cell_a.row,
			values_y->v.cell_range.cell_b.col,
			values_y->v.cell_range.cell_b.row,
			callback_function_sumxy,
			&items_y);
		if (ret != NULL)
		        return value_new_error (ei->pos, gnumeric_err_VALUE);
	} else
		return value_new_error (ei->pos,
					_("Array version not implemented!"));

	if (items_x.num != items_y.num)
		return value_new_error (ei->pos, gnumeric_err_NA);

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
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold "
	   "numbers 13, 22, 31, 33, and 39.  Then\n"
	   "SUMXMY2(A1:A5,B1:B5) equals 409.\n"
	   "\n"
	   "@SEEALSO=SUMSQ,SUMX2MY2,SUMX2PY2")
};

static Value *
gnumeric_sumxmy2 (FunctionEvalInfo *ei, Value **argv)
{
        Value      *values_x = argv[0];
        Value      *values_y = argv[1];
	math_sums_t items_x, items_y;
	Value      *ret;
	float_t     sum;
	GSList     *list1, *list2;

	items_x.num  = 0;
	items_x.list = NULL;
	items_y.num  = 0;
	items_y.list = NULL;

        if (values_x->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet), TRUE,
			values_x->v.cell_range.cell_a.col,
			values_x->v.cell_range.cell_a.row,
			values_x->v.cell_range.cell_b.col,
			values_x->v.cell_range.cell_b.row,
			callback_function_sumxy,
			&items_x);
		if (ret != NULL)
		        return value_new_error (ei->pos, gnumeric_err_VALUE);
	} else
		return value_new_error (ei->pos,
					_("Array version not implemented!"));

        if (values_y->type == VALUE_CELLRANGE) {
		ret = sheet_cell_foreach_range (
			eval_sheet (ei->pos->sheet, ei->pos->sheet), TRUE,
			values_y->v.cell_range.cell_a.col,
			values_y->v.cell_range.cell_a.row,
			values_y->v.cell_range.cell_b.col,
			values_y->v.cell_range.cell_b.row,
			callback_function_sumxy,
			&items_y);
		if (ret != NULL)
		        return value_new_error (ei->pos, gnumeric_err_VALUE);
	} else
		return value_new_error (ei->pos,
					_("Array version not implemented!"));

	if (items_x.num != items_y.num)
	        return value_new_error (ei->pos, gnumeric_err_NA);

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

/***************************************************************************/

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
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "23, 27, 28, 33, and 39.  Then\n"
	   "SUBTOTAL(1,A1:A5) equals 30.\n"
	   "SUBTOTAL(6,A1:A5) equals 22378356.\n"
	   "SUBTOTAL(7,A1:A5) equals 6.164414003.\n"
	   "SUBTOTAL(9,A1:A5) equals 150.\n"
	   "SUBTOTAL(11,A1:A5) equals 30.4.\n"
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
		return value_new_error (ei->pos, gnumeric_err_NUM);

	tree = (ExprTree *) expr_node_list->data;
	if (tree == NULL)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	val = eval_expr (ei->pos, tree);
	if (!val) return NULL;
	if (!VALUE_IS_NUMBER (val)) {
		value_release (val);
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	fun_nbr = value_get_as_int(val);
	value_release (val);
	if (fun_nbr < 1 || fun_nbr > 11)
		return value_new_error (ei->pos, gnumeric_err_NUM);

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

/***************************************************************************/

static char *help_seriessum = {
	N_("@FUNCTION=SERIESSUM\n"
	   "@SYNTAX=SERIESSUM(x,n,m,coefficients)\n"

	   "@DESCRIPTION="
	   "SERIESSUM function returns the sum of a power series.  @x is "
	   "the base of the power serie, @n is the initial power to raise @x, "
	   "@m is the increment to the power for each term in the series, and "
	   "@coefficients is the coefficents by which each successive power "
	   "of @x is multiplied. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "1.23, 2.32, 2.98, 3.42, and 4.33.  Then\n"
	   "SERIESSUM(3,1,2.23,A1:A5) equals 251416.43018.\n"
	   "\n"
	   "@SEEALSO=COUNT,SUM")
};

typedef struct {
        float_t sum;
        float_t x;
        float_t n;
        float_t m;
} math_seriessum_t;

static Value *
callback_function_seriessum (const EvalPosition *ep, Value *value,
			     void *closure)
{
	math_seriessum_t *mm = closure;
	float_t coefficient;

	if (!VALUE_IS_NUMBER (value))
		return value_terminate();

	coefficient = value_get_as_float (value);

	mm->sum += coefficient * pow(mm->x, mm->n);
	mm->n += mm->m;

	return NULL;
}

static Value *
gnumeric_seriessum (FunctionEvalInfo *ei, GList *nodes)
{
        math_seriessum_t p;
        ExprTree         *tree;
	Value            *val;
	float_t          x, n, m;

	if (nodes == NULL)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	/* Get x */
	tree = (ExprTree *) nodes->data;
	if (tree == NULL)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	val = eval_expr (ei->pos, tree);
	if (!val) return NULL;
	if (! VALUE_IS_NUMBER(val)) {
		value_release (val);
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	x = value_get_as_float (val);
	value_release (val);
	nodes = nodes->next;

	/* Get n */
	tree = (ExprTree *) nodes->data;
	if (tree == NULL)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	val = eval_expr (ei->pos, tree);
	if (!val) return NULL;
	if (! VALUE_IS_NUMBER(val)) {
		value_release (val);
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	n = value_get_as_int(val);
	value_release (val);

	nodes = nodes->next;
	if (nodes == NULL)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	/* Get m */
	tree = (ExprTree *) nodes->data;
	if (tree == NULL)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	val = eval_expr (ei->pos, tree);
	if (!val) return NULL;
	if (! VALUE_IS_NUMBER(val)) {
		value_release (val);
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	m = value_get_as_float(val);
	value_release (val);
	nodes = nodes->next;

	p.n = n;
	p.m = m;
	p.x = x;
	p.sum = 0;

	if (function_iterate_argument_values (ei->pos,
					      callback_function_seriessum,
					      &p, nodes,
					      TRUE) != NULL)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_float (p.sum);
}

/***************************************************************************/

static char *help_minverse = {
	N_("@FUNCTION=MINVERSE\n"
	   "@SYNTAX=MINVERSE(array)\n"

	   "@DESCRIPTION="
	   "MINVERSE function returns the inverse matrix of a given matrix. "
	   "\n"
	   "If the matrix cannot be inverted, MINVERSE returns #NUM! error. "
	   "If the matrix does not contain equal number of columns and "
	   "rows, MINVERSE returns #VALUE! error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=MMULT, MDETERM")
};


static Value *
callback_function_mmult_validate(Sheet *sheet, int col, int row,
				 Cell *cell, void *user_data)
{
        int * item_count = user_data;

	if (cell == NULL || cell->value == NULL ||
	    !VALUE_IS_NUMBER(cell->value))
	        return value_terminate();

	++(*item_count);
	return NULL;
}

static int
validate_range_numeric_matrix (const EvalPosition *ep, Value * matrix,
			       int *rows, int *cols,
			       char const **error_string)
{
	Value *res;
	int cell_count = 0;

	*cols = value_area_get_width (ep, matrix);
	*rows = value_area_get_height (ep, matrix);

	/* No checking needed for arrays */
	if (matrix->type == VALUE_ARRAY)
	    return FALSE;

	if (matrix->v.cell_range.cell_a.sheet !=
	    matrix->v.cell_range.cell_b.sheet) {
		*error_string = _("#3D MULT?");
		return TRUE;
	}

	res = sheet_cell_foreach_range (
		eval_sheet (matrix->v.cell_range.cell_a.sheet, ep->sheet), TRUE,
		matrix->v.cell_range.cell_a.col,
		matrix->v.cell_range.cell_a.row,
		matrix->v.cell_range.cell_b.col,
		matrix->v.cell_range.cell_b.row,
		callback_function_mmult_validate,
		&cell_count);

	if (res != NULL || cell_count != (*rows * *cols)) {
		/* As specified in the Excel Docs */
		*error_string = gnumeric_err_VALUE;
		return TRUE;
	}

	return FALSE;
}

static int
minverse(float_t *array, int cols, int rows)
{
        int     i, n, r;
	float_t pivot;

#define ARRAY(C,R) (*(array + (R) + (C) * rows))

	/* Pivot top-down */
	for (r=0; r<rows-1; r++) {
	        /* Select pivot row */
	        for (i = r; ARRAY(r, i) == 0; i++)
		        if (i == rows)
			        return 1;
		if (i != r)
		        for (n = 0; i<cols; n++) {
			        float_t tmp = ARRAY(n, r);
				ARRAY(n, r) = ARRAY(n, i);
				ARRAY(n, i) = tmp;
			}

		for (i=r+1; i<rows; i++) {
		        /* Calculate the pivot */
		        pivot = -ARRAY(r, i) / ARRAY(r, r);

			/* Add the pivot row */
			for (n=r; n<cols; n++)
			  ARRAY(n, i) += pivot * ARRAY(n, r);
		}
	}

	/* Pivot bottom-up */
	for (r=rows-1; r>0; r--) {
	        for (i=r-1; i>=0; i--) {
		        /* Calculate the pivot */
		        pivot = -ARRAY(r, i) / ARRAY(r, r);

			/* Add the pivot row */
			for (n=0; n<cols; n++)
			        ARRAY(n, i) += pivot * ARRAY(n, r);
		}
	}

	for (r=0; r<rows; r++) {
	        pivot = ARRAY(r, r);
		for (i=0; i<cols; i++)
		        ARRAY(i, r) /= pivot;
	}

#undef ARRAY

        return 0;
}

static Value *
gnumeric_minverse (FunctionEvalInfo *ei, Value **argv)
{
	EvalPosition const * const ep = ei->pos;

	int	r, rows;
	int	c, cols;
        Value   *res;
        Value   *values = argv[0];
	float_t *matrix;

	char const *error_string = NULL;

	if (validate_range_numeric_matrix (ep, values, &rows, &cols,
					   &error_string)) {
		return value_new_error (ei->pos, error_string);
	}

	/* Guarantee shape and non-zero size */
	if (cols != rows || !rows || !cols)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	matrix = g_new (float_t, rows*cols*2);
	for (c=0; c<cols; c++)
	        for (r=0; r<rows; r++) {
		        Value const * a =
			      value_area_get_x_y (ep, values, c, r);
		        *(matrix + r + c*cols) = value_get_as_float(a);
			if (c == r)
			        *(matrix + r + (c+cols)*rows) = 1;
			else
			        *(matrix + r + (c+cols)*rows) = 0;
		}

	if (minverse(matrix, cols*2, rows)) {
	        g_free (matrix);
		return value_new_error (ei->pos, gnumeric_err_NUM);
	}

	res = g_new (Value, 1);
	res->type = VALUE_ARRAY;
	res->v.array.x = cols;
	res->v.array.y = rows;
	res->v.array.vals = g_new (Value **, cols);

	for (c = 0; c < cols; ++c){
		res->v.array.vals [c] = g_new (Value *, rows);
		for (r = 0; r < rows; ++r){
			float_t tmp;

			tmp = *(matrix + r + (c+cols)*rows);
			res->v.array.vals[c][r] = value_new_float (tmp);
		}
	}

	g_free (matrix);
	return res;
}

/***************************************************************************/

static char *help_mmult = {
	N_("@FUNCTION=MMULT\n"
	   "@SYNTAX=MMULT(array1,array2)\n"

	   "@DESCRIPTION="
	   "MMULT function returns the matrix product of two arrays. The "
	   "result is an array with the same number of rows as @array1 and the "
	   "same number of columns as @array2. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=TRANSPOSE,MINVERSE")
};


static Value *
gnumeric_mmult (FunctionEvalInfo *ei, Value **argv)
{
	EvalPosition const * const ep = ei->pos;
	int	r, rows_a, rows_b, i;
	int	c, cols_a, cols_b;
        Value *res;
        Value *values_a = argv[0];
        Value *values_b = argv[1];
	char const *error_string = NULL;

	if (validate_range_numeric_matrix (ep, values_a, &rows_a, &cols_a,
					   &error_string) ||
	    validate_range_numeric_matrix (ep, values_b, &rows_b, &cols_b,
					   &error_string)){
		return value_new_error (ei->pos, error_string);
	}

	/* Guarantee shape and non-zero size */
	if (cols_a != rows_b || !rows_a || !rows_b || !cols_a || !cols_b)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	res = g_new (Value, 1);
	res->type = VALUE_ARRAY;
	res->v.array.x = cols_b;
	res->v.array.y = rows_a;
	res->v.array.vals = g_new (Value **, cols_b);

	for (c = 0; c < cols_b; ++c){
		res->v.array.vals [c] = g_new (Value *, rows_a);
		for (r = 0; r < rows_a; ++r){
			/* TODO TODO : Use ints for integer only areas */
			float_t tmp = 0.;
			for (i = 0; i < cols_a; ++i){
				Value const * a =
				    value_area_get_x_y (ep, values_a, i, r);
				Value const * b =
				    value_area_get_x_y (ep, values_b, c, i);
				tmp += value_get_as_float (a) *
				    value_get_as_float (b);
			}
			res->v.array.vals[c][r] = value_new_float (tmp);
		}
	}

	return res;
}

/***************************************************************************/

static char *help_mdeterm = {
	N_("@FUNCTION=MDETERM\n"
	   "@SYNTAX=MDETERM(array)\n"

	   "@DESCRIPTION="
	   "MDETERM function returns the determinant of a given matrix. "
	   "\n"
	   "If the matrix does not contain equal number of columns and "
	   "rows, MDETERM returns #VALUE! error. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that A1, ..., A4 contain numbers 2, 3, 7, and 3, "
	   "B1, ..., B4 4, 2, 4, and 1, C1, ..., C4 9, 4, 3, and 2, and "
	   "D1, ..., D4 7, 3, 6, and 5.  Then.\n"
	   "MDETERM(A1:D4) equals 148.\n"
	   "\n"
	   "@SEEALSO=MMULT, MINVERSE")
};

/* This function implements the LU-Factorization method for solving
 * matrix determinants.  At first the given matrix, A, is converted to
 * a product of a lower triangular matrix, L, and an upper triangluar
 * matrix, U, where A = L*U.  The lower triangular matrix, L, contains
 * ones along the main diagonal.
 * 
 * The determinant of the original matrix, A, is det(A)=det(L)*det(U).
 * The determinant of any triangular matrix is the product of the
 * elements along its main diagonal.  Since det(L) is always one, we
 * can simply write as follows: det(A) = det(U).  As you can see this
 * algorithm is quite efficient.
 *
 * (C) Copyright 1999 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 **/
static float_t
mdeterm(float_t *A, int cols)
{
        int i, j, n;
	float_t product, sum;
	float_t *L, *U;

#define ARRAY(A,C,R) (*((A) + (R) + (C) * cols))

	L = g_new(float_t, cols*cols);
	U = g_new(float_t, cols*cols);

	/* Initialize the matrices with value zero, except fill the L's
	 * main diagonal with ones */
	for (i=0; i<cols; i++)
	        for (n=0; n<cols; n++) {
		        if (i==n)
		                ARRAY(L, i, n) = 1;
			else
		                ARRAY(L, i, n) = 0;
			ARRAY(U, i, n) = 0;
		}

	/* Determine L and U so that A=L*U */
	for (n=0; n<cols; n++) {
	        for (i=n; i<cols; i++) {
		        sum = 0;
			for (j=0; j<n; j++)
			        sum += ARRAY(U, j, i) * ARRAY(L, n, j);
			ARRAY(U, n, i) = ARRAY(A, n, i) - sum;
		}

		for (i=n+1; i<cols; i++) {
		        sum = 0;
			for (j=0; j<i-1; j++)
			        sum += ARRAY(U, j, n) * ARRAY(L, i, j);
			ARRAY(L, i, n) = (ARRAY(A, i, n) - sum) /
			  ARRAY(U, n, n);
		}
	}

	/* Calculate det(U) */
	product = ARRAY(U, 0, 0);
	for (i=1; i<cols; i++)
	        product *= ARRAY(U, i, i);

#undef ARRAY

	g_free(L);
	g_free(U);

	return product;
}

static Value *
gnumeric_mdeterm (FunctionEvalInfo *ei, Value **argv)
{
	EvalPosition const * const ep = ei->pos;

	int	r, rows;
	int	c, cols;
        float_t res;
        Value   *values = argv[0];
	float_t *matrix;

	char const *error_string = NULL;

	if (validate_range_numeric_matrix (ep, values, &rows, &cols,
					   &error_string)) {
		return value_new_error (ei->pos, error_string);
	}

	/* Guarantee shape and non-zero size */
	if (cols != rows || !rows || !cols)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	matrix = g_new (float_t, rows*cols);
	for (c=0; c<cols; c++)
	        for (r=0; r<rows; r++) {
		        Value const * a =
			      value_area_get_x_y (ep, values, c, r);
		        *(matrix + r + c*cols) = value_get_as_float(a);
		}

	res = mdeterm(matrix, cols);
	g_free (matrix);

	return value_new_float(res);
}

/***************************************************************************/

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
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43 and the cells B1, B2, ..., B5 hold "
	   "numbers 13, 22, 31, 33, and 39.  Then\n"
	   "SUMPRODUCT(A1:A5,B1:B5) equals 3370.\n"
	   "\n"
	   "@SEEALSO=SUM,PRODUCT")
};

typedef struct {
        GSList   *components;
        GSList   *current;
        gboolean first;
} math_sumproduct_t;

static Value *
callback_function_sumproduct (const EvalPosition *ep, Value *value,
			      void *closure)
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
		        return value_terminate();
		*((float_t *) mm->current->data) *= x;
		mm->current = mm->current->next;
	}

	return NULL;
}

static Value *
gnumeric_sumproduct (FunctionEvalInfo *ei, GList *expr_node_list)
{
        math_sumproduct_t p;
	GSList            *current;
	float_t           sum;
	Value            *result=NULL;

	if (expr_node_list == NULL)
		return value_new_error (ei->pos, gnumeric_err_NUM);

	p.components = NULL;
	p.first      = TRUE;

	for ( ; result == NULL && expr_node_list;
	      expr_node_list = expr_node_list->next) {
		ExprTree *tree = (ExprTree *) expr_node_list->data;
		Value    *val;

		val = eval_expr (ei->pos, tree);

		if (val) {
		        p.current = p.components;
			result = function_iterate_do_value (
				ei->pos, callback_function_sumproduct, &p,
				val, TRUE);

			value_release (val);
		} else
		        break;
		p.first = FALSE;
	}

	sum = 0;
	for (current=p.components; current != NULL ; current = current->next) {
	        gpointer p = current->data;
	        sum += *((float_t *) p);
		g_free(current->data);
	}
	
	g_slist_free(p.components);

	if (expr_node_list)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_float (sum);
}

/***************************************************************************/

void
math_functions_init (void)
{
	FunctionDefinition *def;
	FunctionCategory *cat = function_get_category (_("Maths / Trig."));

	def = function_add_args  (cat, "abs",     "f",
				  "number",    &help_abs,      gnumeric_abs);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	function_add_args  (cat, "acos",    "f",
			    "number",    &help_acos,     gnumeric_acos);
	function_add_args  (cat, "acosh",   "f",
			    "number",    &help_acosh,    gnumeric_acosh);
	function_add_args  (cat, "asin",    "f",
			    "number",    &help_asin,     gnumeric_asin);
	function_add_args  (cat, "asinh",   "f",
			    "number",    &help_asinh,    gnumeric_asinh);
	function_add_args  (cat, "atan",    "f",
			    "number",    &help_atan,     gnumeric_atan);
	function_add_args  (cat, "atanh",   "f",
			    "number",    &help_atanh,    gnumeric_atanh);
	function_add_args  (cat, "atan2",   "ff",
			    "xnum,ynum", &help_atan2,    gnumeric_atan2);
	function_add_args  (cat, "cos",     "f",
			    "number",    &help_cos,      gnumeric_cos);
	function_add_args  (cat, "cosh",    "f",
			    "number",    &help_cosh,     gnumeric_cosh);
	function_add_args  (cat, "countif", "r?",
			    "range,criteria",
			    &help_countif,     gnumeric_countif);

	def = function_add_args  (cat, "ceil",    "f",
				  "number",    &help_ceil,     gnumeric_ceil);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_args  (cat, "ceiling", "ff",
				  "number,significance",
				  &help_ceiling,     gnumeric_ceiling);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	function_add_args  (cat, "degrees", "f",
			    "number",    &help_degrees,  gnumeric_degrees);
	function_add_args  (cat, "even",    "f",
			    "number",    &help_even,     gnumeric_even);
	function_add_args  (cat, "exp",     "f",
			    "number",    &help_exp,      gnumeric_exp);
	function_add_args  (cat, "fact",    "f",
			    "number",    &help_fact,     gnumeric_fact);
	function_add_args  (cat, "factdouble", "f",
			    "number",
			    &help_factdouble,  gnumeric_factdouble);
	function_add_args  (cat, "combin",  "ff",
			    "n,k",       &help_combin,   gnumeric_combin);

	def = function_add_args  (cat, "floor",   "f|f",
				  "number",    &help_floor,    gnumeric_floor);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	function_add_nodes (cat, "gcd",     "ff",
			    "number1,number2",
			    &help_gcd,         gnumeric_gcd);

	def = function_add_args  (cat, "int",     "f",
				  "number",    &help_int,      gnumeric_int);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	function_add_nodes (cat, "lcm",     0,
			    "",          &help_lcm,      gnumeric_lcm);
	function_add_args  (cat, "ln",      "f",
			    "number",    &help_ln,       gnumeric_ln);
	function_add_args  (cat, "log",     "f|f",
			    "number[,base]",
			    &help_log,         gnumeric_log);
	function_add_args  (cat, "log2",    "f",
			    "number",    &help_log2,     gnumeric_log2);
	function_add_args  (cat, "log10",   "f",
			    "number",    &help_log10,    gnumeric_log10);
	function_add_args  (cat, "mod",     "ff",
			    "num,denom", &help_mod,      gnumeric_mod);

	def = function_add_args  (cat, "mround",  "ff",
				  "number,multiple",
				  &help_mround,      gnumeric_mround);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	function_add_nodes (cat, "multinomial", 0,
			    "",          
			    &help_multinomial, gnumeric_multinomial);
	function_add_args  (cat, "odd" ,    "f",
			    "number",    &help_odd,      gnumeric_odd);
	function_add_args  (cat, "power",   "ff",
			    "x,y",       &help_power,    gnumeric_power);
	function_add_nodes (cat, "product", 0,
			    "number",    &help_product,  gnumeric_product);
	function_add_nodes (cat, "g_product", 0,
			    "number",    &help_g_product,  gnumeric_g_product);
	function_add_args  (cat, "quotient" , "ff",
			    "num,den",   &help_quotient, gnumeric_quotient);
	function_add_args  (cat, "radians", "f",
			    "number",    &help_radians,
			    gnumeric_radians);
	function_add_args  (cat, "rand",    "",
			    "",          &help_rand,     gnumeric_rand);
	function_add_args  (cat, "randbetween", "ff",
			    "bottom,top", 
			    &help_randbetween, gnumeric_randbetween);
	function_add_args  (cat, "roman",      "f|f",
			    "number[,type]",
			    &help_roman,       gnumeric_roman);

	def = function_add_args  (cat, "round",      "f|f",
				  "number[,digits]",
				  &help_round,       gnumeric_round);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_args  (cat, "rounddown",  "f|f",
				  "number,digits",
				  &help_rounddown,   gnumeric_rounddown);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_args  (cat, "roundup",    "f|f",
				  "number,digits",
				  &help_roundup,     gnumeric_roundup);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	function_add_nodes (cat, "seriessum", 0,
			    "x,n,m,coefficients",
			    &help_seriessum,   gnumeric_seriessum);
	function_add_args  (cat, "sign",    "f",
			    "number",    &help_sign,     gnumeric_sign);
	function_add_args  (cat, "sin",     "f",
			    "number",    &help_sin,      gnumeric_sin);
	function_add_args  (cat, "sinh",    "f",
			    "number",    &help_sinh,     gnumeric_sinh);
	function_add_args  (cat, "sqrt",    "f",
			    "number",    &help_sqrt,     gnumeric_sqrt);
	function_add_args  (cat, "sqrtpi",  "f",
			    "number",    &help_sqrtpi,   gnumeric_sqrtpi);
	function_add_nodes (cat, "subtotal", 0,
			    "function_nbr,ref1,ref2,...",
			    &help_subtotal,    gnumeric_subtotal);

	def = function_add_nodes (cat, "sum",     0,
				  "number1,number2,...",
				  &help_sum,	       gnumeric_sum);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	def = function_add_nodes (cat, "suma",    0,
				  "number1,number2,...",
				  &help_suma,	       gnumeric_suma);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	function_add_args  (cat, "sumif",   "r?|r",
			    "range,criteria[,actual_range]",
			    &help_sumif,       gnumeric_sumif);
	function_add_nodes (cat, "sumproduct",   0,
			    "range1,range2,...",
			    &help_sumproduct,  gnumeric_sumproduct);
	function_add_nodes (cat, "sumsq",   0,
			    "number",    &help_sumsq,    gnumeric_sumsq);
	function_add_args  (cat, "sumx2my2", "AA",
			    "array1,array2",
			    &help_sumx2my2,    gnumeric_sumx2my2);
	function_add_args  (cat, "sumx2py2", "AA",
			    "array1,array2",
			    &help_sumx2py2,    gnumeric_sumx2py2);
	function_add_args  (cat, "sumxmy2",  "AA",
			    "array1,array2",
			    &help_sumxmy2,     gnumeric_sumxmy2);
	function_add_args  (cat, "tan",     "f",
			    "number",    &help_tan,      gnumeric_tan);
	function_add_args  (cat, "tanh",    "f",
			    "number",    &help_tanh,     gnumeric_tanh);

	def = function_add_args  (cat, "trunc",   "f|f",
				  "number,digits",
				  &help_trunc,       gnumeric_trunc);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);

	function_add_args  (cat, "pi",      "",
			    "",          &help_pi,       gnumeric_pi);
	function_add_args  (cat, "mmult",   "AA",
			    "array1,array2",
			    &help_mmult,       gnumeric_mmult);
	function_add_args  (cat, "minverse","A",
			    "array",
			    &help_minverse,    gnumeric_minverse);
	function_add_args  (cat, "mdeterm", "A",
			    "array[,matrix_type[,bandsize]]",
			    &help_mdeterm,     gnumeric_mdeterm);
#if 0
	function_add_args  (cat, "logmdeterm", "A|si",
			    "array[,matrix_type[,bandsize]]",
			    &help_logmdeterm, gnumeric_logmdeterm);
#endif
}
