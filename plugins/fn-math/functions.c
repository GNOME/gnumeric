/*
 * fn-math.c:  Built in mathematical functions and functions registration
 * (C) 1998 The Free Software Foundation
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

static Value *
gnumeric_abs (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = fabs (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_acos (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	float_t t;

	t = value_get_as_double (argv [0]);
	if ((t < -1.0) || (t > 1.0)){
		*error_string = _("acos - domain error");
		return NULL;
	}
	v = g_new (Value, 1);
	v->type = VALUE_FLOAT;
	v->v.v_float = acos (t);

	return v;
}

static Value *
gnumeric_acosh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	float_t t;

	t = value_get_as_double (argv [0]);
	if (t < 1.0){
		*error_string = _("acosh - domain error");
		return NULL;
	}
	v = g_new (Value, 1);
	v->type = VALUE_FLOAT;
	v->v.v_float = acosh (t);

	return v;
}

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

#if 0
/* help template */
static char *help_ = {
	N_("<function></function>"
	   "<syntax>(b1, b2, ...)</syntax>n"

	   "<description>"
	   ""
	   "<p>"

	   ""
	   ""
	   "<p>"
	   
	   ""
	   ""
	   ""
	   "</description>"
	   "<seealso></seealso>")
};

#endif

static char *help_floor = {
	N_("<function>FLOOR</function>"
	   "<syntax>FLOOR(b1)</syntax>n"

	   "<description>The FLOOR function rounds b1 down to the next nearest"
	   "integer."
	   "<p>"

	   "Performing this function on a string or empty cell simply does nothing."
	   "<p>"
	   
	   "</description>"
	   "<seealso>CEIL, ABS</seealso>")
};

static char *help_ceil = {
	N_("<function>CEIL</function>"
	   "<syntax>CEIL(b1)</syntax>n"

	   "<description>The CEIL function rounds b1 up to the next nearest"
	   "integer."
	   "<p>"

	   "Performing this function on a string or empty cell simply does nothing."
	   "<p>"
	   
	   "</description>"
	   "<seealso>ABS, FLOOR</seealso>")
};

static char *help_abs = {
	N_("<function>ABS</function>"
	   "<syntax>ABS(b1)</syntax>n"

	   "<description>Implements the Absolute Value function:  the result is "
	   "to drop the negative sign (if present).  This can be done for "
	   "integers and floating point numbers.<p>"

	   "Performing this function on a string or empty cell simply does nothing."
	   "<p>"
	   
	   "</description>"
	   "<seealso>CEIL, FLOOR</seealso>")
};

static char *help_and = {
	N_("<function>AND</function>"
	   "<syntax>AND(b1, b2, ...)</syntax>n"

	   "<description>Implements the logical AND function: the result is TRUE "
	   "if all of the expression evaluates to TRUE, otherwise it returns "
	   "FALSE.<p>"

	   "b1, trough bN are expressions that should evaluate to TRUE or FALSE."
	   "If an integer or floating point value is provided zero is considered "
	   "FALSE and anything else is TRUE.<p>"
	   
	   "If the values contain strings or empty cells those values are "
	   "ignored.  If no logical values are provided, then the error '#VALUE!' "
	   "is returned. "
	   "</description>"
	   "<seealso>OR</seealso>")
};

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

static Value *
gnumeric_asin (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	float_t t;

	t = value_get_as_double (argv [0]);
	if ((t < -1.0) || (t > 1.0)){
		*error_string = _("asin - domain error");
		return NULL;
	}
	v = g_new (Value, 1);
	v->type = VALUE_FLOAT;
	v->v.v_float = asin (t);

	return v;
}

static Value *
gnumeric_asinh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;

	v = g_new (Value, 1);
	v->type = VALUE_FLOAT;
	v->v.v_float = asinh (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_atan (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = atan (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_atanh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	float_t t;

	t = value_get_as_double (argv [0]);
	if ((t <= -1.0) || (t >= 1.0)){
		*error_string = _("atanh: domain error");
		return NULL;
	}
	v->type = VALUE_FLOAT;
	v->v.v_float = atanh (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_atan2 (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = atan2 (value_get_as_double (argv [0]),
			      value_get_as_double (argv [1]));
	
	return v;
}

static char *help_average = {
	N_("<function>AVERAGE</function>"
	   "<syntax>AVERAGE(value1, value2,...)</syntax>"

	   "<description>"
	   "Computes the average of all the values and cells referenced in the "
	   "argument list.  This is equivalent to the sum of the arguments divided "
	   "by the count of the arguments."
	   "</description>"
	   "<seealso>SUM, COUNT</seealso>")
};

static Value *
gnumeric_average (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *result;
	Value *sum, *count;

	sum = gnumeric_sum (tsheet, expr_node_list, eval_col, eval_row, error_string);
	if (!sum)
		return NULL;
	
	count = gnumeric_count (tsheet, expr_node_list, eval_col, eval_row, error_string);
	if (!count){
		value_release (sum);
		return NULL;
	}

	result = g_new (Value, 1);
	result->type = VALUE_FLOAT;
	result->v.v_float = sum->v.v_float / count->v.v_int;
	
	value_release (count);
	value_release (sum);
	
	return result;
}

static Value *
gnumeric_ceil (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = ceil (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_bin2dec (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *value;
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
	value = g_new (Value, 1);
	value->type = VALUE_INTEGER;
	value->v.v_int = result;
	
	return value;
}

static Value *
gnumeric_cos (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = cos (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_cosh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = cos (value_get_as_double (argv [0]));

	return v;
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

static Value *
gnumeric_degrees (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = (value_get_as_double (argv [0]) * 180.0) / M_PI;

	return v;
}

static Value *
gnumeric_exp (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = exp (value_get_as_double (argv [0]));

	return v;
}

static float_t
fact (int n)
{
	if (n == 0)
		return 1;
	return (n * fact (n - 1));
}

static Value *
gnumeric_fact (struct FunctionDefinition *id, Value *argv [], char **error_string)
{
	Value *res;
	int i;

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
	if (i > 69){
		i = 69;
	}
	res->type = VALUE_FLOAT;
	res->v.v_float = fact (i);
	return res;
}

static Value *
gnumeric_floor (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = floor (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_int (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	float_t t;

	t = value_get_as_double (argv [0]);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = t > 0.0 ? floor (t) : ceil (t);

	return v;
}

static Value *
gnumeric_log (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	float_t t;

	t = value_get_as_double (argv [0]);
	if (t < 0.0){
		*error_string = _("log: domain error");
		return NULL;
	}
	v = g_new (Value, 1);
	v->type = VALUE_FLOAT;
	v->v.v_float = log (t);

	return v;
}

static Value *
gnumeric_log2 (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	float_t t;

	t = value_get_as_double (argv [0]);
	if (t < 0.0){
		*error_string = _("log2: domain error");
		return NULL;
	}
	v = g_new (Value, 1);
	v->type = VALUE_FLOAT;
	v->v.v_float = log (t) / M_LN2;

	return v;
}

static Value *
gnumeric_log10 (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	float_t t;

	t = value_get_as_double (argv [0]);
	if (t < 0.0){
		*error_string = _("log10: domain error");
		return NULL;
	}
	v = g_new (Value, 1);
	v->type = VALUE_FLOAT;
	v->v.v_float = log10 (t);

	return v;
}

enum {
	OP_MIN,
	OP_MAX
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
			if (mm->operation == OP_MIN){
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
			if (mm->operation == OP_MIN){
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

	closure.operation = OP_MIN;
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

	closure.operation = OP_MAX;
	closure.found  = 0;
	closure.result = g_new (Value, 1);
	closure.result->type = VALUE_FLOAT;
	closure.result->v.v_float = 0;

	function_iterate_argument_values (sheet, callback_function_min_max,
					  &closure, expr_node_list,
					  eval_col, eval_row, error_string);
	return 	closure.result;
}

static char *help_or = {
	N_("<function>OR</function>"
	   "<syntax>OR(b1, b2, ...)</syntax>"

	   "<description>"
	   "Implements the logical OR function: the result is TRUE if any of the"
	   "values evaluated to TRUE.<p>"
	   "b1, trough bN are expressions that should evaluate to TRUE or FALSE."
	   "If an integer or floating point value is provided zero is considered"
	   "FALSE and anything else is TRUE.<p>"
	   "If the values contain strings or empty cells those values are "
	   "ignored.  If no logical values are provided, then the error '#VALUE!'"
	   "is returned."
	   "</description>"
	   "<seealso>AND</seealso>")
};


static int
callback_function_or (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	Value *result = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		if (value->v.v_int == 1){
			result->v.v_int = 1;
			return FALSE;
		} else
			result->v.v_int = 0;
		break;

	case VALUE_FLOAT:
		if (value->v.v_float == 0.0){
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


static Value *
gnumeric_radians (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = (value_get_as_double (argv [0]) * M_PI) / 180;

	return v;
}

static Value *
gnumeric_sin (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = sin (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_sinh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = sinh (value_get_as_double (argv [0]));
	
	return v;
}

static int
callback_function_sum (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	Value *result = (Value *) closure;

	switch (value->type){
	case VALUE_INTEGER:
		result->v.v_float += value->v.v_int;
		break;
		
	case VALUE_FLOAT:
		result->v.v_float += value->v.v_float;
		break;
		
	case VALUE_STRING:
		result->v.v_float += atof (value->v.str->str);
		break;

	default:
		g_warning ("Unknown VALUE type in callback_function_sum");
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
	result->type = VALUE_FLOAT;
	result->v.v_float = 0.0;
	
	function_iterate_argument_values (sheet, callback_function_sum, result, expr_node_list,
					  eval_col, eval_row, error_string);

	return result;
}

static Value *
gnumeric_tan (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = tan (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_tanh (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = tanh (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_pi (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = M_PI;

	return v;
}

FunctionDefinition math_functions [] = {
	{ "abs",     "f",    "number",    &help_abs, NULL, gnumeric_abs },
	{ "acos",    "f",    "number",    NULL,      NULL, gnumeric_acos },
	{ "acosh",   "f",    "number",    NULL,      NULL, gnumeric_acosh },
	{ "and",     0,      "",          &help_and, gnumeric_and, NULL },
	{ "asin",    "f",    "number",    NULL,      NULL, gnumeric_asin },
	{ "asinh",   "f",    "number",    NULL,      NULL, gnumeric_asinh },
	{ "atan",    "f",    "number",    NULL,      NULL, gnumeric_atan },
	{ "atanh",   "f",    "number",    NULL,      NULL, gnumeric_atanh },
	{ "atan2",   "ff",   "xnum,ynum", NULL,      NULL, gnumeric_atan2 },
	/* avedev */
	{ "average", 0,      "",          &help_average, gnumeric_average, NULL },
	/* besseli */
	/* besselj */
	/* besselk */
	/* bessely */
	{ "bin2dec", "?",    "number",    NULL,       NULL, gnumeric_bin2dec },
	{ "cos",     "f",    "number",    NULL,       NULL, gnumeric_cos },
	{ "cosh",    "f",    "number",    NULL,       NULL, gnumeric_cosh },
	{ "count",   0,      "",          NULL,       gnumeric_count, NULL },
	{ "ceil",    "f",    "number",    &help_ceil, NULL, gnumeric_ceil },
	{ "degrees", "f",    "number",    NULL,       NULL, gnumeric_degrees },
	{ "exp",     "f",    "number",    NULL,       NULL, gnumeric_exp },
	{ "fact",    "f",    "number",    NULL,       NULL, gnumeric_fact },
	{ "floor",   "f",    "number",    &help_floor,NULL, gnumeric_floor },
	{ "int",     "f",    "number",    NULL,       NULL, gnumeric_int },
	{ "log",     "f",    "number",    NULL,       NULL, gnumeric_log },
	{ "log2",    "f",    "number",    NULL,       NULL, gnumeric_log2 },
	{ "log10",   "f",    "number",    NULL,       NULL, gnumeric_log10 },
	{ "min",     0,      "",          NULL,       gnumeric_min, NULL },
	{ "max",     0,      "",          NULL,       gnumeric_max, NULL },
	{ "or",      0,      "",          &help_or,   gnumeric_or, NULL },
	{ "radians", "f",    "number",    NULL,       NULL, gnumeric_radians },
	{ "sin",     "f",    "number",    NULL,       NULL, gnumeric_sin },
	{ "sinh",    "f",    "number",    NULL,       NULL, gnumeric_sinh },
	{ "sum",     0,      "number",    NULL,       gnumeric_sum, NULL },
	{ "tan",     "f",    "number",    NULL,       NULL, gnumeric_tan },
	{ "tanh",    "f",    "number",    NULL,       NULL, gnumeric_tanh },
	{ "pi",      "",     "", NULL,    NULL,       gnumeric_pi },
	{ NULL, NULL },
};

