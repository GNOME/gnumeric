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
	/* This is a constant.  no need to allocate  any memory */
	static ValueType v = VALUE_EMPTY;
	return (Value *)&v;
}

Value *
value_new_bool (gboolean b)
{
	ValueBool *v = g_new (ValueBool, 1);
	*((ValueType *)&(v->type)) = VALUE_BOOLEAN;
	v->val = b;
	return (Value *)v;
}

Value *
value_new_int (int i)
{
	ValueInt *v = g_new (ValueInt, 1);
	*((ValueType *)&(v->type)) = VALUE_INTEGER;
	v->val = i;
	return (Value *)v;
}

Value *
value_new_float (float_t f)
{
	ValueFloat *v = g_new (ValueFloat, 1);
	*((ValueType *)&(v->type)) = VALUE_FLOAT;
	v->val = f;
	return (Value *)v;
}

Value *
value_new_error (EvalPos const *ep, char const *mesg)
{
	ValueErr *v = g_new (ValueErr, 1);
	*((ValueType *)&(v->type)) = VALUE_ERROR;
	v->mesg = string_get (mesg);
	return (Value *)v;
}

Value *
value_new_error_str (EvalPos const *ep, String *mesg)
{
	ValueErr *v = g_new (ValueErr, 1);
	*((ValueType *)&(v->type)) = VALUE_ERROR;
	v->mesg = string_ref (mesg);
	return (Value *)v;
}

Value *
value_new_string (const char *str)
{
	ValueStr *v = g_new (ValueStr, 1);
	*((ValueType *)&(v->type)) = VALUE_STRING;
	v->val = string_get (str);
	return (Value *)v;
}

Value *
value_new_string_str (String *str)
{
	ValueStr *v = g_new (ValueStr, 1);
	*((ValueType *)&(v->type)) = VALUE_STRING;
	v->val = string_ref (str);
	return (Value *)v;
}

Value *
value_new_cellrange_unsafe (const CellRef *a, const CellRef *b)
{
	ValueRange *v = g_new (ValueRange, 1);
	*((ValueType *)&(v->type)) = VALUE_CELLRANGE;
	v->cell.a = *a;
	v->cell.b = *b;
	return (Value *)v;
}

/**
 * value_new_cellrange : Create a new range reference.
 *
 * Attempt to do a sanity check for inverted ranges.
 * NOTE : This is no longer necessary and will be removed.
 * mixed mode references create the possibility of inversion.
 * users of these values need to use the utility routines to
 * evaluate the ranges in their context and normalize then.
 */
Value *
value_new_cellrange (const CellRef *a, const CellRef *b,
		     int const eval_col, int const eval_row)
{
	ValueRange *v = g_new (ValueRange, 1);
	int tmp;

	*((ValueType *)&(v->type)) = VALUE_CELLRANGE;
	v->cell.a = *a;
	v->cell.b = *b;

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
		v->cell.a.col = b->col;
		v->cell.a.col_relative = b->col_relative;
		v->cell.b.col = a->col;
		v->cell.b.col_relative = a->col_relative;
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
		v->cell.a.row = b->row;
		v->cell.a.row_relative = b->row_relative;
		v->cell.b.row = a->row;
		v->cell.b.row_relative = a->row_relative;
	}

	return (Value *)v;
}

Value *
value_new_cellrange_r (Sheet *sheet, const Range *r)
{
	ValueRange *v = g_new (ValueRange, 1);
	CellRef *a, *b;

	*((ValueType *)&(v->type)) = VALUE_CELLRANGE;
	a = &v->cell.a;
	b = &v->cell.b;
	
	a->sheet = sheet;
	b->sheet = NULL;
	a->col   = r->start.col;
	a->row   = r->start.row;
	b->col   = r->end.col;
	b->row   = r->end.row;
	a->col_relative = b->col_relative = FALSE;
	a->row_relative = b->row_relative = FALSE;

	return (Value *)v;
}

Value *
value_new_array_non_init (guint cols, guint rows)
{
	ValueArray *v = g_new (ValueArray, 1);
	*((ValueType *)&(v->type)) = VALUE_ARRAY;
	v->x = cols;
	v->y = rows;
	v->vals = g_new (Value **, cols);
	return (Value *)v;
}

Value *
value_new_array (guint cols, guint rows)
{
	int x, y;
	ValueArray *v = (ValueArray *)value_new_array_non_init (cols, rows);

	for (x = 0; x < cols; x++) {
		v->vals [x] = g_new (Value *, rows);
		for (y = 0; y < rows; y++)
			v->vals [x] [y] = value_new_int (0);
	}
	return (Value *)v;
}

Value *
value_new_array_empty (guint cols, guint rows)
{
	int x, y;
	ValueArray *v = (ValueArray *)value_new_array_non_init (cols, rows);

	for (x = 0; x < cols; x++) {
		v->vals [x] = g_new (Value *, rows);
		for (y = 0; y < rows; y++)
			v->vals [x] [y] = NULL;
	}
	return (Value *)v;
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
		/* We did not allocate anything, there is nothing to free */
		return;

	case VALUE_BOOLEAN:
		break;

	case VALUE_INTEGER:
		mpz_clear (value->v_int.val);
		break;

	case VALUE_FLOAT:
		mpf_clear (value->v_float.val);
		break;

	case VALUE_ERROR:
		string_unref (value->v_err.mesg);
		break;

	case VALUE_STRING:
		string_unref (value->v_str.val);
		break;

	case VALUE_ARRAY: {
		ValueArray *v = (ValueArray *)value;
		guint x, y;

		for (x = 0; x < v->x; x++) {
			for (y = 0; y < v->y; y++) {
				if (v->vals [x] [y])
					value_release (v->vals [x] [y]);
			}
			g_free (v->vals [x]);
		}

		g_free (v->vals);
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
	g_return_val_if_fail (src != NULL, NULL);

	switch (src->type){
	case VALUE_EMPTY:
		return value_new_empty();

	case VALUE_BOOLEAN:
		return value_new_bool(src->v_bool.val);

	case VALUE_INTEGER:
		return value_new_int (src->v_int.val);

	case VALUE_FLOAT:
		return value_new_float (src->v_float.val);

	case VALUE_ERROR:
		return value_new_error_str (&src->v_err.src,
					    src->v_err.mesg);

	case VALUE_STRING:
		return value_new_string_str (src->v_str.val);

	case VALUE_CELLRANGE:
		return value_new_cellrange_unsafe (&src->v_range.cell.a,
						   &src->v_range.cell.b);

	case VALUE_ARRAY:
	{
		int x, y;
		ValueArray *res =
		    (ValueArray *)value_new_array_non_init (src->v_array.x, src->v_array.y);

		for (x = 0; x < res->x; x++) {
			res->vals [x] = g_new (Value *, res->y);
			for (y = 0; y < res->y; y++)
				res->vals [x] [y] = value_duplicate (src->v_array.vals [x][y]);
		}
		return (Value *)res;
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
		return v->v_bool.val;

	case VALUE_STRING:
		return v->v_str.val->str[0] != '\0';

	case VALUE_INTEGER:
		return v->v_int.val != 0;

	case VALUE_FLOAT:
		return v->v_float.val != 0.0;

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
		return g_strdup (value->v_err.mesg->str);

	case VALUE_BOOLEAN: 
		return g_strdup (value->v_bool.val ? _("TRUE") : _("FALSE"));

	case VALUE_STRING:
		return g_strdup (value->v_str.val->str);

	case VALUE_INTEGER:
		return g_strdup_printf ("%d", value->v_int.val);

	case VALUE_FLOAT:
		return g_strdup_printf ("%.*g", DBL_DIG, value->v_float.val);

	case VALUE_ARRAY: {
		GString *str = g_string_new ("{");
		guint x, y;
		char *ans;

		for (y = 0; y < value->v_array.y; y++){
			for (x = 0; x < value->v_array.x; x++){
				const Value *v = value->v_array.vals [x][y];

				g_return_val_if_fail (v->type == VALUE_STRING ||
						      v->type == VALUE_FLOAT ||
						      v->type == VALUE_INTEGER,
						      "Duff Array contents");
				if (x)
					g_string_sprintfa (str, separator);
				if (v->type == VALUE_STRING)
					g_string_sprintfa (str, "\"%s\"",
							   v->v_str.val->str);
				else
					g_string_sprintfa (str, "%g",
							   value_get_as_float (v));
			}
			if (y < value->v_array.y-1)
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
		return atoi (v->v_str.val->str);

	case VALUE_CELLRANGE:
		g_warning ("Getting range as a int: what to do?");
		return 0;

	case VALUE_INTEGER:
		return v->v_int.val;

	case VALUE_ARRAY:
		return 0;

	case VALUE_FLOAT:
		return (int) v->v_float.val;

	case VALUE_BOOLEAN:
		return v->v_bool.val ? 1 : 0;

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
		return atof (v->v_str.val->str);

	case VALUE_CELLRANGE:
		g_warning ("Getting range as a double: what to do?");
		return 0.0;

	case VALUE_INTEGER:
		return (float_t) v->v_int.val;
		
	case VALUE_ARRAY:
		return 0.0;

	case VALUE_FLOAT:
		return (float_t) v->v_float.val;

	case VALUE_BOOLEAN:
		return v->v_bool.val ? 1. : 0.;

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
 */
gboolean
value_is_empty_cell (Value const *v)
{
	return v == NULL || (v->type == VALUE_EMPTY);
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
	g_return_if_fail (array->v_array.y > row);
	g_return_if_fail (array->v_array.x > col);

	if (array->v_array.vals[col][row] != NULL)
		value_release (array->v_array.vals[col][row]);
	array->v_array.vals[col][row] = v;
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

	xcpy = MIN (width, v->v_array.x);
	ycpy = MIN (height, v->v_array.y);

	for (x = 0; x < xcpy; x++)
		for (y = 0; y < ycpy; y++) {
			value_array_set (newval, x, y, v->v_array.vals[x][y]);
			v->v_array.vals[x][y] = NULL;
		}

	tmp = v->v_array.vals;
	v->v_array.vals = newval->v_array.vals;
	newval->v_array.vals = tmp;
	newval->v_array.x = v->v_array.x;
	newval->v_array.y = v->v_array.y;
	v->v_array.x = width;
	v->v_array.y = height;

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
		return a->v_bool.val == b->v_bool.val;

	case VALUE_INTEGER:
		return a->v_int.val == b->v_int.val;

	case VALUE_FLOAT:
		return a->v_float.val == b->v_float.val;

	case VALUE_ERROR:
		return a->v_err.mesg == b->v_err.mesg;

	case VALUE_STRING:
		return a->v_str.val == b->v_str.val;

	case VALUE_CELLRANGE:
		/* FIXME : Should A1 == Sheet1!A1 ? */
		return (!(memcmp (&a->v_range.cell.a,
				  &b->v_range.cell.a,
				  sizeof (CellRef))) &&
			!(memcmp (&a->v_range.cell.b,
				  &b->v_range.cell.b,
				  sizeof (CellRef))));

	case VALUE_ARRAY:
	{
		ValueArray *va = (ValueArray *)a;
		ValueArray *vb = (ValueArray *)b;
		guint x, y;

		if (va->x != vb->x ||
		    va->y != vb->y)
			return FALSE;

		for (y = 0; y < va->y; y++) {
			for (x = 0; x < va->x; x++) {
				if (!value_equal (va->vals [x] [y],
						  vb->vals [x] [y]))
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
