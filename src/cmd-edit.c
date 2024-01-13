/*
 * cmd-edit.c: Various commands to be used by the edit menu.
 *
 * Copyright (C) 2000 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <cmd-edit.h>

#include <application.h>
#include <command-context.h>
#include <workbook-control.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-view.h>
#include <cell.h>
#include <expr.h>
#include <dependent.h>
#include <selection.h>
#include <parse-util.h>
#include <ranges.h>
#include <commands.h>
#include <clipboard.h>
#include <value.h>
#include <wbc-gtk.h>

/**
 * sv_select_cur_row:
 * @sv: The sheet view
 *
 * Selects an entire row
 */
void
sv_select_cur_row (SheetView *sv)
{
	GnmRange const *sel = selection_first_range (sv,  NULL, NULL);
	if (sel != NULL) {
		GnmRange r = *sel;
		sv_selection_reset (sv);
		sv_selection_add_full
			(sv,
			 sv->edit_pos.col, sv->edit_pos.row,
			 0, r.start.row, gnm_sheet_get_last_col (sv->sheet), r.end.row,
			 GNM_SELECTION_MODE_ADD);
		sheet_update (sv->sheet);
	}
}

/**
 * sv_select_cur_col:
 * @sv: The sheet view
 *
 * Selects an entire column
 */
void
sv_select_cur_col (SheetView *sv)
{
	GnmRange const *sel = selection_first_range (sv,  NULL, NULL);
	if (sel != NULL) {
		GnmRange r = *sel;
		sv_selection_reset (sv);
		sv_selection_add_full
			(sv,
			 sv->edit_pos.col, sv->edit_pos.row,
			 r.start.col, 0, r.end.col, gnm_sheet_get_last_row (sv->sheet),
			 GNM_SELECTION_MODE_ADD);
		sheet_update (sv->sheet);
	}
}

/**
 * sv_select_cur_array:
 * @sv: The sheet view
 *
 * If the editpos is part of an array clear the selection and select the array.
 **/
void
sv_select_cur_array (SheetView *sv)
{
	GnmRange a;
	int const c = sv->edit_pos.col;
	int const r = sv->edit_pos.row;

	if (!gnm_cell_array_bound (sheet_cell_get (sv->sheet, c, r), &a))
		return;

	/* leave the edit pos where it is, select the entire array. */
	sv_selection_reset (sv);
	sv_selection_add_full (sv, c, r,
			       a.start.col, a.start.row, a.end.col, a.end.row,
			       GNM_SELECTION_MODE_ADD);
	sheet_update (sv->sheet);
}

static gint
cb_compare_deps (gconstpointer a, gconstpointer b)
{
	GnmCell const *cell_a = a;
	GnmCell const *cell_b = b;
	int tmp;

	if (cell_a->base.sheet != cell_b->base.sheet)
		return cell_a->base.sheet->index_in_wb - cell_b->base.sheet->index_in_wb;

	tmp = cell_a->pos.row - cell_b->pos.row;
	if (tmp != 0)
		return tmp;
	return cell_a->pos.col - cell_b->pos.col;
}

static void
cb_collect_deps (GnmDependent *dep, gpointer user)
{
	if (dependent_is_cell (dep)) {
		GList **list = (GList **)user;
		*list = g_list_prepend (*list, dep);
	}
}

/**
 * sv_select_cur_depends:
 * @sv: The sheet view
 *
 * Select all cells that depend on the expression in the current cell.
 */
void
sv_select_cur_depends (SheetView *sv)
{
	GnmCell  *cur_cell, dummy;
	GList *deps = NULL, *ptr = NULL;

	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

	cur_cell = sheet_cell_get (sv->sheet,
		sv->edit_pos.col, sv->edit_pos.row);
	if (cur_cell == NULL) {
		dummy.base.sheet = sv_sheet (sv);
		dummy.pos = sv->edit_pos;
		cur_cell = &dummy;
	}

	cell_foreach_dep (cur_cell, cb_collect_deps, &deps);
	if (deps == NULL)
		return;

	sv_selection_reset (sv);

	/* Short circuit */
	if (g_list_length (deps) == 1) {
		GnmCell *cell = deps->data;
		sv_selection_add_pos (sv, cell->pos.col, cell->pos.row,
				      GNM_SELECTION_MODE_ADD);
	} else {
		GnmRange *cur = NULL;
		ptr = NULL;

		/* Merge the sorted list of cells into rows */
		for (deps = g_list_sort (deps, &cb_compare_deps) ; deps ; ) {
			GnmCell *cell = deps->data;

			if (cur == NULL ||
			    cur->end.row != cell->pos.row ||
			    cur->end.col+1 != cell->pos.col) {
				if (cur)
					ptr = g_list_prepend (ptr, cur);
				cur = g_new (GnmRange, 1);
				cur->start.row = cur->end.row = cell->pos.row;
				cur->start.col = cur->end.col = cell->pos.col;
			} else
				cur->end.col = cell->pos.col;

			deps = g_list_remove (deps, cell);
		}
		if (cur)
			ptr = g_list_prepend (ptr, cur);

		/* Merge the coalesced rows into ranges */
		deps = ptr;
		for (ptr = NULL ; deps ; ) {
			GnmRange *r1 = deps->data;
			GList *fwd;

			for (fwd = deps->next ; fwd ; ) {
				GnmRange *r2 = fwd->data;

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
			sv_selection_add_range (sv, ptr->data);
			g_free (ptr->data);
			ptr = g_list_remove (ptr, ptr->data);
		}
	}
	sheet_update (sv->sheet);
}

/**
 * sv_select_cur_inputs:
 * @sv: The sheet view
 *
 * Select all cells that are direct potential inputs to the
 * current cell.
 **/
void
sv_select_cur_inputs (SheetView *sv)
{
	GnmCell  *cell;
	GSList   *ranges, *ptr;
	GnmEvalPos ep;

	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

	cell = sheet_cell_get (sv->sheet,
		sv->edit_pos.col, sv->edit_pos.row);
	if (cell == NULL || !gnm_cell_has_expr (cell))
		return;
	ranges = gnm_expr_top_get_ranges (cell->base.texpr);
	if (ranges == NULL)
		return;

	ep.eval = sv->edit_pos;
	ep.sheet = sv->sheet;
	ep.dep = NULL;

	sv_selection_reset (sv);
	for (ptr = ranges ; ptr != NULL ; ptr = ptr->next) {
		GnmValue *v = ptr->data;
		GnmRangeRef const *r = value_get_rangeref (v);

#warning "FIXME: What do we do in these 3D cases?"
		if ((r->a.sheet == r->b.sheet) &&
		    (r->a.sheet == NULL || r->a.sheet == sv->sheet)) {
		      gint row, col;
		      row = gnm_cellref_get_row (&r->a, &ep);
		      col = gnm_cellref_get_col (&r->a, &ep);
		      sv_selection_add_full
			      (sv, col, row, col, row,
			       gnm_cellref_get_col (&r->b, &ep),
			       gnm_cellref_get_row (&r->b, &ep),
			       GNM_SELECTION_MODE_ADD);
		    }
		value_release (v);
	}
	g_slist_free (ranges);

	sheet_update (sv->sheet);
}

/**
 * cmd_paste:
 *
 * Pastes the current cut buffer, copy buffer, or X selection to
 * the destination sheet range.
 *
 * When pasting a cut the destination MUST be the same size as the src.
 *
 * When pasting a copy the destination can be a singleton, or an integer
 * multiple of the size of the source.  This is not tested here.
 * Full undo support.
 **/
void
cmd_paste (WorkbookControl *wbc, GnmPasteTarget const *pt)
{
	GnmCellRegion  *content;
	GnmRange const *src_range;
	GnmRange dst;

	g_return_if_fail (pt != NULL);
	g_return_if_fail (IS_SHEET (pt->sheet));

	dst = pt->range;

	/* Check for locks */
	if (cmd_cell_range_is_locked_effective (pt->sheet, &dst, wbc,
						_("Paste")))
		return ;

	src_range = gnm_app_clipboard_area_get ();
	content = gnm_app_clipboard_contents_get ();

	if (content == NULL && src_range != NULL) {
		/* Pasting a Cut */
		GnmExprRelocateInfo rinfo;
		Sheet *src_sheet = gnm_app_clipboard_sheet_get ();

		/* Validate the size & shape of the target here. */
		int const cols = (src_range->end.col - src_range->start.col);
		int const rows = (src_range->end.row - src_range->start.row);

		if (range_is_singleton (&dst)) {
			dst.end.col = dst.start.col + cols;
			dst.end.row = dst.start.row + rows;
		} else if ((dst.end.col - dst.start.col) != cols ||
			   (dst.end.row - dst.start.row) != rows) {

			char *msg = g_strdup_printf (
				_("destination has a different shape (%dRx%dC) than the original (%dRx%dC)\n\n"
				  "Try selecting a single cell or an area of the same shape and size."),
				(dst.end.row - dst.start.row)+1,
				(dst.end.col - dst.start.col)+1,
				rows+1, cols+1);
			go_cmd_context_error_invalid (GO_CMD_CONTEXT (wbc),
				_("Unable to paste into selection"), msg);
			g_free (msg);
			return;
		}

		rinfo.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
		rinfo.origin = *src_range;
		rinfo.col_offset = dst.start.col - rinfo.origin.start.col;
		rinfo.row_offset = dst.start.row - rinfo.origin.start.row;
		rinfo.origin_sheet = src_sheet;
		rinfo.target_sheet = pt->sheet;

		if (!cmd_paste_cut (wbc, &rinfo, TRUE, NULL))
			gnm_app_clipboard_clear (TRUE);

	/* If this application has marked a selection use it */
	} else if (content != NULL) {
		cmd_paste_copy (wbc, pt, content);
		/* We don't own the contents, so don't unref it.  */
	} else {
		/* See if the control has access to information to paste */
		wb_control_paste_from_selection (wbc, pt);
	}
}

/**
 * cmd_paste_to_selection:
 * @wbc: workbook control
 * @dest_sv: (transfer none): The sheet into which things should be pasted
 * @paste_flags: special paste flags (eg transpose)
 *
 * Using the current selection as a target
 * Full undo support.
 */
void
cmd_paste_to_selection (WorkbookControl *wbc, SheetView *dest_sv, int paste_flags)
{
	GnmRange const *r;
	GnmPasteTarget pt;

	r = selection_first_range (dest_sv, GO_CMD_CONTEXT (wbc), _("Paste"));
	if (!r)
		return;

	pt.sheet = dest_sv->sheet;
	pt.range = *r;
	pt.paste_flags = paste_flags;
	cmd_paste (wbc, &pt);
}

/**
 * cmd_shift_rows:
 * @wbc:	The error context.
 * @sheet	the sheet
 * @col		column marking the start of the shift
 * @start_row	first row
 * @end_row	end row
 * @count	numbers of columns to shift.  negative numbers will
 *		delete count columns, positive number will insert
 *		count columns.
 *
 * Takes the cells in the region (col,start_row):(MAX_COL,end_row)
 * and copies them @count units (possibly negative) to the right.
 **/
void
cmd_shift_rows (WorkbookControl *wbc, Sheet *sheet,
		int col, int start_row, int end_row, int count)
{
	GnmExprRelocateInfo rinfo;
	char *desc;

	rinfo.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
	rinfo.col_offset = count;
	rinfo.row_offset = 0;
	rinfo.origin_sheet = rinfo.target_sheet = sheet;
	rinfo.origin.start.row = start_row;
	rinfo.origin.start.col = col;
	rinfo.origin.end.row = end_row;
	rinfo.origin.end.col = gnm_sheet_get_last_col (sheet);


	if (count > 0) {
		GnmRange r = rinfo.origin;
		r.start.col = r.end.col - count + 1;

		if (!sheet_is_region_empty (sheet, &r)) {
			go_gtk_notice_dialog (wbcg_toplevel (WBC_GTK (wbc)), GTK_MESSAGE_ERROR,
					      _("Inserting these cells would push data off the sheet. "
						"Please enlarge the sheet first."));
			return;
		}
		rinfo.origin.end.col -= count;
	}

	desc = g_strdup_printf ((start_row != end_row)
				? _("Shift rows %s")
				: _("Shift row %s"),
				rows_name (start_row, end_row));
	cmd_paste_cut (wbc, &rinfo, FALSE, desc);
}

/**
 * cmd_shift_cols:
 * @wbc:	The error context.
 * @sheet:	the sheet
 * @start_col:	first column
 * @end_col:	end column
 * @row:	row marking the start of the shift
 * @count:	numbers of rows to shift.  a negative numbers will
 *		delete count rows, positive number will insert
 *		count rows.
 *
 * Takes the cells in the region (start_col,row):(end_col,MAX_ROW)
 * and copies them @count units (possibly negative) downwards.
 */
void
cmd_shift_cols (WorkbookControl *wbc, Sheet *sheet,
		int start_col, int end_col, int row, int count)
{
	GnmExprRelocateInfo rinfo;
	char *desc;

	rinfo.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
	rinfo.col_offset = 0;
	rinfo.row_offset = count;
	rinfo.origin_sheet = rinfo.target_sheet = sheet;
	rinfo.origin.start.col = start_col;
	rinfo.origin.start.row = row;
	rinfo.origin.end.col = end_col;
	rinfo.origin.end.row = gnm_sheet_get_last_row (sheet);
	if (count > 0) {
		GnmRange r = rinfo.origin;
		r.start.row = r.end.row - count + 1;

		if (!sheet_is_region_empty (sheet, &r)) {
			go_gtk_notice_dialog (wbcg_toplevel (WBC_GTK (wbc)), GTK_MESSAGE_ERROR,
					      _("Inserting these cells would push data off the sheet. "
						"Please enlarge the sheet first."));
			return;
		}
		rinfo.origin.end.row -= count;
	}

	desc = g_strdup_printf ((start_col != end_col)
				? _("Shift columns %s")
				: _("Shift column %s"),
				cols_name (start_col, end_col));
	cmd_paste_cut (wbc, &rinfo, FALSE, desc);
}
