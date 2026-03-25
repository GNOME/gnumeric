/*
 * sort.c : Routines for sorting cell ranges
 *
 * Author:
 *	JP Rosevear <jpr@arcavia.com>
 *
 * (C) 2000 JP Rosevear
 * (C) 2000 Morten Welinder
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <sort.h>

#include <commands.h>
#include <clipboard.h>
#include <cell.h>
#include <value.h>
#include <sheet.h>
#include <ranges.h>
#include <goffice/goffice.h>
#include <stdlib.h>

typedef struct {
	GObjectClass parent_class;
} GnmSortDataClass;

G_DEFINE_TYPE (GnmSortData, gnm_sort_data, G_TYPE_OBJECT)

static void
gnm_sort_data_finalize (GObject *obj)
{
	GnmSortData *data = GNM_SORT_DATA (obj);
	g_free (data->clauses);
	g_free (data->locale);
	G_OBJECT_CLASS (gnm_sort_data_parent_class)->finalize (obj);
}

static void
gnm_sort_data_class_init (GnmSortDataClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = gnm_sort_data_finalize;
}

static void
gnm_sort_data_init (GnmSortData *data)
{
}

GnmSortData *
gnm_sort_data_new (void)
{
	return g_object_new (GNM_SORT_DATA_TYPE, NULL);
}

static int
gnm_sort_data_length (GnmSortData const *data)
{
	if (data->top)
		return range_height (&data->range);
	else
		return range_width (&data->range);
}

/* The routines to do the sorting */
static int
sort_compare_cells (GnmCell const *ca, GnmCell const *cb,
		    GnmSortClause *clause, gboolean default_locale)
{
	GnmValue const *a = ca ? ca->value : NULL;
	GnmValue const *b = cb ? cb->value : NULL;
	GnmValueType ta = VALUE_IS_EMPTY (a) ? VALUE_EMPTY : a->v_any.type;
	GnmValueType tb = VALUE_IS_EMPTY (b) ? VALUE_EMPTY : b->v_any.type;
	GnmValDiff comp;
	int ans;

	if (ta == VALUE_EMPTY && tb != VALUE_EMPTY) {
		comp = clause->asc ? IS_LESS : IS_GREATER;
	} else if (tb == VALUE_EMPTY && ta != VALUE_EMPTY) {
		comp = clause->asc ? IS_GREATER : IS_LESS;
	} else if (ta == VALUE_ERROR && tb != VALUE_ERROR) {
		comp = IS_GREATER;
	} else if (tb == VALUE_ERROR && ta != VALUE_ERROR) {
		comp = IS_LESS;
	} else {
		comp = default_locale ? value_compare (a, b, clause->cs)
			: value_compare_no_cache (a, b, clause->cs);
	}

	if (comp == IS_LESS) {
		ans = clause->asc ?  1 : -1;
	} else if (comp == IS_GREATER) {
		ans = clause->asc ? -1 :  1;
	} else
		ans = 0;

	return ans;
}

static int
sort_compare_sets (GnmSortData const *data, int indexa, int indexb,
		   gboolean default_locale)
{
	GnmCell *ca, *cb;
	int clause = 0;

	while (clause < data->num_clause) {
		int result = 0;
		int offset = data->clauses[clause].offset;

		if (data->top) {
			ca = sheet_cell_get (data->sheet,
					     data->range.start.col + offset,
					     data->range.start.row + indexa);
			cb = sheet_cell_get (data->sheet,
					     data->range.start.col + offset,
					     data->range.start.row + indexb);
		} else {
			ca = sheet_cell_get (data->sheet,
					     data->range.start.col + indexa,
					     data->range.start.row + offset);
			cb = sheet_cell_get (data->sheet,
					     data->range.start.col + indexb,
					     data->range.start.row + offset);
		}

		result = sort_compare_cells (ca, cb, &(data->clauses[clause]),
					     default_locale);
		if (result) {
			return result;
		}
		clause++;
	}

	/* Items are identical; make sort stable by using the indices.  */
	return indexa - indexb;
}

static int
sort_qsort_compare (gconstpointer a_, gconstpointer b_, gpointer data_)
{
	const int *a = a_, *b = b_;
	GnmSortData *data = data_;
	return sort_compare_sets (data, *a, *b, TRUE);
}

static int
sort_qsort_compare_in_locale (gconstpointer a_, gconstpointer b_, gpointer data_)
{
	const int *a = a_, *b = b_;
	GnmSortData *data = data_;
	return sort_compare_sets (data, *a, *b, FALSE);
}


static void
sort_permute_range (GnmSortData const *data, GnmRange *range, int adj)
{
	if (data->top) {
		range->start.row = data->range.start.row + adj;
		range->start.col = data->range.start.col;
		range->end.row = range->start.row;
		range->end.col = data->range.end.col;
	} else {
		range->start.row = data->range.start.row;
		range->start.col = data->range.start.col + adj;
		range->end.row = data->range.end.row;
		range->end.col = range->start.col;
	}
}

static int *
gnm_sort_permute_invert (int const *perm, int length)
{
	int i, *rperm;

	rperm = g_new (int, length);
	for (i = 0; i < length; i++)
		rperm[perm[i]] = i;

	return rperm;
}


#undef DEBUG_SORT

static void
sort_permute (GnmSortData *data, int const *perm, int length,
	      GOCmdContext *cc)
{
	int i, *rperm;
	GnmPasteTarget pt;

	pt.sheet = data->sheet;
	pt.paste_flags = PASTE_CONTENTS | PASTE_COMMENTS | PASTE_NO_RECALC;
	if (!data->retain_formats)
		pt.paste_flags = pt.paste_flags | PASTE_FORMATS;

#ifdef DEBUG_SORT
	g_printerr ("Permutation:");
	for (i = 0; i < length; i++)
		g_printerr (" %d", perm[i]);
	g_printerr ("\n");
#endif

	rperm = gnm_sort_permute_invert (perm, length);

	for (i = 0; i < length; i++) {
		GnmRange range1, range2;
		GnmCellRegion *rcopy1, *rcopy2 = NULL;
		int i1, i2;

		/* Special case: element is already in place.  */
		if (i == rperm[i]) {
#ifdef DEBUG_SORT
			g_printerr ("  Fixpoint: %d\n", i);
#endif
			continue;
		}

		/* Track a full cycle.  */
		sort_permute_range (data, &range1, i);
		rcopy1 = clipboard_copy_range (data->sheet, &range1);

#ifdef DEBUG_SORT
		g_printerr ("  Cycle:");
#endif
		i1 = i;
		do {
#ifdef DEBUG_SORT
			g_printerr (" %d", i1);
#endif

			i2 = rperm[i1];

			sort_permute_range (data, &range2, i2);
			if (i2 != i) {
				/* Don't copy the start of the loop; we did that above.  */
				rcopy2 = clipboard_copy_range (data->sheet, &range2);
			}

			pt.range = range2;
			clipboard_paste_region (rcopy1, &pt, cc);
			cellregion_unref (rcopy1);

			/* This is one step behind.  */
			rperm[i1] = i1;

			rcopy1 = rcopy2;
			range1 = range2;
			i1 = i2;
		} while (i1 != i);
#ifdef DEBUG_SORT
		g_printerr ("\n");
#endif
	}

	g_free (rperm);
}

void
gnm_sort_position (GnmSortData *data, int *perm, GOCmdContext *cc)
{
	int length;

	length = gnm_sort_data_length (data);
	sort_permute (data, perm, length, cc);
}

int *
gnm_sort_contents (GnmSortData *data, GOCmdContext *cc)
{
	int length, real_length, i, cur, *iperm, *real, *perm;
	int const first = data->top ? data->range.start.row : data->range.start.col;

	length = gnm_sort_data_length (data);
	real_length = 0;

	/* Discern the rows/cols to be actually sorted */
	real = g_new (int, length);
	perm = g_new (int, length);  // Might not need all
	for (i = 0; i < length; i++) {
		ColRowInfo const *cra = data->top
			? sheet_row_get (data->sheet, first + i)
			: sheet_col_get (data->sheet, first + i);

		if (cra && !cra->visible) {
			real[i] = -1;
		} else {
			real[i] = i;
			perm[real_length] = i;
			real_length++;
		}
	}

	if (real_length > 1) {
		if (data->locale) {
			char *old_locale
				= g_strdup (go_setlocale (LC_ALL, NULL));
			go_setlocale (LC_ALL, data->locale);

			g_qsort_with_data (perm, real_length, sizeof (int),
					   g_str_has_prefix (old_locale, data->locale)
					   ? sort_qsort_compare
					   : sort_qsort_compare_in_locale,
					   data);

			go_setlocale (LC_ALL, old_locale);
			g_free (old_locale);
		} else
			g_qsort_with_data (perm, real_length, sizeof (int),
					   sort_qsort_compare,
					   data);
	}

	cur = 0;
	iperm = g_new (int, length);
	for (i = 0; i < length; i++) {
		if (real[i] != -1) {
			iperm[i] = perm[cur];
			cur++;
		} else {
			iperm[i] = i;
		}
	}
	g_free (perm);
	g_free (real);

	sort_permute (data, iperm, length, cc);

	/* Make up for the PASTE_NO_RECALC.  */
	sheet_region_queue_recalc (data->sheet, &data->range);
	sheet_flag_status_update_range (data->sheet, &data->range);
	sheet_range_calc_spans (data->sheet, &data->range,
				data->retain_formats ? GNM_SPANCALC_RENDER : GNM_SPANCALC_RE_RENDER);
	sheet_redraw_all (data->sheet, FALSE);

	return iperm;
}


/**
 * gnm_sort_data_copy:
 * @data: #GnmSortData
 *
 * Returns: (transfer full): a copy of @data
 **/
GnmSortData *
gnm_sort_data_copy (GnmSortData const *data)
{
	GnmSortData *result;

	g_return_val_if_fail (GNM_IS_SORT_DATA ((gpointer)data), NULL);

	result = gnm_sort_data_new ();
	result->sheet = data->sheet;
	result->range = data->range;
	result->num_clause = data->num_clause;
	result->clauses = go_memdup (data->clauses,
				     data->num_clause * sizeof (GnmSortClause));
	result->top = data->top;
	result->retain_formats = data->retain_formats;
	result->locale = g_strdup (data->locale);

	return result;
}
