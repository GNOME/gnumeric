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
	SortData *data;
} SortDataPerm;


/* Clause stuff */
void
sort_clause_destroy (SortClause *clause)
{
	g_free (clause);
}

/* Data stuff */
void
sort_data_destroy (SortData *data)
{
	sort_clause_destroy (data->clauses);
	g_free (data->range);
	g_free (data);
}

int
sort_data_length (const SortData *data)
{
	if (data->top)
		return range_height (data->range);
	else
		return range_width (data->range);
}

/* The routines to do the sorting */
static int
sort_compare_cells (const Cell *ca, const Cell *cb, SortClause *clause)
{
	Value *a, *b;
	ValueType ta, tb;
	ValueCompare comp = IS_EQUAL;
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
sort_compare_sets (const SortData *data, int indexa, int indexb)
{
	Cell *ca, *cb;
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
sort_qsort_compare (const void *_a, const void *_b)
{
	const SortDataPerm *a = (const SortDataPerm *)_a;
	const SortDataPerm *b = (const SortDataPerm *)_b;

	return sort_compare_sets (a->data, a->index, b->index);
}


static void
sort_permute_range (SortData *data, Range *range, int adj)
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
sort_permute_invert (const int *perm, int length)
{
	int i, *rperm;

	rperm = g_new (int, length);
	for (i = 0; i < length; i++)
		rperm[perm[i]] = i;

	return rperm;
}


#undef DEBUG_SORT

static void
sort_permute (WorkbookControl *context, SortData *data, const int *perm, int length)
{
	int i, *rperm;
	PasteTarget pt;

	pt.sheet = data->sheet;
#warning adding the flag causes 59144, why did I add it in the first place ?
	pt.paste_flags = PASTE_CONTENT; /* | PASTE_EXPR_RELOCATE; */
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
		Range range1, range2;
		CellRegion *rcopy1, *rcopy2 = NULL;
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
			clipboard_paste_region (context, &pt, rcopy1);
			cellregion_free (rcopy1);

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
sort_position (WorkbookControl *context, SortData *data, int *perm)
{
	int length;

	length = sort_data_length (data);
	sort_permute (context, data, perm, length);
}

int *
sort_contents (WorkbookControl *context, SortData *data)
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

	sort_permute (context, data, iperm, length);

	return iperm;
}
