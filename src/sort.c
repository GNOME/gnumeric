/*
 * sort.c : Routines for sorting cell ranges
 *
 * Author:
 * 	JP Rosevear <jpr@arcavia.com>
 *
 * (C) 2000 JP Rosevear
 */
#include <config.h>
#include "gnumeric-type-util.h"
#include "commands.h"
#include "clipboard.h"
#include "cell.h"
#include "workbook-view.h"
#include "parse-util.h"
#include "sort.h"
#include "rendered-value.h"
#include "value.h"

#include <stdlib.h>

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
sort_compare_sets (SortData *data, int indexa, int indexb)
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

	return 0;
}

static void
sort_swap (int *perm, int indexa, int indexb)
{
	int tmp = perm[indexa];

	perm[indexa] = perm[indexb];
	perm[indexb] = tmp;
}

static void
sort_qsort (SortData *data, int *perm, int l, int r)
{
	int pivot, i, j;

	if (l < r) {
		i = l;
		j = r + 1;
		pivot = l;

		while (i < j) {
			i++;

			while (i <= r) {
			       if (sort_compare_sets (data, perm[i],
						      perm[pivot]) == -1) {
				       i++;
			       } else {
				       break;
			       }
			}

			j--;

			while (j >= l) {
				if (sort_compare_sets (data, perm[j],
						       perm[pivot]) == 1) {
					j--;
				} else {
					break;
				}
			}

			if (i <= r) {
				sort_swap (perm, i, j);

			}
		}
		if (i <= r) {
			sort_swap (perm, i, j);
		}

		sort_swap (perm, l, j);

		sort_qsort (data, perm, l, j-1);
		sort_qsort (data, perm, j+1, r);
	}
}

static int
sort_permute_find (int num, int *perm, int length)
{
	int i;

	for (i=0; i < length; i++) {
		if (perm[i] == num) {
			return i;
		}
	}

	return -1;
}

static void
sort_permute_set (int num, guint8 *array)
{

	array[num/8] = array[num/8] | (1 << (num % 8));
}

static gboolean
sort_permute_is_set (int num, guint8 *array)
{

	if (array[num/8] & (1 << (num % 8))) {
			return TRUE;
	}

	return FALSE;
}

static int
sort_permute_next (guint8 *array, int *perm, int length)
{
	int i = 0;

	while (i < length) {
		if (perm[i] != i) {
			if (!sort_permute_is_set (i, array)) {
				return i;
			}
		}
		i++;
	}

	return -1;
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

static void
sort_permute (CommandContext *context, SortData *data, int *perm)
{
	guint8 *array;
	CellRegion *rcopy, *rpaste;
	PasteTarget pt;
	Range range;
	int next, length;

	if (data->top) {
		length = data->range->end.row - data->range->start.row + 1;
	} else {
		length = data->range->end.col - data->range->start.col + 1;
	}

	array = (guint8 *) g_malloc0 (length / 8 + 1);
	next = sort_permute_next (array, perm, length);

	if (next == -1) {
		return;
	}

	sort_permute_range (data, &range, perm[next]);
	rpaste = clipboard_copy_range (data->sheet, &range);

	while (next != -1) {
		sort_permute_range (data, &range, next);
		rcopy = clipboard_copy_range (data->sheet, &range);

		pt.sheet = data->sheet;
		pt.range = range;
		pt.paste_flags = PASTE_FORMATS | PASTE_FORMULAS
			| PASTE_EXPR_RELOCATE;

		clipboard_paste_region (context, &pt, rpaste);
		clipboard_release (rpaste);
		rpaste = rcopy;
		rcopy = NULL;

		sort_permute_set (next, array);

		next = sort_permute_find (next, perm, length);
		if (next < 0)
			break;
		if (sort_permute_is_set (next, array)) {
			next = sort_permute_next (array, perm, length);
			sort_permute_range (data, &range, perm[next]);

			clipboard_release (rpaste);
			rpaste = clipboard_copy_range (data->sheet, &range);
		}
	}
	clipboard_release (rpaste);
	g_free (array);
}

void
sort_position (CommandContext *context, SortData *data, int *perm)
{
	sort_permute (context, data, perm);
}

int *
sort_contents (CommandContext *context, SortData *data)
{
	int *perm;
	int length, i;

	if (data->top) {
		length = data->range->end.row - data->range->start.row + 1;
	} else {
		length = data->range->end.col - data->range->start.col + 1;
	}

	perm = g_new (int, length);
	for (i = 0; i < length; i++) {
		perm[i] = i;
	}

	sort_qsort (data, perm, 0, length - 1);
	sort_permute (context, data, perm);

	return perm;
}
