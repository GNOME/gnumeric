/* vim: set sw=8:
 * $Id$
 */

/*
 * cmd-edit.c: Various commands to be used by the edit menu.
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include "config.h"
#include "cmd-edit.h"
#include "selection.h"
#include "sheet.h"
#include "cell.h"
#include "expr.h"
#include "eval.h"
#include "parse-util.h"
#include "stdio.h"

/**
 * cmd_select_all:
 * @sheet: The sheet
 *
 * Selects all of the cells in the sheet
 */
void
cmd_select_all (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_selection_reset_only (sheet);
	sheet_selection_add_range (sheet, 0, 0, 0, 0,
				   SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);

	sheet_update (sheet);
}

/**
 * cmd_select_cur_row:
 * @sheet: The sheet
 *
 * Selects an entire row
 */
void
cmd_select_cur_row (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_selection_reset_only (sheet);
	sheet_selection_add_range (sheet,
		sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row,
		0, sheet->cursor.edit_pos.row,
		SHEET_MAX_COLS-1, sheet->cursor.edit_pos.row);
	sheet_update (sheet);
}

/**
 * cmd_select_cur_col:
 * @sheet: The sheet
 *
 * Selects an entire column
 */
void
cmd_select_cur_col (Sheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	sheet_selection_reset_only (sheet);
	sheet_selection_add_range (sheet,
		sheet->cursor.edit_pos.col, sheet->cursor.edit_pos.row,
		sheet->cursor.edit_pos.col, 0,
		sheet->cursor.edit_pos.col, SHEET_MAX_ROWS-1);
	sheet_update (sheet);
}

/**
 * cmd_select_cur_array :
 * @sheet: The sheet
 *
 * if the current cell is part of an array select
 * the entire array.
 */
void
cmd_select_cur_array (Sheet *sheet)
{
	ExprArray const *array;
	int col, row;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	col = sheet->cursor.edit_pos.col;
	row = sheet->cursor.edit_pos.row;
	array = cell_is_array (sheet_cell_get (sheet, col, row));

	if (array == NULL)
		return;

	sheet_selection_reset_only (sheet);
	/*
	 * leave the edit cell where it is,
	 * select the entire array.
	 */
	sheet_selection_add_range (sheet, col, row,
				   col - array->x, row - array->y,
				   col - array->x + array->cols,
				   row - array->y + array->rows);

	sheet_update (sheet);
}

static gint
cb_compare_deps (gconstpointer a, gconstpointer b)
{
	Cell const *cell_a = a;
	Cell const *cell_b = b;
	int tmp;

	tmp = cell_a->row_info->pos - cell_b->row_info->pos;
	if (tmp != 0)
		return tmp;
	return cell_a->col_info->pos - cell_b->col_info->pos;
}

/**
 * cmd_select_cur_depends :
 * @sheet: The sheet
 *
 * Select all cells that depend on the expression in the current cell.
 */
void
cmd_select_cur_depends (Sheet *sheet)
{
	Cell  *cur_cell;
	GList *deps, *ptr = NULL;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	cur_cell = sheet_cell_get (sheet, 
				   sheet->cursor.edit_pos.col,
				   sheet->cursor.edit_pos.row);
	if (cur_cell == NULL)
		return;

	deps = cell_get_dependencies (cur_cell);
	if (deps == NULL)
		return;

	sheet_selection_reset_only (sheet);

	/* Short circuit */
	if (g_list_length (deps) == 1) {
		Cell *cell = deps->data;
		sheet_selection_add (sheet, cell->col_info->pos, cell->row_info->pos);
	} else {
		Range *cur = NULL;
		ptr = NULL;

		/* Merge the sorted list of cells into rows */
		for (deps = g_list_sort (deps, &cb_compare_deps) ; deps ; ) {
			Cell *cell = deps->data;

			if (cur == NULL ||
			    cur->end.row != cell->row_info->pos ||
			    cur->end.col+1 != cell->col_info->pos) {
				if (cur)
					ptr = g_list_prepend (ptr, cur);
				cur = g_new (Range, 1);
				cur->start.row = cur->end.row = cell->row_info->pos;
				cur->start.col = cur->end.col = cell->col_info->pos;
			} else
				cur->end.col = cell->col_info->pos;

			deps = g_list_remove (deps, cell);
		}
		if (cur)
			ptr = g_list_prepend (ptr, cur);

		/* Merge the coalesced rows into ranges */
		deps = ptr;
		for (ptr = NULL ; deps ; ) {
			Range *r1 = deps->data;
			GList *fwd;

			for (fwd = deps->next ; fwd ; ) {
				Range *r2 = fwd->data;

				if (r1->start.col == r2->start.col &&
				    r1->end.col == r2->end.col &&
				    r1->start.row-1 == r2->end.row) {
					r1->start.row = r2->start.row;
					g_free (fwd->data);
					fwd = g_list_remove (fwd, r2);
				} else
					fwd = fwd->next;
			}

			ptr = g_list_prepend (ptr, r1);
			deps = g_list_remove (deps, r1);
		}

		/* now select the ranges */
		while (ptr) {
			Range *r = ptr->data;
			sheet_selection_add_range (sheet,
						   r->start.col, r->start.row,
						   r->start.col, r->start.row,
						   r->end.col, r->end.row);
			g_free (ptr->data);
			ptr = g_list_remove (ptr, r);
		}
	}
	sheet_update (sheet);
}

/**
 * cmd_select_cur_inputs :
 * @sheet: The sheet
 *
 * Select all cells that are direct potential inputs to the
 * current cell.
 */
void
cmd_select_cur_inputs (Sheet *sheet)
{
	Cell *cell;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	cell = sheet_cell_get (sheet, 
			       sheet->cursor.edit_pos.col,
			       sheet->cursor.edit_pos.row);

	if (cell == NULL || !cell_has_expr (cell))
		return;

	/* TODO : finish this */
	sheet_update (sheet);
}
