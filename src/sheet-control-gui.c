/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * sheet-control-gui.c: Implements a graphic control for a sheet.
 *
 * Copyright (C) 2000-2006 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 1997-1999 Miguel de Icaza (miguel@kernel.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include "gnumeric.h"
#include "sheet-control-gui-priv.h"

#include "sheet.h"
#include "sheet-private.h"
#include "sheet-view.h"
#include "sheet-merge.h"
#include "workbook.h"
#include "workbook-view.h"
#include "workbook-cmd-format.h"
#include "wbc-gtk-impl.h"
#include "cell.h"
#include "selection.h"
#include "style.h"
#include "sheet-style.h"
#include "sheet-object-impl.h"
#include "sheet-object-cell-comment.h"
#include "sheet-object-image.h"
#include "gui-util.h"
#include "parse-util.h"
#include "selection.h"
#include "application.h"
#include "cellspan.h"
#include "cmd-edit.h"
#include "commands.h"
#include "clipboard.h"
#include "dialogs.h"
#include "gui-file.h"
#include "sheet-merge.h"
#include "ranges.h"
#include "xml-sax.h"
#include "xml-io.h"

#include "gnm-pane-impl.h"
#include "item-bar.h"
#include "item-cursor.h"
#include "widgets/gnumeric-expr-entry.h"

#include <goffice/utils/go-file.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/app/io-context.h>

#include <gdk/gdkkeysyms.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-output-memory.h>
#include <gtk/gtk.h>

#include <string.h>

#define DEBUG_DND

static GObjectClass *scg_parent_class;

static void scg_unant        (SheetControl *sc);
static void set_resize_pane_pos (SheetControlGUI *scg, GtkPaned *p);
static void cb_resize_pane_motion (GtkPaned *p, GParamSpec *pspec, SheetControlGUI *scg);

GnmPane *
scg_pane (SheetControlGUI *scg, int p)
{
	/* it is ok to request a pane when we are not frozen */
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);
	g_return_val_if_fail (p >= 0, NULL);
	g_return_val_if_fail (p < 4, NULL);

	return scg->pane[p];
}


SheetView *
scg_view (SheetControlGUI const *scg)
{
	return scg->sheet_control.view;
}

Sheet *
scg_sheet (SheetControlGUI const *scg)
{
	return scg->sheet_control.sheet;
}

WorkbookControl *
scg_wbc (SheetControlGUI const *scg)
{
	return scg->sheet_control.wbc;
}

WBCGtk *
scg_wbcg (SheetControlGUI const *scg)
{
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);
	return scg->wbcg;
}

static void
scg_redraw_all (SheetControl *sc, gboolean headers)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	SCG_FOREACH_PANE (scg, pane, {
		foo_canvas_request_redraw (FOO_CANVAS (pane),
			0, 0, G_MAXINT, G_MAXINT);
		if (headers) {
			if (NULL != pane->col.canvas)
				foo_canvas_request_redraw (pane->col.canvas,
					0, 0, G_MAXINT, G_MAXINT);
			if (NULL != pane->row.canvas)
				foo_canvas_request_redraw (pane->row.canvas,
					0, 0, G_MAXINT, G_MAXINT);
		}
	});
}

static void
scg_redraw_range (SheetControl *sc, GnmRange const *r)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	Sheet const *sheet = scg_sheet (scg);
	GnmRange visible, area;

	SCG_FOREACH_PANE (scg, pane, {
		visible.start = pane->first;
		visible.end = pane->last_visible;

		if (range_intersection (&area, r, &visible)) {
			sheet_range_bounding_box (sheet, &area);
			gnm_pane_redraw_range (pane, &area);
		}
	};);
}

/* A rough guess of the trade off point between of redrawing all
 * and calculating the redraw size
 */
#define COL_HEURISTIC	20
#define ROW_HEURISTIC	50

static void
scg_redraw_headers (SheetControl *sc,
		    gboolean const col, gboolean const row,
		    GnmRange const * r /* optional == NULL */)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	GnmPane *pane;
	int i;

	for (i = scg->active_panes; i-- > 0 ; ) {
		if (NULL == (pane = scg->pane[i]))
			continue;

		if (col && pane->col.canvas != NULL) {
			int left = 0, right = G_MAXINT - 1;
			FooCanvas * const col_canvas = FOO_CANVAS (pane->col.canvas);

			if (r != NULL) {
				int const size = r->end.col - r->start.col;
				if (-COL_HEURISTIC < size && size < COL_HEURISTIC) {
					left = pane->first_offset.col +
						scg_colrow_distance_get (scg, TRUE,
									 pane->first.col, r->start.col);
					right = left +
						scg_colrow_distance_get (scg, TRUE,
									 r->start.col, r->end.col+1);
				}
			}
			/* Request excludes the far coordinate.  Add 1 to include them */
			if (col_canvas->scroll_x1) {
				foo_canvas_request_redraw (col_canvas,
					gnm_foo_canvas_x_w2c (pane->col.canvas, right + 1), 0,
					gnm_foo_canvas_x_w2c (pane->col.canvas, left), G_MAXINT);
			} else
				foo_canvas_request_redraw (col_canvas,
					left, 0, right+1, G_MAXINT);
		}

		if (row && pane->row.canvas != NULL) {
			int top = 0, bottom = G_MAXINT - 1;
			if (r != NULL) {
				int const size = r->end.row - r->start.row;
				if (-ROW_HEURISTIC < size && size < ROW_HEURISTIC) {
					top = pane->first_offset.row +
						scg_colrow_distance_get (scg, FALSE,
									 pane->first.row, r->start.row);
					bottom = top +
						scg_colrow_distance_get (scg, FALSE,
									 r->start.row, r->end.row+1);
				}
			}
			/* Request excludes the far coordinate.  Add 1 to include them */
			foo_canvas_request_redraw (FOO_CANVAS (pane->row.canvas),
				0, top, G_MAXINT, bottom+1);
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
			 ItemBar const *ib, gboolean is_cols, int w, int h,
			 GPtrArray *btns, GtkWidget *box)
{
	GtkStyle *style;
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

		GTK_WIDGET_UNSET_FLAGS (btn, GTK_CAN_FOCUS);
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

	style = gtk_style_new ();
	if (style->font_desc)
		pango_font_description_free (style->font_desc);
	style->font_desc = pango_font_describe (item_bar_normal_font (ib));

	/* size all of the button so things work after a zoom */
	for (i = 0 ; i < btns->len ; i++) {
		GtkWidget *btn = g_ptr_array_index (btns, i);
		GtkWidget *label = GTK_BIN (GTK_BIN (btn)->child)->child;
		gtk_widget_set_size_request (GTK_WIDGET (btn), w, h);
		gtk_widget_set_style (label, style);
	}

	g_object_unref (style);
	gtk_widget_show_all (box);
}

static void
scg_resize (SheetControlGUI *scg, gboolean force_scroll)
{
	Sheet const *sheet = scg_sheet (scg);
	double const scale = 1. / sheet->last_zoom_factor_used;
	GnmPane *pane = scg_pane (scg, 0);
	int h, w, btn_h, btn_w, tmp;

	/* Recalibrate the starting offsets */
	pane->first_offset.col = scg_colrow_distance_get (scg,
		TRUE, 0, pane->first.col);
	pane->first_offset.row = scg_colrow_distance_get (scg,
		FALSE, 0, pane->first.row);

	/* resize Pane[0] headers */
	h = item_bar_calc_size (scg->pane[0]->col.item);
	btn_h = h - item_bar_indent (scg->pane[0]->col.item);
	w = item_bar_calc_size (scg->pane[0]->row.item);
	btn_w = w - item_bar_indent (scg->pane[0]->row.item);
	gtk_widget_set_size_request (scg->select_all_btn, btn_w, btn_h);
	gtk_widget_set_size_request (GTK_WIDGET (scg->pane[0]->col.canvas), -1, h);
	gtk_widget_set_size_request (GTK_WIDGET (scg->pane[0]->row.canvas), w, -1);

	tmp = item_bar_group_size (scg->pane[0]->col.item,
		sheet->cols.max_outline_level);
	scg_setup_group_buttons (scg, sheet->cols.max_outline_level,
		scg->pane[0]->col.item, TRUE,
		tmp, tmp, scg->col_group.buttons, scg->col_group.button_box);
	scg_setup_group_buttons (scg, sheet->rows.max_outline_level,
		scg->pane[0]->row.item, FALSE,
		-1, btn_h, scg->row_group.buttons, scg->row_group.button_box);

	/* When setting a canvas scroll region to [x0 .. x1[ , pixel x1 is not displayed.
	 * That's why we set scroll region to [-FACTOR + 1 .. +1[ in rtl mode, because
	 * we want x(ltr) = - x(rtl). */

	/* no need to resize panes that are about to go away while unfreezing */
	if (scg->active_panes == 1 || !sv_is_frozen (scg_view (scg))) {
		if (sheet->text_is_rtl)
			foo_canvas_set_scroll_region (scg->pane[0]->col.canvas,
				-GNM_PANE_MAX_X * scale + 1.0, 0, 1.0, h * scale);
		else
			foo_canvas_set_scroll_region (scg->pane[0]->col.canvas,
				0, 0, GNM_PANE_MAX_X * scale, h * scale);
		foo_canvas_set_scroll_region (scg->pane[0]->row.canvas,
			0, 0, w * scale, GNM_PANE_MAX_Y * scale);
	} else {
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

		/* pane 0 has already been done */
		for (i = scg->active_panes; i-- > 1 ; ) {
			GnmPane *pane = scg->pane[i];
			if (NULL != pane) {
				pane->first_offset.col = scg_colrow_distance_get (
					scg, TRUE, 0, pane->first.col);
				pane->first_offset.row = scg_colrow_distance_get (
					scg, FALSE, 0, pane->first.row);
			}
		}

		if (scg->pane[1]) {
			gtk_widget_set_size_request (GTK_WIDGET (scg->pane[1]), r - l, -1);
			/* The item_bar_calcs should be equal */
			/* FIXME : The canvas gets confused when the initial scroll
			 * region is set too early in its life cycle.
			 * It likes it to be at the origin, we can live with that for now.
			 * However, we really should track the bug eventually.
			 */
			h = item_bar_calc_size (scg->pane[1]->col.item);
			gtk_widget_set_size_request (GTK_WIDGET (scg->pane[1]->col.canvas), r - l, h);
			if (sheet->text_is_rtl)
				foo_canvas_set_scroll_region (scg->pane[1]->col.canvas,
					-GNM_PANE_MAX_X * scale + 1.0, 0, 1.0, h * scale);
			else
				foo_canvas_set_scroll_region (scg->pane[1]->col.canvas,
					0, 0, GNM_PANE_MAX_X * scale, h * scale);
		}

		if (scg->pane[3]) {
			gtk_widget_set_size_request (GTK_WIDGET (scg->pane[3]), -1,    b - t);
			/* The item_bar_calcs should be equal */
			w = item_bar_calc_size (scg->pane[3]->row.item);
			gtk_widget_set_size_request (GTK_WIDGET (scg->pane[3]->row.canvas), w, b - t);
			foo_canvas_set_scroll_region (scg->pane[3]->row.canvas,
				0, 0, w * scale, GNM_PANE_MAX_Y * scale);
		}

		if (scg->pane[2])
			gtk_widget_set_size_request (GTK_WIDGET (scg->pane[2]), r - l, b - t);

		if (sheet->text_is_rtl)
			foo_canvas_set_scroll_region (scg->pane[0]->col.canvas,
				-GNM_PANE_MAX_X * scale + 1.0, 0, 1.0, h * scale);
		else
			foo_canvas_set_scroll_region (scg->pane[0]->col.canvas,
				0, 0, GNM_PANE_MAX_X * scale, h * scale);
		foo_canvas_set_scroll_region (scg->pane[0]->row.canvas,
			0, 0, w * scale, GNM_PANE_MAX_Y * scale);
	}

	SCG_FOREACH_PANE (scg, pane, {
		if (sheet->text_is_rtl)
			foo_canvas_set_scroll_region (FOO_CANVAS (pane),
				-(GNM_PANE_MAX_X * scale) + 1.0, 0., 1.0, GNM_PANE_MAX_Y * scale);
		else
			foo_canvas_set_scroll_region (FOO_CANVAS (pane),
				0., 0., GNM_PANE_MAX_X * scale,  GNM_PANE_MAX_Y * scale);
		gnm_pane_reposition_cursors (pane);
	});
}

static void
scg_resize_virt (SheetControl *sc, gboolean force_scroll)
{
	scg_resize ((SheetControlGUI *)sc, force_scroll);
}

/**
 * scg_scrollbar_config :
 * @sc :
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
static void
scg_scrollbar_config (SheetControl const *sc)
{
	SheetControlGUI *scg = SHEET_CONTROL_GUI (sc);
	GtkAdjustment *va = scg->va;
	GtkAdjustment *ha = scg->ha;
	GnmPane *pane = scg_pane (scg, 0);
	Sheet const    *sheet = sc->sheet;
	SheetView const*sv = sc->view;
	int const last_col = pane->last_full.col;
	int const last_row = pane->last_full.row;
	int max_col = last_col;
	int max_row = last_row;

	if (sv_is_frozen (sv)) {
		ha->lower = sv->unfrozen_top_left.col;
		va->lower = sv->unfrozen_top_left.row;
	} else
		ha->lower = va->lower = 0;

	if (max_row < sheet->rows.max_used)
		max_row = sheet->rows.max_used;
	if (max_row < sheet->max_object_extent.row)
		max_row = sheet->max_object_extent.row;
	va->upper = max_row + 1;
	va->value = pane->first.row;
	va->page_size = last_row - pane->first.row + 1;
	va->page_increment = MAX (va->page_size - 3.0, 1.0);
	va->step_increment = 1;

	if (max_col < sheet->cols.max_used)
		max_col = sheet->cols.max_used;
	if (max_col < sheet->max_object_extent.col)
		max_col = sheet->max_object_extent.col;
	ha->upper = max_col + 1;
	ha->page_size = last_col - pane->first.col + 1;
	ha->value = pane->first.col;
	ha->page_increment = MAX (ha->page_size - 3.0, 1.0);
	ha->step_increment = 1;

	gtk_adjustment_changed (va);
	gtk_adjustment_changed (ha);
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
			0, 0, gnm_sheet_get_max_cols (sheet) - 1, gnm_sheet_get_max_rows (sheet) - 1);
		gnm_expr_entry_signal_update (
			wbcg_get_entry_logical (scg->wbcg), TRUE);
	} else if (wbc_gtk_get_guru (scg->wbcg) == NULL) {
		SheetView *sv = scg_view (scg);

		scg_mode_edit (scg);
		wbcg_edit_finish (scg->wbcg, WBC_EDIT_REJECT, NULL);
		sv_selection_reset (sv);
		sv_selection_add_full (sv, sv->edit_pos.col, sv->edit_pos.row,
			0, 0, gnm_sheet_get_max_cols (sheet) - 1, gnm_sheet_get_max_rows (sheet) - 1);
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
					index, 0, index, gnm_sheet_get_max_rows (sv->sheet) - 1);
			else
				scg_rangesel_bound (scg,
					0, index, gnm_sheet_get_max_cols (sv->sheet) - 1, index);
		} else if (is_cols) {
			GnmPane *pane =
				scg_pane (scg, scg->pane[3] ? 3 : 0);
			sv_selection_add_full (sv,
				index, pane->first.row,
				index, 0,
				index, gnm_sheet_get_max_rows (sv->sheet) - 1);
		} else {
			GnmPane *pane =
				scg_pane (scg, scg->pane[1] ? 1 : 0);
			sv_selection_add_full (sv,
				pane->first.col, index,
				0, index,
				gnm_sheet_get_max_cols (sv->sheet) - 1, index);
		}
	}

	/* The edit pos, and the selection may have changed */
	if (!rangesel)
		sheet_update (sv->sheet);
	return TRUE;
}

/***************************************************************************/

static void
cb_select_all_btn_expose (GtkWidget *widget, GdkEventExpose *event, SheetControlGUI *scg)
{
	int offset = scg_sheet (scg)->text_is_rtl ? -1 : 0;

	/* This should be keep in sync with item_bar_cell code (item-bar.c) */
	gdk_draw_rectangle (widget->window,
			    widget->style->bg_gc[GTK_STATE_ACTIVE],
			    TRUE,
			    offset + 1, 1, widget->allocation.width - 1, widget->allocation.height - 1);
	gtk_paint_shadow (widget->style, widget->window, GTK_STATE_NORMAL, GTK_SHADOW_OUT,
			  NULL, NULL, "GnmItemBarCell",
			  offset, 0, widget->allocation.width + 1, widget->allocation.height + 1);
}

static gboolean
cb_select_all_btn_event (GtkWidget *widget, GdkEvent *event, SheetControlGUI *scg)
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
	scg_set_top_row (scg, range->adjustment->value);
}
static void
cb_hscrollbar_value_changed (GtkRange *range, SheetControlGUI *scg)
{
	scg_set_left_col (scg, range->adjustment->value);
}

static void
cb_hscrollbar_adjust_bounds (GtkRange *range, gdouble new_value, Sheet *sheet)
{
	gdouble limit = range->adjustment->upper - range->adjustment->page_size;
	if (range->adjustment->upper < gnm_sheet_get_max_cols (sheet) && new_value >= limit) {
		range->adjustment->upper = new_value + range->adjustment->page_size + 1;
		if (range->adjustment->upper > gnm_sheet_get_max_cols (sheet))
			range->adjustment->upper = gnm_sheet_get_max_cols (sheet);
		gtk_adjustment_changed (range->adjustment);
	}
}
static void
cb_vscrollbar_adjust_bounds (GtkRange *range, gdouble new_value, Sheet *sheet)
{
	gdouble limit = range->adjustment->upper - range->adjustment->page_size;
	if (range->adjustment->upper < gnm_sheet_get_max_rows (sheet) && new_value >= limit) {
		range->adjustment->upper = new_value + range->adjustment->page_size + 1;
		if (range->adjustment->upper > gnm_sheet_get_max_rows (sheet))
			range->adjustment->upper = gnm_sheet_get_max_rows (sheet);
		gtk_adjustment_changed (range->adjustment);
	}
}

static void
cb_table_destroy (SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	int i;

	scg->table = NULL;

	scg_mode_edit (scg);	/* finish any object edits */
	scg_unant (sc);		/* Make sure that everything is unanted */

	if (scg->wbcg) {
		GtkWindow *toplevel = wbcg_toplevel (scg->wbcg);

		/* Only pane-0 ever gets focus */
		if (NULL != toplevel &&
		    toplevel->focus_widget == GTK_WIDGET (scg_pane (scg, 0)))
			gtk_window_set_focus (toplevel, NULL);
	}

	for (i = scg->active_panes; i-- > 0 ; )
		if (NULL != scg->pane[i]) {
			gtk_object_destroy (GTK_OBJECT (scg->pane[i]));
			scg->pane[i] = NULL;
		}

	g_object_unref (G_OBJECT (scg));
}

static void
scg_init (SheetControlGUI *scg)
{
	scg->sheet_control.sheet = NULL;

	scg->comment.selected = NULL;
	scg->comment.item = NULL;
	scg->comment.timer = -1;

	scg->delayedMovement.timer = -1;
	scg->delayedMovement.handler = NULL;

	scg->grab_stack = 0;
	scg->new_object = NULL;
	scg->selected_objects = NULL;
}

/*************************************************************************/

/*
 * calc_left_position:
 * @pane: a #GnmPane
 * @x: first row x position.
 *
 * Calculate x position of left row in canvas coordinates.
 */
#define calc_left_row_position(pane,x)	(scg_sheet ((pane)->simple.scg)->text_is_rtl) ? \
						gnm_foo_canvas_x_w2c ((FooCanvas *) (pane), \
						(x) + GTK_WIDGET (pane)->allocation.width - 1) : (x)

/**
 * gnm_pane_update_inital_top_left :
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

static int
bar_set_left_col (GnmPane *pane, int new_first_col)
{
	FooCanvas *colc;
	int col_offset;
	Sheet *sheet = ((SheetControl*) pane->simple.scg)->sheet;

	g_return_val_if_fail (0 <= new_first_col && new_first_col < gnm_sheet_get_max_cols (sheet), 0);

	col_offset = pane->first_offset.col +=
		scg_colrow_distance_get (pane->simple.scg, TRUE, pane->first.col, new_first_col);
	pane->first.col = new_first_col;
	col_offset  = calc_left_row_position (pane, col_offset);

	/* Scroll the column headers */
	if (NULL != (colc = pane->col.canvas))
		foo_canvas_scroll_to (colc, col_offset,
			pane->first_offset.row);

	return col_offset;
}

static void
gnm_pane_set_left_col (GnmPane *pane, int new_first_col)
{
	Sheet *sheet;
	g_return_if_fail (pane != NULL);
	sheet = ((SheetControl*) pane->simple.scg)->sheet;
	g_return_if_fail (0 <= new_first_col && new_first_col < gnm_sheet_get_max_cols (sheet));

	if (pane->first.col != new_first_col) {
		FooCanvas * const canvas = FOO_CANVAS (pane);
		int const col_offset = bar_set_left_col (pane, new_first_col);

		gnm_pane_compute_visible_region (pane, FALSE);
		foo_canvas_scroll_to (canvas, col_offset, pane->first_offset.row);
		gnm_pane_update_inital_top_left (pane);
	}
}

void
scg_set_left_col (SheetControlGUI *scg, int col)
{
	Sheet const *sheet;
	GnmRange const *bound;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	sheet = scg_sheet (scg);
	bound = &sheet->priv->unhidden_region;
	if (col < bound->start.col)
		col = bound->start.col;
	else if (col >= gnm_sheet_get_max_cols (sheet))
		col = gnm_sheet_get_max_cols (sheet) - 1;
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

static int
bar_set_top_row (GnmPane *pane, int new_first_row)
{
	FooCanvas *rowc;
	int row_offset;
	Sheet *sheet = ((SheetControl*) pane->simple.scg)->sheet;

	g_return_val_if_fail (0 <= new_first_row && new_first_row < gnm_sheet_get_max_rows (sheet), 0);

	row_offset = pane->first_offset.row +=
		scg_colrow_distance_get (pane->simple.scg, FALSE, pane->first.row, new_first_row);
	pane->first.row = new_first_row;

	/* Scroll the row headers */
	if (NULL != (rowc = pane->row.canvas))
		foo_canvas_scroll_to (rowc, 0, row_offset);

	return row_offset;
}

static void
gnm_pane_set_top_row (GnmPane *pane, int new_first_row)
{
	Sheet *sheet;
	g_return_if_fail (pane != NULL);
	sheet = ((SheetControl*) pane->simple.scg)->sheet;
	g_return_if_fail (0 <= new_first_row && new_first_row < gnm_sheet_get_max_rows (sheet));

	if (pane->first.row != new_first_row) {
		FooCanvas * const canvas = FOO_CANVAS(pane);
		int const row_offset = bar_set_top_row (pane, new_first_row);
		int col_offset = pane->first_offset.col;

		gnm_pane_compute_visible_region (pane, FALSE);
		col_offset = calc_left_row_position (pane, col_offset);
		foo_canvas_scroll_to (canvas, col_offset, row_offset);
		gnm_pane_update_inital_top_left (pane);
	}
}

void
scg_set_top_row (SheetControlGUI *scg, int row)
{
	Sheet const *sheet;
	GnmRange const *bound;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	sheet = scg_sheet (scg);
	bound = &sheet->priv->unhidden_region;
	if (row < bound->start.row)
		row = bound->start.row;
	else if (row >= gnm_sheet_get_max_rows (sheet))
		row = gnm_sheet_get_max_rows (sheet) - 1;
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
	int col_offset, row_offset;

	if (pane->first.col != col || force_scroll) {
		if (force_scroll) {
			/* Clear the offsets in case col/row size changed */
			pane->first_offset.col = 0;
			pane->first.col = 0;
		}
		col_offset = bar_set_left_col (pane, col);
		changed = TRUE;
	} else {
		col_offset = calc_left_row_position (pane, pane->first_offset.col);
	}

	if (pane->first.row != row || force_scroll) {
		if (force_scroll) {
			/* Clear the offsets in case col/row size changed */
			pane->first_offset.row = 0;
			pane->first.row = 0;
		}
		row_offset = bar_set_top_row (pane, row);
		changed = TRUE;
	} else
		row_offset = pane->first_offset.row;

	if (!changed)
		return;

	gnm_pane_compute_visible_region (pane, force_scroll);
	foo_canvas_scroll_to (FOO_CANVAS (pane), col_offset, row_offset);
	gnm_pane_update_inital_top_left (pane);
}

static void
scg_set_top_left (SheetControl *sc, int col, int row)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	/* We could be faster if necessary */
	scg_set_left_col (scg, col);
	scg_set_top_row (scg, row);
}

static void
gnm_pane_make_cell_visible (GnmPane *pane, int col, int row,
			      gboolean const force_scroll)
{
	FooCanvas *canvas;
	Sheet *sheet;
	int   new_first_col, new_first_row;

	g_return_if_fail (IS_GNM_PANE (pane));

	/* Avoid calling this before the canvas is realized: We do not know the
	 * visible area, and would unconditionally scroll the cell to the top
	 * left of the viewport.
	 */
	if (!GTK_WIDGET_REALIZED (pane))
		return;

	sheet = ((SheetControl *) pane->simple.scg)->sheet;
	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);
	g_return_if_fail (col < gnm_sheet_get_max_cols (sheet));
	g_return_if_fail (row < gnm_sheet_get_max_rows (sheet));

	canvas = FOO_CANVAS (pane);

	/* Find the new pane->first.col */
	if (col < pane->first.col) {
		new_first_col = col;
	} else if (col > pane->last_full.col) {
		int width = GTK_WIDGET (canvas)->allocation.width;
		ColRowInfo const * const ci = sheet_col_get_info (sheet, col);
		if (ci->size_pixels < width) {
			int first_col = (pane->last_visible.col == pane->first.col)
				? pane->first.col : col;

			for (; first_col > 0; --first_col) {
				ColRowInfo const * const ci = sheet_col_get_info (sheet, first_col);
				if (ci->visible) {
					width -= ci->size_pixels;
					if (width < 0)
						break;
				}
			}
			new_first_col = first_col+1;
		} else
			new_first_col = col;
	} else
		new_first_col = pane->first.col;

	/* Find the new pane->first.row */
	if (row < pane->first.row) {
		new_first_row = row;
	} else if (row > pane->last_full.row) {
		int height = GTK_WIDGET (canvas)->allocation.height;
		ColRowInfo const * const ri = sheet_row_get_info (sheet, row);
		if (ri->size_pixels < height) {
			int first_row = (pane->last_visible.row == pane->first.row)
				? pane->first.row : row;

			for (; first_row > 0; --first_row) {
				ColRowInfo const * const ri = sheet_row_get_info (sheet, first_row);
				if (ri->visible) {
					height -= ri->size_pixels;
					if (height < 0)
						break;
				}
			}
			new_first_row = first_row+1;
		} else
			new_first_row = row;
	} else
		new_first_row = pane->first.row;

	gnm_pane_set_top_left (pane, new_first_col, new_first_row,
				 force_scroll);
}

/**
 * scg_make_cell_visible
 * @scg  : The gui control
 * @col  :
 * @row  :
 * @force_scroll : Completely recalibrate the offsets to the new position
 * @couple_panes : Scroll scroll dynamic panes back to bounds if target
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

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

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
	gboolean const being_frozen = sv_is_frozen (sc->view);

	if (being_frozen) {
		GnmCellPos const *tl = &sc->view->frozen_top_left;
		GnmCellPos const *br = &sc->view->unfrozen_top_left;
		gboolean const freeze_h = br->col > tl->col;
		gboolean const freeze_v = br->row > tl->row;

		gnm_pane_bound_set (scg->pane[0],
			br->col, br->row,
			gnm_sheet_get_max_cols (sc->sheet) - 1, gnm_sheet_get_max_rows (sc->sheet) - 1);

		if (freeze_h) {
			scg->active_panes = 2;
			if (!scg->pane[1]) {
				scg->pane[1] = gnm_pane_new (scg, TRUE, FALSE, 1);
				gtk_table_attach (scg->inner_table,
					GTK_WIDGET (scg->pane[1]),
					1, 2, 2, 3,
					GTK_FILL | GTK_SHRINK,
					GTK_EXPAND | GTK_FILL | GTK_SHRINK,
					0, 0);
				gtk_table_attach (scg->inner_table,
					GTK_WIDGET (scg->pane[1]->col.canvas),
					1, 2, 0, 1,
					GTK_FILL | GTK_SHRINK,
					GTK_FILL,
					0, 0);
			}
			gnm_pane_bound_set (scg->pane[1],
				tl->col, br->row, br->col - 1, gnm_sheet_get_max_rows (sc->sheet) - 1);
		}
		if (freeze_h && freeze_v) {
			scg->active_panes = 4;
			if (!scg->pane[2]) {
				scg->pane[2] = gnm_pane_new (scg, FALSE, FALSE,  2);
				gtk_table_attach (scg->inner_table,
					GTK_WIDGET (scg->pane[2]),
					1, 2, 1, 2,
					GTK_FILL | GTK_SHRINK,
					GTK_FILL,
					0, 0);
			}
			gnm_pane_bound_set (scg->pane[2],
				tl->col, tl->row, br->col - 1, br->row - 1);
		}
		if (freeze_v) {
			scg->active_panes = 4;
			if (!scg->pane[3]) {
				scg->pane[3] = gnm_pane_new (scg, FALSE, TRUE, 3);
				gtk_table_attach (scg->inner_table,
					GTK_WIDGET (scg->pane[3]),
					2, 3, 1, 2,
					GTK_EXPAND | GTK_FILL | GTK_SHRINK,
					GTK_FILL | GTK_SHRINK,
					0, 0);
				gtk_table_attach (scg->inner_table,
					GTK_WIDGET (scg->pane[3]->row.canvas),
					0, 1, 1, 2,
					GTK_FILL | GTK_SHRINK,
					GTK_FILL,
					0, 0);
			}
			gnm_pane_bound_set (scg->pane[3],
				br->col, tl->row, gnm_sheet_get_max_cols (sc->sheet) - 1, br->row - 1);
		}
	} else {
		int i;
		for (i = 1 ; i <= 3 ; i++)
			if (scg->pane[i]) {
				gtk_object_destroy (GTK_OBJECT (scg->pane[i]));
				scg->pane[i] = NULL;
			}

		scg->active_panes = 1;
		gnm_pane_bound_set (scg->pane[0],
			0, 0, gnm_sheet_get_max_cols (sc->sheet) - 1, gnm_sheet_get_max_rows (sc->sheet) - 1);
	}

	gtk_widget_show_all (GTK_WIDGET (scg->inner_table));

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
cb_scg_prefs (G_GNUC_UNUSED Sheet *sheet,
	      G_GNUC_UNUSED GParamSpec *pspec,
	      SheetControlGUI *scg)
{
	scg_adjust_preferences (scg);
}

static void
cb_scg_redraw (Sheet *sheet,
	       GParamSpec *pspec,
	       SheetControlGUI *scg)
{
	cb_scg_prefs (sheet, pspec, scg);
	scg_redraw_all (&scg->sheet_control, TRUE);
}

static void
cb_scg_redraw_resize (Sheet *sheet,
		      GParamSpec *pspec,
		      SheetControlGUI *scg)
{
	cb_scg_redraw (sheet, pspec, scg);
	scg_resize (scg, FALSE);
}

static void
cb_scg_direction_changed (SheetControlGUI *scg)
{
	scg_resize (scg, TRUE);
}

static GnmPane const *
resize_pane_pos (SheetControlGUI *scg, GtkPaned *p,
		 int *colrow_result, int *guide_pos)
{
	ColRowInfo const *cri;
	GnmPane const  *pane = scg_pane (scg, 0);
	gboolean const    vert = (p == scg->hpane);
	int colrow, handle, pos = gtk_paned_get_position (p);

	gtk_widget_style_get (GTK_WIDGET (p), "handle-size", &handle, NULL);
	pos += handle / 2;
	if (vert) {
		pos -= GTK_WIDGET (scg->pane[0]->row.canvas)->allocation.width;
		if (scg->pane[1]) {
			int const w = GTK_WIDGET (scg->pane[1])->allocation.width;
			if (pos < w)
				pane = scg_pane (scg, 1);
			else
				pos -= w;
		}
		pos += pane->first_offset.col;
		colrow = gnm_pane_find_col (pane,
					      gnm_pane_x_w2c (pane, pos),
					      guide_pos);
		*guide_pos = gnm_pane_x_w2c (pane, *guide_pos);
	} else {
		pos -= GTK_WIDGET (scg->pane[0]->col.canvas)->allocation.height;
		if (scg->pane[3]) {
			int const h = GTK_WIDGET (scg->pane[3])->allocation.height;
			if (pos < h)
				pane = scg_pane (scg, 3);
			else
				pos -= h;
		}
		pos += pane->first_offset.row;
		colrow = gnm_pane_find_row (pane, pos, guide_pos);
	}
	cri = sheet_colrow_get_info (scg_sheet (scg), colrow, vert);
	if (pos >= (*guide_pos + cri->size_pixels/2)) {
		*guide_pos += cri->size_pixels;
		colrow++;
	}
	if (NULL != colrow_result)
		*colrow_result = colrow;

	return pane;
}

static void
set_resize_pane_pos (SheetControlGUI *scg, GtkPaned *p)
{
	int handle_size, pane_pos, size;

	if (p == scg->vpane) {
		gtk_widget_get_size_request (
			GTK_WIDGET (scg->pane[0]->col.canvas), NULL, &pane_pos);
		if (scg->pane[3]) {
			gtk_widget_get_size_request (
				GTK_WIDGET (scg->pane[3]), NULL, &size);
			pane_pos += size;
		}
	} else {
		gtk_widget_get_size_request (
			GTK_WIDGET (scg->pane[0]->row.canvas), &pane_pos, NULL);
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
	gtk_paned_set_position (p, pane_pos);
	g_signal_handlers_unblock_by_func (G_OBJECT (p),
		G_CALLBACK (cb_resize_pane_motion), scg);
}

static gboolean
resize_pane_finish (SheetControlGUI *scg, GtkPaned *p)
{
	SheetView *sv =	scg_view (scg);
	GnmCellPos frozen_tl, unfrozen_tl;
	GnmPane const *pane;
	int	   colrow, guide_pos;

	if (p->in_drag)
		return TRUE;
	pane = resize_pane_pos (scg, p, &colrow, &guide_pos);

	if (sv_is_frozen (sv)) {
		frozen_tl   = sv->frozen_top_left;
		unfrozen_tl = sv->unfrozen_top_left;
	} else
		frozen_tl = pane->first;
	if (p == scg->hpane) {
		unfrozen_tl.col = colrow;
		if (!sv_is_frozen (sv))
			unfrozen_tl.row = frozen_tl.row = 0;
	} else {
		unfrozen_tl.row = colrow;
		if (!sv_is_frozen (sv))
			unfrozen_tl.col = frozen_tl.col = 0;
	}
	sv_freeze_panes	(sv, &frozen_tl, &unfrozen_tl);

	scg->pane_drag_handler = 0;
	scg_size_guide_stop (scg);

	set_resize_pane_pos (scg, p);

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
	int colrow, guide_pos;

	resize_pane_pos (scg, p, &colrow, &guide_pos);
	if (scg->pane_drag_handler == 0 && p->in_drag) {
		scg_size_guide_start (scg, vert, colrow, 7);
		scg->pane_drag_handler = g_timeout_add (250,
			vert ? (GSourceFunc) cb_resize_hpane_finish
			     : (GSourceFunc) cb_resize_vpane_finish,
			(gpointer) scg);
	}
	if (scg->pane_drag_handler)
		scg_size_guide_motion (scg, vert, guide_pos);
}

SheetControlGUI *
sheet_control_gui_new (SheetView *sv, WBCGtk *wbcg)
{
	SheetControlGUI *scg;
	GtkUpdateType scroll_update_policy;
	Sheet *sheet;

	g_return_val_if_fail (IS_SHEET_VIEW (sv), NULL);

	sheet = sv_sheet (sv);

	scg = g_object_new (SHEET_CONTROL_GUI_TYPE, NULL);
	scg->wbcg = wbcg;
	scg->sheet_control.wbc = WORKBOOK_CONTROL (wbcg);

	g_object_weak_ref (G_OBJECT (wbcg),
		(GWeakNotify) cb_wbc_destroyed,
		scg);

	scg->active_panes = 1;
	scg->pane[0] = NULL;
	scg->pane[1] = NULL;
	scg->pane[2] = NULL;
	scg->pane[3] = NULL;
	scg->pane_drag_handler = 0;

	scg->col_group.buttons = g_ptr_array_new ();
	scg->row_group.buttons = g_ptr_array_new ();
	scg->col_group.button_box = gtk_vbox_new (TRUE, 0);
	scg->row_group.button_box = gtk_hbox_new (TRUE, 0);
	scg->select_all_btn = gtk_drawing_area_new ();
	gtk_widget_add_events (scg->select_all_btn, GDK_BUTTON_PRESS_MASK);
	g_signal_connect (G_OBJECT (scg->select_all_btn), "expose-event",
			  G_CALLBACK (cb_select_all_btn_expose), scg);
	g_signal_connect (G_OBJECT (scg->select_all_btn), "event",
			  G_CALLBACK (cb_select_all_btn_event), scg);

	scg->corner	 = GTK_TABLE (gtk_table_new (2, 2, FALSE));
	gtk_table_attach (scg->corner, scg->col_group.button_box,
		1, 2, 0, 1,
		GTK_SHRINK,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		0, 0);
	gtk_table_attach (scg->corner, scg->row_group.button_box,
		0, 1, 1, 2,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		GTK_SHRINK,
		0, 0);
	gtk_table_attach (scg->corner, scg->select_all_btn,
		1, 2, 1, 2,
		0,
		0,
		0, 0);

	scg->pane[1] = scg->pane[2] = scg->pane[3] = NULL;
	scg->pane[0] = gnm_pane_new (scg, TRUE, TRUE, 0);
	scg->inner_table = GTK_TABLE (gtk_table_new (3, 3, FALSE));
	gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->corner),
		0, 1, 0, 1,
		GTK_FILL,
		GTK_FILL,
		0, 0);
	gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->pane[0]->col.canvas),
		2, 3, 0, 1,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		GTK_FILL,
		0, 0);
	gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->pane[0]->row.canvas),
		0, 1, 2, 3,
		GTK_FILL,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		0, 0);
	gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->pane[0]),
		2, 3, 2, 3,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		0, 0);
	gtk_widget_show_all (GTK_WIDGET (scg->inner_table));

	/* Scroll bars and their adjustments */
	scroll_update_policy = gnm_app_live_scrolling ()
		? GTK_UPDATE_CONTINUOUS : GTK_UPDATE_DELAYED;
	scg->va = (GtkAdjustment *)gtk_adjustment_new (0., 0., 1, 1., 1., 1.);
	scg->vs = g_object_new (GTK_TYPE_VSCROLLBAR,
			"adjustment",	 scg->va,
			"update-policy", scroll_update_policy,
			NULL);
	g_signal_connect (G_OBJECT (scg->vs),
		"value_changed",
		G_CALLBACK (cb_vscrollbar_value_changed), scg);
	g_signal_connect (G_OBJECT (scg->vs),
		"adjust_bounds",
		G_CALLBACK (cb_vscrollbar_adjust_bounds), sheet);

	scg->ha = (GtkAdjustment *)gtk_adjustment_new (0., 0., 1, 1., 1., 1.);
	scg->hs = g_object_new (GTK_TYPE_HSCROLLBAR,
			"adjustment", scg->ha,
			"update-policy", scroll_update_policy,
			NULL);
	g_signal_connect (G_OBJECT (scg->hs),
		"value_changed",
		G_CALLBACK (cb_hscrollbar_value_changed), scg);
	g_signal_connect (G_OBJECT (scg->hs),
		"adjust_bounds",
		G_CALLBACK (cb_hscrollbar_adjust_bounds), sheet);

	scg->table = GTK_TABLE (gtk_table_new (4, 4, FALSE));
	gtk_table_attach (scg->table, GTK_WIDGET (scg->inner_table),
		0, 1, 0, 1,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		0, 0);
	scg->vpane = g_object_new (GTK_TYPE_VPANED, NULL);
	gtk_paned_add1 (scg->vpane, gtk_label_new (NULL)); /* use a spacer */
	gtk_paned_add2 (scg->vpane, scg->vs);
	gtk_paned_set_position (scg->vpane, 0);
	gtk_table_attach (scg->table, GTK_WIDGET (scg->vpane),
		1, 2, 0, 1,
		GTK_FILL,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		0, 0);
	scg->hpane = g_object_new (GTK_TYPE_HPANED, NULL);
	gtk_paned_add1 (scg->hpane, gtk_label_new (NULL)); /* use a spacer */
	gtk_paned_add2 (scg->hpane, scg->hs);
	gtk_paned_set_position (scg->hpane, 0);
	gtk_table_attach (scg->table, GTK_WIDGET (scg->hpane),
		0, 1, 1, 2,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		GTK_FILL,
		0, 0);
	/* do not connect until after setting position */
	g_signal_connect (G_OBJECT (scg->vpane), "notify::position",
		G_CALLBACK (cb_resize_pane_motion), scg);
	g_signal_connect (G_OBJECT (scg->hpane), "notify::position",
		G_CALLBACK (cb_resize_pane_motion), scg);
	g_signal_connect_data (G_OBJECT (scg->table),
		"size_allocate",
		G_CALLBACK (scg_scrollbar_config), scg, NULL,
		G_CONNECT_AFTER | G_CONNECT_SWAPPED);
	g_signal_connect_object (G_OBJECT (scg->table),
		"destroy",
		G_CALLBACK (cb_table_destroy), G_OBJECT (scg),
		G_CONNECT_SWAPPED);

	sv_attach_control (sv, SHEET_CONTROL (scg));

	g_object_connect
		(G_OBJECT (sheet),
		 "swapped_signal::notify::text-is-rtl", cb_scg_direction_changed, scg,
		 "signal::notify::display-formulas", cb_scg_redraw, scg,
		 "signal::notify::display-zeros", cb_scg_redraw, scg,
		 "signal::notify::display-grid", cb_scg_redraw, scg,
		 "signal::notify::display-column-header", cb_scg_prefs, scg,
		 "signal::notify::display-row-header", cb_scg_prefs, scg,
		 "signal::notify::use-r1c1", cb_scg_redraw, scg,
		 "signal::notify::display-outlines", cb_scg_redraw_resize, scg,
		 "signal::notify::display-outlines-below", cb_scg_redraw_resize, scg,
		 "signal::notify::display-outlines-right", cb_scg_redraw_resize, scg,
		 NULL);

	return scg;
}

static void
scg_comment_timer_clear (SheetControlGUI *scg)
{
	if (scg->comment.timer != -1) {
		g_source_remove (scg->comment.timer);
		scg->comment.timer = -1;
	}
}

static void
scg_finalize (GObject *object)
{
	SheetControlGUI *scg = SHEET_CONTROL_GUI (object);
	SheetControl	*sc = (SheetControl *) scg;
	Sheet		*sheet = scg_sheet (scg);
	GSList *ptr;

	/* remove the object view before we disappear */
	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next )
		SCG_FOREACH_PANE (scg, pane,
			sheet_object_view_destroy (
				sheet_object_get_view (ptr->data, (SheetObjectViewContainer *)pane));
		);

	g_ptr_array_free (scg->col_group.buttons, TRUE);
	g_ptr_array_free (scg->row_group.buttons, TRUE);

	scg_comment_timer_clear (scg);

	if (scg->delayedMovement.timer != -1) {
		g_source_remove (scg->delayedMovement.timer);
		scg->delayedMovement.timer = -1;
	}
	scg_comment_unselect (scg, scg->comment.selected);

	if (sc->view) {
		Sheet *sheet = sv_sheet (sc->view);
		g_signal_handlers_disconnect_by_func (sheet, cb_scg_prefs, scg);
		g_signal_handlers_disconnect_by_func (sheet, cb_scg_redraw, scg);
		g_signal_handlers_disconnect_by_func (sheet, cb_scg_redraw_resize, scg);
		g_signal_handlers_disconnect_by_func (sheet, cb_scg_direction_changed, scg);
		sv_detach_control (sc);
	}

	if (scg->table) {
		gtk_object_destroy (GTK_OBJECT (scg->table));
		scg->table =NULL;
	}

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

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	/* Always have a pane 0 */
	if (scg->pane[0]->cursor.animated == NULL)
		return;

	SCG_FOREACH_PANE (scg, pane, {
		GSList *l;

		for (l = pane->cursor.animated; l; l = l->next)
			gtk_object_destroy (GTK_OBJECT (l->data));

		g_slist_free (pane->cursor.animated);
		pane->cursor.animated = NULL;
	});
}

static void
scg_ant (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	GList *l;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	/* Always have a grid 0 */
	if (NULL != scg->pane[0]->cursor.animated)
		scg_unant (sc);

	for (l = sc->view->ants; l; l = l->next) {
		GnmRange const *r = l->data;

		SCG_FOREACH_PANE (scg, pane, {
			ItemCursor *ic = ITEM_CURSOR (foo_canvas_item_new (
				pane->grid_items,
				item_cursor_get_type (),
				"SheetControlGUI", scg,
				"style",	ITEM_CURSOR_ANTED,
				NULL));
			item_cursor_bound_set (ic, r);
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
			if (sheet->hide_col_header)
				gtk_widget_hide (GTK_WIDGET (pane->col.canvas));
			else
				gtk_widget_show (GTK_WIDGET (pane->col.canvas));
		}

		if (pane->row.canvas != NULL) {
			if (sheet->hide_row_header)
				gtk_widget_hide (GTK_WIDGET (pane->row.canvas));
			else
				gtk_widget_show (GTK_WIDGET (pane->row.canvas));
		}
	});

	if (sheet->hide_col_header || sheet->hide_row_header)
		gtk_widget_hide (GTK_WIDGET (scg->corner));
	else
		gtk_widget_show (GTK_WIDGET (scg->corner));

	if (scg_wbc (scg) != NULL) {
		WorkbookView *wbv = wb_control_view (scg_wbc (scg));
		if (wbv->show_horizontal_scrollbar)
			gtk_widget_show (scg->hs);
		else
			gtk_widget_hide (scg->hs);

		if (wbv->show_vertical_scrollbar)
			gtk_widget_show (scg->vs);
		else
			gtk_widget_hide (scg->vs);
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
	CONTEXT_COL_WIDTH,
	CONTEXT_COL_HIDE,
	CONTEXT_COL_UNHIDE,
	CONTEXT_ROW_HEIGHT,
	CONTEXT_ROW_HIDE,
	CONTEXT_ROW_UNHIDE,
	CONTEXT_COMMENT_EDIT,
	CONTEXT_HYPERLINK_EDIT,
	CONTEXT_HYPERLINK_ADD,
	CONTEXT_HYPERLINK_REMOVE
};
static gboolean
context_menu_handler (GnumericPopupMenuElement const *element,
		      gpointer user_data)
{
	SheetControlGUI *scg = user_data;
	SheetControl	*sc = (SheetControl *) scg;
	SheetView	*sv = sc->view;
	Sheet		*sheet = sc->sheet;
	WBCGtk *wbcg = scg->wbcg;
	WorkbookControl *wbc = sc->wbc;

	g_return_val_if_fail (element != NULL, TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	switch (element->index) {
	case CONTEXT_CUT :
		sv_selection_cut (sv, wbc);
		break;
	case CONTEXT_COPY :
		sv_selection_copy (sv, wbc);
		break;
	case CONTEXT_PASTE :
		cmd_paste_to_selection (wbc, sv, PASTE_DEFAULT);
		break;
	case CONTEXT_PASTE_SPECIAL :
		dialog_paste_special (wbcg);
		break;
	case CONTEXT_INSERT :
		dialog_insert_cells (wbcg);
		break;
	case CONTEXT_DELETE :
		dialog_delete_cells (wbcg);
		break;
	case CONTEXT_CLEAR_CONTENT :
		cmd_selection_clear (wbc, CLEAR_VALUES);
		break;
	case CONTEXT_FORMAT_CELL :
		dialog_cell_format (wbcg, FD_CURRENT);
		break;
	case CONTEXT_COL_WIDTH :
		dialog_col_width (wbcg, FALSE);
		break;
	case CONTEXT_COL_HIDE :
		cmd_selection_colrow_hide (wbc, TRUE, FALSE);
		break;
	case CONTEXT_COL_UNHIDE :
		cmd_selection_colrow_hide (wbc, TRUE, TRUE);
		break;
	case CONTEXT_ROW_HEIGHT :
		dialog_row_height (wbcg, FALSE);
		break;
	case CONTEXT_ROW_HIDE :
		cmd_selection_colrow_hide (wbc, FALSE, FALSE);
		break;
	case CONTEXT_ROW_UNHIDE :
		cmd_selection_colrow_hide (wbc, FALSE, TRUE);
		break;
	case CONTEXT_COMMENT_EDIT:
		dialog_cell_comment (wbcg, sheet, &sv->edit_pos);
		break;

	case CONTEXT_HYPERLINK_EDIT:
	case CONTEXT_HYPERLINK_ADD:
		dialog_hyperlink (wbcg, sc);
		break;

	case CONTEXT_HYPERLINK_REMOVE: {
		GnmStyle *style = gnm_style_new ();
		gnm_style_set_hlink (style, NULL);
		cmd_selection_format (wbc, style, NULL,
			_("Remove Hyperlink"));
		break;
	}
	default :
		break;
	}
	return TRUE;
}

void
scg_context_menu (SheetControlGUI *scg, GdkEventButton *event,
		  gboolean is_col, gboolean is_row)
{
	Sheet *sheet = scg_sheet (scg);

	enum {
		CONTEXT_DISPLAY_FOR_CELLS = 1,
		CONTEXT_DISPLAY_FOR_ROWS = 2,
		CONTEXT_DISPLAY_FOR_COLS = 4,
		CONTEXT_DISPLAY_WITH_HYPERLINK	  = 8,
		CONTEXT_DISPLAY_WITHOUT_HYPERLINK = 16
	};
	enum {
		CONTEXT_DISABLE_PASTE_SPECIAL	= 1,
		CONTEXT_DISABLE_FOR_ROWS	= 2,
		CONTEXT_DISABLE_FOR_COLS	= 4
	};

	static GnumericPopupMenuElement const popup_elements[] = {
		{ N_("Cu_t"),           GTK_STOCK_CUT,
		    0, 0, CONTEXT_CUT },
		{ N_("_Copy"),          GTK_STOCK_COPY,
		    0, 0, CONTEXT_COPY },
		{ N_("_Paste"),         GTK_STOCK_PASTE,
		    0, 0, CONTEXT_PASTE },
		{ N_("Paste _Special"),	NULL,
		    0, CONTEXT_DISABLE_PASTE_SPECIAL, CONTEXT_PASTE_SPECIAL },

		{ "", NULL, 0, 0, 0 },

		/* TODO : One day make the labels smarter.  Generate them to include
		 * quantities.
		 *	eg : Insert 4 rows
		 *	or : Insert row
		 * This is hard for now because there is no memory management for the label
		 * strings, and the logic that knows the count is elsewhere
		 */
		{ N_("_Insert Cells..."),	NULL,
		    CONTEXT_DISPLAY_FOR_CELLS, 0, CONTEXT_INSERT },
		{ N_("_Delete Cells..."),	GTK_STOCK_DELETE,
		    CONTEXT_DISPLAY_FOR_CELLS, 0, CONTEXT_DELETE },
		{ N_("_Insert Column(s)"), "Gnumeric_ColumnAdd",
		    CONTEXT_DISPLAY_FOR_COLS, CONTEXT_DISABLE_FOR_COLS, CONTEXT_INSERT },
		{ N_("_Delete Column(s)"), "Gnumeric_ColumnDelete",
		    CONTEXT_DISPLAY_FOR_COLS, CONTEXT_DISABLE_FOR_COLS, CONTEXT_DELETE },
		{ N_("_Insert Row(s)"), "Gnumeric_RowAdd",
		    CONTEXT_DISPLAY_FOR_ROWS, CONTEXT_DISABLE_FOR_ROWS, CONTEXT_INSERT },
		{ N_("_Delete Row(s)"), "Gnumeric_RowDelete",
		    CONTEXT_DISPLAY_FOR_ROWS, CONTEXT_DISABLE_FOR_ROWS, CONTEXT_DELETE },

		{ N_("Clear Co_ntents"), GTK_STOCK_CLEAR,
		    0, 0, CONTEXT_CLEAR_CONTENT },
		{ N_("Edit Co_mment..."),"Gnumeric_CommentEdit",
		    0, 0, CONTEXT_COMMENT_EDIT },

		/* TODO : Add the comment modification elements */
		{ "", NULL, 0, 0, 0 },

		{ N_("_Format Cells..."), GTK_STOCK_PROPERTIES,
		    0, 0, CONTEXT_FORMAT_CELL },

		/* Column specific (Note some labels duplicate row labels) */
		{ N_("Column _Width..."), "Gnumeric_ColumnSize",
		    CONTEXT_DISPLAY_FOR_COLS, 0, CONTEXT_COL_WIDTH },
		{ N_("_Hide"),		  "Gnumeric_ColumnHide",
		    CONTEXT_DISPLAY_FOR_COLS, 0, CONTEXT_COL_HIDE },
		{ N_("_Unhide"),	  "Gnumeric_ColumnUnhide",
		    CONTEXT_DISPLAY_FOR_COLS, 0, CONTEXT_COL_UNHIDE },

		/* Row specific (Note some labels duplicate col labels) */
		{ N_("_Row Height..."),	  "Gnumeric_RowSize",
		    CONTEXT_DISPLAY_FOR_ROWS, 0, CONTEXT_ROW_HEIGHT },
		{ N_("_Hide"),		  "Gnumeric_RowHide",
		    CONTEXT_DISPLAY_FOR_ROWS, 0, CONTEXT_ROW_HIDE },
		{ N_("_Unhide"),	  "Gnumeric_RowUnhide",
		    CONTEXT_DISPLAY_FOR_ROWS, 0, CONTEXT_ROW_UNHIDE },

		{ N_("_Hyperlink"),	  "Gnumeric_Link_Add",
		    CONTEXT_DISPLAY_WITHOUT_HYPERLINK, 0,
		    CONTEXT_HYPERLINK_ADD },
		{ N_("Edit _Hyperlink"),	  "Gnumeric_Link_Edit",
		    CONTEXT_DISPLAY_WITH_HYPERLINK, 0,
		    CONTEXT_HYPERLINK_EDIT },
		{ N_("_Remove Hyperlink"),	  "Gnumeric_Link_Delete",
		    CONTEXT_DISPLAY_WITH_HYPERLINK, 0,
		    CONTEXT_HYPERLINK_REMOVE },

		{ NULL, NULL, 0, 0, 0 },
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
	gboolean has_link = FALSE;

	wbcg_edit_finish (scg->wbcg, WBC_EDIT_REJECT, NULL);

	/* Now see if there is some selection which selects a whole row or a
	 * whole column and disable the insert/delete col/row menu items
	 * accordingly
	 */
	for (l = scg_view (scg)->selections; l != NULL; l = l->next) {
		GnmRange const *r = l->data;

		if (r->start.row == 0 && r->end.row == gnm_sheet_get_max_rows (sheet) - 1)
			sensitivity_filter |= CONTEXT_DISABLE_FOR_ROWS;

		if (r->start.col == 0 && r->end.col == gnm_sheet_get_max_cols (sheet) - 1)
			sensitivity_filter |= CONTEXT_DISABLE_FOR_COLS;

		if (!has_link && sheet_style_region_contains_link (sheet, r))
			has_link = TRUE;
	}

	if (display_filter & CONTEXT_DISPLAY_FOR_CELLS)
		display_filter |= ((has_link) ?
				   CONTEXT_DISPLAY_WITH_HYPERLINK : CONTEXT_DISPLAY_WITHOUT_HYPERLINK);

	gnumeric_create_popup_menu (popup_elements, &context_menu_handler,
				    scg, display_filter,
				    sensitivity_filter, event);
}

static gboolean
cb_redraw_sel (SheetView *sv, GnmRange const *r, gpointer user_data)
{
	SheetControl *sc = user_data;
	scg_redraw_range (sc, r);
	scg_redraw_headers (sc, TRUE, TRUE, r);
	return TRUE;
}

static void
scg_cursor_visible (SheetControlGUI *scg, gboolean is_visible)
{
	SheetControl *sc = (SheetControl *) scg;

	/* there is always a grid 0 */
	if (NULL == scg->pane[0])
		return;

	SCG_FOREACH_PANE (scg, pane,
		item_cursor_set_visibility (pane->cursor.std, is_visible););

	sv_selection_foreach (sc->view, cb_redraw_sel, sc);
}

/***************************************************************************/

static gboolean
scg_mode_clear (SheetControlGUI *scg)
{
	WorkbookControl *wbc;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), FALSE);

	if (scg->new_object != NULL) {
		g_object_unref (G_OBJECT (scg->new_object));
		scg->new_object = NULL;
	}
	scg_object_unselect (scg, NULL);
	wbc = scg_wbc (scg);
	if (wbc != NULL) /* during destruction */
		wb_control_update_action_sensitivity (wbc);

	return TRUE;
}

/**
 * scg_mode_edit:
 * @sc:  The sheet control
 *
 * Put @sheet into the standard state 'edit mode'.  This shuts down
 * any object editing and frees any objects that are created but not
 * realized.
 **/
void
scg_mode_edit (SheetControlGUI *scg)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	scg_mode_clear (scg);

	/* During destruction we have already been disconnected
	 * so don't bother changing the cursor */
	if (scg->table != NULL &&
	    scg_sheet (scg) != NULL &&
	    scg_view (scg) != NULL) {
		scg_set_display_cursor (scg);
		scg_cursor_visible (scg, TRUE);
	}

	if (scg->wbcg != NULL && wbc_gtk_get_guru (scg->wbcg) != NULL &&
	    scg == wbcg_cur_scg	(scg->wbcg))
		wbcg_edit_finish (scg->wbcg, WBC_EDIT_REJECT, NULL);
}

static void
scg_mode_edit_virt (SheetControl *sc)
{
	scg_mode_edit ((SheetControlGUI *)sc);
}

/**
 * scg_mode_create_object :
 * @so : The object the needs to be placed
 *
 * Takes a newly created SheetObject that has not yet been realized and
 * prepares to place it on the sheet.
 * NOTE : Absorbs a reference to the object.
 **/
void
scg_mode_create_object (SheetControlGUI *scg, SheetObject *so)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (scg_mode_clear (scg)) {
		scg->new_object = so;
		scg_cursor_visible (scg, FALSE);
		scg_take_focus (scg);
		scg_set_display_cursor (scg);
		wb_control_update_action_sensitivity (scg_wbc (scg));
	}
}

static int
calc_obj_place (GnmPane *pane, int canvas_coord, gboolean is_col,
		float *offset)
{
	int origin, colrow;
	ColRowInfo const *cri;
	Sheet const *sheet = scg_sheet (pane->simple.scg);

	if (is_col) {
		colrow = gnm_pane_find_col (pane, canvas_coord, &origin);
		cri = sheet_col_get_info (sheet, colrow);
		if (sheet->text_is_rtl) {
			int tmp = canvas_coord;
			canvas_coord = origin;
			origin = tmp;
		}
	} else {
		colrow = gnm_pane_find_row (pane, canvas_coord, &origin);
		cri = sheet_row_get_info (sheet, colrow);
	}

	/* TODO : handle other anchor types */
	*offset = ((float) (canvas_coord - origin))/ ((float) cri->size_pixels);
	return colrow;
}

#define SO_CLASS(so) SHEET_OBJECT_CLASS (G_OBJECT_GET_CLASS(so))

/**
 * scg_object_select
 * @scg: The #SheetControl to edit in.
 * @so : The #SheetObject to select.
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
		scg_mode_clear (scg);
		scg_cursor_visible (scg, FALSE);
		scg_set_display_cursor (scg);
		scg_unant (SHEET_CONTROL (scg));

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

#if 0
	if (SO_CLASS (so)->set_active != NULL) {
		SCG_FOREACH_PANE (scg, pane, {
			SO_CLASS (so)->set_active (so,
				sheet_object_get_view (so, pane), TRUE);
		});
	}
#endif
	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_object_update_bbox (pane, so););
}

static void
cb_scg_object_unselect (SheetObject *so, double *coords, SheetControlGUI *scg)
{
	SCG_FOREACH_PANE (scg, pane, gnm_pane_object_unselect (pane, so););
	g_signal_handlers_disconnect_by_func (so,
		scg_mode_edit, scg);

#if 0
	if (SO_CLASS (old)->set_active != NULL)
		SCG_FOREACH_PANE (scg, pane, {
			SO_CLASS (old)->set_active (old,
				sheet_object_get_view (old, pane), FALSE);
		});
#endif
}

/**
 * scg_object_unselect :
 * @scg : #SheetControlGUI
 * @so : #SheetObject (optionally NULL)
 *
 * unselect the supplied object, and drop out of edit mode if this is the last
 * one.  If @so == NULL unselect _all_ objects.
 **/
void
scg_object_unselect (SheetControlGUI *scg, SheetObject *so)
{
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
	wb_control_update_action_sensitivity (scg_wbc (scg));
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
snap_pos_to_grid (ObjDragInfo const *info, gboolean is_col, double w_pos,
		  gboolean to_min)
{
	GnmPane const *pane = info->pane;
	Sheet const *sheet = scg_sheet (info->scg);
	int cell  = is_col ? pane->first.col        : pane->first.row;
	int pixel = is_col ? pane->first_offset.col : pane->first_offset.row;
	gboolean snap = FALSE;
	int length = 0;
	ColRowInfo const *cr_info;
	int sheet_max = colrow_max (is_col, sheet);
	int const sign = (is_col && sheet->text_is_rtl) ? -1 : 1;
	int pos = sign * (w_pos * FOO_CANVAS (pane)->pixels_per_unit + .5);

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
				pos = (abs (pos - pixel) < abs (pos - pixel - length)) ? pixel : pixel + length;
			else
				pos = (pixel == pos) ? pixel : (to_min ? pixel : pixel + length);
		}
	}
	return sign * pos / FOO_CANVAS (pane)->pixels_per_unit;
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
			x = snap_pos_to_grid (info, TRUE,  x,
			      scg_sheet (info->scg)->text_is_rtl
				      ? info->dx > 0. : info->dx < 0.);
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
		gboolean const rtl = scg_sheet (info->scg)->text_is_rtl;
		apply_move (so, rtl ? 2 : 0, 1, coords, info, info->snap_to_grid);
		apply_move (so, rtl ? 0 : 2, 3, coords, info, FALSE);
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
 * scg_objects_drag :
 * @scg : #SheetControlGUI
 * @primary : #SheetObject (optionally NULL)
 * @dx :
 * @dy :
 * @drag_type :
 * @symmetric :
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
	scg_object_coords_to_anchor (data->scg, coords, anchor);
	data->objects = g_slist_prepend (data->objects, so);
	data->anchors = g_slist_prepend (data->anchors, anchor);

	if (!sheet_object_rubber_band_directly (so)) {
		SCG_FOREACH_PANE (data->scg, pane, {
			FooCanvasItem **ctrl_pts = g_hash_table_lookup (pane->drag.ctrl_pts, so);
			if (NULL != ctrl_pts[9]) {
				double const *pts = g_hash_table_lookup (
					pane->simple.scg->selected_objects, so);
				SheetObjectView *sov = sheet_object_get_view (so,
					(SheetObjectViewContainer *)pane);

				gtk_object_destroy (GTK_OBJECT (ctrl_pts[9]));
				ctrl_pts[9] = NULL;

				if (NULL == sov)
					sov = sheet_object_new_view (so, (SheetObjectViewContainer *)pane);
				if (NULL != sov)
					sheet_object_view_set_bounds (sov, pts, TRUE);
			}
		});
	}
}

void
scg_objects_drag_commit (SheetControlGUI *scg, int drag_type,
			 gboolean created_objects)
{
	CollectObjectsData data;
	data.objects = data.anchors = NULL;
	data.scg = scg;
	g_hash_table_foreach (scg->selected_objects,
		(GHFunc) cb_collect_objects_to_commit, &data);
	cmd_objects_move (WORKBOOK_CONTROL (scg_wbcg (scg)),
		data.objects, data.anchors, created_objects,
		created_objects /* This is somewhat cheesy and should use ngettext */
			? ((drag_type == 8) ? _("Duplicate Object") : _("Insert Object"))
			: ((drag_type == 8) ? _("Move Object")	    : _("Resize Object")));
}

void
scg_objects_nudge (SheetControlGUI *scg, GnmPane *pane,
		   int drag_type, double dx, double dy, gboolean symmetric, gboolean snap_to_grid)
{
	/* no nudging if we are creating an object */
	if (!scg->new_object) {
		scg_objects_drag (scg, pane, NULL, &dx, &dy, drag_type, symmetric, snap_to_grid, FALSE);
		scg_objects_drag_commit (scg, drag_type, FALSE);
	}
}

void
scg_object_coords_to_anchor (SheetControlGUI const *scg,
			     double const *coords, SheetObjectAnchor *in_out)
{
	/* pane 0 always exists and the others are always use the same basis */
	GnmPane *pane = scg_pane ((SheetControlGUI *)scg, 0);
	double	tmp[4];
	int pixels[4];

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	g_return_if_fail (coords != NULL);

	in_out->base.direction = GOD_ANCHOR_DIR_NONE_MASK;
	if ((coords[0] > coords[2]) == (!scg_sheet (scg)->text_is_rtl)) {
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

	foo_canvas_w2c (FOO_CANVAS (pane), tmp[0], tmp[1],
		pixels + 0, pixels + 1);
	foo_canvas_w2c (FOO_CANVAS (pane),
		tmp[2], tmp[3],
		pixels + 2, pixels + 3);
	in_out->cell_bound.start.col = calc_obj_place (pane, pixels[0], TRUE,
		in_out->offset + 0);
	in_out->cell_bound.start.row = calc_obj_place (pane, pixels[1], FALSE,
		in_out->offset + 1);
	in_out->cell_bound.end.col = calc_obj_place (pane, pixels[2], TRUE,
		in_out->offset + 2);
	in_out->cell_bound.end.row = calc_obj_place (pane, pixels[3], FALSE,
		in_out->offset + 3);
}

static double
cell_offset_calc_pixel (Sheet const *sheet, int i, gboolean is_col, float offset)
{
	ColRowInfo const *cri = sheet_colrow_get_info (sheet, i, is_col);
	return offset * cri->size_pixels;
}

void
scg_object_anchor_to_coords (SheetControlGUI const *scg,
			     SheetObjectAnchor const *anchor, double *coords)
{
	/* pane 0 always exists and the others are always use the same basis */
	GnmPane *pane = scg_pane ((SheetControlGUI *)scg, 0);
	Sheet *sheet = scg_sheet (scg);
	GODrawingAnchorDir direction;
	double pixels[4], scale;
	GnmRange const *r;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	g_return_if_fail (anchor != NULL);
	g_return_if_fail (coords != NULL);

	r = &anchor->cell_bound;
	pixels[0] = scg_colrow_distance_get (scg, TRUE, 0,  r->start.col);
	pixels[2] = pixels[0] + scg_colrow_distance_get (scg, TRUE,
		r->start.col, r->end.col);
	pixels[1] = scg_colrow_distance_get (scg, FALSE, 0, r->start.row);
	pixels[3] = pixels[1] + scg_colrow_distance_get (scg, FALSE,
		r->start.row, r->end.row);
	pixels[0] += cell_offset_calc_pixel (sheet, r->start.col,
		TRUE, anchor->offset[0]);
	pixels[1] += cell_offset_calc_pixel (sheet, r->start.row,
		FALSE, anchor->offset[1]);
	pixels[2] += cell_offset_calc_pixel (sheet, r->end.col,
		TRUE, anchor->offset[2]);
	pixels[3] += cell_offset_calc_pixel (sheet, r->end.row,
		FALSE, anchor->offset[3]);

	direction = anchor->base.direction;
	if (direction == GOD_ANCHOR_DIR_UNKNOWN)
		direction = GOD_ANCHOR_DIR_DOWN_RIGHT;

	scale = 1. / FOO_CANVAS (pane)->pixels_per_unit;
	coords[0] = pixels[direction & GOD_ANCHOR_DIR_H_MASK  ? 0 : 2] * scale;
	coords[1] = pixels[direction & GOD_ANCHOR_DIR_V_MASK  ? 1 : 3] * scale;
	coords[2] = pixels[direction & GOD_ANCHOR_DIR_H_MASK  ? 2 : 0] * scale;
	coords[3] = pixels[direction & GOD_ANCHOR_DIR_V_MASK  ? 3 : 1] * scale;
	if (sheet->text_is_rtl) {
		double tmp = -coords[0];
		coords[0] = -coords[2];
		coords[2] = tmp;
	}
}

/***************************************************************************/

/**
 * scg_comment_display :
 * @scg : The SheetControl
 * @cc  : A cell comment
 *
 */
void
scg_comment_display (SheetControlGUI *scg, GnmComment *cc)
{
	int x, y;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	scg_comment_timer_clear (scg);

	/* If someone clicked and dragged the comment marker this may be NULL */
	if (scg->comment.selected == NULL)
		return;

	if (cc == NULL)
		cc = scg->comment.selected;
	else if (scg->comment.selected != cc)
		scg_comment_unselect (scg, scg->comment.selected);

	g_return_if_fail (IS_CELL_COMMENT (cc));

	if (scg->comment.item == NULL) {
		GtkWidget *text, *frame;
		GtkTextBuffer *buffer;
		GtkTextIter iter;

		scg->comment.item = gtk_window_new (GTK_WINDOW_POPUP);
		gdk_window_get_pointer (NULL, &x, &y, NULL);
		gtk_window_move (GTK_WINDOW (scg->comment.item), x+10, y+10);

		text = gtk_text_view_new ();
		gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text), GTK_WRAP_NONE);
		gtk_text_view_set_editable  (GTK_TEXT_VIEW (text), FALSE);
		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text));
		gtk_text_buffer_get_iter_at_offset (buffer, &iter, 0);

		if (cell_comment_author_get (cc) != NULL) {
			gtk_text_buffer_create_tag (buffer, "bold",
						    "weight", PANGO_WEIGHT_BOLD,
						    NULL);
			gtk_text_buffer_insert_with_tags_by_name (buffer, &iter,
				cell_comment_author_get (cc), -1,
				"bold", NULL);
			gtk_text_buffer_insert (buffer, &iter, "\n", 1);
		}

		if (cell_comment_text_get (cc) != NULL)
			gtk_text_buffer_insert (buffer, &iter,
				cell_comment_text_get (cc), -1);

		frame = gtk_frame_new (NULL);
		gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);

		gtk_container_add (GTK_CONTAINER (scg->comment.item), frame);
		gtk_container_add (GTK_CONTAINER (frame), text);
		gtk_widget_show_all (scg->comment.item);
	}
}

static gint
cb_cell_comment_timer (SheetControlGUI *scg)
{
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), FALSE);
	g_return_val_if_fail (scg->comment.timer != -1, FALSE);

	scg->comment.timer = -1;
	scg_comment_display (scg, scg->comment.selected);
	return FALSE;
}

/**
 * scg_comment_select :
 * @scg : The SheetControl
 * @cc  : A cell comment
 *
 * Prepare @cc for display.
 */
void
scg_comment_select (SheetControlGUI *scg, GnmComment *cc)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	g_return_if_fail (scg->comment.timer == -1);

	if (scg->comment.selected != NULL)
		scg_comment_unselect (scg, scg->comment.selected);

	scg->comment.selected = cc;
	scg->comment.timer = g_timeout_add (1000,
		(GSourceFunc)cb_cell_comment_timer, scg);
}

/**
 * scg_comment_unselect :
 * @scg : The SheetControl
 * @cc  : A cell comment
 *
 * If @cc is the current cell comment being edited/displayed shutdown the
 * display mechanism.
 */
void
scg_comment_unselect (SheetControlGUI *scg, GnmComment *cc)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (cc == scg->comment.selected) {
		scg->comment.selected = NULL;
		scg_comment_timer_clear (scg);

		if (scg->comment.item != NULL) {
			gtk_object_destroy (GTK_OBJECT (scg->comment.item));
			scg->comment.item = NULL;
		}
	}
}

/************************************************************************/
/* Col/Row size support routines.  */

int
scg_colrow_distance_get (SheetControlGUI const *scg, gboolean is_cols,
			 int from, int to)
{
	SheetControl *sc = (SheetControl *) scg;
	ColRowCollection const *collection;
	int default_size;
	int i, pixels = 0;
	int sign = 1;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), 1);

	if (from > to) {
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1;
	}

	g_return_val_if_fail (from >= 0, 1);

	if (is_cols) {
		g_return_val_if_fail (to <= gnm_sheet_get_max_cols (sc->sheet), 1);
		collection = &sc->sheet->cols;
	} else {
		g_return_val_if_fail (to <= gnm_sheet_get_max_rows (sc->sheet), 1);
		collection = &sc->sheet->rows;
	}

	/* Do not use col_row_foreach, it ignores empties.
	 * Optimize this so that long jumps are not quite so horrific
	 * for performance.
	 */
	default_size = collection->default_style.size_pixels;
	for (i = from ; i < to ; ++i) {
		ColRowSegment const *segment =
			COLROW_GET_SEGMENT(collection, i);

		if (segment != NULL) {
			ColRowInfo const *cri = segment->info[COLROW_SUB_INDEX (i)];
			if (cri == NULL)
				pixels += default_size;
			else if (cri->visible)
				pixels += cri->size_pixels;
		} else {
			int segment_end = COLROW_SEGMENT_END (i)+1;
			if (segment_end > to)
				segment_end = to;
			pixels += default_size * (segment_end - i);
			i = segment_end - 1;
		}
	}

	return pixels*sign;
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
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	SCG_FOREACH_PANE (scg, pane, gnm_pane_edit_start (pane););
}

void
scg_edit_stop (SheetControlGUI *scg)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

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

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

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
	gnm_sheet_merge_find_container (sheet, r);
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

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

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
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (!scg->rangesel.active)
		return;
	if (scg->wbcg->rangesel != scg)
		g_warning ("mis configed rangesel");

	scg->wbcg->rangesel = NULL;
	scg->rangesel.active = FALSE;
	SCG_FOREACH_PANE (scg, pane, gnm_pane_rangesel_stop (pane););

	gnm_expr_entry_rangesel_stop (wbcg_get_entry_logical (scg->wbcg),
		clear_string);
}

/**
 * scg_set_display_cursor :
 * @scg :
 *
 * Set the displayed cursor type.
 */
void
scg_set_display_cursor (SheetControlGUI *scg)
{
	GdkCursorType cursor = GDK_CURSOR_IS_PIXMAP;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (scg->new_object != NULL)
		cursor = GDK_CROSSHAIR;

	SCG_FOREACH_PANE (scg, pane, {
		GtkWidget *w = GTK_WIDGET (pane);
		if (w->window) {
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
		col = gnm_sheet_get_max_cols (((SheetControl*) scg)->sheet) - 1;
	} else
		base_col = scg->rangesel.base_corner.col;
	if (row < 0) {
		base_row = 0;
		row = gnm_sheet_get_max_rows (((SheetControl*) scg)->sheet) - 1;
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
 *
 * @scg    : The scg
 * @count  : Number of units to move the cursor vertically
 * @jump_to_bound: skip from the start to the end of ranges
 *                 of filled or unfilled cells.
 * @horiz  : is the movement horizontal or vertical
 *
 * Moves the cursor count rows
 */
void
scg_cursor_move (SheetControlGUI *scg, int n,
		 gboolean jump_to_bound, gboolean horiz)
{
	SheetView *sv = scg_view (scg);
	GnmCellPos tmp = sv->edit_pos_real;

	if (!wbcg_edit_finish (scg->wbcg, WBC_EDIT_ACCEPT, NULL))
		return;

	if (horiz)
		tmp.col = sheet_find_boundary_horizontal (sv->sheet,
			tmp.col, tmp.row, tmp.row,
			n, jump_to_bound);
	else
		tmp.row = sheet_find_boundary_vertical (sv->sheet,
			tmp.col, tmp.row, tmp.col,
			n, jump_to_bound);

	sv_selection_reset (sv);
	sv_cursor_set (sv, &tmp,
		       tmp.col, tmp.row, tmp.col, tmp.row, NULL);
	sv_make_cell_visible (sv, tmp.col, tmp.row, FALSE);
	sv_selection_add_pos (sv, tmp.col, tmp.row);
}

/**
 * scg_cursor_extend :
 * @sheet              : Sheet to operate in.
 * @n                  : Units to extend the selection
 * @jump_to_boundaries : Move to transitions between cells and blanks,
 *                       or move in single steps.
 * @horizontal         : extend vertically or horizontally.
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
	sv_make_cell_visible (sv, visible.col, visible.row, FALSE);
}

void
scg_take_focus (SheetControlGUI *scg)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	/* FIXME: Slightly hackish. */
	if (wbcg_toplevel (scg->wbcg))
		gtk_window_set_focus (wbcg_toplevel (scg->wbcg),
				      GTK_WIDGET (scg_pane (scg, 0)));
}

/*********************************************************************************/
void
scg_size_guide_start (SheetControlGUI *scg, gboolean vert, int colrow, int width)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_size_guide_start (pane, vert, colrow, width););
}
void
scg_size_guide_motion (SheetControlGUI *scg, gboolean vert, int guide_pos)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_size_guide_motion (pane, vert, guide_pos););
}
void
scg_size_guide_stop (SheetControlGUI *scg)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_size_guide_stop (pane););
}
/*********************************************************************************/

void
scg_special_cursor_start (SheetControlGUI *scg, int style, int button)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_special_cursor_start (pane, style, button););
}

void
scg_special_cursor_stop (SheetControlGUI *scg)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_special_cursor_stop (pane););
}

gboolean
scg_special_cursor_bound_set (SheetControlGUI *scg, GnmRange const *r)
{
	gboolean changed = FALSE;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), FALSE);

	SCG_FOREACH_PANE (scg, pane,
		changed |= gnm_pane_special_cursor_bound_set (pane, r););
	return changed;
}

static void
scg_object_create_view	(SheetControl *sc, SheetObject *so)
{
	SheetControlGUI *scg = SHEET_CONTROL_GUI (sc);
	SCG_FOREACH_PANE (scg, pane,
		sheet_object_new_view (so, (SheetObjectViewContainer *)pane););
}

static void
scg_scale_changed (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	double z;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	z = sc->sheet->last_zoom_factor_used;

	SCG_FOREACH_PANE (scg, pane, {
		if (pane->col.canvas != NULL)
			foo_canvas_set_pixels_per_unit (pane->col.canvas, z);
		if (pane->row.canvas != NULL)
			foo_canvas_set_pixels_per_unit (pane->row.canvas, z);
		foo_canvas_set_pixels_per_unit (FOO_CANVAS (pane), z);
	});

	scg_resize (scg, TRUE);
	set_resize_pane_pos (scg, scg->vpane);
	set_resize_pane_pos (scg, scg->hpane);
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
}

GSF_CLASS (SheetControlGUI, sheet_control_gui,
	   scg_class_init, scg_init, SHEET_CONTROL_TYPE)

static gint
cb_scg_queued_movement (SheetControlGUI *scg)
{
	Sheet const *sheet = scg_sheet (scg);
	scg->delayedMovement.timer = -1;
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
 * scg_queue_movement :
 *
 * @scg :
 * @handler :	The movement handler
 * @n :		how far
 * @jump :	TRUE jump to bound
 * @horiz :	TRUE move by cols
 *
 * Do motion compression when possible to avoid redrawing an area that will
 * disappear when we scroll again.
 **/
void
scg_queue_movement (SheetControlGUI	*scg,
		    SCGUIMoveFunc	 handler,
		    int n, gboolean jump, gboolean horiz)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	/* do we need to flush a pending movement */
	if (scg->delayedMovement.timer != -1) {
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
			scg->delayedMovement.timer = -1;
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

	soi = g_object_new (SHEET_OBJECT_IMAGE_TYPE, NULL);
	sheet_object_image_set_image (soi, "", (guint8 *)data, len, TRUE);

	so = SHEET_OBJECT (soi);
	sheet_object_set_anchor (so, anchor);
	sheet_object_set_sheet (so, scg_sheet (scg));
	scg_object_select (scg, so);
	sheet_object_default_size (so, &w, &h);
	scg_objects_drag (scg, NULL, NULL, &w, &h, 7, FALSE, FALSE, FALSE);
	scg_objects_drag_commit	(scg, 7, TRUE);
}

void
scg_paste_image (SheetControlGUI *scg, GnmRange *where,
		 guint8 const *data, unsigned len)
{
	SheetObjectAnchor anchor;

	sheet_object_anchor_init (&anchor, where, NULL,
		GOD_ANCHOR_DIR_DOWN_RIGHT);
	scg_image_create (scg, &anchor, data, len);
}

static void
scg_drag_receive_img_data (SheetControlGUI *scg, double x, double y,
			   guint8 const *data, unsigned len)
{
	double coords[4];
	SheetObjectAnchor anchor;

	sheet_object_anchor_init (&anchor, NULL, NULL,
		GOD_ANCHOR_DIR_DOWN_RIGHT);
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
	IOContext *ioc = gnumeric_io_context_new (GO_CMD_CONTEXT (scg->wbcg));

	if (input != NULL) {
		unsigned len = gsf_input_size (input);
		guint8 const *data = gsf_input_read (input, len, NULL);

		scg_drag_receive_img_data (scg, x, y, data, len);
		g_object_unref (input);
	} else
		go_cmd_context_error (GO_CMD_CONTEXT (ioc), err);

	if (gnumeric_io_error_occurred (ioc) ||
	    gnumeric_io_warning_occurred (ioc)) {
		gnumeric_io_error_display (ioc);
		gnumeric_io_error_clear (ioc);
	}
	g_object_unref (ioc);
}

static void
scg_drag_receive_spreadsheet (SheetControlGUI *scg, const gchar *uri)
{
	GError *err = NULL;
	GsfInput *input = go_file_open (uri, &err);
	IOContext *ioc = gnumeric_io_context_new (GO_CMD_CONTEXT (scg->wbcg));

	if (input != NULL) {
		WorkbookView *wbv;

		wbv = wb_view_new_from_input (input, NULL, ioc, NULL);
		if (wbv != NULL)
			gui_wb_view_show (scg->wbcg,
					  wbv);

	} else
		go_cmd_context_error (GO_CMD_CONTEXT (ioc), err);

	if (gnumeric_io_error_occurred (ioc) ||
	    gnumeric_io_warning_occurred (ioc)) {
		gnumeric_io_error_display (ioc);
		gnumeric_io_error_clear (ioc);
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
		GOD_ANCHOR_DIR_DOWN_RIGHT);
	coords[0] = coords[2] = x;
	coords[1] = coords[3] = y;
	scg_object_coords_to_anchor (scg, coords, &anchor);
	paste_target_init (&pt, sheet, &anchor.cell_bound, PASTE_ALL_TYPES);
	if (content && ((content->cols > 0 && content->rows > 0) ||
			content->objects != NULL))
		cmd_paste_copy (wbc, &pt, content);
}

static void
scg_drag_receive_cellregion (SheetControlGUI *scg, double x, double y,
			     const char *data, unsigned len)
{
	GnmCellRegion *content;

	content = xml_cellregion_read (scg_wbc (scg), scg_sheet (scg), data, len);
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
	go_slist_free_custom (urls, (GFreeFunc) g_free);
}

static void
scg_drag_receive_same_process (SheetControlGUI *scg, GtkWidget *source_widget,
			       double x, double y)
{
	SheetControlGUI *source_scg = NULL;
	GnmPane *pane;

	g_return_if_fail (source_widget != NULL);
	g_return_if_fail (IS_GNM_PANE (source_widget));

	pane = GNM_PANE (source_widget);
	source_scg = pane->simple.scg;
	if (source_scg == scg) {
		GdkWindow *window;
		GdkModifierType mask;
		int xp, yp;
		int xx = x, yy = y;
		int origin_x = 0, origin_y = 0;
		gboolean make_dup;

		window = gtk_widget_get_parent_window (GTK_WIDGET (pane));
		gdk_window_get_pointer (window, &xp, &yp, &mask);
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
		scg_objects_drag_commit	(scg, 8, FALSE);

		if (make_dup) {
			GSList *ptr, *objs = go_hash_keys (scg->selected_objects);

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
			scg_objects_nudge (scg, pane, 8, x - origin_x, y - origin_y, FALSE, FALSE);
		}
	} else {
		GnmCellRegion *content;
		GSList *objects;

		g_return_if_fail (IS_SHEET_CONTROL_GUI (source_scg));

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

#ifdef DEBUG_DND
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
#endif

void
scg_drag_data_received (SheetControlGUI *scg, GtkWidget *source_widget,
			double x, double y, GtkSelectionData *selection_data)
{
	gchar *target_type = gdk_atom_name (selection_data->target);
	const char *sel_data = (const char *)selection_data->data;
	gsize sel_len = selection_data->length;

	if (!strcmp (target_type, "text/uri-list")) {
		scg_drag_receive_uri_list (scg, x, y, sel_data, sel_len);

	} else if (!strncmp (target_type, "image/", 6)) {
		scg_drag_receive_img_data (scg, x, y, selection_data->data, sel_len);
	} else if (!strcmp (target_type, "GNUMERIC_SAME_PROC")) {
		scg_drag_receive_same_process (scg, source_widget, x, y);
	} else if (!strcmp (target_type, "application/x-gnumeric")) {
		scg_drag_receive_cellregion (scg, x, y, sel_data, sel_len);
#ifdef DEBUG_DND
	} else if (!strcmp (target_type, "x-special/gnome-copied-files")) {
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
#endif
	} else
		g_warning ("Unknown target type '%s'!", target_type);

	g_free (target_type);
}

static void
scg_drag_send_image (SheetControlGUI *scg,
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
		if (IS_SHEET_OBJECT_IMAGEABLE (SHEET_OBJECT (ptr->data))) {
			so = SHEET_OBJECT (ptr->data);
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
		(selection_data, selection_data->target,
		 8, gsf_output_memory_get_bytes (omem), osize);
	gsf_output_close (output);
	g_object_unref (output);
	g_free (format);
}

static void
scg_drag_send_graph (SheetControlGUI *scg,
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
		if (IS_SHEET_OBJECT_EXPORTABLE (SHEET_OBJECT (ptr->data))) {
			so = SHEET_OBJECT (ptr->data);
			break;
		}

	if (so == NULL) {
		g_warning ("non exportable object requested\n");
		return;
	}

	output = gsf_output_memory_new ();
	omem = GSF_OUTPUT_MEMORY (output);
	sheet_object_write_object (so, mime_type, output, NULL);
	osize = gsf_output_size (output);

	gtk_selection_data_set
		(selection_data, selection_data->target,
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
	gtk_selection_data_set (selection_data, selection_data->target, 8,
		gsf_output_memory_get_bytes (output),
		gsf_output_size (GSF_OUTPUT (output)));
	g_object_unref (output);
	cellregion_unref (content);
}

void
scg_drag_data_get (SheetControlGUI *scg, GtkSelectionData *selection_data)
{
	gchar *target_name = gdk_atom_name (selection_data->target);
	GSList *objects = go_hash_keys (scg->selected_objects);

	if (strcmp (target_name, "GNUMERIC_SAME_PROC") == 0)
		/* Set dummy selection for process internal dnd */
		gtk_selection_data_set (selection_data, selection_data->target,
			8, (const guint8 *)"", 1);
	else if (strcmp (target_name, "application/x-gnumeric") == 0)
		scg_drag_send_clipboard_objects (SHEET_CONTROL (scg),
			selection_data, objects);
	else if (strcmp (target_name, "application/x-goffice-graph") == 0)
		scg_drag_send_graph (scg, selection_data, objects, target_name);
	else if (strncmp (target_name, "image/", 6) == 0)
		scg_drag_send_image (scg, selection_data, objects, target_name);

	g_free (target_name);
	g_slist_free (objects);
}

void
scg_delete_sheet_if_possible (SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = sc->sheet;
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

