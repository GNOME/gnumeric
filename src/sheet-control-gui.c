/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * sheet-control-gui.c: Implements a graphic control for a sheet.
 *
 * Author:
 *    Miguel de Icaza (miguel@kernel.org)
 *    Jody Goldberg (jgoldberg@home.com)
 */
#include <config.h>

#include "sheet-control-gui-priv.h"
#include "item-bar.h"
#define GNUMERIC_ITEM "SCG"
#include "item-debug.h"
#include "gnumeric-canvas.h"
#include "sheet.h"
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
#include "item-cursor.h"
#include "gnumeric-util.h"
#include "parse-util.h"
#include "selection.h"
#include "application.h"
#include "cellspan.h"
#include "cmd-edit.h"
#include "commands.h"
#include "clipboard.h"
#include "dialogs.h"
#include "widgets/gnumeric-vscrollbar.h"
#include "widgets/gnumeric-hscrollbar.h"
#include "widgets/gnumeric-expr-entry.h"
#include "sheet-merge.h"
#include "ranges.h"

#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <gal/util/e-util.h>
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
scg_redraw_all (SheetControl *sc)
{
	int i;
	SheetControlGUI *scg = (SheetControlGUI *)sc;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	for (i = scg->active_panes; i-- > 0 ; ) {
		GnumericPane *pane = scg->pane + i;
		gnome_canvas_request_redraw (
			GNOME_CANVAS (pane->gcanvas),
			0, 0, INT_MAX, INT_MAX);
		if (NULL != pane->col.canvas)
			gnome_canvas_request_redraw (
				pane->col.canvas,
				0, 0, INT_MAX, INT_MAX);
		if (NULL != pane->row.canvas)
			gnome_canvas_request_redraw (
				pane->row.canvas,
				0, 0, INT_MAX, INT_MAX);
	}
}

static void
scg_redraw_region (SheetControl *sc,
		   int start_col, int start_row,
		   int end_col, int end_row)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	int i;

	for (i = scg->active_panes; i-- > 0 ; ) {
		GnumericPane *pane = scg->pane + i;
		gnm_canvas_redraw_region (pane->gcanvas,
			start_col, start_row, end_col, end_row);
	}
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
			gnome_canvas_request_redraw (
						     GNOME_CANVAS (pane->col.canvas),
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
scg_resize (SheetControl *sc, gboolean force_scroll)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	Sheet *sheet;
	GnumericCanvas *gcanvas;
	int i, h, w;
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
	gtk_widget_set_usize (GTK_WIDGET (scg->pane[0].col.canvas), -1, h+1);
	w = item_bar_calc_size (scg->pane[0].row.item);
	gtk_widget_set_usize (GTK_WIDGET (scg->pane[0].row.canvas), w+1, -1);

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

	for (i = scg->active_panes; i-- > 0 ; )
		gnm_pane_reposition_cursors (scg->pane + i);
}

void
scg_set_zoom_factor (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	double z;
	int i;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	/* TODO : move this to sheetView when we create one */
	z = sc->sheet->last_zoom_factor_used;

	/* Set pixels_per_unit before the font.  The item bars look here for the number */
	for (i = scg->active_panes; i-- > 0 ; ) {
		GnumericPane const *p = scg->pane + i;

		if (p->col.canvas != NULL)
			gnome_canvas_set_pixels_per_unit (p->col.canvas, z);
		if (p->row.canvas != NULL)
			gnome_canvas_set_pixels_per_unit (p->row.canvas, z);
		gnome_canvas_set_pixels_per_unit (GNOME_CANVAS (p->gcanvas), z);
	}

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
	va->upper = max_row;
	va->value = gcanvas->first.row;
	va->page_size = last_row - gcanvas->first.row;
	va->page_increment = MAX (va->page_size - 3, 1);
	va->step_increment = 1;

	if (max_col < sheet->cols.max_used)
		max_col = sheet->cols.max_used;
	if (max_col < sheet->max_object_extent.col)
		max_col = sheet->max_object_extent.col;
	ha->upper = max_col;
	ha->page_size = last_col - gcanvas->first.col;
	ha->value = gcanvas->first.col;
	ha->page_increment = MAX (ha->page_size - 3, 1);
	ha->step_increment = 1;

	gtk_adjustment_changed (va);
	gtk_adjustment_changed (ha);
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

static void
scg_select_all (SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = sc->sheet;
	gboolean const rangesel = wbcg_rangesel_possible (scg->wbcg);

	if (!rangesel) {
		if (!wbcg_edit_has_guru (scg->wbcg)) {
			wbcg_edit_finish (scg->wbcg, FALSE);
			cmd_select_all (sheet);
		}
	} else {
		if (!scg->rangesel.active)
			scg_rangesel_start (scg, 0, 0);
		scg_rangesel_bound (
			scg, 0, 0, SHEET_MAX_COLS-1, SHEET_MAX_ROWS-1);
		sheet_update (sheet);
	}
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

	if (rangesel && !scg->rangesel.active)
		scg_rangesel_start (scg, index, index);

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
		} else {
			/* FIXME : do we want to take the panes into account too ? */
			GnumericCanvas *gcanvas = scg_pane (scg, 0);

			if (is_cols)
				sheet_selection_add_range (sheet,
					index, gcanvas->first.row,
					index, 0,
					index, SHEET_MAX_ROWS-1);
			else
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
vertical_scroll_offset_changed (GtkAdjustment *adj, int top, int is_hint,
				SheetControlGUI *scg)
{
	if (is_hint) {
		char *buffer = g_strdup_printf (_("Row: %s"), row_name (top));
		wb_control_gui_set_status_text (scg->wbcg, buffer);
		g_free (buffer);
	} else {
		if (scg->wbcg)
			wb_control_gui_set_status_text (scg->wbcg, "");
		scg_set_top_row (scg, top);
	}
}

static void
horizontal_scroll_offset_changed (GtkAdjustment *adj, int left, int is_hint,
				  SheetControlGUI *scg)
{
	if (is_hint) {
		char *buffer = g_strdup_printf (_("Column: %s"), col_name (left));
		wb_control_gui_set_status_text (scg->wbcg, buffer);
		g_free (buffer);
	} else {
		if (scg->wbcg)
			wb_control_gui_set_status_text (scg->wbcg, "");
		scg_set_left_col (scg, left);
	}
}

static void
cb_table_destroy (GtkObject *table, SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	int i;

	scg_mode_edit (sc);	/* finish any object edits */
	scg_unant (sc);		/* Make sure that everything is unanted */

 	if (scg->wbcg) {
		GtkWindow *toplevel = wb_control_gui_toplevel (scg->wbcg);

		/* Only pane-0 ever gets focus */
		if (NULL != toplevel &&
		    toplevel->focus_widget == GTK_WIDGET (scg_pane (scg, 0)))
			gtk_window_set_focus (toplevel, NULL);
	}
	scg->table = NULL;
	for (i = scg->active_panes; i-- > 0 ; )
		scg->pane[i].gcanvas = NULL;

	gtk_object_unref (GTK_OBJECT (scg));
}

static void
scg_init (SheetControlGUI *scg)
{
	((SheetControl *) scg)->sheet = NULL;

	scg->comment.selected = NULL;
	scg->comment.item = NULL;
	scg->comment.timer = -1;

	scg->new_object = NULL;
	scg->current_object = NULL;
	scg->drag_object = NULL;
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
		Sheet *sheet = gcanvas->scg->sheet_control.sheet;
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
		scg_colrow_distance_get (gcanvas->scg, TRUE, gcanvas->first.col, new_first_col);
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
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (scg->active_panes > 1) {
		int right = scg->sheet_control.sheet->unfrozen_top_left.col;
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
		scg_colrow_distance_get (gcanvas->scg, FALSE, gcanvas->first.row, new_first_row);
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
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (scg->active_panes > 1) {
		int bottom = scg->sheet_control.sheet->unfrozen_top_left.row;
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

	sheet = ((SheetControl *) gcanvas->scg)->sheet;
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
			    gboolean force_scroll, gboolean couple_panes)
{
	scg_make_cell_visible ((SheetControlGUI *)sc, col, row,
			       force_scroll, couple_panes);
}

/*************************************************************************/

static void
scg_set_panes (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *) sc;
	gboolean const being_frozen = sheet_is_frozen (sc->sheet);
	gboolean const was_frozen = scg->pane[2].gcanvas != NULL;
	int col = 0, row = 0;

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

		gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->pane[2].col.canvas),
				  1, 2, 0, 1,
				  GTK_FILL | GTK_SHRINK,
				  GTK_FILL,
				  0, 0);
		gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->pane[2].row.canvas),
				  0, 1, 1, 2,
				  GTK_FILL | GTK_SHRINK,
				  GTK_FILL,
				  0, 0);
		gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->pane[2].gcanvas),
				  1, 2, 1, 2,
				  GTK_FILL | GTK_SHRINK,
				  GTK_FILL,
				  0, 0);
		gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->pane[3].gcanvas),
				  2, 3, 1, 2,
				  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
				  GTK_FILL | GTK_SHRINK,
				  0, 0);
		gtk_table_attach (scg->inner_table, GTK_WIDGET (scg->pane[1].gcanvas),
				  1, 2, 2, 3,
				  GTK_FILL | GTK_SHRINK,
				  GTK_EXPAND | GTK_FILL | GTK_SHRINK,
				  0, 0);
	} else { 
		/* Use these because the tl, br in sheet have been cleared */
		col = scg->pane[2].gcanvas->first.col;
		row = scg->pane[2].gcanvas->first.row;

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
		/* scroll to starting points */
		CellPos const *tl = &sc->sheet->frozen_top_left;
		CellPos const *br = &sc->sheet->unfrozen_top_left;
		gnm_canvas_set_top_left (scg->pane[3].gcanvas,
					     br->col, tl->row, FALSE);
		gnm_canvas_set_top_left (scg->pane[2].gcanvas,
					     tl->col, tl->row, FALSE);
		gnm_canvas_set_top_left (scg->pane[1].gcanvas,
					     tl->col, br->row, FALSE);
		gnm_canvas_set_top_left (scg->pane[0].gcanvas,
					     br->col, br->row, FALSE);
	} else
		gnm_canvas_set_top_left (scg->pane[0].gcanvas,
					     col, row, FALSE);
}

static void
scg_construct (SheetControlGUI *scg)
{
	int i;

	scg->active_panes = 1;
	scg->table = GTK_TABLE (gtk_table_new (4, 4, FALSE));
	gtk_signal_connect_after (
		GTK_OBJECT (scg->table), "size_allocate",
		GTK_SIGNAL_FUNC (cb_table_size_allocate), scg);
	gtk_signal_connect (
		GTK_OBJECT (scg->table), "destroy",
		GTK_SIGNAL_FUNC (cb_table_destroy), scg);

	scg->inner_table = GTK_TABLE (gtk_table_new (3, 3, FALSE));

	scg->select_all_btn = gtk_button_new ();
	GTK_WIDGET_UNSET_FLAGS (scg->select_all_btn, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (scg->select_all_btn), "clicked",
			    GTK_SIGNAL_FUNC (cb_select_all), scg);
	gtk_table_attach (scg->inner_table, scg->select_all_btn, 0, 1, 0, 1,
			  GTK_FILL,
			  GTK_FILL,
			  0, 0);

	gnm_pane_init (scg->pane + 0, scg, TRUE, 0);
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
	scg->va = gtk_adjustment_new (0., 0., 1, 1., 1., 1.);
	scg->ha = gtk_adjustment_new (0., 0., 1, 1., 1., 1.);
	scg->vs = gnumeric_vscrollbar_new (GTK_ADJUSTMENT (scg->va));
	scg->hs = gnumeric_hscrollbar_new (GTK_ADJUSTMENT (scg->ha));
	gtk_signal_connect (GTK_OBJECT (scg->vs), "offset_changed",
			    GTK_SIGNAL_FUNC (vertical_scroll_offset_changed),
			    scg);
	gtk_signal_connect (GTK_OBJECT (scg->hs), "offset_changed",
			    GTK_SIGNAL_FUNC (horizontal_scroll_offset_changed),
			    scg);

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

	i = sizeof (scg->control_points)/sizeof(GnomeCanvasItem *);
	while (i-- > 0)
		scg->control_points[i] = NULL;
	gtk_object_set_data (GTK_OBJECT (scg->table), SHEET_CONTROL_KEY, scg);
}

SheetControlGUI *
sheet_control_gui_new (Sheet *sheet)
{
	SheetControlGUI *scg;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	scg = gtk_type_new (sheet_control_gui_get_type ());
	scg_construct (scg);
	sheet_attach_control (sheet, SHEET_CONTROL (scg));

	return scg;
}

static void
scg_destroy (GtkObject *object)
{
	SheetControlGUI *scg = SHEET_CONTROL_GUI (object);
	SheetControl *sc = (SheetControl *) scg;

	if (sc->sheet)
		sheet_detach_control (sc);

	if (scg->table)
		gtk_object_unref (GTK_OBJECT (scg->table));
	
	/* FIXME : Should we be pedantic and clear the control points ? */

	if (GTK_OBJECT_CLASS (scg_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (scg_parent_class)->destroy)(object);
}

static void
scg_unant (SheetControl *sc)
{
	SheetControlGUI *scg = (SheetControlGUI *)sc;
	int i;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	/* Always have a pane 0 */
	if (scg->pane[0].anted_cursors == NULL)
		return;

	for (i = scg->active_panes; i-- > 0 ; ) {
		GList *l;
		GnumericPane *pane = scg->pane + i;

		for (l = pane->anted_cursors; l; l = l->next)
			gtk_object_destroy (GTK_OBJECT (l->data));

		g_list_free (pane->anted_cursors);
		pane->anted_cursors = NULL;
	}
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
		int i;

		for (i = scg->active_panes; i-- > 0 ; ) {
			GnumericPane *pane = scg->pane + i;
			ItemCursor *ic = ITEM_CURSOR (gnome_canvas_item_new (
				pane->gcanvas->anted_group,
				item_cursor_get_type (),
				"SheetControlGUI", scg,
				"Style", ITEM_CURSOR_ANTED,
				NULL));
			item_cursor_bound_set (ic, r);
			pane->anted_cursors =
				g_list_prepend (pane->anted_cursors, ic);
		}
	}
}

void
scg_adjust_preferences (SheetControl *sc)
{
	SheetControlGUI *scg = SHEET_CONTROL_GUI (sc);
	Sheet const *sheet = sc->sheet;
	int i;

	for (i = scg->active_panes; i-- > 0 ; ) {
		GnumericPane const *pane = scg->pane + i;
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
	}

	if (sheet->hide_col_header || sheet->hide_row_header)
		gtk_widget_hide (scg->select_all_btn);
	else
		gtk_widget_show (scg->select_all_btn);

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
scg_get_style_font (const Sheet *sheet, MStyle const * const mstyle)
{
	/* Scale the font size by the average scaling factor for the
	 * display.  72dpi is base size
	 */
	double const res  = application_dpi_to_pixels ();

	/* When previewing sheet can == NULL */
	double const zoom = (sheet) ? sheet->last_zoom_factor_used : 1.;

	return mstyle_get_font (mstyle, zoom * res);
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
	CONTEXT_ROW_UNHIDE
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
		{ N_("Cu_t"),           GNOME_STOCK_MENU_CUT,
		    0, 0, CONTEXT_CUT },
		{ N_("_Copy"),          GNOME_STOCK_MENU_COPY,
		    0, 0, CONTEXT_COPY },
		{ N_("_Paste"),         GNOME_STOCK_MENU_PASTE,
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
		{ N_("_Delete..."),	NULL,
		    CONTEXT_DISPLAY_FOR_CELLS, 0, CONTEXT_DELETE },
		{ N_("_Insert Column(s)"), "Menu_Gnumeric_ColumnAdd",
		    CONTEXT_DISPLAY_FOR_COLS, CONTEXT_DISABLE_FOR_COLS, CONTEXT_INSERT },
		{ N_("_Delete Column(s)"), "Menu_Gnumeric_ColumnDelete",
		    CONTEXT_DISPLAY_FOR_COLS, CONTEXT_DISABLE_FOR_COLS, CONTEXT_DELETE },
		{ N_("_Insert Row(s)"), "Menu_Gnumeric_RowAdd",
		    CONTEXT_DISPLAY_FOR_ROWS, CONTEXT_DISABLE_FOR_ROWS, CONTEXT_INSERT },
		{ N_("_Delete Row(s)"), "Menu_Gnumeric_RowDelete",
		    CONTEXT_DISPLAY_FOR_ROWS, CONTEXT_DISABLE_FOR_ROWS, CONTEXT_DELETE },

		{ N_("Clear Co_ntents"),NULL,
		    0, 0, CONTEXT_CLEAR_CONTENT },

		/* TODO : Add the comment modification elements */
		{ "", NULL, 0, 0, 0 },

		{ N_("_Format Cells..."),GNOME_STOCK_MENU_PREF,
		    0, 0, CONTEXT_FORMAT_CELL },

		/* Column specific (Note some labels duplicate row labels) */
		{ N_("Column _Width..."), "Menu_Gnumeric_ColumnSize",
		    CONTEXT_DISPLAY_FOR_COLS, 0, CONTEXT_COL_WIDTH },
		{ N_("_Hide"),		  "Menu_Gnumeric_ColumnHide",
		    CONTEXT_DISPLAY_FOR_COLS, 0, CONTEXT_COL_HIDE },
		{ N_("_Unhide"),	  "Menu_Gnumeric_ColumnUnhide",
		    CONTEXT_DISPLAY_FOR_COLS, 0, CONTEXT_COL_UNHIDE },

		/* Row specific (Note some labels duplicate col labels) */
		{ N_("_Row Height..."),	  "Menu_Gnumeric_RowSize",
		    CONTEXT_DISPLAY_FOR_ROWS, 0, CONTEXT_ROW_HEIGHT },
		{ N_("_Hide"),		  "Menu_Gnumeric_RowHide",
		    CONTEXT_DISPLAY_FOR_ROWS, 0, CONTEXT_ROW_HIDE },
		{ N_("_Unhide"),	  "Menu_Gnumeric_RowUnhide",
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
	int i;
	SheetControl *sc = (SheetControl *) scg;

	/* there is always a grid 0 */
	if (NULL == scg->pane[0].gcanvas)
		return;
	
	for (i = scg->active_panes; i-- > 0 ; )
		item_cursor_set_visibility (scg->pane[i].cursor.std,
					    is_visible);

	selection_foreach_range (sc->sheet, TRUE, cb_redraw_sel, sc);
}

/***************************************************************************/

#define SO_CLASS(so) SHEET_OBJECT_CLASS(GTK_OBJECT(so)->klass)

/**
 * scg_object_destroy_control_points :
 *
 * Destroys the canvas items used as sheet control points
 */
static void
scg_object_destroy_control_points (SheetControlGUI *scg)
{
	int i;

	if (scg == NULL)
		return;

	i = sizeof (scg->control_points)/sizeof(GnomeCanvasItem *);
	while (i-- > 0) {
		gtk_object_destroy (GTK_OBJECT (scg->control_points [i]));
		scg->control_points [i] = NULL;
	}
}

static void
scg_object_stop_editing (SheetControlGUI *scg, SheetObject *so)
{
	GtkObject *view;

	if (so != NULL) {
		if (so == scg->current_object) {
			view = sheet_object_get_view (so, scg);
			scg_object_destroy_control_points (scg);
			scg->current_object = NULL;
			if (SO_CLASS (so)->set_active != NULL)
				SO_CLASS (so)->set_active (so, view, FALSE);
		}
	}
}

static gboolean
scg_mode_clear (SheetControlGUI *scg)
{
	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), FALSE);

	if (scg->new_object != NULL) {
		gtk_object_unref (GTK_OBJECT (scg->new_object));
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
	GtkObject *view;

	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (scg_mode_clear (scg)) {
		view = sheet_object_get_view (so, scg);
		scg->current_object = so;
		if (SO_CLASS (so)->set_active != NULL)
			SO_CLASS (so)->set_active (so, view, TRUE);
		scg_cursor_visible (scg, FALSE);
		scg_object_update_bbox (scg, so, GNOME_CANVAS_ITEM(view), NULL);
	}
}

/**
 * scg_mode_create_object :
 * @so : The object the needs to be placed
 *
 * Takes a newly created SheetObject that has not yet been realized and
 * prepares to place it on the sheet.
 */
void
scg_mode_create_object (SheetControlGUI *scg, SheetObject *so)
{
	g_return_if_fail (IS_SHEET_OBJECT (so));

	if (scg_mode_clear (scg)) {
		scg->new_object = so;
		scg_cursor_visible (scg, FALSE);
	}
}

static void
display_object_menu (SheetObject *so, GnomeCanvasItem *view, GdkEvent *event)
{
	SheetControlGUI *scg = sheet_object_view_control (GTK_OBJECT (view));
	GtkMenu *menu;

	scg_mode_edit_object (scg, so);
	menu = GTK_MENU (gtk_menu_new ());
	SHEET_OBJECT_CLASS (GTK_OBJECT(so)->klass)->populate_menu (so, GTK_OBJECT (view), menu);

	gtk_widget_show_all (GTK_WIDGET (menu));
	gnumeric_popup_menu (menu, &event->button);
}

static void
scg_object_move (SheetControlGUI *scg, SheetObject *so,
		 GnomeCanvasItem *so_view, GtkObject *ctrl_pt,
		 gdouble new_x, gdouble new_y)
{
	int i, idx = GPOINTER_TO_INT (gtk_object_get_user_data (ctrl_pt));
	double new_coords [4], dx, dy;

	dx = new_x - scg->last_x;
	dy = new_y - scg->last_y;
	scg->last_x = new_x;
	scg->last_y = new_y;

	for (i = 4; i-- > 0; )
		new_coords [i] = scg->object_coords [i];

	switch (idx) {
	case 0: new_coords [0] += dx;
		new_coords [1] += dy;
		break;
	case 1: new_coords [1] += dy;
		break;
	case 2: new_coords [1] += dy;
		new_coords [2] += dx;
		break;
	case 3: new_coords [0] += dx;
		break;
	case 4: new_coords [2] += dx;
		break;
	case 5: new_coords [0] += dx;
		new_coords [3] += dy;
		break;
	case 6: new_coords [3] += dy;
		break;
	case 7: new_coords [2] += dx;
		new_coords [3] += dy;
		break;
	case 8: new_coords [0] += dx;
		new_coords [1] += dy;
		new_coords [2] += dx;
		new_coords [3] += dy;
		break;

	default:
		g_warning ("Should not happen %d", idx);
	}

	sheet_object_direction_set (so, new_coords);
	
	/* Tell the object to update its co-ordinates */
	scg_object_update_bbox (scg, so, so_view, new_coords);
}

static gboolean
cb_slide_handler (GnumericCanvas *gcanvas, int col, int row, gpointer user)
{
	int x, y;
	gdouble new_x, new_y;
	SheetControlGUI *scg = gcanvas->scg;
	GtkObject *view = sheet_object_get_view (scg->current_object, scg);

	x = scg_colrow_distance_get (scg, TRUE, gcanvas->first.col, col);
	x += gcanvas->first_offset.col;
	y = scg_colrow_distance_get (scg, FALSE, gcanvas->first.row, row);
	y += gcanvas->first_offset.row;
	gnome_canvas_c2w (GNOME_CANVAS (gcanvas), x, y, &new_x, &new_y);
	scg_object_move (scg, scg->current_object, GNOME_CANVAS_ITEM (view),
			 user, new_x, new_y);

	return TRUE;
}

/**
 * cb_control_point_event :
 *
 * Event handler for the control points.
 * Index & cursor type are stored as user data associated with the CanvasItem
 */
static int
cb_control_point_event (GnomeCanvasItem *ctrl_pt, GdkEvent *event,
			GnomeCanvasItem *so_view)
{
	SheetObject *so = sheet_object_view_obj (GTK_OBJECT (so_view));
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (ctrl_pt->canvas);
	SheetControlGUI *scg = gcanvas->scg;
	WorkbookControl *wbc = WORKBOOK_CONTROL (scg_get_wbcg (scg));
	gint i;

	switch (event->type) {
	case GDK_ENTER_NOTIFY: {
		gpointer p = gtk_object_get_data (GTK_OBJECT (ctrl_pt),
						  "cursor");
		e_cursor_set_widget (ctrl_pt->canvas, GPOINTER_TO_UINT (p));
		break;
	}

	case GDK_BUTTON_RELEASE:
		if (scg->drag_object != so)
			return FALSE;

		cmd_move_object (wbc, so->sheet, 
				 sheet_object_view_obj (GTK_OBJECT (so_view)),
				 scg->initial_coords, scg->object_coords);
		gnm_canvas_slide_stop (gcanvas);
		scg->drag_object = NULL;
		gnome_canvas_item_ungrab (ctrl_pt, event->button.time);
		sheet_object_position (so, NULL);
		break;

	case GDK_BUTTON_PRESS:
		gnm_canvas_slide_stop (gcanvas);

		switch (event->button.button) {
		case 1:
		case 2: scg->drag_object = so;
			gnome_canvas_item_grab (ctrl_pt,
				GDK_POINTER_MOTION_MASK |
				GDK_BUTTON_RELEASE_MASK,
				NULL, event->button.time);
			for (i = 0; i < 4; i++)
				scg->initial_coords [i] = scg->object_coords[i];
			scg->last_x = event->button.x;
			scg->last_y = event->button.y;
			gnm_canvas_slide_init (gcanvas);
			break;

		case 3: display_object_menu (so, so_view, event);
			break;

		default: /* Ignore mouse wheel events */
			return FALSE;
		}
		break;

	case GDK_MOTION_NOTIFY :
		if (scg->drag_object == NULL)
			break;

		/* TODO : motion is still too granular along the internal axis
		 * when the other axis is external.
		 * eg  drag from middle of sheet down.  The x axis is still internal
		 * onlt the y is external, however, since we are autoscrolling
		 * we are limited to moving with col/row coords, not x,y.
		 * Possible solution would be to change the EXTERIOR_ONLY flag
		 * to something like USE_PIXELS_INSTEAD_OF_COLROW and change
		 * the semantics of the col,row args in the callback.  However,
		 * that is more work than I want to do right now.
		 */
		if (gnm_canvas_handle_motion (GNUMERIC_CANVAS (ctrl_pt->canvas),
					      ctrl_pt->canvas, &event->motion,
					      GNM_SLIDE_X | GNM_SLIDE_Y | GNM_SLIDE_EXTERIOR_ONLY,
					      cb_slide_handler, ctrl_pt))
			scg_object_move (scg, scg->current_object, GNOME_CANVAS_ITEM (so_view),
					 GTK_OBJECT (ctrl_pt), event->motion.x, event->motion.y);
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

/*
 * new_control_point
 * @group:  The canvas group to which this control point belongs
 * @so_view: The sheet object view
 * @idx:    control point index to be created
 * @x:      x coordinate of control point
 * @y:      y coordinate of control point
 *
 * This is used to create a number of control points in a sheet
 * object, the meaning of them is used in other parts of the code
 * to belong to the following locations:
 *
 *     0 -------- 1 -------- 2
 *     |                     |
 *     3                     4
 *     |                     |
 *     5 -------- 6 -------- 7
 */
static GnomeCanvasItem *
new_control_point (GtkObject *so_view, int idx, double x, double y,
		   ECursorType ct)
{
	GnomeCanvasItem *item, *so_view_item = GNOME_CANVAS_ITEM (so_view);
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (so_view_item->canvas);

	item = gnome_canvas_item_new (
		gcanvas->object_group,
		gnome_canvas_rect_get_type (),
		"x1",    x - 2,
		"y1",    y - 2,
		"x2",    x + 2,
		"y2",    y + 2,
		"outline_color", "black",
		"fill_color",    "black",
		NULL);

	gtk_signal_connect (GTK_OBJECT (item), "event",
			    GTK_SIGNAL_FUNC (cb_control_point_event),
			    so_view);

	gtk_object_set_user_data (GTK_OBJECT (item), GINT_TO_POINTER (idx));
	gtk_object_set_data (GTK_OBJECT (item), "cursor", GINT_TO_POINTER (ct));

	return item;
}

/**
 * set_item_x_y:
 *
 * Changes the x and y position of the idx-th control point,
 * creating the control point if necessary.
 */
static void
set_item_x_y (SheetControlGUI *scg, GtkObject *so_view, int idx,
	      double x, double y, ECursorType ct)
{
	if (scg->control_points [idx] == NULL)
		scg->control_points [idx] = new_control_point (
			so_view, idx, x, y, ct);
	else
		gnome_canvas_item_set (
		       scg->control_points [idx],
		       "x1", x - 2,
		       "x2", x + 2,
		       "y1", y - 2,
		       "y2", y + 2,
		       NULL);
}

#define normalize_high_low(d1,d2) if (d1<d2) { double tmp=d1; d1=d2; d2=tmp;}

static void
set_acetate_coords (SheetControlGUI *scg, GtkObject *so_view,
		    double l, double t, double r, double b)
{
	GnomeCanvasItem *so_view_item = GNOME_CANVAS_ITEM (so_view);
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (so_view_item->canvas);

	normalize_high_low (r, l);
	normalize_high_low (b, t);

	t -= 10.; b += 10.;
	l -= 10.; r += 10.;
	
	if (scg->control_points [8] == NULL) {
		GnomeCanvasItem *item;
		GtkWidget *event_box = gtk_event_box_new ();

		item = gnome_canvas_item_new (
			gcanvas->object_group,
			gnome_canvas_widget_get_type (),
			"widget", event_box,
			"x",      l,
			"y",      t,
			"width",  r - l + 1.,
			"height", b - t + 1.,
			NULL);
		gtk_signal_connect (GTK_OBJECT (item), "event",
				    GTK_SIGNAL_FUNC (cb_control_point_event),
				    so_view);
		gtk_object_set_user_data (GTK_OBJECT (item), GINT_TO_POINTER (8));
		gtk_object_set_data (GTK_OBJECT (item), "cursor",
				     GINT_TO_POINTER (E_CURSOR_MOVE));

		scg->control_points [8] = item;
	} else
		gnome_canvas_item_set (
		       scg->control_points [8],
		       "x",      l,
		       "y",      t,
		       "width",  r - l + 1.,
		       "height", b - t + 1.,
		       NULL);
}

/**
 * scg_object_update_bbox:
 *
 * @scg : The Sheet control
 * @so : the optional sheet object
 * @so_view: A canvas item representing the view in this control
 * @new_coords : optionally jump the object to new coordinates
 *
 * Re-align the control points so that they appear at the correct verticies for
 * this view of the object.  If the object is not specified
 */
void
scg_object_update_bbox (SheetControlGUI *scg, SheetObject *so,
			GnomeCanvasItem *so_view, double const *new_coords)
{
	double l, t, r ,b;
	GtkObject *so_view_obj;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (so == NULL)
		so = scg->current_object;
	if (so == NULL)
		return;
	g_return_if_fail (IS_SHEET_OBJECT (so));

	so_view_obj = (so_view == NULL)
		? sheet_object_get_view (so, scg) : GTK_OBJECT (so_view);

	if (new_coords != NULL)
		scg_object_calc_position (scg, so, new_coords);
	else
		scg_object_view_position (scg, so, scg->object_coords);
	
	l = scg->object_coords [0];
	t = scg->object_coords [1];
	r = scg->object_coords [2];
	b = scg->object_coords [3];

	/* set the acetate 1st so that the other points
	 * will override it
	 */
	set_acetate_coords (scg, so_view_obj, l, t, r, b);

	set_item_x_y (scg, so_view_obj, 0, l, t,
		      E_CURSOR_SIZE_TL);
	set_item_x_y (scg, so_view_obj, 1, (l + r) / 2, t,
		      E_CURSOR_SIZE_Y);
	set_item_x_y (scg, so_view_obj, 2, r, t,
		      E_CURSOR_SIZE_TR);
	set_item_x_y (scg, so_view_obj, 3, l, (t + b) / 2,
		      E_CURSOR_SIZE_X);
	set_item_x_y (scg, so_view_obj, 4, r, (t + b) / 2,
		      E_CURSOR_SIZE_X);
	set_item_x_y (scg, so_view_obj, 5, l, b,
		      E_CURSOR_SIZE_TR);
	set_item_x_y (scg, so_view_obj, 6, (l + r) / 2, b,
		      E_CURSOR_SIZE_Y);
	set_item_x_y (scg, so_view_obj, 7, r, b,
		      E_CURSOR_SIZE_TL);
}

static int
calc_obj_place (GnumericCanvas *gcanvas, int pixel, gboolean is_col,
		SheetObjectAnchor anchor_type, float *offset)
{
	int origin;
	int colrow;
	ColRowInfo *cri;
	Sheet *sheet = ((SheetControl *) gcanvas->scg)->sheet;

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
	int	i, pixels [4];
	float	fraction [4];
	double	tmp [4];
	Range	range;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));
	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (coords != NULL);

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

	/* FIXME : how to deal with objects and panes ? */
	gcanvas = scg_pane (scg, 0);
	gnome_canvas_w2c (GNOME_CANVAS (gcanvas),
			  tmp [0], tmp [1],
			  pixels +0, pixels + 1);
	gnome_canvas_w2c (GNOME_CANVAS (gcanvas),
			  tmp [2], tmp [3],
			  pixels +2, pixels + 3);
	range.start.col = calc_obj_place (gcanvas, pixels [0], TRUE,
		so->anchor_type [0], fraction + 0);
	range.start.row = calc_obj_place (gcanvas, pixels [1], FALSE,
		so->anchor_type [1], fraction + 1);
	range.end.col = calc_obj_place (gcanvas, pixels [2], TRUE,
		so->anchor_type [2], fraction + 2);
	range.end.row = calc_obj_place (gcanvas, pixels [3], FALSE,
		so->anchor_type [3], fraction + 3);

	sheet_object_range_set (so, &range, fraction, NULL);
}

void
scg_object_view_position (SheetControlGUI *scg, SheetObject *so, double *coords)
{
	SheetObjectDirection direction;
	int pixels [4];
	/* FIXME : how to deal with objects and panes ? */
	GnomeCanvas *canvas = GNOME_CANVAS (scg_pane (scg, 0));

	sheet_object_position_pixels (so, scg, pixels);

	direction = so->direction;
	if (direction == SO_DIR_UNKNOWN)
		direction = SO_DIR_DOWN_RIGHT;

	gnome_canvas_c2w (canvas,
		pixels [direction & SO_DIR_LEFT_MASK  ? 2 : 0],
		pixels [direction & SO_DIR_DOWN_MASK  ? 1 : 3],
		coords +0, coords + 1);
	gnome_canvas_c2w (canvas,
		pixels [direction & SO_DIR_LEFT_MASK  ? 0 : 2],
		pixels [direction & SO_DIR_DOWN_MASK  ? 3 : 1],
		coords +2, coords + 3);
}

static void
cb_sheet_object_view_destroy (GtkObject *view, SheetObject *so)
{
	SheetControlGUI	*scg = sheet_object_view_control (view);

	if (scg) {
		if (scg->current_object == so)
			scg_mode_edit ((SheetControl *) scg);
		else
			scg_object_stop_editing (scg, so);
	}
}

/**
 * cb_sheet_object_canvas_event :
 * @item : The canvas item that recieved the event
 * @event: The event
 * @so   : The sheetobject the itm is a view of.
 *
 * Handle basic events and manipulations of sheet objects.
 */
static int
cb_sheet_object_canvas_event (GnomeCanvasItem *item, GdkEvent *event,
			      SheetObject *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), FALSE);

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		e_cursor_set_widget (item->canvas,
			(so->type == SHEET_OBJECT_ACTION_STATIC)
			? E_CURSOR_ARROW : E_CURSOR_PRESS);
		break;

	case GDK_BUTTON_PRESS: {
		SheetControlGUI *scg = sheet_object_view_control (GTK_OBJECT (item));

		/* Ignore mouse wheel events */
		if (event->button.button > 3)
			return FALSE;

		if (scg->current_object != so)
			scg_mode_edit_object (scg, so);

		if (event->button.button < 3) {
			g_return_val_if_fail (scg->drag_object == NULL, FALSE);
			scg->drag_object = so;

			/* grab the acetate */
			gnome_canvas_item_grab (scg->control_points [8],
						GDK_POINTER_MOTION_MASK |
						GDK_BUTTON_RELEASE_MASK,
						NULL, event->button.time);
			scg->last_x = event->button.x;
			scg->last_y = event->button.y;
		} else
			display_object_menu (so, item, event);
		break;
	}

	default:
		return FALSE;
	}
	return TRUE;
}

/**
 * cb_sheet_object_widget_canvas_event:
 * @widget: The widget it happens on
 * @event:  The event.
 * @item:   The canvas item.
 *
 * Simplified event handler that passes most events to the widget, and steals
 * only pressing Button3
 */
static int
cb_sheet_object_widget_canvas_event (GtkWidget *widget, GdkEvent *event,
				     GnomeCanvasItem *view)
{
	if (event->type == GDK_BUTTON_PRESS && event->button.button == 3) {
		SheetObject *so = sheet_object_view_obj (GTK_OBJECT (view));
		SheetControlGUI *scg = sheet_object_view_control (GTK_OBJECT (view));

		g_return_val_if_fail (so != NULL, FALSE);

		scg_mode_edit_object (scg, so);
		return cb_sheet_object_canvas_event (view, event, so);
	}

	return FALSE;
}

/**
 * scg_object_register :
 *
 * @so : A sheet object
 * @view   : A canvas item acting as a view for @so
 *
 * Setup some standard callbacks for manipulating a view of a sheet object.
 */
void
scg_object_register (SheetObject *so, GnomeCanvasItem *view)
{
	gtk_signal_connect (GTK_OBJECT (view), "event",
			    GTK_SIGNAL_FUNC (cb_sheet_object_canvas_event),
			    so);
	gtk_signal_connect (GTK_OBJECT (view), "destroy",
			    GTK_SIGNAL_FUNC (cb_sheet_object_view_destroy),
			    so);
}

/**
 * scg_object_widget_register :
 *
 * @so : A sheet object
 * @widget : The widget for the sheet object view
 * @view   : A canvas item acting as a view for @so
 *
 * Setup some standard callbacks for manipulating widgets as views of sheet
 * objects.
 */
void
scg_object_widget_register (SheetObject *so, GtkWidget *widget,
			    GnomeCanvasItem *view)
{
	gtk_signal_connect (GTK_OBJECT (widget), "event",
			    GTK_SIGNAL_FUNC (cb_sheet_object_widget_canvas_event),
			    view);
	scg_object_register (so, view);
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
 * Simplistic routine to display the text of a comment in an UGLY popup window.
 * FIXME : this should really bring up another sheetobject with an arrow from
 * it to the comment marker.  However, we lack a decent rich text canvas item
 * until the conversion to pango and the new text box.
 */
void
scg_comment_display (SheetControlGUI *scg, CellComment *cc)
{
	GtkWidget *label;
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
		scg->comment.item = gtk_window_new (GTK_WINDOW_POPUP);
		label = gtk_label_new (cell_comment_text_get (cc));
		gtk_container_add (GTK_CONTAINER (scg->comment.item), label);
		gdk_window_get_pointer (NULL, &x, &y, NULL);
		gtk_widget_set_uposition (scg->comment.item, x+10, y+10);
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

/*************************************************************************/

static void
scg_cursor_bound (SheetControl *sc, Range const *r)
{
	SheetControlGUI *scg = (SheetControlGUI *) sc;
	int i;

	for (i = scg->active_panes ; i-- > 0 ; )
		gnm_pane_cursor_bound_set (scg->pane + i, r);
}

static void
scg_compute_visible_region (SheetControl *sc, gboolean full_recompute)
{
	SheetControlGUI *scg = (SheetControlGUI *) sc;
	int i;

	for (i = scg->active_panes ; i-- > 0 ; )
		gnm_canvas_compute_visible_region (scg->pane[i].gcanvas,
						   full_recompute);
}

void
scg_edit_start (SheetControlGUI *scg)
{
	int i;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	for (i = scg->active_panes ; i-- > 0 ; )
		gnm_pane_edit_start (scg->pane + i);
}

void
scg_edit_stop (SheetControlGUI *scg)
{
	int i;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	scg_rangesel_stop (scg, FALSE);
	for (i = scg->active_panes ; i-- > 0 ; )
		gnm_pane_edit_stop (scg->pane + i);
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
	int i;

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

	/* FIXME : check the semantics of the range change
	 * these may be in the wrong order.
	 * We'll also need to name selections containing merged cells
	 * properly.
	 */
	sheet = ((SheetControl *) scg)->sheet;
	expr_entry = wbcg_get_entry_logical (scg->wbcg);
	gnumeric_expr_entry_freeze (expr_entry);
	ic_changed = gnumeric_expr_entry_set_rangesel_from_range (
		expr_entry, r, sheet, scg->rangesel.cursor_pos);
	if (ic_changed)
		gnumeric_expr_entry_get_rangesel (expr_entry, r, NULL);

	last_r = *r;
	sheet_merge_find_container (sheet, r);
	if (!range_equal (&last_r, r)) {
		(void) gnumeric_expr_entry_set_rangesel_from_range (
			expr_entry, r, sheet, scg->rangesel.cursor_pos);
		/* This can't grow the range further */
	}
	gnumeric_expr_entry_thaw (expr_entry);
	for (i = scg->active_panes ; i-- > 0 ; )
		gnm_pane_rangesel_bound_set (scg->pane + i, r);
}

void
scg_rangesel_start (SheetControlGUI *scg, int col, int row)
{
	int i;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (scg->rangesel.active)
		return;

	if (scg->wbcg->rangesel != NULL)
		g_warning ("mis configed rangesel");

	scg->wbcg->rangesel = scg;
	scg->rangesel.active = TRUE;
	scg->rangesel.cursor_pos =
		GTK_EDITABLE (wbcg_get_entry_logical (scg->wbcg))->current_pos;

	for (i = scg->active_panes ; i-- > 0 ;)
		gnm_pane_rangesel_start (scg->pane + i, col, row);
	scg_rangesel_changed (scg, col, row, col, row);
}

void
scg_rangesel_stop (SheetControlGUI *scg, gboolean clear_string)
{
	int i;
	
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	if (!scg->rangesel.active)
		return;
	if (scg->wbcg->rangesel != scg)
		g_warning ("mis configed rangesel");

	scg->wbcg->rangesel = NULL;
	scg->rangesel.active = FALSE;
	for (i = scg->active_panes ; i-- > 0 ; )
		gnm_pane_rangesel_stop (scg->pane + i);

	gnumeric_expr_entry_rangesel_stopped (
		GNUMERIC_EXPR_ENTRY (wbcg_get_entry_logical (scg->wbcg)),
		clear_string);
}

/*
 * scg_cursor_move_to:
 * @scg:      The scg where the cursor is located
 * @col:      The new column for the cursor.
 * @row:      The new row for the cursor.
 * @clear_selection: If set, clear the selection before moving
 *
 *   Moves the sheet cursor to a new location, it clears the selection,
 *   accepts any pending output on the editing line and moves the cell
 *   cursor.
 */
void
scg_cursor_move_to (SheetControlGUI *scg, int col, int row,
		    gboolean clear_selection)
{
	Sheet *sheet = ((SheetControl *) scg)->sheet;

	/*
	 * Please note that the order here is important, as
	 * the sheet_make_cell_visible call might scroll the
	 * canvas, you should do all of your screen changes
	 * in an atomic fashion.
	 *
	 * The code at some point did do the selection change
	 * after the sheet moved, causing flicker -mig
	 *
	 * If you dont know what this means, just mail me.
	 */

	/* Set the cursor BEFORE making it visible to decrease flicker */
	if (wbcg_edit_finish (scg->wbcg, TRUE) == FALSE)
		return;

	if (clear_selection)
		sheet_selection_reset (sheet);

	sheet_cursor_set (sheet, col, row, col, row, col, row, NULL);
	sheet_make_cell_visible (sheet, col, row);

	if (clear_selection)
		sheet_selection_add (sheet, col, row);
}

void
scg_rangesel_extend_to (SheetControlGUI *scg, int col, int row)
{
	int base_col, base_row;

	g_return_if_fail (scg->rangesel.active);

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

	scg_rangesel_changed (scg, base_col, base_row, col, row);
}

void
scg_rangesel_bound (SheetControlGUI *scg,
		    int base_col, int base_row,
		    int move_col, int move_row)
{
	g_return_if_fail (scg->rangesel.active);
	scg_rangesel_changed (scg, base_col, base_row, move_col, move_row);
}

void
scg_rangesel_move (SheetControlGUI *scg, int n, gboolean jump_to_bound,
		   gboolean horiz)
{
	Sheet *sheet = ((SheetControl *) scg)->sheet;
	CellPos tmp;

	if (!scg->rangesel.active)
		scg_rangesel_start (scg,
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

	if (horiz)
		tmp.col = sheet_find_boundary_horizontal (sheet,
			tmp.col, tmp.row, tmp.row,
			n, jump_to_bound);
	else
		tmp.row = sheet_find_boundary_vertical (sheet,
			tmp.col, tmp.row, tmp.col,
			n, jump_to_bound);

	scg_cursor_move_to (scg, tmp.col, tmp.row, TRUE);
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
	CellPos tmp = sheet->cursor.move_corner;

	if (horiz)
		tmp.col = sheet_find_boundary_horizontal (sheet,
			tmp.col, tmp.row, sheet->cursor.base_corner.row,
			n, jump_to_bound);
	else
		tmp.row = sheet_find_boundary_vertical (sheet,
			tmp.col, tmp.row, sheet->cursor.base_corner.col,
			n, jump_to_bound);

	sheet_selection_extend_to (sheet, tmp.col, tmp.row);
	sheet_make_cell_visible (sheet, tmp.col, tmp.row);
}

void
scg_take_focus (SheetControlGUI *scg)
{
	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	gtk_window_set_focus (wb_control_gui_toplevel (scg->wbcg),
			      GTK_WIDGET (scg_pane (scg, 0)));
}

void
scg_colrow_resize_stop (SheetControlGUI *scg)
{
	int i;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	for (i = scg->active_panes; i-- > 0 ; )
		gnm_pane_colrow_resize_stop (scg->pane + i);
}

void
scg_colrow_resize_start	(SheetControlGUI *scg,
			 gboolean is_cols, int resize_first)
{
	int i;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	for (i = scg->active_panes; i-- > 0 ; )
		gnm_pane_colrow_resize_start (scg->pane + i,
					      is_cols, resize_first);
}

void
scg_colrow_resize_move (SheetControlGUI *scg,
			gboolean is_cols, int pos)
{
	int i;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	for (i = scg->active_panes; i-- > 0 ; )
		gnm_pane_colrow_resize_move (scg->pane + i, is_cols, pos);
}

void
scg_special_cursor_start (SheetControlGUI *scg, int style, int button)
{
	int i;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	for (i = scg->active_panes; i-- > 0 ; )
		gnm_pane_special_cursor_start (scg->pane + i, style, button);
}

void
scg_special_cursor_stop (SheetControlGUI *scg)
{
	int i;

	g_return_if_fail (IS_SHEET_CONTROL_GUI (scg));

	for (i = scg->active_panes; i-- > 0 ; )
		gnm_pane_special_cursor_stop (scg->pane + i);
}

gboolean
scg_special_cursor_bound_set (SheetControlGUI *scg, Range const *r)
{
	int i;
	gboolean changed = FALSE;

	g_return_val_if_fail (IS_SHEET_CONTROL_GUI (scg), FALSE);

	for (i = scg->active_panes; i-- > 0 ; )
		changed |= gnm_pane_special_cursor_bound_set (scg->pane + i, r);
	return changed;
}

static void
scg_class_init (GtkObjectClass *object_class)
{
	SheetControlClass *sc_class = SHEET_CONTROL_CLASS (object_class);

	g_return_if_fail (sc_class != NULL);
	
	scg_parent_class = gtk_type_class (sheet_control_get_type ());
	object_class->destroy = scg_destroy;

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
}

E_MAKE_TYPE (sheet_control_gui, "SheetControlGUI", SheetControlGUI,
	     scg_class_init, scg_init, SHEET_CONTROL_TYPE)
