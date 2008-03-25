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
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "value.h"

#include "parse-util.h"
#include "style.h"
#include "gnm-format.h"
#include "str.h"
#include "position.h"
#include "mathfunc.h"
#include "gutils.h"
#include "workbook.h"
#include "expr.h"

#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <ranges.h>
#include <sheet.h>
#include <cell.h>
#include <number-match.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-locale.h>

#ifndef USE_VALUE_POOLS
#ifdef HAVE_G_SLICE_ALLOC
#define USE_VALUE_POOLS 0
#else
#define USE_VALUE_POOLS 1
#endif
#endif

#if USE_VALUE_POOLS
static GOMemChunk *value_bool_pool;
static GOMemChunk *value_float_pool;
static GOMemChunk *value_error_pool;
static GOMemChunk *value_string_pool;
static GOMemChunk *value_range_pool;
static GOMemChunk *value_array_pool;
#define CHUNK_ALLOC(T,p) ((T*)go_mem_chunk_alloc (p))
#define CHUNK_FREE(p,v) go_mem_chunk_free ((p), (v))
#else
static int value_allocations = 0;
#ifdef HAVE_G_SLICE_ALLOC
#define CHUNK_ALLOC(T,c) (value_allocations++, g_slice_new (T))
#define CHUNK_FREE(p,v) (value_allocations--, g_slice_free1 (sizeof(*v),(v)))
#else
#define CHUNK_ALLOC(T,c) (value_allocations++, g_new (T,1))
#define CHUNK_FREE(p,v) (value_allocations--, g_free ((v)))
#endif
#endif


static struct {
	char const *C_name;
	char const *locale_name;
	GnmString *locale_name_str;
} standard_errors[] = {
	{ N_("#NULL!"), NULL, NULL },
	{ N_("#DIV/0!"), NULL, NULL },
	{ N_("#VALUE!"), NULL, NULL },
	{ N_("#REF!"), NULL, NULL },
	{ N_("#NAME?"), NULL, NULL },
	{ N_("#NUM!"), NULL, NULL },
	{ N_("#N/A"), NULL, NULL },
	{ N_("#UNKNOWN!"), NULL, NULL }
};

GnmValue *
value_new_empty (void)
{
	/* This is a constant.  No need to allocate any memory.  */
	static GnmValueAny v = { VALUE_EMPTY, NULL };
	return (GnmValue *)&v;
}

GnmValue *
value_new_bool (gboolean b)
{
	GnmValueBool *v = CHUNK_ALLOC (GnmValueBool, value_bool_pool);
	*((GnmValueType *)&(v->type)) = VALUE_BOOLEAN;
	v->fmt = NULL;
	v->val = b;
	return (GnmValue *)v;
}

GnmValue *
value_new_int (int i)
{
	return value_new_float (i);
}

GnmValue *
value_new_float (gnm_float f)
{
	if (gnm_finite (f)) {
		GnmValueFloat *v = CHUNK_ALLOC (GnmValueFloat, value_float_pool);
		*((GnmValueType *)&(v->type)) = VALUE_FLOAT;
		v->fmt = NULL;
		v->val = f;
		return (GnmValue *)v;
	} else {
		/* FIXME: bogus ep sent here.  What to do?  */
		return value_new_error_NUM (NULL);
	}
}

GnmValue *
value_new_error (GnmEvalPos const *ep, char const *mesg)
{
	GnmValueErr *v = CHUNK_ALLOC (GnmValueErr, value_error_pool);
	*((GnmValueType *)&(v->type)) = VALUE_ERROR;
	v->fmt = NULL;
	v->mesg = gnm_string_get (mesg);
	return (GnmValue *)v;
}

GnmValue *
value_new_error_str (GnmEvalPos const *ep, GnmString *mesg)
{
	GnmValueErr *v = CHUNK_ALLOC (GnmValueErr, value_error_pool);
	*((GnmValueType *)&(v->type)) = VALUE_ERROR;
	v->fmt = NULL;
	v->mesg = gnm_string_ref (mesg);
	return (GnmValue *)v;
}

GnmValue *
value_new_error_std (GnmEvalPos const *pos, GnmStdError err)
{
	size_t i = (size_t)err;
	g_return_val_if_fail (i < G_N_ELEMENTS (standard_errors), NULL);

	return value_new_error_str (pos, standard_errors[i].locale_name_str);
}


GnmValue *
value_new_error_NULL (GnmEvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_NULL].locale_name_str);
}

GnmValue *
value_new_error_DIV0 (GnmEvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_DIV0].locale_name_str);
}

GnmValue *
value_new_error_VALUE (GnmEvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_VALUE].locale_name_str);
}

GnmValue *
value_new_error_REF (GnmEvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_REF].locale_name_str);
}

GnmValue *
value_new_error_NAME (GnmEvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_NAME].locale_name_str);
}

GnmValue *
value_new_error_NUM (GnmEvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_NUM].locale_name_str);
}

GnmValue *
value_new_error_NA (GnmEvalPos const *pos)
{
	return value_new_error_str (pos, standard_errors[GNM_ERROR_NA].locale_name_str);
}

char const *
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
GnmValue *
value_error_set_pos (GnmValueErr *err, GnmEvalPos const *pos)
{
    g_return_val_if_fail (err != NULL, NULL);
    g_return_val_if_fail (VALUE_IS_ERROR (err), NULL);

    /* err->src = *pos; */
    return (GnmValue *)err;
}

GnmStdError
value_error_classify (GnmValue const *v)
{
	size_t i;

	g_return_val_if_fail (v != NULL, GNM_ERROR_UNKNOWN);

	if (!VALUE_IS_ERROR (v))
		return GNM_ERROR_UNKNOWN;

	for (i = 0; i < G_N_ELEMENTS (standard_errors); i++)
		if (standard_errors[i].locale_name_str == v->v_err.mesg)
			return (GnmStdError)i;

	return GNM_ERROR_UNKNOWN;
}


/* NOTE : absorbs the reference */
GnmValue *
value_new_string_str (GnmString *str)
{
	GnmValueStr *v = CHUNK_ALLOC (GnmValueStr, value_string_pool);
	*((GnmValueType *)&(v->type)) = VALUE_STRING;
	v->fmt = NULL;
	v->val = str;
	return (GnmValue *)v;
}

GnmValue *
value_new_string (char const *str)
{
	return value_new_string_str (gnm_string_get (str));
}

GnmValue *
value_new_string_nocopy (char *str)
{
	return value_new_string_str (gnm_string_get_nocopy (str));
}

GnmValue *
value_new_cellrange_unsafe (GnmCellRef const *a, GnmCellRef const *b)
{
	GnmValueRange *v = CHUNK_ALLOC (GnmValueRange, value_range_pool);
	*((GnmValueType *)&(v->type)) = VALUE_CELLRANGE;
	v->fmt = NULL;
	v->cell.a = *a;
	v->cell.b = *b;
	return (GnmValue *)v;
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
GnmValue *
value_new_cellrange (GnmCellRef const *a, GnmCellRef const *b,
		     int eval_col, int eval_row)
{
	GnmValueRange *v = CHUNK_ALLOC (GnmValueRange, value_range_pool);
	int tmp;

	*((GnmValueType *)&(v->type)) = VALUE_CELLRANGE;
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

	return (GnmValue *)v;
}

GnmValue *
value_new_cellrange_r (Sheet *sheet, GnmRange const *r)
{
	GnmValueRange *v = CHUNK_ALLOC (GnmValueRange, value_range_pool);
	GnmCellRef *a, *b;

	*((GnmValueType *)&(v->type)) = VALUE_CELLRANGE;
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

	return (GnmValue *)v;
}

/**
 * value_new_cellrange_str :
 * @sheet: the sheet where the cell range is evaluated. This really only needed if
 *         the range given does not include a sheet specification.
 * @str: a range specification (ex: "A1", "A1:C3", "Sheet1!A1:C3", "R1C1").
 *
 * Parse @str using the convention associated with @sheet.
 * Returns a (GnmValue *) of type VALUE_CELLRANGE if the @range was
 * succesfully parsed or NULL on failure.
 */
GnmValue *
value_new_cellrange_str (Sheet *sheet, char const *str)
{
	GnmParsePos pp;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (str != NULL, NULL);

	parse_pos_init_sheet (&pp, sheet);
	return value_new_cellrange_parsepos_str (&pp, str);
}

/**
 * value_new_cellrange_parsepos_str :
 * @pp:  if a relative range is specified, then it will be interpreted relative
 *       to this position (affects only A1-style relative references).
 * @str: a range specification (ex: "A1", "A1:C3", "Sheet1!A1:C3", "R1C1").
 *
 * Parse @str using the convention associated with @sheet.
 * Returns a (GnmValue *) of type VALUE_CELLRANGE if the @range was
 * succesfully parsed or NULL on failure.
 */
GnmValue *
value_new_cellrange_parsepos_str (GnmParsePos *pp, char const *str)
{
	GnmExprTop const *texpr;

	g_return_val_if_fail (pp != NULL, NULL);
	g_return_val_if_fail (str != NULL, NULL);

	texpr = gnm_expr_parse_str (str, pp,
		GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES |
		GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS,
		NULL, NULL);

	if (texpr != NULL)  {
		GnmValue *value = gnm_expr_top_get_range (texpr);
		gnm_expr_top_unref (texpr);
		return value;
	}

	return NULL;
}

GnmValue *
value_new_array_non_init (guint cols, guint rows)
{
	GnmValueArray *v = CHUNK_ALLOC (GnmValueArray, value_array_pool);
	*((GnmValueType *)&(v->type)) = VALUE_ARRAY;
	v->fmt = NULL;
	v->x = cols;
	v->y = rows;
	v->vals = g_new (GnmValue **, cols);
	return (GnmValue *)v;
}

GnmValue *
value_new_array (guint cols, guint rows)
{
	guint x, y;
	GnmValueArray *v = (GnmValueArray *)value_new_array_non_init (cols, rows);

	for (x = 0; x < cols; x++) {
		v->vals[x] = g_new (GnmValue *, rows);
		for (y = 0; y < rows; y++)
			v->vals[x][y] = value_new_int (0);
	}
	return (GnmValue *)v;
}

GnmValue *
value_new_array_empty (guint cols, guint rows)
{
	guint x, y;
	GnmValueArray *v = (GnmValueArray *)value_new_array_non_init (cols, rows);

	for (x = 0; x < cols; x++) {
		v->vals[x] = g_new (GnmValue *, rows);
		for (y = 0; y < rows; y++)
			v->vals[x][y] = NULL;
	}
	return (GnmValue *)v;
}

/*
 * Returns TRUE, FALSE, or -1.
 */
static int
value_parse_boolean (char const *str, gboolean translated)
{
	if (translated) {
		/* FIXME: ascii???  */
		if (0 == g_ascii_strcasecmp (str, go_locale_boolean_name (TRUE)))
			return +TRUE;
		else if (0 == g_ascii_strcasecmp (str, go_locale_boolean_name (FALSE)))
			return +FALSE;
		else
			return -1;
	} else {
		if (0 == g_ascii_strcasecmp (str, "TRUE"))
			return +TRUE;
		else if (0 == g_ascii_strcasecmp (str, "FALSE"))
			return +FALSE;
		else
			return -1;
	}
}


GnmValue *
value_new_from_string (GnmValueType t, char const *str, GOFormat *sf,
		       gboolean translated)
{
	GnmValue *res = NULL;
	switch (t) {
	case VALUE_EMPTY:
		res = value_new_empty ();
		break;

	case VALUE_BOOLEAN: {
		int i = value_parse_boolean (str, translated);
		if (i != -1)
			res = value_new_bool ((gboolean)i);
		break;
	}

	case VALUE_INTEGER:
	case VALUE_FLOAT: {
		char *end;
		gnm_float d;

		d = gnm_strto (str, &end);
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
value_release (GnmValue *value)
{
	g_return_if_fail (value != NULL);

	if (VALUE_FMT (value) != NULL)
		go_format_unref (VALUE_FMT (value));

	switch (value->type) {
	case VALUE_EMPTY:
		/* We did not allocate anything, there is nothing to free */
		return;

	case VALUE_BOOLEAN:
		CHUNK_FREE (value_bool_pool, &value->v_bool);
		return;

	case VALUE_FLOAT:
		CHUNK_FREE (value_float_pool, &value->v_float);
		return;

	case VALUE_ERROR:
		/* Do not release VALUE_TERMINATE, it is a magic number */
		if (value == VALUE_TERMINATE) {
			g_warning ("Someone freed VALUE_TERMINATE -- shame on them.");
			return;
		}

		gnm_string_unref (value->v_err.mesg);
		CHUNK_FREE (value_error_pool, &value->v_err);
		return;

	case VALUE_STRING:
		gnm_string_unref (value->v_str.val);
		CHUNK_FREE (value_string_pool, &value->v_str);
		return;

	case VALUE_ARRAY: {
		GnmValueArray *v = &value->v_array;
		int x, y;

		for (x = 0; x < v->x; x++) {
			for (y = 0; y < v->y; y++) {
				if (v->vals[x][y])
					value_release (v->vals[x][y]);
			}
			g_free (v->vals[x]);
		}

		g_free (v->vals);
		CHUNK_FREE (value_array_pool, v);
		return;
	}

	case VALUE_CELLRANGE:
		CHUNK_FREE (value_range_pool, &value->v_range);
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

/**
 * value_dup :
 * @src : #GnmValue
 *
 * Returns a copy of @src.  @src == NULL will return NULL
 **/
GnmValue *
value_dup (GnmValue const *src)
{
	GnmValue *res;

	if (src == NULL)
		return NULL;

	switch (src->type){
	case VALUE_EMPTY:
		res = value_new_empty ();
		break;

	case VALUE_BOOLEAN:
		res = value_new_bool (src->v_bool.val);
		break;

	case VALUE_FLOAT:
		res = value_new_float (src->v_float.val);
		break;

	case VALUE_ERROR:
		res = value_new_error_str (NULL, /* &src->v_err.src, */
					   src->v_err.mesg);
		break;

	case VALUE_STRING:
		gnm_string_ref (src->v_str.val);
		res = value_new_string_str (src->v_str.val);
		break;

	case VALUE_CELLRANGE:
		res = value_new_cellrange_unsafe (&src->v_range.cell.a,
						  &src->v_range.cell.b);
		break;

	case VALUE_ARRAY: {
		int x, y;
		GnmValueArray *array = (GnmValueArray *)value_new_array_non_init (
			src->v_array.x, src->v_array.y);

		for (x = 0; x < array->x; x++) {
			array->vals[x] = g_new (GnmValue *, array->y);
			for (y = 0; y < array->y; y++)
				array->vals[x][y] = value_dup (src->v_array.vals[x][y]);
		}
		res = (GnmValue *)array;
		break;
	}

	default:
		g_warning ("value_dup problem.");
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
	GnmValue const *a = *(GnmValue const **)ptr_a;
	GnmValue const *b = *(GnmValue const **)ptr_b;
	switch (value_compare (a, b, TRUE)) {
	case IS_EQUAL :   return  0;
	case IS_LESS :    return -1;
	case IS_GREATER : return  1;
	default :
		break;
	}
	return a->type - b->type;
}

int
value_cmp_reverse (void const *ptr_a, void const *ptr_b)
{
	return -value_cmp (ptr_a, ptr_b);
}

gboolean
value_equal (GnmValue const *a, GnmValue const *b)
{
	if (a->type != b->type)
		return FALSE;

	switch (a->type) {
	case VALUE_BOOLEAN:
		return a->v_bool.val == b->v_bool.val;

	case VALUE_STRING:
		return a->v_str.val == b->v_str.val;

	case VALUE_ERROR:
		return a->v_err.mesg == b->v_err.mesg;

	case VALUE_FLOAT:
		return a->v_float.val == b->v_float.val;

	case VALUE_EMPTY:
		return TRUE;

	case VALUE_CELLRANGE:
		return	gnm_cellref_equal (&a->v_range.cell.a, &b->v_range.cell.a) &&
			gnm_cellref_equal (&a->v_range.cell.b, &b->v_range.cell.b);

	case VALUE_ARRAY:
		if (a->v_array.x == b->v_array.x && a->v_array.y == b->v_array.y) {
			int x, y;

			for (y = 0; y < a->v_array.y; y++)
				for (x = 0; x < a->v_array.x; x++)
					if (!value_equal (a->v_array.vals[x][y],
							  b->v_array.vals[x][y]))
						return FALSE;
			return TRUE;
		} else
			return FALSE;

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
		return FALSE;
#endif
	}
}

guint
value_hash (GnmValue const *v)
{
	switch (v->type) {
	case VALUE_BOOLEAN:
		return v->v_bool.val ? 0x555aaaa : 0xaaa5555;

	case VALUE_STRING:
		return g_str_hash (v->v_str.val->str);

	case VALUE_ERROR:
		return g_str_hash (v->v_err.mesg->str);

	case VALUE_FLOAT: {
		int expt;
		gnm_float mant = gnm_frexp (gnm_abs (v->v_float.val), &expt);
		guint h = ((guint)(0x80000000u * mant)) ^ expt;
		if (v->v_float.val >= 0)
			h ^= 0x55555555;
		return h;
	}

	case VALUE_EMPTY:
		return 42;

	case VALUE_CELLRANGE:
		/* FIXME: take sheet into account?  */
		return (gnm_cellref_hash (&v->v_range.cell.a) * 3) ^
			gnm_cellref_hash (&v->v_range.cell.b);

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
value_get_as_bool (GnmValue const *v, gboolean *err)
{
	if (err)
		*err = FALSE;

	if (v == NULL)
		return FALSE;

	switch (v->type) {
	case VALUE_EMPTY:
		return FALSE;

	case VALUE_BOOLEAN:
		return v->v_bool.val;

	case VALUE_STRING: {
		int i = value_parse_boolean (value_peek_string (v), FALSE);
		if (i == -1) {
			if (err)
				*err = TRUE;
			return FALSE;
		}
		return (gboolean)i;
	}

	case VALUE_FLOAT:
		return v->v_float.val != 0.0;

	default:
		g_warning ("Unhandled value in value_get_as_bool.");

	case VALUE_CELLRANGE:
	case VALUE_ARRAY:
	case VALUE_ERROR:
		if (err)
			*err = TRUE;
	}
	return FALSE;
}

/*
 * use only if you are sure the value is ok
 */
gboolean
value_get_as_checked_bool (GnmValue const *v)
{
	gboolean result, err;

	result = value_get_as_bool (v, &err);

	g_return_val_if_fail (!err, FALSE);

	return result;
}

void
value_get_as_gstring (GnmValue const *v, GString *target,
		      GnmConventions const *conv)
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
			go_strescape (target, v->v_err.mesg->str);
		} else
			g_string_append (target, value_error_name (e, conv->output.translated));
		return;
	}

	case VALUE_BOOLEAN: {
		gboolean b = v->v_bool.val;
		g_string_append (target,
				 conv->output.translated
				 ? go_locale_boolean_name (b)
				 : (b ? "TRUE" : "FALSE"));
		return;
	}

	case VALUE_STRING:
		g_string_append (target, v->v_str.val->str);
		return;

	case VALUE_FLOAT:
		g_string_append_printf (target, "%.*" GNM_FORMAT_g, GNM_DIG,
					v->v_float.val);
		return;

	case VALUE_ARRAY: {
		GnmValue const *val;
		gunichar row_sep, col_sep;
		int x, y;

		if (conv->array_row_sep)
			row_sep = conv->array_row_sep;
		else
			row_sep = go_locale_get_row_sep ();
		if (conv->array_col_sep)
			col_sep = conv->array_col_sep;
		else
			col_sep = go_locale_get_col_sep ();

		g_string_append_c (target, '{');
		for (y = 0; y < v->v_array.y; y++){
			if (y)
				g_string_append_unichar (target, row_sep);

			for (x = 0; x < v->v_array.x; x++){
				val = v->v_array.vals[x][y];

				if (x)
					g_string_append_unichar (target, col_sep);

				/* quote strings */
				if (VALUE_IS_STRING (val))
					go_strescape (target, val->v_str.val->str);
				else
					value_get_as_gstring (val, target, conv);
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
		GnmRange range;
		range_init_value (&range, v);
		tmp = global_range_name (v->v_range.cell.a.sheet, &range);
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
 **/
char *
value_get_as_string (GnmValue const *v)
{
	GString *res = g_string_sized_new (10);
	value_get_as_gstring (v, res, gnm_conventions_default);
	return g_string_free (res, FALSE);
}

/*
 * Result will stay valid until (a) the value is disposed of, or (b) two
 * further calls to this function are made.
 */
char const *
value_peek_string (GnmValue const *v)
{
	g_return_val_if_fail (v, "");

	if (VALUE_IS_STRING (v))
		return v->v_str.val->str;
	else if (VALUE_IS_ERROR (v))
		return v->v_err.mesg->str;
	else {
		static char *cache[2] = { NULL, NULL };
		static int next = 0;
		char const *s;

		g_free (cache[next]);
		s = cache[next] = value_get_as_string (v);
		next = (next + 1) % G_N_ELEMENTS (cache);
		return s;
	}
}

/*
 * FIXME FIXME FIXME : Support errors
 */
int
value_get_as_int (GnmValue const *v)
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

	case VALUE_ARRAY:
		return 0;

	case VALUE_FLOAT:
		return (int) gnm_fake_trunc (v->v_float.val);

	case VALUE_BOOLEAN:
		return v->v_bool.val ? 1 : 0;

	case VALUE_ERROR:
		return 0;

	default:
		g_warning ("value_get_as_int unknown type 0x%x (%d).", v->type, v->type);
		return 0;
	}
	return 0;
}

/*
 * FIXME FIXME FIXME : Support errors
 */
gnm_float
value_get_as_float (GnmValue const *v)
{
	if (v == NULL)
		return 0.;

	switch (v->type) {
	case VALUE_EMPTY:
		return 0.;

	case VALUE_STRING:
		return gnm_strto (v->v_str.val->str, NULL);

	case VALUE_CELLRANGE:
		g_warning ("Getting range as a double: what to do?");
		return 0.0;

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

gboolean
value_is_zero (GnmValue const *v)
{
	return gnm_abs (value_get_as_float (v)) < 64 * GNM_EPSILON;
}

GnmRangeRef const *
value_get_rangeref (GnmValue const *v)
{
	g_return_val_if_fail (v->type == VALUE_CELLRANGE, NULL);
	return &v->v_range.cell;
}


/**
 * value_coerce_to_number :
 * @v :
 * @valid :
 *
 * If the value can be used as a number return that number
 * otherwise free it at return an appropriate error
 **/
GnmValue *
value_coerce_to_number (GnmValue *v, gboolean *valid, GnmEvalPos const *ep)
{
	g_return_val_if_fail (v != NULL, NULL);

	*valid = FALSE;
	if (VALUE_IS_STRING (v)) {
		GnmValue *tmp = format_match_number (value_peek_string (v), NULL,
			workbook_date_conv (ep->sheet->workbook));
		value_release (v);
		if (tmp == NULL)
			return value_new_error_VALUE (ep);
		v = tmp;
	} else if (VALUE_IS_ERROR (v))
		return v;

	if (!VALUE_IS_NUMBER (v)) {
		value_release (v);
		return value_new_error_VALUE (ep);
	}

	*valid = TRUE;
	return v;
}

void
value_array_set (GnmValue *array, int col, int row, GnmValue *v)
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

static GnmValDiff
compare_bool_bool (GnmValue const *va, GnmValue const *vb)
{
	gboolean err; /* Ignored */
	gboolean const a = value_get_as_bool (va, &err);
	gboolean const b = value_get_as_bool (vb, &err);
	if (a)
		return b ? IS_EQUAL : IS_GREATER;
	return b ? IS_LESS : IS_EQUAL;
}

static GnmValDiff
compare_float_float (GnmValue const *va, GnmValue const *vb)
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
value_diff (GnmValue const *a, GnmValue const *b)
{
	GnmValueType ta, tb;

	/* Handle trivial (including empty/empty) and double NULL */
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
		case VALUE_FLOAT: case VALUE_BOOLEAN:
		default :
			return DBL_MAX;
		}

	} else if (tb == VALUE_STRING) {
		switch (ta) {
		/* (empty, or number) < String */
		case VALUE_EMPTY :
			if (*b->v_str.val->str == '\0')
				return 0.;

		case VALUE_FLOAT : case VALUE_BOOLEAN :
		default :
			return DBL_MAX;
		}
	}

	/* Booleans > all numbers (Why did excel do this ?? ) */
	if (ta == VALUE_BOOLEAN && tb == VALUE_FLOAT)
		return DBL_MAX;
	if (tb == VALUE_BOOLEAN && ta == VALUE_FLOAT)
		return DBL_MAX;

	switch ((ta > tb) ? ta : tb) {
	case VALUE_EMPTY:	/* Empty Empty compare */
		return 0.;

	case VALUE_BOOLEAN:
		return (compare_bool_bool (a, b) == IS_EQUAL) ? 0. : DBL_MAX;

	case VALUE_FLOAT: {
		gnm_float const da = value_get_as_float (a);
		gnm_float const db = value_get_as_float (b);
		return gnm_abs (da - db);
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
GnmValDiff
value_compare (GnmValue const *a, GnmValue const *b, gboolean case_sensitive)
{
	GnmValueType ta, tb;

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

		case VALUE_FLOAT:
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

		case VALUE_FLOAT :
			return IS_LESS;

		/* Strings are < FALSE ?? */
		case VALUE_BOOLEAN :
			return IS_GREATER;

		default :
			return TYPE_MISMATCH;
		}
	}

	/* Booleans > all numbers (Why did excel do this ?? ) */
	if (ta == VALUE_BOOLEAN && tb == VALUE_FLOAT)
		return IS_GREATER;
	if (tb == VALUE_BOOLEAN && ta == VALUE_FLOAT)
		return IS_LESS;

	switch ((ta > tb) ? ta : tb) {
	case VALUE_EMPTY:	/* Empty Empty compare */
		return IS_EQUAL;

	case VALUE_BOOLEAN:
		return compare_bool_bool (a, b);

	case VALUE_FLOAT:
		return compare_float_float (a, b);
	default:
		return TYPE_MISMATCH;
	}
}

void
value_set_fmt (GnmValue *v, GOFormat const *fmt)
{
	if (fmt != NULL)
		go_format_ref ((GOFormat *)fmt);
	if (VALUE_FMT (v) != NULL)
		go_format_unref (VALUE_FMT (v));
	VALUE_FMT (v) = (GOFormat *)fmt;
}

/****************************************************************************/

typedef enum { CRIT_NULL, CRIT_FLOAT, CRIT_BADFLOAT, CRIT_STRING } CritType;

static CritType
criteria_inspect_values (GnmValue const *x, GnmValue const *y,
			 gnm_float *xr, gnm_float *yr,
			 GODateConventions const *date_conv)
{
	GnmValue *vx;

	if (x == NULL || y == NULL)
		return CRIT_NULL;

	if (!VALUE_IS_NUMBER (y))
		return CRIT_STRING;
	*yr = value_get_as_float (y);

	if (VALUE_IS_NUMBER (x)) {
		*xr = value_get_as_float (x);
		return CRIT_FLOAT;
	}

	vx = format_match (value_peek_string (x), NULL, date_conv);
	if (!vx)
		return CRIT_BADFLOAT;

	*xr = value_get_as_float (vx);
	value_release (vx);
	return CRIT_FLOAT;
}


static gboolean
criteria_test_equal (GnmValue const *x, GnmValue const *y,
		     GODateConventions const *date_conv)
{
	gnm_float xf, yf;

	switch (criteria_inspect_values (x, y, &xf, &yf, date_conv)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_BADFLOAT:
		return FALSE;
	case CRIT_FLOAT:
		return xf == yf;
	case CRIT_STRING:
		/* FIXME: _ascii_??? */
		return g_ascii_strcasecmp (value_peek_string (x),
					   value_peek_string (y)) == 0;
	}
}

static gboolean
criteria_test_unequal (GnmValue const *x, GnmValue const *y,
		       GODateConventions const *date_conv)
{
	gnm_float xf, yf;

	switch (criteria_inspect_values (x, y, &xf, &yf, date_conv)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_BADFLOAT:
		return FALSE;
	case CRIT_FLOAT:
		return xf != yf;
	case CRIT_STRING:
		/* FIXME: _ascii_??? */
		return g_ascii_strcasecmp (value_peek_string (x),
					   value_peek_string (y)) != 0;
	}
}

static gboolean
criteria_test_less (GnmValue const *x, GnmValue const *y,
		    GODateConventions const *date_conv)
{
	gnm_float xf, yf;

	switch (criteria_inspect_values (x, y, &xf, &yf, date_conv)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_BADFLOAT:
	case CRIT_STRING:
		return FALSE;
	case CRIT_FLOAT:
		return xf < yf;
	}
}

static gboolean
criteria_test_greater (GnmValue const *x, GnmValue const *y,
		       GODateConventions const *date_conv)
{
	gnm_float xf, yf;

	switch (criteria_inspect_values (x, y, &xf, &yf, date_conv)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_BADFLOAT:
	case CRIT_STRING:
		return FALSE;
	case CRIT_FLOAT:
		return xf > yf;
	}
}

static gboolean
criteria_test_less_or_equal (GnmValue const *x, GnmValue const *y,
			     GODateConventions const *date_conv)
{
	gnm_float xf, yf;

	switch (criteria_inspect_values (x, y, &xf, &yf, date_conv)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_BADFLOAT:
	case CRIT_STRING:
		return FALSE;
	case CRIT_FLOAT:
		return xf <= yf;
	}
}

static gboolean
criteria_test_greater_or_equal (GnmValue const *x, GnmValue const *y,
				GODateConventions const *date_conv)
{
	gnm_float xf, yf;

	switch (criteria_inspect_values (x, y, &xf, &yf, date_conv)) {
	default:
		g_assert_not_reached ();
	case CRIT_NULL:
	case CRIT_BADFLOAT:
	case CRIT_STRING:
		return FALSE;
	case CRIT_FLOAT:
		return xf >= yf;
	}
}

/*
 * Finds a column index of a field.
 */
int
find_column_of_field (GnmEvalPos const *ep,
		      GnmValue const *database, GnmValue const *field)
{
        Sheet *sheet;
        GnmCell  *cell;
	gchar *field_name;
	int   begin_col, end_col, row, n, column;
	int   offset;

	offset = database->v_range.cell.a.col;

	if (VALUE_IS_FLOAT (field))
		return value_get_as_int (field) + offset - 1;

	if (!VALUE_IS_STRING (field))
		return -1;

	sheet = eval_sheet (database->v_range.cell.a.sheet, ep->sheet);
	field_name = value_get_as_string (field);
	column = -1;

	/* find the column that is labeled after `field_name' */
	begin_col = database->v_range.cell.a.col;
	end_col = database->v_range.cell.b.col;
	row = database->v_range.cell.a.row;

	for (n = begin_col; n <= end_col; n++) {
		char const *txt;
		gboolean match;

		cell = sheet_cell_get (sheet, n, row);
		if (cell == NULL)
			continue;
		gnm_cell_eval (cell);

		txt = cell->value
			? value_peek_string (cell->value)
			: "";
		match = (g_ascii_strcasecmp (field_name, txt) == 0);
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
		GnmDBCriteria *criteria = criterias->data;

		for (l = criteria->conditions; l; l = l->next) {
			GnmCriteria *cond = l->data;
			value_release (cond->x);
			g_free (cond);
		}

		g_slist_free (criteria->conditions);
		g_free (criteria);
		criterias = criterias->next;
	}
	g_slist_free (list);
}

/**
 * parse_criteria :
 * @crit_val : #GnmValue
 * @fun : #GnmCriteriaFunc result
 * @test_value : #GnmValue the value to compare against.
 * @iter_flags :
 * @date_conv : #GODateConventions
 *
 * If @crit_val is a number set @text_val and @fun to test for equality, other
 * wise parse it as a string and see if it matches one of the available
 * operators.  Caller is responsible for freeing @test_value.
 **/
void
parse_criteria (GnmValue const *crit_val, GnmCriteriaFunc *fun, GnmValue **test_value,
		CellIterFlags *iter_flags, GODateConventions const *date_conv)
{
	int len;
	char const *criteria;

	if (iter_flags)
		*iter_flags = CELL_ITER_IGNORE_BLANK;
	if (VALUE_IS_NUMBER (crit_val)) {
		*fun = criteria_test_equal;
		*test_value = value_dup (crit_val);
		return;
	}

	criteria = value_peek_string (crit_val);
        if (strncmp (criteria, "<=", 2) == 0) {
		*fun = criteria_test_less_or_equal;
		len = 2;
	} else if (strncmp (criteria, ">=", 2) == 0) {
		*fun = criteria_test_greater_or_equal;
		len = 2;
	} else if (strncmp (criteria, "<>", 2) == 0) {
		*fun = (GnmCriteriaFunc) criteria_test_unequal;
		len = 2;
		if (iter_flags)
			*iter_flags = CELL_ITER_ALL;
	} else if (*criteria == '<') {
		*fun = (GnmCriteriaFunc) criteria_test_less;
		len = 1;
	} else if (*criteria == '=') {
		*fun = (GnmCriteriaFunc) criteria_test_equal;
		len = 1;
	} else if (*criteria == '>') {
		*fun = (GnmCriteriaFunc) criteria_test_greater;
		len = 1;
	} else {
		*fun = (GnmCriteriaFunc) criteria_test_equal;
		len = 0;
	}

	*test_value = format_match (criteria + len, NULL, date_conv);
	if (*test_value == NULL)
		*test_value = value_new_string (criteria + len);
}


static GSList *
parse_criteria_range (Sheet *sheet, int b_col, int b_row, int e_col, int e_row,
		      int   *field_ind)
{
	GnmDBCriteria *new_criteria;
	GSList              *criterias = NULL;
	GSList              *conditions;
	GnmCell		    *cell;
	GnmCriteria     *cond;
	GODateConventions const *date_conv = workbook_date_conv (sheet->workbook);

        int i, j;

	for (i = b_row; i <= e_row; i++) {
		new_criteria = g_new (GnmDBCriteria, 1);
		conditions = NULL;

		for (j = b_col; j <= e_col; j++) {
			cell = sheet_cell_get (sheet, j, i);
			if (cell != NULL)
				gnm_cell_eval (cell);
			if (gnm_cell_is_empty (cell))
				continue;

			cond = g_new (GnmCriteria, 1);
			parse_criteria (cell->value, &cond->fun, &cond->x, NULL, date_conv);
			cond->column = (field_ind != NULL) ? field_ind[j - b_col] : j - b_col;
			conditions = g_slist_prepend (conditions, cond);
		}

		new_criteria->conditions = g_slist_reverse (conditions);
		criterias = g_slist_prepend (criterias, new_criteria);
	}

	return g_slist_reverse (criterias);
}

/*
 * Parses the criteria cell range.
 */
GSList *
parse_database_criteria (GnmEvalPos const *ep, GnmValue const *database, GnmValue const *criteria)
{
	Sheet	*sheet;
	GnmCell	*cell;
        int   i;
	int   b_col, b_row, e_col, e_row;
	int   *field_ind;

	g_return_val_if_fail (criteria->type == VALUE_CELLRANGE, NULL);

	sheet = eval_sheet (criteria->v_range.cell.a.sheet, ep->sheet);
	b_col = criteria->v_range.cell.a.col;
	b_row = criteria->v_range.cell.a.row;
	e_col = criteria->v_range.cell.b.col;
	e_row = criteria->v_range.cell.b.row;

	if (e_col < b_col) {
		int tmp = b_col;
		b_col = e_col;
		e_col = tmp;
	}

	/* Find the index numbers for the columns of criterias */
	field_ind = g_alloca (sizeof (int) * (e_col - b_col + 1));
	for (i = b_col; i <= e_col; i++) {
		cell = sheet_cell_get (sheet, i, b_row);
		if (cell == NULL)
			continue;
		gnm_cell_eval (cell);
		if (gnm_cell_is_empty (cell))
			continue;
		field_ind[i - b_col] =
			find_column_of_field (ep, database, cell->value);
		if (field_ind[i - b_col] == -1)
			return NULL;
	}

	return parse_criteria_range (sheet, b_col, b_row + 1,
				     e_col, e_row, field_ind);
}

/* Finds the rows from the given database that match the criteria.
 */
GSList *
find_rows_that_match (Sheet *sheet, int first_col, int first_row,
		      int last_col, int last_row,
		      GSList *criterias, gboolean unique_only)
{
	GSList	     *rows = NULL;
	GSList const *crit_ptr, *cond_ptr;
	int        row;
	gboolean   add_flag;
	char const *t1, *t2;
	GnmCell   *test_cell;
	GnmCriteria const *cond;
	GODateConventions const *date_conv =
		workbook_date_conv (sheet->workbook);

	for (row = first_row; row <= last_row; row++) {
		add_flag = TRUE;
		for (crit_ptr = criterias; crit_ptr != NULL; crit_ptr = crit_ptr->next) {
			add_flag = TRUE;
			for (cond_ptr = ((GnmDBCriteria const *)crit_ptr->data)->conditions;
			     cond_ptr != NULL ; cond_ptr = cond_ptr->next) {
				cond = cond_ptr->data;
				test_cell = sheet_cell_get (sheet, cond->column, row);
				if (test_cell != NULL) {
					gnm_cell_eval (test_cell);
					if (!gnm_cell_is_empty (test_cell) &&
					    !cond->fun (test_cell->value, cond->x, date_conv)) {
						add_flag = FALSE;
						break;
					}
				}
			}

			if (add_flag)
				break;
		}
		if (add_flag) {
			gint *p;

			if (unique_only) {
				GSList *c;
				GnmCell   *cell;
				gint    i, trow;

				for (c = rows; c != NULL; c = c->next) {
					trow = *((gint *) c->data);
					for (i = first_col; i <= last_col; i++) {
						test_cell = sheet_cell_get (sheet, i, trow);
						cell = sheet_cell_get (sheet, i, row);

						/* FIXME: this is probably not right, but crashing is more wrong.  */
						if (test_cell == NULL || cell == NULL)
							continue;

						t1 = cell->value
							? value_peek_string (cell->value)
							: "";
						t2 = test_cell->value
							? value_peek_string (test_cell->value)
							: "";
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

GType
gnm_value_get_type (void)
{
	static GType t = 0;

	if (t == 0)
		t = g_boxed_type_register_static ("GnmValue",
			 (GBoxedCopyFunc)value_dup,
			 (GBoxedFreeFunc)value_release);
	return t;
}
/****************************************************************************/

GnmValueErr const value_terminate_err = { VALUE_ERROR, NULL, NULL };
static GnmValueFloat const the_value_zero = { VALUE_FLOAT, NULL, 0 };
GnmValue const *value_zero = (GnmValue const *)&the_value_zero;

void
value_init (void)
{
	size_t i;

	for (i = 0; i < G_N_ELEMENTS (standard_errors); i++) {
		standard_errors[i].locale_name = _(standard_errors[i].C_name);
		standard_errors[i].locale_name_str =
			gnm_string_get (standard_errors[i].locale_name);
	}

#if USE_VALUE_POOLS
	value_bool_pool =
		go_mem_chunk_new ("value bool pool",
				   sizeof (GnmValueBool),
				   16 * 1024 - 128);

	value_float_pool =
		go_mem_chunk_new ("value float pool",
				   sizeof (GnmValueFloat),
				   16 * 1024 - 128);

	value_error_pool =
		go_mem_chunk_new ("value error pool",
				   sizeof (GnmValueErr),
				   16 * 1024 - 128);

	value_string_pool =
		go_mem_chunk_new ("value string pool",
				   sizeof (GnmValueStr),
				   16 * 1024 - 128);

	value_range_pool =
		go_mem_chunk_new ("value range pool",
				   sizeof (GnmValueRange),
				   16 * 1024 - 128);

	value_array_pool =
		go_mem_chunk_new ("value array pool",
				   sizeof (GnmValueArray),
				   16 * 1024 - 128);
#endif
}

void
value_shutdown (void)
{
	size_t i;

	for (i = 0; i < G_N_ELEMENTS (standard_errors); i++) {
		gnm_string_unref (standard_errors[i].locale_name_str);
		standard_errors[i].locale_name_str = NULL;
	}

#if USE_VALUE_POOLS
	go_mem_chunk_destroy (value_bool_pool, FALSE);
	value_bool_pool = NULL;

	go_mem_chunk_destroy (value_float_pool, FALSE);
	value_float_pool = NULL;

	go_mem_chunk_destroy (value_error_pool, FALSE);
	value_error_pool = NULL;

	go_mem_chunk_destroy (value_string_pool, FALSE);
	value_string_pool = NULL;

	go_mem_chunk_destroy (value_range_pool, FALSE);
	value_range_pool = NULL;

	go_mem_chunk_destroy (value_array_pool, FALSE);
	value_array_pool = NULL;
#else
	if (value_allocations)
		g_printerr ("Leaking %d values.\n", value_allocations);
#endif
}

/****************************************************************************/
