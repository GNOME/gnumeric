/*
 * value.c:  Utilies for handling, creating, removing values.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org).
 *   Jody Goldberg (jgolderg@home.com)
 */

#include <config.h>
#include <locale.h>
#include "value.h"
#include "utils.h"

Value *
value_new_bool (gboolean b)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_BOOLEAN;
	v->v.v_bool = b;

	return v;
}

Value *
value_new_error (EvalPosition const *ep, char const *mesg)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_ERROR;
	v->v.error.mesg = string_get (mesg);

	return v;
}

Value *
value_new_string (const char *str)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_STRING;
	v->v.str = string_get (str);

	return v;
}

Value *
value_new_int (int i)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_INTEGER;
	v->v.v_int = i;

	return v;
}

Value *
value_new_float (float_t f)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_FLOAT;
	v->v.v_float = f;

	return v;
}

Value *
value_new_cellrange (const CellRef *a, const CellRef *b)
{
	Value *v = g_new (Value, 1);

	v->type = VALUE_CELLRANGE;
	v->v.cell_range.cell_a = *a;
	v->v.cell_range.cell_b = *b;

	return v;
}

Value *
value_new_array (guint width, guint height)
{
	int x, y;

	Value *v = g_new (Value, 1);
	v->type = VALUE_ARRAY;
	v->v.array.x = width;
	v->v.array.y = height;
	v->v.array.vals = g_new (Value **, width);

	for (x = 0; x < width; x++){
		v->v.array.vals [x] = g_new (Value *, height);
		for (y = 0; y < height; y++)
			v->v.array.vals[x][y] = value_new_int (0);
	}
	return v;
}

void
value_release (Value *value)
{
	g_return_if_fail (value != NULL);

#if 0
	/* FIXME FIXME FIXME : Catch if this happens */
	g_return_if_fail (value != value_terminate());
#endif

	switch (value->type){
	case VALUE_BOOLEAN:
		break;

	case VALUE_ERROR:
		string_unref (value->v.error.mesg);
		break;

	case VALUE_STRING:
		string_unref (value->v.str);
		break;

	case VALUE_INTEGER:
		mpz_clear (value->v.v_int);
		break;

	case VALUE_FLOAT:
		mpf_clear (value->v.v_float);
		break;

	case VALUE_ARRAY:{
		guint lpx, lpy;

		for (lpx = 0; lpx < value->v.array.x; lpx++){
			for (lpy = 0; lpy < value->v.array.y; lpy++)
				value_release (value->v.array.vals [lpx][lpy]);
			g_free (value->v.array.vals [lpx]);
		}

		g_free (value->v.array.vals);
		break;
	}

	case VALUE_CELLRANGE:
		break;

	default:
		/*
		 * If we don't recognize the type this is probably garbage.
		 * Do not free it to avoid heap corruption
		 */
		g_warning ("value_release problem\n");
		return;
	}

	/* poison the type before freeing to help catch dangling pointers */
	value->type = 9999;
	g_free (value);
}

/* Debugging utility to print a Value */
void
value_dump (const Value *value)
{
	switch (value->type){
	case VALUE_ERROR:
		printf ("ERROR: %s\n", value->v.error.mesg->str);
		break;

	case VALUE_BOOLEAN:
		printf ("BOOLEAN: %s\n", value->v.v_bool ?_("TRUE"):_("FALSE"));
		break;

	case VALUE_STRING:
		printf ("STRING: %s\n", value->v.str->str);
		break;

	case VALUE_INTEGER:
		printf ("NUM: %d\n", value->v.v_int);
		break;

	case VALUE_FLOAT:
		printf ("Float: %f\n", value->v.v_float);
		break;

	case VALUE_ARRAY: {
		int x, y;
		
		printf ("Array: { ");
		for (y = 0; y < value->v.array.y; y++)
			for (x = 0; x < value->v.array.x; x++)
				value_dump (value->v.array.vals [x][y]);
		printf ("}\n");
		break;
	}
	case VALUE_CELLRANGE: {
		CellRef const *c = &value->v.cell_range.cell_a;
		Sheet const *sheet = c->sheet;

		printf ("CellRange\n");
		if (sheet && sheet->name)
			printf ("'%s':", sheet->name);
		else
			printf ("%p :", sheet);
		printf ("%s%s%s%d\n",
			(c->col_relative ? "":"$"), col_name(c->col),
			(c->row_relative ? "":"$"), c->row+1);
		c = &value->v.cell_range.cell_b;
		if (sheet && sheet->name)
			printf ("'%s':", sheet->name);
		else
			printf ("%p :", sheet);
		printf ("%s%s%s%d\n",
			(c->col_relative ? "":"$"), col_name(c->col),
			(c->row_relative ? "":"$"), c->row+1);
		break;
	}
	default:
		printf ("Unhandled item type\n");
	}
}

/*
 * Makes a copy of a Value
 */
Value *
value_duplicate (const Value *value)
{
	Value *new_value;

	g_return_val_if_fail (value != NULL, NULL);
	new_value = g_new (Value, 1);
	value_copy_to (new_value, value);

	return new_value;
}

/*
 * Copies a Value.
 */
void
value_copy_to (Value *dest, const Value *source)
{
	g_return_if_fail (dest != NULL);
	g_return_if_fail (source != NULL);

	dest->type = source->type;

	switch (source->type){
	case VALUE_BOOLEAN:
		dest->v.v_bool = source->v.v_bool;
		break;

	case VALUE_ERROR:
		string_ref (dest->v.error.mesg = source->v.error.mesg);
		break;

	case VALUE_STRING:
		dest->v.str = source->v.str;
		string_ref (dest->v.str);
		break;

	case VALUE_INTEGER:
		dest->v.v_int = source->v.v_int;
		break;

	case VALUE_FLOAT:
		dest->v.v_float = source->v.v_float;
		break;

	case VALUE_ARRAY: {
		value_array_copy_to (dest, source);
		break;
	}
	case VALUE_CELLRANGE:
		dest->v.cell_range = source->v.cell_range;
		break;

	default:
		g_warning ("value_copy_to problem\n");
		break;
	}
}

/*
 * Casts a value to float if it is integer, and returns
 * a new Value * if required
 */
Value *
value_cast_to_float (Value *v)
{
	Value *newv;

	g_return_val_if_fail (VALUE_IS_NUMBER (v), NULL);

	if (v->type == VALUE_FLOAT)
		return v;
	if (v->type == VALUE_BOOLEAN) {
		value_release (v);
		return value_new_float(v->v.v_bool ? 1. : 0.);
	}

	newv = g_new (Value, 1);
	newv->type = VALUE_FLOAT;
	mpf_set_z (newv->v.v_float, v->v.v_int);
	value_release (v);

	return newv;
}

gboolean
value_get_as_bool (Value const *v, gboolean *err)
{
	*err = FALSE;

	switch (v->type) {
	case VALUE_BOOLEAN:
		return v->v.v_bool;

	case VALUE_STRING:
		/* FIXME FIXME FIXME */
		/* Use locale to support TRUE, FALSE */
		return atoi (v->v.str->str) != 0;

	case VALUE_INTEGER:
		return v->v.v_int != 0;

	case VALUE_FLOAT:
		return v->v.v_float != 0.0;

	default:
		g_warning ("Unhandled value in value_get_boolean");

	case VALUE_EMPTY:
	case VALUE_CELLRANGE:
	case VALUE_ARRAY:
	case VALUE_ERROR:
		*err = TRUE;
	}
	return FALSE;
}

/*
 * simplistic value rendering
 */
char *
value_get_as_string (const Value *value)
{
	struct lconv *locinfo;
	
	char const * separator = ",";
	locinfo = localeconv ();
	if (locinfo->decimal_point &&
	    locinfo->decimal_point [0] == ',' &&
	    locinfo->decimal_point [1] == 0)
		separator = ";";

	switch (value->type){
	case VALUE_ERROR: 
		return g_strdup (value->v.error.mesg->str);

	case VALUE_BOOLEAN: 
		return g_strdup (value->v.v_bool ? _("TRUE") : _("FALSE"));

	case VALUE_STRING:
		return g_strdup (value->v.str->str);

	case VALUE_INTEGER:
		return g_strdup_printf ("%d", value->v.v_int);

	case VALUE_FLOAT:
		return g_strdup_printf ("%.*g", DBL_DIG, value->v.v_float);

	case VALUE_ARRAY: {
		GString *str = g_string_new ("{");
		guint lpx, lpy;
		char *ans;

		for (lpy = 0; lpy < value->v.array.y; lpy++){
			for (lpx = 0; lpx < value->v.array.x; lpx++){
				const Value *v = value->v.array.vals [lpx][lpy];

				g_return_val_if_fail (v->type == VALUE_STRING ||
						      v->type == VALUE_FLOAT ||
						      v->type == VALUE_INTEGER,
						      "Duff Array contents");
				if (lpx)
					g_string_sprintfa (str, separator);
				if (v->type == VALUE_STRING)
					g_string_sprintfa (str, "\"%s\"",
							   v->v.str->str);
				else
					g_string_sprintfa (str, "%g",
							   value_get_as_float (v));
			}
			if (lpy < value->v.array.y-1)
				g_string_sprintfa (str, ";");
		}
		g_string_sprintfa (str, "}");
		ans = str->str;
		g_string_free (str, FALSE);
		return ans;
	}

	case VALUE_CELLRANGE: 
		return value_cellrange_get_as_string (value, TRUE);
	
	default:
		g_warning ("value_string problem\n");
		break;
	}

	return g_strdup ("Internal problem");
}

int
value_get_as_int (const Value *v)
{
	switch (v->type)
	{
	case VALUE_STRING:
		return atoi (v->v.str->str);

	case VALUE_CELLRANGE:
		g_warning ("Getting range as a int: what to do?");
		return 0;

	case VALUE_INTEGER:
		return v->v.v_int;

	case VALUE_ARRAY:
		return 0;

	case VALUE_FLOAT:
		return (int) v->v.v_float;

	case VALUE_BOOLEAN:
		return v->v.v_bool ? 1 : 0;

	case VALUE_ERROR:
		return 0;

	default:
		g_warning ("value_get_as_int unknown type\n");
		return 0;
	}
	return 0.0;
}

float_t
value_get_as_float (const Value *v)
{
	switch (v->type)
	{
	case VALUE_STRING:
		return atof (v->v.str->str);

	case VALUE_CELLRANGE:
		g_warning ("Getting range as a double: what to do?");
		return 0.0;

	case VALUE_INTEGER:
		return (float_t) v->v.v_int;
		
	case VALUE_ARRAY:
		return 0.0;

	case VALUE_FLOAT:
		return (float_t) v->v.v_float;

	case VALUE_BOOLEAN:
		return v->v.v_bool ? 1. : 0.;

	case VALUE_ERROR:
		return 0.;

	default:
		g_warning ("value_get_as_float type error\n");
		break;
	}
	return 0.0;
}

/*
 * value_cellrange_get_as_string:
 * @value: a value containing a VALUE_CELLRANGE
 * @use_relative_syntax: true if you want the result to contain relative indicators
 *
 * Returns: a string reprensenting the Value, for example:
 * use_relative_syntax == TRUE: $a4:$b$1
 * use_relative_syntax == FALSE: a4:b1
 */
char *
value_cellrange_get_as_string (const Value *value, gboolean use_relative_syntax)
{
	GString *str;
	char *ans;
	CellRef const * a, * b;

	g_return_val_if_fail (value != NULL, NULL);
	g_return_val_if_fail (value->type == VALUE_CELLRANGE, NULL);

	a = &value->v.cell_range.cell_a;
	b = &value->v.cell_range.cell_b;

	str = g_string_new ("");
	encode_cellref (str, a, use_relative_syntax);

	if ((a->col != b->col) || (a->row != b->row) ||
	    (a->col_relative != b->col_relative) || (a->sheet != b->sheet)){
		g_string_append_c (str, ':');
		
		encode_cellref (str, b, use_relative_syntax);
	}
	ans = str->str;
	g_string_free (str, FALSE);
	return ans;
}

/*
 * A utility routine for create a special static instance of Value to be used
 * as a magic pointer to flag a request to terminate an iteration.
 * This object should not be copied, or returned to a user visible routine.
 */
Value *
value_terminate()
{
	static Value * term = NULL;

	if (term == NULL)
		term = value_new_error (NULL, _("Terminate"));

	return term;
}

guint
value_area_get_width (const EvalPosition *ep, Value const *v)
{
	g_return_val_if_fail (v, 0);
	g_return_val_if_fail (v->type == VALUE_ARRAY ||
			      v->type == VALUE_CELLRANGE, 1);

	if (v->type == VALUE_ARRAY)
		return v->v.array.x;
	else { /* FIXME: 3D references, may not clip correctly */
		Sheet *sheeta = v->v.cell_range.cell_a.sheet ?
			v->v.cell_range.cell_a.sheet:ep->sheet;
		guint ans = v->v.cell_range.cell_b.col -
			    v->v.cell_range.cell_a.col + 1;
		if (sheeta && sheeta->max_col_used < ans) /* Clip */
			ans = sheeta->max_col_used+1;
		return ans;
	}
}

guint
value_area_get_height (const EvalPosition *ep, Value const *v)
{
	g_return_val_if_fail (v, 0);
	g_return_val_if_fail (v->type == VALUE_ARRAY ||
			      v->type == VALUE_CELLRANGE, 1);

	if (v->type == VALUE_ARRAY)
		return v->v.array.y;
	else { /* FIXME: 3D references, may not clip correctly */
		Sheet *sheeta = eval_sheet (v->v.cell_range.cell_a.sheet, ep->sheet);
		guint ans = v->v.cell_range.cell_b.row -
		            v->v.cell_range.cell_a.row + 1;
		if (sheeta && sheeta->max_row_used < ans) /* Clip */
			ans = sheeta->max_row_used + 1;
		return ans;
	}
}

Value const *
value_area_fetch_x_y (EvalPosition const *ep, Value const *v, guint x, guint y)
{
	Value const * const res = value_area_get_x_y (ep, v, x, y);
	static Value *value_zero = NULL;
	if (res)
		return res;

	if (value_zero == NULL)
		value_zero = value_new_int (0);
	return value_zero;
}

/*
 * An internal routine to get a cell from an array or range.  If any
 * problems occur a NULL is returned.
 */
const Value *
value_area_get_x_y (EvalPosition const *ep, Value const *v, guint x, guint y)
{
	g_return_val_if_fail (v, NULL);
	g_return_val_if_fail (v->type == VALUE_ARRAY ||
			      v->type == VALUE_CELLRANGE,
			      NULL);

	if (v->type == VALUE_ARRAY){
		g_return_val_if_fail (x < v->v.array.x &&
				      y < v->v.array.y,
				      NULL);
		return v->v.array.vals [x][y];
	} else {
		CellRef const * const a = &v->v.cell_range.cell_a;
		CellRef const * const b = &v->v.cell_range.cell_b;
		int a_col = a->col;
		int a_row = a->row;
		int b_col = b->col;
		int b_row = b->row;
		Cell *cell;
		Sheet *sheet;

		/* Handle relative references */
		if (a->col_relative)
			a_col += ep->eval_col;
		if (a->row_relative)
			a_row += ep->eval_row;
		if (b->col_relative)
			b_col += ep->eval_col;
		if (b->row_relative)
			b_row += ep->eval_row;

		/* Handle inverted refereneces */
		if (a_row > b_row) {
			int tmp = a_row;
			a_row = b_row;
			b_row = tmp;
		}
		if (a_col > b_col) {
			int tmp = a_col;
			a_col = b_col;
			b_col = tmp;
		}

		a_col += x;
		a_row += y;

		/*
		 * FIXME FIXME FIXME
		 * This should return NA but some of the math functions may
		 * rely on this for now.
		 */
		g_return_val_if_fail (a_row<=b_row, NULL);
		g_return_val_if_fail (a_col<=b_col, NULL);

		sheet = a->sheet?a->sheet:ep->sheet;
		g_return_val_if_fail (sheet != NULL, NULL);

		/* Speedup */
		if (sheet->max_col_used < a_col ||
		    sheet->max_row_used < a_row)
			return NULL;

		cell = sheet_cell_get (sheet, a_col, a_row);

		if (cell && cell->value)
			return cell->value;
	}
	
	return NULL;
}

void
value_array_set (Value *array, guint col, guint row, Value *v)
{
	g_return_if_fail (v);
	g_return_if_fail (array->type == VALUE_ARRAY);
	g_return_if_fail (col>=0);
	g_return_if_fail (row>=0);
	g_return_if_fail (array->v.array.y > row);
	g_return_if_fail (array->v.array.x > col);

	if (array->v.array.vals[col][row])
		value_release (array->v.array.vals[col][row]);
	array->v.array.vals[col][row] = v;
}

void
value_array_resize (Value *v, guint width, guint height)
{
	int x, y, xcpy, ycpy;
	Value *newval;
	Value ***tmp;

	g_warning ("Totally untested");
	g_return_if_fail (v);
	g_return_if_fail (v->type == VALUE_ARRAY);

	newval = value_new_array (width, height);

	xcpy = MIN (width, v->v.array.x);
	ycpy = MIN (height, v->v.array.y);

	for (x = 0; x < xcpy; x++)
		for (y = 0; y < ycpy; y++) {
			value_array_set (newval, x, y, v->v.array.vals[x][y]);
			v->v.array.vals[x][y] = NULL;
		}

	tmp = v->v.array.vals;
	v->v.array.vals = newval->v.array.vals;
	newval->v.array.vals = tmp;
	newval->v.array.x = v->v.array.x;
	newval->v.array.y = v->v.array.y;
	v->v.array.x = width;
	v->v.array.y = height;

	value_release (newval);
}

void
value_array_copy_to (Value *v, const Value *src)
{
	int x, y;

	g_return_if_fail (src->type == VALUE_ARRAY);
	v->type = VALUE_ARRAY;
	v->v.array.x = src->v.array.x;
	v->v.array.y = src->v.array.y;
	v->v.array.vals = g_new (Value **, v->v.array.x);

	for (x = 0; x < v->v.array.x; x++) {
		v->v.array.vals [x] = g_new (Value *, v->v.array.y);
		for (y = 0; y < v->v.array.y; y++)
			v->v.array.vals [x][y] = value_duplicate (src->v.array.vals [x][y]);
	}
}

/* Initialize temporarily with statics.  The real versions from the locale
 * will be setup in constants_init
 */
char const *gnumeric_err_NULL  = "#NULL!";
char const *gnumeric_err_DIV0  = "#DIV/0!";
char const *gnumeric_err_VALUE = "#VALUE!";
char const *gnumeric_err_REF   = "#REF!";
char const *gnumeric_err_NAME  = "#NAME?";
char const *gnumeric_err_NUM   = "#NUM!";
char const *gnumeric_err_NA    = "#N/A";

void
constants_init (void)
{
	symbol_install (global_symbol_table, "FALSE", SYMBOL_VALUE,
			value_new_bool (FALSE));
	symbol_install (global_symbol_table, "TRUE", SYMBOL_VALUE,
			value_new_bool (TRUE));
	symbol_install (global_symbol_table, "GNUMERIC_VERSION", SYMBOL_VALUE,
			value_new_float (atof (GNUMERIC_VERSION)));

	gnumeric_err_NULL = _("#NULL!");
	gnumeric_err_DIV0 = _("#DIV/0!");
	gnumeric_err_VALUE = _("#VALUE!");
	gnumeric_err_REF = _("#REF!");
	gnumeric_err_NAME = _("#NAME?");
	gnumeric_err_NUM = _("#NUM!");
	gnumeric_err_NA = _("#N/A");
}
