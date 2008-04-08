/*
 * collect.c: Helpers to collect ranges of data.
 *
 * Authors:
 *   Morten Welinder <terra@gnome.org>
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "collect.h"

#include "func.h"
#include "value.h"
#include "expr.h"
#include "expr-impl.h"
#include "gnm-datetime.h"
#include "workbook.h"
#include "sheet.h"
#include "number-match.h"
#include <goffice/utils/go-glib-extras.h>

/* ------------------------------------------------------------------------- */

typedef struct {
	guint alloc_count;
	gnm_float *data;
	guint count;
	CollectFlags flags;
	GSList *info;
	GODateConventions const *date_conv;
} collect_floats_t;

static GnmValue *
callback_function_collect (GnmEvalPos const *ep, GnmValue const *value,
			   void *closure)
{
	gnm_float x = 0;
	collect_floats_t *cl = closure;
	gboolean ignore = FALSE;

	switch (value ? value->type : VALUE_EMPTY) {
	case VALUE_EMPTY:
		if (cl->flags & COLLECT_IGNORE_BLANKS)
			ignore = TRUE;
		break;

	case VALUE_BOOLEAN:
		if (cl->flags & COLLECT_IGNORE_BOOLS)
			ignore = TRUE;
		else if (cl->flags & COLLECT_ZEROONE_BOOLS)
			x = (value->v_bool.val) ? 1. : 0.;
		else
			return value_new_error_VALUE (ep);
		break;

	case VALUE_CELLRANGE :
	case VALUE_ARRAY :
		/* Ranges and arrays are not singleton values treat as errors */

	case VALUE_ERROR:
		if (cl->flags & COLLECT_IGNORE_ERRORS)
			ignore = TRUE;
		else
			return value_new_error_VALUE (ep);
		break;

	case VALUE_FLOAT:
		x = value_get_as_float (value);
		break;

	case VALUE_STRING:
		if (cl->flags & COLLECT_COERCE_STRINGS) {
			GnmValue *vc = format_match_number (value_peek_string (value),
							    NULL,
							    cl->date_conv);
			gboolean bad = !vc || VALUE_IS_BOOLEAN (vc);
			if (vc) {
				x = value_get_as_float (vc);
				value_release (vc);
			} else
				x = 0;

			if (bad)
				return value_new_error_VALUE (ep);
		} else if (cl->flags & COLLECT_IGNORE_STRINGS)
			ignore = TRUE;
		else if (cl->flags & COLLECT_ZERO_STRINGS)
			x = 0;
		else
			return value_new_error_VALUE (ep);
		break;

	default:
		g_warning ("Trouble in callback_function_collect. (%d)",
			   value->type);
		ignore = TRUE;
	}

	if (ignore) {
		if (cl->flags & COLLECT_INFO)
			cl->info = g_slist_prepend (cl->info, GUINT_TO_POINTER (cl->count));
		else {
			return NULL;
		}
	}

	if (cl->count == cl->alloc_count) {
		cl->alloc_count = cl->alloc_count * 2 + 20;
		cl->data = g_renew (gnm_float, cl->data, cl->alloc_count);
	}

	cl->data[cl->count++] = x;
	return NULL;
}

/*
 * collect_floats:
 *
 * exprlist:       List of expressions to evaluate.
 * cr:             Current location (for resolving relative cells).
 * flags:          COLLECT_IGNORE_STRINGS: silently ignore strings.
 *                 COLLECT_COERCE_STRINGS: coerce string into numbers
 *                 COLLECT_ZERO_STRINGS: count strings as 0.
 *                   (Alternative: return #VALUE!.)
 *                 COLLECT_IGNORE_BOOLS: silently ignore bools.
 *                 COLLECT_ZEROONE_BOOLS: count FALSE as 0, TRUE as 1.
 *                   (Alternative: return #VALUE!.)
 *		   COLLECT_IGNORE_SUBTOTAL : ignore expressions that include
 *			the function SUBTOTAL directly and ignore any content
 *			in filtered rows.
 * n:              Output parameter for number of floats.
 *
 * Return value:
 *   NULL in case of strict and a blank.
 *   A copy of the error in the case of strict and an error.
 *   Non-NULL in case of success.  Then n will be set.
 *
 * Evaluate a list of expressions and return the result as an array of
 * gnm_float.
 */
static gnm_float *
collect_floats (int argc, GnmExprConstPtr const *argv,
		GnmEvalPos const *ep, CollectFlags flags,
		int *n, GnmValue **error, GSList **info)
{
	collect_floats_t cl;
	CellIterFlags iter_flags = CELL_ITER_ALL;

	if (info) {
		flags |= COLLECT_INFO;
		*info = NULL;
	} else {
		if (flags & COLLECT_IGNORE_BLANKS)
			iter_flags = CELL_ITER_IGNORE_BLANK;
		flags &= ~COLLECT_INFO;
	}

	if (flags & COLLECT_IGNORE_SUBTOTAL)
		iter_flags |= CELL_ITER_IGNORE_SUBTOTAL;

	cl.alloc_count = 0;
	cl.data = NULL;
	cl.count = 0;
	cl.flags = flags;
	cl.info = NULL;
	cl.date_conv = workbook_date_conv (ep->sheet->workbook);

	*error = function_iterate_argument_values
		(ep, &callback_function_collect, &cl,
		 argc, argv,
		 TRUE, iter_flags);
	if (*error) {
		g_assert (VALUE_IS_ERROR (*error));
		g_free (cl.data);
		g_slist_free (cl.info);
		return NULL;
	}

	if (cl.data == NULL) {
		cl.alloc_count = 1;
		cl.data = g_new (gnm_float, cl.alloc_count);
	}

	if (info)
		*info = cl.info;
	*n = cl.count;
	return cl.data;
}

/* ------------------------------------------------------------------------- */
/* Like collect_floats, but takes a value instead of an expression list.
   Presumably most useful when the value is an array.  */

gnm_float *
collect_floats_value (GnmValue const *val, GnmEvalPos const *ep,
		      CollectFlags flags, int *n, GnmValue **error)
{
	GnmExpr expr_val;
	GnmExprConstPtr argv[1] = { &expr_val };

	gnm_expr_constant_init (&expr_val.constant, val);
	return collect_floats (1, argv, ep, flags, n, error, NULL);
}

/* ------------------------------------------------------------------------- */
/* Like collect_floats_value, but keeps info on missing values */

gnm_float *
collect_floats_value_with_info (GnmValue const *val, GnmEvalPos const *ep,
				CollectFlags flags, int *n, GSList **info,
				GnmValue **error)
{
	GnmExpr expr_val;
	GnmExprConstPtr argv[1] = { &expr_val };
	gnm_float *res;

	gnm_expr_constant_init (&expr_val.constant, val);
	res = collect_floats (1, argv, ep, flags, n, error, info);

	if (info)
		*info = g_slist_reverse (*info);

	return res;
}


/* ------------------------------------------------------------------------- */

GnmValue *
float_range_function (int argc, GnmExprConstPtr const *argv,
		      GnmFuncEvalInfo *ei,
		      float_range_function_t func,
		      CollectFlags flags,
		      GnmStdError func_error)
{
	GnmValue *error = NULL;
	gnm_float *vals, res;
	int n, err;

	vals = collect_floats (argc, argv, ei->pos, flags, &n, &error, NULL);
	if (!vals)
		return error;

	err = func (vals, n, &res);
	g_free (vals);

	if (err)
		return value_new_error_std (ei->pos, func_error);
	else
		return value_new_float (res);
}

/* ------------------------------------------------------------------------- */

/*
 *  gnm_slist_sort_merge:
 *  @list_1: a sorted list of ints with no duplicates
 *  @list_2: another one
 *
 *  gnm_slist_sort_merge returns a new sorted list with all elements
 *  from both @list_1 and @list_2. Duplicates are destroyed. @list1 and @list2
 *  are not anymore valid afterwards since their elements are in the new list
 *  or have been destroyed, in case of duplicates.
 */

GSList *
gnm_slist_sort_merge (GSList *l1,
		      GSList *l2)
{
	GSList list, *l;

	l = &list;

	while (l1 && l2) {
		if (GPOINTER_TO_UINT (l1->data) <= GPOINTER_TO_UINT (l2->data)) {
			if (GPOINTER_TO_UINT (l1->data) == GPOINTER_TO_UINT (l2->data)) {
				/* remove duplicates */
				GSList *m = l2;
				l2 = l2->next;
				m->next = NULL;
				g_slist_free_1 (m);
			}
			l = l->next = l1;
			l1 = l1->next;
		} else {
			l = l->next = l2;
			l2 = l2->next;
		}
	}
	l->next = l1 ? l1 : l2;

	return list.next;
}


/*
 *  gnm_strip_missing:
 *  @data:
 *  @missing:
 *
 */
void
gnm_strip_missing (GArray *data, GSList *missing)
{
	unsigned src, dst;;

	if (missing == NULL)
		return;

	for (src = dst = 0; src < data->len; src++) {
		if (missing && src == GPOINTER_TO_UINT (missing->data))
			missing = missing->next;
		else {
			g_array_index (data, gnm_float, dst) =
				g_array_index (data, gnm_float, src);
			dst++;
		}
	}
	g_array_set_size (data, dst);
}

GnmValue *
float_range_function2 (GnmValue const *val0, GnmValue const *val1,
		       GnmFuncEvalInfo *ei,
		       float_range_function2_t func,
		       CollectFlags flags,
		       GnmStdError func_error)
{
	gnm_float *vals0, *vals1;
	int n0, n1;
	GnmValue *error = NULL;
	GnmValue *res;
	GSList *missing0 = NULL;
	GSList *missing1 = NULL;

	vals0 = collect_floats_value_with_info (val0, ei->pos, flags,
						&n0, &missing0, &error);
	if (error) {
		g_slist_free (missing0);
		return error;
	}

	vals1 = collect_floats_value_with_info (val1, ei->pos, flags,
						&n1, &missing1, &error);
	if (error) {
		g_slist_free (missing0);
		g_slist_free (missing1);
		g_free (vals0);
		return error;
	}

	if (n0 != n1 || n0 == 0)
		res = value_new_error_std (ei->pos, func_error);
	else {
		gnm_float fres;

		if (missing0 || missing1) {
			GSList *missing = gnm_slist_sort_merge (missing0, missing1);
			GArray *gval;
			gval = g_array_new (FALSE, FALSE, sizeof (gnm_float));
			gval = g_array_append_vals (gval, vals0, n0);
			g_free (vals0);
			gnm_strip_missing (gval, missing);
			vals0 = (gnm_float *)gval->data;
			n0 = gval->len;
			g_array_free (gval, FALSE);

			gval = g_array_new (FALSE, FALSE, sizeof (gnm_float));
			gval = g_array_append_vals (gval, vals1, n1);
			g_free (vals1);
			gnm_strip_missing (gval, missing);
			vals1 = (gnm_float *)gval->data;
			n1 = gval->len;
			g_array_free (gval, FALSE);

			g_slist_free (missing);

			if (n0 != n1) {
				g_warning ("This should not happen.  n0=%d n1=%d\n",
					   n0, n1);
			}
		}

		if (func (vals0, vals1, n0, &fres))
			res = value_new_error_std (ei->pos, func_error);
		else
			res = value_new_float (fres);
	}

	g_free (vals0);
	g_free (vals1);
	return res;
}

/* ------------------------------------------------------------------------- */
/* ------------------------------------------------------------------------- */

typedef struct {
	GPtrArray *data;
	CollectFlags flags;
} collect_strings_t;

static GnmValue *
callback_function_collect_strings (GnmEvalPos const *ep, GnmValue const *value,
				   void *closure)
{
	char *text;
	collect_strings_t *cl = closure;

	if (VALUE_IS_EMPTY (value)) {
		if (cl->flags & COLLECT_IGNORE_BLANKS)
			text = NULL;
		else
			text = g_strdup ("");
	} else
		text = value_get_as_string (value);

	if (text)
		g_ptr_array_add (cl->data, text);

	return NULL;
}

static void
collect_strings_free (GPtrArray *data)
{
	g_ptr_array_foreach (data, (GFunc)g_free, NULL);
	g_ptr_array_free (data, TRUE);
}

/*
 * collect_strings:
 *
 * exprlist:       List of expressions to evaluate.
 * ep:             Current location (for resolving relative cells).
 * flags:          0 or COLLECT_IGNORE_BLANKS
 *
 * Return value:
 *   NULL in case of error, error will be set
 *   Non-NULL in case of success.
 *
 * Evaluate a list of expressions and return the result as a GPtrArray of
 * strings.
 */

static GPtrArray *
collect_strings (int argc, GnmExprConstPtr const *argv,
		 GnmEvalPos const *ep, CollectFlags flags,
		 GnmValue **error)
{
	collect_strings_t cl;
	CellIterFlags iter_flags = CELL_ITER_ALL;

	if (flags & COLLECT_IGNORE_BLANKS)
		iter_flags = CELL_ITER_IGNORE_BLANK;

	cl.data = g_ptr_array_new ();
	cl.flags = flags;

	*error = function_iterate_argument_values
		(ep, &callback_function_collect_strings, &cl,
		 argc, argv,
		 TRUE, iter_flags);
	if (*error) {
		g_assert (VALUE_IS_ERROR (*error));
		collect_strings_free (cl.data);
		return NULL;
	}

	return cl.data;
}

GnmValue *
string_range_function (int argc, GnmExprConstPtr const *argv,
		       GnmFuncEvalInfo *ei,
		       string_range_function_t func,
		       CollectFlags flags,
		       GnmStdError func_error)
{
	GnmValue *error = NULL;
	GPtrArray *vals;
	char *res = NULL;
	int err;

	vals = collect_strings (argc, argv, ei->pos, flags, &error);
	if (!vals)
		return error;

	err = func (vals, &res);

	collect_strings_free (vals);

	if (err) {
		g_free (res);
		return value_new_error_std (ei->pos, func_error);
	} else {
		return value_new_string_nocopy (res);
	}
}
