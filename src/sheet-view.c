/* vim: set sw=8: */

/*
 * sheet-view.c:
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"

#include "sheet-view.h"
#include "sheet.h"
#include "sheet-merge.h"
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

#include <gal/util/e-util.h>
#include <libgnome/gnome-i18n.h>

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

	if (wb_view_cur_sheet_view (sv->wbv) == sv)
		wb_view_auto_expr_recalc (sv->wbv, TRUE);

	sv->auto_expr_timer = 0;
	return FALSE;
}

/*************************************************************************/

Sheet *
sv_sheet (SheetView const *sv)
{
	return sv->sheet;
}

WorkbookView *
sv_wbv (SheetView const *sv)
{
	return sv->wbv;
}

void
sv_attach_control (SheetView *sv, SheetControl *sc)
{
	Range bound;

	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (IS_SHEET_CONTROL (sc));
	g_return_if_fail (sc->view == NULL);

	if (sv->controls == NULL)
		sv->controls = g_ptr_array_new ();
	g_ptr_array_add (sv->controls, sc);
	sc->view  = sv;
	sc->sheet = sv_sheet (sv); /* convenient */

	bound.start = sv->cursor.base_corner;
	bound.end = sv->cursor.move_corner;
	range_normalize (&bound);
	sc_cursor_bound (sc, &bound);
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
s_view_finalize (GObject *object)
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

	sv_unant (sv);
	sv_selection_free (sv);
	auto_expr_timer_clear (sv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
sheet_view_class_init (GObjectClass *klass)
{
	SheetViewClass *wbc_class = SHEET_VIEW_CLASS (klass);

	g_return_if_fail (wbc_class != NULL);

	parent_class = g_type_class_peek (G_TYPE_OBJECT);
	klass->finalize = s_view_finalize;
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
	sv->edit_pos_changed.format   = TRUE;
	sv->selection_content_changed = TRUE;
	sv->reposition_selection = TRUE;
	sv->auto_expr_timer = 0;

	sv->frozen_top_left.col = sv->frozen_top_left.row =
	sv->unfrozen_top_left.col = sv->unfrozen_top_left.row = -1;
	sv->initial_top_left.col = sv->initial_top_left.row = 0;

	sv_selection_add_pos (sv, 0, 0);
}

E_MAKE_TYPE (sheet_view, "SheetView", SheetView,
	     sheet_view_class_init, sheet_view_init,
	     G_TYPE_OBJECT);

static void
sv_init_sc (SheetView const *sv, SheetControl *sc)
{
	/* set_panes will change the initial so cache it */
	CellPos initial = sv->initial_top_left;
	sc_set_panes (sc);

	/* And this will restore it */
	sc_set_top_left (sc, initial.col, initial.row);
	sc_scrollbar_config (sc);

	/* Set the visible bound, not the logical bound */
	sc_cursor_bound (sc, selection_first_range (sv, NULL, NULL));
	sc_ant (sc);
}

SheetView *
sheet_view_new (Sheet *sheet, WorkbookView *wbv)
{
	SheetView *sv = g_object_new (SHEET_VIEW_TYPE, NULL);
	sheet_attach_view (sheet, sv);
	sv->wbv = wbv;

	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sv_init_sc (sv, control););
	return sv;
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
sv_redraw_range	(SheetView *sv, Range const *r)
{
	Range tmp = *r;
	if (sv->sheet == NULL) /* beware initialization */
		return;
	sheet_range_bounding_box (sv->sheet, &tmp);
	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sc_redraw_range (control, &tmp););
}

void
sv_redraw_headers (SheetView const *sv,
		   gboolean col, gboolean row,
		   Range const* r /* optional == NULL */)
{
	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sc_redraw_headers (control, col, row, r););
}

gboolean
sv_selection_copy (SheetView *sv, WorkbookControl *wbc)
{
	Range const *sel;

	if (!(sel = selection_first_range (sv, wbc, _("Copy"))))
		return FALSE;

	application_clipboard_cut_copy (wbc, FALSE, sv, sel, TRUE);

	return TRUE;
}

gboolean
sv_selection_cut (SheetView *sv, WorkbookControl *wbc)
{
	Range const *sel;

	/* 'cut' is a poor description of what we're
	 * doing here.  'move' would be a better
	 * approximation.  The key portion of this process is that
	 * the range being moved has all
	 * 	- references to it adjusted to the new site.
	 * 	- relative references from it adjusted.
	 *
	 * NOTE : This command DOES NOT MOVE ANYTHING !
	 *        We only store the src, paste does the move.
	 */
	g_return_val_if_fail (IS_SHEET_VIEW (sv), FALSE);

	if (!(sel = selection_first_range (sv, wbc, _("Cut"))))
		return FALSE;

	if (sheet_range_splits_region (sv_sheet (sv), sel, NULL, wbc, _("Cut")))
		return FALSE;

	application_clipboard_cut_copy (wbc, TRUE, sv, sel, TRUE);

	return TRUE;
}

/**
 * sv_cursor_set :
 * @sv : The sheet
 * @edit_col :
 * @edit_row :
 * @base_col :
 * @base_row :
 * @move_col :
 * @move_row :
 * @bound    : An optionally NULL range that should contain all the supplied points
 */
void
sv_cursor_set (SheetView *sv,
	       CellPos const *edit,
	       int base_col, int base_row,
	       int move_col, int move_row,
	       Range const *bound)
{
	Range r;

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
sv_set_edit_pos (SheetView *sv, CellPos const *pos)
{
	CellPos old;

	g_return_if_fail (IS_SHEET_VIEW (sv));
	g_return_if_fail (pos != NULL);
	g_return_if_fail (pos->col >= 0);
	g_return_if_fail (pos->col < SHEET_MAX_COLS);
	g_return_if_fail (pos->row >= 0);
	g_return_if_fail (pos->row < SHEET_MAX_ROWS);

	old = sv->edit_pos;

	if (old.col != pos->col || old.row != pos->row) {
		Range const *merged = sheet_merge_is_corner (sv->sheet, &old);

		sv->edit_pos_changed.location =
		sv->edit_pos_changed.content =
		sv->edit_pos_changed.format = TRUE;

		/* Redraw before change */
		if (merged == NULL) {
			Range tmp; tmp.start = tmp.end = old;
			sv_redraw_range (sv, &tmp);
		} else
			sv_redraw_range (sv, merged);

		sv->edit_pos_real = *pos;

		/* Redraw after change (handling merged cells) */
		merged = sheet_merge_contains_pos (sv->sheet, &sv->edit_pos_real);
		if (merged == NULL) {
			Range tmp; tmp.start = tmp.end = *pos;
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
sv_flag_status_update_pos (SheetView *sv, CellPos const *pos)
{
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
		sv->edit_pos_changed.format = TRUE;
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
sv_flag_status_update_range (SheetView *sv, Range const *range)
{
	/* Force an update */
	if (range == NULL) {
		sv->selection_content_changed = TRUE;
		sv->edit_pos_changed.location =
		sv->edit_pos_changed.content =
		sv->edit_pos_changed.format = TRUE;
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
		sv->edit_pos_changed.content = sv->edit_pos_changed.format = TRUE;
}

/**
 * sv_flag_format_update_range :
 * @sheet : The sheet being changed
 * @range : the range that is changing.
 *
 * Flag format changes that will require updating the format indicators.
 */
void
sv_flag_format_update_range (SheetView *sv, Range const *range)
{
	if (range_contains (range, sv->edit_pos.col, sv->edit_pos.row))
		sv->edit_pos_changed.format = TRUE;
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
	sv->selection_content_changed = TRUE;
}

void
sv_update (SheetView *sv)
{
	if (sv->edit_pos_changed.content) {
		sv->edit_pos_changed.content = FALSE;
		if (wb_view_cur_sheet_view (sv->wbv) == sv)
			wb_view_edit_line_set (sv->wbv, NULL);
	}

	if (sv->edit_pos_changed.format) {
		sv->edit_pos_changed.format = FALSE;
		if (wb_view_cur_sheet_view (sv->wbv) == sv)
			wb_view_format_feedback (sv->wbv, TRUE);
	}

	if (sv->edit_pos_changed.location) {
		sv->edit_pos_changed.location = FALSE;
		if (wb_view_cur_sheet_view (sv->wbv) == sv) {
			char const *new_pos = cell_pos_name (&sv->edit_pos);
			SHEET_VIEW_FOREACH_CONTROL (sv, sc,
				wb_control_selection_descr_set (sc_wbc (sc), new_pos););
		}
	}

	if (sv->selection_content_changed) {
		int const lag = application_auto_expr_recalc_lag ();
		sv->selection_content_changed = FALSE;
		if (sv->auto_expr_timer == 0 || lag < 0) {
			auto_expr_timer_clear (sv);
			sv->auto_expr_timer = g_timeout_add_full (0, abs (lag), /* seems ok */
				cb_update_auto_expr, (gpointer) sv, NULL);
		}
	}
}

static Value *
fail_if_not_selected (Sheet *sheet, int col, int row, Cell *cell, void *sv)
{
	if (!sv_is_pos_selected (sv, col, row))
		return VALUE_TERMINATE;
	else
		return NULL;
}

/**
 * sheet_is_region_empty_or_selected:
 * @sheet: sheet to check
 * @start_col: starting column
 * @start_row: starting row
 * @end_col:   end column
 * @end_row:   end row
 *
 * Returns TRUE if the specified region of the @sheet does not
 * contain any cells that are not selected.
 *
 * FIXME: Perhaps this routine should be extended to allow testing for specific
 * features of a cell rather than just the existance of the cell.
 */
gboolean
sv_is_region_empty_or_selected (SheetView const *sv, Range const *r)
{
	g_return_val_if_fail (IS_SHEET_VIEW (sv), TRUE);

	return sheet_foreach_cell_in_range (
		sv->sheet, TRUE, r->start.col, r->start.row, r->end.col, r->end.row,
		fail_if_not_selected, (gpointer)sv) == NULL;
}

/**
 * sv_freeze_panes :
 * @sheet    : the sheet
 * @frozen   : top left corner of the frozen region
 * @unfrozen : top left corner of the unfrozen region
 *
 * By definition the unfrozen region must be below the frozen.
 */
void
sv_freeze_panes (SheetView *sv,
		 CellPos const *frozen,
		 CellPos const *unfrozen)
{
	g_return_if_fail (IS_SHEET_VIEW (sv));

	if (frozen != NULL) {
		g_return_if_fail (unfrozen != NULL);
		g_return_if_fail (unfrozen->col > frozen->col);
		g_return_if_fail (unfrozen->row > frozen->row);

		/* Just in case */
		if (unfrozen->col != (SHEET_MAX_COLS-1) &&
		    unfrozen->row != (SHEET_MAX_ROWS-1)) {
			g_return_if_fail (unfrozen->row > frozen->row);
			sv->frozen_top_left = *frozen;
			sv->unfrozen_top_left = *unfrozen;
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

	WORKBOOK_VIEW_FOREACH_CONTROL(sv->wbv, wbc,
		wb_control_menu_state_update (wbc, MS_FREEZE_VS_THAW););
}

gboolean
sv_is_frozen	(SheetView const *sv)
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
	g_return_if_fail (0 <= col && col < SHEET_MAX_COLS);
	g_return_if_fail (0 <= row && row < SHEET_MAX_ROWS);
	g_return_if_fail (!sv_is_frozen (sv) ||
			  (sv->unfrozen_top_left.col <= col &&
			   sv->unfrozen_top_left.row <= row));

	sv->initial_top_left.col = col;
	sv->initial_top_left.row = row;
}
