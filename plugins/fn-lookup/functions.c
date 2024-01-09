/*
 * Range lookup functions
 *
 * Authors:
 *   Michael Meeks <michael@ximian.com>
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 *   JP Rosevear <jpr@arcavia.com>
 *   Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <dependent.h>
#include <cell.h>
#include <collect.h>
#include <sheet.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <expr-impl.h>
#include <application.h>
#include <expr-name.h>
#include <mathfunc.h>
#include <gutils.h>
#include <workbook.h>
#include <sheet.h>
#include <parse-util.h>
#include <gnm-i18n.h>

#include <goffice/goffice.h>
#include <gnm-plugin.h>

#include <string.h>
#include <stdlib.h>

GNM_PLUGIN_MODULE_HEADER;

/* -------------------------------------------------------------------------- */

typedef struct {
	int index;
	union {
		const char *str;
		gnm_float f;
	} u;
} LookupBisectionCacheItemElem;

typedef struct {
	int n;
	LookupBisectionCacheItemElem *data;
} LookupBisectionCacheItem;

static void
lookup_bisection_cache_item_free (LookupBisectionCacheItem *item)
{
	g_free (item->data);
	g_free (item);
}

static int
bisection_compare_string (const void *a_, const void *b_)
{
	const LookupBisectionCacheItemElem *a = a_;
	const LookupBisectionCacheItemElem *b = b_;
	return g_utf8_collate (a->u.str, b->u.str);
}

static int
bisection_compare_float (const void *a_, const void *b_)
{
	const LookupBisectionCacheItemElem *a = a_;
	const LookupBisectionCacheItemElem *b = b_;
	int res;

	if (a->u.f < b->u.f)
		res = -1;
	else if (a->u.f > b->u.f)
		res = +1;
	else
		res = 0;
	return res;
}

/* -------------------------------------------------------------------------- */

static gboolean debug_lookup_caches;
static GStringChunk *lookup_string_pool;
static GOMemChunk *lookup_float_pool;
static GHashTable *linear_hlookup_string_cache;
static GHashTable *linear_hlookup_float_cache;
static GHashTable *linear_hlookup_bool_cache;
static GHashTable *linear_vlookup_string_cache;
static GHashTable *linear_vlookup_float_cache;
static GHashTable *linear_vlookup_bool_cache;
static GHashTable *bisection_hlookup_string_cache;
static GHashTable *bisection_hlookup_float_cache;
static GHashTable *bisection_hlookup_bool_cache;
static GHashTable *bisection_vlookup_string_cache;
static GHashTable *bisection_vlookup_float_cache;
static GHashTable *bisection_vlookup_bool_cache;
static size_t total_cache_size;
static size_t protect_string_pool;
static size_t protect_float_pool;

static void
clear_caches (void)
{
	if (!linear_hlookup_string_cache)
		return;

	if (debug_lookup_caches)
		g_printerr ("Clearing lookup caches [%ld]\n", (long)total_cache_size);

	total_cache_size = 0;

	/* ---------- */

	g_hash_table_destroy (linear_hlookup_string_cache);
	linear_hlookup_string_cache = NULL;

	g_hash_table_destroy (linear_hlookup_float_cache);
	linear_hlookup_float_cache = NULL;

	g_hash_table_destroy (linear_hlookup_bool_cache);
	linear_hlookup_bool_cache = NULL;

	/* ---------- */

	g_hash_table_destroy (linear_vlookup_string_cache);
	linear_vlookup_string_cache = NULL;

	g_hash_table_destroy (linear_vlookup_float_cache);
	linear_vlookup_float_cache = NULL;

	g_hash_table_destroy (linear_vlookup_bool_cache);
	linear_vlookup_bool_cache = NULL;

	/* ---------- */

	g_hash_table_destroy (bisection_hlookup_string_cache);
	bisection_hlookup_string_cache = NULL;

	g_hash_table_destroy (bisection_hlookup_float_cache);
	bisection_hlookup_float_cache = NULL;

	g_hash_table_destroy (bisection_hlookup_bool_cache);
	bisection_hlookup_bool_cache = NULL;

	/* ---------- */

	g_hash_table_destroy (bisection_vlookup_string_cache);
	bisection_vlookup_string_cache = NULL;

	g_hash_table_destroy (bisection_vlookup_float_cache);
	bisection_vlookup_float_cache = NULL;

	g_hash_table_destroy (bisection_vlookup_bool_cache);
	bisection_vlookup_bool_cache = NULL;

	/* ---------- */

	if (!protect_string_pool) {
		g_string_chunk_free (lookup_string_pool);
		lookup_string_pool = NULL;
	}

	if (!protect_float_pool) {
		go_mem_chunk_destroy (lookup_float_pool, TRUE);
		lookup_float_pool = NULL;
	}
}

static void
create_caches (void)
{
	if (linear_hlookup_string_cache)
		return;

	total_cache_size = 0;

	if (!lookup_string_pool)
		lookup_string_pool = g_string_chunk_new (100 * 1024);

	if (!lookup_float_pool)
		lookup_float_pool =
			go_mem_chunk_new ("lookup float pool",
					  sizeof (gnm_float),
					  sizeof (gnm_float) * 1000);

	linear_hlookup_string_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)g_hash_table_destroy);
	linear_hlookup_float_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)g_hash_table_destroy);
	linear_hlookup_bool_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)g_hash_table_destroy);

	linear_vlookup_string_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)g_hash_table_destroy);
	linear_vlookup_float_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)g_hash_table_destroy);
	linear_vlookup_bool_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)g_hash_table_destroy);

	bisection_hlookup_string_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)lookup_bisection_cache_item_free);
	bisection_hlookup_float_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)lookup_bisection_cache_item_free);
	bisection_hlookup_bool_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)lookup_bisection_cache_item_free);

	bisection_vlookup_string_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)lookup_bisection_cache_item_free);
	bisection_vlookup_float_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)lookup_bisection_cache_item_free);
	bisection_vlookup_bool_cache = g_hash_table_new_full
		((GHashFunc)value_hash,
		 (GEqualFunc)value_equal,
		 (GDestroyNotify)value_release,
		 (GDestroyNotify)lookup_bisection_cache_item_free);
}

static void
prune_caches (void)
{
	if (total_cache_size > 10 * GNM_DEFAULT_ROWS) {
		clear_caches ();
		create_caches ();
	}
}

/* -------------------------------------------------------------------------- */

/*
 * We use an extra level of pointers for "cache" here to avoid problems
 * in the case where we later prune the caches.  The pointer to the
 * GHashTable* will stay valid.
 */
typedef struct {
	gboolean is_new;
	GnmValue *key_copy;
	GHashTable *h, **cache;
} LinearLookupInfo;

static GHashTable *
get_linear_lookup_cache (GnmFuncEvalInfo *ei,
			 GnmValue const *data, GnmValueType datatype,
			 gboolean vertical, LinearLookupInfo *pinfo)
{
	GnmValue const *key;

	pinfo->is_new = FALSE;
	pinfo->key_copy = NULL;

	create_caches ();

	switch (datatype) {
	case VALUE_STRING:
		pinfo->cache = vertical
			? &linear_vlookup_string_cache
			: &linear_hlookup_string_cache;
		break;
	case VALUE_FLOAT:
		pinfo->cache = vertical
			? &linear_vlookup_float_cache
			: &linear_hlookup_float_cache;
		break;
	case VALUE_BOOLEAN:
		pinfo->cache = vertical
			? &linear_vlookup_bool_cache
			: &linear_hlookup_bool_cache;
		break;
	default:
		g_assert_not_reached ();
		return NULL;
	}

	switch (data->v_any.type) {
	case VALUE_CELLRANGE: {
		GnmSheetRange sr;
		GnmRangeRef const *rr = value_get_rangeref (data);
		Sheet *end_sheet;
		gnm_rangeref_normalize (rr, ei->pos, &sr.sheet, &end_sheet,
					&sr.range);
		if (sr.sheet != end_sheet)
			return NULL; /* 3D */

		key = pinfo->key_copy =
			value_new_cellrange_r (sr.sheet, &sr.range);
		break;
	}
	case VALUE_ARRAY:
		key = data;
		break;
	default:
		return NULL;
	}

	pinfo->h = g_hash_table_lookup (*pinfo->cache, key);
	if (pinfo->h == NULL) {
		prune_caches ();
		pinfo->is_new = TRUE;
		if (datatype == VALUE_STRING)
			pinfo->h = g_hash_table_new (g_str_hash, g_str_equal);
		else
			pinfo->h = g_hash_table_new
				((GHashFunc)gnm_float_hash,
				 (GEqualFunc)gnm_float_equal);
		if (!pinfo->key_copy)
			pinfo->key_copy = value_dup (key);
	} else {
		value_release (pinfo->key_copy);
		pinfo->key_copy = NULL;
	}

	return pinfo->h;
}

static void
linear_lookup_cache_commit (LinearLookupInfo *pinfo)
{
	/*
	 * It is possible to have an entry for the key already in the
	 * cache.  In that case we fail to deduct from total_cache_size
	 * but it really doesn't matter.
	 */
	total_cache_size += g_hash_table_size (pinfo->h);

	g_hash_table_replace (*pinfo->cache, pinfo->key_copy, pinfo->h);
}

/*
 * We use an extra level of pointers for "cache" here to avoid problems
 * in the case where we later prune the caches.  The pointer to the
 * GHashTable* will stay valid.
 */
typedef struct {
	gboolean is_new;
	GnmValue *key_copy;
	GHashTable **cache;
	LookupBisectionCacheItem *item;
} BisectionLookupInfo;

static LookupBisectionCacheItem *
get_bisection_lookup_cache (GnmFuncEvalInfo *ei,
			    GnmValue const *data, GnmValueType datatype,
			    gboolean vertical, BisectionLookupInfo *pinfo)
{
	GnmValue const *key;

	pinfo->is_new = FALSE;
	pinfo->key_copy = NULL;

	create_caches ();

	/* The "&" here is for the pruning case.  */
	switch (datatype) {
	case VALUE_STRING:
		pinfo->cache = vertical
			? &bisection_vlookup_string_cache
			: &bisection_hlookup_string_cache;
		break;
	case VALUE_FLOAT:
		pinfo->cache = vertical
			? &bisection_vlookup_float_cache
			: &bisection_hlookup_float_cache;
		break;
	case VALUE_BOOLEAN:
		pinfo->cache = vertical
			? &bisection_vlookup_bool_cache
			: &bisection_hlookup_bool_cache;
		break;
	default:
		g_assert_not_reached ();
		return NULL;
	}

	switch (data->v_any.type) {
	case VALUE_CELLRANGE: {
		GnmSheetRange sr;
		GnmRangeRef const *rr = value_get_rangeref (data);
		Sheet *end_sheet;
		gnm_rangeref_normalize (rr, ei->pos, &sr.sheet, &end_sheet,
					&sr.range);
		if (sr.sheet != end_sheet)
			return NULL; /* 3D */

		key = pinfo->key_copy = value_new_cellrange_r (sr.sheet, &sr.range);
		break;
	}
	case VALUE_ARRAY:
		key = data;
		break;
	default:
		return NULL;
	}

	pinfo->item = g_hash_table_lookup (*pinfo->cache, key);
	if (pinfo->item == NULL) {
		prune_caches ();
		pinfo->is_new = TRUE;
		pinfo->item = g_new0 (LookupBisectionCacheItem, 1);
		if (!pinfo->key_copy)
			pinfo->key_copy = value_dup (key);
	} else {
		value_release (pinfo->key_copy);
		pinfo->key_copy = NULL;
	}

	return pinfo->item;
}

static void
bisection_lookup_cache_commit (BisectionLookupInfo *pinfo)
{
	/*
	 * It is possible to have an entry for the key already in the
	 * cache.  In that case we fail to deduct from total_cache_size
	 * but it really doesn't matter.
	 */
	total_cache_size += pinfo->item->n;

	g_hash_table_replace (*pinfo->cache, pinfo->key_copy, pinfo->item);
}


/* -------------------------------------------------------------------------- */

static gboolean
find_type_valid (GnmValue const *find)
{
	/* Excel does not lookup errors or blanks */
	if (VALUE_IS_EMPTY (find))
		return FALSE;
	return VALUE_IS_NUMBER (find) || VALUE_IS_STRING (find);
}

static gboolean
find_compare_type_valid (GnmValue const *find, GnmValue const *val)
{
	if (!val)
		return FALSE;

	if (find->v_any.type == val->v_any.type)
		return TRUE;

	/* Note: floats do not match bools.  */

	return FALSE;
}

/* -------------------------------------------------------------------------- */

static gboolean
is_pattern_match (const char *s)
{
	while (*s) {
		if (*s == '*' || *s == '?' || *s == '~')
			return TRUE;
		s++;
	}
	return FALSE;
}

static int
calc_length (GnmValue const *data, GnmEvalPos const *ep, gboolean vertical)
{
	if (vertical)
		return value_area_get_height (data, ep);
	else
		return value_area_get_width (data, ep);
}

static const GnmValue *
get_elem (GnmValue const *data, guint ui,
	  GnmEvalPos const *ep, gboolean vertical)
{
	if (vertical)
		return value_area_get_x_y (data, 0, ui, ep);
	else
		return value_area_get_x_y (data, ui, 0, ep);
}

enum { LOOKUP_NOT_THERE = -1, LOOKUP_DATA_ERROR = -2 };


static int
find_index_linear_equal_string (GnmFuncEvalInfo *ei,
				GnmValue const *find, GnmValue const *data,
				gboolean vertical)
{
	GHashTable *h;
	gpointer pres;
	char *sc;
	gboolean found;
	LinearLookupInfo info;

	h = get_linear_lookup_cache (ei, data, VALUE_STRING, vertical,
				     &info);
	if (!h)
		return LOOKUP_DATA_ERROR;

	if (info.is_new) {
		int lp, length = calc_length (data, ei->pos, vertical);

		protect_string_pool++;

		for (lp = 0; lp < length; lp++) {
			GnmValue const *v = get_elem (data, lp, ei->pos, vertical);
			char *vc;

			if (!find_compare_type_valid (find, v))
				continue;

			vc = g_utf8_casefold (value_peek_string (v), -1);
			if (!g_hash_table_lookup_extended (h, vc, NULL, NULL)) {
				char *sc = g_string_chunk_insert (lookup_string_pool, vc);
				g_hash_table_insert (h, sc, GINT_TO_POINTER (lp));
			}

			g_free (vc);
		}

		linear_lookup_cache_commit (&info);

		protect_string_pool--;
	}

	sc = g_utf8_casefold (value_peek_string (find), -1);
	found = g_hash_table_lookup_extended (h, sc, NULL, &pres);
	g_free (sc);

	return found ? GPOINTER_TO_INT (pres) : LOOKUP_NOT_THERE;
}

static int
find_index_linear_equal_float (GnmFuncEvalInfo *ei,
			       GnmValue const *find, GnmValue const *data,
			       gboolean vertical)
{
	GHashTable *h;
	gpointer pres;
	gnm_float f;
	gboolean found;
	LinearLookupInfo info;

	/* This handles floats and bools, but with separate caches.  */
	h = get_linear_lookup_cache (ei, data, find->v_any.type, vertical,
				     &info);
	if (!h)
		return LOOKUP_DATA_ERROR;

	if (info.is_new) {
		int lp, length = calc_length (data, ei->pos, vertical);

		protect_float_pool++;

		for (lp = 0; lp < length; lp++) {
			GnmValue const *v = get_elem (data, lp, ei->pos, vertical);
			gnm_float f2;

			if (!find_compare_type_valid (find, v))
				continue;

			f2 = value_get_as_float (v);

			if (!g_hash_table_lookup_extended (h, &f2, NULL, NULL)) {
				gnm_float *fp = go_mem_chunk_alloc (lookup_float_pool);
				*fp = f2;
				g_hash_table_insert (h, fp, GINT_TO_POINTER (lp));
			}
		}

		linear_lookup_cache_commit (&info);

		protect_float_pool--;
	}

	f = value_get_as_float (find);
	found = g_hash_table_lookup_extended (h, &f, NULL, &pres);

	return found ? GPOINTER_TO_INT (pres) : LOOKUP_NOT_THERE;
}

static int
find_index_linear (GnmFuncEvalInfo *ei,
		   GnmValue const *find, GnmValue const *data,
		   gboolean vertical)
{
	if (VALUE_IS_STRING (find))
		return find_index_linear_equal_string
			(ei, find, data, vertical);

	if (VALUE_IS_NUMBER (find))
		return find_index_linear_equal_float
			(ei, find, data, vertical);

	/* I don't think we can get here.  */
	return LOOKUP_DATA_ERROR;
}


static int
wildcard_string_match (const char *key, LookupBisectionCacheItem *bc)
{
	GORegexp rx;
	GORegmatch rm;
	int i, res = LOOKUP_NOT_THERE;

	/* FIXME: Do we want to anchor at the end here? */
	if (gnm_regcomp_XL (&rx, key, GO_REG_ICASE, TRUE, TRUE) != GO_REG_OK) {
		g_warning ("Unexpected regcomp result");
		return LOOKUP_DATA_ERROR;
	}

	for (i = 0; i < bc->n; i++) {
		if (go_regexec (&rx, bc->data[i].u.str, 1, &rm, 0) == GO_REG_OK) {
			res = bc->data[i].index;
			break;
		}
	}

	go_regfree (&rx);
	return res;
}

#undef DEBUG_BISECTION

static int
find_index_bisection (GnmFuncEvalInfo *ei,
		      GnmValue const *find, GnmValue const *data,
		      gint type, gboolean vertical)
{
	int high, low, lastlow, res;
	LookupBisectionCacheItem *bc;
	gboolean stringp;
	int (*comparer) (const void *,const void *);
	LookupBisectionCacheItemElem key;
	BisectionLookupInfo info;

	bc = get_bisection_lookup_cache (ei, data, find->v_any.type, vertical,
					 &info);
	if (!bc)
		return LOOKUP_DATA_ERROR;

	stringp = VALUE_IS_STRING (find);
	comparer = stringp ? bisection_compare_string : bisection_compare_float;

	if (info.is_new) {
		int lp, length = calc_length (data, ei->pos, vertical);

		bc->data = g_new (LookupBisectionCacheItemElem, length + 1);

		if (stringp)
			protect_string_pool++;

		for (lp = 0; lp < length; lp++) {
			GnmValue const *v = get_elem (data, lp, ei->pos, vertical);
			if (!find_compare_type_valid (find, v))
				continue;

			if (stringp) {
				char *vc = g_utf8_casefold (value_peek_string (v), -1);
				bc->data[bc->n].u.str = g_string_chunk_insert (lookup_string_pool, vc);
				g_free (vc);
			} else
				bc->data[bc->n].u.f = value_get_as_float (v);

			bc->data[bc->n].index = lp;
			bc->n++;
		}

		bc->data = g_renew (LookupBisectionCacheItemElem,
				    bc->data,
				    bc->n);
		bisection_lookup_cache_commit (&info);

		if (stringp)
			protect_string_pool--;
	}

#ifdef DEBUG_BISECTION
	g_printerr ("find=%s\n", value_peek_string (find));
#endif

	if (type == 0)
		return wildcard_string_match (value_peek_string (find), bc);

	if (stringp) {
		char *vc = g_utf8_casefold (value_peek_string (find), -1);
		key.u.str = g_string_chunk_insert (lookup_string_pool, vc);
		g_free (vc);
	} else {
#ifdef DEBUG_BISECTION
		int lp;
		for (lp = 0; lp < bc->n; lp++) {
			g_printerr ("Data %d: %g\n", lp, bc->data[lp].u.f);
		}
#endif
		key.u.f = value_get_as_float (find);
	}

	lastlow = LOOKUP_NOT_THERE;
	low = 0;
	high = bc->n - 1;
	while (low <= high) {
		int mid = (low + high) / 2;
		int c = comparer (&key, bc->data + mid);
#ifdef DEBUG_BISECTION
		if (!stringp) {
			g_printerr ("Comparing to #%d %g: %d\n",
				    mid, bc->data[mid].u.f, c);
		}
#endif
		if (c == 0) {
			/*
			 * For some sick reason, XLS wants to take the first
			 * or last match, depending on type.  We could put
			 * this into the cache if we wanted to.
			 */
			int dir = (type > 0 ? +1 : -1);
			while (mid + dir > 0 &&
			       mid + dir < bc->n &&
			       comparer (&key, bc->data + (mid + dir)) == 0)
				mid += dir;
			return bc->data[mid].index;
		}
		if (type < 0)
			c = -c; /* Reverse sorted data.  */
		if (c > 0) {
			lastlow = mid;
			low = mid + 1;
		} else {
			high = mid - 1;
		}
	}

	if (type == 0)
		res = LOOKUP_NOT_THERE;
	else
		res = lastlow;

#ifdef DEBUG_BISECTION
	g_printerr ("res=%d\n", res);
#endif
	if (res >= 0)
		res = bc->data[res].index;
#ifdef DEBUG_BISECTION
	g_printerr ("   index=%d\n", res);
#endif

	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_address[] = {
	{ GNM_FUNC_HELP_NAME, F_("ADDRESS:cell address as text")},
        { GNM_FUNC_HELP_ARG, F_("row_num:row number")},
        { GNM_FUNC_HELP_ARG, F_("col_num:column number")},
        { GNM_FUNC_HELP_ARG, F_("abs_num:1 for an absolute, 2 for a row absolute and column "
				"relative, 3 for a row relative and column absolute, "
				"and 4 for a relative reference; defaults to 1")},
        { GNM_FUNC_HELP_ARG, F_("a1:if TRUE, an A1-style reference is provided, "
				"otherwise an R1C1-style reference; defaults to TRUE")},
        { GNM_FUNC_HELP_ARG, F_("text:name of the worksheet, defaults to no sheet")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{row_num} or @{col_num} is less than one, ADDRESS returns "
				 "#VALUE!")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{abs_num} is greater than 4 ADDRESS returns #VALUE!")},
        { GNM_FUNC_HELP_EXAMPLES, "=ADDRESS(5,4)" },
        { GNM_FUNC_HELP_EXAMPLES, "=ADDRESS(5,4,4)" },
        { GNM_FUNC_HELP_EXAMPLES, "=ADDRESS(5,4,4,FALSE)" },
        { GNM_FUNC_HELP_EXAMPLES, "=ADDRESS(5,4,4,FALSE,\"Sheet99\")" },
        { GNM_FUNC_HELP_SEEALSO, "COLUMNNUMBER"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_address (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	GnmConventionsOut out;
	GnmCellRef	 ref;
	GnmParsePos	 pp;
	gboolean	 err;
	int		 col, row;
	Sheet            *sheet = NULL;
	const char       *sheet_name = args[4] ? value_peek_string (args[4]) : NULL;

	switch (args[2] ? value_get_as_int (args[2]) : 1) {
	case 1: case 5: ref.col_relative = ref.row_relative = FALSE; break;
	case 2: case 6:
		ref.col_relative = TRUE;
		ref.row_relative = FALSE;
		break;
	case 3: case 7:
		ref.col_relative = FALSE;
		ref.row_relative = TRUE;
		break;
	case 4: case 8: ref.col_relative = ref.row_relative = TRUE; break;

	default :
		return value_new_error_VALUE (ei->pos);
	}

	if (sheet_name)
		sheet = workbook_sheet_by_name (ei->pos->sheet->workbook,
						sheet_name);
	/* For unknown or missing sheet, use current sheet.  */
	if (!sheet)
		sheet = ei->pos->sheet;

	ref.sheet = NULL;
	row = ref.row = value_get_as_int (args[0]) - 1;
	col = ref.col = value_get_as_int (args[1]) - 1;
	out.pp = parse_pos_init_evalpos (&pp, ei->pos);
	out.convs = gnm_conventions_default;

	if (NULL != args[3]) {
		/* MS Excel is ridiculous.  This is a special case */
		if (!value_get_as_bool (args[3], &err)) {
			out.convs = gnm_conventions_xls_r1c1;
			if (ref.col_relative)
				col = ei->pos->eval.col + (++ref.col);
			if (ref.row_relative)
				row = ei->pos->eval.row + (++ref.row);
		}
		if (err)
		        return value_new_error_VALUE (ei->pos);
	}

	if (col < 0 || col >= gnm_sheet_get_max_cols (sheet))
		return value_new_error_VALUE (ei->pos);
	if (row < 0 || row >= gnm_sheet_get_max_rows (sheet))
		return value_new_error_VALUE (ei->pos);

	if (!out.convs->r1c1_addresses)
		pp.eval.col = pp.eval.row = 0;

	if (sheet_name && sheet_name[0]) {
		out.accum = gnm_expr_conv_quote (out.convs, sheet_name);
		g_string_append_c (out.accum, out.convs->sheet_name_sep);
	} else if (sheet_name) {
		/* A crazy case.  Invalid name, but ends up unquoted.  */
		out.accum = g_string_new (NULL);
		g_string_append_c (out.accum, out.convs->sheet_name_sep);
	} else
		out.accum = g_string_new (NULL);
	cellref_as_string (&out, &ref, TRUE);

	return value_new_string_nocopy (g_string_free (out.accum, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_areas[] = {
	{ GNM_FUNC_HELP_NAME, F_("AREAS:number of areas in @{reference}")},
        { GNM_FUNC_HELP_ARG, F_("reference:range")},
        { GNM_FUNC_HELP_EXAMPLES, "=AREAS(A1,B2,C3)" },
        { GNM_FUNC_HELP_SEEALSO, "ADDRESS,INDEX,INDIRECT,OFFSET"},
        { GNM_FUNC_HELP_END}
};

/* TODO : we need to rethink EXPR_SET as an operator vs a value type */
static GnmValue *
gnumeric_areas (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmExpr const *expr;
	int res = -1;

	if (argc != 1 || argv[0] == NULL)
		return value_new_error_VALUE (ei->pos);
	expr = argv[0];

 restart:
	switch (GNM_EXPR_GET_OPER (expr)) {
	case GNM_EXPR_OP_CONSTANT:
		if (VALUE_IS_ERROR (expr->constant.value))
			return value_dup (expr->constant.value);
		if (!VALUE_IS_CELLRANGE (expr->constant.value))
			break;

	case GNM_EXPR_OP_CELLREF:
	case GNM_EXPR_OP_RANGE_CTOR:
	case GNM_EXPR_OP_INTERSECT:
		res = 1;
		break;

	case GNM_EXPR_OP_FUNCALL: {
		GnmValue *v = gnm_expr_eval (expr, ei->pos,
			GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
		if (VALUE_IS_CELLRANGE (v))
			res = 1;
		value_release (v);
		break;
	}

	case GNM_EXPR_OP_NAME:
		if (expr_name_is_active (expr->name.name)) {
			expr = expr->name.name->texpr->expr;
			goto restart;
		}
		break;

	case GNM_EXPR_OP_SET:
		res = expr->set.argc;
		break;

	case GNM_EXPR_OP_PAREN:
		expr = expr->unary.value;
		goto restart;

	default:
		break;
	}

	if (res > 0)
		return value_new_int (res);
	return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_choose[] = {
	{ GNM_FUNC_HELP_NAME, F_("CHOOSE:the (@{index}+1)th argument")},
        { GNM_FUNC_HELP_ARG, F_("index:positive number")},
        { GNM_FUNC_HELP_ARG, F_("value1:first value")},
        { GNM_FUNC_HELP_ARG, F_("value2:second value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("CHOOSE returns its (@{index}+1)th argument.")},
	{ GNM_FUNC_HELP_NOTE, F_("@{index} is truncated to an integer. If @{index} < 1 "
				 "or the truncated @{index} > number of values, CHOOSE "
				 "returns #VALUE!")},
        { GNM_FUNC_HELP_EXAMPLES, "=CHOOSE(3,\"Apple\",\"Orange\",\"Grape\",\"Perry\")" },
        { GNM_FUNC_HELP_SEEALSO, "IF"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_choose (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	int     index;
	GnmValue  *v;
	int i;

	if (argc < 1)
		return value_new_error_VALUE (ei->pos);

#warning TODO add array eval
	v = gnm_expr_eval (argv[0], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (!v)
		return NULL;

	if (!VALUE_IS_FLOAT (v)) {
		value_release (v);
		return value_new_error_VALUE (ei->pos);
	}

	index = value_get_as_int (v);
	value_release (v);
	for (i = 1; i < argc; i++) {
		index--;
		if (!index)
			return gnm_expr_eval (argv[i], ei->pos,
					      GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
	}
	return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_vlookup[] = {
	{ GNM_FUNC_HELP_NAME, F_("VLOOKUP:search the first column of @{range} for @{value}")},
        { GNM_FUNC_HELP_ARG, F_("value:search value")},
        { GNM_FUNC_HELP_ARG, F_("range:range to search")},
        { GNM_FUNC_HELP_ARG, F_("column:1-based column offset indicating the return values")},
        { GNM_FUNC_HELP_ARG, F_("approximate:if false, an exact match of @{value} "
				"must be found; defaults to TRUE")},
        { GNM_FUNC_HELP_ARG, F_("as_index:if true, the 0-based row offset is "
				"returned; defaults to FALSE")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("VLOOKUP function finds the row in @{range} that has a first "
					"cell similar to @{value}.  If @{approximate} is not true it "
					"finds the row with an exact equality. If @{approximate} is "
					"true, it finds the last row with first value less than "
					"or equal to "
					"@{value}. If @{as_index} is true the 0-based row offset "
					"is returned.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{approximate} is true, "
				 "then the values must be sorted in order of ascending value.")},
	{ GNM_FUNC_HELP_NOTE, F_("VLOOKUP returns #REF! if @{column} falls outside @{range}.")},
        { GNM_FUNC_HELP_SEEALSO, "HLOOKUP"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_vlookup (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	GnmValue const *find = args[0];
	int col_idx = value_get_as_int (args[2]);
	gboolean approx = args[3] ? value_get_as_checked_bool (args[3]) : TRUE;
	gboolean as_index = args[4] && value_get_as_checked_bool (args[4]);
	int index;
	gboolean is_string_match;

	if (!find_type_valid (find))
		return value_new_error_NA (ei->pos);
	if (col_idx <= 0)
		return value_new_error_VALUE (ei->pos);
	if (col_idx > value_area_get_width (args[1], ei->pos))
		return value_new_error_REF (ei->pos);

	is_string_match = (!approx &&
			   VALUE_IS_STRING (find) &&
			   is_pattern_match (value_peek_string (find)));

	index = is_string_match
		? find_index_bisection (ei, find, args[1], 0, TRUE)
		: (approx
		   ? find_index_bisection (ei, find, args[1], 1, TRUE)
		   : find_index_linear (ei, find, args[1], TRUE));
	if (index == LOOKUP_DATA_ERROR)
		return value_new_error_VALUE (ei->pos);  /* 3D */

	if (as_index)
		return value_new_int (index);

	if (index >= 0) {
	        GnmValue const *v;

		v = value_area_fetch_x_y (args[1], col_idx-1, index, ei->pos);
		g_return_val_if_fail (v != NULL, NULL);
		return value_dup (v);
	}

	return value_new_error_NA (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_hlookup[] = {
	{ GNM_FUNC_HELP_NAME, F_("HLOOKUP:search the first row of @{range} for @{value}")},
        { GNM_FUNC_HELP_ARG, F_("value:search value")},
        { GNM_FUNC_HELP_ARG, F_("range:range to search")},
        { GNM_FUNC_HELP_ARG, F_("row:1-based row offset indicating the return values ")},
        { GNM_FUNC_HELP_ARG, F_("approximate:if false, an exact match of @{value} "
				"must be found; defaults to TRUE")},
        { GNM_FUNC_HELP_ARG, F_("as_index:if true, the 0-based column offset is "
				"returned; defaults to FALSE")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("HLOOKUP function finds the row in @{range} that has a first "
					"cell similar to @{value}.  If @{approximate} is not true it "
					"finds the column with an exact equality. If @{approximate} is "
					"true, it finds the last column with first value less than or "
					"equal to "
					"@{value}. If @{as_index} is true the 0-based column offset "
					"is returned.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{approximate} is true, "
				 "then the values must be sorted in order of ascending value.")},
	{ GNM_FUNC_HELP_NOTE, F_("HLOOKUP returns #REF! if @{row} falls outside @{range}.")},
        { GNM_FUNC_HELP_SEEALSO, "VLOOKUP"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_hlookup (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	GnmValue const *find = args[0];
	int row_idx = value_get_as_int (args[2]);
	gboolean approx = args[3] ? value_get_as_checked_bool (args[3]) : TRUE;
	gboolean as_index = args[4] && value_get_as_checked_bool (args[4]);
	int index;
	gboolean is_string_match;

	if (!find_type_valid (find))
		return value_new_error_NA (ei->pos);
	if (row_idx <= 0)
		return value_new_error_VALUE (ei->pos);
	if (row_idx > value_area_get_height (args[1], ei->pos))
		return value_new_error_REF (ei->pos);

	is_string_match = (!approx &&
			   VALUE_IS_STRING (find) &&
			   is_pattern_match (value_peek_string (find)));

	index = is_string_match
		? find_index_bisection (ei, find, args[1], 0, FALSE)
		: (approx
		   ? find_index_bisection (ei, find, args[1], 1, FALSE)
		   : find_index_linear (ei, find, args[1], FALSE));
	if (index == LOOKUP_DATA_ERROR)
		return value_new_error_VALUE (ei->pos);  /* 3D */

	if (as_index)
		return value_new_int (index);

	if (index >= 0) {
	        GnmValue const *v;

		v = value_area_fetch_x_y (args[1], index, row_idx-1, ei->pos);
		g_return_val_if_fail (v != NULL, NULL);
		return value_dup (v);
	}

	return value_new_error_NA (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_lookup[] = {
	{ GNM_FUNC_HELP_NAME, F_("LOOKUP:contents of @{vector2} at the corresponding location to "
				 "@{value} in @{vector1}")},
        { GNM_FUNC_HELP_ARG, F_("value:value to look up")},
        { GNM_FUNC_HELP_ARG, F_("vector1:range to search:")},
        { GNM_FUNC_HELP_ARG, F_("vector2:range of return values")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("If  @{vector1} has more rows than columns, LOOKUP searches "
					"the first row of @{vector1}, otherwise the first column. "
					"If @{vector2} is omitted the return value is taken from "
					"the last row or column of @{vector1}.")},
	{ GNM_FUNC_HELP_NOTE, F_("If LOOKUP can't find @{value} it uses the largest value less "
				 "than @{value}.")},
	{ GNM_FUNC_HELP_NOTE, F_("The data must be sorted.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{value} is smaller than the first value it returns #N/A.")},
	{ GNM_FUNC_HELP_NOTE, F_("If the corresponding location does not exist in @{vector2}, "
				 "it returns #N/A.")},
        { GNM_FUNC_HELP_SEEALSO, "VLOOKUP,HLOOKUP"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_lookup (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int index = -1;
	GnmValue const *v = args[0];
	GnmValue const *area = args[1];
	GnmValue const *lookup = args[2];
	GnmValue *result, *xlookup = NULL;
	gboolean vertical_search = (value_area_get_width (area, ei->pos) <
				    value_area_get_height (area, ei->pos));
	gboolean vertical_lookup;
	gboolean is_cellrange;

	if (!find_type_valid (v))
		return value_new_error_NA (ei->pos);

	if (lookup) {
		int width = value_area_get_width (lookup, ei->pos);
		int height = value_area_get_height (lookup, ei->pos);

		if (width > 1 && height > 1) {
			return value_new_error_NA (ei->pos);
		}

		vertical_lookup = (width < height);
		is_cellrange = VALUE_IS_CELLRANGE (lookup);
#if 0
		if (is_cellrange) {
			GnmRange r;
			/*
			 * Extend the lookup range all the way.  This is utterly
			 * insane, but Excel really does reach beyond the stated
			 * range.
			 */
			range_init_value (&r, lookup, &ei->pos->eval);
			range_normalize (&r);
			if (vertical_lookup)
				r.end.row = gnm_sheet_get_last_row (ei->pos->sheet);
			else
				r.end.col = gnm_sheet_get_last_col (ei->pos->sheet);

			lookup = xlookup = value_new_cellrange_r (ei->pos->sheet, &r);
		}
#endif
	} else {
		lookup = area;
		vertical_lookup = vertical_search;
		is_cellrange = FALSE;  /* Doesn't matter.  */
	}

	index = find_index_bisection (ei, v, area, 1, vertical_search);

	if (index >= 0) {
		int width = value_area_get_width (lookup, ei->pos);
		int height = value_area_get_height (lookup, ei->pos);
		int x, y;

		if (vertical_lookup)
			x = width - 1, y = index;
		else
			x = index, y = height - 1;

		if (x < width && y < height)
			result = value_dup (value_area_fetch_x_y (lookup, x, y, ei->pos));
		else if (is_cellrange)
			/* We went off the sheet.  */
			result = value_new_int (0);
		else
			/* We went outside an array.  */
			result = value_new_error_NA (ei->pos);
	} else
		result = value_new_error_NA (ei->pos);

	value_release (xlookup);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_match[] = {
	{ GNM_FUNC_HELP_NAME, F_("MATCH:the index of @{seek} in @{vector}")},
        { GNM_FUNC_HELP_ARG, F_("seek:value to find")},
        { GNM_FUNC_HELP_ARG, F_("vector:n by 1 or 1 by n range to be searched")},
        { GNM_FUNC_HELP_ARG, F_("type:+1 (the default) to find the largest value \xe2\x89\xa4 @{seek}, "
				"0 to find the first value = @{seek}, or "
				"-1 to find the smallest value \xe2\x89\xa5 @{seek}")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("MATCH searches @{vector} for @{seek} and returns the 1-based index.")},
	{ GNM_FUNC_HELP_NOTE, F_("For @{type} = -1 the data must be sorted in descending order; "
				 "for @{type} = +1 the data must be sorted in ascending order.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{seek} could not be found, #N/A is returned.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{vector} is neither n by 1 nor 1 by n, #N/A is returned.")},
        { GNM_FUNC_HELP_SEEALSO, "LOOKUP"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_match (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int type, index = -1;
	int width = value_area_get_width (args[1], ei->pos);
	int height = value_area_get_height (args[1], ei->pos);
	gboolean vertical;
	GnmValue const *find = args[0];
	gboolean is_string_match;

	if (!find_type_valid (find))
		return value_new_error_NA (ei->pos);

	if (width > 1 && height > 1)
		return value_new_error_NA (ei->pos);
	vertical = (width > 1 ? FALSE : TRUE);

	type = VALUE_IS_EMPTY (args[2]) ? 1 : value_get_as_int (args[2]);

	is_string_match = (type == 0 &&
			   VALUE_IS_STRING (find) &&
			   is_pattern_match (value_peek_string (find)));

	if (type == 0 && !is_string_match)
		index = find_index_linear (ei, find, args[1], vertical);
	else
		index = find_index_bisection (ei, find, args[1], type,
					      vertical);

	switch (index) {
	case LOOKUP_DATA_ERROR: return value_new_error_VALUE (ei->pos);
	case LOOKUP_NOT_THERE: return value_new_error_NA (ei->pos);
	default: return value_new_int (index + 1);
	}
}

/***************************************************************************/

static GnmFuncHelp const help_indirect[] = {
	{ GNM_FUNC_HELP_NAME, F_("INDIRECT:contents of the cell pointed to by the @{ref_text} string")},
        { GNM_FUNC_HELP_ARG, F_("ref_text:textual reference")},
        { GNM_FUNC_HELP_ARG, F_("format:if true, @{ref_text} is given in A1-style, "
				"otherwise it is given in R1C1 style; defaults to true")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{ref_text} is not a valid reference in the style determined "
				 "by @{format}, INDIRECT returns #REF!")},
        { GNM_FUNC_HELP_EXAMPLES, "If A1 contains 3.14 and A2 contains \"A1\", then\n"
	   "INDIRECT(A2) equals 3.14." },
        { GNM_FUNC_HELP_SEEALSO, "AREAS,INDEX,CELL"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_indirect (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	GnmParsePos  pp;
	GnmValue *res = NULL;
	GnmExprTop const *texpr;
	char const *text = value_peek_string (args[0]);
	GnmConventions const *convs = gnm_conventions_default;

	if (args[1] && !value_get_as_checked_bool (args[1]))
		convs = gnm_conventions_xls_r1c1;

	texpr = gnm_expr_parse_str (text,
		parse_pos_init_evalpos (&pp, ei->pos),
		GNM_EXPR_PARSE_DEFAULT, convs, NULL);

	if (texpr != NULL) {
		res = gnm_expr_top_get_range (texpr);
		gnm_expr_top_unref (texpr);
	}
	return (res != NULL) ? res : value_new_error_REF (ei->pos);
}

/*****************************************************************************/

static GnmFuncHelp const help_index[] = {
	{ GNM_FUNC_HELP_NAME, F_("INDEX:reference to a cell in the given @{array}")},
        { GNM_FUNC_HELP_ARG, F_("array:cell or inline array")},
        { GNM_FUNC_HELP_ARG, F_("row:desired row, defaults to 1")},
        { GNM_FUNC_HELP_ARG, F_("col:desired column, defaults to 1")},
        { GNM_FUNC_HELP_ARG, F_("area:from which area to select a cell, defaults to 1")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("INDEX gives a reference to a cell "
					"in the given @{array}. "
					"The cell is selected by @{row} and @{col}, "
					"which count the rows and "
					"columns in the array.")},
	{ GNM_FUNC_HELP_NOTE, F_("If the reference falls outside the range of @{array},"
				 " INDEX returns #REF!")},
        { GNM_FUNC_HELP_EXAMPLES, F_("Let us assume that the cells A1, A2, ..., A5"
				     " contain numbers 11.4, "
				     "17.3, 21.3, 25.9, and 40.1. Then INDEX(A1:A5,4,1,1) equals 25.9") },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_index (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmExpr const *source;
	int elem[3] = { 0, 0, 0 };
	int i = 0;
	gboolean valid;
	GnmValue *v, *res;

	if (argc == 0 || argc > 4)
		return value_new_error_VALUE (ei->pos);
	source = argv[0];

	/* This is crazy.  */
	while (GNM_EXPR_GET_OPER (source) == GNM_EXPR_OP_PAREN)
		source = source->unary.value;

	v = gnm_expr_eval (source, ei->pos, GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
	if (VALUE_IS_ERROR (v))
		return v;

	for (i = 0; i + 1 < argc && i < (int)G_N_ELEMENTS (elem); i++) {
		GnmValue *vi = value_coerce_to_number (
			gnm_expr_eval (argv[i + 1], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY),
			&valid, ei->pos);
		if (!valid) {
			value_release (v);
			return vi;
		}
		elem[i] = value_get_as_int (vi) - 1;
		value_release (vi);
	}

	if (GNM_EXPR_GET_OPER (source) == GNM_EXPR_OP_SET) {
		int w = value_area_get_height (v, ei->pos);
		int i = elem[2];
		GnmValue *vi;
		if (i < 0 || i >= w) {
			value_release (v);
			return value_new_error_REF (ei->pos);
		}
		vi = value_dup (value_area_fetch_x_y (v, 0, i, ei->pos));
		value_release (v);
		v = vi;
	} else if (elem[2] != 0) {
		value_release (v);
		return value_new_error_REF (ei->pos);
	}

	if (elem[1] < 0 ||
	    elem[1] >= value_area_get_width (v, ei->pos) ||
	    elem[0] < 0 ||
	    elem[0] >= value_area_get_height (v, ei->pos)) {
		value_release (v);
		return value_new_error_REF (ei->pos);
	}

#warning Work out a way to fall back to returning value when a reference is unneeded
	if (VALUE_IS_CELLRANGE (v)) {
		GnmRangeRef const *src = &v->v_range.cell;
		GnmCellRef a = src->a, b = src->b;
		Sheet *start_sheet, *end_sheet;
		GnmRange r;

		gnm_rangeref_normalize (src, ei->pos, &start_sheet, &end_sheet, &r);
		r.start.row += elem[0];
		r.start.col += elem[1];
		a.row = r.start.row; if (a.row_relative) a.row -= ei->pos->eval.row;
		b.row = r.start.row; if (b.row_relative) b.row -= ei->pos->eval.row;
		a.col = r.start.col; if (a.col_relative) a.col -= ei->pos->eval.col;
		b.col = r.start.col; if (b.col_relative) b.col -= ei->pos->eval.col;
		res = value_new_cellrange_unsafe (&a, &b);
	} else if (VALUE_IS_ARRAY (v))
		res = value_dup (value_area_fetch_x_y (v, elem[1], elem[0], ei->pos));
	else
		res = value_new_error_REF (ei->pos);
	value_release (v);
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_column[] = {
	{ GNM_FUNC_HELP_NAME, F_("COLUMN:vector of column numbers") },
	{ GNM_FUNC_HELP_ARG, F_("x:reference, defaults to the position of the current expression") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("COLUMN function returns a Nx1 array containing the sequence "
					"of integers "
					"from the first column to the last column of @{x}.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} is neither an array nor a reference nor a range, "
				 "returns #VALUE!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=COLUMN(A1:C4)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=COLUMN(A:C)" },
	{ GNM_FUNC_HELP_EXAMPLES, F_("column() in G13 equals 7.") },
	{ GNM_FUNC_HELP_SEEALSO, "COLUMNS,ROW,ROWS" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_column (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int col, width, i;
	GnmValue *res;
	GnmValue const *ref = args[0];

	if (ref == NULL) {
		col   = ei->pos->eval.col + 1; /* user visible counts from 0 */
		if (eval_pos_is_array_context (ei->pos))
			gnm_expr_top_get_array_size (ei->pos->array_texpr, &width, NULL);
		else
			return value_new_int (col);
	} else if (VALUE_IS_CELLRANGE (ref)) {
		Sheet    *tmp;
		GnmRange  r;

		gnm_rangeref_normalize (&ref->v_range.cell, ei->pos, &tmp, &tmp, &r);
		col    = r.start.col + 1;
		width  = range_width (&r);
	} else
		return value_new_error_VALUE (ei->pos);

	if (width == 1)
		return value_new_int (col);

	res = value_new_array (width, 1);
	for (i = width; i-- > 0 ; )
		value_array_set (res, i, 0, value_new_int (col + i));
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_columnnumber[] = {
	{ GNM_FUNC_HELP_NAME, F_("COLUMNNUMBER:column number for the given column called @{name}")},
        { GNM_FUNC_HELP_ARG, F_("name:column name such as \"IV\"")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{name} is invalid, COLUMNNUMBER returns #VALUE!")},
        { GNM_FUNC_HELP_EXAMPLES, "=COLUMNNUMBER(\"E\")" },
        { GNM_FUNC_HELP_SEEALSO, "ADDRESS"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_columnnumber (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	char const *name = value_peek_string (args[0]);
	int colno;
	unsigned char relative;
	char const *after = col_parse (name,
				       gnm_sheet_get_size (ei->pos->sheet),
				       &colno, &relative);

	if (after == NULL || *after)
		return value_new_error_VALUE (ei->pos);

	return value_new_int (colno + 1);
}

/***************************************************************************/

static GnmFuncHelp const help_columns[] = {
	{ GNM_FUNC_HELP_NAME, F_("COLUMNS:number of columns in @{reference}")},
        { GNM_FUNC_HELP_ARG, F_("reference:array or area")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{reference} is neither an array nor a reference nor a range, "
				 "COLUMNS returns #VALUE!")},
        { GNM_FUNC_HELP_EXAMPLES, "=COLUMNS(H2:J3)" },
        { GNM_FUNC_HELP_SEEALSO, "COLUMN,ROW,ROWS"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_columns (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	return value_new_int (value_area_get_width (args[0], ei->pos));
}

/***************************************************************************/

static GnmFuncHelp const help_offset[] = {
	{ GNM_FUNC_HELP_NAME, F_("OFFSET:an offset cell range")},
        { GNM_FUNC_HELP_ARG, F_("range:reference or range")},
        { GNM_FUNC_HELP_ARG, F_("row:number of rows to offset @{range}")},
        { GNM_FUNC_HELP_ARG, F_("col:number of columns to offset @{range}")},
        { GNM_FUNC_HELP_ARG, F_("height:height of the offset range, defaults to height of @{range}")},
        { GNM_FUNC_HELP_ARG, F_("width:width of the offset range, defaults to width of @{range}")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OFFSET returns the cell range starting at offset "
					"(@{row},@{col}) from @{range} of height @{height} and "
					"width @{width}.")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{range} is neither a reference nor a range, OFFSET "
				 "returns #VALUE!")},
        { GNM_FUNC_HELP_SEEALSO, "COLUMN,COLUMNS,ROWS,INDEX,INDIRECT,ADDRESS"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_offset (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int tmp;
	int row_offset, col_offset;

	/* Copy the references so we can change them */
	GnmCellRef a = args[0]->v_range.cell.a;
	GnmCellRef b = args[0]->v_range.cell.b;

	row_offset = value_get_as_int (args[1]);
	col_offset = value_get_as_int (args[2]);
	a.row     += row_offset;
	a.col     += col_offset;
	if (a.row < 0 || a.col < 0 ||
	    a.row >= gnm_sheet_get_max_rows (ei->pos->sheet) || a.col >= gnm_sheet_get_max_cols (ei->pos->sheet))
		return value_new_error_REF (ei->pos);

	if (args[3] != NULL) {
		tmp = value_get_as_int (args[3]);
		if (tmp < 1)
			return value_new_error_VALUE (ei->pos);
		b.row = a.row + tmp - 1;
	} else
		b.row += row_offset;
	if (b.col < 0 || b.row >= gnm_sheet_get_max_rows (ei->pos->sheet))
		return value_new_error_REF (ei->pos);
	if (args[4] != NULL) {
		tmp = value_get_as_int (args[4]);
		if (tmp < 1)
			return value_new_error_VALUE (ei->pos);
		b.col = a.col + tmp - 1;
	} else
		b.col += col_offset;
	if (b.col < 0 || b.col >= gnm_sheet_get_max_cols (ei->pos->sheet))
		return value_new_error_REF (ei->pos);

	return value_new_cellrange_unsafe (&a, &b);
}

/***************************************************************************/

static GnmFuncHelp const help_row[] = {
	{ GNM_FUNC_HELP_NAME, F_("ROW:vector of row numbers") },
	{ GNM_FUNC_HELP_ARG, F_("x:reference, defaults to the position of the current expression") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ROW function returns a 1xN array containing the "
					"sequence of integers "
					"from the first row to the last row of @{x}.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{x} is neither an array nor a reference nor a range, "
				 "returns #VALUE!") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ROW(A1:D3)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=ROW(1:3)" },
	{ GNM_FUNC_HELP_SEEALSO, "COLUMN,COLUMNS,ROWS" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_row (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	int row, n, i;
	GnmValue *res;
	GnmValue const *ref = args[0];

	if (ref == NULL) {
		row   = ei->pos->eval.row + 1; /* user visible counts from 0 */
		if (eval_pos_is_array_context (ei->pos))
			gnm_expr_top_get_array_size (ei->pos->array_texpr, NULL, &n);
		else
			return value_new_int (row);
	} else if (VALUE_IS_CELLRANGE (ref)) {
		Sheet    *tmp;
		GnmRange  r;

		gnm_rangeref_normalize (&ref->v_range.cell, ei->pos, &tmp, &tmp, &r);
		row    = r.start.row + 1;
		n = range_height (&r);
	} else
		return value_new_error_VALUE (ei->pos);

	if (n == 1)
		return value_new_int (row);

	res = value_new_array (1, n);
	for (i = n ; i-- > 0 ; )
		value_array_set (res, 0, i, value_new_int (row + i));
	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_rows[] = {
	{ GNM_FUNC_HELP_NAME, F_("ROWS:number of rows in @{reference}")},
        { GNM_FUNC_HELP_ARG, F_("reference:array, reference, or range")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{reference} is neither an array nor a reference nor a range, "
				 "ROWS returns #VALUE!")},
        { GNM_FUNC_HELP_EXAMPLES, "=ROWS(H7:I13)" },
        { GNM_FUNC_HELP_SEEALSO, "COLUMN,COLUMNS,ROW"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_rows (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	return value_new_int (value_area_get_height (args[0], ei->pos));
}

/***************************************************************************/

static GnmFuncHelp const help_sheets[] = {
	{ GNM_FUNC_HELP_NAME, F_("SHEETS:number of sheets in @{reference}")},
        { GNM_FUNC_HELP_ARG, F_("reference:array, reference, or range, defaults to the maximum range")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{reference} is neither an array nor a reference nor a range, "
				 "SHEETS returns #VALUE!")},
        { GNM_FUNC_HELP_EXAMPLES, "=SHEETS()" },
        { GNM_FUNC_HELP_SEEALSO, "COLUMNS,ROWS"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_sheets (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	Workbook const *wb = ei->pos->sheet->workbook;
	GnmValue const *v = args[0];

	if(v) {
		if (VALUE_IS_CELLRANGE (v)) {
			GnmRangeRef const *r = &v->v_range.cell;
			int ans_min, ans_max, a, b;

			a = r->a.sheet ? r->a.sheet->index_in_wb : -1;
			b = r->b.sheet ? r->b.sheet->index_in_wb : -1;

			ans_min = MIN (a,b);
			ans_max = MAX (a,b);

			if (ans_min == -1)
				return value_new_int (1);

			return value_new_int (ans_max - ans_min + 1);
		} else
			return value_new_int (1);
	} else
		return value_new_int (workbook_sheet_count (wb));
}

/***************************************************************************/
static GnmFuncHelp const help_sheet[] = {
	{ GNM_FUNC_HELP_NAME, F_("SHEET:sheet number of @{reference}")},
        { GNM_FUNC_HELP_ARG, F_("reference:reference or literal sheet name, defaults to the current sheet")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{reference} is neither a reference "
				 "nor a literal sheet name, "
				 "SHEET returns #VALUE!")},
        { GNM_FUNC_HELP_EXAMPLES, "=SHEET()" },
        { GNM_FUNC_HELP_EXAMPLES, "=SHEET(\"Sheet1\")" },
        { GNM_FUNC_HELP_SEEALSO, "SHEETS,ROW,COLUMNNUMBER"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_sheet (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	Workbook const *wb = ei->pos->sheet->workbook;
	GnmValue const *v = args[0];
	int n;

	if(v) {
		if (VALUE_IS_CELLRANGE (v)) {
			GnmRangeRef const *r = &v->v_range.cell;
			int a, b;

			a = r->a.sheet ? r->a.sheet->index_in_wb : -1;
			b = r->b.sheet ? r->b.sheet->index_in_wb : -1;

			if (a == -1 && b == -1)
				n = ei->pos->sheet->index_in_wb;
			else if (a == b || a * b < 0)
				n = MAX (a,b);
			else
				return value_new_error_NUM (ei->pos);
		} else if (VALUE_IS_STRING (v)) {
			Sheet *sheet = workbook_sheet_by_name
				(wb, value_peek_string (v));
			if (!sheet)
				return value_new_error_NUM (ei->pos);
			n = sheet->index_in_wb;
		} else
			return value_new_error_VALUE (ei->pos);
	} else
		n = ei->pos->sheet->index_in_wb;

	return value_new_int (1 + n);
}
/***************************************************************************/

static GnmFuncHelp const help_hyperlink[] = {
	{ GNM_FUNC_HELP_NAME, F_("HYPERLINK:second or first arguments")},
        { GNM_FUNC_HELP_ARG, F_("link_location:string")},
        { GNM_FUNC_HELP_ARG, F_("label:string, optional")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("HYPERLINK function currently returns its 2nd argument, "
					"or if that is omitted the 1st argument.")},
        { GNM_FUNC_HELP_EXAMPLES, "=HYPERLINK(\"www.gnome.org\",\"GNOME\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=HYPERLINK(\"www.gnome.org\")" },
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_hyperlink (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	GnmValue const * v = args[1];
	if (v == NULL)
		v = args[0];
	return value_dup (v);
}

/***************************************************************************/

static GnmFuncHelp const help_transpose[] = {
	{ GNM_FUNC_HELP_NAME, F_("TRANSPOSE:the transpose of @{matrix}")},
        { GNM_FUNC_HELP_ARG, F_("matrix:range")},
        { GNM_FUNC_HELP_SEEALSO, "FLIP,MMULT"},
        { GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_transpose (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmEvalPos const * const ep = ei->pos;
        GnmValue const * const matrix = argv[0];
	int	r, c;
        GnmValue *res;

	int const cols = value_area_get_width (matrix, ep);
	int const rows = value_area_get_height (matrix, ep);

	/* Return the value directly for a singleton */
	if (rows == 1 && cols == 1)
		return value_dup (value_area_get_x_y (matrix, 0, 0, ep));

	/* REMEMBER this is a transpose */
	res = value_new_array_non_init (rows, cols);

	for (r = 0; r < rows; ++r) {
		res->v_array.vals [r] = g_new (GnmValue *, cols);
		for (c = 0; c < cols; ++c)
			res->v_array.vals[r][c] = value_dup(
				value_area_get_x_y (matrix, c, r, ep));
	}

	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_flip[] = {
	{ GNM_FUNC_HELP_NAME, F_("FLIP:@{matrix} flipped")},
        { GNM_FUNC_HELP_ARG, F_("matrix:range")},
        { GNM_FUNC_HELP_ARG, F_("vertical:if true, @{matrix} is flipped vertically, "
				"otherwise horizontally; defaults to TRUE")},
        { GNM_FUNC_HELP_SEEALSO, "TRANSPOSE"},
        { GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_flip (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmEvalPos const * const ep = ei->pos;
        GnmValue const * const matrix = argv[0];
	int	r, c;
        GnmValue *res;
	gboolean vertical = argv[1] ? value_get_as_checked_bool (argv[1]) : TRUE;

	int const cols = value_area_get_width (matrix, ep);
	int const rows = value_area_get_height (matrix, ep);

	/* Return the value directly for a singleton */
	if (rows == 1 && cols == 1)
		return value_dup (value_area_get_x_y (matrix, 0, 0, ep));

	res = value_new_array_non_init (cols, rows);

	if (vertical)
		for (c = 0; c < cols; ++c) {
			res->v_array.vals [c] = g_new (GnmValue *, rows);
			for (r = 0; r < rows; ++r)
				res->v_array.vals[c][rows - r - 1] = value_dup
					(value_area_get_x_y (matrix, c, r, ep));
		}
	else
		for (c = 0; c < cols; ++c) {
			res->v_array.vals [c] = g_new (GnmValue *, rows);
			for (r = 0; r < rows; ++r)
				res->v_array.vals[c][r] = value_dup
					(value_area_get_x_y (matrix, cols - c - 1, r, ep));
		}

	return res;
}

/***************************************************************************/
static GnmFuncHelp const help_array[] = {
        { GNM_FUNC_HELP_NAME, F_("ARRAY:vertical array of the arguments")},
        { GNM_FUNC_HELP_ARG, F_("v:value")},
        { GNM_FUNC_HELP_SEEALSO, "TRANSPOSE"},
        { GNM_FUNC_HELP_END}
};


static GnmValue *
callback_function_array (GnmEvalPos const *ep, GnmValue const *value, void *closure)
{
	GSList **list = closure;

	*list = g_slist_prepend
		(*list, value ? value_dup (value) : value_new_empty ());
	return NULL;
}

static GnmValue *
gnumeric_array (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GSList *list = NULL, *l;
	int len, i;
	GnmValue *val = function_iterate_argument_values
		(ei->pos, callback_function_array, &list,
		 argc, argv, FALSE, CELL_ITER_ALL);

	if (val != NULL) {
		g_slist_free_full (list, (GDestroyNotify)value_release);
		return val;
	}
	list = g_slist_reverse (list);
	len = g_slist_length (list);

	if (len == 0) {
		g_slist_free_full (list, (GDestroyNotify)value_release);
		return value_new_error_VALUE (ei->pos);
	}

	if (len == 1) {
		val = list->data;
		g_slist_free (list);
		return val;
	}

	val = value_new_array_empty (1, len);

	for (l = list, i = 0; l != NULL; l = l->next, i++)
		val->v_array.vals[0][i] = l->data;

	g_slist_free (list);
	return val;
}


/***************************************************************************/

static GnmFuncHelp const help_sort[] = {
	{ GNM_FUNC_HELP_NAME, F_("SORT:sorted list of numbers as vertical array")},
	{ GNM_FUNC_HELP_ARG, F_("ref:list of numbers")},
	{ GNM_FUNC_HELP_ARG, F_("order:0 (descending order) or 1 (ascending order); defaults to 0")},
	{ GNM_FUNC_HELP_NOTE, F_("Strings, booleans, and empty cells are ignored.")},
	{ GNM_FUNC_HELP_EXAMPLES, F_("SORT({4,3,5}) evaluates to {5,4,3}")},
	{ GNM_FUNC_HELP_SEEALSO, ("ARRAY")},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_sort (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float *xs;
	int i, j, n;
	GnmValue *result = NULL;

	xs = collect_floats_value (argv[0], ei->pos,
				   COLLECT_IGNORE_STRINGS |
				   COLLECT_IGNORE_BOOLS |
				   COLLECT_IGNORE_BLANKS |
				   COLLECT_SORT,
				   &n, &result);
	if (result)
		goto out;

	switch (argv[1] ? value_get_as_int (argv[1]) : 0) {
	case 0:
		result = value_new_array_empty (1, n);

		for (i = 0, j = n - 1; i < n; i++, j--)
			result->v_array.vals[0][i] = value_new_float (xs[j]);
		break;
	case 1:
		result = value_new_array_empty (1, n);

		for (i = 0; i < n; i++)
			result->v_array.vals[0][i] = value_new_float (xs[i]);
		break;
	default:
		result = value_new_error_VALUE (ei->pos);
		break;
	}

 out:
	g_free (xs);

	return result;
}

/***************************************************************************/

static GnmFuncHelp const help_unique[] = {
	{ GNM_FUNC_HELP_NAME, F_("UNIQUE:unique values in a range or array")},
	{ GNM_FUNC_HELP_ARG, F_("data:range or array")},
	{ GNM_FUNC_HELP_ARG, F_("by_col:by column if TRUE, by row otherwise; defaults to FALSE")},
	{ GNM_FUNC_HELP_ARG, F_("exactly_once:suppress values present multiple times; defaults to FALSE")},
	{ GNM_FUNC_HELP_SEEALSO, ("SORT")},
	{ GNM_FUNC_HELP_END }
};

static gsize
hash_col_row (GnmValue const *data, GnmEvalPos const * const ep,
	      gboolean by_col, int i)
{
	int size = by_col ? value_area_get_height (data, ep) : value_area_get_width (data, ep);
	int j;
	gsize h = 0;

	for (j = 0; j < size; j++) {
		GnmValue const *v = by_col
			? value_area_get_x_y (data, i, j, ep)
			: value_area_get_x_y (data, j, i, ep);
		gsize h1;
		if (value_type_of (v) == VALUE_STRING) {
			char *s = g_utf8_casefold (value_peek_string (v), -1);
			h1 = g_str_hash (s);
			g_free (s);
		} else
			h1 = value_hash (v);

		h ^= h1;
		h ^= h >> 23;
		h *= 0x2127599bf4325c37ULL;
	}
	return h;
}

static GnmValDiff
compare_col_row (GnmValue const *data, GnmEvalPos const * const ep,
		 gboolean by_col, int i1, int i2)
{
	int size = by_col ? value_area_get_height (data, ep) : value_area_get_width (data, ep);
	int j;

	for (j = 0; j < size; j++) {
		GnmValue const *v1 = by_col
			? value_area_get_x_y (data, i1, j, ep)
			: value_area_get_x_y (data, j, i1, ep);
		GnmValueType t1 = value_type_of (v1);
		GnmValue const *v2 = by_col
			? value_area_get_x_y (data, i2, j, ep)
			: value_area_get_x_y (data, j, i2, ep);
		GnmValueType t2 = value_type_of (v2);

		return t1 == t2
			? value_compare (v1, v2, FALSE)
			: (t1 < t2 ? IS_LESS : IS_GREATER);
	}

	return IS_EQUAL;
}

static GnmValue *
gnumeric_unique (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmEvalPos const * const ep = ei->pos;
        GnmValue const * const data = argv[0];
	gboolean by_col = argv[1] ? value_get_as_checked_bool (argv[1]) : FALSE;
	gboolean exactly_once = argv[2] ? value_get_as_checked_bool (argv[2]) : FALSE;
	int i, sx, sy, x, y, count, rcount;
	guint8 *keep;
	GnmValue *res;
	GHashTable *seen;

	sx = value_area_get_width (data, ep);
	sy = value_area_get_height (data, ep);
	count = by_col ? sx : sy;

	keep = g_new0 (guint8, count);
	seen = g_hash_table_new_full (g_direct_hash, g_direct_equal,
				      NULL, (GDestroyNotify)g_slist_free);
	for (i = 0; i < count; i++) {
		gsize h = hash_col_row (data, ep, by_col, i);
		GSList *l, *items = g_hash_table_lookup (seen, GSIZE_TO_POINTER (h));
		gboolean found = FALSE;

		for (l = items; l; l = l->next) {
			int i2 = GPOINTER_TO_INT (l->data);
			if (compare_col_row (data, ep, by_col, i, i2) == IS_EQUAL) {
				found = TRUE;
				if (exactly_once)
					keep[i2] = 2;
				break;
			}
		}
		if (!found) {
			keep[i] = 1;
			if (items)
				items->next = g_slist_prepend (items->next, GINT_TO_POINTER (i));
			else
				g_hash_table_insert (seen, GSIZE_TO_POINTER (h),
						     g_slist_prepend (NULL, GINT_TO_POINTER (i)));
		}
	}

	rcount = 0;
	for (i = 0; i < count; i++) {
		if (exactly_once && keep[i] > 1) keep[i] = 0;
		rcount += keep[i];
	}

	if (rcount == 0)
		res = value_new_error_VALUE (ep);
	else if (by_col) {
		res = value_new_array_empty (rcount, sy);
		for (i = x = 0; x < sx; x++) {
			if (!keep[x])
				continue;
			for (y = 0; y < sy; y++) {
				GnmValue const *v = value_area_get_x_y (data, x, y, ep);
				res->v_array.vals[i][y] = value_dup (v);
			}
			i++;
		}
	} else {
		res = value_new_array_empty (sx, rcount);
		for (i = y = 0; y < sy; y++) {
			if (!keep[y])
				continue;
			for (x = 0; x < sx; x++) {
				GnmValue const *v = value_area_get_x_y (data, x, y, ep);
				res->v_array.vals[x][i] = value_dup (v);
			}
			i++;
		}
	}

	g_hash_table_destroy (seen);

	g_free (keep);
	return res;
}

/***************************************************************************/

GnmFuncDescriptor const lookup_functions[] = {
	{ "address",   "ff|fbs",
	  help_address,  gnumeric_address, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "areas", NULL,
	  help_areas,	NULL,	gnumeric_areas,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "choose", NULL,
	  help_choose,	NULL,	gnumeric_choose,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "column",     "|A",
	  help_column,   gnumeric_column, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "columnnumber", "s",
	  help_columnnumber, gnumeric_columnnumber, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "columns",   "A",
	  help_columns, gnumeric_columns, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hlookup",   "EAf|bb",
	  help_hlookup, gnumeric_hlookup, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "hyperlink", "S|S",
	  help_hyperlink, gnumeric_hyperlink, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_SUBSET, GNM_FUNC_TEST_STATUS_BASIC },
	{ "indirect",  "s|b",
	  help_indirect, gnumeric_indirect, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "index",     "A|fff",
	  help_index,    NULL, gnumeric_index,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "lookup",    "EA|r",
	  help_lookup,   gnumeric_lookup, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "match",     "EA|f",
	  help_match,    gnumeric_match, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "offset",    "rff|ff",
	  help_offset,   gnumeric_offset, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "row",       "|A",
	  help_row,      gnumeric_row, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "rows",      "A",
	  help_rows,     gnumeric_rows, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "sheets",      "|A",
	  help_sheets,     gnumeric_sheets, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "sheet",      "|?",
	  help_sheet,     gnumeric_sheet, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "sort",         "r|f",
	  help_sort, gnumeric_sort, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_SUBSET, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "transpose", "A",
	  help_transpose, gnumeric_transpose, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "unique",       "A|bb",
	  help_unique, gnumeric_unique, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_SUBSET, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "vlookup",   "EAf|bb",
	  help_vlookup, gnumeric_vlookup, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "array", NULL,
	  help_array, NULL, gnumeric_array,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "flip", "A|b",
	  help_flip, gnumeric_flip, NULL,
	  GNM_FUNC_RETURNS_NON_SCALAR, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },

        {NULL}
};

G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	debug_lookup_caches = gnm_debug_flag ("lookup-caches");
	g_signal_connect (gnm_app_get_app (), "recalc-clear-caches",
			  G_CALLBACK (clear_caches), NULL);
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	g_signal_handlers_disconnect_by_func (gnm_app_get_app (),
					      G_CALLBACK (clear_caches), NULL);

	if (protect_string_pool) {
		g_printerr ("Imbalance in string pool: %d\n", (int)protect_string_pool);
		protect_string_pool = 0;
	}
	if (protect_float_pool) {
		g_printerr ("Imbalance in float pool: %d\n", (int)protect_float_pool);
		protect_float_pool = 0;
	}

	clear_caches ();
}
