/*
 * sheet-control-gui.c: Implements a graphic control for a sheet.
 *
 * Copyright (C) 2000-2006 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 1997-1999 Miguel de Icaza (miguel@kernel.org)
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
#include <gnumeric.h>
#include <sheet-control-gui-priv.h>

#include <sheet.h>
#include <sheet-private.h>
#include <sheet-view.h>
#include <sheet-merge.h>
#include <workbook.h>
#include <workbook-view.h>
#include <workbook-cmd-format.h>
#include <wbc-gtk-impl.h>
#include <cell.h>
#include <selection.h>
#include <style.h>
#include <sheet-style.h>
#include <sheet-object-impl.h>
#include <sheet-object-cell-comment.h>
#include <sheet-object-image.h>
#include <gui-util.h>
#include <gutils.h>
#include <parse-util.h>
#include <selection.h>
#include <application.h>
#include <cellspan.h>
#include <cmd-edit.h>
#include <commands.h>
#include <gnm-commands-slicer.h>
#include <clipboard.h>
#include <dialogs/dialogs.h>
#include <gui-file.h>
#include <sheet-merge.h>
#include <ranges.h>
#include <xml-sax.h>
#include <style-color.h>
#include <gnumeric-conf.h>

#include <gnm-pane-impl.h>
#include <item-bar.h>
#include <item-cursor.h>
#include <widgets/gnm-expr-entry.h>
#include <gnm-sheet-slicer.h>
#include <input-msg.h>

#include <go-data-slicer-field.h>
#include <goffice/goffice.h>

#include <gdk/gdkkeysyms.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-output-memory.h>

#include <string.h>

static GObjectClass *scg_parent_class;

static void scg_unant (SheetControl *sc);
static void set_resize_pane_pos (SheetControlGUI *scg, GtkPaned *p);
static void cb_resize_pane_motion (GtkPaned *p, GParamSpec *pspec, SheetControlGUI *scg);


/**
 * scg_pane:
 * @scg: #SheetControlGUI
 * @pane: the pane index.
 *
 * Returns: (transfer none): the pane.
 **/
GnmPane *
scg_pane (SheetControlGUI *scg, int p)
{
	/* it is ok to request a pane when we are not frozen */
	g_return_val_if_fail (GNM_IS_SCG (scg), NULL);
	g_return_val_if_fail (p >= 0, NULL);
	g_return_val_if_fail (p < 4, NULL);

	return scg->pane[p];
}

/**
 * scg_view:
 * @scg: #SheetControlGUI
 *
 * Returns: (transfer none): the sheet view.
 **/
SheetView *
scg_view (SheetControlGUI const *scg)
{
	g_return_val_if_fail (GNM_IS_SCG (scg), NULL);
	return scg->sheet_control.view;
}


/**
 * scg_sheet:
 * @scg: #SheetControlGUI
 *
 * Returns: (transfer none): the sheet.
 **/
Sheet *
scg_sheet (SheetControlGUI const *scg)
{
	return sc_sheet ((SheetControl *)scg);
}


/**
 * scg_wbc:
 * @scg: #SheetControlGUI
 *
 * Returns: (transfer none): the workbook control.
 **/
WorkbookControl *
scg_wbc (SheetControlGUI const *scg)
{
	g_return_val_if_fail (GNM_IS_SCG (scg), NULL);
	return scg->sheet_control.wbc;
}


/**
 * scg_wbcg:
 * @scg: #SheetControlGUI
 *
 * Returns: (transfer none): the #WBCGtk.
 **/

WBCGtk *
scg_wbcg (SheetControlGUI const *scg)
{
	g_return_val_if_fail (GNM_IS_SCG (scg), NULL);
	return scg->wbcg;
}

static void
scg_redraw_all (SheetControl *sc, gboolean headers)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;

	g_return_if_fail (GNM_IS_SCG (scg));

	SCG_FOREACH_PANE (scg, pane, {
		goc_canvas_invalidate (GOC_CANVAS (pane),
			G_MININT64, 0, G_MAXINT64, G_MAXINT64);
		if (headers) {
			if (NULL != pane->col.canvas)
				goc_canvas_invalidate (pane->col.canvas,
					0, 0, G_MAXINT64, G_MAXINT64);
			if (NULL != pane->row.canvas)
				goc_canvas_invalidate (pane->row.canvas,
					0, 0, G_MAXINT64, G_MAXINT64);
		}
	});
}

static void
scg_redraw_range (SheetControl *sc, GnmRange const *r)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	Sheet const *sheet = scg_sheet (scg);
	GnmRange visible, area;

	/*
	 * Getting the bounding box causes row respans to be done if
	 * needed.  That can be expensive, so just redraw the whole
	 * sheet if the row count is too big.
	 */
	if (r->end.row - r->start.row > 500) {
		scg_redraw_all (sc, FALSE);
		return;
	}

	/* We potentially do a lot of recalcs as part of this, so make sure
	   stuff that caches sub-computations see the whole thing instead
	   of clearing between cells.  */
	gnm_app_recalc_start ();

	SCG_FOREACH_PANE (scg, pane, {
		visible.start = pane->first;
		visible.end = pane->last_visible;

		if (range_intersection (&area, r, &visible)) {
			sheet_range_bounding_box (sheet, &area);
			gnm_pane_redraw_range (pane, &area);
		}
	};);

	gnm_app_recalc_finish ();
}

static void
scg_redraw_headers (SheetControl *sc,
		    gboolean const col, gboolean const row,
		    GnmRange const * r /* optional == NULL */)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	GnmPane *pane;
	int i;
	double scale;

	/*
	 * A rough guess of the trade off point between of redrawing all
	 * and calculating the redraw size
	 */
	const int COL_HEURISTIC = 20;
	const int ROW_HEURISTIC = 50;

	for (i = scg->active_panes; i-- > 0 ; ) {
		if (NULL == (pane = scg->pane[i]))
			continue;

		if (col && pane->col.canvas != NULL) {
			int left = 0, right = G_MAXINT - 1;
			GocCanvas * const col_canvas = GOC_CANVAS (pane->col.canvas);
			scale = goc_canvas_get_pixels_per_unit (col_canvas);

			if (r != NULL) {
				int const size = r->end.col - r->start.col;
				if (-COL_HEURISTIC < size && size < COL_HEURISTIC) {
					left = pane->first_offset.x +
						scg_colrow_distance_get (scg, TRUE,
									 pane->first.col, r->start.col);
					right = left +
						scg_colrow_distance_get (scg, TRUE,
									 r->start.col, r->end.col+1);
				}
			}
			goc_canvas_invalidate (col_canvas,
				left / scale, 0, right / scale, G_MAXINT64);
		}

		if (row && pane->row.canvas != NULL) {
			gint64 top = 0, bottom = G_MAXINT64 - 1;
			scale = goc_canvas_get_pixels_per_unit (pane->row.canvas);
			if (r != NULL) {
				int const size = r->end.row - r->start.row;
				if (-ROW_HEURISTIC < size && size < ROW_HEURISTIC) {
					top = pane->first_offset.y +
						scg_colrow_distance_get (scg, FALSE,
									 pane->first.row, r->start.row);
					bottom = top +
						scg_colrow_distance_get (scg, FALSE,
									 r->start.row, r->end.row+1);
				}
			}
			goc_canvas_invalidate (GOC_CANVAS (pane->row.canvas),
				0, top / scale, G_MAXINT64, bottom / scale);
		}
	}
}

static void
cb_outline_button (GtkWidget *btn, SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	WorkbookControl *wbc = sc->wbc;
	GPtrArray const *btns;
	unsigned i = 0;
	gboolean is_cols = g_object_get_data (G_OBJECT (btn), "is_cols") != NULL;

	/* which button */
	btns = is_cols ? scg->col_group.buttons : scg->row_group.buttons;
	for (i = 0; i < btns->len; i++)
		if (g_ptr_array_index (btns, i) == btn)
			break;

	g_return_if_fail (i < btns->len);

	cmd_global_outline_change (wbc, is_cols, i+1);
}

static void
scg_setup_group_buttons (SheetControlGUI *scg, unsigned max_outline,
			 GnmItemBar const *ib, gboolean is_cols, int w, int h,
			 GPtrArray *btns, GtkWidget *box)
{
	PangoFontDescription *font_desc;
	unsigned i;
	Sheet const *sheet = scg_sheet (scg);

	if (!sheet->display_outlines)
		max_outline = 0;
	else if (max_outline > 0)
		max_outline++;

	while (btns->len > max_outline) {
		GtkWidget *w = g_ptr_array_remove_index_fast (btns, btns->len - 1);
		gtk_container_remove (GTK_CONTAINER (box),
			gtk_widget_get_parent (w));
	}

	while (btns->len < max_outline) {
		GtkWidget *out = gtk_alignment_new (.5, .5, 1., 1.);
		GtkWidget *in  = gtk_alignment_new (.5, .5, 0., 0.);
		GtkWidget *btn = gtk_button_new ();
		char *tmp = g_strdup_printf ("<small>%d</small>", btns->len+1);
		GtkWidget *label = gtk_label_new (NULL);
		gtk_label_set_markup (GTK_LABEL (label), tmp);
		g_free (tmp);

		gtk_widget_set_can_focus (btn, FALSE);
		gtk_container_add (GTK_CONTAINER (in), label);
		gtk_container_add (GTK_CONTAINER (btn), in);
		gtk_container_add (GTK_CONTAINER (out), btn);
		gtk_box_pack_start (GTK_BOX (box), out, TRUE, TRUE, 0);
		g_ptr_array_add (btns, btn);

		g_signal_connect (G_OBJECT (btn),
			"clicked",
			G_CALLBACK (cb_outline_button), scg);
		if (is_cols)
			g_object_set_data (G_OBJECT (btn),
					   "is_cols", GINT_TO_POINTER (1));
	}

	font_desc = item_bar_normal_font (ib);

	/* size all of the button so things work after a zoom */
	for (i = 0 ; i < btns->len ; i++) {
		GtkWidget *btn = g_ptr_array_index (btns, i);
		GtkWidget *label = gtk_bin_get_child (GTK_BIN (gtk_bin_get_child (GTK_BIN (btn))));
		gtk_widget_set_size_request (GTK_WIDGET (btn), w, h);
		gtk_widget_override_font (label, font_desc);
	}

	pango_font_description_free (font_desc);
	gtk_widget_show_all (box);
}

static void
scg_resize (SheetControlGUI *scg, G_GNUC_UNUSED gboolean force_scroll)
{
	Sheet const *sheet = scg_sheet (scg);
	GnmPane *pane = scg_pane (scg, 0);
	int h, w, btn_h, btn_w, tmp;

	if (!pane)
		return;

	/* Recalibrate the starting offsets */
	pane->first_offset.x = scg_colrow_distance_get (scg,
		TRUE, 0, pane->first.col);
	pane->first_offset.y = scg_colrow_distance_get (scg,
		FALSE, 0, pane->first.row);

	/* resize Pane[0] headers */
	h = gnm_item_bar_calc_size (scg->pane[0]->col.item);
	btn_h = h - gnm_item_bar_indent (scg->pane[0]->col.item);
	w = gnm_item_bar_calc_size (scg->pane[0]->row.item);
	btn_w = w - gnm_item_bar_indent (scg->pane[0]->row.item);
	gtk_widget_set_size_request (scg->select_all_btn, btn_w, btn_h);
	gtk_widget_set_size_request (GTK_WIDGET (scg->pane[0]->col.canvas), -1, h);
	gtk_widget_set_size_request (GTK_WIDGET (scg->pane[0]->row.canvas), w, -1);

	tmp = gnm_item_bar_group_size (scg->pane[0]->col.item,
				       sheet->cols.max_outline_level);
	scg_setup_group_buttons (scg, sheet->cols.max_outline_level,
				 scg->pane[0]->col.item, TRUE,
				 tmp, tmp, scg->col_group.buttons, scg->col_group.button_box);
	scg_setup_group_buttons (scg, sheet->rows.max_outline_level,
				 scg->pane[0]->row.item, FALSE,
				 -1, btn_h, scg->row_group.buttons, scg->row_group.button_box);

	if (scg->active_panes != 1 && gnm_sheet_view_is_frozen (scg_view (scg))) {
		GnmCellPos const *tl = &scg_view (scg)->frozen_top_left;
		GnmCellPos const *br = &scg_view (scg)->unfrozen_top_left;
		int const l = scg_colrow_distance_get (scg, TRUE,
						       0, tl->col);
		int const r = scg_colrow_distance_get (scg, TRUE,
						       tl->col, br->col) + l;
		int const t = scg_colrow_distance_get (scg, FALSE,
						       0, tl->row);
		int const b = scg_colrow_distance_get (scg, FALSE,
						       tl->row, br->row) + t;
		int i;
		int fw = MIN (scg->screen_width, r - l);
		int fh = MIN (scg->screen_height, b - t);

		/* pane 0 has already been done */
		for (i = scg->active_panes; i-- > 1 ; ) {
			GnmPane *pane = scg->pane[i];
			if (NULL != pane) {
				pane->first_offset.x = scg_colrow_distance_get (
					scg, TRUE, 0, pane->first.col);
				pane->first_offset.y = scg_colrow_distance_get (
					scg, FALSE, 0, pane->first.row);
			}
		}

		if (scg->pane[1]) {
			if (gnm_debug_flag ("frozen-panes"))
				g_printerr ("Pane 1: %d\n", r - l);

			gtk_widget_set_size_request (GTK_WIDGET (scg->pane[1]), fw, -1);
			/* The item_bar_calcs should be equal */
			/* FIXME : The canvas gets confused when the initial scroll
			 * region is set too early in its life cycle.
			 * It likes it to be at the origin, we can live with that for now.
			 * However, we really should track the bug eventually.
			 */
			h = gnm_item_bar_calc_size (scg->pane[1]->col.item);
			gtk_widget_set_size_request (GTK_WIDGET (scg->pane[1]->col.canvas), fw, h);
		}

		if (scg->pane[3]) {
			if (gnm_debug_flag ("frozen-panes"))
				g_printerr ("Pane 2: %d\n", b - t);

			gtk_widget_set_size_request (GTK_WIDGET (scg->pane[3]), -1, fh);
			/* The item_bar_calcs should be equal */
			w = gnm_item_bar_calc_size (scg->pane[3]->row.item);
			gtk_widget_set_size_request (GTK_WIDGET (scg->pane[3]->row.canvas), w, fh);
		}

		if (scg->pane[2]) {
			if (gnm_debug_flag ("frozen-panes"))
				g_printerr ("Pane 3: %d %d\n", r - l, b - t);

			gtk_widget_set_size_request (GTK_WIDGET (scg->pane[2]), fw, fh);
		}
	}

	SCG_FOREACH_PANE (scg, pane, {
			gnm_pane_reposition_cursors (pane);
	});
}

static void
scg_resize_virt (SheetControl *sc, gboolean force_scroll)
{
	scg_resize ((SheetControlGUI *)sc, force_scroll);
}

static void
gnm_adjustment_configure (GtkAdjustment *adjustment,
                          gdouble        value,
                          gdouble        lower,
                          gdouble        upper,
                          gdouble        step_increment,
                          gdouble        page_increment,
                          gdouble        page_size)
{
	g_object_freeze_notify (G_OBJECT (adjustment));

	// These do nothing if value isn't changed
	gtk_adjustment_set_lower (adjustment, lower);
	gtk_adjustment_set_upper (adjustment, upper);
	gtk_adjustment_set_step_increment (adjustment, step_increment);
	gtk_adjustment_set_page_increment (adjustment, page_increment);
	gtk_adjustment_set_page_size (adjustment, page_size);

	g_object_thaw_notify (G_OBJECT (adjustment));

	// These fire signals if nothing changes, so check by hand
	if (!(gtk_adjustment_get_value (adjustment) == value))
		gtk_adjustment_set_value (adjustment, value);

}

/**
 * scg_scrollbar_config:
 * @sc:
 *
 * Manages the scrollbar dimensions and paging parameters.
 * Currently sizes things based on the cols/rows visible in pane-0.  This has
 * several subtleties.
 *
 * 1) Using cols/rows instead of pixels means that the scrollbar changes size
 * as it passes through regions with different sized cols/rows.
 *
 * 2) It does NOT take into account hidden rows/cols  So a region that contains
 * a large hidden segment will appear larger.
 *
 * 3) It only uses pane-0 because that is the only one that can scroll in both
 * dimensions.  The others are of fixed size.
 */
static gboolean
scg_scrollbar_config_real (SheetControl const *sc)
{
	SheetControlGUI *scg = GNM_SCG (sc);
	GtkAdjustment *va = scg->va;
	GtkAdjustment *ha = scg->ha;
	GnmPane *pane = scg_pane (scg, 0);
	SheetView const *sv = sc->view;
	Sheet const *sheet = sv->sheet;

	if (pane) {
		int const last_col = pane->last_full.col;
		int const last_row = pane->last_full.row;
		int max_col = last_col;
		int max_row = last_row;

		if (max_row < sheet->rows.max_used)
			max_row = sheet->rows.max_used;
		if (max_row < sheet->max_object_extent.row)
			max_row = sheet->max_object_extent.row;
		gnm_adjustment_configure
			(va,
			 pane->first.row,
			 gnm_sheet_view_is_frozen (sv) ? sv->unfrozen_top_left.row : 0,
			 max_row + 1,
			 1,
			 MAX (gtk_adjustment_get_page_size (va) - 3.0, 1.0),
			 last_row - pane->first.row + 1);

		if (max_col < sheet->cols.max_used)
			max_col = sheet->cols.max_used;
		if (max_col < sheet->max_object_extent.col)
			max_col = sheet->max_object_extent.col;
		gnm_adjustment_configure
			(ha,
			 pane->first.col,
			 gnm_sheet_view_is_frozen (sv) ? sv->unfrozen_top_left.col : 0,
			 max_col + 1,
			 1,
			 MAX (gtk_adjustment_get_page_size (ha) - 3.0, 1.0),
			 last_col - pane->first.col + 1);
	}

	scg->scroll_bar_timer = 0;
	return FALSE;
}


static void
scg_scrollbar_config (SheetControl *sc)
{
	SheetControlGUI *scg = GNM_SCG (sc);
	/* See bug 789412 */
	if (!scg->scroll_bar_timer)
		scg->scroll_bar_timer =
			g_timeout_add (1,
				       (GSourceFunc) scg_scrollbar_config_real,
				       scg);
}

void
scg_colrow_size_set (SheetControlGUI *scg,
		     gboolean is_cols, int index, int new_size_pixels)
{
	WorkbookControl *wbc = scg_wbc (scg);
	SheetView *sv = scg_view (scg);

	/* If all cols/rows in the selection are completely selected
	 * then resize all of them, otherwise just resize the selected col/row.
	 */
	if (!sv_is_full_colrow_selected (sv, is_cols, index))
		cmd_resize_colrow (wbc, sv->sheet, is_cols,
			colrow_get_index_list (index, index, NULL), new_size_pixels);
	else
		workbook_cmd_resize_selected_colrow (wbc, sv->sheet, is_cols,
			new_size_pixels);
}

void
scg_select_all (SheetControlGUI *scg)
{
	Sheet *sheet = scg_sheet (scg);
	gboolean const rangesel = wbcg_rangesel_possible (scg->wbcg);

	if (rangesel) {
		scg_rangesel_bound (scg,
			0, 0, gnm_sheet_get_last_col (sheet), gnm_sheet_get_last_row (sheet));
		gnm_expr_entry_signal_update (
			wbcg_get_entry_logical (scg->wbcg), TRUE);
	} else if (wbc_gtk_get_guru (scg->wbcg) == NULL) {
		SheetView *sv = scg_view (scg);

		scg_mode_edit (scg);
		wbcg_edit_finish (scg->wbcg, WBC_EDIT_REJECT, NULL);
		sv_selection_reset (sv);
		sv_selection_add_full (sv, sv->edit_pos.col, sv->edit_pos.row,
				       0, 0, gnm_sheet_get_last_col (sheet),
				       gnm_sheet_get_last_row (sheet),
				       GNM_SELECTION_MODE_ADD);
	}
	sheet_update (sheet);
}

gboolean
scg_colrow_select (SheetControlGUI *scg, gboolean is_cols,
		   int index, int modifiers)
{
	SheetView *sv = scg_view (scg);
	gboolean const rangesel = wbcg_rangesel_possible (scg->wbcg);

	if (!rangesel &&
	    !wbcg_edit_finish (scg->wbcg, WBC_EDIT_ACCEPT, NULL))
		return FALSE;

	if (modifiers & GDK_SHIFT_MASK) {
		if (rangesel) {
			if (is_cols)
				scg_rangesel_extend_to (scg, index, -1);
			else
				scg_rangesel_extend_to (scg, -1, index);
		} else {
			if (is_cols)
				sv_selection_extend_to (sv, index, -1);
			else
				sv_selection_extend_to (sv, -1, index);
		}
	} else {
		if (!rangesel && !(modifiers & GDK_CONTROL_MASK))
			sv_selection_reset (sv);

		if (rangesel) {
			if (is_cols)
				scg_rangesel_bound (scg,
					index, 0, index, gnm_sheet_get_last_row (sv->sheet));
			else
				scg_rangesel_bound (scg,
					0, index, gnm_sheet_get_last_col (sv->sheet), index);
		} else if (is_cols) {
			GnmPane *pane =
				scg_pane (scg, scg->pane[3] ? 3 : 0);
			sv_selection_add_full (sv,
					       index, pane->first.row,
					       index, 0,
					       index, gnm_sheet_get_last_row (sv->sheet),
					       GNM_SELECTION_MODE_ADD);
		} else {
			GnmPane *pane =
				scg_pane (scg, scg->pane[1] ? 1 : 0);
			sv_selection_add_full (sv,
					       pane->first.col, index,
					       0, index,
					       gnm_sheet_get_last_col (sv->sheet), index,
					       GNM_SELECTION_MODE_ADD);
		}
	}

	/* The edit pos, and the selection may have changed */
	if (!rangesel)
		sheet_update (sv->sheet);
	return TRUE;
}

/***************************************************************************/

static void
cb_select_all_btn_draw (GtkWidget *widget, cairo_t *cr, SheetControlGUI *scg)
{
	int offset = scg_sheet (scg)->text_is_rtl ? -1 : 0;
	GtkAllocation a;
	GtkStyleContext *ctxt = gtk_widget_get_style_context (widget);

	gtk_widget_get_allocation (widget, &a);

	gtk_style_context_save (ctxt);
	gtk_style_context_set_state (ctxt, GTK_STATE_FLAG_NORMAL);
	gtk_render_background (ctxt, cr, offset + 1, 1,
			       a.width - 1, a.height - 1);
	gtk_render_frame (ctxt, cr, offset, 0, a.width + 1, a.height + 1);
	gtk_style_context_restore (ctxt);
}

static gboolean
cb_select_all_btn_event (G_GNUC_UNUSED GtkWidget *widget, GdkEvent *event, SheetControlGUI *scg)
{
	if (event->type == GDK_BUTTON_PRESS) {
		scg_select_all (scg);
		return TRUE;
	}

	return FALSE;
}

static void
cb_vscrollbar_value_changed (GtkRange *range, SheetControlGUI *scg)
{
	GtkAdjustment *adj = gtk_range_get_adjustment (range);
	scg_set_top_row (scg, gtk_adjustment_get_value (adj));
}

static void
cb_hscrollbar_value_changed (GtkRange *range, SheetControlGUI *scg)
{
	GtkAdjustment *adj = gtk_range_get_adjustment (range);
	scg_set_left_col (scg, gtk_adjustment_get_value (adj));
}

static void
cb_hscrollbar_adjust_bounds (GtkRange *range, gdouble new_value, Sheet *sheet)
{
	GtkAdjustment *adj = gtk_range_get_adjustment (range);
	double upper = gtk_adjustment_get_upper (adj);
	double page_size = gtk_adjustment_get_page_size (adj);
	gdouble limit = upper - page_size;
	if (upper < gnm_sheet_get_max_cols (sheet) && new_value >= limit) {
		upper = new_value + page_size + 1;
		if (upper > gnm_sheet_get_max_cols (sheet))
			upper = gnm_sheet_get_max_cols (sheet);
		gtk_adjustment_set_upper (adj, upper);
	}
}
static void
cb_vscrollbar_adjust_bounds (GtkRange *range, gdouble new_value, Sheet *sheet)
{
	GtkAdjustment *adj = gtk_range_get_adjustment (range);
	double upper = gtk_adjustment_get_upper (adj);
	double page_size = gtk_adjustment_get_page_size (adj);
	gdouble limit = upper - page_size;
	if (upper < gnm_sheet_get_max_rows (sheet) && new_value >= limit) {
		upper = new_value + page_size + 1;
		if (upper > gnm_sheet_get_max_rows (sheet))
			upper = gnm_sheet_get_max_rows (sheet);
		gtk_adjustment_set_upper (adj, upper);
	}
}

static void
cb_table_destroy (SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	int i;

	g_clear_object (&scg->grid);

	scg_mode_edit (scg);	/* finish any object edits */
	scg_unant (sc);		/* Make sure that everything is unanted */

	if (scg->wbcg) {
		GtkWindow *toplevel = wbcg_toplevel (scg->wbcg);

		/* Only pane-0 ever gets focus */
		if (NULL != toplevel &&
		    gtk_window_get_focus (toplevel) == GTK_WIDGET (scg_pane (scg, 0)))
			gtk_window_set_focus (toplevel, NULL);
	}

	for (i = scg->active_panes; i-- > 0 ; )
		if (NULL != scg->pane[i]) {
			gtk_widget_destroy (GTK_WIDGET (scg->pane[i]));
			scg->pane[i] = NULL;
		}

	g_object_unref (scg);
}

static void
scg_init (SheetControlGUI *scg)
{
	scg->comment.selected = NULL;
	scg->comment.item = NULL;
	scg->comment.timer = 0;

	scg->delayedMovement.timer = 0;
	scg->delayedMovement.handler = NULL;

	scg->grab_stack = 0;
	scg->selected_objects = NULL;

	scg->im.item = NULL;
	scg->im.timer = 0;

	// These shouldn't matter and will be overwritten
	scg->screen_width = 1920;
	scg->screen_height = 1200;
}

/*************************************************************************/

/**
 * gnm_pane_update_inital_top_left:
 * A convenience routine to store the new topleft back in the view.
 */
static void
gnm_pane_update_inital_top_left (GnmPane const *pane)
{
	if (pane->index == 0) {
		SheetView *sv = scg_view (pane->simple.scg);
		sv->initial_top_left = pane->first;
	}
}

static gint64
bar_set_left_col (GnmPane *pane, int new_first_col)
{
	GocCanvas *colc;
	gint64 col_offset;


	col_offset = pane->first_offset.x +=
		scg_colrow_distance_get (pane->simple.scg, TRUE, pane->first.col, new_first_col);
	pane->first.col = new_first_col;

	/* Scroll the column headers */
	if (NULL != (colc = pane->col.canvas))
		goc_canvas_scroll_to (colc, col_offset / colc->pixels_per_unit, 0);

	return col_offset;
}

static void
gnm_pane_set_left_col (GnmPane *pane, int new_first_col)
{
	Sheet *sheet;
	g_return_if_fail (pane != NULL);
	sheet = scg_sheet (pane->simple.scg);
	g_return_if_fail (0 <= new_first_col && new_first_col < gnm_sheet_get_max_cols (sheet));

	if (pane->first.col != new_first_col) {
		GocCanvas * const canvas = GOC_CANVAS (pane);
		gint64 const col_offset = bar_set_left_col (pane, new_first_col);

		gnm_pane_compute_visible_region (pane, FALSE);
		goc_canvas_scroll_to (canvas, col_offset / canvas->pixels_per_unit, pane->first_offset.y / canvas->pixels_per_unit);
		gnm_pane_update_inital_top_left (pane);
	}
}

void
scg_set_left_col (SheetControlGUI *scg, int col)
{
	Sheet const *sheet;
	GnmRange const *bound;

	g_return_if_fail (GNM_IS_SCG (scg));

	sheet = scg_sheet (scg);
	bound = &sheet->priv->unhidden_region;
	if (col < bound->start.col)
		col = bound->start.col;
	else if (col >= gnm_sheet_get_max_cols (sheet))
		col = gnm_sheet_get_last_col (sheet);
	else if (col > bound->end.col)
		col = bound->end.col;

	if (scg->pane[1]) {
		int right = scg_view (scg)->unfrozen_top_left.col;
		if (col < right)
			col = right;
	}
	if (scg->pane[3])
		gnm_pane_set_left_col (scg_pane (scg, 3), col);
	gnm_pane_set_left_col (scg_pane (scg, 0), col);
}

static gint64
bar_set_top_row (GnmPane *pane, int new_first_row)
{
	GocCanvas *rowc;
	gint64 row_offset;

	row_offset = pane->first_offset.y +=
		scg_colrow_distance_get (pane->simple.scg, FALSE, pane->first.row, new_first_row);
	pane->first.row = new_first_row;

	/* Scroll the row headers */
	if (NULL != (rowc = pane->row.canvas))
		goc_canvas_scroll_to (rowc, 0, row_offset / rowc->pixels_per_unit);

	return row_offset;
}

static void
gnm_pane_set_top_row (GnmPane *pane, int new_first_row)
{
	Sheet *sheet;
	g_return_if_fail (pane != NULL);
	sheet = scg_sheet (pane->simple.scg);
	g_return_if_fail (0 <= new_first_row && new_first_row < gnm_sheet_get_max_rows (sheet));

	if (pane->first.row != new_first_row) {
		GocCanvas * const canvas = GOC_CANVAS(pane);
		gint64 const row_offset = bar_set_top_row (pane, new_first_row);
		gint64 col_offset = pane->first_offset.x;

		gnm_pane_compute_visible_region (pane, FALSE);
		goc_canvas_scroll_to (canvas, col_offset / canvas->pixels_per_unit, row_offset / canvas->pixels_per_unit);
		gnm_pane_update_inital_top_left (pane);
	}
}

void
scg_set_top_row (SheetControlGUI *scg, int row)
{
	Sheet const *sheet;
	GnmRange const *bound;

	g_return_if_fail (GNM_IS_SCG (scg));

	sheet = scg_sheet (scg);
	bound = &sheet->priv->unhidden_region;
	if (row < bound->start.row)
		row = bound->start.row;
	else if (row >= gnm_sheet_get_max_rows (sheet))
		row = gnm_sheet_get_last_row (sheet);
	else if (row > bound->end.row)
		row = bound->end.row;

	if (scg->pane[3]) {
		int bottom = scg_view (scg)->unfrozen_top_left.row;
		if (row < bottom)
			row = bottom;
	}
	if (scg->pane[1])
		gnm_pane_set_top_row (scg_pane (scg, 1), row);
	gnm_pane_set_top_row (scg_pane (scg, 0), row);
}

static void
gnm_pane_set_top_left (GnmPane *pane,
			 int col, int row, gboolean force_scroll)
{
	gboolean changed = FALSE;
	gint64 col_offset, row_offset;
	GocCanvas *canvas;

	g_return_if_fail (0 <= col &&
			  col < gnm_sheet_get_max_cols (scg_sheet (pane->simple.scg)));
	g_return_if_fail (0 <= row &&
			  row < gnm_sheet_get_max_rows (scg_sheet (pane->simple.scg)));

	if (pane->first.col != col || force_scroll) {
		if (force_scroll) {
			/* Clear the offsets in case col/row size changed */
			pane->first_offset.x = 0;
			pane->first.col = 0;
		}

		col_offset = bar_set_left_col (pane, col);
		changed = TRUE;
	} else {
		col_offset = pane->first_offset.x;
	}

	if (pane->first.row != row || force_scroll) {
		if (force_scroll) {
			/* Clear the offsets in case col/row size changed */
			pane->first_offset.y = 0;
			pane->first.row = 0;
		}
		row_offset = bar_set_top_row (pane, row);
		changed = TRUE;
	} else
		row_offset = pane->first_offset.y;

	if (!changed)
		return;

	gnm_pane_compute_visible_region (pane, force_scroll);
	canvas = GOC_CANVAS (pane);
	goc_canvas_scroll_to (canvas, col_offset / canvas->pixels_per_unit, row_offset / canvas->pixels_per_unit);
	gnm_pane_update_inital_top_left (pane);
}

static void
scg_set_top_left (SheetControl *sc, int col, int row)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;

	g_return_if_fail (GNM_IS_SCG (scg));

	if (!scg->pane[0])
		return;
	/* We could be faster if necessary */
	scg_set_left_col (scg, col);
	scg_set_top_row (scg, row);
}

static void
gnm_pane_make_cell_visible (GnmPane *pane, int col, int row,
			    gboolean const force_scroll)
{
	GocCanvas *canvas;
	Sheet *sheet;
	int   new_first_col, new_first_row;
	GnmRange range;
	GtkAllocation ca;

	g_return_if_fail (GNM_IS_PANE (pane));

	/* Avoid calling this before the canvas is realized: We do not know the
	 * visible area, and would unconditionally scroll the cell to the top
	 * left of the viewport.
	 */
	if (!gtk_widget_get_realized (GTK_WIDGET (pane)))
		return;

	sheet = scg_sheet (pane->simple.scg);
	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);
	g_return_if_fail (col < gnm_sheet_get_max_cols (sheet));
	g_return_if_fail (row < gnm_sheet_get_max_rows (sheet));

	canvas = GOC_CANVAS (pane);
	range.start.col = range.end.col = col;
	range.start.row = range.end.row = row;
	gnm_sheet_merge_find_bounding_box (sheet, &range);

	gtk_widget_get_allocation (GTK_WIDGET (canvas), &ca);

	/* Find the new pane->first.col */
	if (range.start.col < pane->first.col) {
		new_first_col = range.start.col;
	} else if (range.end.col > pane->last_full.col) {
		int width = ca.width;
		ColRowInfo const * const ci = sheet_col_get_info (sheet, range.end.col);
		if (ci->size_pixels < width) {
			int first_col = (pane->last_visible.col == pane->first.col)
				? pane->first.col : range.end.col;

			for (; first_col > 0; --first_col) {
				ColRowInfo const * const ci = sheet_col_get_info (sheet, first_col);
				if (ci->visible) {
					width -= ci->size_pixels;
					if (width < 0)
						break;
				}
			}
			new_first_col = first_col+1;
			if (new_first_col > range.start.col)
				new_first_col = range.start.col;
		} else
			new_first_col = col;
	} else
		new_first_col = pane->first.col;

	/* Find the new pane->first.row */
	if (range.start.row < pane->first.row) {
		new_first_row = range.start.row;
	} else if (range.end.row > pane->last_full.row) {
		int height = ca.height;
		ColRowInfo const * const ri = sheet_row_get_info (sheet, range.end.row);
		if (ri->size_pixels < height) {
			int first_row = (pane->last_visible.row == pane->first.row)
				? pane->first.row : range.end.row;

			for (; first_row > 0; --first_row) {
				ColRowInfo const * const ri = sheet_row_get_info (sheet, first_row);
				if (ri->visible) {
					height -= ri->size_pixels;
					if (height < 0)
						break;
				}
			}
			new_first_row = first_row+1;
			if (new_first_row > range.start.row)
				new_first_row = range.start.row;
		} else
			new_first_row = row;
	} else
		new_first_row = pane->first.row;

	gnm_pane_set_top_left (pane, new_first_col, new_first_row,
				 force_scroll);
}

/**
 * scg_make_cell_visible:
 * @scg: The gui control
 * @col:
 * @row:
 * @force_scroll: Completely recalibrate the offsets to the new position
 * @couple_panes: Scroll scroll dynamic panes back to bounds if target
 *                 is in frozen segment.
 *
 * Ensure that cell (col, row) is visible.
 * Sheet is scrolled if cell is outside viewport.
 */
void
scg_make_cell_visible (SheetControlGUI *scg, int col, int row,
		       gboolean force_scroll, gboolean couple_panes)
{
	SheetView const *sv = scg_view (scg);
	GnmCellPos const *tl, *br;

	g_return_if_fail (GNM_IS_SCG (scg));

	if (!scg->active_panes)
		return;

	tl = &sv->frozen_top_left;
	br = &sv->unfrozen_top_left;
	if (col < br->col) {
		if (row >= br->row) {	/* pane 1 */
			if (col < tl->col)
				col = tl->col;
			gnm_pane_make_cell_visible (scg->pane[1],
				col, row, force_scroll);
			gnm_pane_set_top_left (scg->pane[0],
				couple_panes ? br->col : scg->pane[0]->first.col,
				scg->pane[1]->first.row,
				force_scroll);
			if (couple_panes && scg->pane[3])
				gnm_pane_set_left_col (scg->pane[3], br->col);
		} else if (couple_panes) { /* pane 2 */
			/* FIXME : We may need to change the way this routine
			 * is used to fix this.  Because we only know what the
			 * target cell is we cannot absolutely differentiate
			 * between col & row scrolling.  For now use the
			 * heuristic that if the col was visible this is a
			 * vertical jump.
			 */
			if (scg->pane[2]->first.col <= col &&
			    scg->pane[2]->last_visible.col >= col) {
				scg_set_top_row (scg, row);
			} else
				scg_set_left_col (scg, col);
		}
	} else if (row < br->row) {	/* pane 3 */
		if (row < tl->row)
			row = tl->row;
		gnm_pane_make_cell_visible (scg->pane[3],
			col, row, force_scroll);
		gnm_pane_set_top_left (scg->pane[0],
			scg->pane[3]->first.col,
			couple_panes
			? br->row
			: scg->pane[0]->first.row,
			force_scroll);
		if (couple_panes && scg->pane[1])
			gnm_pane_set_top_row (scg->pane[1],
				br->row);
	} else {			 /* pane 0 */
		gnm_pane_make_cell_visible (scg->pane[0],
			col, row, force_scroll);
		if (scg->pane[1])
			gnm_pane_set_top_left (scg->pane[1],
				tl->col, scg->pane[0]->first.row, force_scroll);
		if (scg->pane[3])
			gnm_pane_set_top_left (scg->pane[3],
				scg->pane[0]->first.col, tl->row, force_scroll);
	}
	if (scg->pane[2])
		gnm_pane_set_top_left (scg->pane[2],
			tl->col, tl->row, force_scroll);
}

static void
scg_make_cell_visible_virt (SheetControl *sc, int col, int row,
			    gboolean couple_panes)
{
	scg_make_cell_visible ((SheetControlGUI *)sc, col, row,
			       FALSE, couple_panes);
}

/*************************************************************************/

static void
scg_set_panes (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *) sc;
	SheetView	*sv = sc->view;
	gboolean const being_frozen = gnm_sheet_view_is_frozen (sv);
	GocDirection direction = (sv_sheet (sv)->text_is_rtl)? GOC_DIRECTION_RTL: GOC_DIRECTION_LTR;

	g_return_if_fail (GNM_IS_SHEET_VIEW (sv));

	if (!scg->pane[0])
		return;
	if (being_frozen) {
		GnmCellPos const *tl = &sv->frozen_top_left;
		GnmCellPos const *br = &sv->unfrozen_top_left;
		gboolean const freeze_h = br->col > tl->col;
		gboolean const freeze_v = br->row > tl->row;

		gnm_pane_bound_set (scg->pane[0],
			br->col, br->row,
			gnm_sheet_get_last_col (sv->sheet), gnm_sheet_get_last_row (sv->sheet));

		if (freeze_h) {
			scg->active_panes = 2;
			if (!scg->pane[1]) {
				scg->pane[1] = gnm_pane_new (scg, TRUE, FALSE, 1);
				gnm_pane_set_direction (scg->pane[1], direction);
				gtk_grid_attach (scg->grid,
				                 GTK_WIDGET (scg->pane[1]),
				                 2, 3, 1, 1);
				gtk_grid_attach (scg->grid,
				                 GTK_WIDGET (scg->pane[1]->col.canvas),
						 2, 0, 1, 2);
			}
			gnm_pane_bound_set (scg->pane[1],
				tl->col, br->row, br->col - 1, gnm_sheet_get_last_row (sv->sheet));
		}
		if (freeze_h && freeze_v) {
			scg->active_panes = 4;
			if (!scg->pane[2]) {
				scg->pane[2] = gnm_pane_new (scg, FALSE, FALSE,  2);
				gnm_pane_set_direction (scg->pane[2], direction);
				gtk_grid_attach (scg->grid,
				                 GTK_WIDGET (scg->pane[2]),
				                 2, 2, 1, 1);
			}
			gnm_pane_bound_set (scg->pane[2],
				tl->col, tl->row, br->col - 1, br->row - 1);
		}
		if (freeze_v) {
			scg->active_panes = 4;
			if (!scg->pane[3]) {
				scg->pane[3] = gnm_pane_new (scg, FALSE, TRUE, 3);
				gnm_pane_set_direction (scg->pane[3], direction);
				gtk_grid_attach (scg->grid,
				                 GTK_WIDGET (scg->pane[3]),
				                 3, 2, 1, 1);
				gtk_grid_attach (scg->grid,
				                 GTK_WIDGET (scg->pane[3]->row.canvas),
				                 0, 2, 2, 1);
			}
			gnm_pane_bound_set (scg->pane[3],
				br->col, tl->row, gnm_sheet_get_last_col (sv->sheet), br->row - 1);
		}
	} else {
		int i;
		for (i = 1 ; i <= 3 ; i++)
			if (scg->pane[i]) {
				gtk_widget_destroy (GTK_WIDGET (scg->pane[i]));
				scg->pane[i] = NULL;
			}

		scg->active_panes = 1;
		gnm_pane_bound_set (scg->pane[0],
			0, 0, gnm_sheet_get_last_col (sv->sheet), gnm_sheet_get_last_row (sv->sheet));
	}

	gtk_widget_show_all (GTK_WIDGET (scg->grid));

	/* in case headers are hidden */
	scg_adjust_preferences (scg);
	scg_resize (scg, TRUE);

	if (being_frozen) {
		GnmCellPos const *tl = &sc->view->frozen_top_left;

		if (scg->pane[1])
			gnm_pane_set_left_col (scg->pane[1], tl->col);
		if (scg->pane[2])
			gnm_pane_set_top_left (scg->pane[2], tl->col, tl->row, TRUE);
		if (scg->pane[3])
			gnm_pane_set_top_row (scg->pane[3], tl->row);
	}
	set_resize_pane_pos (scg, scg->vpane);
	set_resize_pane_pos (scg, scg->hpane);
}

static void
cb_wbc_destroyed (SheetControlGUI *scg)
{
	scg->wbcg = NULL;
	scg->sheet_control.wbc = NULL;
}

static void
cb_scg_redraw (SheetControlGUI *scg)
{
	scg_adjust_preferences (scg);
	scg_redraw_all (&scg->sheet_control, TRUE);
}

static void
cb_scg_redraw_resize (SheetControlGUI *scg)
{
	cb_scg_redraw (scg);
	scg_resize (scg, FALSE);
}

static void
cb_scg_sheet_resized (SheetControlGUI *scg)
{
	cb_scg_redraw_resize (scg);
	sc_set_panes (&scg->sheet_control);
}

static void
cb_scg_direction_changed (SheetControlGUI *scg)
{
	/* set direction in the canvas */
	int i = scg->active_panes;
	while (i-- > 0) {
		GnmPane *pane = scg->pane[i];
		if (NULL != pane)
			gnm_pane_set_direction (scg->pane[i],
			                        scg_sheet (scg)->text_is_rtl? GOC_DIRECTION_RTL: GOC_DIRECTION_LTR);
	}
	scg_resize (scg, TRUE);
}

static GnmPane const *
resize_pane_pos (SheetControlGUI *scg, GtkPaned *p,
		 int *colrow_result, gint64 *guide_pos)
{
	ColRowInfo const *cri;
	GnmPane const *pane = scg_pane (scg, 0);
	gboolean const vert = (p == scg->hpane);
	int colrow, handle;
	gint64 pos = gtk_paned_get_position (p);

	gtk_widget_style_get (GTK_WIDGET (p), "handle-size", &handle, NULL);
	pos += handle / 2;
	if (vert) {
		if (gtk_widget_get_visible (GTK_WIDGET (pane->row.canvas))) {
			GtkAllocation ca;
			gtk_widget_get_allocation (GTK_WIDGET (pane->row.canvas), &ca);
			pos -= ca.width;
		}
		if (scg->pane[1]) {
			GtkAllocation pa;
			gtk_widget_get_allocation (GTK_WIDGET (scg->pane[1]),
						   &pa);

			if (pos < pa.width)
				pane = scg_pane (scg, 1);
			else
				pos -= pa.width;
		}
		pos = MAX (pos, 0);
		pos += pane->first_offset.x;
		colrow = gnm_pane_find_col (pane, pos, guide_pos);
	} else {
		if (gtk_widget_get_visible (GTK_WIDGET (pane->col.canvas))) {
			GtkAllocation ca;
			gtk_widget_get_allocation (GTK_WIDGET (pane->col.canvas), &ca);
			pos -= ca.height;
		}
		if (scg->pane[3]) {
			GtkAllocation pa;
			gtk_widget_get_allocation (GTK_WIDGET (scg->pane[3]),
						   &pa);
			if (pos < pa.height)
				pane = scg_pane (scg, 3);
			else
				pos -= pa.height;
		}
		pos = MAX (pos, 0);
		pos += pane->first_offset.y;
		colrow = gnm_pane_find_row (pane, pos, guide_pos);
	}
	cri = sheet_colrow_get_info (scg_sheet (scg), colrow, vert);
	if (pos >= (*guide_pos + cri->size_pixels / 2)) {
		*guide_pos += cri->size_pixels;
		colrow++;
	}
	if (NULL != colrow_result)
		*colrow_result = colrow;

	return pane;
}

static void
scg_gtk_paned_set_position (SheetControlGUI *scg, GtkPaned *p, int pane_pos)
{
	/* A negative position is special to GtkPaned.  */
	pane_pos = MAX (pane_pos, 0);

	if (p == scg->vpane)
		scg->vpos = pane_pos;
	else
		scg->hpos = pane_pos;

	gtk_paned_set_position (p, pane_pos);
}

static void
set_resize_pane_pos (SheetControlGUI *scg, GtkPaned *p)
{
	int handle_size, pane_pos, size;
	GnmPane *pane0 = scg->pane[0];

	if (!pane0)
		return;

	if (p == scg->vpane) {
		if (gtk_widget_get_visible (GTK_WIDGET (pane0->col.canvas))) {
			GtkAllocation alloc;
			gtk_widget_get_allocation (GTK_WIDGET (pane0->col.canvas), &alloc);
			pane_pos = alloc.height;
		} else
			pane_pos = 0;
		if (scg->pane[3]) {
			gtk_widget_get_size_request (
				GTK_WIDGET (scg->pane[3]), NULL, &size);
			pane_pos += size;
		}
	} else {
		if (gtk_widget_get_visible (GTK_WIDGET (pane0->row.canvas))) {
			GtkAllocation alloc;
			gtk_widget_get_allocation (GTK_WIDGET (pane0->row.canvas), &alloc);
			pane_pos = alloc.width;
		} else
			pane_pos = 0;
		if (scg->pane[1]) {
			gtk_widget_get_size_request (
				GTK_WIDGET (scg->pane[1]), &size, NULL);
			pane_pos += size;
		}
	}
	gtk_widget_style_get (GTK_WIDGET (p), "handle-size", &handle_size, NULL);
	pane_pos -= handle_size / 2;

	g_signal_handlers_block_by_func (G_OBJECT (p),
		G_CALLBACK (cb_resize_pane_motion), scg);
	scg_gtk_paned_set_position (scg, p, pane_pos);
	g_signal_handlers_unblock_by_func (G_OBJECT (p),
		G_CALLBACK (cb_resize_pane_motion), scg);
}

static void
cb_check_resize (GtkPaned *p, GtkAllocation *allocation, SheetControlGUI *scg);

static gboolean
resize_pane_finish (SheetControlGUI *scg, GtkPaned *p)
{
	SheetView *sv =	scg_view (scg);
	GnmCellPos frozen_tl, unfrozen_tl;
	GnmPane const *pane;
	int colrow;
	gint64 guide_pos;

#warning GTK3: replace this?
#if 0
	if (p->in_drag)
		return TRUE;
#endif
	pane = resize_pane_pos (scg, p, &colrow, &guide_pos);

	if (gnm_sheet_view_is_frozen (sv)) {
		frozen_tl   = sv->frozen_top_left;
		unfrozen_tl = sv->unfrozen_top_left;
	} else
		frozen_tl = pane->first;
	if (p == scg->hpane) {
		unfrozen_tl.col = colrow;
		if (!gnm_sheet_view_is_frozen (sv))
			unfrozen_tl.row = frozen_tl.row = 0;
	} else {
		unfrozen_tl.row = colrow;
		if (!gnm_sheet_view_is_frozen (sv))
			unfrozen_tl.col = frozen_tl.col = 0;
	}
	gnm_sheet_view_freeze_panes	(sv, &frozen_tl, &unfrozen_tl);

	scg->pane_drag_handler = 0;
	scg_size_guide_stop (scg);

	set_resize_pane_pos (scg, p);

	g_signal_handlers_unblock_by_func
		(G_OBJECT (p),
		 G_CALLBACK (cb_check_resize), scg);

	return FALSE;
}
static gboolean
cb_resize_vpane_finish (SheetControlGUI *scg)
{
	return resize_pane_finish (scg, scg->vpane);
}
static gboolean
cb_resize_hpane_finish (SheetControlGUI *scg)
{
	return resize_pane_finish (scg, scg->hpane);
}

static void
cb_resize_pane_motion (GtkPaned *p,
		       G_GNUC_UNUSED GParamSpec *pspec,
		       SheetControlGUI *scg)
{
	gboolean const vert = (p == scg->hpane);
	int colrow;
	gint64 guide_pos;

	resize_pane_pos (scg, p, &colrow, &guide_pos);
#warning GTK3: what replaces p->in_drag?
	if (scg->pane_drag_handler == 0/* && p->in_drag*/) {
		g_signal_handlers_block_by_func
			(G_OBJECT (p),
			 G_CALLBACK (cb_check_resize), scg);
		scg_size_guide_start (scg, vert, colrow, FALSE);
		scg->pane_drag_handler = g_timeout_add (250,
			vert ? (GSourceFunc) cb_resize_hpane_finish
			     : (GSourceFunc) cb_resize_vpane_finish,
			(gpointer) scg);
	}
	if (scg->pane_drag_handler)
		scg_size_guide_motion (scg, vert, guide_pos);

}

static void
cb_check_resize (GtkPaned *p, G_GNUC_UNUSED GtkAllocation *allocation,
		 SheetControlGUI *scg)
{
	gboolean const vert = (p == scg->vpane);
	gint max, pos = vert ? scg->vpos : scg->hpos;

	g_object_get (G_OBJECT (p), "max-position", &max, NULL);
	if (pos > max)
		pos = max;

	if (gtk_paned_get_position (p) != pos) {
		g_signal_handlers_block_by_func
			(G_OBJECT (p),
			 G_CALLBACK (cb_resize_pane_motion), scg);
		gtk_paned_set_position (p, pos);
		g_signal_handlers_unblock_by_func
			(G_OBJECT (p),
			 G_CALLBACK (cb_resize_pane_motion), scg);
	}
}

struct resize_closure {
	GtkPaned *p;
	SheetControlGUI *scg;
};

static gboolean
idle_resize (struct resize_closure *r)
{

	set_resize_pane_pos (r->scg, r->p);
	g_free (r);
	return FALSE;
}

static void
cb_canvas_resize (GtkWidget *w, G_GNUC_UNUSED GtkAllocation *allocation,
		 SheetControlGUI *scg)
{
	struct resize_closure *r = g_new (struct resize_closure, 1);
	r->scg = scg;
	r->p = (w == GTK_WIDGET (scg->pane[0]->col.canvas))? scg->hpane: scg->vpane;
	/* The allocation is not correct at this point, weird */
	g_idle_add ((GSourceFunc) idle_resize, r);
}

static gboolean
post_create_cb (SheetControlGUI *scg)
{
	Sheet *sheet = sc_sheet (GNM_SHEET_CONTROL (scg));
	if (sheet->sheet_objects)
		scg_object_select (scg, (SheetObject *) sheet->sheet_objects->data);
	return FALSE;
}

static gboolean
sheet_object_key_pressed (G_GNUC_UNUSED GtkWidget *w, GdkEventKey *event, SheetControlGUI *scg)
{
	Sheet *sheet = scg_sheet (scg);
	WorkbookControl * wbc = scg_wbc (scg);
	Workbook * wb = wb_control_get_workbook (wbc);
	switch (event->keyval) {
	case GDK_KEY_KP_Page_Up:
	case GDK_KEY_Page_Up:
		if ((event->state & GDK_CONTROL_MASK) != 0){
			if ((event->state & GDK_SHIFT_MASK) != 0){
				WorkbookSheetState * old_state = workbook_sheet_state_new(wb);
				int old_pos = sheet->index_in_wb;

				if (old_pos > 0){
					workbook_sheet_move(sheet, -1);
					cmd_reorganize_sheets (wbc, old_state, sheet);
				}
			} else {
				gnm_notebook_prev_page (scg->wbcg->bnotebook);
			}
			return FALSE;
		}
		break;
	case GDK_KEY_KP_Page_Down:
	case GDK_KEY_Page_Down:

		if ((event->state & GDK_CONTROL_MASK) != 0){
			if ((event->state & GDK_SHIFT_MASK) != 0){
				WorkbookSheetState * old_state = workbook_sheet_state_new(wb);
				int num_sheets = workbook_sheet_count(wb);
				gint old_pos = sheet->index_in_wb;

				if (old_pos < num_sheets - 1){
					workbook_sheet_move(sheet, 1);
					cmd_reorganize_sheets (wbc, old_state, sheet);
				}
			} else {
				gnm_notebook_next_page (scg->wbcg->bnotebook);
			}
			return FALSE;
		}
		break;
	}
	return TRUE;
}

static void
cb_screen_changed (GtkWidget *widget, G_GNUC_UNUSED GdkScreen *prev,
		   SheetControlGUI *scg)
{
	GdkScreen *screen = gtk_widget_get_screen (widget);

	if (screen) {
		scg->screen_width = gdk_screen_get_width (screen);
		scg->screen_height = gdk_screen_get_height (screen);
	}
}

SheetControlGUI *
sheet_control_gui_new (SheetView *sv, WBCGtk *wbcg)
{
	SheetControlGUI *scg;
	Sheet *sheet;
	GocDirection direction;
	GdkRGBA cfore, cback;

	g_return_val_if_fail (GNM_IS_SHEET_VIEW (sv), NULL);

	sheet = sv_sheet (sv);
	direction = (sheet->text_is_rtl)? GOC_DIRECTION_RTL: GOC_DIRECTION_LTR;

	scg = g_object_new (GNM_SCG_TYPE, NULL);
	scg->wbcg = wbcg;
	scg->sheet_control.wbc = GNM_WBC (wbcg);

	g_object_weak_ref (G_OBJECT (wbcg),
		(GWeakNotify) cb_wbc_destroyed,
		scg);

	if (sheet->sheet_type == GNM_SHEET_DATA) {
		scg->active_panes = 1;
		scg->pane[0] = NULL;
		scg->pane[1] = NULL;
		scg->pane[2] = NULL;
		scg->pane[3] = NULL;
		scg->pane_drag_handler = 0;

		scg->col_group.buttons = g_ptr_array_new ();
		scg->row_group.buttons = g_ptr_array_new ();
		scg->col_group.button_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		g_object_set (scg->col_group.button_box,
		              "halign", GTK_ALIGN_CENTER,
		              "homogeneous", TRUE,
		              NULL);
		scg->row_group.button_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
		g_object_set (scg->row_group.button_box,
		              "valign", GTK_ALIGN_CENTER,
		              "homogeneous", TRUE,
		              NULL);
		scg->select_all_btn = gtk_drawing_area_new ();
		gtk_style_context_add_class (gtk_widget_get_style_context (scg->select_all_btn),
					     GTK_STYLE_CLASS_BUTTON);
		gtk_style_context_add_class (gtk_widget_get_style_context (scg->select_all_btn),
					     "all");
		gtk_widget_add_events (scg->select_all_btn, GDK_BUTTON_PRESS_MASK);
		g_signal_connect (G_OBJECT (scg->select_all_btn), "draw",
				  G_CALLBACK (cb_select_all_btn_draw), scg);
		g_signal_connect (G_OBJECT (scg->select_all_btn), "event",
				  G_CALLBACK (cb_select_all_btn_event), scg);

		scg->grid = GTK_GRID (gtk_grid_new ());
		gtk_grid_attach (scg->grid, scg->col_group.button_box,
		                 1, 0, 1, 1);
		gtk_grid_attach (scg->grid, scg->row_group.button_box,
		                 0, 1, 1, 1);
		gtk_grid_attach (scg->grid, scg->select_all_btn, 1, 1, 1, 1);

		scg->pane[1] = scg->pane[2] = scg->pane[3] = NULL;
		scg->pane[0] = gnm_pane_new (scg, TRUE, TRUE, 0);
		gnm_pane_set_direction (scg->pane[0], direction);
		gtk_grid_attach (scg->grid,
		                 GTK_WIDGET (scg->pane[0]->col.canvas),
		                 3, 0, 1, 2);
		gtk_grid_attach (scg->grid,
		                 GTK_WIDGET (scg->pane[0]->row.canvas),
		                 0, 3, 2, 1);
		g_object_set (scg->pane[0],
		              "hexpand", TRUE,
		              "vexpand", TRUE,
		              NULL);
		gtk_grid_attach (scg->grid, GTK_WIDGET (scg->pane[0]),
		                 3, 3, 1, 1);
		g_signal_connect_after (G_OBJECT (scg->pane[0]->col.canvas), "size-allocate",
			G_CALLBACK (cb_canvas_resize), scg);
		g_signal_connect_after (G_OBJECT (scg->pane[0]->row.canvas), "size-allocate",
			G_CALLBACK (cb_canvas_resize), scg);

		scg->va = (GtkAdjustment *)gtk_adjustment_new (0., 0., 1, 1., 1., 1.);
		scg->vs = g_object_new (GTK_TYPE_SCROLLBAR,
		                "orientation", GTK_ORIENTATION_VERTICAL,
				"adjustment", scg->va,
		                NULL);
		g_signal_connect (G_OBJECT (scg->vs),
			"value_changed",
			G_CALLBACK (cb_vscrollbar_value_changed), scg);
		g_signal_connect (G_OBJECT (scg->vs),
			"adjust_bounds",
			G_CALLBACK (cb_vscrollbar_adjust_bounds), sheet);

		scg->ha = (GtkAdjustment *)gtk_adjustment_new (0., 0., 1, 1., 1., 1.);
		scg->hs = g_object_new (GTK_TYPE_SCROLLBAR,
				"adjustment", scg->ha,
				NULL);
		g_signal_connect (G_OBJECT (scg->hs),
			"value_changed",
			G_CALLBACK (cb_hscrollbar_value_changed), scg);
		g_signal_connect (G_OBJECT (scg->hs),
			"adjust_bounds",
			G_CALLBACK (cb_hscrollbar_adjust_bounds), sheet);

		g_object_ref (scg->grid);
		scg->vpane = g_object_new (GTK_TYPE_PANED, "orientation", GTK_ORIENTATION_VERTICAL, NULL);
		gtk_paned_add1 (scg->vpane, gtk_label_new (NULL)); /* use a spacer */
		gtk_paned_add2 (scg->vpane, scg->vs);
		scg_gtk_paned_set_position (scg, scg->vpane, 0);
		gtk_widget_set_vexpand (GTK_WIDGET (scg->vpane), TRUE);
		gtk_grid_attach (scg->grid,
		                 GTK_WIDGET (scg->vpane), 4, 0, 1, 4);
		scg->hpane = g_object_new (GTK_TYPE_PANED, NULL);
		gtk_paned_add1 (scg->hpane, gtk_label_new (NULL)); /* use a spacer */
		gtk_paned_add2 (scg->hpane, scg->hs);
		scg_gtk_paned_set_position (scg, scg->hpane, 0);
		gtk_widget_set_hexpand (GTK_WIDGET (scg->hpane), TRUE);
		gtk_grid_attach (scg->grid,
		                 GTK_WIDGET (scg->hpane), 0, 4, 4, 1);
		/* do not connect until after setting position */
		g_signal_connect (G_OBJECT (scg->vpane), "notify::position",
			G_CALLBACK (cb_resize_pane_motion), scg);
		g_signal_connect (G_OBJECT (scg->hpane), "notify::position",
			G_CALLBACK (cb_resize_pane_motion), scg);
		g_signal_connect_after (G_OBJECT (scg->vpane), "size-allocate",
			G_CALLBACK (cb_check_resize), scg);
		g_signal_connect_after (G_OBJECT (scg->hpane), "size-allocate",
			G_CALLBACK (cb_check_resize), scg);

		g_signal_connect_data (G_OBJECT (scg->grid),
			"size-allocate",
			G_CALLBACK (scg_scrollbar_config), scg, NULL,
			G_CONNECT_AFTER | G_CONNECT_SWAPPED);
		g_signal_connect_object (G_OBJECT (scg->grid),
			"destroy",
			G_CALLBACK (cb_table_destroy), G_OBJECT (scg),
			G_CONNECT_SWAPPED);

		gnm_sheet_view_attach_control (sv, GNM_SHEET_CONTROL (scg));

		g_object_connect (G_OBJECT (sheet),
			 "swapped_signal::notify::text-is-rtl", cb_scg_direction_changed, scg,
			 "swapped_signal::notify::display-formulas", cb_scg_redraw, scg,
			 "swapped_signal::notify::display-zeros", cb_scg_redraw, scg,
			 "swapped_signal::notify::display-grid", cb_scg_redraw, scg,
			 "swapped_signal::notify::display-column-header", scg_adjust_preferences, scg,
			 "swapped_signal::notify::display-row-header", scg_adjust_preferences, scg,
			 "swapped_signal::notify::use-r1c1", cb_scg_redraw, scg,
			 "swapped_signal::notify::display-outlines", cb_scg_redraw_resize, scg,
			 "swapped_signal::notify::display-outlines-below", cb_scg_redraw_resize, scg,
			 "swapped_signal::notify::display-outlines-right", cb_scg_redraw_resize, scg,
			 "swapped_signal::notify::columns", cb_scg_sheet_resized, scg,
			 "swapped_signal::notify::rows", cb_scg_sheet_resized, scg,
			 NULL);
	} else {
		scg->active_panes = 0;
		scg->grid = GTK_GRID (gtk_grid_new ());
		g_object_ref (scg->grid);
		sheet->hide_col_header = sheet->hide_row_header = FALSE;
		if (sheet->sheet_type == GNM_SHEET_OBJECT) {
			/* WHY store this in ->vs?  */
			scg->vs = g_object_new (GOC_TYPE_CANVAS,
						"hexpand", TRUE,
						"vexpand", TRUE,
						NULL);
			gtk_style_context_add_class (gtk_widget_get_style_context (scg->vs),
						     "full-sheet");
			gtk_grid_attach (scg->grid, scg->vs, 0, 0, 1, 1);
			gtk_widget_set_can_focus (scg->vs, TRUE);
			gtk_widget_set_can_default (scg->vs, TRUE);
			g_signal_connect (G_OBJECT (scg->vs), "key-press-event",
			                  G_CALLBACK (sheet_object_key_pressed), scg);
		}
		gnm_sheet_view_attach_control (sv, GNM_SHEET_CONTROL (scg));
		if (scg->vs) {
			g_object_set_data (G_OBJECT (scg->vs), "sheet-control", scg);
			if (sheet->sheet_objects) {
				/* we need an idle function because not everything is initialized at this point */
				sheet_object_new_view ((SheetObject *) sheet->sheet_objects->data,
						       (SheetObjectViewContainer*) scg->vs);
				g_idle_add ((GSourceFunc) post_create_cb, scg);
			}
		}
	}

	scg->label = g_object_new
		(GNM_NOTEBOOK_BUTTON_TYPE,
		 "label", sheet->name_unquoted,
		 //"valign", GTK_ALIGN_START,
		 "background-color",
		 (sheet->tab_color
		  ? go_color_to_gdk_rgba (sheet->tab_color->go_color,
					  &cback)
		  : NULL),
		 "text-color",
		 (sheet->tab_text_color
		  ? go_color_to_gdk_rgba (sheet->tab_text_color->go_color,
					  &cfore)
		  : NULL),
		 NULL);
	g_object_ref (scg->label);

	g_signal_connect (G_OBJECT (scg->grid),
			  "screen-changed",
			  G_CALLBACK (cb_screen_changed),
			  scg);

	return scg;
}

static void
scg_comment_timer_clear (SheetControlGUI *scg)
{
	if (scg->comment.timer != 0) {
		g_source_remove (scg->comment.timer);
		scg->comment.timer = 0;
	}
}

static void
scg_im_destroy (SheetControlGUI *scg) {
	if (scg->im.timer != 0) {
		g_source_remove (scg->im.timer);
		scg->im.timer = 0;
	}
	if (scg->im.item) {
		gtk_widget_destroy (scg->im.item);
		scg->im.item = NULL;
	}
}

static void
scg_finalize (GObject *object)
{
	SheetControlGUI *scg = GNM_SCG (object);
	SheetControl	*sc = (SheetControl *) scg;
	Sheet		*sheet = scg_sheet (scg);
	GSList *ptr;

	/* remove the object view before we disappear */
	scg_object_unselect (scg, NULL);
	if (*scg->pane)
		for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next )
			SCG_FOREACH_PANE (scg, pane,
				g_object_unref (
					sheet_object_get_view (ptr->data, (SheetObjectViewContainer *)pane));
			);

	if (scg->col_group.buttons) {
		g_ptr_array_free (scg->col_group.buttons, TRUE);
		g_ptr_array_free (scg->row_group.buttons, TRUE);
	}

	if (scg->pane_drag_handler) {
		g_source_remove (scg->pane_drag_handler);
		scg->pane_drag_handler = 0;
	}

	if (scg->scroll_bar_timer) {
		g_source_remove (scg->scroll_bar_timer);
		scg->scroll_bar_timer = 0;
	}

	scg_comment_timer_clear (scg);

	if (scg->delayedMovement.timer != 0) {
		g_source_remove (scg->delayedMovement.timer);
		scg->delayedMovement.timer = 0;
	}
	scg_comment_unselect (scg, scg->comment.selected);

	scg_im_destroy (scg);

	if (sc->view) {
		Sheet *sheet = sv_sheet (sc->view);
		g_signal_handlers_disconnect_by_func (sheet, scg_adjust_preferences, scg);
		g_signal_handlers_disconnect_by_func (sheet, cb_scg_redraw, scg);
		g_signal_handlers_disconnect_by_func (sheet, cb_scg_redraw_resize, scg);
		g_signal_handlers_disconnect_by_func (sheet, cb_scg_sheet_resized, scg);
		g_signal_handlers_disconnect_by_func (sheet, cb_scg_direction_changed, scg);
		gnm_sheet_view_detach_control (sc->view, sc);
	}

	if (scg->grid) {
		gtk_widget_destroy (GTK_WIDGET (scg->grid));
		g_object_unref (scg->grid);
		scg->grid = NULL;
	}

	g_clear_object (&scg->label);

	if (scg->wbcg != NULL)
		g_object_weak_unref (G_OBJECT (scg->wbcg),
			(GWeakNotify) cb_wbc_destroyed,
			scg);

	(*scg_parent_class->finalize) (object);
}

static void
scg_unant (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;

	g_return_if_fail (GNM_IS_SCG (scg));

	/* Always have a pane 0 */
	if (scg->active_panes == 0  || scg->pane[0]->cursor.animated == NULL)
		return;

	SCG_FOREACH_PANE (scg, pane, {
		GSList *l;

		for (l = pane->cursor.animated; l; l = l->next) {
			GocItem *item = l->data;
			goc_item_destroy (item);
		}

		g_slist_free (pane->cursor.animated);
		pane->cursor.animated = NULL;
	});
}

static void
scg_ant (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	GList *l;

	g_return_if_fail (GNM_IS_SCG (scg));

	if (scg->active_panes == 0)
		return;

	/* Always have a grid 0 */
	if (NULL != scg->pane[0]->cursor.animated)
		scg_unant (sc);

	for (l = sc->view->ants; l; l = l->next) {
		GnmRange const *r = l->data;

		SCG_FOREACH_PANE (scg, pane, {
			GnmItemCursor *ic = GNM_ITEM_CURSOR (goc_item_new (
				pane->grid_items,
				gnm_item_cursor_get_type (),
				"SheetControlGUI", scg,
				"style",	GNM_ITEM_CURSOR_ANTED,
				NULL));
			gnm_item_cursor_bound_set (ic, r);
			pane->cursor.animated =
				g_slist_prepend (pane->cursor.animated, ic);
		});
	}
}

void
scg_adjust_preferences (SheetControlGUI *scg)
{
	Sheet const *sheet = scg_sheet (scg);

	SCG_FOREACH_PANE (scg, pane, {
		if (pane->col.canvas != NULL) {
			gtk_widget_set_visible (GTK_WIDGET (pane->col.canvas),
						!sheet->hide_col_header);
		}

		if (pane->row.canvas != NULL) {
			gtk_widget_set_visible (GTK_WIDGET (pane->row.canvas),
						!sheet->hide_row_header);
		}
	});

	if (scg->select_all_btn) {
		/* we used to test for the corner table existence, why??? */
		gboolean visible = !(sheet->hide_col_header || sheet->hide_row_header);
		gtk_widget_set_visible (scg->select_all_btn, visible);
		gtk_widget_set_visible (scg->row_group.button_box, visible);
		gtk_widget_set_visible (scg->col_group.button_box, visible);

		if (scg_wbc (scg) != NULL) {
			WorkbookView *wbv = wb_control_view (scg_wbc (scg));
			gtk_widget_set_visible (scg->hs,
						wbv->show_horizontal_scrollbar);

			gtk_widget_set_visible (scg->vs,
						wbv->show_vertical_scrollbar);
		}
	}
}

/***************************************************************************/

enum {
	CONTEXT_CUT	= 1,
	CONTEXT_COPY,
	CONTEXT_PASTE,
	CONTEXT_PASTE_SPECIAL,
	CONTEXT_INSERT,
	CONTEXT_DELETE,
	CONTEXT_CLEAR_CONTENT,
	CONTEXT_FORMAT_CELL,
	CONTEXT_FORMAT_CELL_COND,
	CONTEXT_CELL_AUTOFIT_WIDTH,
	CONTEXT_CELL_AUTOFIT_HEIGHT,
	CONTEXT_CELL_MERGE,
	CONTEXT_CELL_UNMERGE,
	CONTEXT_COL_WIDTH,
	CONTEXT_COL_HIDE,
	CONTEXT_COL_UNHIDE,
	CONTEXT_COL_AUTOFIT,
	CONTEXT_ROW_HEIGHT,
	CONTEXT_ROW_HIDE,
	CONTEXT_ROW_UNHIDE,
	CONTEXT_ROW_AUTOFIT,
	CONTEXT_COMMENT_EDIT,
	CONTEXT_COMMENT_ADD,
	CONTEXT_COMMENT_REMOVE,
	CONTEXT_HYPERLINK_EDIT,
	CONTEXT_HYPERLINK_ADD,
	CONTEXT_HYPERLINK_REMOVE,
	CONTEXT_DATA_SLICER_REFRESH,	/* refresh and redraw */
	CONTEXT_DATA_SLICER_EDIT	/* prop dialog */
};
static void
context_menu_handler (GnmPopupMenuElement const *element,
		      gpointer user_data)
{
	SheetControlGUI *scg = user_data;
	SheetControl	*sc = (SheetControl *) scg;
	SheetView	*sv = sc->view;
	Sheet		*sheet = sv->sheet;
	WBCGtk		*wbcg = scg->wbcg;
	WorkbookControl *wbc = sc->wbc;

	g_return_if_fail (element != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	switch (element->index) {
	case CONTEXT_CUT:
		gnm_sheet_view_selection_cut (sv, wbc);
		break;
	case CONTEXT_COPY:
		gnm_sheet_view_selection_copy (sv, wbc);
		break;
	case CONTEXT_PASTE:
		cmd_paste_to_selection (wbc, sv, PASTE_DEFAULT);
		break;
	case CONTEXT_PASTE_SPECIAL:
		dialog_paste_special (wbcg);
		break;
	case CONTEXT_INSERT:
		dialog_insert_cells (wbcg);
		break;
	case CONTEXT_DELETE:
		dialog_delete_cells (wbcg);
		break;
	case CONTEXT_CLEAR_CONTENT:
		cmd_selection_clear (wbc, CLEAR_VALUES);
		break;
	case CONTEXT_FORMAT_CELL:
		dialog_cell_format (wbcg, FD_CURRENT, 0);
		break;
	case CONTEXT_FORMAT_CELL_COND:
		dialog_cell_format_cond (wbcg);
		break;
	case CONTEXT_CELL_AUTOFIT_HEIGHT:
		workbook_cmd_autofit_selection
			(wbc, wb_control_cur_sheet (wbc), FALSE);
		break;
	case CONTEXT_CELL_AUTOFIT_WIDTH:
		workbook_cmd_autofit_selection
			(wbc, wb_control_cur_sheet (wbc), TRUE);
		break;
	case CONTEXT_CELL_MERGE : {
		GSList *range_list = selection_get_ranges
			(wb_control_cur_sheet_view (wbc), FALSE);
		cmd_merge_cells (wbc, wb_control_cur_sheet (wbc), range_list, FALSE);
		range_fragment_free (range_list);
	}
		break;
	case CONTEXT_CELL_UNMERGE : {
		GSList *range_list = selection_get_ranges
			(wb_control_cur_sheet_view (wbc), FALSE);
		cmd_unmerge_cells (wbc, wb_control_cur_sheet (wbc), range_list);
		range_fragment_free (range_list);

	}
		break;
	case CONTEXT_COL_WIDTH:
		dialog_col_width (wbcg, FALSE);
		break;
	case CONTEXT_COL_AUTOFIT:
		workbook_cmd_resize_selected_colrow
			(wbc, wb_control_cur_sheet (wbc), TRUE, -1);
		break;
	case CONTEXT_COL_HIDE:
		cmd_selection_colrow_hide (wbc, TRUE, FALSE);
		break;
	case CONTEXT_COL_UNHIDE:
		cmd_selection_colrow_hide (wbc, TRUE, TRUE);
		break;
	case CONTEXT_ROW_HEIGHT:
		dialog_row_height (wbcg, FALSE);
		break;
	case CONTEXT_ROW_AUTOFIT:
		workbook_cmd_resize_selected_colrow
			(wbc, wb_control_cur_sheet (wbc), FALSE, -1);
		break;
	case CONTEXT_ROW_HIDE:
		cmd_selection_colrow_hide (wbc, FALSE, FALSE);
		break;
	case CONTEXT_ROW_UNHIDE:
		cmd_selection_colrow_hide (wbc, FALSE, TRUE);
		break;
	case CONTEXT_COMMENT_EDIT:
	case CONTEXT_COMMENT_ADD:
		dialog_cell_comment (wbcg, sheet, &sv->edit_pos);
		break;
	case CONTEXT_COMMENT_REMOVE:
		cmd_selection_clear (GNM_WBC (wbcg), CLEAR_COMMENTS);
		break;
	case CONTEXT_HYPERLINK_EDIT:
	case CONTEXT_HYPERLINK_ADD:
		dialog_hyperlink (wbcg, sc);
		break;

	case CONTEXT_HYPERLINK_REMOVE: {
		GnmStyle *style = gnm_style_new ();
		GSList *l;
		int n_links = 0;
		gchar const *format;
		gchar *name;

		for (l = scg_view (scg)->selections; l != NULL; l = l->next) {
			GnmRange const *r = l->data;
			GnmStyleList *styles;

			styles = sheet_style_collect_hlinks (sheet, r);
			n_links += g_slist_length (styles);
			style_list_free (styles);
		}
		format = ngettext ("Remove %d Link", "Remove %d Links", n_links);
		name = g_strdup_printf (format, n_links);
		gnm_style_set_hlink (style, NULL);
		cmd_selection_format (wbc, style, NULL, name);
		g_free (name);
		break;
	}
	case CONTEXT_DATA_SLICER_REFRESH:
		cmd_slicer_refresh (wbc);
		break;
	case CONTEXT_DATA_SLICER_EDIT:
		dialog_data_slicer (wbcg, FALSE);
		break;

	default:
		break;
	}
}

void
scg_context_menu (SheetControlGUI *scg, GdkEvent *event,
		  gboolean is_col, gboolean is_row)
{
	SheetView *sv	 = scg_view (scg);
	Sheet	  *sheet = sv_sheet (sv);

	enum {
		CONTEXT_DISPLAY_FOR_CELLS		= 1 << 0,
		CONTEXT_DISPLAY_FOR_ROWS		= 1 << 1,
		CONTEXT_DISPLAY_FOR_COLS		= 1 << 2,
		CONTEXT_DISPLAY_WITH_HYPERLINK		= 1 << 3,
		CONTEXT_DISPLAY_WITHOUT_HYPERLINK	= 1 << 4,
		CONTEXT_DISPLAY_WITH_HYPERLINK_IN_RANGE	= 1 << 5,
		CONTEXT_DISPLAY_WITH_DATA_SLICER	= 1 << 6,
		CONTEXT_DISPLAY_WITH_DATA_SLICER_ROW	= 1 << 7,
		CONTEXT_DISPLAY_WITH_DATA_SLICER_COL	= 1 << 8,
		CONTEXT_DISPLAY_WITH_COMMENT		= 1 << 9,
		CONTEXT_DISPLAY_WITHOUT_COMMENT	        = 1 << 10,
		CONTEXT_DISPLAY_WITH_COMMENT_IN_RANGE	= 1 << 11
	};
	enum {
		CONTEXT_DISABLE_PASTE_SPECIAL	= 1 << 0,
		CONTEXT_DISABLE_FOR_ROWS	= 1 << 1,
		CONTEXT_DISABLE_FOR_COLS	= 1 << 2,
		CONTEXT_DISABLE_FOR_CELLS       = 1 << 3,
		CONTEXT_DISABLE_FOR_DISCONTIGUOUS_SELECTION = 1 << 4,
		CONTEXT_DISABLE_FOR_ALL_COLS        = 1 << 5,
		CONTEXT_DISABLE_FOR_ALL_ROWS        = 1 << 6,
		CONTEXT_DISABLE_FOR_NOMERGES        = 1 << 7,
		CONTEXT_DISABLE_FOR_ONLYMERGES        = 1 << 8
	};

	/* Note: keep the following two in sync!*/
	enum {
		POPUPITEM_CUT = 0,
		POPUPITEM_COPY,
		POPUPITEM_PASTE,
		POPUPITEM_PASTESPECIAL,
		POPUPITEM_SEP1,
		POPUPITEM_INSERT_CELL,
		POPUPITEM_DELETE_CELL,
		POPUPITEM_INSERT_COLUMN,
		POPUPITEM_DELETE_COLUMN,
		POPUPITEM_INSERT_ROW,
		POPUPITEM_DELETE_ROW,
		POPUPITEM_CLEAR_CONTENTS,
		POPUPITEM_SEP2,
		POPUPITEM_COMMENT_ADD,
		POPUPITEM_COMMENT_EDIT,
		POPUPITEM_COMMENT_REMOVE,
		POPUPITEM_LINK_ADD,
		POPUPITEM_LINK_EDIT,
		POPUPITEM_LINK_REMOVE,
		POPUPITEM_SEP3,
		POPUPITEM_DATASLICER_EDIT,
		POPUPITEM_DATASLICER_REFRESH,
		POPUPITEM_DATASLICER_FIELD_ORDER,
		POPUPITEM_DATASLICER_LEFT,
		POPUPITEM_DATASLICER_RIGHT,
		POPUPITEM_DATASLICER_UP,
		POPUPITEM_DATASLICER_DOWN,
		POPUPITEM_DATASLICER_SUBMENU,
		POPUPITEM_FORMAT
	};

	static GnmPopupMenuElement popup_elements[] = {
		{ N_("Cu_t"),           "edit-cut",
		    0, 0, CONTEXT_CUT, NULL },
		{ N_("_Copy"),          "edit-copy",
		    0, 0, CONTEXT_COPY, NULL },
		{ N_("_Paste"),         "edit-paste",
		    0, 0, CONTEXT_PASTE, NULL },
		{ N_("Paste _Special"),	"edit-paste",
		    0, CONTEXT_DISABLE_PASTE_SPECIAL, CONTEXT_PASTE_SPECIAL, NULL },

		{ "", NULL, 0, 0, 0, NULL },

		{ N_("_Insert Cells..."),	NULL,
		    CONTEXT_DISPLAY_FOR_CELLS,
		  CONTEXT_DISABLE_FOR_DISCONTIGUOUS_SELECTION, CONTEXT_INSERT, NULL },
		{ N_("_Delete Cells..."), "edit-delete",
		    CONTEXT_DISPLAY_FOR_CELLS,
		  CONTEXT_DISABLE_FOR_DISCONTIGUOUS_SELECTION, CONTEXT_DELETE, NULL },
		{ N_("_Insert Column(s)"), "gnumeric-column-add",
		    CONTEXT_DISPLAY_FOR_COLS,
		  CONTEXT_DISABLE_FOR_DISCONTIGUOUS_SELECTION,
		  CONTEXT_INSERT, NULL },
		{ N_("_Delete Column(s)"), "gnumeric-column-delete",
		    CONTEXT_DISPLAY_FOR_COLS,
		  CONTEXT_DISABLE_FOR_DISCONTIGUOUS_SELECTION,
		  CONTEXT_DELETE, NULL },
		{ N_("_Insert Row(s)"), "gnumeric-row-add",
		    CONTEXT_DISPLAY_FOR_ROWS,
		  CONTEXT_DISABLE_FOR_DISCONTIGUOUS_SELECTION,
		  CONTEXT_INSERT, NULL },
		{ N_("_Delete Row(s)"), "gnumeric-row-delete",
		    CONTEXT_DISPLAY_FOR_ROWS,
		  CONTEXT_DISABLE_FOR_DISCONTIGUOUS_SELECTION,
		  CONTEXT_DELETE, NULL },

		{ N_("Clear Co_ntents"), "edit-clear",
		    0, 0, CONTEXT_CLEAR_CONTENT, NULL },

		{ "", NULL, CONTEXT_DISPLAY_FOR_CELLS, 0, 0, NULL },

		{ N_("Add _Comment..."),	  "gnumeric-comment-add",
		    CONTEXT_DISPLAY_WITHOUT_COMMENT, 0, CONTEXT_COMMENT_ADD, NULL },
		{ N_("Edit Co_mment..."),"gnumeric-comment-edit",
		    CONTEXT_DISPLAY_WITH_COMMENT, 0, CONTEXT_COMMENT_EDIT, NULL },
		{ N_("_Remove Comments"),	  "gnumeric-comment-delete",
		    CONTEXT_DISPLAY_WITH_COMMENT_IN_RANGE, 0, CONTEXT_COMMENT_REMOVE, NULL },

		{ N_("Add _Hyperlink..."),	  "gnumeric-link-add",
		    CONTEXT_DISPLAY_WITHOUT_HYPERLINK, 0,
		    CONTEXT_HYPERLINK_ADD, NULL },
		{ N_("Edit _Hyperlink..."),	  "gnumeric-link-edit",
		    CONTEXT_DISPLAY_WITH_HYPERLINK, 0,
		    CONTEXT_HYPERLINK_EDIT, NULL },
		{ N_("_Remove Hyperlink"),	  "gnumeric-link-delete",
		    CONTEXT_DISPLAY_WITH_HYPERLINK_IN_RANGE, 0,
		    CONTEXT_HYPERLINK_REMOVE, NULL },

		{ "", NULL, 0, 0, 0, NULL },

		{ N_("_Edit DataSlicer"),	NULL,
		     CONTEXT_DISPLAY_WITH_DATA_SLICER, 0,
		     CONTEXT_DATA_SLICER_EDIT, NULL },
		{ N_("_Refresh DataSlicer"),	NULL,
		     CONTEXT_DISPLAY_WITH_DATA_SLICER, 0,
		     CONTEXT_DATA_SLICER_REFRESH, NULL },

		{ N_("DataSlicer Field _Order "), NULL,
		     CONTEXT_DISPLAY_WITH_DATA_SLICER_ROW | CONTEXT_DISPLAY_WITH_DATA_SLICER_COL, 0,
		     -1, NULL },	/* start sub menu */
		{ N_("Left"), "go-previous",
		     CONTEXT_DISPLAY_WITH_DATA_SLICER_ROW, 0,
		     CONTEXT_DATA_SLICER_REFRESH, NULL },
		{ N_("Right"), "go-next",
		     CONTEXT_DISPLAY_WITH_DATA_SLICER_ROW, 0,
		     CONTEXT_DATA_SLICER_REFRESH, NULL },
		{ N_("Up"), "go-up",
		     CONTEXT_DISPLAY_WITH_DATA_SLICER_COL, 0,
		     CONTEXT_DATA_SLICER_REFRESH, NULL },
		{ N_("Down"), "go-down",
		     CONTEXT_DISPLAY_WITH_DATA_SLICER_COL, 0,
		     CONTEXT_DATA_SLICER_REFRESH, NULL },
		{ "",	NULL,
		     CONTEXT_DISPLAY_WITH_DATA_SLICER_ROW | CONTEXT_DISPLAY_WITH_DATA_SLICER_COL, 0,
		     -1, NULL },	/* end sub menu */

		{ N_("_Format All Cells..."), GTK_STOCK_PROPERTIES,
		    0, 0, CONTEXT_FORMAT_CELL, NULL },
		{ N_("C_onditional Formatting..."), GTK_STOCK_PROPERTIES,
		    0, 0, CONTEXT_FORMAT_CELL_COND, NULL },
		{ N_("Cell"), NULL, 0, 0, -1, NULL},/* start sub menu */
		{ N_("_Merge"), "gnumeric-cells-merge",   0,
		  CONTEXT_DISABLE_FOR_ONLYMERGES, CONTEXT_CELL_MERGE, NULL },
		{ N_("_Unmerge"), "gnumeric-cells-split",   0,
		  CONTEXT_DISABLE_FOR_NOMERGES, CONTEXT_CELL_UNMERGE, NULL },
		{ N_("Auto Fit _Width"), "gnumeric-column-size",   0, 0, CONTEXT_CELL_AUTOFIT_WIDTH, NULL },
		{ N_("Auto Fit _Height"), "gnumeric-row-size",   0, 0, CONTEXT_CELL_AUTOFIT_HEIGHT, NULL },
		{ "", NULL, 0, 0, -1, NULL},/* end sub menu */


		/* Column specific (Note some labels duplicate row labels) */
		{ N_("Column"), NULL, 0, 0, -1, NULL},/* start sub menu */
		{ N_("_Width..."), "gnumeric-column-size",   0, 0, CONTEXT_COL_WIDTH, NULL },
		{ N_("_Auto Fit Width"), "gnumeric-column-size",   0, 0, CONTEXT_COL_AUTOFIT, NULL },
		{ N_("_Hide"),	   "gnumeric-column-hide",   0, CONTEXT_DISABLE_FOR_ALL_COLS, CONTEXT_COL_HIDE, NULL },
		{ N_("_Unhide"),   "gnumeric-column-unhide", 0, 0, CONTEXT_COL_UNHIDE, NULL },
		{ "", NULL, 0, 0, -1, NULL},/* end sub menu */

		/* Row specific (Note some labels duplicate col labels) */
		{ N_("Row"), NULL, 0, 0, -1, NULL},/* start sub menu */
		{ N_("Hei_ght..."), "gnumeric-row-size",   0, 0, CONTEXT_ROW_HEIGHT, NULL },
		{ N_("_Auto Fit Height"), "gnumeric-row-size",   0, 0, CONTEXT_ROW_AUTOFIT, NULL },
		{ N_("_Hide"),	    "gnumeric-row-hide",   0, CONTEXT_DISABLE_FOR_ALL_ROWS, CONTEXT_ROW_HIDE, NULL },
		{ N_("_Unhide"),    "gnumeric-row-unhide", 0, 0, CONTEXT_ROW_UNHIDE, NULL },
		{ "", NULL, 0, 0, -1, NULL},/* end sub menu */

		{ NULL, NULL, 0, 0, 0, NULL },
	};

	/* row and column specific operations */
	int display_filter =
		((!is_col && !is_row) ? CONTEXT_DISPLAY_FOR_CELLS : 0) |
		(is_col ? CONTEXT_DISPLAY_FOR_COLS : 0) |
		(is_row ? CONTEXT_DISPLAY_FOR_ROWS : 0);

	/* Paste special only applies to local copies, not cuts, or remote
	 * items
	 */
	int sensitivity_filter =
		(!gnm_app_clipboard_is_empty () &&
		!gnm_app_clipboard_is_cut ())
		? 0 : CONTEXT_DISABLE_PASTE_SPECIAL;

	GSList *l;
	gboolean has_link = FALSE, has_comment = FALSE;
	int n_comments = 0, n_links = 0, n_cols = 0, n_rows = 0, n_cells = 0;
	GnmSheetSlicer *slicer;
	GnmRange rge;
	int n_sel = 0;
	gboolean full_sheet = FALSE, only_merges = TRUE, no_merges = TRUE;

	wbcg_edit_finish (scg->wbcg, WBC_EDIT_REJECT, NULL);

	/* Now see if there is some selection which selects a whole row or a
	 * whole column and disable the insert/delete col/row menu items
	 * accordingly
	 */
	for (l = scg_view (scg)->selections; l != NULL; l = l->next) {
		GnmRange const *r = l->data;
		GnmRange const *merge;
		GSList *objs, *merges;
		GnmStyleList *styles;
		int h, w;
		gboolean rfull_h = range_is_full (r, sheet, TRUE);
		gboolean rfull_v = range_is_full (r, sheet, FALSE);

		n_sel++;

		if (!range_is_singleton (r)) {
			merge = gnm_sheet_merge_is_corner (sheet, &(r->start));
			if (NULL == merge || !range_equal (merge, r))
				only_merges = FALSE;
			merges = gnm_sheet_merge_get_overlap (sheet, r);
			if (merges != NULL) {
				no_merges = FALSE;
				g_slist_free (merges);
			}
		}

		if (rfull_v) {
			display_filter |= CONTEXT_DISPLAY_FOR_COLS;
			display_filter &= ~CONTEXT_DISPLAY_FOR_CELLS;
			sensitivity_filter |= CONTEXT_DISABLE_FOR_ALL_ROWS;
		} else
			sensitivity_filter |= CONTEXT_DISABLE_FOR_ROWS;


		if (rfull_h) {
			display_filter |= CONTEXT_DISPLAY_FOR_ROWS;
			display_filter &= ~CONTEXT_DISPLAY_FOR_CELLS;
			sensitivity_filter |= CONTEXT_DISABLE_FOR_ALL_COLS;
		} else
			sensitivity_filter |= CONTEXT_DISABLE_FOR_COLS;

		if (!(rfull_h || rfull_v))
			sensitivity_filter |= CONTEXT_DISABLE_FOR_CELLS;

		full_sheet = full_sheet || (rfull_h && rfull_v);

		h = range_height (r);
		w = range_width (r);
		n_cols += w;
		n_rows += h;
		n_cells += w * h;

		styles = sheet_style_collect_hlinks (sheet, r);
		n_links += g_slist_length (styles);
		style_list_free (styles);

		objs = sheet_objects_get (sheet, r, GNM_CELL_COMMENT_TYPE);
		n_comments += g_slist_length (objs);
		g_slist_free (objs);
	}

	if (only_merges)
		sensitivity_filter |= CONTEXT_DISABLE_FOR_ONLYMERGES;
	if (no_merges)
		sensitivity_filter |= CONTEXT_DISABLE_FOR_NOMERGES;


	if ((display_filter & CONTEXT_DISPLAY_FOR_COLS) &&
	    (display_filter & CONTEXT_DISPLAY_FOR_ROWS))
		display_filter = 0;
	if (n_sel > 1)
		sensitivity_filter |= CONTEXT_DISABLE_FOR_DISCONTIGUOUS_SELECTION;

	has_comment = (sheet_get_comment (sheet, &sv->edit_pos) != NULL);
	range_init_cellpos (&rge, &sv->edit_pos);
	has_link = (NULL != sheet_style_region_contains_link (sheet, &rge));

	slicer = gnm_sheet_view_editpos_in_slicer (scg_view (scg));
	/* FIXME: disabled for now */
	if (0 && slicer) {
		GODataSlicerField *dsf = gnm_sheet_slicer_field_header_at_pos (slicer, &sv->edit_pos);
		if (NULL != dsf) {
			if (go_data_slicer_field_get_field_type_pos (dsf, GDS_FIELD_TYPE_COL) >= 0)
				display_filter |= CONTEXT_DISPLAY_WITH_DATA_SLICER_COL;
			if (go_data_slicer_field_get_field_type_pos (dsf, GDS_FIELD_TYPE_ROW) >= 0)
				display_filter |= CONTEXT_DISPLAY_WITH_DATA_SLICER_ROW;
		}
		display_filter |= CONTEXT_DISPLAY_WITH_DATA_SLICER;
		display_filter &= ~CONTEXT_DISPLAY_FOR_CELLS;
	}

	if (display_filter & CONTEXT_DISPLAY_FOR_CELLS) {
		char const *format;
		display_filter |= ((has_link) ?
				   CONTEXT_DISPLAY_WITH_HYPERLINK : CONTEXT_DISPLAY_WITHOUT_HYPERLINK);
		display_filter |= ((n_links > 0) ?
				   CONTEXT_DISPLAY_WITH_HYPERLINK_IN_RANGE : CONTEXT_DISPLAY_WITHOUT_HYPERLINK);
		display_filter |= ((has_comment) ?
				   CONTEXT_DISPLAY_WITH_COMMENT : CONTEXT_DISPLAY_WITHOUT_COMMENT);
		display_filter |= ((n_comments > 0) ?
				   CONTEXT_DISPLAY_WITH_COMMENT_IN_RANGE : CONTEXT_DISPLAY_WITHOUT_COMMENT);
		if (n_links > 0) {
			/* xgettext : %d gives the number of links. This is input to ngettext. */
			format = ngettext ("_Remove %d Link", "_Remove %d Links", n_links);
			popup_elements[POPUPITEM_LINK_REMOVE].allocated_name = g_strdup_printf (format, n_links);
		}
		if (n_comments > 0) {
			/* xgettext : %d gives the number of comments. This is input to ngettext. */
			format = ngettext ("_Remove %d Comment", "_Remove %d Comments", n_comments);
			popup_elements[POPUPITEM_COMMENT_REMOVE].allocated_name = g_strdup_printf (format, n_comments);
		}
		format = ngettext ("_Insert %d Cell...", "_Insert %d Cells...", n_cells);
		popup_elements[POPUPITEM_INSERT_CELL].allocated_name = g_strdup_printf (format, n_cells);
		format = ngettext ("_Delete %d Cell...", "_Delete %d Cells...", n_cells);
		popup_elements[POPUPITEM_DELETE_CELL].allocated_name = g_strdup_printf (format, n_cells);
	}

	if (display_filter & CONTEXT_DISPLAY_FOR_COLS) {
		char const *format;
		format = ngettext ("_Insert %d Column", "_Insert %d Columns", n_cols);
		popup_elements[POPUPITEM_INSERT_COLUMN].allocated_name = g_strdup_printf (format, n_cols);
		format = ngettext ("_Delete %d Column", "_Delete %d Columns", n_cols);
		popup_elements[POPUPITEM_DELETE_COLUMN].allocated_name = g_strdup_printf (format, n_cols);
		if (!(sensitivity_filter & (CONTEXT_DISABLE_FOR_CELLS | CONTEXT_DISABLE_FOR_ROWS))) {
			format = ngettext ("_Format %d Column", "_Format %d Columns", n_cols);
			popup_elements[POPUPITEM_FORMAT].allocated_name
				= g_strdup_printf (format, n_cols);
		}
	}
	if (display_filter & CONTEXT_DISPLAY_FOR_ROWS) {
		char const *format;
		format = ngettext ("_Insert %d Row", "_Insert %d Rows", n_rows);
		popup_elements[POPUPITEM_INSERT_ROW].allocated_name = g_strdup_printf (format, n_rows);
		format = ngettext ("_Delete %d Row", "_Delete %d Rows", n_rows);
		popup_elements[POPUPITEM_DELETE_ROW].allocated_name = g_strdup_printf (format, n_rows);

		if (!(sensitivity_filter & (CONTEXT_DISABLE_FOR_CELLS | CONTEXT_DISABLE_FOR_COLS))) {
			format = ngettext ("_Format %d Row", "_Format %d Rows", n_rows);
			popup_elements[POPUPITEM_FORMAT].allocated_name
				= g_strdup_printf (format, n_rows);
		}
	}
	if (!popup_elements[POPUPITEM_FORMAT].allocated_name && !full_sheet) {
		char const *format;
		format = ngettext ("_Format %d Cell...", "_Format %d Cells...", n_cells);
		popup_elements[POPUPITEM_FORMAT].allocated_name = g_strdup_printf (format, n_cells);
	}


	gnm_create_popup_menu (popup_elements,
			       &context_menu_handler, scg, NULL,
			       display_filter, sensitivity_filter, event);
}

static gboolean
cb_redraw_sel (G_GNUC_UNUSED SheetView *sv, GnmRange const *r, gpointer user_data)
{
	SheetControl *sc = user_data;
	scg_redraw_range (sc, r);
	scg_redraw_headers (sc, TRUE, TRUE, r);
	return TRUE;
}

void
scg_cursor_visible (SheetControlGUI *scg, gboolean is_visible)
{
	SheetControl *sc = (SheetControl *) scg;

	/* there is always a grid 0 */
	if (NULL == scg->pane[0])
		return;

	SCG_FOREACH_PANE (scg, pane,
		gnm_item_cursor_set_visibility (pane->cursor.std, is_visible););

	sv_selection_foreach (sc->view, cb_redraw_sel, sc);
}

/***************************************************************************/

/**
 * scg_mode_edit:
 * @scg:  The sheet control
 *
 * Put @sheet into the standard state 'edit mode'.  This shuts down
 * any object editing and frees any objects that are created but not
 * realized.
 **/
void
scg_mode_edit (SheetControlGUI *scg)
{
	WBCGtk *wbcg;
	g_return_if_fail (GNM_IS_SCG (scg));

	wbcg = scg->wbcg;

	if (wbcg != NULL) /* Can be NULL during destruction */
		wbcg_insert_object_clear (wbcg);

	scg_object_unselect (scg, NULL);

	/* During destruction we have already been disconnected
	 * so don't bother changing the cursor */
	if (scg->grid != NULL &&
	    scg_sheet (scg) != NULL &&
	    scg_view (scg) != NULL) {
		scg_set_display_cursor (scg);
		scg_cursor_visible (scg, TRUE);
	}

	if (wbcg != NULL && wbc_gtk_get_guru (wbcg) != NULL &&
	    scg == wbcg_cur_scg	(wbcg))
		wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);

	if (wbcg)
		wb_control_update_action_sensitivity (GNM_WBC (wbcg));
}

static void
scg_mode_edit_virt (SheetControl *sc)
{
	scg_mode_edit ((SheetControlGUI *)sc);
}

static int
calc_obj_place (GnmPane *pane, gint64 canvas_coord, gboolean is_col,
		double *offset)
{
	gint64 origin;
	int colrow;
	ColRowInfo const *cri;
	Sheet const *sheet = scg_sheet (pane->simple.scg);

	if (is_col) {
		colrow = gnm_pane_find_col (pane, canvas_coord, &origin);
		cri = sheet_col_get_info (sheet, colrow);
	} else {
		colrow = gnm_pane_find_row (pane, canvas_coord, &origin);
		cri = sheet_row_get_info (sheet, colrow);
	}

	/* TODO : handle other anchor types */
	*offset = (canvas_coord - origin) / (double)cri->size_pixels;
	return colrow;
}

#define SO_CLASS(so) GNM_SO_CLASS (G_OBJECT_GET_CLASS(so))

/**
 * scg_object_select:
 * @scg: The #SheetControl to edit in.
 * @so: The #SheetObject to select.
 *
 * Adds @so to the set of selected objects and prepares it for user editing.
 * Adds a reference to @ref if it is selected.
 **/
void
scg_object_select (SheetControlGUI *scg, SheetObject *so)
{
	double *coords;

	if (scg->selected_objects == NULL) {
		if (wb_view_is_protected (sv_wbv (scg_view (scg)), TRUE) ||
		    !wbcg_edit_finish (scg->wbcg, WBC_EDIT_ACCEPT, NULL))
			return;
		g_object_ref (so);

		wbcg_insert_object_clear (scg->wbcg);
		scg_cursor_visible (scg, FALSE);
		scg_set_display_cursor (scg);
		scg_unant (GNM_SHEET_CONTROL (scg));

		scg->selected_objects = g_hash_table_new_full (
			g_direct_hash, g_direct_equal,
			(GDestroyNotify) g_object_unref, (GDestroyNotify) g_free);
		wb_control_update_action_sensitivity (scg_wbc (scg));
	} else {
		g_return_if_fail (g_hash_table_lookup (scg->selected_objects, so) == NULL);
		g_object_ref (so);
	}

	coords = g_new (double, 4);
	scg_object_anchor_to_coords (scg, sheet_object_get_anchor (so), coords);
	g_hash_table_insert (scg->selected_objects, so, coords);
	g_signal_connect_object (so, "unrealized",
		G_CALLBACK (scg_mode_edit), scg, G_CONNECT_SWAPPED);

	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_object_update_bbox (pane, so););
}

static void
cb_scg_object_unselect (SheetObject *so, G_GNUC_UNUSED double *coords, SheetControlGUI *scg)
{
	SCG_FOREACH_PANE (scg, pane, gnm_pane_object_unselect (pane, so););
	g_signal_handlers_disconnect_by_func (so,
		scg_mode_edit, scg);
}

/**
 * scg_object_unselect:
 * @scg: #SheetControlGUI
 * @so: #SheetObject (nullable)
 *
 * Unselect the supplied object, and drop out of edit mode if this is the last
 * one.  If @so is %NULL unselect _all_ objects.
 **/
void
scg_object_unselect (SheetControlGUI *scg, SheetObject *so)
{
	WorkbookControl *wbc = scg_wbc (scg);

	/* cheesy cycle avoidance */
	if (scg->selected_objects == NULL)
		return;

	if (so != NULL) {
		double *pts = g_hash_table_lookup (scg->selected_objects, so);
		g_return_if_fail (pts != NULL);
		cb_scg_object_unselect (so, pts, scg);
		g_hash_table_remove (scg->selected_objects, so);
		if (g_hash_table_size (scg->selected_objects) > 0)
			return;
	} else
		g_hash_table_foreach (scg->selected_objects,
			(GHFunc) cb_scg_object_unselect, scg);

	g_hash_table_destroy (scg->selected_objects);
	scg->selected_objects = NULL;
	scg_mode_edit (scg);
	if (wbc)
		wb_control_update_action_sensitivity (wbc);
}

void
scg_object_select_next	(SheetControlGUI *scg, gboolean reverse)
{
	Sheet *sheet = scg_sheet (scg);
	GSList *ptr = sheet->sheet_objects;

	g_return_if_fail (ptr != NULL);

	if ((scg->selected_objects == NULL) ||
	    (g_hash_table_size (scg->selected_objects) == 0)) {
		scg_object_select (scg, ptr->data);
		return;
	} else {
		GSList *prev = NULL;
		for (; ptr != NULL ; prev = ptr, ptr = ptr->next)
			if (NULL != g_hash_table_lookup
			    (scg->selected_objects, ptr->data)) {
				SheetObject *target;
				if (reverse) {
					if (ptr->next == NULL)
						target = sheet->sheet_objects->data;
					else
						target = ptr->next->data;
				} else {
					if (NULL == prev) {
						GSList *last = g_slist_last (ptr);
						target = last->data;
					} else
						target = prev->data;
				}
				if (ptr->data != target) {
					scg_object_unselect (scg, NULL);
					scg_object_select (scg, target);
					return;
				}
			}
	}
}

typedef struct {
	SheetControlGUI *scg;
	GnmPane	*pane;
	SheetObject	*primary_object;
	int	 drag_type;
	double	 dx, dy;
	gboolean symmetric;
	gboolean snap_to_grid;
	gboolean is_mouse_move;
} ObjDragInfo;

static double
snap_pos_to_grid (ObjDragInfo const *info, gboolean is_col, double pos,
		  gboolean to_min)
{
	GnmPane const *pane = info->pane;
	Sheet const *sheet = scg_sheet (info->scg);
	int cell  = is_col ? pane->first.col        : pane->first.row;
	gint64 pixel = is_col ? pane->first_offset.x : pane->first_offset.y;
	gboolean snap = FALSE;
	int length = 0;
	ColRowInfo const *cr_info;
	int sheet_max = colrow_max (is_col, sheet);

	if (pos < pixel) {
		while (cell > 0 && pos < pixel) {
			cr_info = sheet_colrow_get_info (sheet, --cell, is_col);
			if (cr_info->visible) {
				length = cr_info->size_pixels;
				pixel -= length;
			}
		}
		if (pos < pixel)
			pos = pixel;
	} else {
		do {
			cr_info = sheet_colrow_get_info (sheet, cell, is_col);
			if (cr_info->visible) {
				length = cr_info->size_pixels;
				if (pixel <= pos && pos <= pixel + length)
					snap = TRUE;
				pixel += length;
			}
		} while (++cell < sheet_max && !snap);
		pixel -= length;
		if (snap) {
			if (info->is_mouse_move)
				pos = (fabs (pos - pixel) < fabs (pos - pixel - length)) ? pixel : pixel + length;
			else
				pos = (pixel == pos) ? pixel : (to_min ? pixel : pixel + length);
		}
	}
	return/* sign */ pos;
}

static void
apply_move (SheetObject *so, int x_idx, int y_idx, double *coords,
	    ObjDragInfo *info, gboolean snap_to_grid)
{
	gboolean move_x = (x_idx >= 0);
	gboolean move_y = (y_idx >= 0);
	double x, y;

	x = move_x ? coords[x_idx] + info->dx : 0;
	y = move_y ? coords[y_idx] + info->dy : 0;

	if (snap_to_grid) {
		g_return_if_fail (info->pane != NULL);

		if (move_x)
			x = snap_pos_to_grid (info, TRUE,  x, info->dx < 0.);
		if (move_y)
			y = snap_pos_to_grid (info, FALSE, y, info->dy < 0.);
		if (info->primary_object == so || NULL == info->primary_object) {
			if (move_x) info->dx = x - coords[x_idx];
			if (move_y) info->dy = y - coords[y_idx];
		}
	}

	if (move_x) coords[x_idx] = x;
	if (move_y) coords[y_idx] = y;

	if (info->symmetric && !snap_to_grid) {
		if (move_x) coords[x_idx == 0 ? 2 : 0] -= info->dx;
		if (move_y) coords[y_idx == 1 ? 3 : 1] -= info->dy;
	}
}

static void
drag_object (SheetObject *so, double *coords, ObjDragInfo *info)
{
	static struct {
		int x_idx, y_idx;
	} const idx_info[8] = {
		{ 0, 1}, {-1, 1}, { 2, 1}, { 0,-1},
		{ 2,-1}, { 0, 3}, {-1, 3}, { 2, 3}
	};

	g_return_if_fail (info->drag_type <= 8);

	if (info->drag_type == 8) {
		apply_move (so, 0, 1, coords, info, info->snap_to_grid);
		apply_move (so, 2, 3, coords, info, FALSE);
	} else
		apply_move (so,
			    idx_info[info->drag_type].x_idx,
			    idx_info[info->drag_type].y_idx,
			    coords, info, info->snap_to_grid);
	SCG_FOREACH_PANE (info->scg, pane,
		gnm_pane_object_update_bbox (pane, so););
}

static void
cb_drag_selected_objects (SheetObject *so, double *coords, ObjDragInfo *info)
{
	if (so != info->primary_object)
		drag_object (so, coords, info);
}

/**
 * scg_objects_drag:
 * @scg: #SheetControlGUI
 * @primary: #SheetObject (optionally NULL)
 * @dx:
 * @dy:
 * @drag_type:
 * @symmetric:
 *
 * Move the control points and drag views of the currently selected objects to
 * a new position.  This movement is only made in @scg not in the actual
 * objects.
 **/
void
scg_objects_drag (SheetControlGUI *scg, GnmPane *pane,
		  SheetObject *primary,
		  gdouble *dx, gdouble *dy,
		  int drag_type, gboolean symmetric,
		  gboolean snap_to_grid,
		  gboolean is_mouse_move)
{
	double *coords;

	ObjDragInfo info;
	info.scg = scg;
	info.pane = pane;
	info.primary_object = primary;
	info.dx = *dx;
	info.dy = *dy;
	info.symmetric = symmetric;
	info.drag_type = drag_type;
	info.snap_to_grid = snap_to_grid;
	info.is_mouse_move = is_mouse_move;

	if (primary != NULL) {
		coords = g_hash_table_lookup (scg->selected_objects, primary);
		drag_object (primary, coords, &info);
	}

	g_hash_table_foreach (scg->selected_objects,
		(GHFunc) cb_drag_selected_objects, &info);

	*dx = info.dx;
	*dy = info.dy;
}

typedef struct {
	SheetControlGUI *scg;
	GSList *objects, *anchors;
} CollectObjectsData;
static void
cb_collect_objects_to_commit (SheetObject *so, double *coords, CollectObjectsData *data)
{
	SheetObjectAnchor *anchor = sheet_object_anchor_dup (
		sheet_object_get_anchor (so));
	if (!sheet_object_can_resize (so)) {
		/* FIXME: that code should be invalid */
		double scale = goc_canvas_get_pixels_per_unit (GOC_CANVAS (data->scg->pane[0])) / 72.;
		sheet_object_default_size (so, coords + 2, coords + 3);
		coords[2] *= gnm_app_display_dpi_get (TRUE) * scale;
		coords[3] *= gnm_app_display_dpi_get (FALSE) * scale;
		coords[2] += coords[0];
		coords[3] += coords[1];
	}
	scg_object_coords_to_anchor (data->scg, coords, anchor);
	data->objects = g_slist_prepend (data->objects, so);
	data->anchors = g_slist_prepend (data->anchors, anchor);

	if (!sheet_object_rubber_band_directly (so)) {
		SCG_FOREACH_PANE (data->scg, pane, {
			GocItem **ctrl_pts = g_hash_table_lookup (pane->drag.ctrl_pts, so);
			if (NULL != ctrl_pts[9]) {
				double const *pts = g_hash_table_lookup (
					pane->simple.scg->selected_objects, so);
				SheetObjectView *sov = sheet_object_get_view (so,
					(SheetObjectViewContainer *)pane);

				g_object_unref (ctrl_pts[9]);
				ctrl_pts[9] = NULL;

				if (NULL == sov)
					sov = sheet_object_new_view (so, (SheetObjectViewContainer *) pane);
				if (NULL != sov)
					sheet_object_view_set_bounds (sov, pts, TRUE);
			}
		});
	}
}

static char *
scg_objects_drag_commit_get_undo_text (int drag_type, int n,
				       gboolean created_objects)
{
	char const *format;

	if (created_objects) {
		if (drag_type == 8)
			/* xgettext : %d gives the number of objects. This is input to ngettext. */
			format = ngettext ("Duplicate %d Object", "Duplicate %d Objects", n);
		else
			/* xgettext : %d gives the number of objects. This is input to ngettext. */
			format = ngettext ("Insert %d Object", "Insert %d Objects", n);
	} else {
		if (drag_type == 8)
			/* xgettext : %d gives the number of objects. This is input to ngettext. */
			format = ngettext ("Move %d Object", "Move %d Objects", n);
		else
			/* xgettext : %d gives the number of objects. This is input to ngettext. */
			format = ngettext ("Resize %d Object", "Resize %d Objects", n);
	}

	return g_strdup_printf (format, n);

}

void
scg_objects_drag_commit (SheetControlGUI *scg, int drag_type,
			 gboolean created_objects,
			 GOUndo **pundo, GOUndo **predo, gchar **undo_title)
{
	CollectObjectsData data;
	char *text = NULL;
	GOUndo *undo = NULL;
	GOUndo *redo = NULL;

	data.objects = data.anchors = NULL;
	data.scg = scg;
	g_hash_table_foreach (scg->selected_objects,
		(GHFunc) cb_collect_objects_to_commit, &data);

	undo = sheet_object_move_undo (data.objects, created_objects);
	redo = sheet_object_move_do (data.objects, data.anchors, created_objects);
	text = scg_objects_drag_commit_get_undo_text
		(drag_type,  g_slist_length (data.objects), created_objects);

	if (pundo && predo) {
		*pundo = undo;
		*predo = redo;
		if (undo_title)
			*undo_title = text;
	} else {
		cmd_generic (GNM_WBC (scg_wbcg (scg)),
			     text, undo, redo);
		g_free (text);
	}
	g_slist_free (data.objects);
	g_slist_free_full (data.anchors, g_free);
}

void
scg_objects_nudge (SheetControlGUI *scg, GnmPane *pane,
		   int drag_type, double dx, double dy, gboolean symmetric, gboolean snap_to_grid)
{
	/* no nudging if we are creating an object */
	if (!scg->wbcg->new_object) {
		scg_objects_drag (scg, pane, NULL, &dx, &dy, drag_type, symmetric, snap_to_grid, FALSE);
		scg_objects_drag_commit (scg, drag_type, FALSE, NULL, NULL, NULL);
	}
}

void
scg_object_coords_to_anchor (SheetControlGUI const *scg,
			     double const *coords, SheetObjectAnchor *in_out)
{
	Sheet *sheet = scg_sheet (scg);
	/* pane 0 always exists and the others are always use the same basis */
	GnmPane *pane = scg_pane ((SheetControlGUI *)scg, 0);
	double	tmp[4];
	g_return_if_fail (GNM_IS_SCG (scg));
	g_return_if_fail (coords != NULL);

	in_out->base.direction = GOD_ANCHOR_DIR_NONE_MASK;
	if (coords[0] > coords[2]) {
		tmp[0] = coords[2];
		tmp[2] = coords[0];
	} else {
		tmp[0] = coords[0];
		tmp[2] = coords[2];
		in_out->base.direction = GOD_ANCHOR_DIR_RIGHT;
	}
	if (coords[1] > coords[3]) {
		tmp[1] = coords[3];
		tmp[3] = coords[1];
	} else {
		tmp[1] = coords[1];
		tmp[3] = coords[3];
		in_out->base.direction |= GOD_ANCHOR_DIR_DOWN;
	}

	switch (in_out->mode) {
	case GNM_SO_ANCHOR_TWO_CELLS:
		in_out->cell_bound.start.col = calc_obj_place (pane, tmp[0], TRUE,
			in_out->offset + 0);
		in_out->cell_bound.start.row = calc_obj_place (pane, tmp[1], FALSE,
			in_out->offset + 1);
		in_out->cell_bound.end.col = calc_obj_place (pane, tmp[2], TRUE,
			in_out->offset + 2);
		in_out->cell_bound.end.row = calc_obj_place (pane, tmp[3], FALSE,
			in_out->offset + 3);
		break;
	case GNM_SO_ANCHOR_ONE_CELL:
		in_out->cell_bound.start.col = calc_obj_place (pane, tmp[0], TRUE,
			in_out->offset + 0);
		in_out->cell_bound.start.row = calc_obj_place (pane, tmp[1], FALSE,
			in_out->offset + 1);
		in_out->cell_bound.end = in_out->cell_bound.start;
		in_out->offset[2] = (tmp[2] - tmp[0]) / colrow_compute_pixel_scale (sheet, TRUE);
		in_out->offset[3] = (tmp[3] - tmp[1]) / colrow_compute_pixel_scale (sheet, FALSE);
		break;
	case GNM_SO_ANCHOR_ABSOLUTE: {
		double h, v;
		range_init (&in_out->cell_bound, 0, 0, 0, 0);
		h = colrow_compute_pixel_scale (sheet, TRUE);
		v = colrow_compute_pixel_scale (sheet, FALSE);
		in_out->offset[0] = tmp[0] / h;
		in_out->offset[1] = tmp[1] / v;
		in_out->offset[2] = (tmp[2] - tmp[0]) / h;
		in_out->offset[3] = (tmp[3] - tmp[1]) / v;
		break;
	}
	}
}

static double
cell_offset_calc_pixel (Sheet const *sheet, int i, gboolean is_col,
			double offset)
{
	ColRowInfo const *cri = sheet_colrow_get_info (sheet, i, is_col);
	return offset * cri->size_pixels;
}

void
scg_object_anchor_to_coords (SheetControlGUI const *scg,
			     SheetObjectAnchor const *anchor, double *coords)
{
	Sheet *sheet = scg_sheet (scg);
	GODrawingAnchorDir direction;
	gint64 pixels[4];
	GnmRange const *r;

	g_return_if_fail (GNM_IS_SCG (scg));
	g_return_if_fail (anchor != NULL);
	g_return_if_fail (coords != NULL);

	r = &anchor->cell_bound;
	if (anchor->mode != GNM_SO_ANCHOR_ABSOLUTE) {
		pixels[0] = scg_colrow_distance_get (scg, TRUE, 0,  r->start.col);
		pixels[1] = scg_colrow_distance_get (scg, FALSE, 0, r->start.row);
		if (anchor->mode == GNM_SO_ANCHOR_TWO_CELLS) {
			pixels[2] = pixels[0] + scg_colrow_distance_get (scg, TRUE,
				r->start.col, r->end.col);
			pixels[3] = pixels[1] + scg_colrow_distance_get (scg, FALSE,
				r->start.row, r->end.row);
			/* add .5 to offsets so that the rounding is optimal */
			pixels[0] += cell_offset_calc_pixel (sheet, r->start.col,
				TRUE, anchor->offset[0]) + .5;
			pixels[1] += cell_offset_calc_pixel (sheet, r->start.row,
				FALSE, anchor->offset[1]) + .5;
			pixels[2] += cell_offset_calc_pixel (sheet, r->end.col,
				TRUE, anchor->offset[2]) + .5;
			pixels[3] += cell_offset_calc_pixel (sheet, r->end.row,
				FALSE, anchor->offset[3]) + .5;
		} else {
			/* add .5 to offsets so that the rounding is optimal */
			pixels[0] += cell_offset_calc_pixel (sheet, r->start.col,
				TRUE, anchor->offset[0]) + .5;
			pixels[1] += cell_offset_calc_pixel (sheet, r->start.row,
				FALSE, anchor->offset[1]) + .5;
			pixels[2] = pixels[0] + go_fake_floor (anchor->offset[2] * colrow_compute_pixel_scale (sheet, TRUE) + .5);
			pixels[3] = pixels[1] + go_fake_floor (anchor->offset[3] * colrow_compute_pixel_scale (sheet, TRUE) + .5);
		}
	} else {
		double h, v;
		h = colrow_compute_pixel_scale (sheet, TRUE);
		v = colrow_compute_pixel_scale (sheet, FALSE);
		pixels[0] = go_fake_floor (anchor->offset[0] * h);
		pixels[1] = go_fake_floor (anchor->offset[1] * v);
		pixels[2] = go_fake_floor ((anchor->offset[0] + anchor->offset[2]) * h);
		pixels[3] = go_fake_floor ((anchor->offset[1] + anchor->offset[3]) * v);
	}

	direction = anchor->base.direction;
	if (direction == GOD_ANCHOR_DIR_UNKNOWN)
		direction = GOD_ANCHOR_DIR_DOWN_RIGHT;

	coords[0] = pixels[direction & GOD_ANCHOR_DIR_H_MASK  ? 0 : 2];
	coords[1] = pixels[direction & GOD_ANCHOR_DIR_V_MASK  ? 1 : 3];
	coords[2] = pixels[direction & GOD_ANCHOR_DIR_H_MASK  ? 2 : 0];
	coords[3] = pixels[direction & GOD_ANCHOR_DIR_V_MASK  ? 3 : 1];
}

/***************************************************************************/

static gboolean
scg_comment_display_filter_cb (PangoAttribute *attribute, gboolean *state)
{
	if (attribute->klass->type == PANGO_ATTR_FOREGROUND &&
	    attribute->start_index != attribute->end_index)
		*state = TRUE;
	return FALSE;
}

/**
 * scg_comment_display:
 * @scg: The SheetControl
 * @cc: A cell comment
 *
 */
void
scg_comment_display (SheetControlGUI *scg, GnmComment *cc,
		     int x, int y)
{
	g_return_if_fail (GNM_IS_SCG (scg));

	scg_comment_timer_clear (scg);

	/* If someone clicked and dragged the comment marker this may be NULL */
	if (scg->comment.selected == NULL)
		return;

	if (cc == NULL)
		cc = scg->comment.selected;
	else if (scg->comment.selected != cc)
		scg_comment_unselect (scg, scg->comment.selected);

	g_return_if_fail (GNM_IS_CELL_COMMENT (cc));

	if (scg->comment.item == NULL) {
		GtkWidget *label, *box;
		char *comment_text;
		PangoAttrList *comment_markup;
		char const *comment_author;

		g_object_get (G_OBJECT (cc),
			      "text", &comment_text,
			      "markup", &comment_markup,
			      NULL);
		comment_author = cell_comment_author_get (cc);

		box = gtk_box_new (GTK_ORIENTATION_VERTICAL, FALSE);

		if (comment_author != NULL) {
			char *text;
			PangoAttrList *attrs;
			PangoAttribute *attr;

			/* xgettext: this is a by-line for cell comments */
			text = g_strdup_printf (_("By %s:"), comment_author);
			label = gtk_label_new (text);
			g_free (text);

			attrs = pango_attr_list_new ();
			attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
			attr->start_index = 0;
			attr->end_index = G_MAXINT;
			pango_attr_list_insert (attrs, attr);
			gtk_label_set_attributes (GTK_LABEL (label), attrs);
			pango_attr_list_unref (attrs);

			gtk_widget_set_halign (label, GTK_ALIGN_START);
			gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 0);
			gtk_box_set_spacing (GTK_BOX (box), 10);
		}

		label = gtk_label_new (comment_text);
		if (comment_markup) {
			gboolean font_colour_set = FALSE;
			pango_attr_list_filter
				(comment_markup,
				 (PangoAttrFilterFunc) scg_comment_display_filter_cb,
				 &font_colour_set);
			if (font_colour_set) {
				/* Imported comments may have a font colour set. */
				/* If that is the case, we set a background colour. */
				guint length = strlen (comment_text);
				PangoAttribute *attr = pango_attr_foreground_new (0,0,0);
				attr->start_index = 0;
				attr->end_index = length;
				pango_attr_list_insert_before (comment_markup, attr);
				attr = pango_attr_background_new (255*255, 255*255, 224*255 );
				attr->start_index = 0;
				attr->end_index = length;
				pango_attr_list_insert_before (comment_markup, attr);
			}
			gtk_label_set_attributes (GTK_LABEL (label), comment_markup);
		}
		g_free (comment_text);
		gtk_widget_set_halign (label, GTK_ALIGN_START);
		gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);

		gnm_convert_to_tooltip (GTK_WIDGET (scg->grid), box);

		scg->comment.item = gtk_widget_get_toplevel (box);
		gtk_window_move (GTK_WINDOW (scg->comment.item),
				 x + 10, y + 10);

		gtk_widget_show_all (scg->comment.item);
	}
}

static gint
cb_cell_comment_timer (SheetControlGUI *scg)
{
	g_return_val_if_fail (GNM_IS_SCG (scg), FALSE);
	g_return_val_if_fail (scg->comment.timer != 0, FALSE);

	scg->comment.timer = 0;
	scg_comment_display (scg, scg->comment.selected,
			     scg->comment.x, scg->comment.y);
	return FALSE;
}

/**
 * scg_comment_select:
 * @scg: The SheetControl
 * @cc: A cell comment
 *
 * Prepare @cc for display.
 */
void
scg_comment_select (SheetControlGUI *scg, GnmComment *cc, int x, int y)
{
	g_return_if_fail (GNM_IS_SCG (scg));

	if (scg->comment.selected != NULL)
		scg_comment_unselect (scg, scg->comment.selected);

	g_return_if_fail (scg->comment.timer == 0);

	scg->comment.selected = cc;
	scg->comment.timer = g_timeout_add (1000,
		(GSourceFunc)cb_cell_comment_timer, scg);
	scg->comment.x = x;
	scg->comment.y = y;
}

/**
 * scg_comment_unselect:
 * @scg: The SheetControl
 * @cc: A cell comment
 *
 * If @cc is the current cell comment being edited/displayed shutdown the
 * display mechanism.
 */
void
scg_comment_unselect (SheetControlGUI *scg, GnmComment *cc)
{
	g_return_if_fail (GNM_IS_SCG (scg));

	if (cc == scg->comment.selected) {
		scg->comment.selected = NULL;
		scg_comment_timer_clear (scg);

		if (scg->comment.item != NULL) {
			gtk_widget_destroy (scg->comment.item);
			scg->comment.item = NULL;
		}
	}
}

/************************************************************************/
/* Col/Row size support routines.  */

gint64
scg_colrow_distance_get (SheetControlGUI const *scg, gboolean is_cols,
			 int from, int to)
{
	Sheet *sheet = scg_sheet (scg);
	return sheet_colrow_get_distance_pixels (sheet, is_cols, from, to);
}

/*************************************************************************/

static void
scg_cursor_bound (SheetControl *sc, GnmRange const *r)
{
	SheetControlGUI *scg = (SheetControlGUI *) sc;
	SCG_FOREACH_PANE (scg, pane, gnm_pane_cursor_bound_set (pane, r););
}

static void
scg_recompute_visible_region (SheetControl *sc, gboolean full_recompute)
{
	SheetControlGUI *scg = (SheetControlGUI *) sc;

	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_compute_visible_region (pane, full_recompute););
}

void
scg_edit_start (SheetControlGUI *scg)
{
	g_return_if_fail (GNM_IS_SCG (scg));

	SCG_FOREACH_PANE (scg, pane, gnm_pane_edit_start (pane););
}

void
scg_edit_stop (SheetControlGUI *scg)
{
	g_return_if_fail (GNM_IS_SCG (scg));

	scg_rangesel_stop (scg, FALSE);
	SCG_FOREACH_PANE (scg, pane, gnm_pane_edit_stop (pane););
}

/**
 * scg_rangesel_changed:
 * @scg:   The scg
 *
 * Notify expr_entry that the expression range has changed.
 **/
static void
scg_rangesel_changed (SheetControlGUI *scg,
		      int base_col, int base_row,
		      int move_col, int move_row)
{
	GnmExprEntry *expr_entry;
	gboolean ic_changed;
	GnmRange *r, last_r;
	Sheet *sheet;

	g_return_if_fail (GNM_IS_SCG (scg));

	scg->rangesel.base_corner.col = base_col;
	scg->rangesel.base_corner.row = base_row;
	scg->rangesel.move_corner.col = move_col;
	scg->rangesel.move_corner.row = move_row;

	r = &scg->rangesel.displayed;
	if (base_col < move_col) {
		r->start.col =  base_col;
		r->end.col =  move_col;
	} else {
		r->end.col =  base_col;
		r->start.col =  move_col;
	}
	if (base_row < move_row) {
		r->start.row =  base_row;
		r->end.row =  move_row;
	} else {
		r->end.row =  base_row;
		r->start.row =  move_row;
	}

	sheet = scg_sheet (scg);
	expr_entry = wbcg_get_entry_logical (scg->wbcg);

	gnm_expr_entry_freeze (expr_entry);
	/* The order here is tricky.
	 * 1) Assign the range to the expr entry.
	 */
	ic_changed = gnm_expr_entry_load_from_range (
		expr_entry, sheet, r);

	/* 2) if the expr entry changed the region get the new region */
	if (ic_changed)
		gnm_expr_entry_get_rangesel (expr_entry, r, NULL);

	/* 3) now double check that all merged regions are fully contained */
	last_r = *r;
	gnm_sheet_merge_find_bounding_box (sheet, r);
	if (!range_equal (&last_r, r))
		gnm_expr_entry_load_from_range (expr_entry, sheet, r);

	gnm_expr_entry_thaw (expr_entry);

	SCG_FOREACH_PANE (scg, pane, gnm_pane_rangesel_bound_set (pane, r););
}

void
scg_rangesel_start (SheetControlGUI *scg,
		    int base_col, int base_row,
		    int move_col, int move_row)
{
	GnmRange r;

	g_return_if_fail (GNM_IS_SCG (scg));

	if (scg->rangesel.active)
		return;

	if (scg->wbcg->rangesel != NULL)
		g_warning ("misconfiged rangesel");

	scg->wbcg->rangesel = scg;
	scg->rangesel.active = TRUE;

	gnm_expr_entry_find_range (wbcg_get_entry_logical (scg->wbcg));

	range_init (&r, base_col, base_row, move_col, move_row);
	SCG_FOREACH_PANE (scg, pane, gnm_pane_rangesel_start (pane, &r););
	scg_rangesel_changed (scg, base_col, base_row, move_col, move_row);
}

void
scg_rangesel_stop (SheetControlGUI *scg, gboolean clear_string)
{
	g_return_if_fail (GNM_IS_SCG (scg));

	if (!scg->rangesel.active)
		return;
	if (scg->wbcg->rangesel != scg)
		g_warning ("misconfiged rangesel");

	scg->wbcg->rangesel = NULL;
	scg->rangesel.active = FALSE;
	SCG_FOREACH_PANE (scg, pane, gnm_pane_rangesel_stop (pane););

	gnm_expr_entry_rangesel_stop (wbcg_get_entry_logical (scg->wbcg),
		clear_string);
}

/**
 * scg_set_display_cursor:
 * @scg:
 *
 * Set the displayed cursor type.
 */
void
scg_set_display_cursor (SheetControlGUI *scg)
{
	GdkCursorType cursor = GDK_CURSOR_IS_PIXMAP;

	g_return_if_fail (GNM_IS_SCG (scg));

	if (scg->wbcg->new_object != NULL)
		cursor = GDK_CROSSHAIR;

	SCG_FOREACH_PANE (scg, pane, {
		GtkWidget *w = GTK_WIDGET (pane);
		if (gtk_widget_get_window (w)) {
			if (cursor == GDK_CURSOR_IS_PIXMAP)
				gnm_widget_set_cursor (w, pane->mouse_cursor);
			else
				gnm_widget_set_cursor_type (w, cursor);
		}
	});
}

void
scg_rangesel_extend_to (SheetControlGUI *scg, int col, int row)
{
	int base_col, base_row;

	if (col < 0) {
		base_col = 0;
		col = gnm_sheet_get_last_col (scg_sheet (scg));
	} else
		base_col = scg->rangesel.base_corner.col;
	if (row < 0) {
		base_row = 0;
		row = gnm_sheet_get_last_row (scg_sheet (scg));
	} else
		base_row = scg->rangesel.base_corner.row;

	if (scg->rangesel.active)
		scg_rangesel_changed (scg, base_col, base_row, col, row);
	else
		scg_rangesel_start (scg, base_col, base_row, col, row);
}

void
scg_rangesel_bound (SheetControlGUI *scg,
		    int base_col, int base_row,
		    int move_col, int move_row)
{
	if (scg->rangesel.active)
		scg_rangesel_changed (scg, base_col, base_row, move_col, move_row);
	else
		scg_rangesel_start (scg, base_col, base_row, move_col, move_row);
}

void
scg_rangesel_move (SheetControlGUI *scg, int n, gboolean jump_to_bound,
		   gboolean horiz)
{
	SheetView *sv = scg_view (scg);
	GnmCellPos tmp;

	if (!scg->rangesel.active) {
		tmp.col = sv->edit_pos_real.col;
		tmp.row = sv->edit_pos_real.row;
	} else
		tmp = scg->rangesel.base_corner;

	if (horiz)
		tmp.col = sheet_find_boundary_horizontal (
			sv_sheet (sv), tmp.col, tmp.row, tmp.row, n, jump_to_bound);
	else
		tmp.row = sheet_find_boundary_vertical (
			sv_sheet (sv), tmp.col, tmp.row, tmp.col, n, jump_to_bound);

	if (scg->rangesel.active)
		scg_rangesel_changed (scg, tmp.col, tmp.row, tmp.col, tmp.row);
	else
		scg_rangesel_start   (scg, tmp.col, tmp.row, tmp.col, tmp.row);
	scg_make_cell_visible (scg, tmp.col, tmp.row, FALSE, FALSE);
	gnm_expr_entry_signal_update (
		wbcg_get_entry_logical (scg->wbcg), FALSE);
}

void
scg_rangesel_extend (SheetControlGUI *scg, int n,
		     gboolean jump_to_bound, gboolean horiz)
{
	Sheet *sheet = scg_sheet (scg);

	if (scg->rangesel.active) {
		GnmCellPos tmp = scg->rangesel.move_corner;

		if (horiz)
			tmp.col = sheet_find_boundary_horizontal (sheet,
				tmp.col, tmp.row, scg->rangesel.base_corner.row,
				n, jump_to_bound);
		else
			tmp.row = sheet_find_boundary_vertical (sheet,
				tmp.col, tmp.row, scg->rangesel.base_corner.col,
				n, jump_to_bound);

		scg_rangesel_changed (scg,
			scg->rangesel.base_corner.col,
			scg->rangesel.base_corner.row, tmp.col, tmp.row);

		scg_make_cell_visible (scg,
			scg->rangesel.move_corner.col,
			scg->rangesel.move_corner.row, FALSE, TRUE);
		gnm_expr_entry_signal_update (
			wbcg_get_entry_logical (scg->wbcg), FALSE);
	} else
		scg_rangesel_move (scg, n, jump_to_bound, horiz);
}

/**
 * scg_cursor_move:
 * @scg: The scg
 * @dir: Number of units to move the cursor
 * @jump_to_bound: skip from the start to the end of ranges
 *                 of filled or unfilled cells.
 * @horiz: is the movement horizontal or vertical
 *
 * Moves the cursor count rows
 */
void
scg_cursor_move (SheetControlGUI *scg, int n,
		 gboolean jump_to_bound, gboolean horiz)
{
	SheetView *sv = scg_view (scg);
	GnmCellPos tmp = sv->edit_pos_real;
	int step = (n>0) ? 1 : -1;

	if (!wbcg_edit_finish (scg->wbcg, WBC_EDIT_ACCEPT, NULL))
		return;

	if (horiz)
		tmp.col = sheet_find_boundary_horizontal (sv->sheet,
			tmp.col + n - step, tmp.row, tmp.row,
			step, jump_to_bound);
	else
		tmp.row = sheet_find_boundary_vertical
			(sv->sheet,
			 tmp.col, tmp.row + n - step,
			 tmp.col,
			 step, jump_to_bound);

	sv_selection_reset (sv);
	gnm_sheet_view_cursor_set (sv, &tmp,
		       tmp.col, tmp.row, tmp.col, tmp.row, NULL);
	gnm_sheet_view_make_cell_visible (sv, tmp.col, tmp.row, FALSE);
	sv_selection_add_pos (sv, tmp.col, tmp.row, GNM_SELECTION_MODE_ADD);
}

/**
 * scg_cursor_extend:
 * @scg: The scg
 * @n: Units to extend the selection
 * @jump_to_bound: Move to transitions between cells and blanks,
 *                       or move in single steps.
 * @horiz: extend vertically or horizontally.
 */
void
scg_cursor_extend (SheetControlGUI *scg, int n,
		   gboolean jump_to_bound, gboolean horiz)
{
	SheetView *sv = scg_view (scg);
	GnmCellPos move = sv->cursor.move_corner;
	GnmCellPos visible = scg->pane[0]->first;

	if (!wbcg_edit_finish (scg->wbcg, WBC_EDIT_ACCEPT, NULL))
		return;

	if (horiz)
		visible.col = move.col = sheet_find_boundary_horizontal (sv->sheet,
			move.col, move.row, sv->cursor.base_corner.row,
			n, jump_to_bound);
	else
		visible.row = move.row = sheet_find_boundary_vertical (sv->sheet,
			move.col, move.row, sv->cursor.base_corner.col,
			n, jump_to_bound);

	sv_selection_extend_to (sv, move.col, move.row);
	gnm_sheet_view_make_cell_visible (sv, visible.col, visible.row, FALSE);
}

void
scg_take_focus (SheetControlGUI *scg)
{
	g_return_if_fail (GNM_IS_SCG (scg));

	/* FIXME: Slightly hackish. */
	if (wbcg_toplevel (scg->wbcg))
		gtk_window_set_focus (wbcg_toplevel (scg->wbcg),
		                      (scg_sheet (scg)->sheet_type == GNM_SHEET_OBJECT)?
				      GTK_WIDGET (scg->vs): GTK_WIDGET (scg_pane (scg, 0)));
}

/*********************************************************************************/
void
scg_size_guide_start (SheetControlGUI *scg,
		      gboolean vert, int colrow, gboolean is_colrow_resize)
{
	g_return_if_fail (GNM_IS_SCG (scg));
	SCG_FOREACH_PANE (scg, pane,
			  gnm_pane_size_guide_start (pane, vert, colrow, is_colrow_resize););
}
void
scg_size_guide_motion (SheetControlGUI *scg, gboolean vert, gint64 guide_pos)
{
	g_return_if_fail (GNM_IS_SCG (scg));
	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_size_guide_motion (pane, vert, guide_pos););
}
void
scg_size_guide_stop (SheetControlGUI *scg)
{
	g_return_if_fail (GNM_IS_SCG (scg));
	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_size_guide_stop (pane););
}
/*********************************************************************************/

void
scg_special_cursor_start (SheetControlGUI *scg, int style, int button)
{
	g_return_if_fail (GNM_IS_SCG (scg));

	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_special_cursor_start (pane, style, button););
}

void
scg_special_cursor_stop (SheetControlGUI *scg)
{
	g_return_if_fail (GNM_IS_SCG (scg));

	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_special_cursor_stop (pane););
}

gboolean
scg_special_cursor_bound_set (SheetControlGUI *scg, GnmRange const *r)
{
	gboolean changed = FALSE;

	g_return_val_if_fail (GNM_IS_SCG (scg), FALSE);

	SCG_FOREACH_PANE (scg, pane,
		changed |= gnm_pane_special_cursor_bound_set (pane, r););
	return changed;
}

static void
scg_object_create_view	(SheetControl *sc, SheetObject *so)
{
	SheetControlGUI *scg = GNM_SCG (sc);
	if (scg->active_panes)
		SCG_FOREACH_PANE (scg, pane,
			sheet_object_new_view (so, (SheetObjectViewContainer *)pane););
	else
		sheet_object_new_view (so, (SheetObjectViewContainer *)scg->vs);
}

static void
scg_scale_changed (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	Sheet *sheet = scg_sheet (scg);
	double z;
	GSList *ptr;

	g_return_if_fail (GNM_IS_SCG (scg));

	z = sheet->last_zoom_factor_used;

	SCG_FOREACH_PANE (scg, pane, {
		if (pane->col.canvas != NULL)
			goc_canvas_set_pixels_per_unit (pane->col.canvas, z);
		if (pane->row.canvas != NULL)
			goc_canvas_set_pixels_per_unit (pane->row.canvas, z);
		goc_canvas_set_pixels_per_unit (GOC_CANVAS (pane), z);
	});

	scg_resize (scg, TRUE);
	set_resize_pane_pos (scg, scg->vpane);
	set_resize_pane_pos (scg, scg->hpane);
	/* now, update sheet objects positions and sizes */
	for (ptr = sheet->sheet_objects; ptr; ptr = ptr->next)
		sheet_object_update_bounds (GNM_SO (ptr->data), NULL);
}

static gboolean
cb_cell_im_timer (SheetControlGUI *scg)
{
	g_return_val_if_fail (GNM_IS_SCG (scg), FALSE);
	g_return_val_if_fail (scg->im.timer != 0, FALSE);

	scg->im.timer = 0;
	scg_im_destroy (scg);
	return FALSE;
}

static GnmPane *
scg_find_pane (SheetControlGUI *scg, GnmCellPos *pos)
{
	int i;

	for (i = 0; i < scg->active_panes; i++) {
		GnmPane *pane = scg->pane[i];

		if (pane &&
		    pane->first.col <= pos->col &&
		    pane->first.row <= pos->row &&
		    pane->last_visible.col >= pos->col &&
		    pane->last_visible.row >= pos->row)
			return pane;
	}
	return NULL;
}

static void
scg_show_im_tooltip (SheetControl *sc, GnmInputMsg *im, GnmCellPos *pos)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	GnmPane *pane;

	g_return_if_fail (GNM_IS_SCG (scg));

	scg_im_destroy (scg);

	pane = scg_find_pane (scg, pos);

	if (im && pane) {
		GtkWidget *label, *box;
		char const *text, *title;
		int len_text, len_title;
		int x, y, x_origin, y_origin;
		GtkAllocation allocation;
		Sheet *sheet = scg_sheet (scg);
		gboolean rtl = sheet->text_is_rtl;

		text = gnm_input_msg_get_msg   (im);
		title = gnm_input_msg_get_title (im);
		len_text = (text == NULL) ? 0 : strlen (text);
		len_title = (title == NULL) ? 0 : strlen (title);

		if ((len_text == 0) && (len_title == 0))
			return;

		box = gtk_box_new (GTK_ORIENTATION_VERTICAL, FALSE);

		if (len_title > 0) {
			PangoAttrList *attrs;
			PangoAttribute *attr;

			label = gtk_label_new (title);

			attrs = pango_attr_list_new ();
			attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
			attr->start_index = 0;
			attr->end_index = G_MAXINT;
			pango_attr_list_insert (attrs, attr);
			gtk_label_set_attributes (GTK_LABEL (label), attrs);
			pango_attr_list_unref (attrs);

			gtk_widget_set_halign (label, GTK_ALIGN_START);
			gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 0);
		}
		if (len_text > 0) {
			label = gtk_label_new (text);

			gtk_widget_set_halign (label, GTK_ALIGN_START);
			gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 0);
			if (len_title > 0)
				gtk_box_set_spacing (GTK_BOX (box), 10);
		}
		gnm_convert_to_tooltip (GTK_WIDGET (scg->grid), box);
		scg->im.item = gtk_widget_get_toplevel (box);

		x = sheet_col_get_distance_pixels
			(sheet, pane->first.col, pos->col + (rtl ? 1 : 0));

		y = sheet_row_get_distance_pixels
			(sheet, pane->first.row, pos->row + 1);

		gtk_widget_get_allocation (GTK_WIDGET (pane), &allocation);
		if (rtl)
			x = allocation.width - x;
		x += allocation.x;
		y += allocation.y;

		gdk_window_get_position
			(gtk_widget_get_parent_window (GTK_WIDGET (pane)),
			 &x_origin, &y_origin);
		x += x_origin;
		y += y_origin;

		gtk_window_move (GTK_WINDOW (scg->im.item), x + 10, y + 10);
		gtk_widget_show_all (scg->im.item);
		scg->im.timer = g_timeout_add (1500, (GSourceFunc)cb_cell_im_timer, scg);
	}
}

static void
scg_freeze_object_view (SheetControl *sc, gboolean freeze)
{
	SCG_FOREACH_PANE
		(GNM_SCG(sc), pane,
		 goc_group_freeze (pane->object_views, freeze);
		 goc_group_freeze (pane->grid_items, freeze););
}

static void
scg_class_init (GObjectClass *object_class)
{
	SheetControlClass *sc_class = SHEET_CONTROL_CLASS (object_class);

	g_return_if_fail (sc_class != NULL);

	scg_parent_class = g_type_class_peek_parent (object_class);

	object_class->finalize = scg_finalize;

	sc_class->resize                   = scg_resize_virt;
	sc_class->redraw_all               = scg_redraw_all;
	sc_class->redraw_range		   = scg_redraw_range;
	sc_class->redraw_headers           = scg_redraw_headers;
	sc_class->ant                      = scg_ant;
	sc_class->unant                    = scg_unant;
	sc_class->scrollbar_config         = scg_scrollbar_config;
	sc_class->mode_edit                = scg_mode_edit_virt;
	sc_class->set_top_left		   = scg_set_top_left;
	sc_class->recompute_visible_region = scg_recompute_visible_region;
	sc_class->make_cell_visible        = scg_make_cell_visible_virt;
	sc_class->cursor_bound             = scg_cursor_bound;
	sc_class->set_panes		   = scg_set_panes;
	sc_class->object_create_view	   = scg_object_create_view;
	sc_class->scale_changed		   = scg_scale_changed;
	sc_class->show_im_tooltip	   = scg_show_im_tooltip;
	sc_class->freeze_object_view	   = scg_freeze_object_view;
}

GSF_CLASS (SheetControlGUI, sheet_control_gui,
	   scg_class_init, scg_init, GNM_SHEET_CONTROL_TYPE)

static gint
cb_scg_queued_movement (SheetControlGUI *scg)
{
	Sheet const *sheet = scg_sheet (scg);
	scg->delayedMovement.timer = 0;
	(*scg->delayedMovement.handler) (scg,
		scg->delayedMovement.n, FALSE,
		scg->delayedMovement.horiz);
	if (wbcg_is_editing (scg->wbcg))
		sheet_update_only_grid (sheet);
	else
		sheet_update (sheet);
	return FALSE;
}

/**
 * scg_queue_movement:
 * @scg:
 * @handler: (scope async): The movement handler
 * @n:		how far
 * @jump:	TRUE jump to bound
 * @horiz:	TRUE move by cols
 *
 * Do motion compression when possible to avoid redrawing an area that will
 * disappear when we scroll again.
 **/
void
scg_queue_movement (SheetControlGUI	*scg,
		    SCGUIMoveFunc	 handler,
		    int n, gboolean jump, gboolean horiz)
{
	g_return_if_fail (GNM_IS_SCG (scg));

	/* do we need to flush a pending movement */
	if (scg->delayedMovement.timer != 0) {
		if (jump ||
		    /* do not skip more than 3 requests at a time */
		    scg->delayedMovement.counter > 3 ||
		    scg->delayedMovement.handler != handler ||
		    scg->delayedMovement.horiz != horiz) {
			g_source_remove (scg->delayedMovement.timer);
			(*scg->delayedMovement.handler) (scg,
				scg->delayedMovement.n, FALSE,
				scg->delayedMovement.horiz);
			scg->delayedMovement.handler = NULL;
			scg->delayedMovement.timer = 0;
		} else {
			scg->delayedMovement.counter++;
			scg->delayedMovement.n += n;
			return;
		}
	}

	/* jumps are always immediate */
	if (jump) {
		Sheet const *sheet = scg_sheet (scg);
		(*handler) (scg, n, TRUE, horiz);
		if (wbcg_is_editing (scg->wbcg))
			sheet_update_only_grid (sheet);
		else
			sheet_update (sheet);
		return;
	}

	scg->delayedMovement.counter = 1;
	scg->delayedMovement.handler = handler;
	scg->delayedMovement.horiz   = horiz;
	scg->delayedMovement.n	     = n;
	scg->delayedMovement.timer   = g_timeout_add (10,
		(GSourceFunc)cb_scg_queued_movement, scg);
}

static void
scg_image_create (SheetControlGUI *scg, SheetObjectAnchor *anchor,
		  guint8 const *data, unsigned len)
{
	SheetObjectImage *soi;
	SheetObject *so;
	double w, h;

	/* ensure that we are not editing anything else */
	scg_mode_edit (scg);

	soi = g_object_new (GNM_SO_IMAGE_TYPE, NULL);
	sheet_object_image_set_image (soi, "", data, len);

	so = GNM_SO (soi);
	sheet_object_set_anchor (so, anchor);
	sheet_object_set_sheet (so, scg_sheet (scg));
	scg_object_select (scg, so);
	sheet_object_default_size (so, &w, &h);
	scg_objects_drag (scg, NULL, NULL, &w, &h, 7, FALSE, FALSE, FALSE);
	scg_objects_drag_commit	(scg, 7, TRUE, NULL, NULL, NULL);
}

void
scg_paste_image (SheetControlGUI *scg, GnmRange *where,
		 guint8 const *data, unsigned len)
{
	SheetObjectAnchor anchor;

	sheet_object_anchor_init (&anchor, where, NULL,
		GOD_ANCHOR_DIR_DOWN_RIGHT, GNM_SO_ANCHOR_TWO_CELLS);
	scg_image_create (scg, &anchor, data, len);
}

static void
scg_drag_receive_img_data (SheetControlGUI *scg, double x, double y,
			   guint8 const *data, unsigned len)
{
	double coords[4];
	SheetObjectAnchor anchor;

	sheet_object_anchor_init (&anchor, NULL, NULL,
		GOD_ANCHOR_DIR_DOWN_RIGHT, GNM_SO_ANCHOR_TWO_CELLS);
	coords[0] = coords[2] = x;
	coords[1] = coords[3] = y;
	scg_object_coords_to_anchor (scg, coords, &anchor);
	scg_image_create (scg, &anchor, data, len);
}

static void
scg_drag_receive_img_uri (SheetControlGUI *scg, double x, double y, const gchar *uri)
{
	GError *err = NULL;
	GsfInput *input = go_file_open (uri, &err);
	GOIOContext *ioc = go_io_context_new (GO_CMD_CONTEXT (scg->wbcg));

	if (input != NULL) {
		unsigned len = gsf_input_size (input);
		guint8 const *data = gsf_input_read (input, len, NULL);

		scg_drag_receive_img_data (scg, x, y, data, len);
		g_object_unref (input);
	} else
		go_cmd_context_error (GO_CMD_CONTEXT (ioc), err);

	if (go_io_error_occurred (ioc) ||
	    go_io_warning_occurred (ioc)) {
		go_io_error_display (ioc);
		go_io_error_clear (ioc);
	}
	g_object_unref (ioc);
}

static void
scg_drag_receive_spreadsheet (SheetControlGUI *scg, const gchar *uri)
{
	GError *err = NULL;
	GsfInput *input = go_file_open (uri, &err);
	GOIOContext *ioc = go_io_context_new (GO_CMD_CONTEXT (scg->wbcg));

	if (input != NULL) {
		WorkbookView *wbv;

		wbv = workbook_view_new_from_input (input, uri, NULL, ioc, NULL);
		if (wbv != NULL)
			gui_wb_view_show (scg->wbcg,
					  wbv);

	} else
		go_cmd_context_error (GO_CMD_CONTEXT (ioc), err);

	if (go_io_error_occurred (ioc) ||
	    go_io_warning_occurred (ioc)) {
		go_io_error_display (ioc);
		go_io_error_clear (ioc);
	}
	g_object_unref (ioc);
}

static void
scg_paste_cellregion (SheetControlGUI *scg, double x, double y,
		      GnmCellRegion *content)
{
	WorkbookControl	*wbc  = scg_wbc (scg);
	Sheet *sheet = scg_sheet (scg) ;
	GnmPasteTarget pt;
	SheetObjectAnchor anchor;
	double coords[4];

	sheet_object_anchor_init (&anchor, NULL, NULL,
		GOD_ANCHOR_DIR_DOWN_RIGHT, GNM_SO_ANCHOR_TWO_CELLS);
	coords[0] = coords[2] = x;
	coords[1] = coords[3] = y;
	scg_object_coords_to_anchor (scg, coords, &anchor);
	paste_target_init (&pt, sheet, &anchor.cell_bound, PASTE_ALL_SHEET);
	if (content && ((content->cols > 0 && content->rows > 0) ||
			content->objects != NULL))
		cmd_paste_copy (wbc, &pt, content);
}

static void
scg_drag_receive_cellregion (SheetControlGUI *scg, double x, double y,
			     const char *data, unsigned len)
{
	GnmCellRegion *content;
	GOIOContext *io_context =
		go_io_context_new (GO_CMD_CONTEXT (scg->wbcg));

	content = gnm_xml_cellregion_read (scg_wbc (scg), io_context,
				       scg_sheet (scg), data, len);
	g_object_unref (io_context);
	if (content != NULL) {
		scg_paste_cellregion (scg, x, y, content);
		cellregion_unref (content);
	}
}

static void
scg_drag_receive_uri_list (SheetControlGUI *scg, double x, double y,
			   const char *data, unsigned len)
{
	char *cdata = g_strndup (data, len);
	GSList *urls = go_file_split_urls (cdata);
	GSList *l;

	g_free (cdata);
	for (l = urls; l; l = l-> next) {
		char const *uri_str = l->data;
		gchar *mime = go_get_mime_type (uri_str);
		/* Note that we have imperfect detection of mime-type with some
		 * platforms, e.g. Win32. In the worst case if
		 * go_get_mime_type() doesn't return "application/x-gnumeric"
		 * (registry corruption?) it will give "text/plain" and a
		 * spreadsheet file is assumed. */
		if (!mime)
			continue;

		if (!strncmp (mime, "image/", 6))
			scg_drag_receive_img_uri (scg, x, y, uri_str);
		else if (!strcmp (mime, "application/x-gnumeric") ||
			 !strcmp (mime, "application/vnd.ms-excel") ||
			 !strcmp (mime, "application/vnd.sun.xml.calc") ||
			 !strcmp (mime, "application/vnd.oasis.opendocument.spreadsheet") ||
			 !strcmp (mime, "application/vnd.lotus-1-2-3") ||
			 !strcmp (mime, "application/x-applix-spreadsheet") ||
			 !strcmp (mime, "application/x-dbase") ||
			 !strcmp (mime, "application/x-oleo") ||
			 !strcmp (mime, "application/x-quattropro") ||
			 !strcmp (mime, "application/x-sc") ||
			 /* !strcmp (mime, "application/xhtml+xml") || */
			 !strcmp (mime, "text/spreadsheet") ||
			 !strcmp (mime, "text/tab-separated-values") ||
			 !strcmp (mime, "text/x-comma-separated-values") ||
			 !strcmp (mime, "text/html") ||
			 !strcmp (mime, "text/plain")) {
			scg_drag_receive_spreadsheet (scg, uri_str);
		} else {
			g_printerr ("Received URI %s with mime type %s.\n", uri_str, mime);
			g_printerr ("I have no idea what to do with that.\n");
		}
		g_free (mime);
	}
	g_slist_free_full (urls, (GDestroyNotify) g_free);
}

static void
scg_drag_receive_same_process (SheetControlGUI *scg, GtkWidget *source_widget,
			       double x, double y)
{
	SheetControlGUI *source_scg = NULL;
	GnmPane *pane;

	g_return_if_fail (source_widget != NULL);
	g_return_if_fail (GNM_IS_PANE (source_widget));

	pane = GNM_PANE (source_widget);
	x *= goc_canvas_get_pixels_per_unit (GOC_CANVAS (pane));
	y *= goc_canvas_get_pixels_per_unit (GOC_CANVAS (pane));
	source_scg = pane->simple.scg;
	if (source_scg == scg) {
		GdkWindow *window;
		GdkModifierType mask;
		gint64 xx = x, yy = y;
		gint64 origin_x = 0, origin_y = 0;
		gboolean make_dup;
		GOUndo *undo = NULL;
		GOUndo *redo = NULL;
		gchar *title = NULL;

		window = gtk_widget_get_parent_window (GTK_WIDGET (pane));
		gdk_window_get_device_position (window,
				gdk_device_manager_get_client_pointer (gdk_display_get_device_manager (gdk_window_get_display (window))),
				NULL, NULL, &mask);

		make_dup = ((mask & GDK_CONTROL_MASK) != 0);

		/* When copying objects, we have to create a copy of current selection.
		 * Since new objects are on top of canvas, we have to move current selection
		 * back to original position, create a copy of selected objects, make them
		 * the current selection, then move these objects to drop location. */

		if (make_dup) {
			xx = origin_x = pane->drag.origin_x;
			yy = origin_y = pane->drag.origin_y;
		}

		gnm_pane_objects_drag (pane, NULL, xx, yy, 8, FALSE,
				       (mask & GDK_SHIFT_MASK) != 0);
		pane->drag.origin_x = pane->drag.last_x;
		pane->drag.origin_y = pane->drag.last_y;

		if (make_dup) {
			GSList *ptr, *objs = go_hash_keys (scg->selected_objects);
			GOUndo *nudge_undo = NULL;
			GOUndo *nudge_redo = NULL;
			double dx, dy;

			for (ptr = objs ; ptr != NULL ; ptr = ptr->next) {
				SheetObject *dup_obj = sheet_object_dup (ptr->data);
				if (dup_obj != NULL) {
					sheet_object_set_sheet (dup_obj, scg_sheet (scg));
					scg_object_select (scg, dup_obj);
					g_object_unref (dup_obj);
					scg_object_unselect (scg, ptr->data);
				}
			}
			g_slist_free (objs);
			scg_objects_drag_commit	(scg, 8, TRUE, &undo, &redo, &title);
			dx = x - origin_x;
			dy = y - origin_y;
			scg_objects_drag (scg, pane, NULL, &dx, &dy, 8, FALSE, FALSE, FALSE);
			scg_objects_drag_commit (scg, 8, FALSE, &nudge_undo, &nudge_redo, NULL);
			undo = go_undo_combine (undo, nudge_undo);
			redo = go_undo_combine (nudge_redo, redo);
		} else
			scg_objects_drag_commit	(scg, 8, FALSE, &undo, &redo, &title);
		cmd_generic (GNM_WBC (scg_wbcg (scg)), title, undo, redo);
		g_free (title);
	} else {
		GnmCellRegion *content;
		GSList *objects;

		g_return_if_fail (GNM_IS_SCG (source_scg));

		objects = go_hash_keys (source_scg->selected_objects);
		content = clipboard_copy_obj (scg_sheet (source_scg),
					      objects);
		if (content != NULL) {
			scg_paste_cellregion (scg, x, y, content);
			cellregion_unref (content);
		}
		g_slist_free (objects);
	}
}

/*  Keep in sync with gtk_selection_data_targets_include_text() */
static gboolean
is_text_target (gchar *target_type)
{
	const gchar *charset;
	gchar       *text_plain_locale;
	gboolean ret;

	g_get_charset (&charset);
	text_plain_locale = g_strdup_printf ("text/plain;charset=%s", charset);
	ret = !strcmp (target_type, "UTF8_STRING") ||
	      !strcmp (target_type, "COMPOUND_TEXT") ||
	      !strcmp (target_type, "TEXT") ||
	      !strcmp (target_type, "STRING") ||
	      !strcmp (target_type, "text/plain;charset=utf-8") ||
	      !strcmp (target_type, text_plain_locale) ||
	      !strcmp (target_type, "text/plain");
	g_free (text_plain_locale);
	return ret;
}

void
scg_drag_data_received (SheetControlGUI *scg, GtkWidget *source_widget,
			double x, double y, GtkSelectionData *selection_data)
{
	gchar *target_type = gdk_atom_name (gtk_selection_data_get_target (selection_data));
	const char *sel_data = (const char *)gtk_selection_data_get_data (selection_data);
	gsize sel_len = gtk_selection_data_get_length (selection_data);

	if (!strcmp (target_type, "text/uri-list")) {
		scg_drag_receive_uri_list (scg, x, y, sel_data, sel_len);

	} else if (!strncmp (target_type, "image/", 6)) {
		scg_drag_receive_img_data (scg, x, y, sel_data, sel_len);
	} else if (!strcmp (target_type, "GNUMERIC_SAME_PROC")) {
		scg_drag_receive_same_process (scg, source_widget, x, y);
	} else if (!strcmp (target_type, "application/x-gnumeric")) {
		scg_drag_receive_cellregion (scg, x, y, sel_data, sel_len);
	} else
		g_warning ("Unknown target type '%s'!", target_type);

	if (gnm_debug_flag ("dnd")) {
		if (!strcmp (target_type, "x-special/gnome-copied-files")) {
			char *cdata = g_strndup (sel_data, sel_len);
			g_print ("data length: %d, data: %s\n",
				 (int)sel_len, cdata);
			g_free (cdata);
		} else if (!strcmp (target_type, "_NETSCAPE_URL")) {
			char *cdata = g_strndup (sel_data, sel_len);
			g_print ("data length: %d, data: %s\n",
				 (int)sel_len, cdata);
			g_free (cdata);
		} else if (is_text_target (target_type)) {
			char *cdata = g_strndup (sel_data, sel_len);
			g_print ("data length: %d, data: %s\n",
				 (int)sel_len, cdata);
			g_free (cdata);
		} else if (!strcmp (target_type, "text/html")) {
			char *cdata = g_strndup (sel_data, sel_len);
			/* For mozilla, need to convert the encoding */
			g_print ("data length: %d, data: %s\n", (int)sel_len, cdata);
			g_free (cdata);
		}
	}

	g_free (target_type);
}

static void
scg_drag_send_image (G_GNUC_UNUSED SheetControlGUI *scg,
		     GtkSelectionData *selection_data,
		     GSList *objects,
		     gchar const *mime_type)
{
	SheetObject *so = NULL;
	GsfOutput *output;
	GsfOutputMemory *omem;
	gsf_off_t osize;
	char *format;
	GSList *ptr;

	for (ptr = objects; ptr != NULL; ptr = ptr->next) {
		if (GNM_IS_SO_IMAGEABLE (GNM_SO (ptr->data))) {
			so = GNM_SO (ptr->data);
			break;
		}
	}
	if (so == NULL) {
		g_warning ("non imageable object requested as image\n");
		return;
	}

	format = go_mime_to_image_format (mime_type);
	if (!format) {
		g_warning ("No image format for %s\n", mime_type);
		g_free (format);
		return;
	}

	output = gsf_output_memory_new ();
	omem = GSF_OUTPUT_MEMORY (output);
	sheet_object_write_image (so, format, -1.0, output, NULL);
	osize = gsf_output_size (output);

	gtk_selection_data_set
		(selection_data,
		 gtk_selection_data_get_target (selection_data),
		 8, gsf_output_memory_get_bytes (omem), osize);
	gsf_output_close (output);
	g_object_unref (output);
	g_free (format);
}

static void
scg_drag_send_graph (G_GNUC_UNUSED SheetControlGUI *scg,
		     GtkSelectionData *selection_data,
		     GSList *objects,
		     gchar const *mime_type)
{
	SheetObject *so = NULL;
	GsfOutput *output;
	GsfOutputMemory *omem;
	gsf_off_t osize;
	GSList *ptr;

	for (ptr = objects; ptr != NULL; ptr = ptr->next)
		if (GNM_IS_SO_EXPORTABLE (GNM_SO (ptr->data))) {
			so = GNM_SO (ptr->data);
			break;
		}

	if (so == NULL) {
		g_warning ("non exportable object requested\n");
		return;
	}

	output = gsf_output_memory_new ();
	omem = GSF_OUTPUT_MEMORY (output);
	sheet_object_write_object (so, mime_type, output, NULL,
				   gnm_conventions_default);
	osize = gsf_output_size (output);

	gtk_selection_data_set
		(selection_data,
		 gtk_selection_data_get_target (selection_data),
		 8, gsf_output_memory_get_bytes (omem), osize);
	gsf_output_close (output);
	g_object_unref (output);
}

static void
scg_drag_send_clipboard_objects (SheetControl *sc,
				 GtkSelectionData *selection_data,
				 GSList *objects)
{
	GnmCellRegion	*content = clipboard_copy_obj (sc_sheet (sc), objects);
	GsfOutputMemory *output;

	if (content == NULL)
		return;

	output  = gnm_cellregion_to_xml (content);
	gtk_selection_data_set
		(selection_data,
		 gtk_selection_data_get_target (selection_data),
		 8,
		 gsf_output_memory_get_bytes (output),
		 gsf_output_size (GSF_OUTPUT (output)));
	g_object_unref (output);
	cellregion_unref (content);
}

static void
scg_drag_send_text (SheetControlGUI *scg, GtkSelectionData *sd)
{
	Sheet *sheet = scg_sheet (scg);
	GnmRange range = sheet_get_extent (sheet, TRUE, TRUE);
	GnmCellRegion *reg = clipboard_copy_range (sheet, &range);
	GString *s = cellregion_to_string (reg, TRUE, sheet_date_conv (sheet));

	cellregion_unref (reg);
	if (!s)
		return;
	gtk_selection_data_set (sd, gtk_selection_data_get_target (sd),
				8, s->str, s->len);
	g_string_free (s, TRUE);
}

void
scg_drag_data_get (SheetControlGUI *scg, GtkSelectionData *selection_data)
{
	GdkAtom target = gtk_selection_data_get_target (selection_data);
	gchar *target_name = gdk_atom_name (target);
	GSList *objects = scg->selected_objects
		? go_hash_keys (scg->selected_objects)
		: NULL;

	if (strcmp (target_name, "GNUMERIC_SAME_PROC") == 0)
		/* Set dummy selection for process internal dnd */
		gtk_selection_data_set (selection_data, target,
					8, (const guint8 *)"", 1);
	else if (strcmp (target_name, "GNUMERIC_SHEET") == 0)
		gtk_selection_data_set (selection_data, target,
					8, (void *)scg, sizeof (scg));
	else if (strcmp (target_name, "application/x-gnumeric") == 0)
		scg_drag_send_clipboard_objects (GNM_SHEET_CONTROL (scg),
			selection_data, objects);
	else if (strcmp (target_name, "application/x-goffice-graph") == 0)
		scg_drag_send_graph (scg, selection_data, objects, target_name);
	else if (strncmp (target_name, "image/", 6) == 0)
		scg_drag_send_image (scg, selection_data, objects, target_name);
	else if (strcmp (target_name, "UTF8_STRING") == 0)
		scg_drag_send_text (scg, selection_data);

	g_free (target_name);
	g_slist_free (objects);
}

void
scg_delete_sheet_if_possible (SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = scg_sheet (scg);
	Workbook *wb = sheet->workbook;

	/* If this is the last sheet left, ignore the request */
	if (workbook_sheet_count (wb) != 1) {
		WorkbookSheetState *old_state = workbook_sheet_state_new (wb);
		WorkbookControl *wbc = sc->wbc;
		workbook_sheet_delete (sheet);
		/* Careful: sc just ceased to be valid.  */
		cmd_reorganize_sheets (wbc, old_state, sheet);
	}
}

void
scg_reload_item_edits (SheetControlGUI *scg)
{
	SCG_FOREACH_PANE (scg, pane, {
			if (pane->editor != NULL)
				goc_item_bounds_changed
					(GOC_ITEM (pane->editor));
	});
}
