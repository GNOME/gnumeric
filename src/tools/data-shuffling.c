/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * data-shuffling.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2003 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <sheet.h>
#include <sheet-filter.h>
#include <cell.h>
#include <ranges.h>
#include <gui-util.h>
#include <tool-dialogs.h>
#include <dao-gui-utils.h>
#include <value.h>
#include <workbook-edit.h>

#include <glade/glade.h>
#include <widgets/gnumeric-expr-entry.h>
#include "mathfunc.h"
#include "data-shuffling.h"
#include "dao.h"
#include "expr.h"


typedef struct {
	CellPos a;
	CellPos b;
} swap_t;

static void
swap_values (data_shuffling_t *ds, 
	     int col_a, int row_a, int col_b, int row_b)
{
	swap_t *s = g_new (swap_t, 1);

	s->a.col = col_a;
	s->a.row = row_a;
	s->b.col = col_b;
	s->b.row = row_b;

	ds->changes = g_slist_prepend (ds->changes, s);
}


static void
shuffle_cols (data_shuffling_t *ds)
{
	int i, j, rnd_col;

	for (i = ds->a_col; i <= ds->b_col; i++) {
		rnd_col = (int) (ds->cols * random_01 () + ds->a_col);

		if (i != rnd_col)
			swap_values (ds, i, 0, rnd_col, 0);
	}
}

static void
shuffle_rows (data_shuffling_t *ds)
{
	int i, j, rnd_row;

	for (i = ds->a_row; i <= ds->b_row; i++) {
		rnd_row = (int) (ds->rows * random_01 () + ds->a_row);

		if (i != rnd_row)
			swap_values (ds, 0, i, 0, rnd_row);
	}
}

static void
shuffle_area (data_shuffling_t *ds)
{
	int i, j;
	int rnd_col;
	int rnd_row;

	for (i = ds->a_col; i <= ds->b_col; i++) {
		rnd_col = (int) (ds->cols * random_01 () + ds->a_col);

		for (j = ds->a_row; j <= ds->b_row; j++) {
			rnd_row = (int) (ds->rows * random_01 () + ds->a_row);
		
			swap_values (ds, i, j, rnd_col, rnd_row);
		}
	}
}

static void
init_shuffling_tool (data_shuffling_t *st, Sheet *sheet, Value *range,
		     data_analysis_output_t *dao)
{
	Cell *cell;
	int  i, j;

	st->a_col   = range->v_range.cell.a.col;
	st->a_row   = range->v_range.cell.a.row;
	st->b_col   = range->v_range.cell.b.col;
	st->b_row   = range->v_range.cell.b.row;
	st->cols    = st->b_col - st->a_col + 1;
	st->rows    = st->b_row - st->a_row + 1;
	st->dao     = dao;
	st->sheet   = sheet;
	st->changes = NULL;
}

static void
do_swap_cells (data_shuffling_t *st, swap_t *sw)
{
        GnmExprRelocateInfo reverse;

        reverse.target_sheet = st->sheet;
        reverse.origin_sheet = st->sheet;
	st->tmp_area.end.col = st->tmp_area.start.col;
	st->tmp_area.end.row = st->tmp_area.start.row;

	/* Move A to a tmp_area. */
	range_init (&reverse.origin, sw->a.col, sw->a.row, sw->a.col,
		    sw->a.row);
        reverse.col_offset = st->tmp_area.start.col - sw->a.col;
        reverse.row_offset = st->tmp_area.start.row - sw->a.row;
	sheet_move_range (&reverse, NULL, COMMAND_CONTEXT (st->wbc));

	/* Move B to A. */
	range_init (&reverse.origin, sw->b.col, sw->b.row, sw->b.col,
		    sw->b.row);
        reverse.col_offset = sw->a.col - sw->b.col;
        reverse.row_offset = sw->a.row - sw->b.row;
	sheet_move_range (&reverse, NULL, COMMAND_CONTEXT (st->wbc));

	/* Move tmp_area to B. */
	range_init (&reverse.origin, st->tmp_area.start.col,
		    st->tmp_area.start.row,
		    st->tmp_area.end.col, st->tmp_area.end.row);
        reverse.col_offset = sw->b.col - st->tmp_area.start.col;
        reverse.row_offset = sw->b.row - st->tmp_area.start.row;
	sheet_move_range (&reverse, NULL, COMMAND_CONTEXT (st->wbc));
}

static void
do_swap_cols (data_shuffling_t *st, swap_t *sw)
{
        GnmExprRelocateInfo reverse;

        reverse.target_sheet = st->sheet;
        reverse.origin_sheet = st->sheet;
	st->tmp_area.end.col = st->tmp_area.start.col;
	st->tmp_area.end.row = st->tmp_area.start.row + st->rows - 1;

	/* Move A to a tmp_area. */
	range_init (&reverse.origin, sw->a.col, st->a_row, sw->a.col,
		    st->b_row);
        reverse.col_offset = st->tmp_area.start.col - sw->a.col;
        reverse.row_offset = st->tmp_area.start.row - st->a_row;
	sheet_move_range (&reverse, NULL, COMMAND_CONTEXT (st->wbc));

	/* Move B to A. */
	range_init (&reverse.origin, sw->b.col, st->a_row, sw->b.col,
		    st->b_row);
        reverse.col_offset = sw->a.col - sw->b.col;
        reverse.row_offset = 0;
	sheet_move_range (&reverse, NULL, COMMAND_CONTEXT (st->wbc));

	/* Move tmp_area to B. */
	range_init (&reverse.origin, st->tmp_area.start.col,
		    st->tmp_area.start.row,
		    st->tmp_area.end.col, st->tmp_area.end.row);
        reverse.col_offset = sw->b.col - st->tmp_area.start.col;
        reverse.row_offset = st->a_row - st->tmp_area.start.row;
	sheet_move_range (&reverse, NULL, COMMAND_CONTEXT (st->wbc));
}

static void
do_swap_rows (data_shuffling_t *st, swap_t *sw)
{
        GnmExprRelocateInfo reverse;

        reverse.target_sheet = st->sheet;
        reverse.origin_sheet = st->sheet;
	st->tmp_area.end.col = st->tmp_area.start.col + st->cols - 1;
	st->tmp_area.end.row = st->tmp_area.start.row;

	/* Move A to a tmp_area. */
	range_init (&reverse.origin, st->a_col, sw->a.row, st->b_col,
		    sw->a.row);
        reverse.col_offset = st->tmp_area.start.col - st->a_col;
        reverse.row_offset = st->tmp_area.start.row - sw->a.row;
	sheet_move_range (&reverse, NULL, COMMAND_CONTEXT (st->wbc));

	/* Move B to A. */
	range_init (&reverse.origin, st->a_col, sw->b.row, st->b_col,
		    sw->b.row);
        reverse.col_offset = 0;
        reverse.row_offset = sw->a.row - sw->b.row;
	sheet_move_range (&reverse, NULL, COMMAND_CONTEXT (st->wbc));

	/* Move tmp_area to B. */
	range_init (&reverse.origin, st->tmp_area.start.col,
		    st->tmp_area.start.row,
		    st->tmp_area.end.col, st->tmp_area.end.row);
        reverse.col_offset = st->a_col - st->tmp_area.start.col;
        reverse.row_offset = sw->b.row - st->tmp_area.start.row;
	sheet_move_range (&reverse, NULL, COMMAND_CONTEXT (st->wbc));	
}

static void
run_shuffling_tool (data_shuffling_t *st)
{
	GSList *cur;

	range_init (&st->tmp_area, 7, 0, 7, 0);

	if (st->type == SHUFFLE_COLS)
		for (cur = st->changes; cur; cur = cur->next)
			do_swap_cols (st, (swap_t *) cur->data);
	else if (st->type == SHUFFLE_ROWS)
		for (cur = st->changes; cur; cur = cur->next)
			do_swap_rows (st, (swap_t *) cur->data);
	else /* SHUFFLE_AREA */
		for (cur = st->changes; cur; cur = cur->next)
			do_swap_cells (st, (swap_t *) cur->data);
}

data_shuffling_t *
data_shuffling (WorkbookControl        *wbc,
		data_analysis_output_t *dao,
		Sheet                  *sheet,
		Value                  *input_range, 
		int                    shuffling_type)
{
	data_shuffling_t *st = g_new (data_shuffling_t, 1);

	dao_prepare_output (wbc, dao, "Shuffeled");

	init_shuffling_tool (st, sheet, input_range, dao);
	st->type = shuffling_type;
	st->wbc  = wbc;

	if (shuffling_type == SHUFFLE_COLS)
		shuffle_cols (st);
	else if (shuffling_type == SHUFFLE_ROWS)
		shuffle_rows (st);
	else /* SHUFFLE_AREA */
		shuffle_area (st);

	return st;
}

void
data_shuffling_redo (data_shuffling_t *st)
{
	GSList *tmp;

	run_shuffling_tool (st);
	dao_autofit_columns (st->dao);
	sheet_redraw_all (st->sheet, TRUE);

	/* Reverse the list for undo. */
	tmp = g_slist_reverse (st->changes);
	st->changes = tmp;
}

static void
cb_free (swap_t *data, gpointer ignore)
{
	g_free (data);
}

void
data_shuffling_free (data_shuffling_t *st)
{
	g_free (st->dao);
	g_slist_foreach (st->changes, (GFunc) cb_free, NULL);
	g_slist_free (st->changes);	
}
