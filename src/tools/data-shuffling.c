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


typedef struct {
	Value ***values;
	int   a_col;
	int   b_col;
	int   a_row;
	int   b_row;
	int   cols;
	int   rows;

	data_analysis_output_t *dao;
	Sheet                  *sheet;
} data_shuffling_t;


static void
swap_values (data_shuffling_t *ds, 
	     int col_a, int row_a, int col_b, int row_b)
{
	Value *tmp = ds->values [row_a][col_a];

	ds->values [row_a][col_a] = ds->values [row_b][col_b];
	ds->values [row_b][col_b] = tmp;
}


static void
shuffle_cols (data_shuffling_t *ds)
{
	int i, j, rnd_col;

	for (i = ds->a_col; i <= ds->b_col; i++) {
		rnd_col = (int) (ds->cols * random_01 () + ds->a_col);

		if (i == rnd_col)
			continue;
		for (j = ds->a_row; j <= ds->b_row; j++)
			swap_values (ds, i, j, rnd_col, j);
	}
}

static void
shuffle_rows (data_shuffling_t *ds)
{
	int i, j, rnd_row;

	for (i = ds->a_row; i <= ds->b_row; i++) {
		rnd_row = (int) (ds->rows * random_01 () + ds->a_row);

		if (i == rnd_row)
			continue;
		for (j = ds->a_col; j <= ds->b_col; j++)
			swap_values (ds, j, i, j, rnd_row);
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
	int  cols, rows, i, j;

	st->a_col = range->v_range.cell.a.col;
	st->a_row = range->v_range.cell.a.row;
	st->b_col = range->v_range.cell.b.col;
	st->b_row = range->v_range.cell.b.row;
	st->cols  = st->b_col - st->a_col + 1;
	st->rows  = st->b_row - st->a_row + 1;
	st->dao   = dao;
	st->sheet = sheet;

	/* Initialize an array for bookeeping of the value pointers. */
	st->values = g_new (Value **, st->rows);
	for (i = 0; i < st->rows; i++)
		st->values [i] = g_new0 (Value *, st->cols);


	/* Read the values from the cells; all of these will be written
	 * back by close_shuffling_tool. */
	for (i = st->a_row; i <= st->b_row; i++) {
		for (j = st->a_col; j <= st->b_col; j++) {
			cell = sheet_cell_get (sheet, j, i);
			st->values [i - st->a_row][j - st->a_col] =
				(cell == NULL) ?
				value_new_empty () :
				value_duplicate (cell->value);
		}
	}
}

static void
close_shuffling_tool (data_shuffling_t *st)
{
	int i, j;

	/* Write back the values of the cells into the new places. */
	for (i = 0; i < st->rows; i++) {
		for (j = 0; j < st->cols; j++) {
			dao_set_cell_value (st->dao, j + st->a_col,
					    i + st->a_row, st->values [i][j]);
		}
	}

	/* Free the bookeeping array. Since the Values are written into
	 * a sheet using dao_set_cell_value, they don't have to be free'ed,
	 * right?? */
	for (i = 0; i < st->rows; i++)
		g_free (st->values [i]);
	g_free (st->values);
}

void
data_shuffling (WorkbookControl        *wbc,
		data_analysis_output_t *dao,
		Sheet                  *sheet,
		Value                  *input_range, 
		int                    shuffling_type)
{
	data_shuffling_t st;

	dao_prepare_output (wbc, dao, "Shuffeled");

	init_shuffling_tool (&st, sheet, input_range, dao);

	if (shuffling_type == SHUFFLE_COLS)
		shuffle_cols (&st);
	else if (shuffling_type == SHUFFLE_ROWS)
		shuffle_rows (&st);
	else /* SHUFFLE_AREA */
		shuffle_area (&st);

	close_shuffling_tool (&st);
	dao_autofit_columns (dao);
	sheet_redraw_all (sheet, TRUE);
}
