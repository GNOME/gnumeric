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

/* The routines to do the sorting */
static int
sort_compare_values (const SortData * ain,
		     const SortData * bin,
		     int clause)
{
	Cell  *ca, *cb;
	Value *a,  *b;
	int ans = 0, fans = 0;

	ca = ain->cells [ain->clauses [clause].offset];
	cb = bin->cells [bin->clauses [clause].offset];

	if (!ca)
		a = value_new_int (0);
	else
		a = ca->value;
	if (!cb)
		b = value_new_int (0);
	else
		b = cb->value;

	if (ain->clauses[clause].val) {
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
					if (ain->clauses [clause].cs)
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

		if (ain->clauses [clause].cs)
			ans = strcmp (sa, sb);
		else
			ans = g_strcasecmp (sa, sb);
		g_free (sa);
		g_free (sb);
	}

	if (ans == 0)
		if (clause < ain->num_clause - 1)
			fans = sort_compare_values(ain, bin, ++clause);
		else
			fans = ans;
	else if (ans < 0)
		fans = ain->clauses [clause].asc ?  1 : -1;
	else
		fans = ain->clauses [clause].asc ? -1 :  1;

	if (!ca)
		value_release (a);
	if (!cb)
		value_release (b);

	return fans;
}

static int
sort_compare_values2 (const SortData * ain, const SortData * bin) {
	if (ain->pos < bin->pos)
		return -1;
	else if (ain->pos > bin->pos)
		return 1;
	else
		return 0;
}

static int
sort_qsort_func (const void *a, const void *b)
{
	return sort_compare_values (a, b, 0);
}

static int
sort_qsort_func2 (const void *a, const void *b)
{
	return sort_compare_values2 (a, b);
}

static void
sort_range (Sheet *sheet, Range *range,
	    SortData *data, gboolean columns, void *func)
{
	Cell *cell;
	int lp, length, divisions, lp2;
	int start_row, end_row, start_col, end_col;

	start_row = range->start.row;
	start_col = range->start.col;
	end_row = range->end.row;
	end_col = range->end.col;

	if (columns) {
		length    = end_row - start_row + 1;
		divisions = end_col - start_col + 1;
	} else {
		length    = end_col - start_col + 1;
		divisions = end_row - start_row + 1;
	}

	for (lp = 0; lp < length; lp++) {
		data [lp].cells = g_new (Cell *, divisions);

		for (lp2 = 0; lp2 < divisions; lp2++) {
			Cell *cell;
			if (columns)
				cell = sheet_cell_get (sheet,
						       start_col + lp2,
						       start_row + lp);
			else
				cell = sheet_cell_get (sheet,
						       start_col + lp,
						       start_row + lp2);
			data [lp].cells[lp2] = cell;
			if (cell)
				sheet_cell_remove_simple (sheet, cell);
		}
	}
	sheet_redraw_cell_region (sheet, start_col, start_row,
				  end_col, end_row);

	qsort (data, length, sizeof(SortData), func);

	for (lp = 0; lp < length; lp++) {
		for (lp2 = 0; lp2 < divisions; lp2++) {
			cell = data [lp].cells [lp2];
			if (cell) {
				if (columns)
					sheet_cell_insert (sheet,
							   cell,
							   start_col+lp2,
							   start_row+lp);
				else
					sheet_cell_insert (sheet,
							   cell,
							   start_col+lp,
							   start_row+lp2);
			}
		}
		g_free (data [lp].cells);
	}
}

void
sort_position (Sheet *sheet, Range *range,
	       SortData *data, gboolean columns)
{
	sort_range (sheet, range, data, columns, sort_qsort_func2);
}

void
sort_contents (Sheet *sheet, Range *range,
	       SortData *data, gboolean columns)
{
	sort_range (sheet, range, data, columns, sort_qsort_func);
}
