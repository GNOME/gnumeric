/*
 * func.c:  Built in mathematical functions and functions registration
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

static Value *
gnumeric_abs (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = fabs (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_acos (Value *argv [], char **error_string)
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
gnumeric_acosh (Value *argv [], char **error_string)
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

static Value *
gnumeric_asin (Value *argv [], char **error_string)
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
gnumeric_asinh (Value *argv [], char **error_string)
{
	Value *v;

	v = g_new (Value, 1);
	v->type = VALUE_FLOAT;
	v->v.v_float = asinh (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_atan (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = atan (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_atanh (Value *argv [], char **error_string)
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
gnumeric_atan2 (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = atan2 (value_get_as_double (argv [0]),
			      value_get_as_double (argv [1]));
	
	return v;
}

static Value *
gnumeric_ceil (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = ceil (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_cos (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = cos (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_cosh (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = cos (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_degrees (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = (value_get_as_double (argv [0]) * 180.0) / M_PI;

	return v;
}

static Value *
gnumeric_exp (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = exp (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_floor (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = floor (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_int (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	float_t t;

	t = value_get_as_double (argv [0]);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = t > 0.0 ? floor (t) : ceil (t);

	return v;
}

static Value *
gnumeric_log (Value *argv [], char **error_string)
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
gnumeric_log2 (Value *argv [], char **error_string)
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
gnumeric_log10 (Value *argv [], char **error_string)
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

static Value *
gnumeric_radians (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = (value_get_as_double (argv [0]) * M_PI) / 180;

	return v;
}

static Value *
gnumeric_sin (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = sin (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_sinh (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = sinh (value_get_as_double (argv [0]));
	
	return v;
}

static float_t
gnumeric_sum_add_value (Sheet *sheet, Value *v)
{
	float_t total = 0.0;
	
	switch (v->type){
	case VALUE_INTEGER:
		return v->v.v_int;
		break;
		
	case VALUE_FLOAT:
		return v->v.v_float;
		break;
		
	case VALUE_STRING:
		return atof (v->v.str->str);
		break;
		
	case VALUE_CELLRANGE: {
		int col     = v->v.cell_range.cell_a.col;
		int frow    = v->v.cell_range.cell_a.row;
		int top_col = v->v.cell_range.cell_b.col;
		int top_row = v->v.cell_range.cell_b.row;
		int row;
		Cell *cell;

		{
			static warn_shown;

			if (!warn_shown){
				g_warning ("SUM is not being smart right now, "
					   "it should use the cell iterator\n");
				warn_shown = 1;
			}
		}
		
		for (; col <= top_col; col++){
			for (row = frow; row <= top_row; row++){
				cell = sheet_cell_get (sheet, col, row);
				if (!cell)
					continue;
				if (!cell->value)
					continue;
				total += gnumeric_sum_add_value (sheet, cell->value);
			}
		}
		return total;
		
	}

	case VALUE_ARRAY: {
		GList *l;

		for (l = v->v.array; l; l = l->next)
			total += gnumeric_sum_add_value (sheet, l->data);
		
		return total;
	}
	default:
		g_warning ("VALUE type not handled in SUM\n");
		return 0.0;
	}
}

static Value *
gnumeric_sum (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *result;
	Sheet *sheet = (Sheet *) tsheet;
	float_t total = 0.0;

	result = g_new (Value, 1);
	result->type = VALUE_FLOAT;
	
	for (; expr_node_list; expr_node_list = expr_node_list->next){
		ExprTree *tree = (ExprTree *) expr_node_list->data;
		Value *v;

		v = eval_expr (tsheet, tree, eval_col, eval_row, error_string);
		total += gnumeric_sum_add_value (sheet, v);
		value_release (v);
	}
	result->v.v_float = total;
	
	return result;
}

static Value *
gnumeric_tan (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = tan (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_tanh (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = tanh (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_pi (Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = M_PI;

	return v;
}

FunctionDefinition math_functions [] = {
	{ "abs",     "f",    "number", NULL, gnumeric_abs },
	{ "acos",    "f",    "number", NULL, gnumeric_acos },
	{ "acosh",   "f",    "number", NULL, gnumeric_acosh },
	{ "asin",    "f",    "number", NULL, gnumeric_asin },
	{ "asinh",   "f",    "number", NULL, gnumeric_asinh },
	{ "atan",    "f",    "number", NULL, gnumeric_atan },
	{ "atanh",   "f",    "number", NULL, gnumeric_atanh },
	{ "atan2",   "ff",   "xnum,ynum", NULL, gnumeric_atan2 },
	{ "cos",     "f",    "number", NULL, gnumeric_cos },
	{ "cosh",    "f",    "number", NULL, gnumeric_cosh },
	{ "ceil",    "f",    "number", NULL, gnumeric_ceil },
	{ "degrees", "f",    "number", NULL, gnumeric_degrees },
	{ "exp",     "f",    "number", NULL, gnumeric_exp },
	{ "floor",   "f",    "number", NULL, gnumeric_floor },
	{ "int",     "f",    "number", NULL, gnumeric_int },
	{ "log",     "f",    "number", NULL, gnumeric_log },
	{ "log2",    "f",    "number", NULL, gnumeric_log2 },
	{ "log10",   "f",    "number", NULL, gnumeric_log10 },
	{ "radians", "f",    "number", NULL, gnumeric_radians },
	{ "sin",     "f",    "number", NULL, gnumeric_sin },
	{ "sinh",    "f",    "number", NULL, gnumeric_sinh },
	{ "sum",     0,      "number", gnumeric_sum, NULL },
	{ "tan",     "f",    "number", NULL, gnumeric_tan },
	{ "tanh",    "f",    "number", NULL, gnumeric_tanh },
	{ "pi",      "",     "", NULL, gnumeric_pi },
	{ NULL, NULL },
};

static void
install_symbols (FunctionDefinition *functions)
{
	int i;
	
	for (i = 0; functions [i].name; i++){
		symbol_install (functions [i].name, SYMBOL_FUNCTION, &functions [i]);
	}
}

void
functions_init (void)
{
	install_symbols (math_functions);
	install_symbols (sheet_functions);
}
