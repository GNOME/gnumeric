/*
 * sort.c : Routines for sorting cell ranges
 *
 * Author:
 * 	JP Rosevear <jpr@arcavia.com>
 *
 * (C) 2000 JP Rosevear
 * (C) 2000 Morten Welinder
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "sort.h"

#include "commands.h"
#include "clipboard.h"
#include "cell.h"
#include "value.h"
#include "sheet.h"
#include "ranges.h"

#include <stdlib.h>

typedef struct {
	int index;
	GnmSortData *data;
} SortDataPerm;


/* Clause stuff */
void
sort_clause_destroy (GnmSortClause *clause)
{
	g_free (clause);
}

/* Data stuff */
void
sort_data_destroy (GnmSortData *data)
{
	sort_clause_destroy (data->clauses);
	g_free (data->range);
	g_free (data);
}

int
sort_data_length (GnmSortData const *data)
{
	if (data->top)
		return range_height (data->range);
	else
		return range_width (data->range);
}

/* The routines to do the sorting */
static int
sort_compare_cells (GnmCell const *ca, GnmCell const *cb, GnmSortClause *clause)
{
	GnmValue *a, *b;
	GnmValueType ta, tb;
	GnmValDiff comp = IS_EQUAL;
	int ans = 0;

	if (!ca)
		a = NULL;
	else
		a = ca->value;
	if (!cb)
		b = NULL;
	else
		b = cb->value;

	ta = VALUE_IS_EMPTY (a) ? VALUE_EMPTY : a->type;
	tb = VALUE_IS_EMPTY (b) ? VALUE_EMPTY : b->type;

	if (ta == VALUE_EMPTY && tb != VALUE_EMPTY) {
		comp = clause->asc ? IS_LESS : IS_GREATER;
	} else if (tb == VALUE_EMPTY && ta != VALUE_EMPTY) {
		comp = clause->asc ? IS_GREATER : IS_LESS;
	} else if (ta == VALUE_ERROR && tb != VALUE_ERROR) {
		comp = IS_GREATER;
	} else if (tb == VALUE_ERROR && ta != VALUE_ERROR) {
		comp = IS_LESS;
	} else {
		comp = value_compare (a, b, clause->cs);
	}

	if (comp == IS_LESS) {
		ans = clause->asc ?  1 : -1;
	} else if (comp == IS_GREATER) {
		ans = clause->asc ? -1 :  1;
	}

	return ans;
}

static int
sort_compare_sets (GnmSortData const *data, int indexa, int indexb)
{
	GnmCell *ca, *cb;
	int clause = 0;

	while (clause < data->num_clause) {
		int result = 0;
		int offset = data->clauses[clause].offset;

		if (data->top) {
			ca = sheet_cell_get (data->sheet,
					     data->range->start.col + offset,
					     data->range->start.row + indexa);
			cb = sheet_cell_get (data->sheet,
					     data->range->start.col + offset,
					     data->range->start.row + indexb);
		} else {
			ca = sheet_cell_get (data->sheet,
					     data->range->start.col + indexa,
					     data->range->start.row + offset);
			cb = sheet_cell_get (data->sheet,
					     data->range->start.col + indexb,
					     data->range->start.row + offset);
		}

		result = sort_compare_cells (ca, cb, &(data->clauses[clause]));
		if (result) {
			return result;
		}
		clause++;
	}

	/* Items are identical; make sort stable by using the indices.  */
	return indexa - indexb;
}

static int
sort_qsort_compare (void const *_a, void const *_b)
{
	SortDataPerm const *a = (SortDataPerm const *)_a;
	SortDataPerm const *b = (SortDataPerm const *)_b;

	return sort_compare_sets (a->data, a->index, b->index);
}


static void
sort_permute_range (GnmSortData const *data, GnmRange *range, int adj)
{
	if (data->top) {
		range->start.row = data->range->start.row + adj;
		range->start.col = data->range->start.col;
		range->end.row = range->start.row;
		range->end.col = data->range->end.col;
	} else {
		range->start.row = data->range->start.row;
		range->start.col = data->range->start.col + adj;
		range->end.row = data->range->end.row;
		range->end.col = range->start.col;
	}
}

int *
sort_permute_invert (int const *perm, int length)
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
	pt.paste_flags = PASTE_CONTENT | PASTE_COMMENTS | PASTE_NO_RECALC;
	if (!data->retain_formats)
		pt.paste_flags = pt.paste_flags | PASTE_FORMATS;

#ifdef DEBUG_SORT
	fprintf (stderr, "Permutation:");
	for (i = 0; i < length; i++)
		fprintf (stderr, " %d", perm[i]);
	fprintf (stderr, "\n");
#endif

	rperm = sort_permute_invert (perm, length);

	for (i = 0; i < length; i++) {
		GnmRange range1, range2;
		GnmCellRegion *rcopy1, *rcopy2 = NULL;
		int i1, i2;

		/* Special case: element is already in place.  */
		if (i == rperm[i]) {
#ifdef DEBUG_SORT
			fprintf (stderr, "  Fixpoint: %d\n", i);
#endif
			continue;
		}

		/* Track a full cycle.  */
		sort_permute_range (data, &range1, i);
		rcopy1 = clipboard_copy_range (data->sheet, &range1);

#ifdef DEBUG_SORT
		fprintf (stderr, "  Cycle:");
#endif
		i1 = i;
		do {
#ifdef DEBUG_SORT
			fprintf (stderr, " %d", i1);
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
		fprintf (stderr, "\n");
#endif
	}

	g_free (rperm);
}

void
sort_position (GnmSortData *data, int *perm, GOCmdContext *cc)
{
	int length;

	length = sort_data_length (data);
	sort_permute (data, perm, length, cc);
}

int *
sort_contents (GnmSortData *data, GOCmdContext *cc)
{
	ColRowInfo const *cra;
	SortDataPerm *perm;
	int length, real_length, i, cur, *iperm, *real;
	int const first = data->top ? data->range->start.row : data->range->start.col;

	length = sort_data_length (data);
	real_length = 0;

	/* Discern the rows/cols to be actually sorted */
	real = g_new (int, length);
	for (i = 0; i < length; i++) {
		cra = data->top
			? sheet_row_get (data->sheet, first + i)
			: sheet_col_get (data->sheet, first + i);

		if (cra && !cra->visible) {
			real[i] = -1;
		} else {
			real[i] = i;
			real_length++;
		}
	}

	cur = 0;
	perm = g_new (SortDataPerm, real_length);
	for (i = 0; i < length; i++) {
		if (real[i] != -1) {
			perm[cur].index = i;
			perm[cur].data = data;
			cur++;
		}
	}

	if (real_length > 1)
		qsort (perm, real_length, sizeof (SortDataPerm), sort_qsort_compare);

	cur = 0;
	iperm = g_new (int, length);
	for (i = 0; i < length; i++) {
		if (real[i] != -1) {
			iperm[i] = perm[cur].index;
			cur++;
		} else {
			iperm[i] = i;
		}
	}
	g_free (perm);
	g_free (real);

	sort_permute (data, iperm, length, cc);

	/* Make up for the PASTE_NO_RECALC.  */
	sheet_region_queue_recalc (data->sheet, data->range);
	sheet_flag_status_update_range (data->sheet, data->range);
	sheet_range_calc_spans (data->sheet, data->range,
				data->retain_formats ? SPANCALC_RENDER : SPANCALC_RE_RENDER);
	sheet_redraw_all (data->sheet, FALSE);

	return iperm;
}
