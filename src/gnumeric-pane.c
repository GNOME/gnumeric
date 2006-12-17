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
#include "item-acetate.h"
#include "item-bar.h"
#include "item-cursor.h"
#include "item-edit.h"
#include "item-grid.h"
#include "sheet-control-gui-priv.h"
#include "workbook-control.h"
#include "workbook-edit.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-object-impl.h"
#include "ranges.h"
#include "commands.h"
#include "gui-util.h"

#include <goffice/utils/go-glib-extras.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-line.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkseparatormenuitem.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkdnd.h>
#include <gdk/gdkdisplay.h>
#include <glib/gi18n-lib.h>
#include <math.h>
#define GNUMERIC_ITEM "GnmPane"
#include "item-debug.h"

#undef DEBUG_DND

static void cb_pane_popup_menu (GnmPane *pane);

/**
 * For now, application/x-gnumeric is disabled. It handles neither
 * images nor graphs correctly.
 */
static GtkTargetEntry const drag_types_in[] = {
	{(char *) "GNUMERIC_SAME_PROC", GTK_TARGET_SAME_APP, 0},
	/* {(char *) "application/x-gnumeric", 0, 0}, */
};

static GtkTargetEntry const drag_types_out[] = {
	{(char *) "GNUMERIC_SAME_PROC", GTK_TARGET_SAME_APP, 0},
	{(char *) "application/x-gnumeric", 0, 0},
};

static void
gnm_pane_header_realized (GtkWidget *widget)
{
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);
}

static void
gnumeric_pane_header_init (GnmPane *pane, SheetControlGUI *scg,
			   gboolean is_col_header)
{
	Sheet *sheet;
	FooCanvas *canvas = gnm_simple_canvas_new (scg);
	FooCanvasGroup *group = FOO_CANVAS_GROUP (canvas->root);
	FooCanvasItem *item = foo_canvas_item_new (group,
		item_bar_get_type (),
		"GnumericCanvas", pane->gcanvas,
		"IsColHeader", is_col_header,
		NULL);

	foo_canvas_set_center_scroll_region (canvas, FALSE);
	/* give a non-constraining default in case something scrolls before we
	 * are realized */
	foo_canvas_set_scroll_region (canvas,
		0, 0, GNUMERIC_CANVAS_FACTOR_X, GNUMERIC_CANVAS_FACTOR_Y);
	if (is_col_header) {
		pane->col.canvas = canvas;
		pane->col.item = ITEM_BAR (item);
	} else {
		pane->row.canvas = canvas;
		pane->row.item = ITEM_BAR (item);
	}
	pane->size_guide.points = NULL;
	pane->size_guide.start  = NULL;
	pane->size_guide.guide  = NULL;

	if (NULL != scg &&
	    NULL != (sheet = scg_sheet (scg)) &&
	    fabs (1. - sheet->last_zoom_factor_used) > 1e-6)
		foo_canvas_set_pixels_per_unit (canvas, sheet->last_zoom_factor_used);

	g_signal_connect (G_OBJECT (canvas), "realize",
		G_CALLBACK (gnm_pane_header_realized), NULL);
}

static void
gnm_pane_clear_obj_size_tip (GnmPane *pane)
{
	if (pane->size_tip) {
		gtk_widget_destroy (gtk_widget_get_toplevel (pane->size_tip));
		pane->size_tip = NULL;
	}
}

static void
gnm_pane_display_obj_size_tip (GnmPane *pane, SheetObject const *so)
{
	SheetControlGUI *scg = pane->gcanvas->simple.scg;
	double const *coords = g_hash_table_lookup (scg->selected_objects, so);
	double pts[4];
	char *msg;
	SheetObjectAnchor anchor;

	g_return_if_fail (so != NULL);

	if (pane->size_tip == NULL) {
		GtkWidget *top;
		int x, y;
		pane->size_tip = gnumeric_create_tooltip ();
		top = gtk_widget_get_toplevel (pane->size_tip);
		/* do not use gnumeric_position_tooltip because it places the tip
		 * too close to the mouse and generates LEAVE events */
		gdk_window_get_pointer (NULL, &x, &y, NULL);
		gtk_window_move (GTK_WINDOW (top), x + 10, y + 10);
		gtk_widget_show_all (top);
	}

	g_return_if_fail (pane->size_tip != NULL);

	sheet_object_anchor_cpy	(&anchor, sheet_object_get_anchor (so));
	scg_object_coords_to_anchor (scg, coords, &anchor);
	sheet_object_anchor_to_pts (&anchor, scg_sheet (scg), pts);
	msg = g_strdup_printf (_("%.1f x %.1f pts\n%d x %d pixels"),
		MAX (fabs (pts[2] - pts[0]), 0),
		MAX (fabs (pts[3] - pts[1]), 0),
		MAX ((int)floor (fabs (coords [2] - coords [0]) + 0.5), 0),
		MAX ((int)floor (fabs (coords [3] - coords [1]) + 0.5), 0));
	gtk_label_set_text (GTK_LABEL (pane->size_tip), msg);
	g_free (msg);
}

static void
cb_ctrl_pts_free (GtkObject **ctrl_pts)
{
	int i = 10;
	while (i-- > 0)
		if (ctrl_pts [i] != NULL)
			gtk_object_destroy (ctrl_pts [i]);
	g_free (ctrl_pts);
}

static void
cb_pane_drag_data_received (GtkWidget *widget, GdkDragContext *context,
			    gint x, gint y, GtkSelectionData *selection_data,
			    guint info, guint time, GnmPane *pane)
{
	double wx, wy;
	GnmCanvas *gcanvas = pane->gcanvas;

#ifdef DEBUG_DND
	{
		gchar *target_name = gdk_atom_name (selection_data->target);
		g_print ("drag-data-received - %s\n", target_name);
		g_free (target_name);
	}
#endif

	gnm_canvas_window_to_coord (gcanvas, x, y, &wx, &wy);
	scg_drag_data_received (gcanvas->simple.scg,
		gtk_drag_get_source_widget (context),
		wx, wy, selection_data);
}

static void
cb_pane_drag_data_get (GtkWidget *widget, GdkDragContext *context,
		       GtkSelectionData *selection_data,
		       guint info, guint time,
		       SheetControlGUI *scg)
{
#ifdef DEBUG_DND
	gchar *target_name = gdk_atom_name (selection_data->target);
	g_print ("drag-data-get - %s \n", target_name);
	g_free (target_name);
#endif
	scg_drag_data_get (scg, selection_data);
}

/* Move the rubber bands if we are the source */
static gboolean
cb_pane_drag_motion (GtkWidget *widget, GdkDragContext *context,
		     int x, int y, guint32 time, GnmPane *pane)
{
	GtkWidget *source_widget = gtk_drag_get_source_widget (context);
	SheetControlGUI *scg = GNM_CANVAS (widget)->simple.scg;

	if ((IS_GNM_CANVAS (source_widget) &&
	     GNM_CANVAS (source_widget)->simple.scg == scg)) {
		/* same scg */
		GnmCanvas *gcanvas = GNM_CANVAS (widget);
		GdkModifierType mask;
		double wx, wy;

		g_object_set_data (&context->parent_instance,
			"wbcg", scg_wbcg (scg));
		gnm_canvas_window_to_coord (gcanvas, x, y, &wx, &wy);

		gdk_window_get_pointer (gtk_widget_get_parent_window (source_widget),
			NULL, NULL, &mask);
		gnm_pane_objects_drag (GNM_CANVAS (source_widget)->pane, NULL,
			wx, wy, 8, FALSE, (mask & GDK_SHIFT_MASK) != 0);
		gdk_drag_status (context, 
				 (mask & GDK_CONTROL_MASK) != 0 ? GDK_ACTION_COPY : GDK_ACTION_MOVE, 
				 time);
	}
	return TRUE;
}

static void
cb_pane_drag_end (GtkWidget *widget, GdkDragContext *context,
		  GnmPane *source_pane)
{
	/* sync the ctrl-pts with the object in case the drag was canceled. */
	gnm_pane_objects_drag (source_pane, NULL,
		source_pane->drag.origin_x,
		source_pane->drag.origin_y,
		8, FALSE, FALSE);
	source_pane->drag.had_motion = FALSE;
}

/**
 * Move the rubber bands back to original position when curser leaves
 * the scg, but not when it moves to another pane. We use object data,
 * and rely on gtk sending drag_move to the new widget before sending
 * drag_leave to the old one.
 */
static void
cb_pane_drag_leave (GtkWidget *widget, GdkDragContext *context,
		    guint32 time, GnmPane *pane)
{
	GtkWidget *source_widget = gtk_drag_get_source_widget (context);
	GnmPane *source_pane;
	WorkbookControlGUI *wbcg;
  
	if (!source_widget || !IS_GNM_CANVAS (source_widget)) return;

	source_pane = GNM_CANVAS (source_widget)->pane;

	wbcg = scg_wbcg (source_pane->gcanvas->simple.scg);
	if (wbcg == g_object_get_data (&context->parent_instance, "wbcg"))
		return;

	gnm_pane_objects_drag (source_pane, NULL,
		source_pane->drag.origin_x,
		source_pane->drag.origin_y,
		8, FALSE, FALSE);
	source_pane->drag.had_motion = FALSE;
}

static void
gnm_pane_drag_dest_init (GnmPane *pane, SheetControlGUI *scg)
{
	GtkWidget *widget = GTK_WIDGET (pane->gcanvas);

	gtk_drag_dest_set (widget, GTK_DEST_DEFAULT_ALL,
			   drag_types_in, G_N_ELEMENTS (drag_types_in),
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);
	gtk_drag_dest_add_uri_targets (widget);
	gtk_drag_dest_add_image_targets (widget);
	gtk_drag_dest_add_text_targets (widget);

	g_object_connect (G_OBJECT (widget),
		"signal::drag-data-received",	G_CALLBACK (cb_pane_drag_data_received), pane,
		"signal::drag-data-get",	G_CALLBACK (cb_pane_drag_data_get),	scg,
		"signal::drag-motion",		G_CALLBACK (cb_pane_drag_motion),	pane,
		"signal::drag-leave",		G_CALLBACK (cb_pane_drag_leave),	pane,
		"signal::drag-end",		G_CALLBACK (cb_pane_drag_end),		pane,
		NULL);
}

static void
cb_pane_init_objs (GnmPane *pane)
{
	/* create views for the sheet objects now that we exist */
	Sheet *sheet = scg_sheet (pane->gcanvas->simple.scg);
	GSList *ptr;

	if (sheet != NULL)
		for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next)
			sheet_object_new_view (ptr->data,
				(SheetObjectViewContainer *)pane);
}

void
gnm_pane_init (GnmPane *pane, SheetControlGUI *scg,
	       gboolean col_headers, gboolean row_headers, int index)
{
	FooCanvasItem	 *item;
	Sheet *sheet;

	g_return_if_fail (!pane->is_active);

	pane->gcanvas   = gnm_canvas_new (scg, pane);
	pane->index     = index;
	pane->is_active = TRUE;
	g_signal_connect_swapped (pane->gcanvas,
		"popup-menu",
		G_CALLBACK (cb_pane_popup_menu), pane);
	g_signal_connect_swapped (G_OBJECT (pane->gcanvas), "realize",
		G_CALLBACK (cb_pane_init_objs), pane);

	if (NULL != scg &&
	    NULL != (sheet = scg_sheet (scg)) &&
	    fabs (1. - sheet->last_zoom_factor_used) > 1e-6)
		foo_canvas_set_pixels_per_unit (FOO_CANVAS (pane->gcanvas),
						sheet->last_zoom_factor_used);

	item = foo_canvas_item_new (pane->gcanvas->grid_items,
		item_grid_get_type (),
		"SheetControlGUI", scg,
		NULL);
	pane->grid = ITEM_GRID (item);

	item = foo_canvas_item_new (pane->gcanvas->grid_items,
		item_cursor_get_type (),
		"SheetControlGUI", scg,
		NULL);
	pane->cursor.std = ITEM_CURSOR (item);

	pane->editor = NULL;
	pane->cursor.rangesel = NULL;
	pane->cursor.special = NULL;
	pane->cursor.rangehighlight = NULL;
	pane->anted_cursors = NULL;
	pane->size_tip = NULL;

	if (col_headers)
		gnumeric_pane_header_init (pane, scg, TRUE);
	else
		pane->col.canvas = NULL;

	if (row_headers)
		gnumeric_pane_header_init (pane, scg, FALSE);
	else
		pane->row.canvas = NULL;

	pane->drag.button = 0;
	pane->drag.ctrl_pts = g_hash_table_new_full (g_direct_hash, g_direct_equal,
		NULL, (GDestroyNotify) cb_ctrl_pts_free);
	gnm_pane_drag_dest_init (pane, scg);
	pane->mouse_cursor = NULL;
}

void
gnm_pane_release (GnmPane *pane)
{
	g_return_if_fail (pane->gcanvas != NULL);
	g_return_if_fail (pane->is_active);

	gtk_object_destroy (GTK_OBJECT (pane->gcanvas));
	pane->gcanvas = NULL;
	pane->is_active = FALSE;

	if (pane->col.canvas != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->col.canvas));
		pane->col.canvas = NULL;
	}

	if (pane->row.canvas != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->row.canvas));
		pane->row.canvas = NULL;
	}
	if (pane->anted_cursors != NULL) {
		g_slist_free (pane->anted_cursors);
		pane->anted_cursors = NULL;
	}

	if (pane->mouse_cursor) {
		gdk_cursor_unref (pane->mouse_cursor);
		pane->mouse_cursor = NULL;
	}
	gnm_pane_clear_obj_size_tip (pane);

	if (pane->drag.ctrl_pts) {
		g_hash_table_destroy (pane->drag.ctrl_pts);
		pane->drag.ctrl_pts = NULL;
	}

	/* Be anal just in case we somehow manage to remove a pane
	 * unexpectedly.
	 */
	pane->grid = NULL;
	pane->editor = NULL;
	pane->cursor.std = pane->cursor.rangesel = pane->cursor.special = pane->cursor.rangehighlight = NULL;
	pane->size_guide.guide = NULL;
	pane->size_guide.start = NULL;
	pane->size_guide.points = NULL;
}

void
gnm_pane_bound_set (GnmPane *pane,
		    int start_col, int start_row,
		    int end_col, int end_row)
{
	GnmRange r;

	g_return_if_fail (pane != NULL);
	g_return_if_fail (pane->gcanvas != NULL);

	range_init (&r, start_col, start_row, end_col, end_row);
	foo_canvas_item_set (FOO_CANVAS_ITEM (pane->grid),
			     "bound", &r,
			     NULL);
}

/****************************************************************************/

void
gnm_pane_size_guide_start (GnmPane *pane, gboolean vert, int colrow, int width)
{
	SheetControlGUI const *scg;
	GnmCanvas const *gcanvas;
	FooCanvasPoints *points;
	double zoom;
	gboolean text_is_rtl;

	g_return_if_fail (pane != NULL);
	g_return_if_fail (pane->size_guide.guide  == NULL);
	g_return_if_fail (pane->size_guide.start  == NULL);
	g_return_if_fail (pane->size_guide.points == NULL);

	gcanvas = pane->gcanvas;
	scg = gcanvas->simple.scg;
	text_is_rtl = scg->sheet_control.sheet->text_is_rtl;
	zoom = FOO_CANVAS (gcanvas)->pixels_per_unit;

	points = pane->size_guide.points = foo_canvas_points_new (2);
	if (vert) {
		double x = scg_colrow_distance_get (scg, TRUE,
					0, colrow) / zoom;
		if (text_is_rtl)
			x = -x;
		points->coords [0] = x;
		points->coords [1] = scg_colrow_distance_get (scg, FALSE,
					0, gcanvas->first.row) / zoom;
		points->coords [2] = x;
		points->coords [3] = scg_colrow_distance_get (scg, FALSE,
					0, gcanvas->last_visible.row+1) / zoom;
	} else {
		double const y = scg_colrow_distance_get (scg, FALSE,
					0, colrow) / zoom;
		points->coords [0] = scg_colrow_distance_get (scg, TRUE,
					0, gcanvas->first.col) / zoom;
		points->coords [1] = y;
		points->coords [2] = scg_colrow_distance_get (scg, TRUE,
					0, gcanvas->last_visible.col+1) / zoom;
		points->coords [3] = y;

		if (text_is_rtl) {
			points->coords [0] *= -1.;
			points->coords [2] *= -1.;
		}
	}

	/* Guideline positioning is done in gnm_pane_size_guide_motion */
	pane->size_guide.guide = foo_canvas_item_new (gcanvas->action_items,
		FOO_TYPE_CANVAS_LINE,
		"fill-color", "black",
		"width-pixels", width,
		NULL);

	/* cheat for now and differentiate between col/row resize and frozen panes
	 * using the width.  Frozen pane guides do not require a start line */
	if (width == 1)
		pane->size_guide.start = foo_canvas_item_new (gcanvas->action_items,
			FOO_TYPE_CANVAS_LINE,
			"points", points,
			"fill-color", "black",
			"width-pixels", 1,
			NULL);
	else {
		static char const dat [] = { 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88 };
		GdkBitmap *stipple = gdk_bitmap_create_from_data (
			GTK_WIDGET (pane->gcanvas)->window, dat, 8, 8);
		foo_canvas_item_set (pane->size_guide.guide, "fill-stipple", stipple, NULL);
		g_object_unref (stipple);
	}
}

void
gnm_pane_size_guide_stop (GnmPane *pane)
{
	g_return_if_fail (pane != NULL);

	if (pane->size_guide.points != NULL) {
		foo_canvas_points_free (pane->size_guide.points);
		pane->size_guide.points = NULL;
	}
	if (pane->size_guide.start != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->size_guide.start));
		pane->size_guide.start = NULL;
	}
	if (pane->size_guide.guide != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->size_guide.guide));
		pane->size_guide.guide = NULL;
	}
}

/**
 * gnm_pane_size_guide_motion
 * @pane : #GnmPane
 * @vert : TRUE for a vertical guide, FALSE for horizontal
 * @guide_pos : in unscaled sheet pixel coords
 *
 * Moves the guide line to @guide_pos.
 * NOTE : gnm_pane_size_guide_start must be called before any calls to
 *	gnm_pane_size_guide_motion
 **/
void
gnm_pane_size_guide_motion (GnmPane *pane, gboolean vert, int guide_pos)
{
	FooCanvasItem *resize_guide = FOO_CANVAS_ITEM (pane->size_guide.guide);
	FooCanvasPoints *points     = pane->size_guide.points;
	double const	 scale	    = 1. / resize_guide->canvas->pixels_per_unit;

	if (vert) {
		gboolean text_is_rtl = pane->gcanvas->simple.scg->sheet_control.sheet->text_is_rtl;
		points->coords [0] = points->coords [2] = scale *
			(text_is_rtl ? -guide_pos : guide_pos);
	} else
		points->coords [1] = points->coords [3] = scale * guide_pos;

	foo_canvas_item_set (resize_guide, "points",  points, NULL);
}

/****************************************************************************/

static void
cb_update_ctrl_pts (SheetObject *so, FooCanvasItem **ctrl_pts, GnmPane *pane)
{
	gnm_pane_object_update_bbox (pane, so);
}

/* Called when the zoom changes */
void
gnm_pane_reposition_cursors (GnmPane *pane)
{
	GSList *l;

	item_cursor_reposition (pane->cursor.std);
	if (NULL != pane->cursor.rangesel)
		item_cursor_reposition (pane->cursor.rangesel);
	if (NULL != pane->cursor.special)
		item_cursor_reposition (pane->cursor.special);
	if (NULL != pane->cursor.rangehighlight)
		item_cursor_reposition (ITEM_CURSOR (pane->cursor.rangehighlight));
	for (l = pane->anted_cursors; l; l = l->next)
		item_cursor_reposition (ITEM_CURSOR (l->data));

	/* ctrl pts do not scale with the zoom, compensate */
	if (pane->drag.ctrl_pts != NULL)
		g_hash_table_foreach (pane->drag.ctrl_pts,
			(GHFunc) cb_update_ctrl_pts, pane);
}

gboolean
gnm_pane_cursor_bound_set (GnmPane *pane, GnmRange const *r)
{
	return item_cursor_bound_set (pane->cursor.std, r);
}

/****************************************************************************/

gboolean
gnm_pane_rangesel_bound_set (GnmPane *pane, GnmRange const *r)
{
	return item_cursor_bound_set (pane->cursor.rangesel, r);
}
void
gnm_pane_rangesel_start (GnmPane *pane, GnmRange const *r)
{
	FooCanvasItem *item;
	SheetControlGUI *scg = pane->gcanvas->simple.scg;

	g_return_if_fail (pane->cursor.rangesel == NULL);

	/* Hide the primary cursor while the range selection cursor is visible
	 * and we are selecting on a different sheet than the expr being edited
	 */
	if (scg_sheet (scg) != wb_control_cur_sheet (scg_wbc (scg)))
		item_cursor_set_visibility (pane->cursor.std, FALSE);

	item = foo_canvas_item_new (pane->gcanvas->grid_items,
		item_cursor_get_type (),
		"SheetControlGUI", scg,
		"style",	ITEM_CURSOR_ANTED,
		NULL);
	pane->cursor.rangesel = ITEM_CURSOR (item);
	item_cursor_bound_set (pane->cursor.rangesel, r);

	/* If we are selecting a range on a different sheet this may be NULL */
	if (pane->editor)
		item_edit_disable_highlight (ITEM_EDIT (pane->editor));
}

void
gnm_pane_rangesel_stop (GnmPane *pane)
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
gnm_pane_special_cursor_bound_set (GnmPane *pane, GnmRange const *r)
{
	return item_cursor_bound_set (pane->cursor.special, r);
}

void
gnm_pane_special_cursor_start (GnmPane *pane, int style, int button)
{
	FooCanvasItem *item;
	FooCanvas *canvas = FOO_CANVAS (pane->gcanvas);

	g_return_if_fail (pane->cursor.special == NULL);
	item = foo_canvas_item_new (
		FOO_CANVAS_GROUP (canvas->root),
		item_cursor_get_type (),
		"SheetControlGUI", pane->gcanvas->simple.scg,
		"style",	   style,
		"button",	   button,
		NULL);
	pane->cursor.special = ITEM_CURSOR (item);
}

void
gnm_pane_special_cursor_stop (GnmPane *pane)
{
	g_return_if_fail (pane->cursor.special != NULL);

	gtk_object_destroy (GTK_OBJECT (pane->cursor.special));
	pane->cursor.special = NULL;
}

void
gnm_pane_mouse_cursor_set (GnmPane *pane, GdkCursor *c)
{
	gdk_cursor_ref (c);
	if (pane->mouse_cursor)
		gdk_cursor_unref (pane->mouse_cursor);
	pane->mouse_cursor = c;
}

/****************************************************************************/

void
gnm_pane_edit_start (GnmPane *pane)
{
	GnmCanvas const *gcanvas = pane->gcanvas;
	SheetView const *sv = scg_view (gcanvas->simple.scg);
	GnmCellPos const *edit_pos;

	g_return_if_fail (pane->editor == NULL);

	/* TODO : this could be slicker.
	 * Rather than doing a visibility check here.
	 * we could make item-edit smarter, and have it bound check on the
	 * entire region rather than only its canvas.
	 */
	edit_pos = &sv->edit_pos;
	if (edit_pos->col >= gcanvas->first.col &&
	    edit_pos->col <= gcanvas->last_visible.col &&
	    edit_pos->row >= gcanvas->first.row &&
	    edit_pos->row <= gcanvas->last_visible.row) {
		FooCanvas *canvas = FOO_CANVAS (gcanvas);
		FooCanvasItem *item;

		item = foo_canvas_item_new (FOO_CANVAS_GROUP (canvas->root),
			item_edit_get_type (),
			"SheetControlGUI", gcanvas->simple.scg,
			NULL);

		pane->editor = ITEM_EDIT (item);
	}
}

void
gnm_pane_edit_stop (GnmPane *pane)
{
	if (pane->editor != NULL) {
		gtk_object_destroy (GTK_OBJECT (pane->editor));
		pane->editor = NULL;
	}
}

void
gnm_pane_objects_drag (GnmPane *pane, SheetObject *so,
		       gdouble new_x, gdouble new_y, int drag_type,
		       gboolean symmetric,gboolean snap_to_grid)
{
 	double dx, dy;
 	dx = new_x - pane->drag.last_x;
 	dy = new_y - pane->drag.last_y;
	pane->drag.had_motion = TRUE;

 	scg_objects_drag (pane->gcanvas->simple.scg, pane->gcanvas,
 		so, &dx, &dy, drag_type, symmetric, snap_to_grid, TRUE);

 	pane->drag.last_x += dx;
 	pane->drag.last_y += dy;
}

#define CTRL_PT_SIZE		4
#define CTRL_PT_OUTLINE		0
/* space for 2 halves and a full */
#define CTRL_PT_TOTAL_SIZE 	(CTRL_PT_SIZE*4 + CTRL_PT_OUTLINE*2)

/* new_x and new_y are in world coords */
static void
gnm_pane_object_move (GnmPane *pane, GObject *ctrl_pt,
		      gdouble new_x, gdouble new_y,
 		      gboolean symmetric,
 		      gboolean snap_to_grid)
{
	int const idx = GPOINTER_TO_INT (g_object_get_data (ctrl_pt, "index"));
	SheetObject *so  = g_object_get_data (G_OBJECT (ctrl_pt), "so");

	gnm_pane_objects_drag (pane, so, new_x, new_y, idx,
			       symmetric, snap_to_grid);
 	if (idx != 8)
		gnm_pane_display_obj_size_tip (pane, so);
}

static gboolean
cb_slide_handler (GnmCanvas *gcanvas, GnmCanvasSlideInfo const *info)
{
	int x, y;
	SheetControlGUI const *scg = gcanvas->simple.scg;
	double const scale = 1. / FOO_CANVAS (gcanvas)->pixels_per_unit;

	x = scg_colrow_distance_get (scg, TRUE, gcanvas->first.col, info->col);
	x += gcanvas->first_offset.col;
	y = scg_colrow_distance_get (scg, FALSE, gcanvas->first.row, info->row);
	y += gcanvas->first_offset.row;

	if (scg->sheet_control.sheet->text_is_rtl)
		x *= -1;

	gnm_pane_object_move (gcanvas->pane, info->user_data,
		x * scale, y * scale, FALSE, FALSE);

	return TRUE;
}

static void
cb_so_menu_activate (GObject *menu, FooCanvasItem *view)
{
	SheetObjectAction const *a = g_object_get_data (menu, "action");
	if (a->func)
		(a->func) (sheet_object_view_get_so (SHEET_OBJECT_VIEW (view)),
			   SHEET_CONTROL (GNM_SIMPLE_CANVAS (view->canvas)->scg));


}

static GtkWidget *
build_so_menu (GnmPane *pane, SheetObjectView *view,
	       GPtrArray const *actions, unsigned *i)
{
	SheetObjectAction const *a;
	GtkWidget *item, *menu = gtk_menu_new ();

	while (*i < actions->len) {
		a = g_ptr_array_index (actions, *i);
		(*i)++;
		if (a->submenu < 0)
			break;
		if (a->icon != NULL) {
			if (a->label != NULL) {
				item = gtk_image_menu_item_new_with_mnemonic (_(a->label));
				gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
					gtk_image_new_from_stock (a->icon, GTK_ICON_SIZE_MENU));
			} else
				item = gtk_image_menu_item_new_from_stock (a->icon, NULL);
		} else if (a->label != NULL)
			item = gtk_menu_item_new_with_mnemonic (_(a->label));
		else
			item = gtk_separator_menu_item_new ();
		if (a->submenu > 0)
			gtk_menu_item_set_submenu (GTK_MENU_ITEM (item),
				build_so_menu (pane, view, actions, i));
		else if (a->label != NULL || a->icon != NULL) { /* not a separator or menu */
			g_object_set_data (G_OBJECT (item), "action", (gpointer)a);
			g_signal_connect_object (G_OBJECT (item), "activate",
				G_CALLBACK (cb_so_menu_activate), view, 0);
		}
		gtk_menu_shell_append (GTK_MENU_SHELL (menu),  item);
	}
	return menu;
}

static void
cb_ptr_array_free (GPtrArray *actions)
{
	g_ptr_array_free (actions, TRUE);
}

/* event and so can be NULL */
static void
display_object_menu (GnmPane *pane, SheetObject *so, GdkEvent *event)
{
	SheetControlGUI *scg = pane->gcanvas->simple.scg;
	GPtrArray *actions = g_ptr_array_new ();
	GtkWidget *menu;
	unsigned i = 0;

	if (NULL != so &&
	    NULL == g_hash_table_lookup (scg->selected_objects, so))
		scg_object_select (scg, so);

	sheet_object_populate_menu (so, actions);

	if (actions->len == 0) {
		g_ptr_array_free (actions, TRUE);
		return;
	}

	menu = build_so_menu (pane,
		sheet_object_get_view (so, (SheetObjectViewContainer *) pane),
		actions, &i);
	g_object_set_data_full (G_OBJECT (menu), "actions", actions,
		(GDestroyNotify)cb_ptr_array_free);
	gtk_widget_show_all (menu);
	gnumeric_popup_menu (GTK_MENU (menu), &event->button);
}

static void
cb_collect_selected_objs (SheetObject *so, double *coords, GSList **accum)
{
	*accum = g_slist_prepend (*accum, so);
}

static void
cb_pane_popup_menu (GnmPane *pane)
{
	SheetControlGUI *scg = pane->gcanvas->simple.scg;

	/* ignore new_object, it is not visible, and should not create a
	 * context menu */
	if (NULL != scg->selected_objects) {
		GSList *accum = NULL;
		g_hash_table_foreach (scg->selected_objects,
			(GHFunc) cb_collect_selected_objs, &accum);
		if (NULL != accum && NULL == accum->next)
			display_object_menu (pane, accum->data, NULL);
		g_slist_free (accum);
	} else {
		/* the popup-menu signal is a binding. the grid almost always
		 * has focus we need to cheat to find out if the user
		 * realllllly wants a col/row header menu */
		gboolean is_col = FALSE;
		gboolean is_row = FALSE;
		GdkWindow *gdk_win = gdk_display_get_window_at_pointer (
			gtk_widget_get_display (GTK_WIDGET (pane->gcanvas)),
			NULL, NULL);

		if (gdk_win != NULL) {
			gpointer gtk_win_void = NULL;
			GtkWindow *gtk_win = NULL;
			gdk_window_get_user_data (gdk_win, &gtk_win_void);
			gtk_win = gtk_win_void;
			if (gtk_win != NULL) {
				if (gtk_win == (GtkWindow *)pane->col.canvas)
					is_col = TRUE;
				else if (gtk_win == (GtkWindow *)pane->row.canvas)
					is_row = TRUE;
			}
		}

		scg_context_menu (scg, NULL, is_col, is_row);
	}
}

static void
control_point_set_cursor (SheetControlGUI const *scg, FooCanvasItem *ctrl_pt)
{
	double const *coords = g_hash_table_lookup (scg->selected_objects,
		g_object_get_data (G_OBJECT (ctrl_pt), "so"));
	gboolean invert_h = coords [0] > coords [2];
	gboolean invert_v = coords [1] > coords [3];
	GdkCursorType cursor;

	switch (GPOINTER_TO_INT (g_object_get_data (G_OBJECT (ctrl_pt), "index"))) {
	case 1: invert_v = !invert_v;
	case 6: cursor = invert_v ? GDK_TOP_SIDE : GDK_BOTTOM_SIDE;
		break;

	case 3: invert_h = !invert_h;
	case 4: cursor = invert_h ? GDK_LEFT_SIDE  : GDK_RIGHT_SIDE;
		break;

	case 2: invert_h = !invert_h;
	case 0: cursor = invert_v
			? (invert_h ? GDK_BOTTOM_RIGHT_CORNER : GDK_BOTTOM_LEFT_CORNER)
			: (invert_h ? GDK_TOP_RIGHT_CORNER : GDK_TOP_LEFT_CORNER);
		break;

	case 7: invert_h = !invert_h;
	case 5: cursor = invert_v
			? (invert_h ? GDK_TOP_RIGHT_CORNER : GDK_TOP_LEFT_CORNER)
			: (invert_h ? GDK_BOTTOM_RIGHT_CORNER : GDK_BOTTOM_LEFT_CORNER);
		break;

	case 8:
	default :
		cursor = GDK_FLEUR;
	}
	gnm_widget_set_cursor_type (GTK_WIDGET (ctrl_pt->canvas), cursor);
}

static void
target_list_add_list (GtkTargetList *targets, GtkTargetList *added_targets)
{
	GList *ptr;
	GtkTargetPair *tp;

	g_return_if_fail (targets != NULL);

	if (added_targets == NULL)
		return;

	for (ptr = added_targets->list; ptr !=  NULL; ptr = ptr->next) {
		tp = (GtkTargetPair *)ptr->data;
		gtk_target_list_add (targets, tp->target, tp->flags, tp->info);
	}
}

/**
 * Drag one or more sheet objects using GTK drag and drop, to the same
 * sheet, another workbook, another gnumeric or a different application.
 */
static void
gnm_pane_drag_begin (GnmPane *pane, SheetObject *so, GdkEvent *event)
{
	GdkDragContext *context;
	GtkTargetList *targets, *im_targets;
	FooCanvas *canvas    = FOO_CANVAS (pane->gcanvas);
	SheetControlGUI *scg = pane->gcanvas->simple.scg;
	GSList *objects;
	SheetObject *imageable = NULL, *exportable = NULL;
	GSList *ptr;
	SheetObject *candidate;

	targets = gtk_target_list_new (drag_types_out,
				  G_N_ELEMENTS (drag_types_out));
	objects = go_hash_keys (scg->selected_objects);
	for (ptr = objects; ptr != NULL; ptr = ptr->next) {
		candidate = SHEET_OBJECT (ptr->data);

		if (exportable == NULL &&
		    IS_SHEET_OBJECT_EXPORTABLE (candidate))
			exportable = candidate;
		if (imageable == NULL &&
		    IS_SHEET_OBJECT_IMAGEABLE (candidate))
			imageable = candidate;
	}

	if (exportable) {
		im_targets = sheet_object_exportable_get_target_list (exportable);
		if (im_targets != NULL) {
			target_list_add_list (targets, im_targets);
			gtk_target_list_unref (im_targets);
		}
	} 
	if (imageable) {
		im_targets = sheet_object_get_target_list (imageable);
		if (im_targets != NULL) {
			target_list_add_list (targets, im_targets);
			gtk_target_list_unref (im_targets);
		}
	}
#ifdef DEBUG_DND
	{
		GList *l;
		g_print ("%d offered formats:\n", g_list_length (targets->list));
		for (l = targets->list; l; l = l->next) {
			GtkTargetPair *pair = (GtkTargetPair *)l->data;
			char *target_name = gdk_atom_name (pair->target);
			g_print ("%s\n", target_name);
			g_free (target_name);
 		}
	}
#endif

	context = gtk_drag_begin (GTK_WIDGET (canvas), targets,
				  GDK_ACTION_COPY | GDK_ACTION_MOVE,
				  pane->drag.button, event);
	gtk_target_list_unref (targets);
	g_slist_free (objects);
}

void
gnm_pane_object_start_resize (GnmPane *pane, GdkEventButton *event,
			      SheetObject *so, int drag_type, gboolean is_creation)
{
	FooCanvasItem **ctrl_pts;

	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (0 <= drag_type);
	g_return_if_fail (drag_type < 9);

	ctrl_pts = g_hash_table_lookup (pane->drag.ctrl_pts, so);

	g_return_if_fail (NULL != ctrl_pts);

	gnm_simple_canvas_grab (ctrl_pts [drag_type],
		GDK_POINTER_MOTION_MASK |
		GDK_BUTTON_PRESS_MASK |
		GDK_BUTTON_RELEASE_MASK,
		NULL, event->time);
	pane->drag.created_objects = is_creation;
	pane->drag.button = event->button;
	pane->drag.last_x = pane->drag.origin_x = event->x;
	pane->drag.last_y = pane->drag.origin_y = event->y;
	pane->drag.had_motion = FALSE;
	gnm_canvas_slide_init (pane->gcanvas);
	gnm_widget_set_cursor_type (GTK_WIDGET (pane->gcanvas), GDK_HAND2);
}

static int
cb_control_point_event (FooCanvasItem *ctrl_pt, GdkEvent *event, GnmPane *pane)
{
	GnmCanvas *gcanvas = GNM_CANVAS (ctrl_pt->canvas);
	SheetControlGUI *scg = gcanvas->simple.scg;
	SheetObject *so;
	int idx;

	if (wbcg_edit_get_guru (scg_wbcg (scg)) != NULL)
		return FALSE;

	so  = g_object_get_data (G_OBJECT (ctrl_pt), "so");
	idx = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (ctrl_pt), "index"));
	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		control_point_set_cursor (scg, ctrl_pt);

		if (idx != 8) {
			foo_canvas_item_set (ctrl_pt,
				"fill-color",    "green",
				NULL);
			gnm_pane_display_obj_size_tip (pane, so);
		}
		break;

	case GDK_LEAVE_NOTIFY:
		scg_set_display_cursor (scg);
		if (idx != 8) {
			foo_canvas_item_set (ctrl_pt,
				"fill-color",    "white",
				NULL);
			gnm_pane_clear_obj_size_tip (pane);
		}
		break;

	case GDK_2BUTTON_PRESS:
		if (pane->drag.button == 1)
			sheet_object_get_editor (so, SHEET_CONTROL (scg));
		else
			break;

	case GDK_BUTTON_RELEASE:
		if (pane->drag.button != (int)event->button.button)
			break;
		pane->drag.button = 0;
		gnm_simple_canvas_ungrab (ctrl_pt, event->button.time);
		gnm_canvas_slide_stop (gcanvas);
		control_point_set_cursor (scg, ctrl_pt);
		if (idx == 8)
			; /* ignore fake event generated by the dnd code */
		else if (pane->drag.had_motion)
			scg_objects_drag_commit	(scg, idx,
				pane->drag.created_objects);
		else if (pane->drag.created_objects && idx == 7) {
			double w, h;
			sheet_object_default_size (so, &w, &h);
			scg_objects_drag (scg, NULL, NULL, &w, &h, 7, FALSE, FALSE, FALSE);
			scg_objects_drag_commit	(scg, 7, TRUE);
		}
		gnm_pane_clear_obj_size_tip (pane);
		break;

	case GDK_BUTTON_PRESS:
		if (0 != pane->drag.button)
			break;
		switch (event->button.button) {
		case 1:
		case 2: gnm_pane_object_start_resize (pane, &event->button, so,  idx, FALSE);
			break;
		case 3: display_object_menu (pane, so, event);
			break;
		default: /* Ignore mouse wheel events */
			return FALSE;
		}
		break;

	case GDK_MOTION_NOTIFY :
		if (0 == pane->drag.button)
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
		if (idx == 8) 
			gnm_pane_drag_begin (pane, so, event);
		else if (gnm_canvas_handle_motion (GNM_CANVAS (ctrl_pt->canvas),
						   ctrl_pt->canvas, &event->motion,
						   GNM_CANVAS_SLIDE_X | GNM_CANVAS_SLIDE_Y |
						   GNM_CANVAS_SLIDE_EXTERIOR_ONLY,
						   cb_slide_handler, ctrl_pt))
			gnm_pane_object_move (pane, G_OBJECT (ctrl_pt),
					      event->motion.x, event->motion.y,
					      (event->button.state & GDK_CONTROL_MASK) != 0,
					      (event->button.state & GDK_SHIFT_MASK) != 0);
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

/**
 * new_control_point
 * @pane: #GnmPane
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
 *
 *     8 == a clear overlay that extends slightly beyond the region
 *     9 == an optional stippled rectangle for moving/resizing expensive
 *         objects
 **/
static FooCanvasItem *
new_control_point (GnmPane *pane, SheetObject *so, int idx, double x, double y)
{
	FooCanvasItem *item;
	GnmCanvas *gcanvas = pane->gcanvas;

	item = foo_canvas_item_new (
		gcanvas->action_items,
		FOO_TYPE_CANVAS_ELLIPSE,
		"outline-color", "black",
		"fill-color",    "white",
		"width-pixels",  CTRL_PT_OUTLINE,
		NULL);

	g_signal_connect (G_OBJECT (item),
		"event",
		G_CALLBACK (cb_control_point_event), pane);

	g_object_set_data (G_OBJECT (item), "index",  GINT_TO_POINTER (idx));
	g_object_set_data (G_OBJECT (item), "so",  so);

	return item;
}

/**
 * set_item_x_y:
 * Changes the x and y position of the idx-th control point,
 * creating the control point if necessary.
 **/
static void
set_item_x_y (GnmPane *pane, SheetObject *so, FooCanvasItem **ctrl_pts,
	      int idx, double x, double y, gboolean visible)
{
	double const scale = 1. / FOO_CANVAS (pane->gcanvas)->pixels_per_unit;
	if (ctrl_pts [idx] == NULL)
		ctrl_pts [idx] = new_control_point (pane, so, idx, x, y);
	foo_canvas_item_set (ctrl_pts [idx],
	       "x1", x - CTRL_PT_SIZE * scale,
	       "y1", y - CTRL_PT_SIZE * scale,
	       "x2", x + CTRL_PT_SIZE * scale,
	       "y2", y + CTRL_PT_SIZE * scale,
	       NULL);
	if (visible)
		foo_canvas_item_show (ctrl_pts [idx]);
	else
		foo_canvas_item_hide (ctrl_pts [idx]);
}

#define normalize_high_low(d1,d2) if (d1<d2) { double tmp=d1; d1=d2; d2=tmp;}

static void
set_acetate_coords (GnmPane *pane, SheetObject *so, FooCanvasItem **ctrl_pts,
		    double l, double t, double r, double b)
{
	if (!sheet_object_rubber_band_directly (so)) {
		if (NULL == ctrl_pts [9]) {
			static char const dashed [] = { 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 };
			GdkBitmap *stipple = gdk_bitmap_create_from_data (
				GTK_WIDGET (pane->gcanvas)->window, dashed, 8, 8);
			ctrl_pts [9] = foo_canvas_item_new (pane->gcanvas->action_items,
				FOO_TYPE_CANVAS_RECT,
				"fill-color",		NULL,
				"width-units",   	1.,
				"outline-color",	"black",
				"outline-stipple",	stipple,
				NULL);
			g_object_unref (stipple);
			foo_canvas_item_lower_to_bottom (ctrl_pts [9]);
		}
		normalize_high_low (r, l);
		normalize_high_low (b, t);
		foo_canvas_item_set (ctrl_pts [9],
		       "x1", l, "y1", t, "x2", r, "y2", b,
		       NULL);
	} else {
		double coords[4];
		SheetObjectView *sov = sheet_object_get_view (so, (SheetObjectViewContainer *)pane);
		if (NULL == sov)
			sov = sheet_object_new_view (so, (SheetObjectViewContainer *)pane);

		coords [0] = l; coords [2] = r; coords [1] = t; coords [3] = b;
		if (NULL != sov)
			sheet_object_view_set_bounds (sov, coords, TRUE);
		normalize_high_low (r, l);
		normalize_high_low (b, t);
	}

	l -= (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2 - 1;
	r += (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2;
	t -= (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2 - 1;
	b += (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2;

	if (NULL == ctrl_pts [8]) {
#undef WITH_STIPPLE_BORDER /* not so pretty */
#ifdef WITH_STIPPLE_BORDER
		static char const diagonal [] = { 0xcc, 0x66, 0x33, 0x99, 0xcc, 0x66, 0x33, 0x99 };
		GdkBitmap *stipple = gdk_bitmap_create_from_data (
			GTK_WIDGET (pane->gcanvas)->window, diagonal, 8, 8);
#endif
		FooCanvasItem *item = foo_canvas_item_new (
			pane->gcanvas->action_items,
			item_acetate_get_type (),
			"fill-color",		NULL,
#ifdef WITH_STIPPLE_BORDER
			"width-units",		(double)(CTRL_PT_SIZE + CTRL_PT_OUTLINE),
			"outline-color",	"black",
			"outline-stipple",	stipple,
#endif
#if 0
			/* work around the screwup in canvas-item-shape that adds a large
			 * border to anything that uses miter (required for gnome-canvas
			 * not foocanvas */
			"join-style",		GDK_JOIN_ROUND,
#endif
			NULL);
#ifdef WITH_STIPPLE_BORDER
		g_object_unref (stipple);
#endif
		g_signal_connect (G_OBJECT (item), "event",
			G_CALLBACK (cb_control_point_event), pane);
		g_object_set_data (G_OBJECT (item), "index",
			GINT_TO_POINTER (8));
		g_object_set_data (G_OBJECT (item), "so", so);

		ctrl_pts [8] = item;
	}
	foo_canvas_item_set (ctrl_pts [8],
	       "x1", l,
	       "y1", t,
	       "x2", r,
	       "y2", b,
	       NULL);
}

void
gnm_pane_object_unselect (GnmPane *pane, SheetObject *so)
{
	gnm_pane_clear_obj_size_tip (pane);
	g_hash_table_remove (pane->drag.ctrl_pts, so);
}

/**
 * gnm_pane_object_update_bbox :
 * @pane : #GnmPane
 * @so : #SheetObject
 *
 * Updates the position and potentially creates control points
 * for manipulating the size/position of @so.
 **/
void
gnm_pane_object_update_bbox (GnmPane *pane, SheetObject *so)
{
	FooCanvasItem **ctrl_pts = g_hash_table_lookup (pane->drag.ctrl_pts, so);
	double const *pts = g_hash_table_lookup (
		pane->gcanvas->simple.scg->selected_objects, so);

	if (ctrl_pts == NULL) {
		ctrl_pts = g_new0 (FooCanvasItem *, 10);
		g_hash_table_insert (pane->drag.ctrl_pts, so, ctrl_pts);
	}

	g_return_if_fail (ctrl_pts != NULL);

	/* set the acetate 1st so that the other points will override it */
	set_acetate_coords (pane, so, ctrl_pts, pts[0], pts[1], pts[2], pts[3]);
	set_item_x_y (pane, so, ctrl_pts, 0, pts[0], pts[1], TRUE);
	set_item_x_y (pane, so, ctrl_pts, 1, (pts[0] + pts[2]) / 2, pts[1],
		      fabs (pts[2]-pts[0]) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so, ctrl_pts, 2, pts[2], pts[1], TRUE);
	set_item_x_y (pane, so, ctrl_pts, 3, pts[0], (pts[1] + pts[3]) / 2,
		      fabs (pts[3]-pts[1]) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so, ctrl_pts, 4, pts[2], (pts[1] + pts[3]) / 2,
		      fabs (pts[3]-pts[1]) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so, ctrl_pts, 5, pts[0], pts[3], TRUE);
	set_item_x_y (pane, so, ctrl_pts, 6, (pts[0] + pts[2]) / 2, pts[3],
		      fabs (pts[2]-pts[0]) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so, ctrl_pts, 7, pts[2], pts[3], TRUE);
}

static int
cb_sheet_object_canvas_event (FooCanvasItem *view, GdkEvent *event,
			      SheetObject *so)
{
	GnmPane	*pane = GNM_CANVAS (view->canvas)->pane;

	g_return_val_if_fail (IS_SHEET_OBJECT (so), FALSE);

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		gnm_widget_set_cursor_type (GTK_WIDGET (view->canvas),
			(so->flags & SHEET_OBJECT_CAN_PRESS) ? GDK_HAND2 : GDK_ARROW);
		return FALSE;

	case GDK_BUTTON_PRESS:
		if (event->button.button > 3)
			return FALSE;

		/* cb_sheet_object_widget_canvas_event calls even if selected */
		if (NULL == g_hash_table_lookup (pane->drag.ctrl_pts, so)) {
			SheetObjectClass *soc =
				G_TYPE_INSTANCE_GET_CLASS (so, SHEET_OBJECT_TYPE, SheetObjectClass);

			if (soc->interactive && event->button.button != 3)
				return FALSE;

			if (!(event->button.state & GDK_SHIFT_MASK))
				scg_object_unselect (pane->gcanvas->simple.scg, NULL);
			scg_object_select (pane->gcanvas->simple.scg, so);
			if (NULL == g_hash_table_lookup (pane->drag.ctrl_pts, so))
				return FALSE;	/* protected ? */
		}

		if (event->button.button < 3)
			gnm_pane_object_start_resize (pane, &event->button, so, 8, FALSE);
		else
			display_object_menu (pane, so, event);
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

static void
cb_sheet_object_view_destroyed (FooCanvasItem *view, SheetObject *so)
{
	if (NULL != view->canvas) {
		GnmPane *pane = GNM_CANVAS (view->canvas)->pane;
		if (pane != NULL &&
		    g_hash_table_lookup (pane->drag.ctrl_pts, so) != NULL)
			scg_object_unselect (GNM_SIMPLE_CANVAS (view->canvas)->scg, so);
	}
}

static int
cb_sheet_object_widget_canvas_event (GtkWidget *widget, GdkEvent *event,
				     FooCanvasItem *view)
{
	if (event->type == GDK_ENTER_NOTIFY ||
	    (event->type == GDK_BUTTON_PRESS && event->button.button == 3))
		return cb_sheet_object_canvas_event (view, event,
			sheet_object_view_get_so (SHEET_OBJECT_VIEW (view)));
	return FALSE;
}

static void
cb_bounds_changed (SheetObject *so, FooCanvasItem *sov)
{
	double coords[4];
	SheetControlGUI *scg = GNM_SIMPLE_CANVAS (sov->canvas)->scg;
	if (NULL != scg->selected_objects &&
	    NULL != g_hash_table_lookup (scg->selected_objects, so))
		return;
	scg_object_anchor_to_coords (scg, sheet_object_get_anchor (so), coords);
	sheet_object_view_set_bounds (SHEET_OBJECT_VIEW (sov),
		coords, so->flags & SHEET_OBJECT_IS_VISIBLE);
}

/**
 * gnm_pane_object_register :
 * @so : A sheet object
 * @view   : A canvas item acting as a view for @so
 * @selectable : Add handlers for selecting and editing the object
 *
 * Setup some standard callbacks for manipulating a view of a sheet object.
 **/
SheetObjectView *
gnm_pane_object_register (SheetObject *so, FooCanvasItem *view, gboolean selectable)
{
	if (selectable) {
		g_signal_connect (view, "event",
			G_CALLBACK (cb_sheet_object_canvas_event), so);
		g_signal_connect (view, "destroy",
			G_CALLBACK (cb_sheet_object_view_destroyed), so);
	}
	g_signal_connect_object (so, "bounds-changed",
		G_CALLBACK (cb_bounds_changed), view, 0);
	return SHEET_OBJECT_VIEW (view);
}

/**
 * gnm_pane_object_widget_register :
 *
 * @so : A sheet object
 * @widget : The widget for the sheet object view
 * @view   : A canvas item acting as a view for @so
 *
 * Setup some standard callbacks for manipulating widgets as views of sheet
 * objects.
 **/
void
gnm_pane_widget_register (SheetObject *so, GtkWidget *w, FooCanvasItem *view)
{
	g_signal_connect (G_OBJECT (w), "event",
		G_CALLBACK (cb_sheet_object_widget_canvas_event), view);

	if (GTK_IS_CONTAINER (w)) {
		GList *ptr, *children = gtk_container_get_children (GTK_CONTAINER (w));
		for (ptr = children ; ptr != NULL; ptr = ptr->next)
			gnm_pane_widget_register (so, ptr->data, view);
		g_list_free (children);
	}
}
