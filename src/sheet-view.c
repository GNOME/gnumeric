/*
 * sheet-view.c:
 *
 * Copyright (C) 2002-2006 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include <gnumeric.h>

#include <sheet-view.h>
#include <sheet.h>
#include <sheet-merge.h>
#include <sheet-filter.h>
#include <gnm-sheet-slicer.h>
#include <sheet-private.h>
#include <sheet-control.h>
#include <sheet-control-priv.h>
#include <workbook-view.h>
#include <workbook-control.h>
#include <ranges.h>
#include <selection.h>
#include <application.h>
#include <value.h>
#include <parse-util.h>
#include <expr-name.h>
#include <command-context.h>
#include <gnumeric-conf.h>
#include <sheet-style.h>
#include <mstyle.h>
#include <gutils.h>

#include <gsf/gsf-impl-utils.h>

#define GNM_SHEET_VIEW_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNM_SHEET_VIEW_TYPE, SheetViewClass))
static GObjectClass *parent_class;

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
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
	sv->edit_pos_changed.content = TRUE;
}

static void
sv_sheet_visibility_changed (Sheet *sheet,
			     G_GNUC_UNUSED GParamSpec *pspec,
			     SheetView *sv)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
	/* See bug 366477.  */
	if (sheet_is_visible (sheet) && !wb_view_cur_sheet (sv->sv_wbv))
		wb_view_sheet_focus (sv->sv_wbv, sheet);
}

static void
sv_sheet_r1c1_changed (G_GNUC_UNUSED Sheet *sheet,
		       G_GNUC_UNUSED GParamSpec *pspec,
		       SheetView *sv)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
	sv->edit_pos_changed.location = TRUE;
}

/**
 * sv_sheet:
 * @sv: #SheetView
 *
 * Returns: (transfer none): the sheet.
 **/
Sheet *
sv_sheet (SheetView const *sv)
{
	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), NULL);
	return sv->sheet;
}

/**
 * sv_wbv:
 * @sv: #SheetView
 *
 * Returns: (transfer none): the workbook view.
 **/
WorkbookView *
sv_wbv (SheetView const *sv)
{
	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), NULL);
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
gnm_sheet_view_attach_control (SheetView *sv, SheetControl *sc)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
	g_return_if_fail (GNM_IS_SHEET_CONTROL (sc));
	g_return_if_fail (sc->view == NULL);

	g_ptr_array_add (sv->controls, sc);
	sc->view  = sv;
	sv_init_sc (sv, sc);
}

void
gnm_sheet_view_detach_control (SheetView *sv, SheetControl *sc)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
	g_return_if_fail (GNM_IS_SHEET_CONTROL (sc));
	g_return_if_fail (sv == sc->view);

	g_ptr_array_remove (sv->controls, sc);
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
gnm_sheet_view_weak_ref (SheetView *sv, SheetView **ptr)
{
	g_return_if_fail (ptr != NULL);

	*ptr = sv;
	if (sv != NULL)
		g_object_weak_ref (G_OBJECT (sv),
			(GWeakNotify) sv_weakref_notify,
			ptr);
}

void
gnm_sheet_view_weak_unref (SheetView **ptr)
{
	g_return_if_fail (ptr != NULL);

	if (*ptr != NULL) {
		g_object_weak_unref (G_OBJECT (*ptr),
			(GWeakNotify) sv_weakref_notify,
			ptr);
		*ptr = NULL;
	}
}

static void
sv_finalize (GObject *object)
{
	SheetView *sv = GNM_SHEET_VIEW (object);
	g_ptr_array_free (sv->controls, TRUE);
	parent_class->finalize (object);
}

static void
sv_real_dispose (GObject *object)
{
	SheetView *sv = GNM_SHEET_VIEW (object);

	while (sv->controls->len > 0) {
		SheetControl *control =
			g_ptr_array_index (sv->controls,
					   sv->controls->len - 1);
		gnm_sheet_view_detach_control (sv, control);
		g_object_unref (control);
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

	gnm_sheet_view_unant (sv);
	sv_selection_free (sv);
	sv_selection_simplified_free (sv);
	auto_expr_timer_clear (sv);

	parent_class->dispose (object);
}

static void
gnm_sheet_view_class_init (GObjectClass *klass)
{
	SheetViewClass *wbc_class = GNM_SHEET_VIEW_CLASS (klass);

	g_return_if_fail (wbc_class != NULL);

	parent_class = g_type_class_peek_parent (klass);
	klass->dispose = sv_real_dispose;
	klass->finalize = sv_finalize;
}

static void
gnm_sheet_view_init (GObject *object)
{
	SheetView *sv = GNM_SHEET_VIEW (object);

	sv->controls = g_ptr_array_new ();

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

	sv->selections = NULL;
	sv->selection_mode = GNM_SELECTION_MODE_ADD;
	sv->selections_simplified = NULL;
	sv_selection_add_pos (sv, 0, 0, GNM_SELECTION_MODE_ADD);
}

GSF_CLASS (SheetView, gnm_sheet_view,
	   gnm_sheet_view_class_init, gnm_sheet_view_init,
	   G_TYPE_OBJECT)

SheetView *
gnm_sheet_view_new (Sheet *sheet, WorkbookView *wbv)
{
	SheetView *sv;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	sv = g_object_new (GNM_SHEET_VIEW_TYPE, NULL);
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
gnm_sheet_view_dispose (SheetView *sv)
{
	g_object_run_dispose (G_OBJECT (sv));
}

void
gnm_sheet_view_unant (SheetView *sv)
{
	GList *ptr;

	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

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
 * gnm_sheet_view_ant:
 * @sv:
 * @ranges: (element-type GnmRange) (transfer none): The ranges to ant.
 */
void
gnm_sheet_view_ant (SheetView *sv, GList *ranges)
{
	GList *ptr;

	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
	g_return_if_fail (ranges != NULL);

	if (sv->ants != NULL)
		gnm_sheet_view_unant (sv);
	for (ptr = ranges; ptr != NULL; ptr = ptr->next)
		sv->ants = g_list_prepend (sv->ants, gnm_range_dup (ptr->data));
	sv->ants = g_list_reverse (sv->ants);

	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sc_ant (control););
}

void
gnm_sheet_view_make_cell_visible (SheetView *sv, int col, int row,
		      gboolean couple_panes)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
	SHEET_VIEW_FOREACH_CONTROL(sv, control,
		sc_make_cell_visible (control, col, row, couple_panes););
}

void
gnm_sheet_view_redraw_range	(SheetView *sv, GnmRange const *r)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

	SHEET_VIEW_FOREACH_CONTROL (sv, sc, sc_redraw_range (sc, r););
}

void
gnm_sheet_view_redraw_headers (SheetView const *sv,
		   gboolean col, gboolean row,
		   GnmRange const* r /* optional == NULL */)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sc_redraw_headers (control, col, row, r););
}

void
gnm_sheet_view_resize (SheetView *sv, gboolean force_scroll)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

	SHEET_VIEW_FOREACH_CONTROL (sv, control,
		sc_resize (control, force_scroll););
}


gboolean
gnm_sheet_view_selection_copy (SheetView *sv, WorkbookControl *wbc)
{
	GnmRange const *sel;

	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), FALSE);
	if (!(sel = selection_first_range (sv, GO_CMD_CONTEXT (wbc), _("Copy"))))
		return FALSE;

	gnm_app_clipboard_cut_copy (wbc, FALSE, sv, sel, TRUE);

	return TRUE;
}

gboolean
gnm_sheet_view_selection_cut (SheetView *sv, WorkbookControl *wbc)
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
	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), FALSE);

	if (!(sel = selection_first_range (sv, GO_CMD_CONTEXT (wbc), _("Cut"))))
		return FALSE;

	if (sheet_range_splits_region (sv_sheet (sv), sel, NULL, GO_CMD_CONTEXT (wbc), _("Cut")))
		return FALSE;

	gnm_app_clipboard_cut_copy (wbc, TRUE, sv, sel, TRUE);

	return TRUE;
}

/**
 * gnm_sheet_view_cursor_set:
 * @sv: The sheet
 * @edit:
 * @base_col:
 * @base_row:
 * @move_col:
 * @move_row:
 * @bound: (nullable): A range that should contain all the supplied points
 **/
void
gnm_sheet_view_cursor_set (SheetView *sv,
	       GnmCellPos const *edit,
	       int base_col, int base_row,
	       int move_col, int move_row,
	       GnmRange const *bound)
{
	GnmRange r;

	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

	/* Change the edit position */
	gnm_sheet_view_set_edit_pos (sv, edit);

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
gnm_sheet_view_set_edit_pos (SheetView *sv, GnmCellPos const *pos)
{
	GnmCellPos old;
	GnmRange const *merged;

	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
	g_return_if_fail (pos != NULL);

	old = sv->edit_pos;
	sv->first_tab_col = -1; /* invalidate */

	if (old.col == pos->col && old.row == pos->row)
		return;

	g_return_if_fail (IS_SHEET (sv->sheet));
	g_return_if_fail (pos->col >= 0);
	g_return_if_fail (pos->col < gnm_sheet_get_max_cols (sv->sheet));
	g_return_if_fail (pos->row >= 0);
	g_return_if_fail (pos->row < gnm_sheet_get_max_rows (sv->sheet));


	merged = gnm_sheet_merge_is_corner (sv->sheet, &old);

	sv->edit_pos_changed.location =
		sv->edit_pos_changed.content =
		sv->edit_pos_changed.style  = TRUE;

	/* Redraw before change */
	if (merged == NULL) {
		GnmRange tmp; tmp.start = tmp.end = old;
		gnm_sheet_view_redraw_range (sv, &tmp);
	} else
		gnm_sheet_view_redraw_range (sv, merged);

	sv->edit_pos_real = *pos;

	/* Redraw after change (handling merged cells) */
	merged = gnm_sheet_merge_contains_pos (sv->sheet, &sv->edit_pos_real);
	if (merged == NULL) {
		GnmRange tmp; tmp.start = tmp.end = *pos;
		gnm_sheet_view_redraw_range (sv, &tmp);
		sv->edit_pos = sv->edit_pos_real;
	} else {
		gnm_sheet_view_redraw_range (sv, merged);
		sv->edit_pos = merged->start;
	}
}

/**
 * gnm_sheet_view_flag_status_update_pos:
 * @sv:
 * @pos:
 *
 *    flag the view as requiring an update to the status display
 *    if the supplied cell location is the edit cursor, or part of the
 *    selected region.
 *
 * Will cause the format toolbar, the edit area, and the auto expressions to be
 * updated if appropriate.
 */
void
gnm_sheet_view_flag_status_update_pos (SheetView *sv, GnmCellPos const *pos)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
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
 * gnm_sheet_view_flag_status_update_range:
 * @sv:
 * @range: (nullable): If %NULL then force an update.
 *
 * flag the sheet as requiring an update to the status display if the supplied
 * cell location contains the edit cursor, or intersects of the selected region.
 *
 * Will cause the format toolbar, the edit area, and the auto expressions to be
 * updated if appropriate.
 */
void
gnm_sheet_view_flag_status_update_range (SheetView *sv, GnmRange const *range)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

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
 * gnm_sheet_view_flag_style_update_range:
 * @sv: The sheet being changed
 * @range: the range that is changing.
 *
 * Flag style changes that will require updating the style  indicators.
 */
void
gnm_sheet_view_flag_style_update_range (SheetView *sv, GnmRange const *range)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
	g_return_if_fail (range != NULL);
	if (range_contains (range, sv->edit_pos.col, sv->edit_pos.row))
		sv->edit_pos_changed.style  = TRUE;
}

/**
 * gnm_sheet_view_flag_selection_change:
 * @sv:
 *
 * flag the sheet as requiring an update to the status display
 *
 * Will cause auto expressions to be updated
 */
void
gnm_sheet_view_flag_selection_change (SheetView *sv)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
	sv->selection_content_changed = TRUE;
}

static void
sheet_view_edit_pos_tool_tips (SheetView *sv)
{
	GnmStyle const *style;
	GnmInputMsg	*im = NULL;

	style = sheet_style_get (sv->sheet,
				 sv->edit_pos.col,
				 sv->edit_pos.row);
	if (style != NULL && gnm_style_is_element_set (style, MSTYLE_INPUT_MSG))
		im = gnm_style_get_input_msg (style);

	/* We need to call these even with im == NULL to remove the old tooltip.*/
	SHEET_VIEW_FOREACH_CONTROL (sv, control,
				    sc_show_im_tooltip (control, im, &sv->edit_pos););
}

void
gnm_sheet_view_update (SheetView *sv)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

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
		if (wb_view_cur_sheet_view (sv->sv_wbv) == sv) {
			wb_view_selection_desc (sv->sv_wbv, TRUE, NULL);
			SHEET_VIEW_FOREACH_CONTROL
				(sv, sc, wb_control_menu_state_update
				 (sc_wbc (sc),
				  MS_COMMENT_LINKS | MS_PAGE_BREAKS););
			sheet_view_edit_pos_tool_tips (sv);
		}
	}

	if (sv->selection_content_changed) {
		int const lag = gnm_conf_get_core_gui_editing_recalclag ();
		sv->selection_content_changed = FALSE;
		if (sv->auto_expr_timer == 0 || lag < 0) {
			auto_expr_timer_clear (sv);
			sv->auto_expr_timer = g_timeout_add_full (0, abs (lag), /* seems ok */
				cb_update_auto_expr, (gpointer) sv, NULL);
		}
		SHEET_VIEW_FOREACH_CONTROL (sv, sc,
			wb_control_menu_state_update (sc_wbc (sc), MS_ADD_VS_REMOVE_FILTER |
						      MS_COMMENT_LINKS_RANGE););
	}

	SHEET_VIEW_FOREACH_CONTROL (sv, sc,
				    wb_control_menu_state_update
				    (sc_wbc (sc), MS_SELECT_OBJECT););

}

/**
 * gnm_sheet_view_editpos_in_filter:
 * @sv: #SheetView
 *
 * Returns: (nullable): #GnmFilter that overlaps the sv::edit_pos
 **/
GnmFilter *
gnm_sheet_view_editpos_in_filter (SheetView const *sv)
{
	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), NULL);
	return gnm_sheet_filter_at_pos (sv->sheet, &sv->edit_pos);
}

/**
 * gnm_sheet_view_selection_intersects_filter_rows:
 * @sv: #SheetView
 *
 * Returns: (nullable): #GnmFilter whose rows intersect the rows
 *          of the current selection.
 **/
GnmFilter *
gnm_sheet_view_selection_intersects_filter_rows (SheetView const *sv)
{
	GnmRange const *r;
	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), NULL);
	r = selection_first_range (sv, NULL, NULL);

	return r ? gnm_sheet_filter_intersect_rows
	  (sv->sheet, r->start.row, r->end.row) : NULL;
}

/**
 * gnm_sheet_view_selection_extends_filter:
 * @sv: #SheetView
 *
 * Returns: (nullable): #GnmFilter whose rows intersect the rows
 *          of the current selection range to which the filter can be
 *          extended.
 **/
GnmRange *
gnm_sheet_view_selection_extends_filter (SheetView const *sv,
					 GnmFilter const *f)
{
	GnmRange const *r;
	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), NULL);
	r = selection_first_range (sv, NULL, NULL);

	return gnm_sheet_filter_can_be_extended (sv->sheet, f, r);
}




/**
 * gnm_sheet_view_editpos_in_slicer:
 * @sv: #SheetView
 *
 * Returns: (transfer none) (nullable): #GnmSheetSlicer that overlaps the
 * sv::edit_pos
 **/
GnmSheetSlicer *
gnm_sheet_view_editpos_in_slicer (SheetView const *sv)
{
	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), NULL);
	return gnm_sheet_slicers_at_pos (sv->sheet, &sv->edit_pos);
}

/**
 * gnm_sheet_view_freeze_panes:
 * @sv: the sheet
 * @frozen_top_left: (nullable): top left corner of the frozen region
 * @unfrozen_top_left: (nullable): top left corner of the unfrozen region
 *
 * By definition the unfrozen region must be below the frozen.
 * If @frozen_top_left == @unfrozen_top_left or @frozen_top_left == %NULL unfreeze
 **/
void
gnm_sheet_view_freeze_panes (SheetView *sv,
			     GnmCellPos const *frozen,
			     GnmCellPos const *unfrozen)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

	if (gnm_debug_flag ("frozen-panes")) {
		g_printerr ("Frozen: %-10s",
			    frozen ? cellpos_as_string (frozen) : "-");
		g_printerr ("Unfrozen: %s\n",
			    unfrozen ? cellpos_as_string (unfrozen) : "-");
	}

	if (frozen != NULL) {
		g_return_if_fail (unfrozen != NULL);
		g_return_if_fail (unfrozen->col >= frozen->col);
		g_return_if_fail (unfrozen->row >= frozen->row);

		/* Just in case */
		if (unfrozen->col != gnm_sheet_get_last_col (sv->sheet) &&
		    unfrozen->row != gnm_sheet_get_last_row (sv->sheet) &&
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
 * gnm_sheet_view_panes_insdel_colrow:
 * @sv:
 * @is_cols: %TRUE for columns, %FALSE for rows.
 * @is_insert:
 * @start:
 * @count:
 *
 * Adjust the positions of frozen panes as necessary to handle col/row
 * insertions and deletions.  note this assumes that the ins/del operations
 * have already set the flags that will force a resize.
 **/
void
gnm_sheet_view_panes_insdel_colrow (SheetView *sv, gboolean is_cols,
				    gboolean is_insert, int start, int count)
{
	GnmCellPos tl;
	GnmCellPos br;

	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

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
	gnm_sheet_view_freeze_panes (sv, &tl, &br);
}

gboolean
gnm_sheet_view_is_frozen (SheetView const *sv)
{
	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), FALSE);

	/* be flexible, in the future we will support 2 way splits too */
	return  sv->unfrozen_top_left.col >= 0 ||
		sv->unfrozen_top_left.row >= 0;
}

/**
  gnm_sheet_view_set_initial_top_left:
 * @sv: the sheet view.
 * @col:
 * @row:
 *
 * Sets the top left cell that a newly created sheet control should display.
 * This corresponds to the top left cell visible in pane 0 (frozen or not).
 * NOTE : the unfrozen_top_left != initial_top_left.  Unfrozen is the first
 * unfrozen cell, and corresponds to the _minimum_ cell in pane 0.  However,
 * the pane can scroll and may have something else currently visible as the top
 * left.
 */
void
gnm_sheet_view_set_initial_top_left (SheetView *sv, int col, int row)
{
	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));
	g_return_if_fail (0 <= col && col < gnm_sheet_get_max_cols (sv->sheet));
	g_return_if_fail (0 <= row && row < gnm_sheet_get_max_rows (sv->sheet));
	g_return_if_fail (!gnm_sheet_view_is_frozen (sv) ||
			  (sv->unfrozen_top_left.col <= col &&
			   sv->unfrozen_top_left.row <= row));

	sv->initial_top_left.col = col;
	sv->initial_top_left.row = row;
}
