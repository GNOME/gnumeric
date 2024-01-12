/*
 * collect.c: Helpers to collect ranges of data.
 *
 * Authors:
 *   Morten Welinder <terra@gnome.org>
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <collect.h>

#include <func.h>
#include <application.h>
#include <value.h>
#include <expr.h>
#include <expr-impl.h>
#include <gnm-datetime.h>
#include <workbook.h>
#include <sheet.h>
#include <ranges.h>
#include <number-match.h>
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

/* ------------------------------------------------------------------------- */

typedef struct {
	/* key */
	GnmValue *vx;
	GnmValue *vy;
	CollectFlags flags;

	/* result */
	int n;
	gnm_float *data_x;
	gnm_float *data_y;
	GnmValue *error;
} PairsFloatsCacheEntry;

static void
pairs_floats_cache_entry_free (PairsFloatsCacheEntry *entry)
{
	value_release (entry->vx);
	value_release (entry->vy);
	value_release (entry->error);
	g_free (entry->data_x);
	g_free (entry->data_y);
	g_free (entry);
}

static guint
pairs_floats_cache_entry_hash (const PairsFloatsCacheEntry *entry)
{
	/* FIXME: this does not consider the sheet, ie. the same pair of */
	/* ranges on two different sheets yields the same hash value     */
	return value_hash (entry->vx) ^ (value_hash (entry->vy) << 1) ^ (guint)entry->flags;
}

static gboolean
pairs_floats_cache_entry_equal (const PairsFloatsCacheEntry *a,
				 const PairsFloatsCacheEntry *b)

{
	return (a->flags == b->flags &&
		value_equal (a->vx, b->vx) &&
		value_equal (a->vy, b->vy));
}

/* ------------------------------------------------------------------------- */


static gulong cache_handler;
static GHashTable *single_floats_cache;
static GHashTable *pairs_floats_cache;
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
	g_hash_table_destroy (pairs_floats_cache);
	pairs_floats_cache = NULL;

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
	pairs_floats_cache = g_hash_table_new_full
		((GHashFunc)pairs_floats_cache_entry_hash,
		 (GEqualFunc)pairs_floats_cache_entry_equal,
		 (GDestroyNotify)pairs_floats_cache_entry_free,
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
	if (total_cache_size > GNM_DEFAULT_ROWS * 32) {
		if (0) g_printerr ("Pruning collect cache from size %ld.\n",
				   (long)total_cache_size);

		total_cache_size = 0;
		g_hash_table_foreach_remove (single_floats_cache,
					     cb_prune,
					     NULL);
		g_hash_table_foreach_remove (pairs_floats_cache,
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

static PairsFloatsCacheEntry *
get_pairs_floats_cache_entry (GnmValue const *vx, GnmValue const *vy,
			      CollectFlags flags)
{
	PairsFloatsCacheEntry key;

	if (flags & (COLLECT_INFO | COLLECT_IGNORE_SUBTOTAL))
		return NULL;

	create_caches ();

	key.vx = (GnmValue *)vx;
	key.vy = (GnmValue *)vy;
	key.flags = flags;

	return g_hash_table_lookup (pairs_floats_cache, &key);
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

static PairsFloatsCacheEntry *
get_or_fake_pairs_cache_entry (GnmValue const *key_x, GnmValue const *key_y,
			       CollectFlags flags,
			       GnmEvalPos const *ep)
{
	PairsFloatsCacheEntry *ce;

	ce = get_pairs_floats_cache_entry (key_x, key_y, flags);
	if (ce) return ce;

	/* FIXME: we should also try the pairs switched */

	return NULL;
}

static GnmValue *
get_single_cache_key_from_value (GnmValue const *r, GnmEvalPos const *ep)
{
	GnmValue *key;
	GnmSheetRange sr;
	GnmRangeRef const *rr;
	Sheet *end_sheet;
	int h, w;
	const int min_size = 25;

	rr = value_get_rangeref (r);
	gnm_rangeref_normalize (rr, ep, &sr.sheet, &end_sheet, &sr.range);
	if (sr.sheet != end_sheet)
		return NULL; /* 3D */

	h = range_height (&sr.range);
	w = range_width (&sr.range);
	if (h < min_size && w < min_size && h * w < min_size)
		return NULL;

	key = value_new_cellrange_r (sr.sheet, &sr.range);

	return key;
}

static GnmValue *
get_single_cache_key (GnmExpr const *e, GnmEvalPos const *ep)
{
	GnmValue *r = gnm_expr_get_range (e);

	if (r) {
		GnmValue *v = get_single_cache_key_from_value (r, ep);
		value_release (r);
		return v;
	} else
		return NULL;

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

	switch (value ? value->v_any.type : VALUE_EMPTY) {
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
			x = value_get_as_float (value);
		else
			return value_new_error_VALUE (ep);
		break;

	case VALUE_CELLRANGE:
	case VALUE_ARRAY:
		/* Ranges and arrays are not singleton values treat as errors */

	case VALUE_ERROR:
		if (cl->flags & COLLECT_IGNORE_ERRORS)
			ignore = TRUE;
		else if (cl->flags & COLLECT_ZERO_ERRORS)
			x = 0;
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
			   value->v_any.type);
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

/**
 * collect_floats: (skip):
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
 *   %NULL in case of strict and a blank.
 *   A copy of the error in the case of strict and an error.
 *   Non-%NULL in case of success.  Then @n will be set.
 *
 * Evaluate a list of expressions and return the result as an array of
 * gnm_float.
 */
gnm_float *
collect_floats (int argc, GnmExprConstPtr const *argv,
		GnmEvalPos const *ep, CollectFlags flags,
		int *n, GnmValue **error, GSList **info,
		gboolean *constp)
{
	collect_floats_t cl;
	CellIterFlags iter_flags = CELL_ITER_ALL;
	GnmValue *key = NULL;
	CollectFlags keyflags = flags & ~COLLECT_ORDER_IRRELEVANT;
	gboolean strict;

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
			return go_memdup_n (ce->data, *n, sizeof (gnm_float));
		}
	}

	/* ---------------------------------------- */

	if (flags & COLLECT_IGNORE_SUBTOTAL)
		iter_flags |= (CELL_ITER_IGNORE_SUBTOTAL |
			       CELL_ITER_IGNORE_FILTERED);

	strict = (flags & (COLLECT_IGNORE_ERRORS | COLLECT_ZERO_ERRORS)) == 0;

	cl.alloc_count = 0;
	cl.data = NULL;
	cl.count = 0;
	cl.flags = flags;
	cl.info = NULL;
	cl.date_conv = sheet_date_conv (ep->sheet);

	*error = function_iterate_argument_values
		(ep, &callback_function_collect, &cl,
		 argc, argv,
		 strict, iter_flags);
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
			ce->data = go_memdup_n (cl.data, MAX (1, *n), sizeof (gnm_float));
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
/**
 * collect_floats_value_with_info:
 * @val: #GnmValue
 * @ep: #GnmEvalPos
 * @flags: #CollectFlags
 * @n:
 * @info: (element-type guint):
 * @error:
 *
 * Like collect_floats_value, but keeps info on missing values
 **/

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

/**
 * float_range_function:
 * @argc: number of arguments
 * @argv: (in) (array length=argc): function arguments
 * @ei: #GnmFuncEvalInfo describing evaluation context
 * @func: (scope call): implementation function
 * @flags: #CollectFlags flags describing the collection and interpretation
 * of values from @argv.
 * @func_error: A #GnmStdError to use to @func indicates an error.
 *
 * This implements a Gnumeric sheet function that operates on a list of
 * numbers.  This function collects the arguments and uses @func to do
 * the actual computation.
 *
 * Returns: (transfer full): Function result or error value.
 **/
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

/**
 * gnm_slist_sort_merge:
 * @list_1: (element-type guint) (transfer container): a sorted list of
 * unsigned integers with no duplicates.
 * @list_2: (element-type guint) (transfer container): another one
 *
 * gnm_slist_sort_merge merges two lists of unsigned integers.
 *
 * Returns: (element-type guint) (transfer container): the mergedlist.
 **/
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


/**
 * gnm_strip_missing:
 * @data: (inout) (array length=n): Array
 * @n: (inout): Number of elements in @data.
 * @missing: (element-type guint): indices of elements to remove in increasing
 * order.
 *
 * This removes the data elements from @data whose indices are given by
 * @missing.  @n is the number of elements and it updated upon return.
 **/
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

static PairsFloatsCacheEntry *
collect_float_pairs_ce (GnmValue const *vx, GnmValue const *vy,
			GnmEvalPos const *ep, CollectFlags flags)
{
	PairsFloatsCacheEntry *ce = g_new0 (PairsFloatsCacheEntry, 1);
	GSList *missing0 = NULL, *missing1 = NULL;
	int n0, n1;

	ce->flags = flags;

	ce->data_x = collect_floats_value_with_info (vx, ep, flags,
						     &n0, &missing0, &ce->error);
	if (ce->error)
		goto err;

	ce->data_y = collect_floats_value_with_info (vy, ep, flags,
						     &n1, &missing1, &ce->error);

	if (ce->error)
		goto err;

	if (n0 != n1) {
		ce->n = -1;
		goto err;
	}

	if (missing0 || missing1) {
		missing0 = gnm_slist_sort_merge (missing0, missing1);
		missing1 = NULL;
		gnm_strip_missing (ce->data_x, &n0, missing0);
		gnm_strip_missing (ce->data_y, &n1, missing0);
	}
	ce->n = n0;

 err:
	if (ce->n <= 0) {
		g_free (ce->data_x);
		ce->data_x = NULL;
		g_free (ce->data_y);
		ce->data_y = NULL;
	}

	g_slist_free (missing0);
	g_slist_free (missing1);

	return ce;
}

/**
 * collect_float_pairs: (skip)
 * @v0: value describing first data range
 * @v1: value describing second data range
 * @ep: evaluation position
 * @flags: flags describing how to handle value types
 * @xs0: (out) (array length=n): return location for first data vector
 * @xs1: (out) (array length=n): return location for second data vector
 * @n: (out): return location for number of data points
 * @constp: (out) (optional): Return location for a flag describing who own
 * the vectors returned in @xs0 and @xs1.  If present and %TRUE, the
 * resulting data vectors in @xs0 and @xs1 are not owned by the caller.
 * If not-present or %FALSE, the callers owns and must free the result.
 *
 * If @n is not positive upon return, no data has been allocated.
 * If @n is negative upon return, the two ranges had different sizes.
 *
 * Note: introspection cannot handle this functions parameter mix.
 *
 * Returns: (transfer full) (nullable): Error value.
 */
GnmValue *
collect_float_pairs (GnmValue const *vx, GnmValue const *vy,
		     GnmEvalPos const *ep, CollectFlags flags,
		     gnm_float **xs0, gnm_float **xs1, int *n,
		     gboolean *constp)
{
	GnmValue *key_x = NULL;
	GnmValue *key_y = NULL;
	PairsFloatsCacheEntry *ce = NULL;
	gboolean use_cache, free_keys = TRUE;

	if (VALUE_IS_CELLRANGE (vx))
		key_x = get_single_cache_key_from_value (vx, ep);
	if (VALUE_IS_CELLRANGE (vy))
		key_y = get_single_cache_key_from_value (vy, ep);

	if ((use_cache = (key_x && key_y)))
		ce = get_or_fake_pairs_cache_entry (key_x, key_y, flags, ep);

	if (!ce) {
		ce = collect_float_pairs_ce (vx, vy, ep, flags);
		if (use_cache) {
			PairsFloatsCacheEntry *ce2;
			ce->vx = key_x;
			ce->vy = key_y;
			free_keys = FALSE;

			/*
			 * We looked for the entry earlier and it was not there.
			 * However, sub-calculation might have added it so be careful
			 * to adjust sizes and replace the not-so-old entry.
			 * See bug 627079.
			 */
			ce2 = g_hash_table_lookup (pairs_floats_cache, ce);
			if (ce2)
				total_cache_size -= 1 + ce2->n;

			g_hash_table_replace (pairs_floats_cache, ce, ce);
			total_cache_size += 1 + ce->n;
		}
	}

	if (free_keys) {
		value_release (key_x);
		value_release (key_y);
	}

	if (ce == NULL)
		return value_new_error_VALUE (ep);
	else {
		if (ce->error) {
			if (use_cache)
				return value_dup (ce->error);
			else {
				GnmValue *ret = ce->error;
				ce->error = NULL;
				pairs_floats_cache_entry_free (ce);
				return ret;
			}
		}
		*n = ce->n;
		if (ce->n <= 0) {
			if (!use_cache)
				pairs_floats_cache_entry_free (ce);
			*xs0 = NULL;
			*xs1 = NULL;
			if (constp)
				*constp = FALSE;
			return NULL;
		}
		if (use_cache) {
			if (constp) {
				*xs0 = ce->data_x;
				*xs1 = ce->data_y;
				*constp = TRUE;
			} else {
				*xs0 = go_memdup_n (ce->data_x, *n, sizeof (gnm_float));
				*xs1 = go_memdup_n (ce->data_y, *n, sizeof (gnm_float));
			}
		} else {
			if (constp)
				*constp = FALSE;
			*xs0 = ce->data_x;
			*xs1 = ce->data_y;
			ce->data_x = NULL;
			ce->data_y = NULL;
			pairs_floats_cache_entry_free (ce);
		}
		return NULL;
	}
}

/**
 * float_range_function2d:
 * @val0: First range
 * @val1: Second range
 * @ei: #GnmFuncEvalInfo describing evaluation context
 * @func: (scope call): implementation function
 * @flags: #CollectFlags flags describing the collection and interpretation
 * of values from @val0 and @val1.
 * @func_error: A #GnmStdError to use to @func indicates an error.
 * @data: user data for @func
 *
 * This implements a Gnumeric sheet function that operates on a matched
 * pair of ranges.  This function collects the arguments and uses @func to do
 * the actual computation.
 *
 * Returns: (transfer full): Function result or error value.
 **/
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
	gboolean constp = FALSE;

	res = collect_float_pairs (val0, val1, ei->pos, flags,
				   &vals0, &vals1, &n, &constp);
	if (res)
		return res;

	if (n <= 0)
		return value_new_error_std (ei->pos, func_error);

	if (func (vals0, vals1, n, &fres, data))
		res = value_new_error_std (ei->pos, func_error);
	else
		res = value_new_float (fres);

	if (!constp) {
		g_free (vals0);
		g_free (vals1);
	}
	return res;
}

/**
 * float_range_function2:
 * @val0: First range
 * @val1: Second range
 * @ei: #GnmFuncEvalInfo describing evaluation context
 * @func: (scope call): implementation function
 * @flags: #CollectFlags flags describing the collection and interpretation
 * of values from @val0 and @val1.
 * @func_error: A #GnmStdError to use to @func indicates an error.
 *
 * This implements a Gnumeric sheet function that operates on a matched
 * pair of ranges.  This function collects the arguments and uses @func to do
 * the actual computation.
 *
 * Returns: (transfer full): Function result or error value.
 **/
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

/**
 * collect_strings:
 * @argc: number of arguments
 * @argv: (in) (array length=argc): function arguments
 * @ep: Evaluation position
 * @flags: #CollectFlags flags describing the collection and interpretation
 * of values from @argv.
 * @error: (out): Error return value
 *
 * Evaluate a list of expressions and return the result as a #GPtrArray of
 * strings.
 *
 * Returns: (transfer full) (nullable) (element-type utf8): array of strings.
 */
static GPtrArray *
collect_strings (int argc, GnmExprConstPtr const *argv,
		 GnmEvalPos const *ep, CollectFlags flags,
		 GnmValue **error)
{
	collect_strings_t cl;
	CellIterFlags iter_flags = CELL_ITER_ALL;
	gboolean strict;

	/* We don't handle these flags */
	g_return_val_if_fail (!(flags & COLLECT_ZERO_ERRORS), NULL);
	g_return_val_if_fail (!(flags & COLLECT_ZERO_STRINGS), NULL);
	g_return_val_if_fail (!(flags & COLLECT_ZEROONE_BOOLS), NULL);
	g_return_val_if_fail (!(flags & COLLECT_ZERO_BLANKS), NULL);

	if (flags & COLLECT_IGNORE_BLANKS)
		iter_flags = CELL_ITER_IGNORE_BLANK;

	strict = (flags & (COLLECT_IGNORE_ERRORS | COLLECT_ZERO_ERRORS)) == 0;

	cl.data = g_ptr_array_new ();
	cl.flags = flags;

	*error = function_iterate_argument_values
		(ep, &callback_function_collect_strings, &cl,
		 argc, argv,
		 strict, iter_flags);
	if (*error) {
		g_assert (VALUE_IS_ERROR (*error));
		collect_strings_free (cl.data);
		return NULL;
	}

	return cl.data;
}

/**
 * string_range_function:
 * @argc: number of arguments
 * @argv: (in) (array length=argc): function arguments
 * @ei: #GnmFuncEvalInfo describing evaluation context
 * @func: (scope call): implementation function
 * @flags: #CollectFlags flags describing the collection and interpretation
 * of values from @argv.
 * @func_error: A #GnmStdError to use to @func indicates an error.
 *
 * This implements a Gnumeric sheet function that operates on a list of
 * strings.  This function collects the arguments and uses @func to do
 * the actual computation.
 *
 * Returns: (transfer full): Function result or error value.
 **/
GnmValue *
string_range_function (int argc, GnmExprConstPtr const *argv,
		       GnmFuncEvalInfo *ei,
		       string_range_function_t func,
		       gpointer user,
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

	err = func (vals, &res, user);

	collect_strings_free (vals);

	if (err) {
		g_free (res);
		return value_new_error_std (ei->pos, func_error);
	} else {
		return value_new_string_nocopy (res);
	}
}
