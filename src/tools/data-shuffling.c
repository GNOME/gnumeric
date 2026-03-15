/*
 * data-shuffling.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2003 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2026 by Morten Welinder (terra@gnome.org)
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>

#include <sheet.h>
#include <sheet-filter.h>
#include <cell.h>
#include <ranges.h>
#include <value.h>
#include <command-context.h>
#include <goffice/goffice.h>

#include <gnm-random.h>
#include <tools/data-shuffling.h>
#include <expr.h>
#include <tools/dao.h>


G_DEFINE_TYPE (GnmDataShuffle, gnm_data_shuffle, G_TYPE_OBJECT)

static void
gnm_data_shuffle_finalize (GObject *obj)
{
	GnmDataShuffle *st = GNM_DATA_SHUFFLE (obj);
	dao_free (st->dao);
	g_slist_free_full (st->changes, g_free);
	G_OBJECT_CLASS (gnm_data_shuffle_parent_class)->finalize (obj);
}

static void
gnm_data_shuffle_class_init (GnmDataShuffleClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = gnm_data_shuffle_finalize;
}

static void
gnm_data_shuffle_init (GnmDataShuffle *ds)
{
}

typedef struct {
	GnmCellPos a;
	GnmCellPos b;
} swap_t;

static void
swap_values (GnmDataShuffle *ds,
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
shuffle_cols (GnmDataShuffle *ds)
{
	int i;

	for (i = ds->a_col; i <= ds->b_col; i++) {
		int rnd_col = gnm_random_uniform_int (ds->cols) + ds->a_col;

		if (i != rnd_col)
			swap_values (ds, i, 0, rnd_col, 0);
	}
}

static void
shuffle_rows (GnmDataShuffle *ds)
{
	int i;

	for (i = ds->a_row; i <= ds->b_row; i++) {
		int rnd_row = gnm_random_uniform_int (ds->rows) + ds->a_row;

		if (i != rnd_row)
			swap_values (ds, 0, i, 0, rnd_row);
	}
}

static void
shuffle_area (GnmDataShuffle *ds)
{
	int i, j;
	int rnd_col;
	int rnd_row;

	for (i = ds->a_col; i <= ds->b_col; i++) {
		rnd_col = gnm_random_uniform_int (ds->cols) + ds->a_col;
		for (j = ds->a_row; j <= ds->b_row; j++) {
			rnd_row = gnm_random_uniform_int (ds->rows) + ds->a_row;
			swap_values (ds, i, j, rnd_col, rnd_row);
		}
	}
}

static void
init_shuffling_tool (GnmDataShuffle *st, Sheet *sheet,
		     GnmValue const *range,
		     data_analysis_output_t *dao)
{
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
do_swap_cells (GnmDataShuffle *st, swap_t *sw, WorkbookControl *wbc)
{
        GnmExprRelocateInfo reverse;

	reverse.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
        reverse.target_sheet = st->sheet;
        reverse.origin_sheet = st->sheet;
	st->tmp_area.end.col = st->tmp_area.start.col;
	st->tmp_area.end.row = st->tmp_area.start.row;

	/* Move A to a tmp_area. */
	range_init (&reverse.origin, sw->a.col, sw->a.row, sw->a.col,
		    sw->a.row);
        reverse.col_offset = st->tmp_area.start.col - sw->a.col;
        reverse.row_offset = st->tmp_area.start.row - sw->a.row;
	sheet_move_range (&reverse, NULL, GO_CMD_CONTEXT (wbc));

	/* Move B to A. */
	range_init (&reverse.origin, sw->b.col, sw->b.row, sw->b.col,
		    sw->b.row);
        reverse.col_offset = sw->a.col - sw->b.col;
        reverse.row_offset = sw->a.row - sw->b.row;
	sheet_move_range (&reverse, NULL, GO_CMD_CONTEXT (wbc));

	/* Move tmp_area to B. */
	range_init (&reverse.origin, st->tmp_area.start.col,
		    st->tmp_area.start.row,
		    st->tmp_area.end.col, st->tmp_area.end.row);
        reverse.col_offset = sw->b.col - st->tmp_area.start.col;
        reverse.row_offset = sw->b.row - st->tmp_area.start.row;
	sheet_move_range (&reverse, NULL, GO_CMD_CONTEXT (wbc));
}

static void
do_swap_cols (GnmDataShuffle *st, swap_t *sw, WorkbookControl *wbc)
{
        GnmExprRelocateInfo reverse;

	reverse.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
        reverse.target_sheet = st->sheet;
        reverse.origin_sheet = st->sheet;
	st->tmp_area.end.col = st->tmp_area.start.col;
	st->tmp_area.end.row = st->tmp_area.start.row + st->rows - 1;

	/* Move A to a tmp_area. */
	range_init (&reverse.origin, sw->a.col, st->a_row, sw->a.col,
		    st->b_row);
        reverse.col_offset = st->tmp_area.start.col - sw->a.col;
        reverse.row_offset = st->tmp_area.start.row - st->a_row;
	sheet_move_range (&reverse, NULL, GO_CMD_CONTEXT (wbc));

	/* Move B to A. */
	range_init (&reverse.origin, sw->b.col, st->a_row, sw->b.col,
		    st->b_row);
        reverse.col_offset = sw->a.col - sw->b.col;
        reverse.row_offset = 0;
	sheet_move_range (&reverse, NULL, GO_CMD_CONTEXT (wbc));

	/* Move tmp_area to B. */
	range_init (&reverse.origin, st->tmp_area.start.col,
		    st->tmp_area.start.row,
		    st->tmp_area.end.col, st->tmp_area.end.row);
        reverse.col_offset = sw->b.col - st->tmp_area.start.col;
        reverse.row_offset = st->a_row - st->tmp_area.start.row;
	sheet_move_range (&reverse, NULL, GO_CMD_CONTEXT (wbc));
}

static void
do_swap_rows (GnmDataShuffle *st, swap_t *sw, WorkbookControl *wbc)
{
        GnmExprRelocateInfo reverse;

	reverse.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
        reverse.target_sheet = st->sheet;
        reverse.origin_sheet = st->sheet;
	st->tmp_area.end.col = st->tmp_area.start.col + st->cols - 1;
	st->tmp_area.end.row = st->tmp_area.start.row;

	/* Move A to a tmp_area. */
	range_init (&reverse.origin, st->a_col, sw->a.row, st->b_col,
		    sw->a.row);
        reverse.col_offset = st->tmp_area.start.col - st->a_col;
        reverse.row_offset = st->tmp_area.start.row - sw->a.row;
	sheet_move_range (&reverse, NULL, GO_CMD_CONTEXT (wbc));

	/* Move B to A. */
	range_init (&reverse.origin, st->a_col, sw->b.row, st->b_col,
		    sw->b.row);
        reverse.col_offset = 0;
        reverse.row_offset = sw->a.row - sw->b.row;
	sheet_move_range (&reverse, NULL, GO_CMD_CONTEXT (wbc));

	/* Move tmp_area to B. */
	range_init (&reverse.origin, st->tmp_area.start.col,
		    st->tmp_area.start.row,
		    st->tmp_area.end.col, st->tmp_area.end.row);
        reverse.col_offset = st->a_col - st->tmp_area.start.col;
        reverse.row_offset = sw->b.row - st->tmp_area.start.row;
	sheet_move_range (&reverse, NULL, GO_CMD_CONTEXT (wbc));
}

static void
run_shuffling_tool (GnmDataShuffle *st, WorkbookControl *wbc)
{
	GSList  *cur;
	GnmCell *cell;
	int      i, j;

	if (st->type == GNM_DATA_SHUFFLE_COLS) {
		/* Find empty space. */
		for (i = gnm_sheet_get_last_col (st->sheet); i >= 0; i--)
			for (j = gnm_sheet_get_last_row (st->sheet); j >= 0; j--) {
				cell = sheet_cell_get (st->sheet, i, j);
				if (cell != NULL)
					break;
				else if (gnm_sheet_get_max_rows (st->sheet) - j >= st->rows)
					goto cols_out;
			}
	cols_out:
		if (i < 0)
			return;
		range_init (&st->tmp_area, i, j, i, j + st->rows - 1);
		for (cur = st->changes; cur; cur = cur->next)
			do_swap_cols (st, (swap_t *) cur->data, wbc);
	} else if (st->type == GNM_DATA_SHUFFLE_ROWS) {
		/* Find empty space. */
		for (i = gnm_sheet_get_last_row (st->sheet); i >= 0; i--)
			for (j = gnm_sheet_get_last_col (st->sheet); j >= 0; j--) {
				cell = sheet_cell_get (st->sheet, j, i);
				if (cell != NULL)
					break;
				else if (gnm_sheet_get_max_cols (st->sheet) - j >= st->cols)
					goto rows_out;
			}
	rows_out:
		if (i < 0)
			return;
		range_init (&st->tmp_area, j, i, j + st->cols - 1, i);
		for (cur = st->changes; cur; cur = cur->next)
			do_swap_rows (st, (swap_t *) cur->data, wbc);
	} else {
		/* GNM_DATA_SHUFFLE_AREA */
		/* Find empty space. */
		for (i = gnm_sheet_get_last_col (st->sheet); i >= 0; i--)
			for (j = gnm_sheet_get_last_row (st->sheet); j >= 0; j--) {
				cell = sheet_cell_get (st->sheet, i, j);
				if (cell == NULL)
					goto area_out;
			}
	area_out:
		if (i < 0)
			return;
		range_init (&st->tmp_area, i, j, i, j);
		for (cur = st->changes; cur; cur = cur->next)
			do_swap_cells (st, (swap_t *) cur->data, wbc);
	}
}

/**
 * gnm_data_shuffle_new:
 * @dao: (transfer full): #data_analysis_output_t
 * @sheet: #Sheet
 * @input_range: (transfer none): range to shuffle
 * @shuffling_type: #GnmDataShuffleType
 *
 * Returns: (transfer full): a new #GnmDataShuffle.
 **/
GnmDataShuffle *
gnm_data_shuffle_new (data_analysis_output_t *dao,
		      Sheet                  *sheet,
		      GnmValue const         *input_range,
		      GnmDataShuffleType      shuffling_type)
{
	GnmDataShuffle *st = g_object_new (GNM_DATA_SHUFFLE_TYPE, NULL);

	init_shuffling_tool (st, sheet, input_range, dao);
	st->type = shuffling_type;

	if (shuffling_type == GNM_DATA_SHUFFLE_COLS)
		shuffle_cols (st);
	else if (shuffling_type == GNM_DATA_SHUFFLE_ROWS)
		shuffle_rows (st);
	else /* GNM_DATA_SHUFFLE_AREA */
		shuffle_area (st);

	return st;
}

/**
 * gnm_data_shuffle_redo:
 * @st: #GnmDataShuffle
 * @wbc: #WorkbookControl
 *
 * Performs or re-performs the shuffling operation.
 **/
void
gnm_data_shuffle_redo (GnmDataShuffle *st, WorkbookControl *wbc)
{
	run_shuffling_tool (st, wbc);
	dao_autofit_columns (st->dao);
	sheet_redraw_all (st->sheet, TRUE);

	/* Reverse the list for undo. */
	st->changes = g_slist_reverse (st->changes);
}
