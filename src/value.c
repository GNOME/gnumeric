/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * value.c:  Utilies for handling, creating, removing values.
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org).
 *   Michael Meeks   (mmeeks@gnu.org)
 *   Jody Goldberg (jgolderg@home.com)
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "value.h"

#include "parse-util.h"
#include "style.h"
#include "format.h"
#include "portability.h"
#include "str.h"
#include "mathfunc.h"

#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <libgnome/gnome-i18n.h>

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
value_new_float (gnum_float f)
{
	if (FINITE (f)) {
		ValueFloat *v = g_new (ValueFloat, 1);
		*((ValueType *)&(v->type)) = VALUE_FLOAT;
		v->val = f;
		return (Value *)v;
	} else {
		/* FIXME: bogus ep sent here.  What to do?  */
		return value_new_error (NULL, gnumeric_err_VALUE);
	}
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

/**
 * value_error_set_pos :
 * @err :
 * @pos :
 *
 * Change the position of a ValueError.
 */
Value *
value_error_set_pos (ValueErr *err, EvalPos const *pos)
{
    g_return_val_if_fail (err != NULL, NULL);
    g_return_val_if_fail (err->type == VALUE_ERROR, NULL);

    err->src = *pos;
    return (Value *)err;
}

Value *
value_new_string (char const *str)
{
	ValueStr *v = g_new (ValueStr, 1);
	*((ValueType *)&(v->type)) = VALUE_STRING;
	v->val = string_get (str);
	return (Value *)v;
}

/* NOTE : absorbs the reference */
Value *
value_new_string_str (String *str)
{
	ValueStr *v = g_new (ValueStr, 1);
	*((ValueType *)&(v->type)) = VALUE_STRING;
	v->val = str;
	return (Value *)v;
}

Value *
value_new_cellrange_unsafe (CellRef const *a, CellRef const *b)
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
value_new_cellrange (CellRef const *a, CellRef const *b,
		     int eval_col, int eval_row)
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
value_new_cellrange_r (Sheet *sheet, Range const *r)
{
	ValueRange *v = g_new (ValueRange, 1);
	CellRef *a, *b;

	*((ValueType *)&(v->type)) = VALUE_CELLRANGE;
	a = &v->cell.a;
	b = &v->cell.b;

	a->sheet = sheet;
	b->sheet = sheet;
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
	guint x, y;
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
	guint x, y;
	ValueArray *v = (ValueArray *)value_new_array_non_init (cols, rows);

	for (x = 0; x < cols; x++) {
		v->vals [x] = g_new (Value *, rows);
		for (y = 0; y < rows; y++)
			v->vals [x] [y] = NULL;
	}
	return (Value *)v;
}

Value *
value_new_from_string (ValueType t, char const *str)
{
	switch (t) {
	case VALUE_EMPTY:
		return value_new_empty ();

	case VALUE_BOOLEAN:
		/* Is it a boolean */
		if (0 == g_strcasecmp (str, _("TRUE")))
			return value_new_bool (TRUE);
		if (0 == g_strcasecmp (str, _("FALSE")))
			return value_new_bool (FALSE);
		return NULL;

	case VALUE_INTEGER:
	{
		char *end;
		long l;

		errno = 0;
		l = strtol (str, &end, 10);
		if (str != end && *end == '\0') {
			if (errno != ERANGE)
				return value_new_int ((int)l);
		}
		return NULL;
	}

	case VALUE_FLOAT:
	{
		char *end;
		double d;

		errno = 0;
		d = strtod (str, &end);
		if (str != end && *end == '\0') {
			if (errno != ERANGE)
				return value_new_float ((gnum_float)d);
		}
		return NULL;
	}

	case VALUE_ERROR:
		return value_new_error (NULL, str);

	case VALUE_STRING:
		return value_new_string (str);

	/* Should not happend */
	case VALUE_ARRAY:
	case VALUE_CELLRANGE:
	default:
		g_warning ("value_new_from_string problem\n");
		return NULL;
	}
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
		break;

	case VALUE_FLOAT:
		break;

	case VALUE_ERROR:
		string_unref (value->v_err.mesg);
		break;

	case VALUE_STRING:
		string_unref (value->v_str.val);
		break;

	case VALUE_ARRAY: {
		ValueArray *v = (ValueArray *)value;
		int x, y;

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
value_duplicate (Value const *src)
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
		string_ref (src->v_str.val);
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
 * use only if you are sure the value is ok
 */
gboolean
value_get_as_checked_bool (Value const *v)
{
	gboolean result, err;

	result = value_get_as_bool (v, &err);

	g_return_val_if_fail (!err, FALSE);

	return result;
}

/**
 * value_get_as_string :
 * @v :
 *
 * simplistic value rendering
 *
 * Returns a string that must be freed.
 */
char *
value_get_as_string (Value const *v)
{
	if (v == NULL)
		return g_strdup ("");

	switch (v->type){
	case VALUE_EMPTY:
		return g_strdup ("");

	case VALUE_ERROR:
		return g_strdup (v->v_err.mesg->str);

	case VALUE_BOOLEAN:
		return g_strdup (v->v_bool.val ? _("TRUE") : _("FALSE"));

	case VALUE_STRING:
		return g_strdup (v->v_str.val->str);

	case VALUE_INTEGER:
		return g_strdup_printf ("%d", v->v_int.val);

	case VALUE_FLOAT:
		return g_strdup_printf ("%.*g", DBL_DIG, v->v_float.val);

	case VALUE_ARRAY: {
		char const row_sep = format_get_arg_sep ();
		char const col_sep = format_get_col_sep ();
		GString *str = g_string_new ("{");
		int x, y;
		char *ans;

		for (y = 0; y < v->v_array.y; y++){
			for (x = 0; x < v->v_array.x; x++){
				Value const *val = v->v_array.vals [x][y];

				g_return_val_if_fail (val->type == VALUE_STRING ||
						      val->type == VALUE_FLOAT ||
						      val->type == VALUE_INTEGER,
						      "Duff Array contents");
				if (x)
					g_string_append_c (str, row_sep);
				if (val->type == VALUE_STRING)
					g_string_sprintfa (str, "\"%s\"",
							   val->v_str.val->str);
				else
					g_string_sprintfa (str, "%g",
							   value_get_as_float (val));
			}
			if (y < v->v_array.y-1)
				g_string_append_c (str, col_sep);
		}
		g_string_sprintfa (str, "}");
		ans = str->str;
		g_string_free (str, FALSE);
		return ans;
	}

	case VALUE_CELLRANGE: {
		char *a = cellref_name (&v->v_range.cell.a, NULL, FALSE);
		char *b = cellref_name (&v->v_range.cell.b, NULL, FALSE);
		char *res = g_strconcat (a, ":", b, NULL);

		g_free (a);
		g_free (b);

		/* This is a fallback that should never be called.
		 * without a parsepos we can not resolve relative references.
		 */
		g_warning ("value_as_string called for a cellrange ?");
		return res;
	}

	default:
		g_warning ("value_get_as_string problem\n");
		break;
	}

	return g_strdup ("Internal problem");
}

/*
 * Result will stay valid until (a) the value is disposed of, or (b) two
 * further calls to this function are made.
 */
char const *
value_peek_string (Value const *v)
{
	g_return_val_if_fail (v, "");

	if (v->type == VALUE_STRING)
		return v->v_str.val->str;
	else {
		static char *cache[2] = { 0 };
		static int next = 0;
		char const *s;

		g_free (cache[next]);
		s = cache[next] = value_get_as_string (v);
		next = (next + 1) % (sizeof (cache) / sizeof (cache[0]));
		return s;
	}
}



/*
 * FIXME FIXME FIXME : Support errors
 */
int
value_get_as_int (Value const *v)
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
gnum_float
value_get_as_float (Value const *v)
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
		return (gnum_float) v->v_int.val;

	case VALUE_ARRAY:
		return 0.0;

	case VALUE_FLOAT:
		return (gnum_float) v->v_float.val;

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
value_array_set (Value *array, int col, int row, Value *v)
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
value_array_resize (Value *v, int width, int height)
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

static ValueCompare
compare_bool_bool (Value const *va, Value const *vb)
{
	gboolean err; /* Ignored */
	gboolean const a = value_get_as_bool (va, &err);
	gboolean const b = value_get_as_bool (vb, &err);
	if (a)
		return b ? IS_EQUAL : IS_GREATER;
	return b ? IS_LESS : IS_EQUAL;
}

static ValueCompare
compare_int_int (Value const *va, Value const *vb)
{
	int const a = value_get_as_int (va);
	int const b = value_get_as_int (vb);
	if (a == b)
		return IS_EQUAL;
	else if (a < b)
		return IS_LESS;
	else
		return IS_GREATER;
}

static ValueCompare
compare_float_float (Value const *va, Value const *vb)
{
	gnum_float const a = value_get_as_float (va);
	gnum_float const b = value_get_as_float (vb);
	if (a == b)
		return IS_EQUAL;
	else if (a < b)
		return IS_LESS;
	else
		return IS_GREATER;
}

/**
 * value_diff :
 *
 * @a : value a
 * @b : value b
 *
 * Returns a positive difference between 2 values
 */
double
value_diff (Value const *a, Value const *b)
{
	ValueType ta, tb;

	/* Handle trivial and double NULL case */
	if (a == b)
		return 0.;

	ta = VALUE_IS_EMPTY (a) ? VALUE_EMPTY : a->type;
	tb = VALUE_IS_EMPTY (b) ? VALUE_EMPTY : b->type;

	/* string > empty */
	if (ta == VALUE_STRING) {
		switch (tb) {
		/* Strings are > (empty, or number) */
		case VALUE_EMPTY :
			if (*a->v_str.val->str == '\0')
				return 0.;
			return DBL_MAX;

		/* If both are strings compare as string */
		case VALUE_STRING :
		{
			gint t = strcoll (a->v_str.val->str, b->v_str.val->str);
			if (t == 0)
				return 0.;
		}
		case VALUE_INTEGER : case VALUE_FLOAT : case VALUE_BOOLEAN :
		default :
			return DBL_MAX;
		}

	} else if (tb == VALUE_STRING) {
		switch (ta) {
		/* (empty, or number) < String */
		case VALUE_EMPTY :
			if (*b->v_str.val->str == '\0')
				return 0.;

		case VALUE_INTEGER : case VALUE_FLOAT : case VALUE_BOOLEAN :
		default :
			return DBL_MAX;
		}
	}

	/* Booleans > all numbers (Why did excel do this ??) */
	if (ta == VALUE_BOOLEAN && (tb == VALUE_INTEGER || tb == VALUE_FLOAT))
		return DBL_MAX;
	if (tb == VALUE_BOOLEAN && (ta == VALUE_INTEGER || ta == VALUE_FLOAT))
		return DBL_MAX;

	switch ((ta > tb) ? ta : tb) {
	case VALUE_EMPTY:	/* Empty Empty compare */
		return 0.;

	case VALUE_BOOLEAN:
		return (compare_bool_bool (a, b) == IS_EQUAL) ? 0. : DBL_MAX;

	case VALUE_INTEGER: {
		int const ia = value_get_as_int (a);
		int const ib = value_get_as_int (b);
		return abs (ia-ib);
	}

	case VALUE_FLOAT: {
		double const da = value_get_as_float (a);
		double const db = value_get_as_float (b);
		return fabs (da-db);
	}
	default:
		return TYPE_MISMATCH;
	}
}

/**
 * value_compare :
 *
 * @a : value a
 * @b : value b
 * @case_sensitive : are string comparisons case sensitive.
 */
ValueCompare
value_compare (Value const *a, Value const *b, gboolean case_sensitive)
{
	ValueType ta, tb;

	/* Handle trivial and double NULL case */
	if (a == b)
		return IS_EQUAL;

	ta = VALUE_IS_EMPTY (a) ? VALUE_EMPTY : a->type;
	tb = VALUE_IS_EMPTY (b) ? VALUE_EMPTY : b->type;

	/* string > empty */
	if (ta == VALUE_STRING) {
		switch (tb) {
		/* Strings are > (empty, or number) */
		case VALUE_EMPTY :
			if (*a->v_str.val->str == '\0')
				return IS_EQUAL;

		case VALUE_INTEGER : case VALUE_FLOAT :
			return IS_GREATER;

		/* Strings are < FALSE ?? */
		case VALUE_BOOLEAN :
			return IS_LESS;

		/* If both are strings compare as string */
		case VALUE_STRING :
		{
			gint t;

			if (case_sensitive) {
				t = strcoll (a->v_str.val->str, b->v_str.val->str);
			} else {
				gchar *str_a, *str_b;

				str_a = g_alloca (strlen (a->v_str.val->str) + 1);
				str_b = g_alloca (strlen (b->v_str.val->str) + 1);
				g_strdown (strcpy (str_a, a->v_str.val->str));
				g_strdown (strcpy (str_b, b->v_str.val->str));
				t = strcoll (str_a, str_b);
			}

			if (t == 0)
				return IS_EQUAL;
			else if (t > 0)
				return IS_GREATER;
			else
				return IS_LESS;
		}
		default :
			return TYPE_MISMATCH;
		}
	} else if (tb == VALUE_STRING) {
		switch (ta) {
		/* (empty, or number) < String */
		case VALUE_EMPTY :
			if (*b->v_str.val->str == '\0')
				return IS_EQUAL;

		case VALUE_INTEGER : case VALUE_FLOAT :
			return IS_LESS;

		/* Strings are < FALSE ?? */
		case VALUE_BOOLEAN :
			return IS_GREATER;

		default :
			return TYPE_MISMATCH;
		}
	}

	/* Booleans > all numbers (Why did excel do this ??) */
	if (ta == VALUE_BOOLEAN && (tb == VALUE_INTEGER || tb == VALUE_FLOAT))
		return IS_GREATER;
	if (tb == VALUE_BOOLEAN && (ta == VALUE_INTEGER || ta == VALUE_FLOAT))
		return IS_LESS;

	switch ((ta > tb) ? ta : tb) {
	case VALUE_EMPTY:	/* Empty Empty compare */
		return IS_EQUAL;

	case VALUE_BOOLEAN:
		return compare_bool_bool (a, b);

	case VALUE_INTEGER:
		return compare_int_int (a, b);

	case VALUE_FLOAT:
		return compare_float_float (a, b);
	default:
		return TYPE_MISMATCH;
	}
}
