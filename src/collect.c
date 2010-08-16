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
#include "application.h"
#include "value.h"
#include "expr.h"
#include "expr-impl.h"
#include "gnm-datetime.h"
#include "workbook.h"
#include "sheet.h"
#include "ranges.h"
#include "number-match.h"
#include <goffice/goffice.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */

typedef struct {
	/* key */
	GnmValue *value;
	CollectFlags flags;

	/* result */
	int n;
	gnm_float *data;
	GnmValue *error;
} SingleFloatsCacheEntry;

static void
single_floats_cache_entry_free (SingleFloatsCacheEntry *entry)
{
	value_release (entry->value);
	value_release (entry->error);
	g_free (entry->data);
	g_free (entry);
}

static guint
single_floats_cache_entry_hash (const SingleFloatsCacheEntry *entry)
{
	return value_hash (entry->value) ^ (guint)entry->flags;
}

static gboolean
single_floats_cache_entry_equal (const SingleFloatsCacheEntry *a,
				 const SingleFloatsCacheEntry *b)

{
	return (a->flags == b->flags &&
		value_equal (a->value, b->value));
}

static gulong cache_handler;
static GHashTable *single_floats_cache;
static size_t total_cache_size;

static void
clear_caches (void)
{
	if (!cache_handler)
		return;

	g_signal_handler_disconnect (gnm_app_get_app (), cache_handler);
	cache_handler = 0;

	g_hash_table_destroy (single_floats_cache);
	single_floats_cache = NULL;

	total_cache_size = 0;
}

static void
create_caches (void)
{
	if (cache_handler)
		return;

	cache_handler =
		g_signal_connect (gnm_app_get_app (), "recalc-clear-caches",
				  G_CALLBACK (clear_caches), NULL);

	single_floats_cache = g_hash_table_new_full
		((GHashFunc)single_floats_cache_entry_hash,
		 (GEqualFunc)single_floats_cache_entry_equal,
		 (GDestroyNotify)single_floats_cache_entry_free,
		 NULL);

	total_cache_size = 0;
}

static gboolean
cb_prune (gpointer key, gpointer value, gpointer user)
{
	return TRUE;
}

static void
prune_caches (void)
{
	if (total_cache_size > GNM_DEFAULT_ROWS * 10) {
		if (0) g_printerr ("Pruning collect cache from size %ld.\n",
				   (long)total_cache_size);

		total_cache_size = 0;
		g_hash_table_foreach_remove (single_floats_cache,
					     cb_prune,
					     NULL);
	}
}

static SingleFloatsCacheEntry *
get_single_floats_cache_entry (GnmValue const *value, CollectFlags flags)
{
	SingleFloatsCacheEntry key;

	if (flags & (COLLECT_INFO | COLLECT_IGNORE_SUBTOTAL))
		return NULL;

	create_caches ();

	key.value = (GnmValue *)value;
	key.flags = flags;

	return g_hash_table_lookup (single_floats_cache, &key);
}

static SingleFloatsCacheEntry *
get_or_fake_cache_entry (GnmValue const *key, CollectFlags flags,
			 GnmEvalPos const *ep)
{
	SingleFloatsCacheEntry *ce;

	ce = get_single_floats_cache_entry (key, flags);
	if (ce) return ce;

	if (flags & COLLECT_ORDER_IRRELEVANT) {
		ce = get_single_floats_cache_entry (key, flags | COLLECT_SORT);
		if (ce)
			return ce;
	}

	if (flags & COLLECT_SORT) {
		/* FIXME: Try unsorted.  */
	}

	return NULL;
}


static GnmValue *
get_single_cache_key (GnmExpr const *e, GnmEvalPos const *ep)
{
	GnmValue *r, *key;
	GnmSheetRange sr;
	GnmRangeRef const *rr;
	Sheet *end_sheet;
	int h, w;
	const int min_size = 25;

	r = gnm_expr_get_range (e);
	if (!r)
		return NULL;

	rr = value_get_rangeref (r);
	gnm_rangeref_normalize (rr, ep, &sr.sheet, &end_sheet, &sr.range);
	if (sr.sheet != end_sheet) {
		value_release (r);
		return NULL; /* 3D */
	}

	h = range_height (&sr.range);
	w = range_width (&sr.range);
	if (h < min_size && w < min_size && h * w < min_size) {
		value_release (r);
		return NULL;
	}

	key = value_new_cellrange_r (sr.sheet, &sr.range);
	value_release (r);

	return key;
}

/* ------------------------------------------------------------------------- */

static int
float_compare (const void *a_, const void *b_)
{
	gnm_float const *a = a_;
	gnm_float const *b = b_;

        if (*a < *b)
                return -1;
	else if (*a == *b)
		return 0;
	else
		return 1;
}

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
		else if (cl->flags & COLLECT_ZERO_BLANKS)
			x = 0;
		else
			return value_new_error_VALUE (ep);
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
		int *n, GnmValue **error, GSList **info,
		gboolean *constp)
{
	collect_floats_t cl;
	CellIterFlags iter_flags = CELL_ITER_ALL;
	GnmValue *key = NULL;
	CollectFlags keyflags = flags & ~COLLECT_ORDER_IRRELEVANT;

	if (constp)
		*constp = FALSE;

	if (info) {
		*info = NULL;
		g_return_val_if_fail (!(flags & COLLECT_SORT), NULL);
		flags |= COLLECT_INFO;
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

	/* ---------------------------------------- */
	/* Try cache. */

	if (argc == 1 &&
	    (flags & (COLLECT_INFO | COLLECT_IGNORE_SUBTOTAL)) == 0) {
		key = get_single_cache_key (argv[0], ep);
	}
	if (key) {
		SingleFloatsCacheEntry *ce =
			get_or_fake_cache_entry (key, keyflags, ep);
		if (ce) {
			value_release (key);
			if (ce->error) {
				*error = value_dup (ce->error);
				return NULL;
			}
			*n = ce->n;
			if (constp) {
				*constp = TRUE;
				return ce->data;
			}
			return g_memdup (ce->data, *n * sizeof (gnm_float));
		}
	}

	/* ---------------------------------------- */

	*error = function_iterate_argument_values
		(ep, &callback_function_collect, &cl,
		 argc, argv,
		 TRUE, iter_flags);
	if (*error) {
		g_assert (VALUE_IS_ERROR (*error));
		g_free (cl.data);
		cl.data = NULL;
		cl.count = 0;
		g_slist_free (cl.info);
		cl.info = NULL;
	} else {
		if (cl.data == NULL) {
			cl.alloc_count = 1;
			cl.data = g_new (gnm_float, cl.alloc_count);
		}

		if (flags & COLLECT_SORT) {
			qsort (cl.data, cl.count, sizeof (cl.data[0]),
			       float_compare);
		}
	}

	if (info)
		*info = cl.info;
	*n = cl.count;

	if (key) {
		SingleFloatsCacheEntry *ce = g_new (SingleFloatsCacheEntry, 1);
		SingleFloatsCacheEntry *ce2;
		ce->value = key;
		ce->flags = keyflags;
		ce->n = *n;
		ce->error = value_dup (*error);
		if (cl.data == NULL)
			ce->data = NULL;
		else if (constp) {
			*constp = TRUE;
			ce->data = cl.data;
		} else
			ce->data = g_memdup (cl.data, MAX (1, *n) * sizeof (gnm_float));
		prune_caches ();

		/*
		 * We looked for the entry earlier and it was not there.
		 * However, sub-calculation might have added it so be careful
		 * to adjust sizes and replace the not-so-old entry.
		 * See bug 627079.
		 */
		ce2 = g_hash_table_lookup (single_floats_cache, ce);
		if (ce2)
			total_cache_size -= 1 + ce2->n;

		g_hash_table_replace (single_floats_cache, ce, ce);
		total_cache_size += 1 + *n;
	}
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
	return collect_floats (1, argv, ep, flags, n, error, NULL, NULL);
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
	res = collect_floats (1, argv, ep, flags, n, error, info, NULL);

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
	gboolean constp;

	vals = collect_floats (argc, argv, ei->pos, flags, &n, &error,
			       NULL, &constp);
	if (!vals)
		return error;

	err = func (vals, n, &res);
	if (!constp) g_free (vals);

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
gnm_strip_missing (gnm_float *data, int *n, GSList *missing)
{
	unsigned src, dst;

	if (missing == NULL)
		return;

	for (src = dst = 0; (int)dst < *n; src++) {
		if (missing && src == GPOINTER_TO_UINT (missing->data)) {
			missing = missing->next;
			(*n)--;
		} else {
			data[dst] = data[src];
			dst++;
		}
	}
}

/**
 * collect_float_pairs:
 * @v0: value describing first data range
 * @v1: value describing second data range
 * @ep: evaluation position
 * @flags: flags describing how to handle value types
 * @xs0: return location for first data vector
 * @xs1: return location for second data vector
 * @n: return location for number of data points
 *
 * If @n is not positive upon return, no data has been allocated.
 * If @n is negative upon return, the two ranges had different
 * sizes.
 */
GnmValue *
collect_float_pairs (GnmValue const *v0, GnmValue const *v1,
		     GnmEvalPos const *ep, CollectFlags flags,
		     gnm_float **xs0, gnm_float **xs1, int *n)
{
	GSList *missing0 = NULL, *missing1 = NULL;
	GnmValue *error = NULL;
	int n0, n1;

	*xs0 = *xs1 = NULL;
	*n = 0;

	*xs0 = collect_floats_value_with_info (v0, ep, flags,
					       &n0, &missing0, &error);
	if (error)
		goto err;

	*xs1 = collect_floats_value_with_info (v1, ep, flags,
					       &n1, &missing1, &error);
	if (error)
		goto err;

	if (n0 != n1) {
		*n = -1;
		goto err;
	}

	if (missing0 || missing1) {
		missing0 = gnm_slist_sort_merge (missing0, missing1);
		missing1 = NULL;
		gnm_strip_missing (*xs0, &n0, missing0);
		gnm_strip_missing (*xs1, &n1, missing0);
	}

	*n = n0;

err:
	if (*n <= 0) {
		g_free (*xs0); *xs0 = NULL;
		g_free (*xs1); *xs1 = NULL;
	}
	g_slist_free (missing0);
	g_slist_free (missing1);
	return error;
}

GnmValue *
float_range_function2d (GnmValue const *val0, GnmValue const *val1,
			GnmFuncEvalInfo *ei,
			float_range_function2d_t func,
			CollectFlags flags,
			GnmStdError func_error,
			gpointer data)
{
	gnm_float *vals0, *vals1;
	int n;
	GnmValue *res;
	gnm_float fres;

	res = collect_float_pairs (val0, val1, ei->pos, flags,
				   &vals0, &vals1, &n);
	if (res)
		return res;

	if (n <= 0)
		return value_new_error_std (ei->pos, func_error);

	if (func (vals0, vals1, n, &fres, data))
		res = value_new_error_std (ei->pos, func_error);
	else
		res = value_new_float (fres);

	g_free (vals0);
	g_free (vals1);
	return res;
}

GnmValue *
float_range_function2 (GnmValue const *val0, GnmValue const *val1,
		       GnmFuncEvalInfo *ei,
		       float_range_function2_t func,
		       CollectFlags flags,
		       GnmStdError func_error)
{
	return float_range_function2d (val0, val1, ei,
				       (float_range_function2d_t)func,
				       flags,
				       func_error,
				       NULL);
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
