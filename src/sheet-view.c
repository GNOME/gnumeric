/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-view.c:
 *
 * Copyright (C) 2002-2006 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
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
#include <stdlib.h>
#include "gnumeric.h"

#include "sheet-view.h"
#include "sheet.h"
#include "sheet-merge.h"
#include "sheet-filter.h"
#include "sheet-private.h"
#include "sheet-control.h"
#include "sheet-control-priv.h"
#include "workbook-view.h"
#include "workbook-control.h"
#include "ranges.h"
#include "selection.h"
#include "application.h"
#include "value.h"
#include "parse-util.h"
#include "expr-name.h"
#include "command-context.h"

#include <gsf/gsf-impl-utils.h>

/*************************************************************************/

static void
auto_expr_timer_clear (SheetView *sv)
{
	if (sv->auto_expr_timer != 0) {
		g_source_remove (sv->auto_expr_timer);
		sv->auto_expr_timer = 0;
	}
}

static gboolean
cb_update_auto_expr (gpointer data)
{
	SheetView *sv = (SheetView *) data;

	if (wb_view_cur_sheet_view (sv->sv_wbv) == sv)
		wb_view_auto_expr_recalc (sv->sv_wbv);

	sv->auto_expr_timer = 0;
	return FALSE;
}

/*************************************************************************/

static void
sv_sheet_name_changed (G_GNUC_UNUSED Sheet *sheet,
		       G_GNUC_UNUSED GParamSpec *pspec,
		       SheetView *sv)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	sv->edit_pos_changed.content = TRUE;
}

static void
sv_sheet_visibility_changed (Sheet *sheet,
			     G_GNUC_UNUSED GParamSpec *pspec,
			     SheetView *sv)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	/* See bug 366477.  */
	if (sheet_is_visible (sheet) && !wb_view_cur_sheet (sv->sv_wbv))
		wb_view_sheet_focus (sv->sv_wbv, sheet);
}

static void
sv_sheet_r1c1_changed (Sheet *sheet,
		       G_GNUC_UNUSED GParamSpec *pspec,
		       SheetView *sv)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	sv->edit_pos_changed.location = TRUE;
}

Sheet *
sv_sheet (SheetView const *sv)
{
	g_return_val_if_fail (IS_SHEET_VIEW (sv), NULL);
	return sv->sheet;
}

WorkbookView *
sv_wbv (SheetView const *sv)
{
	g_return_val_if_fail (IS_SHEET_VIEW (sv), NULL);
	return sv->sv_wbv;
}

static void
sv_init_sc (SheetView const *sv, SheetControl *sc)
{
	GnmCellPos initial;

	sc_scale_changed (sc);

	/* set_panes will change the initial so cache it */
	initial = sv->initial_top_left;
	sc_set_panes (sc);

	/* And this will restore it */
	sc_set_top_left (sc, initial.col, initial.row);
	sc_scrollbar_config (sc);

	/* Set the visible bound, not the logical bound */
	sc_cursor_bound (sc, selection_first_range (sv, NULL, NULL));
	sc_ant (sc);
}

void
sv_attach_control (SheetView *sv, SheetControl *sc)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (IS_SHEET_CONTROL (sc));
	g_return_if_fail (sc->view == NULL);

	if (sv->controls == NULL)
		sv->controls = g_ptr_array_new ();
	g_ptr_array_add (sv->controls, sc);
	sc->view  = sv;
	sc->sheet = sv_sheet (sv); /* convenient */
	sv_init_sc (sv, sc);
}

void
sv_detach_control (SheetControl *sc)
{
	g_return_if_fail (IS_SHEET_CONTROL (sc));
	g_return_if_fail (IS_SHEET_VIEW (sc->view));

	g_ptr_array_remove (sc->view->controls, sc);
	if (sc->view->controls->len == 0) {
		g_ptr_array_free (sc->view->controls, TRUE);
		sc->view->controls = NULL;
	}
	sc->view = NULL;
}

static void
sv_weakref_notify (SheetView **ptr, GObject *sv)
{
	g_return_if_fail (ptr != NULL);
	g_return_if_fail (*ptr == (SheetView *)sv); /* remember sv is dead */
	*ptr = NULL;
}

void
sv_weak_ref (SheetView *sv, SheetView **ptr)
{
	g_return_if_fail (ptr != NULL);

	*ptr = sv;
	if (sv != NULL)
		g_object_weak_ref (G_OBJECT (sv),
			(GWeakNotify) sv_weakref_notify,
			ptr);
}

void
sv_weak_unref (SheetView **ptr)
{
	g_return_if_fail (ptr != NULL);

	if (*ptr != NULL) {
		g_object_weak_unref (G_OBJECT (*ptr),
			(GWeakNotify) sv_weakref_notify,
			ptr);
		*ptr = NULL;
	}
}

static GObjectClass *parent_class;

static void
sv_real_dispose (GObject *object)
{
	SheetView *sv = SHEET_VIEW (object);

	if (sv->controls != NULL) {
		SHEET_VIEW_FOREACH_CONTROL (sv, control, {
			sv_detach_control (control);
			g_object_unref (G_OBJECT (control));
		});
		if (sv->controls != NULL)
			g_warning ("Unexpected left over controls");
	}

	if (sv->sheet) {
		Sheet *sheet = sv->sheet;
		sv->sheet = NULL;
		g_ptr_array_remove (sheet->sheet_views, sv);
		g_signal_handlers_disconnect_by_func (sheet, sv_sheet_name_changed, sv);
		g_signal_handlers_disconnect_by_func (sheet, sv_sheet_visibility_changed, sv);
		g_signal_handlers_disconnect_by_func (sheet, sv_sheet_r1c1_changed, sv);
		g_object_unref (sv);
		g_object_unref (sheet);
	}

	sv_unant (sv);
	sv_selection_free (sv);
	auto_expr_timer_clear (sv);

	parent_class->dispose (object);
}

static void
sheet_view_class_init (GObjectClass *klass)
{
	SheetViewClass *wbc_class = SHEET_VIEW_CLASS (klass);

	g_return_if_fail (wbc_class != NULL);

	parent_class = g_type_class_peek_parent (klass);
	klass->dispose = sv_real_dispose;
}

static void
sheet_view_init (GObject *object)
{
	SheetView *sv = SHEET_VIEW (object);

	/* Init menu states */
	sv->enable_insert_rows = TRUE;
	sv->enable_insert_cols = TRUE;
	sv->enable_insert_cells = TRUE;

	sv->edit_pos_changed.location = TRUE;
	sv->edit_pos_changed.content  = TRUE;
	sv->edit_pos_changed.style    = TRUE;
	sv->selection_content_changed = TRUE;
	sv->reposition_selection = TRUE;
	sv->auto_expr_timer = 0;

	sv->frozen_top_left.col = sv->frozen_top_left.row =
	sv->unfrozen_top_left.col = sv->unfrozen_top_left.row = -1;
	sv->initial_top_left.col = sv->initial_top_left.row = 0;

	sv_selection_add_pos (sv, 0, 0);
}

GSF_CLASS (SheetView, sheet_view,
	   sheet_view_class_init, sheet_view_init,
	   G_TYPE_OBJECT);

SheetView *
sheet_view_new (Sheet *sheet, WorkbookView *wbv)
{
	SheetView *sv;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	sv = g_object_new (SHEET_VIEW_TYPE, NULL);
	sv->sheet = g_object_ref (sheet);
	sv->sv_wbv = wbv;
	g_ptr_array_add (sheet->sheet_views, sv);
	g_object_ref (sv);

	g_signal_connect (G_OBJECT (sheet),
			  "notify::name",
			  G_CALLBACK (sv_sheet_name_changed),
			  sv);

	g_signal_connect (G_OBJECT (sheet),
			  "notify::visibility",
			  G_CALLBACK (sv_sheet_visibility_changed),
			  sv);

	g_signal_connect (G_OBJECT (sheet),
			  "notify::use-r1c1",
			  G_CALLBACK (sv_sheet_r1c1_changed),
			  sv);

	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sv_init_sc (sv, control););
	return sv;
}

void
sv_dispose (SheetView *sv)
{
	g_object_run_dispose (G_OBJECT (sv));
}

void
sv_unant (SheetView *sv)
{
	GList *ptr;

	g_return_if_fail (IS_SHEET_VIEW (sv));

	if (sv->ants == NULL)
		return;
	for (ptr = sv->ants; ptr != NULL; ptr = ptr->next)
		g_free (ptr->data);
	g_list_free (sv->ants);
	sv->ants = NULL;

	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sc_unant (control););
}

/**
 * sv_ant :
 * @sv :
 * @ranges :
 */
void
sv_ant (SheetView *sv, GList *ranges)
{
	GList *ptr;

	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (ranges != NULL);

	if (sv->ants != NULL)
		sv_unant (sv);
	for (ptr = ranges; ptr != NULL; ptr = ptr->next)
		sv->ants = g_list_prepend (sv->ants, range_dup (ptr->data));
	sv->ants = g_list_reverse (sv->ants);

	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sc_ant (control););
}

void
sv_make_cell_visible (SheetView *sv, int col, int row,
		      gboolean couple_panes)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	SHEET_VIEW_FOREACH_CONTROL(sv, control,
		sc_make_cell_visible (control, col, row, couple_panes););
}

void
sv_redraw_range	(SheetView *sv, GnmRange const *r)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));

	SHEET_VIEW_FOREACH_CONTROL (sv, sc, sc_redraw_range (sc, r););
}

void
sv_redraw_headers (SheetView const *sv,
		   gboolean col, gboolean row,
		   GnmRange const* r /* optional == NULL */)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));

	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sc_redraw_headers (control, col, row, r););
}

gboolean
sv_selection_copy (SheetView *sv, WorkbookControl *wbc)
{
	GnmRange const *sel;

	g_return_val_if_fail (IS_SHEET_VIEW (sv), FALSE);
	if (!(sel = selection_first_range (sv, GO_CMD_CONTEXT (wbc), _("Copy"))))
		return FALSE;

	gnm_app_clipboard_cut_copy (wbc, FALSE, sv, sel, TRUE);

	return TRUE;
}

gboolean
sv_selection_cut (SheetView *sv, WorkbookControl *wbc)
{
	GnmRange const *sel;

	/* 'cut' is a poor description of what we're
	 * doing here.  'move' would be a better
	 * approximation.  The key portion of this process is that
	 * the range being moved has all
	 *	- references to it adjusted to the new site.
	 *	- relative references from it adjusted.
	 *
	 * NOTE : This command DOES NOT MOVE ANYTHING !
	 *        We only store the src, paste does the move.
	 */
	g_return_val_if_fail (IS_SHEET_VIEW (sv), FALSE);

	if (!(sel = selection_first_range (sv, GO_CMD_CONTEXT (wbc), _("Cut"))))
		return FALSE;

	if (sheet_range_splits_region (sv_sheet (sv), sel, NULL, GO_CMD_CONTEXT (wbc), _("Cut")))
		return FALSE;

	gnm_app_clipboard_cut_copy (wbc, TRUE, sv, sel, TRUE);

	return TRUE;
}

/**
 * sv_cursor_set :
 * @sv : The sheet
 * @edit :
 * @base_col :
 * @base_row :
 * @move_col :
 * @move_row :
 * @bound    : An optionally NULL range that should contain all the supplied points
 **/
void
sv_cursor_set (SheetView *sv,
	       GnmCellPos const *edit,
	       int base_col, int base_row,
	       int move_col, int move_row,
	       GnmRange const *bound)
{
	GnmRange r;

	g_return_if_fail (IS_SHEET_VIEW (sv));

	/* Change the edit position */
	sv_set_edit_pos (sv, edit);

	sv->cursor.base_corner.col = base_col;
	sv->cursor.base_corner.row = base_row;
	sv->cursor.move_corner.col = move_col;
	sv->cursor.move_corner.row = move_row;

	if (bound == NULL) {
		if (base_col < move_col) {
			r.start.col =  base_col;
			r.end.col =  move_col;
		} else {
			r.end.col =  base_col;
			r.start.col =  move_col;
		}
		if (base_row < move_row) {
			r.start.row =  base_row;
			r.end.row =  move_row;
		} else {
			r.end.row =  base_row;
			r.start.row =  move_row;
		}
		bound = &r;
	}

	g_return_if_fail (range_is_sane	(bound));

	SHEET_VIEW_FOREACH_CONTROL(sv, control,
		sc_cursor_bound (control, bound););
}

void
sv_set_edit_pos (SheetView *sv, GnmCellPos const *pos)
{
	GnmCellPos old;

	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (pos != NULL);
	g_return_if_fail (pos->col >= 0);
	g_return_if_fail (pos->col < gnm_sheet_get_max_cols (sv->sheet));
	g_return_if_fail (pos->row >= 0);
	g_return_if_fail (pos->row < gnm_sheet_get_max_rows (sv->sheet));

	old = sv->edit_pos;
	sv->first_tab_col = -1; /* invalidate */

	if (old.col != pos->col || old.row != pos->row) {
		GnmRange const *merged = gnm_sheet_merge_is_corner (sv->sheet, &old);

		sv->edit_pos_changed.location =
		sv->edit_pos_changed.content =
		sv->edit_pos_changed.style  = TRUE;

		/* Redraw before change */
		if (merged == NULL) {
			GnmRange tmp; tmp.start = tmp.end = old;
			sv_redraw_range (sv, &tmp);
		} else
			sv_redraw_range (sv, merged);

		sv->edit_pos_real = *pos;

		/* Redraw after change (handling merged cells) */
		merged = gnm_sheet_merge_contains_pos (sv->sheet, &sv->edit_pos_real);
		if (merged == NULL) {
			GnmRange tmp; tmp.start = tmp.end = *pos;
			sv_redraw_range (sv, &tmp);
			sv->edit_pos = sv->edit_pos_real;
		} else {
			sv_redraw_range (sv, merged);
			sv->edit_pos = merged->start;
		}
	}
}

/**
 * sv_flag_status_update_pos:
 *    flag the view as requiring an update to the status display
 *    if the supplied cell location is the edit cursor, or part of the
 *    selected region.
 *
 * @cell : The cell that has changed.
 *
 * Will cause the format toolbar, the edit area, and the auto expressions to be
 * updated if appropriate.
 */
void
sv_flag_status_update_pos (SheetView *sv, GnmCellPos const *pos)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (pos != NULL);

	/* if a part of the selected region changed value update
	 * the auto expressions
	 */
	if (sv_is_pos_selected (sv, pos->col, pos->row))
		sv->selection_content_changed = TRUE;

	/* If the edit cell changes value update the edit area
	 * and the format toolbar
	 */
	if (pos->col == sv->edit_pos.col && pos->row == sv->edit_pos.row)
		sv->edit_pos_changed.content =
		sv->edit_pos_changed.style  = TRUE;
}

/**
 * sheet_flag_status_update_range:
 *    flag the sheet as requiring an update to the status display
 *    if the supplied cell location contains the edit cursor, or intersects of
 *    the selected region.
 *
 * @sheet :
 * @range : If NULL then force an update.
 *
 * Will cause the format toolbar, the edit area, and the auto expressions to be
 * updated if appropriate.
 */
void
sv_flag_status_update_range (SheetView *sv, GnmRange const *range)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));

	/* Force an update */
	if (range == NULL) {
		sv->selection_content_changed = TRUE;
		sv->edit_pos_changed.location =
		sv->edit_pos_changed.content =
		sv->edit_pos_changed.style  = TRUE;
		return;
	}

	/* if a part of the selected region changed value update
	 * the auto expressions
	 */
	if (sv_is_range_selected (sv, range))
		sv->selection_content_changed = TRUE;

	/* If the edit cell changes value update the edit area
	 * and the format toolbar
	 */
	if (range_contains (range, sv->edit_pos.col, sv->edit_pos.row))
		sv->edit_pos_changed.content = sv->edit_pos_changed.style  = TRUE;
}

/**
 * sv_flag_style_update_range :
 * @sheet : The sheet being changed
 * @range : the range that is changing.
 *
 * Flag style  changes that will require updating the style  indicators.
 */
void
sv_flag_style_update_range (SheetView *sv, GnmRange const *range)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (range != NULL);
	if (range_contains (range, sv->edit_pos.col, sv->edit_pos.row))
		sv->edit_pos_changed.style  = TRUE;
}

/**
 * sv_flag_selection_change :
 *    flag the sheet as requiring an update to the status display
 *
 * @sheet :
 *
 * Will cause auto expressions to be updated
 */
void
sv_flag_selection_change (SheetView *sv)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	sv->selection_content_changed = TRUE;
}

void
sv_update (SheetView *sv)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));

	if (sv->edit_pos_changed.content) {
		sv->edit_pos_changed.content = FALSE;
		if (wb_view_cur_sheet_view (sv->sv_wbv) == sv)
			wb_view_edit_line_set (sv->sv_wbv, NULL);
	}

	if (sv->edit_pos_changed.style ) {
		sv->edit_pos_changed.style  = FALSE;
		if (wb_view_cur_sheet_view (sv->sv_wbv) == sv)
			wb_view_style_feedback (sv->sv_wbv);
	}

	if (sv->edit_pos_changed.location) {
		sv->edit_pos_changed.location = FALSE;
		if (wb_view_cur_sheet_view (sv->sv_wbv) == sv)
			wb_view_selection_desc (sv->sv_wbv, TRUE, NULL);
	}

	if (sv->selection_content_changed) {
		int const lag = gnm_app_auto_expr_recalc_lag ();
		sv->selection_content_changed = FALSE;
		if (sv->auto_expr_timer == 0 || lag < 0) {
			auto_expr_timer_clear (sv);
			sv->auto_expr_timer = g_timeout_add_full (0, abs (lag), /* seems ok */
				cb_update_auto_expr, (gpointer) sv, NULL);
		}
		SHEET_VIEW_FOREACH_CONTROL (sv, sc,
			wb_control_menu_state_update (sc_wbc (sc), MS_ADD_VS_REMOVE_FILTER););
	}
}

static GnmValue *
cb_fail_if_not_selected (GnmCellIter const *iter, gpointer sv)
{
	if (!sv_is_pos_selected (sv, iter->pp.eval.col, iter->pp.eval.row))
		return VALUE_TERMINATE;
	else
		return NULL;
}

/**
 * sv_first_selection_in_filter :
 * @sv :
 *
 * Is the row in the edit_pos part of an auto filter ?
 **/
GnmFilter *
sv_first_selection_in_filter (SheetView const *sv)
{
	GSList *ptr;
	GnmRange const *r;

	g_return_val_if_fail (IS_SHEET_VIEW (sv), NULL);
	g_return_val_if_fail (sv->selections != NULL, NULL);

	r = sv->selections->data;
	for (ptr = sv->sheet->filters; ptr != NULL ; ptr = ptr->next)
		if (gnm_filter_overlaps_range (ptr->data, r))
			return ptr->data;
	return NULL;
}

/**
 * sheet_is_region_empty_or_selected:
 * @sheet: sheet to check
 * @r : the region
 *
 * Returns TRUE if the specified region of the @sheet does not
 * contain any cells that are not selected.
 *
 * FIXME: Perhaps this routine should be extended to allow testing for specific
 * features of a cell rather than just the existance of the cell.
 */
gboolean
sv_is_region_empty_or_selected (SheetView const *sv, GnmRange const *r)
{
	g_return_val_if_fail (IS_SHEET_VIEW (sv), TRUE);

	return sheet_foreach_cell_in_range (
		sv->sheet, CELL_ITER_IGNORE_NONEXISTENT,
		r->start.col, r->start.row, r->end.col, r->end.row,
		cb_fail_if_not_selected, (gpointer)sv) == NULL;
}

/**
 * sv_freeze_panes :
 * @sheet    : the sheet
 * @frozen   : top left corner of the frozen region
 * @unfrozen : top left corner of the unfrozen region
 *
 * By definition the unfrozen region must be below the frozen.
 * If @frozen == @unfrozen or @frozen == NULL unfreeze
 **/
void
sv_freeze_panes (SheetView *sv,
		 GnmCellPos const *frozen,
		 GnmCellPos const *unfrozen)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));

	if (frozen != NULL) {
		g_return_if_fail (unfrozen != NULL);
		g_return_if_fail (unfrozen->col >= frozen->col);
		g_return_if_fail (unfrozen->row >= frozen->row);

		/* Just in case */
		if (unfrozen->col != (gnm_sheet_get_max_cols (sv->sheet)-1) &&
		    unfrozen->row != (gnm_sheet_get_max_rows (sv->sheet)-1) &&
		    !gnm_cellpos_equal (frozen, unfrozen)) {
			sv->frozen_top_left = *frozen;
			sv->unfrozen_top_left = *unfrozen;
			if (sv->frozen_top_left.col == sv->unfrozen_top_left.col)
				sv->frozen_top_left.col = sv->unfrozen_top_left.col = 0;
			if (sv->frozen_top_left.row == sv->unfrozen_top_left.row)
				sv->frozen_top_left.row = sv->unfrozen_top_left.row = 0;
		} else
			frozen = unfrozen = NULL;
	}

	if (frozen == NULL) {
		g_return_if_fail (unfrozen == NULL);

		/* no change */
		if (sv->frozen_top_left.col < 0 &&
		    sv->frozen_top_left.row < 0 &&
		    sv->unfrozen_top_left.col < 0 &&
		    sv->unfrozen_top_left.row < 0)
			return;

		sv->initial_top_left = sv->frozen_top_left;
		sv->frozen_top_left.col = sv->frozen_top_left.row =
		sv->unfrozen_top_left.col = sv->unfrozen_top_left.row = -1;
	}

	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sv_init_sc (sv, control););

	WORKBOOK_VIEW_FOREACH_CONTROL(sv->sv_wbv, wbc,
		wb_control_menu_state_update (wbc, MS_FREEZE_VS_THAW););
}

/**
 * sv_panes_insdel_colrow :
 * @sv :
 * @is_cols :
 * @is_insert :
 * @start :
 * @count :
 *
 * Adjust the positions of frozen panes as necessary to handle col/row
 * insertions and deletions.  note this assumes that the ins/del operations
 * have already set the flags that will force a resize.
 **/
void
sv_panes_insdel_colrow (SheetView *sv, gboolean is_cols,
			gboolean is_insert, int start, int count)
{
	GnmCellPos tl;
	GnmCellPos br;

	g_return_if_fail (IS_SHEET_VIEW (sv));

	tl = sv->frozen_top_left;	/* _copy_ them */
	br = sv->unfrozen_top_left;

	if (is_cols) {
		/* ignore if not frozen, or acting in unfrozen region */
		if (br.col <= tl.col || br.col <= start)
			return;
		if (is_insert) {
			br.col += count;
			if (tl.col > start)
				tl.col += count;
			if (br.col < tl.col || br.col >= gnm_sheet_get_max_cols (sv->sheet))
				return;
		} else {
			if (tl.col >= start)
				tl.col -= MIN (count, tl.col - start);
			br.col -= count;
			if (br.col <= tl.col)
				br.col = tl.col + 1;
		}
	} else {
		/* ignore if not frozen, or acting in unfrozen region */
		if (br.row <= tl.row || br.row <= start)
			return;
		if (is_insert) {
			br.row += count;
			if (tl.row > start)
				tl.row += count;
			if (br.row < tl.row || br.row >= gnm_sheet_get_max_rows (sv->sheet))
				return;
		} else {
			if (tl.row >= start)
				tl.row -= MIN (count, tl.row - start);
			br.row -= count;
			if (br.row <= tl.row)
				br.row = tl.row + 1;
		}
	}
	sv_freeze_panes (sv, &tl, &br);
}

gboolean
sv_is_frozen (SheetView const *sv)
{
	g_return_val_if_fail (IS_SHEET_VIEW (sv), FALSE);

	/* be flexible, in the future we will support 2 way splits too */
	return  sv->unfrozen_top_left.col >= 0 ||
		sv->unfrozen_top_left.row >= 0;
}

/**
 * sv_set_initial_top_left
 * @sv : the sheet view.
 * @col   :
 * @row   :
 *
 * Sets the top left cell that a newly created sheet control should display.
 * This corresponds to the top left cell visible in pane 0 (frozen or not).
 * NOTE : the unfrozen_top_left != initial_top_left.  Unfrozen is the first
 * unfrozen cell, and corresponds to the _minimum_ cell in pane 0.  However,
 * the pane can scroll and may have something else currently visible as the top
 * left.
 */
void
sv_set_initial_top_left (SheetView *sv, int col, int row)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (0 <= col && col < gnm_sheet_get_max_cols (sv->sheet));
	g_return_if_fail (0 <= row && row < gnm_sheet_get_max_rows (sv->sheet));
	g_return_if_fail (!sv_is_frozen (sv) ||
			  (sv->unfrozen_top_left.col <= col &&
			   sv->unfrozen_top_left.row <= row));

	sv->initial_top_left.col = col;
	sv->initial_top_left.row = row;
}
