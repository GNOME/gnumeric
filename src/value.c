/*
 * value.c:  Utilies for handling, creating, removing values.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org).
 *   Michael Meeks   (mmeeks@gnu.org)
 *   Jody Goldberg (jgolderg@home.com)
 */

#include <config.h>
#include <locale.h>
#include <gnome.h>
#include "gnumeric.h"
#include "value.h"
#include "parse-util.h"
#include "style.h"
#include <stdlib.h>

Value *
value_new_empty (void)
{
	Value *v = g_new (Value, 1);
	*((ValueType *)&(v->type)) = VALUE_EMPTY;

	return v;
}

Value *
value_new_bool (gboolean b)
{
	Value *v = g_new (Value, 1);

	*((ValueType *)&(v->type)) = VALUE_BOOLEAN;
	v->v.v_bool = b;

	return v;
}

Value *
value_new_error (EvalPosition const *ep, char const *mesg)
{
	Value *v = g_new (Value, 1);

	*((ValueType *)&(v->type)) = VALUE_ERROR;
	v->v.error.mesg = string_get (mesg);

	return v;
}

Value *
value_new_error_str (EvalPosition const *ep, String *mesg)
{
	Value *v = g_new (Value, 1);

	*((ValueType *)&(v->type)) = VALUE_ERROR;
	v->v.error.mesg = string_ref (mesg);

	return v;
}

Value *
value_new_string (const char *str)
{
	Value *v = g_new (Value, 1);

	*((ValueType *)&(v->type)) = VALUE_STRING;
	v->v.str = string_get (str);

	return v;
}

Value *
value_new_string_str (String *str)
{
	Value *v = g_new (Value, 1);

	*((ValueType *)&(v->type)) = VALUE_STRING;
	v->v.str = string_ref (str);

	return v;
}

Value *
value_new_int (int i)
{
	Value *v = g_new (Value, 1);

	*((ValueType *)&(v->type)) = VALUE_INTEGER;
	v->v.v_int = i;

	return v;
}

Value *
value_new_float (float_t f)
{
	Value *v = g_new (Value, 1);

	*((ValueType *)&(v->type)) = VALUE_FLOAT;
	v->v.v_float = f;

	return v;
}

Value *
value_new_cellrange_unsafe (const CellRef *a, const CellRef *b)
{
	Value *v = g_new (Value, 1);
	*((ValueType *)&(v->type)) = VALUE_CELLRANGE;
	v->v.cell_range.cell_a = *a;
	v->v.cell_range.cell_b = *b;
	return v;
}

Value *
value_new_cellrange (const CellRef *a, const CellRef *b,
		     int const eval_col, int const eval_row)
{
	Value *v = g_new (Value, 1);
	int tmp;

	*((ValueType *)&(v->type)) = VALUE_CELLRANGE;
	v->v.cell_range.cell_a = *a;
	v->v.cell_range.cell_b = *b;

	/* Sanity checking to avoid inverted ranges */
		tmp = a->col;
	if (a->col_relative != b->col_relative) {
		/* Make a tmp copy of a in the same mode as b */
		if (a->col_relative)
			tmp += eval_col;
		else
			tmp -= eval_col;
	}
	if (tmp > b->col) {
		v->v.cell_range.cell_a.col = b->col;
		v->v.cell_range.cell_a.col_relative = b->col_relative;
		v->v.cell_range.cell_b.col = a->col;
		v->v.cell_range.cell_b.col_relative = a->col_relative;
	}

	tmp = a->row;
	if (a->row_relative != b->row_relative) {
		/* Make a tmp copy of a in the same mode as b */
		if (a->row_relative)
			tmp += eval_row;
		else
			tmp -= eval_row;
	}
	if (tmp > b->row) {
		v->v.cell_range.cell_a.row = b->row;
		v->v.cell_range.cell_a.row_relative = b->row_relative;
		v->v.cell_range.cell_b.row = a->row;
		v->v.cell_range.cell_b.row_relative = a->row_relative;
	}

	return v;
}

Value *
value_new_cellrange_r (Sheet *sheet, const Range *r)
{
	Value *v = g_new (Value, 1);
	CellRef *a, *b;

	*((ValueType *)&(v->type)) = VALUE_CELLRANGE;
	a = &v->v.cell_range.cell_a;
	b = &v->v.cell_range.cell_b;
	
	a->sheet = sheet;
	b->sheet = NULL;
	a->col   = r->start.col;
	a->row   = r->start.row;
	b->col   = r->end.col;
	b->row   = r->end.row;
	a->col_relative = b->col_relative = FALSE;
	a->row_relative = b->row_relative = FALSE;

	return v;
}

Value *
value_new_array_non_init (guint cols, guint rows)
{
	Value *v = g_new (Value, 1);
	*((ValueType *)&(v->type)) = VALUE_ARRAY;
	v->v.array.x = cols;
	v->v.array.y = rows;
	v->v.array.vals = g_new (Value **, cols);
	return v;
}

Value *
value_new_array (guint cols, guint rows)
{
	int x, y;
	Value * v = value_new_array_non_init (cols, rows);

	for (x = 0; x < cols; x++) {
		v->v.array.vals [x] = g_new (Value *, rows);
		for (y = 0; y < rows; y++)
			v->v.array.vals [x] [y] = value_new_int (0);
	}
	return v;
}

Value *
value_new_array_empty (guint cols, guint rows)
{
	int x, y;
	Value * v = value_new_array_non_init (cols, rows);

	for (x = 0; x < cols; x++) {
		v->v.array.vals [x] = g_new (Value *, rows);
		for (y = 0; y < rows; y++)
			v->v.array.vals [x] [y] = NULL;
	}
	return v;
}

void
value_release (Value *value)
{
	g_return_if_fail (value != NULL);

	/* Do not release value_terminate it is a magic number */
	if (value == value_terminate ())
		return;

	switch (value->type) {
	case VALUE_EMPTY:
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

	case VALUE_ARRAY: {
		guint x, y;

		for (x = 0; x < value->v.array.x; x++) {
			for (y = 0; y < value->v.array.y; y++) {
				if (value->v.array.vals [x] [y])
					value_release (value->v.array.vals [x] [y]);
			}
			g_free (value->v.array.vals [x]);
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
	*((ValueType *)&(value->type)) = 9999;
	g_free (value);
}

/*
 * Makes a copy of a Value
 */
Value *
value_duplicate (const Value *src)
{
	Value *new_value;

	g_return_val_if_fail (src != NULL, NULL);

	new_value = g_new (Value, 1);
	switch (src->type){
	case VALUE_EMPTY:
		return value_new_empty();

	case VALUE_BOOLEAN:
		return value_new_bool(src->v.v_bool);

	case VALUE_INTEGER:
		return value_new_int (src->v.v_int);

	case VALUE_FLOAT:
		return value_new_float (src->v.v_float);

	case VALUE_ERROR:
		return value_new_error_str (&src->v.error.src,
					    src->v.error.mesg);

	case VALUE_STRING:
		return value_new_string_str (src->v.str);

	case VALUE_CELLRANGE:
		return value_new_cellrange_unsafe (&src->v.cell_range.cell_a,
						   &src->v.cell_range.cell_b);

	case VALUE_ARRAY:
	{
		int x, y;
		Value *v = value_new_array_non_init (src->v.array.x, src->v.array.y);

		for (x = 0; x < v->v.array.x; x++) {
			v->v.array.vals [x] = g_new (Value *, v->v.array.y);
			for (y = 0; y < v->v.array.y; y++)
				v->v.array.vals [x] [y] = value_duplicate (src->v.array.vals [x][y]);
		}
		return v;
	}

	default:
		break;
	}
	g_warning ("value_duplicate problem\n");
	return value_new_empty();
}

gboolean
value_get_as_bool (Value const *v, gboolean *err)
{
	*err = FALSE;

	if (v == NULL)
		return FALSE;

	switch (v->type) {
	case VALUE_EMPTY:
		return FALSE;

	case VALUE_BOOLEAN:
		return v->v.v_bool;

	case VALUE_STRING:
		return v->v.str->str[0] != '\0';

	case VALUE_INTEGER:
		return v->v.v_int != 0;

	case VALUE_FLOAT:
		return v->v.v_float != 0.0;

	default:
		g_warning ("Unhandled value in value_get_boolean");

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
	static char *separator;

	if (!separator){
		struct lconv *locinfo;
		
		locinfo = localeconv ();
		if (locinfo->decimal_point &&
		    locinfo->decimal_point [0] == ',' &&
		    locinfo->decimal_point [1] == 0)
			separator = ";";
		else
			separator = ",";
	}

	if (value == NULL)
		return g_strdup ("");

	switch (value->type){
	case VALUE_EMPTY:
		return g_strdup ("");

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
		guint x, y;
		char *ans;

		for (y = 0; y < value->v.array.y; y++){
			for (x = 0; x < value->v.array.x; x++){
				const Value *v = value->v.array.vals [x][y];

				g_return_val_if_fail (v->type == VALUE_STRING ||
						      v->type == VALUE_FLOAT ||
						      v->type == VALUE_INTEGER,
						      "Duff Array contents");
				if (x)
					g_string_sprintfa (str, separator);
				if (v->type == VALUE_STRING)
					g_string_sprintfa (str, "\"%s\"",
							   v->v.str->str);
				else
					g_string_sprintfa (str, "%g",
							   value_get_as_float (v));
			}
			if (y < value->v.array.y-1)
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

/*
 * FIXME FIXME FIXME : Support errors
 */
int
value_get_as_int (const Value *v)
{
	if (v == NULL)
		return 0;
	switch (v->type) {
	case VALUE_EMPTY:
		return 0;

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
	return 0;
}

/*
 * FIXME FIXME FIXME : Support errors
 */
float_t
value_get_as_float (const Value *v)
{
	if (v == NULL)
		return 0.;

	switch (v->type) {
	case VALUE_EMPTY:
		return 0.;

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
 * Test @v to see if it fits the heurists used to identify the results of
 * accessing an empty cell.  Which are currently
 * 1) v == NULL
 * 2) v->type == VALUE_EMPTY (new)
 * 3) v == string("")
 */
gboolean
value_is_empty_cell (Value const *v)
{
	return v == NULL ||
	      (v->type == VALUE_EMPTY) ||
	      (v->type == VALUE_STRING && v->v.str->str[0] == '\0');
}

/*
 * A utility routine for create a special static instance of Value to be used
 * as a magic pointer to flag a request to terminate an iteration.
 * This object should not be copied, or returned to a user visible routine.
 */
Value *
value_terminate (void)
{
	static Value * term = NULL;

	if (term == NULL)
		term = value_new_error (NULL, _("Terminate"));

	return term;
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

	if (array->v.array.vals[col][row] != NULL)
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


gboolean
value_equal (const Value *a, const Value *b)
{
	if (a == b)
		return TRUE;

	if (!a || !b)
		return FALSE;

	if (a->type != b->type)
		return FALSE;

	switch (a->type) {
	case VALUE_EMPTY:
		return TRUE;

	case VALUE_BOOLEAN:
		return a->v.v_bool == b->v.v_bool;

	case VALUE_INTEGER:
		return a->v.v_int == b->v.v_int;

	case VALUE_FLOAT:
		return a->v.v_float == b->v.v_float;

	case VALUE_ERROR:
		return a->v.error.mesg == b->v.error.mesg;

	case VALUE_STRING:
		return a->v.str == b->v.str;

	case VALUE_CELLRANGE:
		return (!(memcmp (&a->v.cell_range.cell_a,
				  &b->v.cell_range.cell_a,
				  sizeof (CellRef))) &&
			!(memcmp (&a->v.cell_range.cell_b,
				  &b->v.cell_range.cell_b,
				  sizeof (CellRef))));

	case VALUE_ARRAY:
	{
		guint x, y;

		if (a->v.array.x != b->v.array.x ||
		    a->v.array.y != b->v.array.y)
			return FALSE;

		for (y = 0; y < a->v.array.y; y++) {
			for (x = 0; x < a->v.array.x; x++) {
				if (!value_equal (a->v.array.vals [x] [y],
						  b->v.array.vals [x] [y]))
					return FALSE;
			}
		}
		return TRUE;
		
	}
	}
	return FALSE;
}

StyleHAlignFlags
value_get_default_halign (Value const *v, MStyle const *mstyle)
{
	StyleHAlignFlags align = mstyle_get_align_h (mstyle);
	g_return_val_if_fail (v != NULL, HALIGN_RIGHT);

	if (align == HALIGN_GENERAL) {
		if (v->type == VALUE_FLOAT || v->type == VALUE_INTEGER)
			return HALIGN_RIGHT;
		return HALIGN_LEFT;
	}

	return align;
}
