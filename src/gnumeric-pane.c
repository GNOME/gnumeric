/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnumeric-pane.c: A convenience wrapper struct to manage the widgets
 *     and supply some utilites for manipulating panes.
 *
 * Author:
 *    Jody Goldberg (jgoldberg@home.com)
 */
#include <config.h>
#include <gnumeric-pane.h>
#include <gnumeric-sheet.h>
#include <item-bar.h>
#include <sheet-control-gui.h>
#include <ranges.h>

static void
canvas_bar_realized (GtkWidget *widget, gpointer ignored)
{
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);
}

static void
gnumeric_pane_header_init (GnumericPane *pane, gboolean is_col_header)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gnome_canvas_new ());
	GnomeCanvasGroup *group = GNOME_CANVAS_GROUP (canvas->root);
	GnomeCanvasItem *item = gnome_canvas_item_new (group,
		item_bar_get_type (),
		"ItemBar::GnumericSheet", pane->gsheet,
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
	 * but it wil ltakes some cleaning up that I have no time for just now.
	 */
	if (is_col_header)
		gtk_layout_set_hadjustment (GTK_LAYOUT (canvas),
			gtk_layout_get_hadjustment (GTK_LAYOUT (pane->gsheet)));
	else
		gtk_layout_set_vadjustment (GTK_LAYOUT (canvas),
			gtk_layout_get_vadjustment (GTK_LAYOUT (pane->gsheet)));
#endif

	gtk_signal_connect (GTK_OBJECT (canvas), "realize",
		canvas_bar_realized, NULL);
}

void
gnumeric_pane_init (GnumericPane *pane, SheetControlGUI *scg,
		    gboolean headers, int index)
{
	pane->gsheet = gnumeric_sheet_new (scg, pane);
	pane->index = index;

	if (headers) {
		gnumeric_pane_header_init (pane, TRUE);
		gnumeric_pane_header_init (pane, FALSE);
	} else
		pane->col.canvas = pane->row.canvas = NULL;
}

void
gnumeric_pane_release (GnumericPane *pane)
{
	g_return_if_fail (pane->gsheet != NULL);
	gtk_object_destroy (GTK_OBJECT (pane->gsheet));
	pane->gsheet = NULL;

	if (pane->col.canvas != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->col.canvas));
		pane->col.canvas = NULL;
	}

	if (pane->row.canvas != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->row.canvas));
		pane->row.canvas = NULL;
	}
}

void
gnumeric_pane_set_bounds (GnumericPane *pane,
			  int start_col, int start_row,
			  int end_col, int end_row)
{
	Range r;

	g_return_if_fail (pane != NULL);
	g_return_if_fail (pane->gsheet != NULL);

	range_init (&r, start_col, start_row, end_col, end_row);
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (pane->gsheet->item_grid),
			       "ItemGrid::Bound", &r,
			       NULL);
}

void
gnumeric_pane_colrow_resize_start (GnumericPane *pane,
				   gboolean is_cols, int resize_pos)
{
	SheetControlGUI const *scg;
	GnumericSheet const *gsheet;
	GnomeCanvasPoints *points;
	GnomeCanvasItem *item;
	double zoom;

	g_return_if_fail (pane != NULL);
	g_return_if_fail (pane->colrow_resize.guide  == NULL);
	g_return_if_fail (pane->colrow_resize.start  == NULL);
	g_return_if_fail (pane->colrow_resize.points == NULL);

	gsheet = pane->gsheet;
	scg = gsheet->scg;
	zoom = GNOME_CANVAS (gsheet)->pixels_per_unit;

	points = pane->colrow_resize.points = gnome_canvas_points_new (2);
	if (is_cols) {
		double const x = scg_colrow_distance_get (scg, TRUE,
					0, resize_pos) / zoom;
		points->coords [0] = x;
		points->coords [1] = scg_colrow_distance_get (scg, FALSE,
					0, gsheet->first.row) / zoom;
		points->coords [2] = x;
		points->coords [3] = scg_colrow_distance_get (scg, FALSE,
					0, gsheet->last_visible.row+1) / zoom;
	} else {
		double const y = scg_colrow_distance_get (scg, FALSE,
					0, resize_pos) / zoom;
		points->coords [0] = scg_colrow_distance_get (scg, TRUE,
					0, gsheet->first.col) / zoom;
		points->coords [1] = y;
		points->coords [2] = scg_colrow_distance_get (scg, TRUE,
					0, gsheet->last_visible.col+1) / zoom;
		points->coords [3] = y;
	}

	/* Position the stationary only.  Guide line is handled elsewhere. */
	item = gnome_canvas_item_new (pane->gsheet->object_group,
				      gnome_canvas_line_get_type (),
				      "points", points,
				      "fill_color", "black",
				      "width_pixels", 1,
				      NULL);
	pane->colrow_resize.start = GTK_OBJECT (item);

	item = gnome_canvas_item_new (pane->gsheet->object_group,
				      gnome_canvas_line_get_type (),
				      "fill_color", "black",
				      "width_pixels", 1,
				      NULL);
	pane->colrow_resize.guide = GTK_OBJECT (item);
}

void
gnumeric_pane_colrow_resize_end (GnumericPane *pane)
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
gnumeric_pane_colrow_resize_move (GnumericPane *pane,
				  gboolean is_cols, int resize_pos)
{
	GnomeCanvasItem *resize_guide;
	GnomeCanvasPoints *points;
	double zoom;

	g_return_if_fail (pane != NULL);

	resize_guide = GNOME_CANVAS_ITEM (pane->colrow_resize.guide);
	points = pane->colrow_resize.points;
	zoom = GNOME_CANVAS (pane->gsheet)->pixels_per_unit;

	if (is_cols)
		points->coords [0] = points->coords [2] = resize_pos / zoom;
	else
		points->coords [1] = points->coords [3] = resize_pos / zoom;

	gnome_canvas_item_set (resize_guide,
			       "points",  points,
			       NULL);
}
