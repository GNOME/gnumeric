/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnumeric-pane.c: A convenience wrapper struct to manage the widgets
 *     and supply some utilites for manipulating panes.
 *
 * Author:
 *    Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gnumeric-pane.h"

#include "gnumeric-canvas.h"
#include "gnumeric-simple-canvas.h"
#include "item-bar.h"
#include "item-cursor.h"
#include "item-edit.h"
#include "item-grid.h"
#include "sheet-control-gui.h"
#include "workbook-control.h"
#include "sheet.h"
#include "ranges.h"

static void
canvas_bar_realized (GtkWidget *widget, gpointer ignored)
{
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);
}

static void
gnumeric_pane_header_init (GnumericPane *pane, SheetControlGUI *scg,
			   gboolean is_col_header)
{
	GnomeCanvas *canvas = gnm_simple_canvas_new (scg);
	GnomeCanvasGroup *group = GNOME_CANVAS_GROUP (canvas->root);
	GnomeCanvasItem *item = gnome_canvas_item_new (group,
		item_bar_get_type (),
		"ItemBar::GnumericCanvas", pane->gcanvas,
		"ItemBar::IsColHeader", is_col_header,
		NULL);

	if (is_col_header) {
		pane->col.canvas = canvas;
		pane->col.item = ITEM_BAR (item);
	} else {
		pane->row.canvas = canvas;
		pane->row.item = ITEM_BAR (item);
	}
	pane->colrow_resize.points = NULL;
	pane->colrow_resize.start  = NULL;
	pane->colrow_resize.guide  = NULL;

#if 0
	/* This would be simpler, just scroll the table and the head moves too.
	 * but it will take some cleaning up that I have no time for just now.
	 */
	if (is_col_header)
		gtk_layout_set_hadjustment (GTK_LAYOUT (canvas),
			gtk_layout_get_hadjustment (GTK_LAYOUT (pane->gcanvas)));
	else
		gtk_layout_set_vadjustment (GTK_LAYOUT (canvas),
			gtk_layout_get_vadjustment (GTK_LAYOUT (pane->gcanvas)));
#endif

	gtk_signal_connect (GTK_OBJECT (canvas), "realize",
		canvas_bar_realized, NULL);
}

void
gnm_pane_init (GnumericPane *pane, SheetControlGUI *scg,
	       gboolean headers, int index)
{
	GnomeCanvasItem	 *item;
	GnomeCanvasGroup *gcanvas_group;
	Range r;

	pane->gcanvas = gnumeric_canvas_new (scg, pane);
	pane->index = index;

	gcanvas_group = GNOME_CANVAS_GROUP (GNOME_CANVAS (pane->gcanvas)->root);
	item = gnome_canvas_item_new (gcanvas_group,
		item_grid_get_type (),
		"ItemGrid::SheetControlGUI", scg,
		NULL);
	pane->grid = ITEM_GRID (item);

	item = gnome_canvas_item_new (gcanvas_group,
		item_cursor_get_type (),
		"ItemCursor::SheetControlGUI", scg,
		NULL);
	pane->cursor.std = ITEM_CURSOR (item);
	gnm_pane_cursor_bound_set (pane, range_init (&r, 0, 0, 0, 0)); /* A1 */

	pane->editor = NULL;
	pane->cursor.rangesel = NULL;
	pane->cursor.special = NULL;

	if (headers) {
		gnumeric_pane_header_init (pane, scg, TRUE);
		gnumeric_pane_header_init (pane, scg, FALSE);
	} else
		pane->col.canvas = pane->row.canvas = NULL;
}

void
gnm_pane_release (GnumericPane *pane)
{
	g_return_if_fail (pane->gcanvas != NULL);

	gtk_object_destroy (GTK_OBJECT (pane->gcanvas));
	pane->gcanvas = NULL;

	if (pane->col.canvas != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->col.canvas));
		pane->col.canvas = NULL;
	}

	if (pane->row.canvas != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->row.canvas));
		pane->row.canvas = NULL;
	}
	if (pane->anted_cursors != NULL) {
		g_list_free (pane->anted_cursors);
		pane->anted_cursors = NULL;
	}

	/* Be anal just in case we somehow manage to remove a pane
	 * unexpectedly.
	 */
	pane->grid = NULL;
	pane->editor = NULL;
	pane->cursor.std = pane->cursor.rangesel = pane->cursor.special = NULL;
	pane->colrow_resize.guide = NULL;
	pane->colrow_resize.start = NULL;
	pane->colrow_resize.points = NULL;
}

void
gnm_pane_bound_set (GnumericPane *pane,
		    int start_col, int start_row,
		    int end_col, int end_row)
{
	Range r;

	g_return_if_fail (pane != NULL);
	g_return_if_fail (pane->gcanvas != NULL);

	range_init (&r, start_col, start_row, end_col, end_row);
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (pane->grid),
			       "ItemGrid::Bound", &r,
			       NULL);
}

/****************************************************************************/

void
gnm_pane_colrow_resize_start (GnumericPane *pane,
			      gboolean is_cols, int resize_pos)
{
	SheetControlGUI const *scg;
	GnumericCanvas const *gcanvas;
	GnomeCanvasPoints *points;
	GnomeCanvasItem *item;
	double zoom;

	g_return_if_fail (pane != NULL);
	g_return_if_fail (pane->colrow_resize.guide  == NULL);
	g_return_if_fail (pane->colrow_resize.start  == NULL);
	g_return_if_fail (pane->colrow_resize.points == NULL);

	gcanvas = pane->gcanvas;
	scg = gcanvas->simple.scg;
	zoom = GNOME_CANVAS (gcanvas)->pixels_per_unit;

	points = pane->colrow_resize.points = gnome_canvas_points_new (2);
	if (is_cols) {
		double const x = scg_colrow_distance_get (scg, TRUE,
					0, resize_pos) / zoom;
		points->coords [0] = x;
		points->coords [1] = scg_colrow_distance_get (scg, FALSE,
					0, gcanvas->first.row) / zoom;
		points->coords [2] = x;
		points->coords [3] = scg_colrow_distance_get (scg, FALSE,
					0, gcanvas->last_visible.row+1) / zoom;
	} else {
		double const y = scg_colrow_distance_get (scg, FALSE,
					0, resize_pos) / zoom;
		points->coords [0] = scg_colrow_distance_get (scg, TRUE,
					0, gcanvas->first.col) / zoom;
		points->coords [1] = y;
		points->coords [2] = scg_colrow_distance_get (scg, TRUE,
					0, gcanvas->last_visible.col+1) / zoom;
		points->coords [3] = y;
	}

	/* Position the stationary only.  Guide line is handled elsewhere. */
	item = gnome_canvas_item_new (pane->gcanvas->object_group,
				      gnome_canvas_line_get_type (),
				      "points", points,
				      "fill_color", "black",
				      "width_pixels", 1,
				      NULL);
	pane->colrow_resize.start = GTK_OBJECT (item);

	item = gnome_canvas_item_new (pane->gcanvas->object_group,
				      gnome_canvas_line_get_type (),
				      "fill_color", "black",
				      "width_pixels", 1,
				      NULL);
	pane->colrow_resize.guide = GTK_OBJECT (item);
}

void
gnm_pane_colrow_resize_stop (GnumericPane *pane)
{
	g_return_if_fail (pane != NULL);

	if (pane->colrow_resize.points != NULL) {
		gnome_canvas_points_free (pane->colrow_resize.points);
		pane->colrow_resize.points = NULL;
	}
	if (pane->colrow_resize.start != NULL) {
		gtk_object_destroy (pane->colrow_resize.start);
		pane->colrow_resize.start = NULL;
	}
	if (pane->colrow_resize.guide != NULL) {
		gtk_object_destroy (pane->colrow_resize.guide);
		pane->colrow_resize.guide = NULL;
	}
}

void
gnm_pane_colrow_resize_move (GnumericPane *pane,
			     gboolean is_cols, int resize_pos)
{
	GnomeCanvasItem *resize_guide;
	GnomeCanvasPoints *points;
	double zoom;

	g_return_if_fail (pane != NULL);

	resize_guide = GNOME_CANVAS_ITEM (pane->colrow_resize.guide);
	points = pane->colrow_resize.points;
	zoom = GNOME_CANVAS (pane->gcanvas)->pixels_per_unit;

	if (is_cols)
		points->coords [0] = points->coords [2] = resize_pos / zoom;
	else
		points->coords [1] = points->coords [3] = resize_pos / zoom;

	gnome_canvas_item_set (resize_guide,
			       "points",  points,
			       NULL);
}

/****************************************************************************/

void
gnm_pane_reposition_cursors (GnumericPane *pane)
{
	GList *l;

	item_cursor_reposition (pane->cursor.std);
	if (NULL != pane->cursor.rangesel)
		item_cursor_reposition (pane->cursor.rangesel);
	if (NULL != pane->cursor.special)
		item_cursor_reposition (pane->cursor.special);
	for (l = pane->anted_cursors; l; l = l->next)
		item_cursor_reposition (ITEM_CURSOR (l->data));
}

gboolean
gnm_pane_cursor_bound_set (GnumericPane *pane, Range const *r)
{
	return item_cursor_bound_set (pane->cursor.std, r);
}

/****************************************************************************/

gboolean
gnm_pane_rangesel_bound_set (GnumericPane *pane, Range const *r)
{
	return item_cursor_bound_set (pane->cursor.rangesel, r);
}
void
gnm_pane_rangesel_start (GnumericPane *pane, Range const *r)
{
	GnomeCanvas *canvas = GNOME_CANVAS (pane->gcanvas);
	GnomeCanvasItem *tmp;
	GnomeCanvasGroup *group = GNOME_CANVAS_GROUP (canvas->root);
	SheetControl *sc = (SheetControl *) pane->gcanvas->simple.scg;

	g_return_if_fail (pane->cursor.rangesel == NULL);

	/* Hide the primary cursor while the range selection cursor is visible
	 * and we are selecting on a different sheet than the expr being edited
	 */
	if (sc_sheet (sc) != wb_control_cur_sheet (sc_wbc(sc)))
		item_cursor_set_visibility (pane->cursor.std, FALSE);

	tmp = gnome_canvas_item_new (group,
		item_cursor_get_type (),
		"SheetControlGUI", pane->gcanvas->simple.scg,
		"Style", ITEM_CURSOR_ANTED, NULL);
	pane->cursor.rangesel = ITEM_CURSOR (tmp);
	item_cursor_bound_set (pane->cursor.rangesel, r);

	/* If we are selecting a range on a different sheet this may be NULL */
	if (pane->editor)
		item_edit_disable_highlight (ITEM_EDIT (pane->editor));
}

void
gnm_pane_rangesel_stop (GnumericPane *pane)
{
	g_return_if_fail (pane->cursor.rangesel != NULL);

	gtk_object_destroy (GTK_OBJECT (pane->cursor.rangesel));
	pane->cursor.rangesel = NULL;

	/* If we are selecting a range on a different sheet this may be NULL */
	if (pane->editor)
		item_edit_enable_highlight (ITEM_EDIT (pane->editor));

	/* Make the primary cursor visible again */
	item_cursor_set_visibility (pane->cursor.std, TRUE);

	gnm_canvas_slide_stop (pane->gcanvas);
}

/****************************************************************************/

gboolean
gnm_pane_special_cursor_bound_set (GnumericPane *pane, Range const *r)
{
	return item_cursor_bound_set (pane->cursor.special, r);
}

void
gnm_pane_special_cursor_start (GnumericPane *pane, int style, int button)
{
	GnomeCanvasItem *item;
	GnomeCanvas *canvas = GNOME_CANVAS (pane->gcanvas);

	g_return_if_fail (pane->cursor.special == NULL);
	item = gnome_canvas_item_new (
		GNOME_CANVAS_GROUP (canvas->root),
		item_cursor_get_type (),
		"ItemCursor::SheetControlGUI", pane->gcanvas->simple.scg,
		"ItemCursor::Style", style,
		"ItemCursor::Button", button,
		NULL);
	pane->cursor.special = ITEM_CURSOR (item);
}

void
gnm_pane_special_cursor_stop (GnumericPane *pane)
{
	g_return_if_fail (pane->cursor.special != NULL);

	gtk_object_destroy (GTK_OBJECT (pane->cursor.special));
	pane->cursor.special = NULL;
}

/****************************************************************************/

void
gnm_pane_edit_start (GnumericPane *pane)
{
	GnumericCanvas const *gcanvas = pane->gcanvas;
	Sheet const *sheet = sc_sheet (SHEET_CONTROL (gcanvas->simple.scg));
	CellPos const *edit_pos;

	g_return_if_fail (pane->editor == NULL);

	/* TODO : this could be slicker.
	 * Rather than doing a visibility check here.
	 * we could make item-edit smarter, and have it bound check on the
	 * entire region rather than only its canvas.
	 */
	edit_pos = &sheet->edit_pos;
	if (edit_pos->col >= gcanvas->first.col &&
	    edit_pos->col <= gcanvas->last_visible.col &&
	    edit_pos->row >= gcanvas->first.row &&
	    edit_pos->row <= gcanvas->last_visible.row) {
		GnomeCanvas *canvas = GNOME_CANVAS (gcanvas);
		GnomeCanvasItem *item;

		item = gnome_canvas_item_new (GNOME_CANVAS_GROUP (canvas->root),
			item_edit_get_type (),
			"ItemEdit::SheetControlGUI",     gcanvas->simple.scg,
			NULL);

		pane->editor = ITEM_EDIT (item);
	}
}

void
gnm_pane_edit_stop (GnumericPane *pane)
{
	if (pane->editor != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->editor));
		pane->editor = NULL;
	}
}
