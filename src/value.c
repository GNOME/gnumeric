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
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "value.h"

#include "parse-util.h"
#include "style.h"
#include "format.h"
#include "str.h"
#include "position.h"
#include "mathfunc.h"
#include "gutils.h"

#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <ranges.h>
#include <sheet.h>
#include <cell.h>
#include <number-match.h>

#ifndef USE_VALUE_POOLS
#define USE_VALUE_POOLS 1
#endif

#if USE_VALUE_POOLS
#define CHUNK_ALLOC(T,p) ((T*)gnm_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) gnm_mem_chunk_free ((p), (v))
#else
#define CHUNK_ALLOC(T,c) g_new (T,1)
#define CHUNK_FREE(p,v) g_free ((v))
#endif


static struct {
	const char *C_name;
	const char *locale_name;
	String *locale_name_str;
} standard_errors[8] = {
	{ N_("#NULL!"), NULL, NULL },
	{ N_("#DIV/0!"), NULL, NULL },
	{ N_("#VALUE!"), NULL, NULL },
	{ N_("#REF!"), NULL, NULL },
	{ N_("#NAME?"), NULL, NULL },
	{ N_("#NUM!"), NULL, NULL },
	{ N_("#N/A"), NULL, NULL },
	{ N_("#RECALC!"), NULL, NULL }
};


Value *
value_new_empty (void)
{
	/* This is a constant.  No need to allocate any memory.  */
	static ValueAny v = { VALUE_EMPTY, NULL };
	return (Value *)&v;
}

/* Memory pool for ints and bools.  */
static gnm_mem_chunk *value_int_pool;
Value *
value_new_bool (gboolean b)
{
	ValueBool *v = CHUNK_ALLOC (ValueBool, value_int_pool);
	*((ValueType *)&(v->type)) = VALUE_BOOLEAN;
	v->fmt = NULL;
	v->val = b;
	return (Value *)v;
}

Value *
value_new_int (int i)
{
	ValueInt *v = CHUNK_ALLOC (ValueInt, value_int_pool);
	*((ValueType *)&(v->type)) = VALUE_INTEGER;
	v->fmt = NULL;
	v->val = i;
	return (Value *)v;
}

static gnm_mem_chunk *value_float_pool;
Value *
value_new_float (gnm_float f)
{
	if (finitegnum (f)) {
		ValueFloat *v = CHUNK_ALLOC (ValueFloat, value_float_pool);
		*((ValueType *)&(v->type)) = VALUE_FLOAT;
		v->fmt = NULL;
		v->val = f;
		return (Value *)v;
	} else {
		/* FIXME: bogus ep sent here.  What to do?  */
		return value_new_error_NUM (NULL);
	}
}

/* Memory pool for error values.  */
static gnm_mem_chunk *value_error_pool;
Value *
value_new_error (EvalPos const *ep, char const *mesg)
{
	ValueErr *v = CHUNK_ALLOC (ValueErr, value_error_pool);
	*((ValueType *)&(v->type)) = VALUE_ERROR;
	v->fmt = NULL;
	v->mesg = string_get (mesg);
	return (Value *)v;
}

Value *
value_new_error_str (EvalPos const *ep, String *mesg)
{
	ValueErr *v = CHUNK_ALLOC (ValueErr, value_error_pool);
	*((ValueType *)&(v->type)) = VALUE_ERROR;
	v->fmt = NULL;
	v->mesg = string_ref (mesg);
	return (Value *)v;
}

Value *
value_new_error_std (EvalPos const *pos, GnmStdError err)
{
	size_t i = (size_t)err;
	g_return_val_if_fail (i < G_N_ELEMENTS (standard_errors), NULL);

	return value_new_error_str (pos, standard_errors[i].locale_name_str);
}


Value *
value_new_error_NULL (EvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_NULL].locale_name_str);
}

Value *
value_new_error_DIV0 (EvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_DIV0].locale_name_str);
}

Value *
value_new_error_VALUE (EvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_VALUE].locale_name_str);
}

Value *
value_new_error_REF (EvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_REF].locale_name_str);
}

Value *
value_new_error_NAME (EvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_NAME].locale_name_str);
}

Value *
value_new_error_NUM (EvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_NUM].locale_name_str);
}

Value *
value_new_error_NA (EvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_NA].locale_name_str);
}

Value *
value_new_error_RECALC (EvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_RECALC].locale_name_str);
}

const char *
value_error_name (GnmStdError err, gboolean translated)
{
	size_t i = (size_t)err;
	g_return_val_if_fail (i < G_N_ELEMENTS (standard_errors), NULL);

	if (translated)
		return standard_errors[i].locale_name;
	else
		return standard_errors[i].C_name;
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

GnmStdError
value_error_classify (const Value *v)
{
	size_t i;

	g_return_val_if_fail (v != NULL, GNM_ERROR_UNKNOWN);

	if (v->type != VALUE_ERROR)
		return GNM_ERROR_UNKNOWN;

	for (i = 0; i < G_N_ELEMENTS (standard_errors); i++)
		if (standard_errors[i].locale_name_str == v->v_err.mesg)
			return (GnmStdError)i;

	return GNM_ERROR_UNKNOWN;
}


static gnm_mem_chunk *value_string_pool;

/* NOTE : absorbs the reference */
Value *
value_new_string_str (String *str)
{
	ValueStr *v = CHUNK_ALLOC (ValueStr, value_string_pool);
	*((ValueType *)&(v->type)) = VALUE_STRING;
	v->fmt = NULL;
	v->val = str;
	return (Value *)v;
}

Value *
value_new_string (char const *str)
{
	return value_new_string_str (string_get (str));
}

Value *
value_new_string_nocopy (char *str)
{
	return value_new_string_str (string_get_nocopy (str));
}

static gnm_mem_chunk *value_range_pool;
Value *
value_new_cellrange_unsafe (CellRef const *a, CellRef const *b)
{
	ValueRange *v = CHUNK_ALLOC (ValueRange, value_range_pool);
	*((ValueType *)&(v->type)) = VALUE_CELLRANGE;
	v->fmt = NULL;
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
	ValueRange *v = CHUNK_ALLOC (ValueRange, value_range_pool);
	int tmp;

	*((ValueType *)&(v->type)) = VALUE_CELLRANGE;
	v->fmt = NULL;
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
	ValueRange *v = CHUNK_ALLOC (ValueRange, value_range_pool);
	CellRef *a, *b;

	*((ValueType *)&(v->type)) = VALUE_CELLRANGE;
	v->fmt = NULL;
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

static gnm_mem_chunk *value_array_pool;
Value *
value_new_array_non_init (guint cols, guint rows)
{
	ValueArray *v = CHUNK_ALLOC (ValueArray, value_array_pool);
	*((ValueType *)&(v->type)) = VALUE_ARRAY;
	v->fmt = NULL;
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
		v->vals[x] = g_new (Value *, rows);
		for (y = 0; y < rows; y++)
			v->vals[x][y] = value_new_int (0);
	}
	return (Value *)v;
}

Value *
value_new_array_empty (guint cols, guint rows)
{
	guint x, y;
	ValueArray *v = (ValueArray *)value_new_array_non_init (cols, rows);

	for (x = 0; x < cols; x++) {
		v->vals[x] = g_new (Value *, rows);
		for (y = 0; y < rows; y++)
			v->vals[x][y] = NULL;
	}
	return (Value *)v;
}

Value *
value_new_from_string (ValueType t, char const *str, StyleFormat *sf,
		       gboolean translated)
{
	Value *res = NULL;
	switch (t) {
	case VALUE_EMPTY:
		res = value_new_empty ();
		break;

	case VALUE_BOOLEAN: {
		const char *C_true = N_("TRUE");
		const char *C_false = N_("FALSE");

		if (0 == g_ascii_strcasecmp (str, translated ? _(C_true) : C_true))
			res = value_new_bool (TRUE);
		else if (0 == g_ascii_strcasecmp (str, translated ? _(C_false) : C_false))
			res = value_new_bool (FALSE);
		break;
	}

	case VALUE_INTEGER: {
		char *end;
		long l;

		errno = 0;
		l = strtol (str, &end, 10);
		if (str != end && *end == '\0' && errno != ERANGE)
			res = value_new_int ((int)l);
		break;
	}

	case VALUE_FLOAT: {
		char *end;
		gnm_float d;

		errno = 0;
		d = strtognum (str, &end);
		if (str != end && *end == '\0' && errno != ERANGE)
			res = value_new_float (d);
		break;
	}

	case VALUE_ERROR:
		/*
		 * Tricky.  We are currently storing errors in translated
		 * format, so we might have to undo that.
		 */
		if (!translated) {
			size_t i;
			for (i = 0; i < G_N_ELEMENTS (standard_errors); i++)
				if (strcmp (standard_errors[i].C_name, str) == 0) {
					res = value_new_error_std (NULL, (GnmStdError)i);
					break;
				}					
		}
		if (!res)
			res = value_new_error (NULL, str);
		break;

	case VALUE_STRING:
		res = value_new_string (str);
		break;

	/* Should not happen.  */
	case VALUE_ARRAY:
	case VALUE_CELLRANGE:
	default:
		g_warning ("value_new_from_string problem.");
		return NULL;
	}

	if (res)
		value_set_fmt (res, sf);
	return res;
}

void
value_release (Value *value)
{
	g_return_if_fail (value != NULL);

	if (VALUE_FMT (value) != NULL)
		style_format_unref (VALUE_FMT (value));

	switch (value->type) {
	case VALUE_EMPTY:
		/* We did not allocate anything, there is nothing to free */
		return;

	case VALUE_BOOLEAN:
	case VALUE_INTEGER:
		CHUNK_FREE (value_int_pool, value);
		return;

	case VALUE_FLOAT:
		CHUNK_FREE (value_float_pool, value);
		return;

	case VALUE_ERROR:
		/* Do not release VALUE_TERMINATE, it is a magic number */
		if (value == VALUE_TERMINATE) {
			g_warning ("Someone freed VALUE_TERMINATE -- shame on them.");
			return;
		}

		string_unref (value->v_err.mesg);
		CHUNK_FREE (value_error_pool, value);
		return;

	case VALUE_STRING:
		string_unref (value->v_str.val);
		CHUNK_FREE (value_string_pool, value);
		return;

	case VALUE_ARRAY: {
		ValueArray *v = (ValueArray *)value;
		int x, y;

		for (x = 0; x < v->x; x++) {
			for (y = 0; y < v->y; y++) {
				if (v->vals[x][y])
					value_release (v->vals[x][y]);
			}
			g_free (v->vals[x]);
		}

		g_free (v->vals);
		CHUNK_FREE (value_array_pool, value);
		return;
	}

	case VALUE_CELLRANGE:
		CHUNK_FREE (value_range_pool, value);
		return;

	default:
		/*
		 * If we don't recognize the type this is probably garbage.
		 * Do not free it to avoid heap corruption
		 */
		g_warning ("value_release problem.");
		return;
	}
	g_assert_not_reached ();
}

/*
 * Makes a copy of a Value
 */
Value *
value_duplicate (Value const *src)
{
	Value *res;

	g_return_val_if_fail (src != NULL, NULL);

	switch (src->type){
	case VALUE_EMPTY:
		res = value_new_empty ();
		break;

	case VALUE_BOOLEAN:
		res = value_new_bool (src->v_bool.val);
		break;

	case VALUE_INTEGER:
		res = value_new_int (src->v_int.val);
		break;

	case VALUE_FLOAT:
		res = value_new_float (src->v_float.val);
		break;

	case VALUE_ERROR:
		res = value_new_error_str (&src->v_err.src,
					   src->v_err.mesg);
		break;

	case VALUE_STRING:
		string_ref (src->v_str.val);
		res = value_new_string_str (src->v_str.val);
		break;

	case VALUE_CELLRANGE:
		res = value_new_cellrange_unsafe (&src->v_range.cell.a,
						  &src->v_range.cell.b);
		break;

	case VALUE_ARRAY: {
		int x, y;
		ValueArray *array = (ValueArray *)value_new_array_non_init (
			src->v_array.x, src->v_array.y);

		for (x = 0; x < array->x; x++) {
			array->vals[x] = g_new (Value *, array->y);
			for (y = 0; y < array->y; y++)
				array->vals[x][y] = value_duplicate (src->v_array.vals[x][y]);
		}
		res = (Value *)array;
		break;
	}

	default:
		g_warning ("value_duplicate problem.");
		res = value_new_empty ();
	}
	value_set_fmt (res, VALUE_FMT (src));
	return res;
}

/**
 * value_cmp :
 * @ptr_a :
 * @ptr_b :
 *
 * qsort style comparison function.
 **/
int
value_cmp (void const *ptr_a, void const *ptr_b)
{
	Value const *a = *(Value const **)ptr_a;
	Value const *b = *(Value const **)ptr_b;
	switch (value_compare (a, b, TRUE)) {
	case IS_EQUAL :   return  0;
	case IS_LESS :    return -1;
	case IS_GREATER : return  1;
	default :
		break;
	};
	return a->type - b->type;
}

int
value_cmp_reverse (void const *ptr_a, void const *ptr_b)
{
	Value const *a = *(Value const **)ptr_a;
	Value const *b = *(Value const **)ptr_b;
	switch (value_compare (a, b, TRUE)) {
	case IS_EQUAL :   return  0;
	case IS_LESS :	  return  1;
	case IS_GREATER : return -1;
	default :
		break;
	};
	return b->type - a->type;
}

gint
value_equal (Value const *a, Value const *b)
{
	return IS_EQUAL == value_compare (a, b, TRUE);
}

guint
value_hash (Value const *v)
{
	switch (v->type) {
	case VALUE_BOOLEAN:
		return v->v_bool.val ? 0x555aaaa : 0xaaa5555;

	case VALUE_STRING:
		return g_str_hash (v->v_str.val->str);

	case VALUE_ERROR:
		return g_str_hash (v->v_err.mesg->str);

	case VALUE_INTEGER:
		return (guint)(v->v_int.val);

	case VALUE_FLOAT: {
		int expt;
		gnm_float mant = frexpgnum (gnumabs (v->v_float.val), &expt);
		guint h = ((guint)(0x80000000u * mant)) ^ expt;
		if (v->v_float.val >= 0)
			h ^= 0x55555555;
		return h;
	}

	case VALUE_EMPTY:
		return 42;

	case VALUE_CELLRANGE:
		/* FIXME: take sheet into account?  */
		return (cellref_hash (&v->v_range.cell.a) * 3) ^
			cellref_hash (&v->v_range.cell.b);

	case VALUE_ARRAY: {
		int i;
		guint h = (v->v_array.x * 257) ^ (v->v_array.y + 42);

		/* For speed, just walk the diagonal.  */
		for (i = 0; i < v->v_array.x && i < v->v_array.y; i++) {
			h *= 5;
			if (v->v_array.vals[i][i])
				h ^= value_hash (v->v_array.vals[i][i]);
		}
		return h;
	}

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
		return 0;
#endif
	}
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
		g_warning ("Unhandled value in value_get_boolean.");

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

void
value_get_as_gstring (GString *target, Value const *v,
		      const GnmExprConventions *conv)
{
	if (v == NULL)
		return;

	switch (v->type){
	case VALUE_EMPTY:
		return;

	case VALUE_ERROR: {
		GnmStdError e = value_error_classify (v);
		if (e == GNM_ERROR_UNKNOWN) {
			g_string_append_c (target, '#');
			gnm_strescape (target, v->v_err.mesg->str);
		} else
			g_string_append (target, value_error_name (e, conv->output_translated));
		return;
	}

	case VALUE_BOOLEAN: {
		const char *cval = v->v_bool.val ? N_("TRUE") : N_("FALSE");
		g_string_append (target, conv->output_translated ? _(cval) : cval);
		return;
	}

	case VALUE_STRING:
		g_string_append (target, v->v_str.val->str);
		return;

	case VALUE_INTEGER:
		g_string_append_printf (target, "%d", v->v_int.val);
		return;

	case VALUE_FLOAT:
		g_string_append_printf (target, "%.*" GNUM_FORMAT_g, GNUM_DIG,
					v->v_float.val);
		return;

	case VALUE_ARRAY: {
		const char *row_sep, *col_sep;
		char locale_arg_sep[2], locale_col_sep[2];
		int x, y;

		if (conv->output_argument_sep)
			row_sep = conv->output_argument_sep;
		else {
			locale_arg_sep[0] = format_get_arg_sep ();
			locale_arg_sep[1] = 0;
			row_sep = locale_arg_sep;
		}

		if (conv->output_array_col_sep)
			col_sep = conv->output_array_col_sep;
		else {
			locale_col_sep[0] = format_get_col_sep ();
			locale_col_sep[1] = 0;
			col_sep = locale_col_sep;
		}

		g_string_append_c (target, '{');
		for (y = 0; y < v->v_array.y; y++){
			if (y)
				g_string_append (target, col_sep);

			for (x = 0; x < v->v_array.x; x++){
				Value const *val = v->v_array.vals[x][y];

				if (x)
					g_string_append (target, row_sep);

				/* quote strings */
				if (val->type == VALUE_STRING)
					gnm_strescape (target, val->v_str.val->str);
				else
					value_get_as_gstring (target, val, conv);
			}
		}
		g_string_append_c (target, '}');
		return;
	}

	case VALUE_CELLRANGE: {
		char *tmp;
		/* Note: this makes only sense for absolute references or
		 *       references relative to A1
		 */
		Range range;
		range_init_value (&range, v);
		tmp = global_range_name (v->v_range.cell.a.sheet, &range);;
		g_string_append (target, tmp);
		g_free (tmp);
		return;
	}

	default:
		break;
	}

	g_assert_not_reached ();
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
	GString *res = g_string_sized_new (10);
	value_get_as_gstring (res, v, gnm_expr_conventions_default);
	return g_string_free (res, FALSE);
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
	else if (v->type == VALUE_ERROR)
		return v->v_err.mesg->str;
	else {
		static char *cache[2] = { 0 , 0};
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
		return (int) gnumeric_fake_trunc (v->v_float.val);

	case VALUE_BOOLEAN:
		return v->v_bool.val ? 1 : 0;

	case VALUE_ERROR:
		return 0;

	default:
		g_warning ("value_get_as_int unknown type.");
		return 0;
	}
	return 0;
}

/*
 * FIXME FIXME FIXME : Support errors
 */
gnm_float
value_get_as_float (Value const *v)
{
	if (v == NULL)
		return 0.;

	switch (v->type) {
	case VALUE_EMPTY:
		return 0.;

	case VALUE_STRING:
		return strtognum (v->v_str.val->str, NULL);

	case VALUE_CELLRANGE:
		g_warning ("Getting range as a double: what to do?");
		return 0.0;

	case VALUE_INTEGER:
		return (gnm_float) v->v_int.val;

	case VALUE_ARRAY:
		return 0.0;

	case VALUE_FLOAT:
		return (gnm_float) v->v_float.val;

	case VALUE_BOOLEAN:
		return v->v_bool.val ? 1. : 0.;

	case VALUE_ERROR:
		return 0.;

	default:
		g_warning ("value_get_as_float type error.");
		break;
	}
	return 0.0;
}

/**
 * value_coerce_to_number :
 * @v :
 * @valid :
 *
 * If the value can be used as a number return that number
 * otherwise free it at return an appropriate error
 **/
Value *
value_coerce_to_number (Value *v, gboolean *valid, EvalPos const *ep)
{
	g_return_val_if_fail (v != NULL, NULL);

	*valid = FALSE;
	if (v->type == VALUE_STRING) {
		Value *tmp = format_match_number (value_peek_string (v), NULL);
		value_release (v);
		if (tmp == NULL)
			return value_new_error_VALUE (ep);
		v = tmp;
	} else if (v->type == VALUE_ERROR)
		return v;

	if (!VALUE_IS_NUMBER (v)) {
		value_release (v);
		return value_new_error_VALUE (ep);
	}

	*valid = TRUE;
	return v;
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
	gnm_float const a = value_get_as_float (va);
	gnm_float const b = value_get_as_float (vb);
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
 * IGNORES format.
 *
 * Returns a non-negative difference between 2 values
 */
gnm_float
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
			gint t = g_utf8_collate (a->v_str.val->str, b->v_str.val->str);
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

	/* Booleans > all numbers (Why did excel do this ?? ) */
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
		return abs (ia - ib);
	}

	case VALUE_FLOAT: {
		const gnm_float da = value_get_as_float (a);
		const gnm_float db = value_get_as_float (b);
		return gnumabs (da - db);
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
 *
 * IGNORES format.
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
				t = g_utf8_collate (a->v_str.val->str, b->v_str.val->str);
			} else {
				char *str_a = g_utf8_casefold (a->v_str.val->str, -1);
				char *str_b = g_utf8_casefold (b->v_str.val->str, -1);

				t = g_utf8_collate (str_a, str_b);
				g_free (str_a);
				g_free (str_b);
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

	/* Booleans > all numbers (Why did excel do this ?? ) */
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

void
value_set_fmt (Value *v, StyleFormat const *fmt)
{
	if (fmt != NULL)
		style_format_ref ((StyleFormat *)fmt);
	if (VALUE_FMT (v) != NULL)
		style_format_unref (VALUE_FMT (v));
	VALUE_FMT (v) = (StyleFormat *)fmt;
}

/****************************************************************************/

gboolean
criteria_test_equal (Value const *x, Value const *y)
{
	g_return_val_if_fail (x != NULL, FALSE);
	g_return_val_if_fail (y != NULL, FALSE);
        if (VALUE_IS_NUMBER (x) && VALUE_IS_NUMBER (y))
	        if (value_get_as_float (x) == value_get_as_float (y))
		        return 1;
		else
		        return 0;
	else if (x->type == VALUE_STRING && y->type == VALUE_STRING
		 && g_ascii_strcasecmp (x->v_str.val->str, y->v_str.val->str) == 0)
	        return 1;
	else
	        return 0;
}

gboolean
criteria_test_unequal (Value const *x, Value const *y)
{
	g_return_val_if_fail (x != NULL, FALSE);
	g_return_val_if_fail (y != NULL, FALSE);
        if (VALUE_IS_NUMBER (x) && VALUE_IS_NUMBER (y))
	        if (value_get_as_float (x) != value_get_as_float (y))
		        return 1;
		else
		        return 0;
	else if (x->type == VALUE_STRING && y->type == VALUE_STRING
		 && g_ascii_strcasecmp (x->v_str.val->str, y->v_str.val->str) != 0)
	        return 1;
	else
	        return 0;
}

gboolean
criteria_test_less (Value const *x, Value const *y)
{
	g_return_val_if_fail (x != NULL, FALSE);
	g_return_val_if_fail (y != NULL, FALSE);
        if (VALUE_IS_NUMBER (x) && VALUE_IS_NUMBER (y))
	        if (value_get_as_float (x) < value_get_as_float (y))
		        return 1;
		else
		        return 0;
	else
	        return 0;
}

gboolean
criteria_test_greater (Value const *x, Value const *y)
{
	g_return_val_if_fail (x != NULL, FALSE);
	g_return_val_if_fail (y != NULL, FALSE);
        if (VALUE_IS_NUMBER (x) && VALUE_IS_NUMBER (y))
	        if (value_get_as_float (x) > value_get_as_float (y))
		        return 1;
		else
		        return 0;
	else
	        return 0;
}

gboolean
criteria_test_less_or_equal (Value const *x, Value const *y)
{
	g_return_val_if_fail (x != NULL, FALSE);
	g_return_val_if_fail (y != NULL, FALSE);
        if (VALUE_IS_NUMBER (x) && VALUE_IS_NUMBER (y))
	        if (value_get_as_float (x) <= value_get_as_float (y))
		        return 1;
		else
		        return 0;
	else
	        return 0;
}

gboolean
criteria_test_greater_or_equal (Value const *x, Value const *y)
{
	g_return_val_if_fail (x != NULL, FALSE);
	g_return_val_if_fail (y != NULL, FALSE);
        if (VALUE_IS_NUMBER (x) && VALUE_IS_NUMBER (y))
	        if (value_get_as_float (x) >= value_get_as_float (y))
		        return 1;
		else
		        return 0;
	else
	        return 0;
}

/*
 * Finds a column index of a field.
 */
int
find_column_of_field (const EvalPos *ep, Value *database, Value *field)
{
        Sheet *sheet;
        Cell  *cell;
	gchar *field_name;
	int   begin_col, end_col, row, n, column;
	int   offset;

	offset = database->v_range.cell.b.col -
	  database->v_range.cell.a.col;

	if (field->type == VALUE_INTEGER)
	        return value_get_as_int (field) + offset - 1;

	if (field->type != VALUE_STRING)
	        return -1;

	sheet = eval_sheet (database->v_range.cell.a.sheet, ep->sheet);
	field_name = value_get_as_string (field);
	column = -1;

	/* find the column that is labeled after `field_name' */
	begin_col = database->v_range.cell.a.col;
	end_col = database->v_range.cell.b.col;
	row = database->v_range.cell.a.row;

	for (n = begin_col; n <= end_col; n++) {
		char *txt;
		gboolean match;

	        cell = sheet_cell_get (sheet, n, row);
		if (cell == NULL)
		        continue;
		cell_eval (cell);

		txt = cell_get_rendered_text (cell);
		match = (g_ascii_strcasecmp (field_name, txt) == 0);
		g_free (txt);
		if (match) {
		        column = n;
			break;
		}
	}

	g_free (field_name);
	return column;
}

/*
 * Frees the allocated memory.
 */
void
free_criterias (GSList *criterias)
{
        GSList *list = criterias;

        while (criterias != NULL) {
		GSList *l;
	        database_criteria_t *criteria = criterias->data;

		for (l = criteria->conditions; l; l = l->next) {
			func_criteria_t *cond = l->data;
			value_release (cond->x);
			g_free (cond);
		}

		g_slist_free (criteria->conditions);
		g_free (criteria);
	        criterias = criterias->next;
	}
	g_slist_free (list);
}

void
parse_criteria (char const *criteria, criteria_test_fun_t *fun,
		Value **test_value)
{
	int len;

        if (strncmp (criteria, "<=", 2) == 0) {
	        *fun = (criteria_test_fun_t) criteria_test_less_or_equal;
		len = 2;
	} else if (strncmp (criteria, ">=", 2) == 0) {
	        *fun = (criteria_test_fun_t) criteria_test_greater_or_equal;
		len = 2;
	} else if (strncmp (criteria, "<>", 2) == 0) {
	        *fun = (criteria_test_fun_t) criteria_test_unequal;
		len = 2;
	} else if (*criteria == '<') {
	        *fun = (criteria_test_fun_t) criteria_test_less;
		len = 1;
	} else if (*criteria == '=') {
	        *fun = (criteria_test_fun_t) criteria_test_equal;
		len = 1;
	} else if (*criteria == '>') {
	        *fun = (criteria_test_fun_t) criteria_test_greater;
		len = 1;
	} else {
	        *fun = (criteria_test_fun_t) criteria_test_equal;
		len = 0;
	}

	*test_value = format_match (criteria + len, NULL);
	if (*test_value == NULL)
		*test_value = value_new_string (criteria + len);
}


GSList *
parse_criteria_range (Sheet *sheet, int b_col, int b_row, int e_col, int e_row,
		      int   *field_ind)
{
	database_criteria_t *new_criteria;
	GSList              *criterias = NULL;
	GSList              *conditions;
	Cell 		    *cell;
	func_criteria_t     *cond;
	gchar               *cell_str;

        int i, j;

	for (i = b_row; i <= e_row; i++) {
	        new_criteria = g_new (database_criteria_t, 1);
		conditions = NULL;

		for (j = b_col; j <= e_col; j++) {
		        cell = sheet_cell_get (sheet, j, i);
			if (cell != NULL)
				cell_eval (cell);
			if (cell_is_blank (cell))
			        continue;

			cond = g_new (func_criteria_t, 1);

			/* Equality condition (in number format) */
			if (VALUE_IS_NUMBER (cell->value)) {
			        cond->x = value_duplicate (cell->value);
				cond->fun = (criteria_test_fun_t) criteria_test_equal;
			} else {
				/* Other conditions (in string format) */
				cell_str = cell_get_rendered_text (cell);
				parse_criteria (cell_str, &cond->fun, &cond->x);
				g_free (cell_str);
			}

			cond->column = (field_ind != NULL) ? field_ind[j - b_col] : j - b_col;
			conditions = g_slist_append (conditions, cond);
		}

		new_criteria->conditions = conditions;
		criterias = g_slist_append (criterias, new_criteria);
	}

	return criterias;
}

/*
 * Parses the criteria cell range.
 */
GSList *
parse_database_criteria (const EvalPos *ep, Value *database,
			 Value *criteria)
{
	Sheet	*sheet;
	GSList	*criterias;
	Cell	*cell;

        int   i;
	int   b_col, b_row, e_col, e_row;
	int   *field_ind;

	sheet = eval_sheet (criteria->v_range.cell.a.sheet, ep->sheet);
	b_col = criteria->v_range.cell.a.col;
	b_row = criteria->v_range.cell.a.row;
	e_col = criteria->v_range.cell.b.col;
	e_row = criteria->v_range.cell.b.row;

	field_ind = g_new (int, (e_col - b_col + 1));

	/* Find the index numbers for the columns of criterias */
	for (i = b_col; i <= e_col; i++) {
	        cell = sheet_cell_get (sheet, i, b_row);
		if (cell != NULL)
			cell_eval (cell);
		if (cell_is_blank (cell))
		        continue;
		field_ind[i - b_col] =
		        find_column_of_field (ep, database, cell->value);
		if (field_ind[i - b_col] == -1) {
		        g_free (field_ind);
			return NULL;
		}
	}

	criterias = parse_criteria_range (sheet, b_col, b_row + 1,
					  e_col, e_row,
					  field_ind);

	g_free (field_ind);
	return criterias;
}

/* Finds the rows from the given database that match the criteria.
 */
GSList *
find_rows_that_match (Sheet *sheet, int first_col, int first_row,
		      int last_col, int last_row,
		      GSList *criterias, gboolean unique_only)
{
	GSList *current, *conditions, *rows;
	Cell   *test_cell;
	int    row, add_flag;
	rows = NULL;

	for (row = first_row; row <= last_row; row++) {

		current = criterias;
		add_flag = 1;
		for (current = criterias; current != NULL;
		     current = current->next) {
			database_criteria_t *current_criteria;

			add_flag = 1;
			current_criteria = current->data;
			conditions = current_criteria->conditions;

			while (conditions != NULL) {
				func_criteria_t const *cond = conditions->data;

				test_cell = sheet_cell_get (sheet,
					first_col + cond->column, row);
				if (test_cell != NULL)
					cell_eval (test_cell);
				if (cell_is_blank (test_cell))
					continue;

				if (!cond->fun (test_cell->value, cond->x)) {
					add_flag = 0;
					break;
				}
				conditions = conditions->next;
			}

			if (add_flag)
				break;
		}
		if (add_flag) {
			gint *p;

			if (unique_only) {
				GSList *c;
				Cell   *cell;
				gint    i, trow;
				gchar  *t1, *t2;

				for (c = rows; c != NULL; c = c->next) {
					trow = *((gint *) c->data);
					for (i = first_col; i <= last_col; i++) {
						test_cell =
							sheet_cell_get (sheet, i, trow);
						cell =
							sheet_cell_get (sheet, i, row);
						t1 = cell_get_rendered_text (cell);
						t2 = cell_get_rendered_text (test_cell);
						if (strcmp (t1, t2) != 0)
							goto row_ok;
					}
					goto filter_row;
row_ok:
					;
				}
			}
			p = g_new (gint, 1);
			*p = row;
			rows = g_slist_prepend (rows, (gpointer) p);
filter_row:
			;
		}
	}

	return g_slist_reverse (rows);
}

/****************************************************************************/

const ValueErr value_terminate_err = { VALUE_ERROR, NULL, NULL };
static const ValueInt the_value_zero = { VALUE_INTEGER, NULL, 0 };
const Value *value_zero = (const Value *)&the_value_zero;

void
value_init (void)
{
	size_t i;

	for (i = 0; i < G_N_ELEMENTS (standard_errors); i++) {
		standard_errors[i].locale_name = _(standard_errors[i].C_name);
		standard_errors[i].locale_name_str =
			string_get (standard_errors[i].locale_name);
	}

#if USE_VALUE_POOLS
	/* ValueInt and ValueBool ought to have the same size.  */
	value_int_pool =
		gnm_mem_chunk_new ("value int/bool pool",
				   MAX (sizeof (ValueInt), sizeof (ValueBool)),
				   16 * 1024 - 128);

	value_float_pool =
		gnm_mem_chunk_new ("value float pool",
				   sizeof (ValueFloat),
				   16 * 1024 - 128);

	value_error_pool =
		gnm_mem_chunk_new ("value error pool",
				   sizeof (ValueErr),
				   16 * 1024 - 128);

	value_string_pool =
		gnm_mem_chunk_new ("value string pool",
				   sizeof (ValueStr),
				   16 * 1024 - 128);

	value_range_pool =
		gnm_mem_chunk_new ("value range pool",
				   sizeof (ValueRange),
				   16 * 1024 - 128);

	value_array_pool =
		gnm_mem_chunk_new ("value array pool",
				   sizeof (ValueArray),
				   16 * 1024 - 128);
#endif
}

void
value_shutdown (void)
{
	size_t i;

	for (i = 0; i < G_N_ELEMENTS (standard_errors); i++) {
		string_unref (standard_errors[i].locale_name_str);
		standard_errors[i].locale_name_str = NULL;
	}

#if USE_VALUE_POOLS
	gnm_mem_chunk_destroy (value_int_pool, FALSE);
	value_int_pool = NULL;

	gnm_mem_chunk_destroy (value_float_pool, FALSE);
	value_float_pool = NULL;

	gnm_mem_chunk_destroy (value_error_pool, FALSE);
	value_error_pool = NULL;

	gnm_mem_chunk_destroy (value_string_pool, FALSE);
	value_string_pool = NULL;

	gnm_mem_chunk_destroy (value_range_pool, FALSE);
	value_range_pool = NULL;

	gnm_mem_chunk_destroy (value_array_pool, FALSE);
	value_array_pool = NULL;
#endif
}

/****************************************************************************/
