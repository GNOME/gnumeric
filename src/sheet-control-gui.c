/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * sheet-control-gui.c: Implements a graphic control for a sheet.
 *
 * Author:
 *    Miguel de Icaza (miguel@kernel.org)
 *    Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "sheet-control-gui-priv.h"

#include "sheet.h"
#include "sheet-private.h"
#include "sheet-merge.h"
#include "workbook.h"
#include "workbook-view.h"
#include "workbook-edit.h"
#include "workbook-cmd-format.h"
#include "workbook-control-gui-priv.h"
#include "cell.h"
#include "selection.h"
#include "style.h"
#include "sheet-object-impl.h"
#include "sheet-object-cell-comment.h"
#include "gui-util.h"
#include "parse-util.h"
#include "selection.h"
#include "application.h"
#include "cellspan.h"
#include "cmd-edit.h"
#include "commands.h"
#include "clipboard.h"
#include "dialogs.h"
#include "sheet-merge.h"
#include "ranges.h"

#include "gnumeric-canvas.h"
#include "item-bar.h"
#include "item-cursor.h"
#include "widgets/gnumeric-expr-entry.h"

#include <libgnome/gnome-i18n.h>
#include <gdk/gdkkeysyms.h>
#include <gal/util/e-util.h>
#include <gal/widgets/e-colors.h>
#include <gal/widgets/e-cursors.h>

#include <string.h>

static SheetControlClass *scg_parent_class;

static void scg_ant                    (SheetControl *sc);
static void scg_unant                  (SheetControl *sc);

GnumericCanvas *
scg_pane (SheetControlGUI *scg, int p)
{
	/* it is ok to request a pane when we are not frozen */
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);
	g_return_val_if_fail (p >= 0, NULL);
	g_return_val_if_fail (p < 4, NULL);

	return scg->pane[p].gcanvas;
}

WorkbookControlGUI *
scg_get_wbcg (SheetControlGUI const *scg)
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
		gnome_canvas_request_redraw (
			GNOME_CANVAS (pane->gcanvas),
			0, 0, INT_MAX, INT_MAX);
		if (headers) {
			if (NULL != pane->col.canvas)
				gnome_canvas_request_redraw (
					pane->col.canvas,
					0, 0, INT_MAX, INT_MAX);
			if (NULL != pane->row.canvas)
				gnome_canvas_request_redraw (
					pane->row.canvas,
					0, 0, INT_MAX, INT_MAX);
		}
	});
}

static void
scg_redraw_region (SheetControl *sc,
		   int start_col, int start_row,
		   int end_col, int end_row)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;

	SCG_FOREACH_PANE (scg, pane,
		gnm_canvas_redraw_region (pane->gcanvas,
			start_col, start_row, end_col, end_row););
}

/* A rough guess of the trade off point between of redrawing all
 * and calculating the redraw size
 */
#define COL_HEURISTIC	20
#define ROW_HEURISTIC	50

static void
scg_redraw_headers (SheetControl *sc,
		    gboolean const col, gboolean const row,
		    Range const * r /* optional == NULL */)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	GnumericPane *pane;
 	GnumericCanvas *gcanvas;
	int i;

	for (i = scg->active_panes; i-- > 0 ; ) {
		pane = scg->pane + i;
		gcanvas = pane->gcanvas;

		if (col && pane->col.canvas != NULL) {
			int left = 0, right = INT_MAX-1;
			if (r != NULL) {
				int const size = r->end.col - r->start.col;
				if (-COL_HEURISTIC < size && size < COL_HEURISTIC) {
					left = gcanvas->first_offset.col +
						scg_colrow_distance_get (scg, TRUE,
									 gcanvas->first.col, r->start.col);
					right = left +
						scg_colrow_distance_get (scg, TRUE,
									 r->start.col, r->end.col+1);
				}
			}
			/* Request excludes the far coordinate.  Add 1 to include them */
			gnome_canvas_request_redraw (GNOME_CANVAS (pane->col.canvas),
				left, 0, right+1, INT_MAX);
		}

		if (row && pane->row.canvas != NULL) {
			int top = 0, bottom = INT_MAX-1;
			if (r != NULL) {
				int const size = r->end.row - r->start.row;
				if (-ROW_HEURISTIC < size && size < ROW_HEURISTIC) {
					top = gcanvas->first_offset.row +
						scg_colrow_distance_get (scg, FALSE,
									 gcanvas->first.row, r->start.row);
					bottom = top +
						scg_colrow_distance_get (scg, FALSE,
									 r->start.row, r->end.row+1);
				}
			}
			/* Request excludes the far coordinate.  Add 1 to include them */
			gnome_canvas_request_redraw (GNOME_CANVAS (pane->row.canvas),
				0, top, INT_MAX, bottom+1);
		}
	}
}

static void
scg_setup_group_buttons (SheetControlGUI *scg, unsigned max_outline,
			 ItemBar const *ib, int w, int h,
			 GPtrArray *btns, GtkWidget *box)
{
	GtkStyle *style;
	unsigned i;

	if (max_outline > 0)
		max_outline++;

	while (btns->len > max_outline)
		gtk_container_remove (GTK_CONTAINER (box),
			g_ptr_array_remove_index_fast (btns, btns->len - 1));

	while (btns->len < max_outline) {
		GtkWidget *out = gtk_alignment_new (.5, .5, 1., 1.);
		GtkWidget *in  = gtk_alignment_new (.5, .5, 0., 0.);
		GtkWidget *btn = gtk_button_new ();
		char *tmp = g_strdup_printf ("%d", btns->len+1);
		gtk_container_add (GTK_CONTAINER (in), gtk_label_new (tmp));
		gtk_container_add (GTK_CONTAINER (btn), in);
		gtk_container_add (GTK_CONTAINER (out), btn);
		gtk_box_pack_end (GTK_BOX (box), out, TRUE, TRUE, 0);
		g_ptr_array_add (btns, out);
		g_free (tmp);
	}

	{
		StyleFont const *sf = item_bar_normal_font (ib);
		style = gtk_style_new ();
		if (style->font_desc)
			pango_font_description_free (style->font_desc);
		style->font_desc = pango_font_description_copy (
			pango_context_get_font_description (sf->pango.context));
	}

	/* size all of the button so things work after a zoom */
	for (i = 0 ; i < btns->len ; i++) {
		GtkWidget *btn = g_ptr_array_index (btns, i);
		GtkWidget *label = GTK_BIN (GTK_BIN (GTK_BIN (btn)->child)->child)->child;
		gtk_widget_set_usize (GTK_WIDGET (btn), w, h);
		gtk_widget_set_style (label, style);
	}

	gtk_style_unref (style);
	gtk_widget_show_all (box);
}

static void
scg_resize (SheetControl *sc, gboolean force_scroll)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	Sheet *sheet;
	GnumericCanvas *gcanvas;
	int h, w, btn_h, btn_w, tmp;
	double zoom;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	sheet = sc->sheet;
	zoom = sheet->last_zoom_factor_used;

	/* Recalibrate the starting offsets */
	gcanvas = scg_pane (scg, 0);
	gcanvas->first_offset.col = scg_colrow_distance_get (scg,
		TRUE, 0, gcanvas->first.col);
	gcanvas->first_offset.row = scg_colrow_distance_get (scg,
		FALSE, 0, gcanvas->first.row);

	/* resize Pane[0] headers */
	h = item_bar_calc_size (scg->pane[0].col.item);
	btn_h = h - item_bar_indent (scg->pane[0].col.item);
	gtk_widget_set_usize (GTK_WIDGET (scg->pane[0].col.canvas), -1, h);
	w = item_bar_calc_size (scg->pane[0].row.item);
	btn_w = w - item_bar_indent (scg->pane[0].row.item);
	gtk_widget_set_usize (GTK_WIDGET (scg->pane[0].row.canvas), w, -1);

	gtk_widget_set_usize (scg->select_all_btn, btn_w, btn_h);

	tmp = item_bar_group_size (scg->pane[0].col.item,
		sheet->cols.max_outline_level);
	scg_setup_group_buttons (scg, sheet->cols.max_outline_level,
		scg->pane[0].col.item,
		tmp, tmp, scg->col_group.buttons, scg->col_group.button_box);
	scg_setup_group_buttons (scg, sheet->rows.max_outline_level,
		scg->pane[0].row.item,
		-1, btn_h, scg->row_group.buttons, scg->row_group.button_box);

	if (scg->active_panes == 1) {
		gnome_canvas_set_scroll_region (scg->pane[0].col.canvas,
			0, 0, GNUMERIC_CANVAS_FACTOR_X / zoom, h / zoom);
		gnome_canvas_set_scroll_region (scg->pane[0].row.canvas,
			0, 0, w / zoom, GNUMERIC_CANVAS_FACTOR_Y / zoom);
	} else {
		CellPos const *tl = &sheet->frozen_top_left;
		CellPos const *br = &sheet->unfrozen_top_left;
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
			GnumericPane const *p = scg->pane + i;
			p->gcanvas->first_offset.col = scg_colrow_distance_get (
				scg, TRUE, 0, p->gcanvas->first.col);
			p->gcanvas->first_offset.row = scg_colrow_distance_get (
				scg, FALSE, 0, p->gcanvas->first.row);
		}

		gtk_widget_set_usize (GTK_WIDGET (scg->pane[1].gcanvas), r - l, -1);
		gtk_widget_set_usize (GTK_WIDGET (scg->pane[2].gcanvas), r - l, b - t);
		gtk_widget_set_usize (GTK_WIDGET (scg->pane[3].gcanvas), -1,    b - t);

		/* The item_bar_calcs should be equal */
		/* FIXME : The canvas gets confused when the initial scroll
		 * region is set too early in its life cycle.
		 * It likes it to be at the origin, we can live with that for now.
		 * However, we really should track the bug eventually.
		 */
		h = item_bar_calc_size (scg->pane[2].col.item);
		gtk_widget_set_usize (GTK_WIDGET (scg->pane[2].col.canvas), r - l, h+1);
		gnome_canvas_set_scroll_region (scg->pane[2].col.canvas,
			0, 0, GNUMERIC_CANVAS_FACTOR_X / zoom, h / zoom);
			/* l / zoom, 0, r / zoom, h / zoom); */
		gnome_canvas_set_scroll_region (scg->pane[0].col.canvas,
			0, 0, GNUMERIC_CANVAS_FACTOR_X / zoom, h / zoom);
			/* r / zoom, 0, GNUMERIC_CANVAS_FACTOR_X / zoom, h / zoom); */

		/* The item_bar_calcs should be equal */
		w = item_bar_calc_size (scg->pane[2].row.item);
		gtk_widget_set_usize (GTK_WIDGET (scg->pane[2].row.canvas), w+1, b - t);
		gnome_canvas_set_scroll_region (scg->pane[2].row.canvas,
			0, 0, w / zoom, GNUMERIC_CANVAS_FACTOR_Y / zoom);
			/* 0, t / zoom, w / zoom, b / zoom); */
		gnome_canvas_set_scroll_region (scg->pane[0].row.canvas,
			0, 0, w / zoom, GNUMERIC_CANVAS_FACTOR_Y / zoom);
			/* 0, b / zoom, w / zoom, GNUMERIC_CANVAS_FACTOR_Y / zoom); */
	}

	SCG_FOREACH_PANE (scg, pane, gnm_pane_reposition_cursors (pane););
}

void
scg_set_zoom_factor (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	double z;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	/* TODO : move this to sheetView when we create one */
	z = sc->sheet->last_zoom_factor_used;

	/* Set pixels_per_unit before the font.  The item bars look here for the number */
	SCG_FOREACH_PANE (scg, pane, {
		if (pane->col.canvas != NULL)
			gnome_canvas_set_pixels_per_unit (pane->col.canvas, z);
		if (pane->row.canvas != NULL)
			gnome_canvas_set_pixels_per_unit (pane->row.canvas, z);
		gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (pane->gcanvas), z);
	});

	scg_resize (sc, TRUE);
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
void
scg_scrollbar_config (SheetControl const *sc)
{
	SheetControlGUI *scg = SHEET_CONTROL_GUI (sc);
	GtkAdjustment *va = GTK_ADJUSTMENT (scg->va);
	GtkAdjustment *ha = GTK_ADJUSTMENT (scg->ha);
	GnumericCanvas *gcanvas = scg_pane (scg, 0);
	Sheet         *sheet = sc->sheet;
	int const last_col = gcanvas->last_full.col;
	int const last_row = gcanvas->last_full.row;
	int max_col = last_col;
	int max_row = last_row;

	if (sheet_is_frozen (sheet)) {
		ha->lower = sheet->unfrozen_top_left.col;
		va->lower = sheet->unfrozen_top_left.row;
	} else
		ha->lower = va->lower = 0;

	if (max_row < sheet->rows.max_used)
		max_row = sheet->rows.max_used;
	if (max_row < sheet->max_object_extent.row)
		max_row = sheet->max_object_extent.row;
	va->upper = max_row + 1;
	va->value = gcanvas->first.row;
	va->page_size = last_row - gcanvas->first.row + 1;
	va->page_increment = MAX (va->page_size - 3.0, 1.0);
	va->step_increment = 1;

	if (max_col < sheet->cols.max_used)
		max_col = sheet->cols.max_used;
	if (max_col < sheet->max_object_extent.col)
		max_col = sheet->max_object_extent.col;
	ha->upper = max_col + 1;
	ha->page_size = last_col - gcanvas->first.col + 1;
	ha->value = gcanvas->first.col;
	ha->page_increment = MAX (ha->page_size - 3.0, 1.0);
	ha->step_increment = 1;

	gtk_adjustment_changed (va);
	gtk_adjustment_changed (ha);
}

void
scg_colrow_size_set (SheetControlGUI *scg,
		     gboolean is_cols, int index, int new_size_pixels)
{
	SheetControl *sc = (SheetControl *) scg;
	WorkbookControl *wbc = sc->wbc;
	Sheet *sheet = sc->sheet;

	/* If all cols/rows in the selection are completely selected
	 * then resize all of them, otherwise just resize the selected col/row.
	 */
	if (!sheet_selection_full_cols_rows (sheet, is_cols, index)) {
		ColRowIndexList *s = colrow_get_index_list (index, index, NULL);
		cmd_resize_colrow (wbc, sheet, is_cols, s, new_size_pixels);
	} else
		workbook_cmd_resize_selected_colrow (wbc, is_cols,
						     sheet, new_size_pixels);
}

void
scg_select_all (SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = sc->sheet;
	gboolean const rangesel = wbcg_rangesel_possible (scg->wbcg);

	if (rangesel) {
		scg_rangesel_bound (scg,
			0, 0, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	} else if (!wbcg_edit_has_guru (scg->wbcg)) {
		scg_mode_edit (SHEET_CONTROL (sc));
		wbcg_edit_finish (scg->wbcg, FALSE);
		sheet_selection_reset (sheet);
		sheet_selection_add_range (sheet, 0, 0, 0, 0,
			SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	}
	sheet_update (sheet);
}

void
scg_colrow_select (SheetControlGUI *scg, gboolean is_cols,
		   int index, int modifiers)
{
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = sc->sheet;
	gboolean const rangesel = wbcg_rangesel_possible (scg->wbcg);

	if (!rangesel)
		if (!wbcg_edit_finish (scg->wbcg, TRUE))
			return;

	if (modifiers & GDK_SHIFT_MASK) {
		if (rangesel) {
			if (is_cols)
				scg_rangesel_extend_to (scg, index, -1);
			else
				scg_rangesel_extend_to (scg, -1, index);
		} else {
			if (is_cols)
				sheet_selection_extend_to (sheet, index, -1);
			else
				sheet_selection_extend_to (sheet, -1, index);
		}
	} else {
		if (!rangesel && !(modifiers & GDK_CONTROL_MASK))
			sheet_selection_reset (sheet);

		if (rangesel) {
			if (is_cols)
				scg_rangesel_bound (scg,
					index, 0, index, SHEET_MAX_ROWS-1);
			else
				scg_rangesel_bound (scg,
					0, index, SHEET_MAX_COLS-1, index);
		} else if (is_cols) {
			GnumericCanvas *gcanvas =
				scg_pane (scg, scg->active_panes > 1 ? 3 : 0);
			sheet_selection_add_range (sheet,
				index, gcanvas->first.row,
				index, 0,
				index, SHEET_MAX_ROWS-1);
		} else {
			GnumericCanvas *gcanvas =
				scg_pane (scg, scg->active_panes > 1 ? 1 : 0);
			sheet_selection_add_range (sheet,
				gcanvas->first.col, index,
				0, index,
				SHEET_MAX_COLS-1, index);
		}
	}

	/* The edit pos, and the selection may have changed */
	sheet_update (sheet);
}

/***************************************************************************/

static void
cb_select_all (GtkWidget *the_button, SheetControlGUI *scg)
{
	scg_select_all (scg);
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
cb_hscrollbar_adjust_bounds (GtkRange *range, gdouble new_value)
{
	gdouble limit = range->adjustment->upper - range->adjustment->page_size;
	if (range->adjustment->upper < SHEET_MAX_COLS && new_value > limit) {
		range->adjustment->upper = new_value + range->adjustment->page_size;
		if (range->adjustment->upper > SHEET_MAX_COLS)
			range->adjustment->upper = SHEET_MAX_COLS;
		gtk_adjustment_changed (range->adjustment);
	}
}
static void
cb_vscrollbar_adjust_bounds (GtkRange *range, gdouble new_value)
{
	gdouble limit = range->adjustment->upper - range->adjustment->page_size;
	if (range->adjustment->upper < SHEET_MAX_ROWS && new_value > limit) {
		range->adjustment->upper = new_value + range->adjustment->page_size;
		if (range->adjustment->upper > SHEET_MAX_ROWS)
			range->adjustment->upper = SHEET_MAX_ROWS;
		gtk_adjustment_changed (range->adjustment);
	}
}

#if 0
/*
 * scg_make_edit_pos_visible
 * @scg  Sheet view
 *
 * Make the cell at the edit position visible.
 *
 * To be called from the "size_allocate" signal handler when the geometry of a
 * new sheet view has been configured.
 */
static void
scg_make_edit_pos_visible (SheetControl *sc)
{
	scg_make_cell_visible (sc,
			       scg->sheet->edit_pos.col,
			       scg->sheet->edit_pos.row,
			       TRUE);

}
#endif

static void
cb_table_size_allocate (GtkWidget *widget, GtkAllocation *alloc,
			SheetControlGUI *scg)
{
#if 0
	/* FIXME
	 * When a new sheet is added this is called and if the edit cell was
	 * not visible we change the scroll position even though to the user
	 * the size did not change and there is no reason for the scrolling to
	 * jump.
	 *
	 * Can we somehow do this only if the edit pos was visible initially ?
	 */
	scg_make_edit_pos_visible ((SheetControl *) scg);
#endif
	scg_scrollbar_config ((SheetControl *) scg);
}

static void
cb_table_destroy (GtkObject *table, SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;

	scg_mode_edit (sc);	/* finish any object edits */
	scg_unant (sc);		/* Make sure that everything is unanted */

 	if (scg->wbcg) {
		GtkWindow *toplevel = wbcg_toplevel (scg->wbcg);

		/* Only pane-0 ever gets focus */
		if (NULL != toplevel &&
		    toplevel->focus_widget == GTK_WIDGET (scg_pane (scg, 0)))
			gtk_window_set_focus (toplevel, NULL);
	}

	scg->table = NULL;
	SCG_FOREACH_PANE (scg, pane, pane->gcanvas = NULL;);

	g_object_unref (G_OBJECT (scg));
}

static void
scg_init (SheetControlGUI *scg)
{
	((SheetControl *) scg)->sheet = NULL;

	scg->comment.selected = NULL;
	scg->comment.item = NULL;
	scg->comment.timer = -1;

	scg->grab_stack = 0;
	scg->new_object = NULL;
	scg->current_object = NULL;
}

/*************************************************************************/

/**
 * gnm_canvas_update_inital_top_left :
 * A convenience routine to store the new topleft back in the view.
 * FIXME : for now we don't have a sheetView so we endup just storing it in the
 * Sheet.  Having this operation wrapped nicely here will make it easy to fix
 * later.
 */
static void
gnm_canvas_update_inital_top_left (GnumericCanvas const *gcanvas)
{
	/* FIXME : we need SheetView */
	if (gcanvas->pane->index == 0) {
		Sheet *sheet = gcanvas->simple.scg->sheet_control.sheet;
		sheet->initial_top_left.col = gcanvas->first.col;
		sheet->initial_top_left.row = gcanvas->first.row;
	}
}

static int
bar_set_left_col (GnumericCanvas *gcanvas, int new_first_col)
{
	GnomeCanvas *colc;
	int col_offset;

	g_return_val_if_fail (0 <= new_first_col && new_first_col < SHEET_MAX_COLS, 0);

	col_offset = gcanvas->first_offset.col +=
		scg_colrow_distance_get (gcanvas->simple.scg, TRUE, gcanvas->first.col, new_first_col);
	gcanvas->first.col = new_first_col;

	/* Scroll the column headers */
	if (NULL != (colc = gcanvas->pane->col.canvas))
		gnome_canvas_scroll_to (colc, col_offset, gcanvas->first_offset.row);

	return col_offset;
}

static void
gnm_canvas_set_left_col (GnumericCanvas *gcanvas, int new_first_col)
{
	g_return_if_fail (gcanvas != NULL);
	g_return_if_fail (0 <= new_first_col && new_first_col < SHEET_MAX_COLS);

	if (gcanvas->first.col != new_first_col) {
		GnomeCanvas * const canvas = GNOME_CANVAS(gcanvas);
		int const col_offset = bar_set_left_col (gcanvas, new_first_col);

		gnm_canvas_compute_visible_region (gcanvas, FALSE);
		gnome_canvas_scroll_to (canvas, col_offset, gcanvas->first_offset.row);
		gnm_canvas_update_inital_top_left (gcanvas);
	}
}

void
scg_set_left_col (SheetControlGUI *scg, int col)
{
	Sheet const *sheet;
	Range const *bound;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	sheet = scg->sheet_control.sheet;
	bound = &sheet->priv->unhidden_region;
	if (col < bound->start.col)
		col = bound->start.col;
	else if (col > bound->end.col)
		col = bound->end.col;

	if (scg->active_panes > 1) {
		int right = sheet->unfrozen_top_left.col;
		if (col < right)
			col = right;
		gnm_canvas_set_left_col (scg_pane (scg, 3), col);
	}
	gnm_canvas_set_left_col (scg_pane (scg, 0), col);
}

static int
bar_set_top_row (GnumericCanvas *gcanvas, int new_first_row)
{
	GnomeCanvas *rowc;
	int row_offset;

	g_return_val_if_fail (0 <= new_first_row && new_first_row < SHEET_MAX_ROWS, 0);

	row_offset = gcanvas->first_offset.row +=
		scg_colrow_distance_get (gcanvas->simple.scg, FALSE, gcanvas->first.row, new_first_row);
	gcanvas->first.row = new_first_row;

	/* Scroll the row headers */
	if (NULL != (rowc = gcanvas->pane->row.canvas))
		gnome_canvas_scroll_to (rowc, gcanvas->first_offset.col, row_offset);

	return row_offset;
}

static void
gnm_canvas_set_top_row (GnumericCanvas *gcanvas, int new_first_row)
{
	g_return_if_fail (gcanvas != NULL);
	g_return_if_fail (0 <= new_first_row && new_first_row < SHEET_MAX_ROWS);

	if (gcanvas->first.row != new_first_row) {
		GnomeCanvas * const canvas = GNOME_CANVAS(gcanvas);
		int const row_offset = bar_set_top_row (gcanvas, new_first_row);

		gnm_canvas_compute_visible_region (gcanvas, FALSE);
		gnome_canvas_scroll_to (canvas, gcanvas->first_offset.col, row_offset);
		gnm_canvas_update_inital_top_left (gcanvas);
	}
}

void
scg_set_top_row (SheetControlGUI *scg, int row)
{
	Sheet const *sheet;
	Range const *bound;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	sheet = scg->sheet_control.sheet;
	bound = &sheet->priv->unhidden_region;
	if (row < bound->start.row)
		row = bound->start.row;
	else if (row > bound->end.row)
		row = bound->end.row;

	if (scg->active_panes > 1) {
		int bottom = sheet->unfrozen_top_left.row;
		if (row < bottom)
			row = bottom;
		gnm_canvas_set_top_row (scg_pane (scg, 1), row);
	}
	gnm_canvas_set_top_row (scg_pane (scg, 0), row);
}

static void
gnm_canvas_set_top_left (GnumericCanvas *gcanvas,
			 int col, int row, gboolean force_scroll)
{
	gboolean changed = FALSE;
	int col_offset, row_offset;

	if (gcanvas->first.col != col || force_scroll) {
		if (force_scroll) {
			/* Clear the offsets in case col/row size changed */
			gcanvas->first_offset.col = 0;
			gcanvas->first.col = 0;
		}
		col_offset = bar_set_left_col (gcanvas, col);
		changed = TRUE;
	} else
		col_offset = gcanvas->first_offset.col;

	if (gcanvas->first.row != row || force_scroll) {
		if (force_scroll) {
			/* Clear the offsets in case col/row size changed */
			gcanvas->first_offset.row = 0;
			gcanvas->first.row = 0;
		}
		row_offset = bar_set_top_row (gcanvas, row);
		changed = TRUE;
	} else
		row_offset = gcanvas->first_offset.row;

	if (!changed)
		return;

	gnm_canvas_compute_visible_region (gcanvas, force_scroll);
	gnome_canvas_scroll_to (GNOME_CANVAS (gcanvas), col_offset, row_offset);
	gnm_canvas_update_inital_top_left (gcanvas);
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
gnm_canvas_make_cell_visible (GnumericCanvas *gcanvas, int col, int row,
			      gboolean const force_scroll)
{
	GnomeCanvas *canvas;
	Sheet *sheet;
	int   new_first_col, new_first_row;

	g_return_if_fail (IS_GNUMERIC_CANVAS (gcanvas));

	/* Avoid calling this before the canvas is realized: We do not know the
	 * visible area, and would unconditionally scroll the cell to the top
	 * left of the viewport.
	 */
	if (!GTK_WIDGET_REALIZED (gcanvas))
		return;

	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);
	g_return_if_fail (col < SHEET_MAX_COLS);
	g_return_if_fail (row < SHEET_MAX_ROWS);

	sheet = ((SheetControl *) gcanvas->simple.scg)->sheet;
	canvas = GNOME_CANVAS (gcanvas);

	/* Find the new gcanvas->first.col */
	if (col < gcanvas->first.col) {
		new_first_col = col;
	} else if (col > gcanvas->last_full.col) {
		int width = GTK_WIDGET (canvas)->allocation.width;
		int first_col = (gcanvas->last_visible.col == gcanvas->first.col)
			? gcanvas->first.col : col;

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
		new_first_col = gcanvas->first.col;

	/* Find the new gcanvas->first.row */
	if (row < gcanvas->first.row) {
		new_first_row = row;
	} else if (row > gcanvas->last_full.row) {
		int height = GTK_WIDGET (canvas)->allocation.height;
		int first_row = (gcanvas->last_visible.row == gcanvas->first.row)
			? gcanvas->first.row : row;

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
		new_first_row = gcanvas->first.row;

	gnm_canvas_set_top_left (gcanvas, new_first_col, new_first_row,
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
	Sheet const *sheet;
	CellPos const *tl, *br;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	sheet = ((SheetControl *) scg)->sheet;

	if (scg->active_panes == 1) {
		gnm_canvas_make_cell_visible (scg_pane (scg, 0),
			col, row, force_scroll);
		return;
	}

	tl = &sheet->frozen_top_left;
	br = &sheet->unfrozen_top_left;
	if (col < br->col) {
		if (row >= br->row) {	/* pane 1 */
			if (col < tl->col)
				col = tl->col;
			gnm_canvas_make_cell_visible (scg->pane[1].gcanvas,
				col, row, force_scroll);
			gnm_canvas_set_top_left (scg->pane[0].gcanvas,
				     couple_panes
				     ? br->col
				     : scg->pane[0].gcanvas->first.col,
				     scg->pane[1].gcanvas->first.row,
				     force_scroll);
			if (couple_panes)
				gnm_canvas_set_left_col (scg->pane[3].gcanvas, br->col);
		} else if (couple_panes) { /* pane 2 */
			/* FIXME : We may need to change the way this routine
			 * is used to fix this.  Because we only know what the
			 * target cell is we cannot absolutely differentiate
			 * between col & row scrolling.  For now use the
			 * heuristic that if the col was visible this is a
			 * vertical jump.
			 */
			if (scg->pane[2].gcanvas->first.col <= col &&
			    scg->pane[2].gcanvas->last_visible.col >= col) {
				scg_set_top_row (scg, row);
			} else
				scg_set_left_col (scg, col);
		}
	} else if (row < br->row) {	/* pane 3 */
		if (row < tl->row)
			row = tl->row;
		gnm_canvas_make_cell_visible (scg->pane[3].gcanvas,
			col, row, force_scroll);
		gnm_canvas_set_top_left (scg->pane[0].gcanvas,
			scg->pane[3].gcanvas->first.col,
			couple_panes
			? br->row
			: scg->pane[0].gcanvas->first.row,
			force_scroll);
		if (couple_panes)
			gnm_canvas_set_top_row (scg->pane[1].gcanvas,
				br->row);
	} else {			 /* pane 0 */
		gnm_canvas_make_cell_visible (scg->pane[0].gcanvas,
			col, row, force_scroll);
		gnm_canvas_set_top_left (scg->pane[1].gcanvas,
			tl->col, scg->pane[0].gcanvas->first.row, force_scroll);
		gnm_canvas_set_top_left (scg->pane[3].gcanvas,
			scg->pane[0].gcanvas->first.col, tl->row, force_scroll);
	}
	gnm_canvas_set_top_left (scg->pane[2].gcanvas,
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
	gboolean const being_frozen = sheet_is_frozen (sc->sheet);
	gboolean const was_frozen = scg->pane[2].gcanvas != NULL;

	if (!being_frozen && !was_frozen)
		return;

	/* TODO : support just h or v split */
	if (being_frozen) {
		CellPos const *tl = &sc->sheet->frozen_top_left;
		CellPos const *br = &sc->sheet->unfrozen_top_left;

		gnm_pane_init (scg->pane + 1, scg, FALSE, 1);
		gnm_pane_init (scg->pane + 2, scg, TRUE,  2);
		gnm_pane_init (scg->pane + 3, scg, FALSE, 3);
		scg->active_panes = 4;
		gnm_pane_bound_set (scg->pane + 0,
			br->col, br->row,
			SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
		gnm_pane_bound_set (scg->pane + 1,
			tl->col, br->row, br->col-1, SHEET_MAX_ROWS-1);
		gnm_pane_bound_set (scg->pane + 2,
			tl->col, tl->row, br->col-1, br->row-1);
		gnm_pane_bound_set (scg->pane + 3,
			br->col, tl->row, SHEET_MAX_COLS-1, br->row-1);

		gtk_table_attach (scg->inner_table,
			GTK_WIDGET (scg->pane[2].col.canvas),
			1, 2, 0, 1,
			GTK_FILL | GTK_SHRINK,
			GTK_FILL,
			0, 0);
		gtk_table_attach (scg->inner_table,
			GTK_WIDGET (scg->pane[2].row.canvas),
			0, 1, 1, 2,
			GTK_FILL | GTK_SHRINK,
			GTK_FILL,
			0, 0);
		gtk_table_attach (scg->inner_table,
			GTK_WIDGET (scg->pane[2].gcanvas),
			1, 2, 1, 2,
			GTK_FILL | GTK_SHRINK,
			GTK_FILL,
			0, 0);
		gtk_table_attach (scg->inner_table,
			GTK_WIDGET (scg->pane[3].gcanvas),
			2, 3, 1, 2,
			GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			GTK_FILL | GTK_SHRINK,
			0, 0);
		gtk_table_attach (scg->inner_table,
			GTK_WIDGET (scg->pane[1].gcanvas),
			1, 2, 2, 3,
			GTK_FILL | GTK_SHRINK,
			GTK_EXPAND | GTK_FILL | GTK_SHRINK,
			0, 0);
	} else {
		gnm_pane_release (scg->pane + 1);
		gnm_pane_release (scg->pane + 2);
		gnm_pane_release (scg->pane + 3);
		scg->active_panes = 1;
		gnm_pane_bound_set (scg->pane + 0,
			0, 0, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
	}

	gtk_widget_show_all (GTK_WIDGET (scg->inner_table));

	/* in case headers are hidden */
	scg_adjust_preferences (SHEET_CONTROL (scg));
	scg_resize (SHEET_CONTROL (scg), TRUE);

	if (being_frozen) {
		CellPos const *tl = &sc->sheet->frozen_top_left;

		gnm_canvas_set_left_col (scg->pane[1].gcanvas, tl->col);
		gnm_canvas_set_top_row (scg->pane[3].gcanvas, tl->row);
		gnm_canvas_set_top_left (scg->pane[2].gcanvas,
					 tl->col, tl->row, TRUE);
	}
}

SheetControlGUI *
sheet_control_gui_new (Sheet *sheet)
{
	SheetControlGUI *scg;
	GtkUpdateType scroll_update_policy;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	scg = g_object_new (sheet_control_gui_get_type (), NULL);

	scg->active_panes = 1;
	scg->pane [0].is_active = FALSE;
	scg->pane [1].is_active = FALSE;
	scg->pane [2].is_active = FALSE;
	scg->pane [3].is_active = FALSE;

	scg->col_group.buttons = g_ptr_array_new ();
	scg->row_group.buttons = g_ptr_array_new ();
	scg->col_group.button_box = gtk_vbox_new (0, TRUE);
	scg->row_group.button_box = gtk_hbox_new (0, TRUE);
	scg->select_all_btn = gtk_button_new ();
	GTK_WIDGET_UNSET_FLAGS (scg->select_all_btn, GTK_CAN_FOCUS);
	g_signal_connect (G_OBJECT (scg->select_all_btn),
		"clicked",
		G_CALLBACK (cb_select_all), scg);

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

	gnm_pane_init (scg->pane + 0, scg, TRUE, 0);
	scg->inner_table = GTK_TABLE (gtk_table_new (3, 3, FALSE));
	gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->corner),
		0, 1, 0, 1,
		GTK_FILL,
		GTK_FILL,
		0, 0);
	gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->pane[0].col.canvas),
		2, 3, 0, 1,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		GTK_FILL,
		0, 0);
	gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->pane[0].row.canvas),
		0, 1, 2, 3,
		GTK_FILL,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		0, 0);
	gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->pane[0].gcanvas),
		2, 3, 2, 3,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		0, 0);
	gtk_widget_show_all (GTK_WIDGET (scg->inner_table));

	/* Scroll bars and their adjustments */
	scroll_update_policy = application_live_scrolling ()
		? GTK_UPDATE_CONTINUOUS : GTK_UPDATE_DELAYED;
	scg->va = gtk_adjustment_new (0., 0., 1, 1., 1., 1.);
	scg->vs = g_object_new (GTK_TYPE_VSCROLLBAR,
			"adjustment",	 GTK_ADJUSTMENT (scg->va),
			"update_policy", scroll_update_policy,
			NULL);
	g_signal_connect (G_OBJECT (scg->vs),
		"value_changed",
		G_CALLBACK (cb_vscrollbar_value_changed), scg);
	g_signal_connect (G_OBJECT (scg->vs),
		"adjust_bounds",
		G_CALLBACK (cb_vscrollbar_adjust_bounds), NULL);

	scg->ha = gtk_adjustment_new (0., 0., 1, 1., 1., 1.);
	scg->hs = g_object_new (GTK_TYPE_HSCROLLBAR,
			"adjustment", GTK_ADJUSTMENT (scg->ha),
			"update_policy", scroll_update_policy,
			NULL);
	g_signal_connect (G_OBJECT (scg->hs),
		"value_changed",
		G_CALLBACK (cb_hscrollbar_value_changed), scg);
	g_signal_connect (G_OBJECT (scg->hs),
		"adjust_bounds",
		G_CALLBACK (cb_hscrollbar_adjust_bounds), NULL);

	scg->table = GTK_TABLE (gtk_table_new (4, 4, FALSE));
	gtk_object_set_data (GTK_OBJECT (scg->table), SHEET_CONTROL_KEY, scg);
	gtk_table_attach (scg->table, GTK_WIDGET (scg->inner_table),
		0, 1, 0, 1,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		0, 0);
	gtk_table_attach (scg->table, scg->vs,
		1, 2, 0, 1,
		GTK_FILL,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		0, 0);
	gtk_table_attach (scg->table, scg->hs,
		0, 1, 1, 2,
		GTK_EXPAND | GTK_FILL | GTK_SHRINK,
		GTK_FILL,
		0, 0);
	g_signal_connect_after (G_OBJECT (scg->table),
		"size_allocate",
		G_CALLBACK (cb_table_size_allocate), scg);
	g_signal_connect (G_OBJECT (scg->table),
		"destroy",
		G_CALLBACK (cb_table_destroy), scg);

	sheet_attach_control (sheet, SHEET_CONTROL (scg));

	return scg;
}

static void
scg_finalize (GObject *object)
{
	SheetControlGUI *scg = SHEET_CONTROL_GUI (object);
	SheetControl *sc = (SheetControl *) scg;
	Sheet	     *sheet = sc_sheet (sc);
	GList *ptr;

	/* remove the object view before we disappear */
	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next )
		sc_object_destroy_view	(sc, SHEET_OBJECT (ptr->data));

	g_ptr_array_free (scg->col_group.buttons, TRUE);
	g_ptr_array_free (scg->row_group.buttons, TRUE);

	if (sc->sheet)
		sheet_detach_control (sc);

	if (scg->table)
		gtk_object_unref (GTK_OBJECT (scg->table));

	if (G_OBJECT_CLASS (scg_parent_class)->finalize)
		(*G_OBJECT_CLASS (scg_parent_class)->finalize)(object);
}

static void
scg_unant (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	/* Always have a pane 0 */
	if (scg->pane[0].anted_cursors == NULL)
		return;

	SCG_FOREACH_PANE (scg, pane, {
		GSList *l;

		for (l = pane->anted_cursors; l; l = l->next)
			gtk_object_destroy (GTK_OBJECT (l->data));

		g_slist_free (pane->anted_cursors);
		pane->anted_cursors = NULL;
	});
}

static void
scg_ant (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	GList *l;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	/* Always have a grid 0 */
	if (NULL != scg->pane[0].anted_cursors)
		scg_unant (sc);

	for (l = sc->sheet->ants; l; l = l->next) {
		Range const *r = l->data;

		SCG_FOREACH_PANE (scg, pane, {
			ItemCursor *ic = ITEM_CURSOR (gnome_canvas_item_new (
				pane->gcanvas->anted_group,
				item_cursor_get_type (),
				"SheetControlGUI", scg,
				"Style", ITEM_CURSOR_ANTED,
				NULL));
			item_cursor_bound_set (ic, r);
			pane->anted_cursors =
				g_slist_prepend (pane->anted_cursors, ic);
		});
	}
}

void
scg_adjust_preferences (SheetControl *sc)
{
	SheetControlGUI *scg = SHEET_CONTROL_GUI (sc);
	Sheet const *sheet = sc->sheet;

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

	if (sc->wbc != NULL) {
		WorkbookView *wbv = wb_control_view (sc->wbc);
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

StyleFont *
scg_get_style_font (Sheet const *sheet, MStyle const *mstyle)
{
	/* When previewing sheet can == NULL */
	double const zoom = (sheet) ? sheet->last_zoom_factor_used : 1.;

	return mstyle_get_font (mstyle, zoom);
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
	CONTEXT_COMMENT_EDIT
};
static gboolean
context_menu_handler (GnumericPopupMenuElement const *element,
		      gpointer user_data)
{
	SheetControlGUI *scg = user_data;
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = sc->sheet;
	WorkbookControlGUI *wbcg = scg->wbcg;
	WorkbookControl *wbc = sc->wbc;

	g_return_val_if_fail (element != NULL, TRUE);
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	switch (element->index) {
	case CONTEXT_CUT :
		sheet_selection_cut (wbc, sheet);
		break;
	case CONTEXT_COPY :
		sheet_selection_copy (wbc, sheet);
		break;
	case CONTEXT_PASTE :
		cmd_paste_to_selection (wbc, sheet, PASTE_DEFAULT);
		break;
	case CONTEXT_PASTE_SPECIAL : {
		int flags = dialog_paste_special (wbcg);
		if (flags != 0)
			cmd_paste_to_selection (wbc, sheet, flags);
		break;
	}
	case CONTEXT_INSERT :
		dialog_insert_cells (wbcg, sheet);
		break;
	case CONTEXT_DELETE :
		dialog_delete_cells (wbcg, sheet);
		break;
	case CONTEXT_CLEAR_CONTENT :
		cmd_clear_selection (wbc, sheet, CLEAR_VALUES);
		break;
	case CONTEXT_FORMAT_CELL :
		dialog_cell_format (wbcg, sheet, FD_CURRENT);
		break;
	case CONTEXT_COL_WIDTH :
		sheet_dialog_set_column_width (NULL, wbcg);
		break;
	case CONTEXT_COL_HIDE :
		cmd_colrow_hide_selection (wbc, sheet, TRUE, FALSE);
		break;
	case CONTEXT_COL_UNHIDE :
		cmd_colrow_hide_selection (wbc, sheet, TRUE, TRUE);
		break;
	case CONTEXT_ROW_HEIGHT :
		sheet_dialog_set_row_height (NULL, wbcg);
		break;
	case CONTEXT_ROW_HIDE :
		cmd_colrow_hide_selection (wbc, sheet, FALSE, FALSE);
		break;
	case CONTEXT_ROW_UNHIDE :
		cmd_colrow_hide_selection (wbc, sheet, FALSE, TRUE);
		break;
	case CONTEXT_COMMENT_EDIT:
		dialog_cell_comment (wbcg, sheet, &sheet->edit_pos);
		break;
	default :
		break;
	};
	return TRUE;
}

void
scg_context_menu (SheetControlGUI *scg, GdkEventButton *event,
		  gboolean is_col, gboolean is_row)
{
	SheetControl *sc = (SheetControl *) scg;

	enum {
		CONTEXT_DISPLAY_FOR_CELLS = 1,
		CONTEXT_DISPLAY_FOR_ROWS = 2,
		CONTEXT_DISPLAY_FOR_COLS = 4
	};
	enum {
		CONTEXT_DISABLE_PASTE_SPECIAL = 1,
		CONTEXT_DISABLE_FOR_ROWS = 2,
		CONTEXT_DISABLE_FOR_COLS = 4
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
		 * 	eg : Insert 4 rows
		 * 	or : Insert row
		 * This is hard for now because there is no memory management for the label
		 * strings, and the logic that knows the count is elsewhere
		 */
		{ N_("_Insert..."),	NULL,
		    CONTEXT_DISPLAY_FOR_CELLS, 0, CONTEXT_INSERT },
		{ N_("_Delete..."),	GTK_STOCK_DELETE,
		    CONTEXT_DISPLAY_FOR_CELLS, 0, CONTEXT_DELETE },
		{ N_("_Insert Column(s)"), "Gnumeric_ColumnAdd",
		    CONTEXT_DISPLAY_FOR_COLS, CONTEXT_DISABLE_FOR_COLS, CONTEXT_INSERT },
		{ N_("_Delete Column(s)"), "Gnumeric_ColumnDelete",
		    CONTEXT_DISPLAY_FOR_COLS, CONTEXT_DISABLE_FOR_COLS, CONTEXT_DELETE },
		{ N_("_Insert Row(s)"), "Gnumeric_RowAdd",
		    CONTEXT_DISPLAY_FOR_ROWS, CONTEXT_DISABLE_FOR_ROWS, CONTEXT_INSERT },
		{ N_("_Delete Row(s)"), "Gnumeric_RowDelete",
		    CONTEXT_DISPLAY_FOR_ROWS, CONTEXT_DISABLE_FOR_ROWS, CONTEXT_DELETE },

		{ N_("Clear Co_ntents"),NULL,
		    0, 0, CONTEXT_CLEAR_CONTENT },
		{ N_("_Add / Modify comment..."),"Gnumeric_CommentEdit",
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

		{ NULL, NULL, 0, 0, 0 },
	};

	/* row and column specific operations */
	int const display_filter =
		((!is_col && !is_row) ? CONTEXT_DISPLAY_FOR_CELLS : 0) |
		(is_col ? CONTEXT_DISPLAY_FOR_COLS : 0) |
		(is_row ? CONTEXT_DISPLAY_FOR_ROWS : 0);

	/*
	 * Paste special does not apply to cut cells.  Enable
	 * when there is nothing in the local clipboard, or when
	 * the clipboard has the results of a copy.
	 */
	int sensitivity_filter =
	    (application_clipboard_is_empty () ||
	    (application_clipboard_contents_get () != NULL))
		? 0 : CONTEXT_DISABLE_PASTE_SPECIAL;

	GList *l;

	wbcg_edit_finish (scg->wbcg, FALSE);

	/*
	 * Now see if there is some selection which selects a
	 * whole row or a whole column and disable the insert/delete col/row menu items
	 * accordingly
	 */
	for (l = sc->sheet->selections; l != NULL; l = l->next) {
		Range const *r = l->data;

		if (r->start.row == 0 && r->end.row == SHEET_MAX_ROWS - 1)
			sensitivity_filter |= CONTEXT_DISABLE_FOR_ROWS;

		if (r->start.col == 0 && r->end.col == SHEET_MAX_COLS - 1)
			sensitivity_filter |= CONTEXT_DISABLE_FOR_COLS;
	}

	gnumeric_create_popup_menu (popup_elements, &context_menu_handler,
				    scg, display_filter,
				    sensitivity_filter, event);
}

static gboolean
cb_redraw_sel (Sheet *sheet, Range const *r, gpointer user_data)
{
	SheetControl *sc = user_data;
	scg_redraw_region (sc,
		r->start.col, r->start.row, r->end.col, r->end.row);
	scg_redraw_headers (sc, TRUE, TRUE, r);
	return TRUE;
}

static void
scg_cursor_visible (SheetControlGUI *scg, gboolean is_visible)
{
	SheetControl *sc = (SheetControl *) scg;

	/* there is always a grid 0 */
	if (NULL == scg->pane[0].gcanvas)
		return;

	SCG_FOREACH_PANE (scg, pane,
		item_cursor_set_visibility (pane->cursor.std, is_visible););

	selection_foreach_range (sc->sheet, TRUE, cb_redraw_sel, sc);
}

/***************************************************************************/

#define SO_CLASS(so) SHEET_OBJECT_CLASS (G_OBJECT_GET_CLASS(so))

void
scg_object_stop_editing (SheetControlGUI *scg, SheetObject *so)
{
	GObject *view;

	if (so == NULL || so != scg->current_object)
		return;

	SCG_FOREACH_PANE (scg, pane, gnm_pane_object_stop_editing (pane););

	g_object_unref (G_OBJECT (scg->current_object));
	scg->current_object = NULL;

	if (SO_CLASS (so)->set_active != NULL)
		SCG_FOREACH_PANE (scg, pane, {
			view = sheet_object_get_view (so, pane);
			SO_CLASS (so)->set_active (so, view, FALSE);
		});
}

static gboolean
scg_mode_clear (SheetControlGUI *scg)
{
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), FALSE);

	if (scg->new_object != NULL) {
		g_object_unref (G_OBJECT (scg->new_object));
		scg->new_object = NULL;
	}
	scg_object_stop_editing (scg, scg->current_object);

	return TRUE;
}

/*
 * scg_mode_edit:
 * @sc:  The sheet control
 *
 * Put @sheet into the standard state 'edit mode'.  This shuts down
 * any object editing and frees any objects that are created but not
 * realized.
 */
void
scg_mode_edit (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	scg_mode_clear (scg);
	scg_set_display_cursor (scg);

	/* During destruction we have already been disconnected
	 * so don't bother changing the cursor
	 */
	if (sc->sheet != NULL)
		scg_cursor_visible (scg, TRUE);

	if (wbcg_edit_has_guru (scg->wbcg))
		wbcg_edit_finish (scg->wbcg, FALSE);
}

/*
 * scg_mode_edit_object
 * @scg: The SheetControl to edit in.
 * @so : The SheetObject to select.
 *
 * Makes @so the currently selected object and prepares it for
 * user editing.
 */
void
scg_mode_edit_object (SheetControlGUI *scg, SheetObject *so)
{
	GObject *view;

	g_return_if_fail (IS_SHEET_OBJECT (so));

	/* Add protective ref before clearing the mode, in case we are starting
	 * to edit a newly created object
	 */
	g_object_ref (G_OBJECT (so));
	if (wbcg_edit_finish (scg->wbcg, TRUE) &&
	    scg_mode_clear (scg)) {
		scg->current_object = so;
		g_object_ref (G_OBJECT (so));

		if (SO_CLASS (so)->set_active != NULL)
			SCG_FOREACH_PANE (scg, pane, {
				view = sheet_object_get_view (so, pane);
				SO_CLASS (so)->set_active (so, view, TRUE);
			});
		scg_cursor_visible (scg, FALSE);
		scg_object_update_bbox (scg, so, NULL);
		scg_set_display_cursor (scg);
	}
	g_object_unref (G_OBJECT (so));
}

/**
 * scg_mode_create_object :
 * @so : The object the needs to be placed
 *
 * Takes a newly created SheetObject that has not yet been realized and
 * prepares to place it on the sheet.
 * NOTE : Absorbs a reference to the object.
 */
void
scg_mode_create_object (SheetControlGUI *scg, SheetObject *so)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (scg_mode_clear (scg)) {
		scg->new_object = so;
		scg_cursor_visible (scg, FALSE);
		scg_set_display_cursor (scg);
	}
}

void
scg_object_nudge (SheetControlGUI *scg, int x_offset, int y_offset)
{
	int i;
	double new_coords [4];

	if (scg->current_object == NULL)
		return;

	for (i = 4; i-- > 0; )
		new_coords [i] = scg->object_coords [i];

	new_coords [0] += x_offset;
	new_coords [1] += y_offset;
	new_coords [2] += x_offset;
	new_coords [3] += y_offset;

	/* Tell the object to update its co-ordinates */
	scg_object_update_bbox (scg, scg->current_object, new_coords);
}

/**
 * scg_object_update_bbox:
 *
 * @scg : The Sheet control
 * @so : the optional sheet object
 * @new_coords : optionally jump the object to new coordinates
 *
 * Re-align the control points so that they appear at the correct verticies for
 * this view of the object.
 */
void
scg_object_update_bbox (SheetControlGUI *scg, SheetObject *so,
			double const *new_coords)
{
	double l, t, r ,b;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (so == NULL)
		so = scg->current_object;
	if (so == NULL)
		return;
	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (new_coords != NULL)
		scg_object_calc_position (scg, so, new_coords);
	else
		scg_object_view_position (scg, so, scg->object_coords);

	l = scg->object_coords [0];
	t = scg->object_coords [1];
	r = scg->object_coords [2] + 1;
	b = scg->object_coords [3] + 1;

	SCG_FOREACH_PANE (scg, pane,
		gnm_pane_object_set_bounds (pane, so, l, t, r ,b););
}

static int
calc_obj_place (GnumericCanvas *gcanvas, int pixel, gboolean is_col,
		SheetObjectAnchorType anchor_type, float *offset)
{
	int origin;
	int colrow;
	ColRowInfo const *cri;
	Sheet *sheet = ((SheetControl *) gcanvas->simple.scg)->sheet;

	if (is_col) {
		colrow = gnm_canvas_find_col (gcanvas, pixel, &origin);
		cri = sheet_col_get_info (sheet, colrow);
	} else {
		colrow = gnm_canvas_find_row (gcanvas, pixel, &origin);
		cri = sheet_row_get_info (sheet, colrow);
	}

	/* TODO : handle other anchor types */
	*offset = ((float) (pixel - origin))/ ((float) cri->size_pixels);
	if (anchor_type == SO_ANCHOR_PERCENTAGE_FROM_COLROW_END)
		*offset = 1. - *offset;
	return colrow;
}

void
scg_object_calc_position (SheetControlGUI *scg, SheetObject *so, double const *coords)
{
	GnumericCanvas *gcanvas;
	SheetObjectAnchor anchor;
	int i, pixels [4];
	double tmp [4];

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (coords != NULL);

	sheet_object_anchor_cpy (&anchor, sheet_object_anchor_get (so));

	if (coords [0] > coords [2]) {
		tmp [0] = coords [2];
		tmp [2] = coords [0];
	} else {
		tmp [0] = coords [0];
		tmp [2] = coords [2];
	}
	if (coords [1] > coords [3]) {
		tmp [1] = coords [3];
		tmp [3] = coords [1];
	} else {
		tmp [1] = coords [1];
		tmp [3] = coords [3];
	}

	for (i = 4; i-- > 0 ;)
		scg->object_coords [i] = coords [i];

	/* pane 0 always exists and the others are always use the same basis */
	gcanvas = scg_pane (scg, 0);
	gnome_canvas_w2c (GNOME_CANVAS (gcanvas),
		tmp [0], tmp [1],
		pixels + 0, pixels + 1);
	gnome_canvas_w2c (GNOME_CANVAS (gcanvas),
		tmp [2], tmp [3],
		pixels + 2, pixels + 3);
	anchor.cell_bound.start.col = calc_obj_place (gcanvas, pixels [0], TRUE,
		so->anchor.type [0], anchor.offset + 0);
	anchor.cell_bound.start.row = calc_obj_place (gcanvas, pixels [1], FALSE,
		so->anchor.type [1], anchor.offset + 1);
	anchor.cell_bound.end.col = calc_obj_place (gcanvas, pixels [2], TRUE,
		so->anchor.type [2], anchor.offset + 2);
	anchor.cell_bound.end.row = calc_obj_place (gcanvas, pixels [3], FALSE,
		so->anchor.type [3], anchor.offset + 3);

	sheet_object_anchor_set (so, &anchor);
}

void
scg_object_view_position (SheetControlGUI *scg, SheetObject *so, double *coords)
{
	SheetObjectDirection direction;
	double pixels [4];
	/* pane 0 always exists and the others are always use the same basis */
	GnomeCanvas *canvas = GNOME_CANVAS (scg_pane (scg, 0));

	sheet_object_position_pixels_get (so, SHEET_CONTROL (scg), pixels);

	direction = so->anchor.direction;
	if (direction == SO_DIR_UNKNOWN)
		direction = SO_DIR_DOWN_RIGHT;

	gnome_canvas_c2w (canvas,
		pixels [direction & SO_DIR_H_MASK  ? 0 : 2],
		pixels [direction & SO_DIR_V_MASK  ? 1 : 3],
		coords +0, coords + 1);
	gnome_canvas_c2w (canvas,
		pixels [direction & SO_DIR_H_MASK  ? 2 : 0],
		pixels [direction & SO_DIR_V_MASK  ? 3 : 1],
		coords +2, coords + 3);
}

/***************************************************************************/

static void
scg_comment_timer_clear (SheetControlGUI *scg)
{
	if (scg->comment.timer != -1) {
		gtk_timeout_remove (scg->comment.timer);
		scg->comment.timer = -1;
	}
}

/**
 * scg_comment_display :
 * @scg : The SheetControl
 * @cc  : A cell comment
 *
 */
void
scg_comment_display (SheetControlGUI *scg, CellComment *cc)
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
		GtkWidget *label, *frame, *scroll;
		GtkAdjustment *adjust_1, *adjust_2;

		scg->comment.item = gtk_window_new (GTK_WINDOW_POPUP);
		gdk_window_get_pointer (NULL, &x, &y, NULL);
		gtk_widget_set_uposition (scg->comment.item, x+10, y+10);

		label = gtk_text_view_new ();
		gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(label), GTK_WRAP_NONE);
		gnumeric_textview_set_text (GTK_TEXT_VIEW(label),
					    cell_comment_text_get (cc));
		gtk_text_view_set_editable (GTK_TEXT_VIEW(label), FALSE);

/* FIXME: when gtk+ has been fixed (#73562), we should skip the scrolled window */

		adjust_1 =  GTK_ADJUSTMENT (gtk_adjustment_new (0.0,0.0,100.0,1.0,1.0,10.0));
		adjust_2 =  GTK_ADJUSTMENT (gtk_adjustment_new (0.0,0.0,100.0,1.0,1.0,10.0));
		scroll = gtk_scrolled_window_new (adjust_1, adjust_2);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), 
						GTK_POLICY_NEVER, GTK_POLICY_NEVER);

		frame = gtk_frame_new (NULL);
		gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);

		gtk_container_add (GTK_CONTAINER (scg->comment.item), frame);
		gtk_container_add (GTK_CONTAINER (frame), scroll);
		gtk_container_add (GTK_CONTAINER (scroll), label);
		gtk_widget_show_all (scg->comment.item);
	}
}

/**
 * cb_cell_comment_timer :
 *
 * Utility routine to disaply a comment after a short delay.
 */
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
scg_comment_select (SheetControlGUI *scg, CellComment *cc)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	g_return_if_fail (scg->comment.timer == -1);

	if (scg->comment.selected != NULL)
		scg_comment_unselect (scg, scg->comment.selected);

	scg->comment.selected = cc;
	scg->comment.timer = gtk_timeout_add (1000,
		(GtkFunction)cb_cell_comment_timer, scg);
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
scg_comment_unselect (SheetControlGUI *scg, CellComment *cc)
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
		g_return_val_if_fail (to <= SHEET_MAX_COLS, 1);
		collection = &sc->sheet->cols;
	} else {
		g_return_val_if_fail (to <= SHEET_MAX_ROWS, 1);
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
			ColRowInfo const *cri = segment->info [COLROW_SUB_INDEX(i)];
			if (cri == NULL)
				pixels += default_size;
			else if (cri->visible)
				pixels += cri->size_pixels;
		} else {
			int segment_end = COLROW_SEGMENT_END(i)+1;
			if (segment_end > to)
				segment_end = to;
			pixels += default_size * (segment_end - i);
			i = segment_end-1;
		}
	}

	return pixels*sign;
}

static float
scg_colrow_distance_get_virtual (SheetControl const *sc, gboolean is_cols,
				 int from, int to)
{
	return scg_colrow_distance_get (SHEET_CONTROL_GUI (sc), is_cols,
					from, to);
}

/*************************************************************************/

static void
scg_cursor_bound (SheetControl *sc, Range const *r)
{
	SheetControlGUI *scg = (SheetControlGUI *) sc;
	SCG_FOREACH_PANE (scg, pane, gnm_pane_cursor_bound_set (pane, r););
}

static void
scg_compute_visible_region (SheetControl *sc, gboolean full_recompute)
{
	SheetControlGUI *scg = (SheetControlGUI *) sc;

	SCG_FOREACH_PANE (scg, pane, 
		gnm_canvas_compute_visible_region (pane->gcanvas,
						   full_recompute););
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
 */
static void
scg_rangesel_changed (SheetControlGUI *scg,
		      int base_col, int base_row,
		      int move_col, int move_row)
{
	GnumericExprEntry *expr_entry;
	gboolean ic_changed;
	Range *r, last_r;
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

	sheet = ((SheetControl *) scg)->sheet;
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
	sheet_merge_find_container (sheet, r);
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
	Range r;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (scg->rangesel.active)
		return;

	if (scg->wbcg->rangesel != NULL)
		g_warning ("mis configed rangesel");

	scg->wbcg->rangesel = scg;
	scg->rangesel.active = TRUE;

	gnm_expr_entry_rangesel_start (wbcg_get_entry_logical (scg->wbcg));

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
	int cursor;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (scg->new_object != NULL)
		cursor = E_CURSOR_THIN_CROSS;
	else if (scg->current_object != NULL)
		cursor = E_CURSOR_ARROW;
	else
		cursor = E_CURSOR_FAT_CROSS;

	SCG_FOREACH_PANE (scg, pane, e_cursor_set_widget (pane->gcanvas, cursor););
}

void
scg_rangesel_extend_to (SheetControlGUI *scg, int col, int row)
{
	int base_col, base_row;

	if (col < 0) {
		base_col = 0;
		col = SHEET_MAX_COLS - 1;
	} else
		base_col = scg->rangesel.base_corner.col;
	if (row < 0) {
		base_row = 0;
		row = SHEET_MAX_ROWS - 1;
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
	Sheet *sheet = ((SheetControl *) scg)->sheet;
	CellPos tmp;

	if (!scg->rangesel.active)
		scg_rangesel_start (scg,
			sheet->edit_pos_real.col, sheet->edit_pos_real.row,
			sheet->edit_pos_real.col, sheet->edit_pos_real.row);

	tmp = scg->rangesel.base_corner;
	if (horiz)
		tmp.col = sheet_find_boundary_horizontal (
			sheet, tmp.col, tmp.row, tmp.row, n, jump_to_bound);
	else
		tmp.row = sheet_find_boundary_vertical (
			sheet, tmp.col, tmp.row, tmp.col, n, jump_to_bound);

	scg_rangesel_changed (scg, tmp.col, tmp.row, tmp.col, tmp.row);
	scg_make_cell_visible (scg, tmp.col, tmp.row, FALSE, TRUE);
}

void
scg_rangesel_extend (SheetControlGUI *scg, int n,
		     gboolean jump_to_bound, gboolean horiz)
{
	Sheet *sheet = ((SheetControl *) scg)->sheet;

	if (scg->rangesel.active) {
		CellPos tmp = scg->rangesel.move_corner;

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
	Sheet *sheet = ((SheetControl *) scg)->sheet;
	CellPos tmp = sheet->edit_pos_real;

	if (!wbcg_edit_finish (scg->wbcg, TRUE))
		return;

	if (horiz)
		tmp.col = sheet_find_boundary_horizontal (sheet,
			tmp.col, tmp.row, tmp.row,
			n, jump_to_bound);
	else
		tmp.row = sheet_find_boundary_vertical (sheet,
			tmp.col, tmp.row, tmp.col,
			n, jump_to_bound);

	sheet_selection_reset (sheet);
	sheet_cursor_set (sheet, tmp.col, tmp.row, tmp.col, tmp.row,
			  tmp.col, tmp.row, NULL);
	sheet_make_cell_visible (sheet, tmp.col, tmp.row, TRUE);
	sheet_selection_add (sheet, tmp.col, tmp.row);
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
	Sheet *sheet = ((SheetControl *) scg)->sheet;
	CellPos move = sheet->cursor.move_corner;
	CellPos visible = scg->pane[0].gcanvas->first;

	if (horiz)
		visible.col = move.col = sheet_find_boundary_horizontal (sheet,
			move.col, move.row, sheet->cursor.base_corner.row,
			n, jump_to_bound);
	else
		visible.row = move.row = sheet_find_boundary_vertical (sheet,
			move.col, move.row, sheet->cursor.base_corner.col,
			n, jump_to_bound);

	sheet_selection_extend_to (sheet, move.col, move.row);
	sheet_make_cell_visible (sheet, visible.col, visible.row, FALSE);
}

GtkWidget *
scg_toplevel (SheetControlGUI *scg)
{
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), NULL);

	return GTK_WIDGET (scg->table);
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

void
scg_colrow_resize_stop (SheetControlGUI *scg)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	SCG_FOREACH_PANE (scg, pane, 
		gnm_pane_colrow_resize_stop (pane););
}

void
scg_colrow_resize_start	(SheetControlGUI *scg,
			 gboolean is_cols, int resize_first)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	SCG_FOREACH_PANE (scg, pane, 
		gnm_pane_colrow_resize_start (pane,
			is_cols, resize_first););
}

void
scg_colrow_resize_move (SheetControlGUI *scg,
			gboolean is_cols, int pos)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	SCG_FOREACH_PANE (scg, pane, 
		gnm_pane_colrow_resize_move (pane, is_cols, pos););
}

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
scg_special_cursor_bound_set (SheetControlGUI *scg, Range const *r)
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
		sheet_object_new_view (so, sc, (gpointer)pane););
}

static void
scg_object_destroy_view	(SheetControl *sc, SheetObject *so)
{
	SheetControlGUI *scg = SHEET_CONTROL_GUI (sc);
	GObject *view;
	SCG_FOREACH_PANE (scg, pane, {
		view = sheet_object_get_view (so, (gpointer)pane);
		gtk_object_destroy (GTK_OBJECT (view));
	});
}

static void
scg_class_init (GObjectClass *object_class)
{
	SheetControlClass *sc_class = SHEET_CONTROL_CLASS (object_class);

	g_return_if_fail (sc_class != NULL);

	scg_parent_class = gtk_type_class (sheet_control_get_type ());
	object_class->finalize = scg_finalize;

	sc_class->resize                 = scg_resize;
	sc_class->set_zoom_factor        = scg_set_zoom_factor;
	sc_class->redraw_all             = scg_redraw_all;
	sc_class->redraw_region     	 = scg_redraw_region;
	sc_class->redraw_headers         = scg_redraw_headers;
	sc_class->ant                    = scg_ant;
	sc_class->unant                  = scg_unant;
	sc_class->adjust_preferences     = scg_adjust_preferences;
	sc_class->scrollbar_config       = scg_scrollbar_config;
	sc_class->set_top_left		 = scg_set_top_left;
	sc_class->compute_visible_region = scg_compute_visible_region;
	sc_class->make_cell_visible      = scg_make_cell_visible_virt; /* wrapper */
	sc_class->cursor_bound           = scg_cursor_bound;
	sc_class->set_panes		 = scg_set_panes;
	sc_class->colrow_distance_get	 = scg_colrow_distance_get_virtual;
	sc_class->object_create_view	 = scg_object_create_view;
	sc_class->object_destroy_view	 = scg_object_destroy_view;
}

E_MAKE_TYPE (sheet_control_gui, "SheetControlGUI", SheetControlGUI,
	     scg_class_init, scg_init, SHEET_CONTROL_TYPE)
