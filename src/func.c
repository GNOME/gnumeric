/*
 * func.c:  Built in functions
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

static Value *
gnumeric_abs (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = fabs (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_acos (int argc, Value *argv [], char **error_string)
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
gnumeric_acosh (int argc, Value *argv [], char **error_string)
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
gnumeric_asin (int argc, Value *argv [], char **error_string)
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
gnumeric_asinh (int argc, Value *argv [], char **error_string)
{
	Value *v;

	v = g_new (Value, 1);
	v->type = VALUE_FLOAT;
	v->v.v_float = asinh (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_atan (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = atan (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_atanh (int argc, Value *argv [], char **error_string)
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
gnumeric_atan2 (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = atan2 (value_get_as_double (argv [0]),
			      value_get_as_double (argv [1]));
	
	return v;
}

static Value *
gnumeric_ceil (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = ceil (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_cos (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = cos (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_cosh (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = cos (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_exp (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = exp (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_floor (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = floor (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_int (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	float_t t;

	t = value_get_as_double (argv [0]);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = t > 0.0 ? floor (t) : ceil (t);

	return v;
}

static Value *
gnumeric_log (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = log (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_log10 (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = log10 (value_get_as_double (argv [0]));

	return v;
}

static Value *
gnumeric_sin (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = sin (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_sinh (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);
	
	v->type = VALUE_FLOAT;
	v->v.v_float = sinh (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_tan (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = tan (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_tanh (int argc, Value *argv [], char **error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = tanh (value_get_as_double (argv [0]));
	
	return v;
}

static Value *
gnumeric_pi (int argc, Value *argv [], char *error_string)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = M_PI;

	return v;
}

FunctionDefinition internal_functions [] = {
	{ "abs",   "f",    VALUE_FLOAT, NULL, gnumeric_abs },
	{ "acos",  "f",    VALUE_FLOAT, NULL, gnumeric_acos },
	{ "acosh", "f",    VALUE_FLOAT, NULL, gnumeric_acosh },
	{ "asin",  "f",    VALUE_FLOAT, NULL, gnumeric_asin },
	{ "asinh", "f",    VALUE_FLOAT, NULL, gnumeric_asinh },
	{ "atan",  "f",    VALUE_FLOAT, NULL, gnumeric_atan },
	{ "atanh", "f",    VALUE_FLOAT, NULL, gnumeric_atanh },
	{ "atan2", "ff",   VALUE_FLOAT, NULL, gnumeric_atan2 },
	{ "cos",   "f",    VALUE_FLOAT, NULL, gnumeric_cos },
	{ "cosh",  "f",    VALUE_FLOAT, NULL, gnumeric_cosh },
	{ "ceil",  "f",    VALUE_FLOAT, NULL, gnumeric_ceil },
	{ "exp",   "f",    VALUE_FLOAT, NULL, gnumeric_exp },
	{ "floor", "f",    VALUE_FLOAT, NULL, gnumeric_floor },
	{ "int",   "f",    VALUE_FLOAT, NULL, gnumeric_int },
	{ "log",   "f",    VALUE_FLOAT, NULL, gnumeric_log },
	{ "log10", "f",    VALUE_FLOAT, NULL, gnumeric_log10 },
	{ "sin",   "f",    VALUE_FLOAT, NULL, gnumeric_sin },
	{ "sinh",  "f",    VALUE_FLOAT, NULL, gnumeric_sinh },
	{ "tan",   "f",    VALUE_FLOAT, NULL, gnumeric_tan },
	{ "tanh",  "f",    VALUE_FLOAT, NULL, gnumeric_tanh },
	{ "pi",    "",     VALUE_FLOAT, NULL, gnumeric_pi },
	{ NULL, NULL },
};

void
functions_init (void)
{
	int i;
	
	for (i = 0; internal_functions [i].name; i++){
		symbol_install (internal_functions [i].name,
				SYMBOL_FUNCTION,
				&internal_functions [i]);
	}
}
