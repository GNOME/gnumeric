/*
 * sort.c : Routines for sorting cell ranges
 *
 * Author:
 * 	JP Rosevear <jpr@arcavia.com>
 *
 * (C) 2000 JP Rosevear
 * (C) 2000 Morten Welinder
 */
#include <config.h>
#include "commands.h"
#include "clipboard.h"
#include "cell.h"
#include "sort.h"
#include "value.h"
#include "rendered-value.h"

#include <stdlib.h>
#include <stdio.h>

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

static int
sort_data_length (const SortData *data)
{
	if (data->top)
		return data->range->end.row - data->range->start.row + 1;
	else
		return data->range->end.col - data->range->start.col + 1;
}


/* The routines to do the sorting */
static int
sort_compare_cells (const Cell *ca, const Cell *cb, SortClause *clause)
{
	Value *a,  *b;
	int ans = 0, fans = 0;

	if (!ca)
		a = value_new_int (0);
	else
		a = ca->value;
	if (!cb)
		b = value_new_int (0);
	else
		b = cb->value;

	if (clause->val) {
		switch (a->type) {
		case VALUE_EMPTY:
		case VALUE_BOOLEAN:
		case VALUE_FLOAT:
		case VALUE_INTEGER:
			switch (b->type) {
			case VALUE_EMPTY:
			case VALUE_BOOLEAN:
			case VALUE_FLOAT:
			case VALUE_INTEGER:
			{
				float_t fa, fb;
				fa = value_get_as_float (a);
				fb = value_get_as_float (b);
				if (fa < fb)
					ans = -1;
				else if (fa == fb)
					ans = 0;
				else
					ans = 1;
				break;
			}
			default:
				ans = -1;
				break;
			}
			break;
		default: {
			switch (b->type) {
			case VALUE_EMPTY:
			case VALUE_BOOLEAN:
			case VALUE_FLOAT:
			case VALUE_INTEGER:
				ans = 1;
				break;
			default: {
					char *sa, *sb;
					sa  = value_get_as_string (a);
					sb  = value_get_as_string (b);
					if (clause->cs)
						ans = strcmp (sa, sb);
					else
						ans = g_strcasecmp (sa, sb);
					g_free (sa);
					g_free (sb);
					break;
				}
				}
				break;
			}
		}
	} else {
		char *sa, *sb;
		if (ca)
			sa = cell_get_entered_text (ca);
		else
			sa = g_strdup ("0");
		if (cb)
			sb = cell_get_entered_text (cb);
		else
			sb = g_strdup ("0");

		if (clause->cs)
			ans = strcmp (sa, sb);
		else
			ans = g_strcasecmp (sa, sb);
		g_free (sa);
		g_free (sb);
	}

	if (ans == 0)
		fans = ans;
	else if (ans < 0)
		fans = clause->asc ?  1 : -1;
	else
		fans = clause->asc ? -1 :  1;

	if (!ca)
		value_release (a);
	if (!cb)
		value_release (b);

	return fans;
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

#undef DEBUG_SORT

static void
sort_permute (CommandContext *context, SortData *data, const int *perm, int length)
{
	int i, *rperm;
	PasteTarget pt;

	pt.sheet = data->sheet;
	pt.paste_flags = PASTE_FORMATS | PASTE_FORMULAS	| PASTE_EXPR_RELOCATE;

	rperm = g_new (int, length);
	for (i = 0; i < length; i++)
		rperm[perm[i]] = i;

#ifdef DEBUG_SORT
	fprintf (stderr, "Permutation:");
	for (i = 0; i < length; i++)
		fprintf (stderr, " %d", perm[i]);
	fprintf (stderr, "\n");
#endif

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
			clipboard_release (rcopy1);

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
sort_position (CommandContext *context, SortData *data, int *perm)
{
	int length;

	length = sort_data_length (data);
	sort_permute (context, data, perm, length);
}

int *
sort_contents (CommandContext *context, SortData *data)
{
	SortDataPerm *perm;
	int length, i, *iperm;

	length = sort_data_length (data);

	perm = g_new (SortDataPerm, length);

	for (i = 0; i < length; i++) {
		perm[i].index = i;
		/*
		 * A constant member to get around qsort's lack of a user_data
		 * argument.
		 */
		perm[i].data = data;
	}

	qsort (perm, length, sizeof (SortDataPerm), sort_qsort_compare);

	iperm = g_new (int, length);
	for (i = 0; i < length; i++)
		iperm[i] = perm[i].index;
	g_free (perm);

	sort_permute (context, data, iperm, length);

	return iperm;
}
