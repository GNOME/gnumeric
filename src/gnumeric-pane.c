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

#include <libfoocanvas/foo-canvas-line.h>
#include <libfoocanvas/foo-canvas-rect-ellipse.h>
#include <gal/widgets/e-cursors.h>
#include <math.h>
#define GNUMERIC_ITEM "GnmPane"
#include "item-debug.h"

static void
gnumeric_pane_realized (GtkWidget *widget, gpointer ignored)
{
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);
}

static void
gnumeric_pane_header_init (GnumericPane *pane, SheetControlGUI *scg,
			   gboolean is_col_header)
{
	FooCanvas *canvas = gnm_simple_canvas_new (scg);
	FooCanvasGroup *group = FOO_CANVAS_GROUP (canvas->root);
	FooCanvasItem *item = foo_canvas_item_new (group,
		item_bar_get_type (),
		"ItemBar::GnumericCanvas", pane->gcanvas,
		"ItemBar::IsColHeader", is_col_header,
		NULL);

	/* give a non-constraining default in case something scrolls before we
	 * are realized
	 */
	foo_canvas_set_scroll_region (canvas,
		0, 0, GNUMERIC_CANVAS_FACTOR_X, GNUMERIC_CANVAS_FACTOR_Y);
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

	g_signal_connect (G_OBJECT (canvas),
		"realize",
		G_CALLBACK (gnumeric_pane_realized), NULL);
}

void
gnm_pane_init (GnumericPane *pane, SheetControlGUI *scg,
	       gboolean col_headers, gboolean row_headers, int index)
{
	FooCanvasItem	 *item;
	FooCanvasGroup *gcanvas_group;
	Sheet *sheet;
	Range r;
	int i;

	g_return_if_fail (!pane->is_active);

	pane->gcanvas   = gnm_canvas_new (scg, pane);
	pane->index     = index;
	pane->is_active = TRUE;

	gcanvas_group = FOO_CANVAS_GROUP (FOO_CANVAS (pane->gcanvas)->root);
	item = foo_canvas_item_new (gcanvas_group,
		item_grid_get_type (),
		"ItemGrid::SheetControlGUI", scg,
		NULL);
	pane->grid = ITEM_GRID (item);

	item = foo_canvas_item_new (gcanvas_group,
		item_cursor_get_type (),
		"ItemCursor::SheetControlGUI", scg,
		NULL);
	pane->cursor.std = ITEM_CURSOR (item);
	gnm_pane_cursor_bound_set (pane, range_init (&r, 0, 0, 0, 0)); /* A1 */

	pane->editor = NULL;
	pane->cursor.rangesel = NULL;
	pane->cursor.special = NULL;
	pane->cursor.rangehighlight = NULL;
	pane->anted_cursors = NULL;

	if (col_headers)
		gnumeric_pane_header_init (pane, scg, TRUE);
	else
		pane->col.canvas = NULL;

	if (row_headers)
		gnumeric_pane_header_init (pane, scg, FALSE);
	else
		pane->row.canvas = NULL;

	pane->drag_object = NULL;
	i = G_N_ELEMENTS (pane->control_points);
	while (i-- > 0)
		pane->control_points[i] = NULL;

	/* create views for the sheet objects */
	sheet = sc_sheet (SHEET_CONTROL (scg));
	if (sheet != NULL) {
		GList *ptr;
		for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next)
			sheet_object_new_view (ptr->data, SHEET_CONTROL (scg),
					       (gpointer)pane);
	}
	pane->cursor_type = E_CURSOR_FAT_CROSS;
}

void
gnm_pane_release (GnumericPane *pane)
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

	/* Be anal just in case we somehow manage to remove a pane
	 * unexpectedly.
	 */
	pane->grid = NULL;
	pane->editor = NULL;
	pane->cursor.std = pane->cursor.rangesel = pane->cursor.special = pane->cursor.rangehighlight = NULL;
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
	foo_canvas_item_set (FOO_CANVAS_ITEM (pane->grid),
			     "ItemGrid::Bound", &r,
			     NULL);
}

/****************************************************************************/

void
gnm_pane_colrow_resize_start (GnumericPane *pane,
			      gboolean is_cols, int resize_pos)
{
	SheetControlGUI const *scg;
	GnmCanvas const *gcanvas;
	FooCanvasPoints *points;
	FooCanvasItem *item;
	double zoom;

	g_return_if_fail (pane != NULL);
	g_return_if_fail (pane->colrow_resize.guide  == NULL);
	g_return_if_fail (pane->colrow_resize.start  == NULL);
	g_return_if_fail (pane->colrow_resize.points == NULL);

	gcanvas = pane->gcanvas;
	scg = gcanvas->simple.scg;
	zoom = FOO_CANVAS (gcanvas)->pixels_per_unit;

	points = pane->colrow_resize.points = foo_canvas_points_new (2);
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
	item = foo_canvas_item_new (pane->gcanvas->object_group,
				      FOO_TYPE_CANVAS_LINE,
				      "points", points,
				      "fill_color", "black",
				      "width_pixels", 1,
				      NULL);
	pane->colrow_resize.start = GTK_OBJECT (item);

	item = foo_canvas_item_new (pane->gcanvas->object_group,
				      FOO_TYPE_CANVAS_LINE,
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
		foo_canvas_points_free (pane->colrow_resize.points);
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
	FooCanvasItem *resize_guide;
	FooCanvasPoints *points;
	double zoom;

	g_return_if_fail (pane != NULL);

	resize_guide = FOO_CANVAS_ITEM (pane->colrow_resize.guide);
	points = pane->colrow_resize.points;
	zoom = FOO_CANVAS (pane->gcanvas)->pixels_per_unit;

	if (is_cols)
		points->coords [0] = points->coords [2] = resize_pos / zoom;
	else
		points->coords [1] = points->coords [3] = resize_pos / zoom;

	foo_canvas_item_set (resize_guide,
			       "points",  points,
			       NULL);
}

/****************************************************************************/

void
gnm_pane_reposition_cursors (GnumericPane *pane)
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
	FooCanvas *canvas = FOO_CANVAS (pane->gcanvas);
	FooCanvasItem *tmp;
	FooCanvasGroup *group = FOO_CANVAS_GROUP (canvas->root);
	SheetControl *sc = (SheetControl *) pane->gcanvas->simple.scg;

	g_return_if_fail (pane->cursor.rangesel == NULL);

	/* Hide the primary cursor while the range selection cursor is visible
	 * and we are selecting on a different sheet than the expr being edited
	 */
	if (sc_sheet (sc) != wb_control_cur_sheet (sc_wbc(sc)))
		item_cursor_set_visibility (pane->cursor.std, FALSE);

	tmp = foo_canvas_item_new (group,
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
	FooCanvasItem *item;
	FooCanvas *canvas = FOO_CANVAS (pane->gcanvas);

	g_return_if_fail (pane->cursor.special == NULL);
	item = foo_canvas_item_new (
		FOO_CANVAS_GROUP (canvas->root),
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
	GnmCanvas const *gcanvas = pane->gcanvas;
	SheetView const *sv = sc_view (SHEET_CONTROL (gcanvas->simple.scg));
	CellPos const *edit_pos;

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

void
gnm_pane_object_stop_editing (GnumericPane *pane)
{
	int i = G_N_ELEMENTS (pane->control_points);
	while (i-- > 0) {
		gtk_object_destroy (GTK_OBJECT (pane->control_points [i]));
		pane->control_points [i] = NULL;
	}
}

#define CTRL_PT_SIZE		4
#define CTRL_PT_OUTLINE		2
/* space for 2 halves and a full */
#define CTRL_PT_TOTAL_SIZE 	(CTRL_PT_SIZE*4 + CTRL_PT_OUTLINE*2)

static void
gnm_pane_object_move (SheetControlGUI *scg, SheetObject *so,
		      GtkObject *ctrl_pt,
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

	/* moving any of the points other than the overlay resizes */
	if (idx != 8)
		scg->object_was_resized = TRUE;
	sheet_object_direction_set (so, new_coords);

	/* Tell the object to update its co-ordinates */
	scg_object_update_bbox (scg, so, new_coords);
}

static gboolean
cb_slide_handler (GnmCanvas *gcanvas, int col, int row, gpointer user)
{
	int x, y;
	gdouble new_x, new_y;
	SheetControlGUI *scg = gcanvas->simple.scg;

	x = scg_colrow_distance_get (scg, TRUE, gcanvas->first.col, col);
	x += gcanvas->first_offset.col;
	y = scg_colrow_distance_get (scg, FALSE, gcanvas->first.row, row);
	y += gcanvas->first_offset.row;
	foo_canvas_c2w (FOO_CANVAS (gcanvas), x, y, &new_x, &new_y);
	gnm_pane_object_move (scg, scg->current_object, user, new_x, new_y);

	return TRUE;
}

static void
display_object_menu (SheetObject *so, FooCanvasItem *view, GdkEvent *event)
{
	GtkMenu *menu;
	SheetControlGUI *scg =
		SHEET_CONTROL_GUI (sheet_object_view_control (G_OBJECT (view)));

	scg_mode_edit_object (scg, so);
	menu = GTK_MENU (gtk_menu_new ());
	SHEET_OBJECT_CLASS (G_OBJECT_GET_CLASS(so))->populate_menu (so, G_OBJECT (view), menu);

	gtk_widget_show_all (GTK_WIDGET (menu));
	gnumeric_popup_menu (menu, &event->button);
}

/**
 * cb_control_point_event :
 *
 * Event handler for the control points.
 * Index & cursor type are stored as user data associated with the CanvasItem
 */
static int
cb_control_point_event (FooCanvasItem *ctrl_pt, GdkEvent *event,
			FooCanvasItem *so_view)
{
	SheetObject *so = sheet_object_view_obj (G_OBJECT (so_view));
	GnumericPane *pane = sheet_object_view_key (G_OBJECT (so_view));
	GnmCanvas *gcanvas = GNM_CANVAS (ctrl_pt->canvas);
	SheetControlGUI *scg = gcanvas->simple.scg;
	WorkbookControl *wbc = WORKBOOK_CONTROL (scg_get_wbcg (scg));

	if (wbcg_edit_has_guru (scg_get_wbcg (scg)))
		return FALSE;

	switch (event->type) {
	case GDK_ENTER_NOTIFY: {
		gpointer p = gtk_object_get_data (GTK_OBJECT (ctrl_pt),
						  "cursor");
		e_cursor_set_widget (ctrl_pt->canvas, GPOINTER_TO_UINT (p));
		if (pane->control_points [8] != ctrl_pt)
			foo_canvas_item_set (ctrl_pt,
				"fill_color",    "green",
				NULL);
		break;
	}
	case GDK_LEAVE_NOTIFY:
		scg_set_display_cursor (scg);
		if (pane->control_points [8] != ctrl_pt)
			foo_canvas_item_set (ctrl_pt,
				"fill_color",    "white",
				NULL);
		break;

	case GDK_BUTTON_RELEASE:
		if (pane->drag_object != so)
			return FALSE;

		cmd_object_move (wbc, so, &scg->old_anchor,
				 scg->object_was_resized);
		gnm_canvas_slide_stop (gcanvas);
		pane->drag_object = NULL;
		gnm_simple_canvas_ungrab (ctrl_pt, event->button.time);
		sheet_object_update_bounds (so, NULL);
		break;

	case GDK_BUTTON_PRESS:
		gnm_canvas_slide_stop (gcanvas);

		switch (event->button.button) {
		case 1:
		case 2: pane->drag_object = so;
			gnm_simple_canvas_grab (ctrl_pt,
				GDK_POINTER_MOTION_MASK |
				GDK_BUTTON_RELEASE_MASK,
				NULL, event->button.time);
			sheet_object_anchor_cpy (&scg->old_anchor, sheet_object_anchor_get (so));
			scg->object_was_resized = FALSE;
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
		if (pane->drag_object == NULL)
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
		if (gnm_canvas_handle_motion (GNM_CANVAS (ctrl_pt->canvas),
					      ctrl_pt->canvas, &event->motion,
					      GNM_CANVAS_SLIDE_X | GNM_CANVAS_SLIDE_Y | GNM_CANVAS_SLIDE_EXTERIOR_ONLY,
					      cb_slide_handler, ctrl_pt))
			gnm_pane_object_move (scg, scg->current_object,
				GTK_OBJECT (ctrl_pt), event->motion.x, event->motion.y);
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

/**
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
 **/
static FooCanvasItem *
new_control_point (GObject *so_view, int idx, double x, double y,
		   ECursorType ct)
{
	FooCanvasItem *item, *so_view_item = FOO_CANVAS_ITEM (so_view);
	GnmCanvas *gcanvas = GNM_CANVAS (so_view_item->canvas);

	item = foo_canvas_item_new (
		gcanvas->object_group,
		FOO_TYPE_CANVAS_RECT,
		"outline_color", "black",
		"fill_color",    "white",
		"width_pixels",  CTRL_PT_OUTLINE,
		NULL);

	g_signal_connect (G_OBJECT (item),
		"event",
		G_CALLBACK (cb_control_point_event), so_view);

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
set_item_x_y (GnumericPane *pane, GObject *so_view, int idx,
	      double x, double y, ECursorType ct, gboolean visible)
{
	if (pane->control_points [idx] == NULL)
		pane->control_points [idx] = new_control_point (
			so_view, idx, x, y, ct);
	foo_canvas_item_set (
	       pane->control_points [idx],
	       "x1", x - CTRL_PT_SIZE,
	       "y1", y - CTRL_PT_SIZE,
	       "x2", x + CTRL_PT_SIZE,
	       "y2", y + CTRL_PT_SIZE,
	       NULL);
	if (visible)
		foo_canvas_item_show (pane->control_points [idx]);
	else
		foo_canvas_item_hide (pane->control_points [idx]);
}

#define normalize_high_low(d1,d2) if (d1<d2) { double tmp=d1; d1=d2; d2=tmp;}

static void
set_acetate_coords (GnumericPane *pane, GObject *so_view,
		    double l, double t, double r, double b)
{
	FooCanvasItem *so_view_item = FOO_CANVAS_ITEM (so_view);
	GnmCanvas *gcanvas = GNM_CANVAS (so_view_item->canvas);

	normalize_high_low (r, l);
	normalize_high_low (b, t);

	l -= (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2 - 1;
	r += (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2 - 1;
	t -= (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2 - 1;
	b += (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2 - 1;

	if (pane->control_points [8] == NULL) {
		static char diagonal [] = { 0xcc, 0x66, 0x33, 0x99, 0xcc, 0x66, 0x33, 0x99 };
		GdkBitmap *stipple = gdk_bitmap_create_from_data (NULL, diagonal, 8, 8);
		FooCanvasItem *item = foo_canvas_item_new (
			gcanvas->object_group,
			item_acetate_get_type (),
			"fill_color",		NULL,
			"width_pixels",		CTRL_PT_SIZE + CTRL_PT_OUTLINE,
			"outline_color",	"black",
			"outline_stipple",	stipple,
#if 0
			/* work around the screwup in canvas-item-shape that adds a large
			 * border to anything that uses miter (required for gnome-canvas
			 * not foocanvas */
			"join_style",		GDK_JOIN_ROUND,
#endif
			NULL);
		g_object_unref (stipple);
		g_signal_connect (G_OBJECT (item),
			"event",
			G_CALLBACK (cb_control_point_event), so_view);
		gtk_object_set_user_data (GTK_OBJECT (item), GINT_TO_POINTER (8));
		gtk_object_set_data (GTK_OBJECT (item), "cursor",
			GINT_TO_POINTER (E_CURSOR_MOVE));

		pane->control_points [8] = item;
	}
	foo_canvas_item_set (
	       pane->control_points [8],
	       "x1", l,
	       "y1", t,
	       "x2", r,
	       "y2", b,
	       NULL);
}

void
gnm_pane_object_set_bounds (GnumericPane *pane, SheetObject *so,
			    double l, double t, double r, double b)
{
	GObject *so_view_obj = sheet_object_get_view (so, pane);

	g_return_if_fail (so_view_obj != NULL);

	/* set the acetate 1st so that the other points
	 * will override it
	 */
	set_acetate_coords (pane, so_view_obj, l, t, r, b);

	set_item_x_y (pane, so_view_obj, 0, l, t,
		      E_CURSOR_SIZE_TL, TRUE);
	set_item_x_y (pane, so_view_obj, 1, (l + r) / 2, t,
		      E_CURSOR_SIZE_Y, fabs (r-l) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so_view_obj, 2, r, t,
		      E_CURSOR_SIZE_TR, TRUE);
	set_item_x_y (pane, so_view_obj, 3, l, (t + b) / 2,
		      E_CURSOR_SIZE_X, fabs (b-t) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so_view_obj, 4, r, (t + b) / 2,
		      E_CURSOR_SIZE_X, fabs (b-t) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so_view_obj, 5, l, b,
		      E_CURSOR_SIZE_TR, TRUE);
	set_item_x_y (pane, so_view_obj, 6, (l + r) / 2, b,
		      E_CURSOR_SIZE_Y, fabs (r-l) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so_view_obj, 7, r, b,
		      E_CURSOR_SIZE_TL, TRUE);
}

static int
cb_sheet_object_canvas_event (FooCanvasItem *item, GdkEvent *event,
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
		SheetControlGUI *scg =
			SHEET_CONTROL_GUI (sheet_object_view_control (G_OBJECT (item)));

		/* Ignore mouse wheel events */
		if (event->button.button > 3)
			return FALSE;

		if (scg->current_object != so)
			scg_mode_edit_object (scg, so);

		if (event->button.button < 3) {
			GnumericPane *pane = sheet_object_view_key (G_OBJECT (item));

			g_return_val_if_fail (pane->drag_object == NULL, FALSE);
			pane->drag_object = so;

			/* grab the acetate */
			gnm_simple_canvas_grab (pane->control_points [8],
				GDK_POINTER_MOTION_MASK |
				GDK_BUTTON_RELEASE_MASK,
				NULL, event->button.time);
			sheet_object_anchor_cpy (&scg->old_anchor, sheet_object_anchor_get (so));
			scg->object_was_resized = FALSE;
			scg->last_x = event->button.x;
			scg->last_y = event->button.y;
			gnm_canvas_slide_init (GNM_CANVAS (item->canvas));
		} else
			display_object_menu (so, item, event);
		break;
	}

	default:
		return FALSE;
	}
	return TRUE;
}

static void
cb_sheet_object_view_destroyed (GObject *view, SheetObject *so)
{
	SheetControl *sc = sheet_object_view_control (view);
	SheetControlGUI	*scg = SHEET_CONTROL_GUI (sc);

	g_return_if_fail (IS_SHEET_OBJECT (so));
	g_return_if_fail (view != NULL);

	if (scg) {
		if (scg->current_object == so)
			scg_mode_edit ((SheetControl *) scg);
		else
			scg_object_stop_editing (scg, so);
	}
}

static int
cb_sheet_object_widget_canvas_event (GtkWidget *widget, GdkEvent *event,
				     FooCanvasItem *view)
{
	if (event->type == GDK_BUTTON_PRESS && event->button.button == 3) {
		SheetObject *so = sheet_object_view_obj (G_OBJECT (view));
		SheetControlGUI *scg =
			SHEET_CONTROL_GUI (sheet_object_view_control (G_OBJECT (view)));

		g_return_val_if_fail (so != NULL, FALSE);

		scg_mode_edit_object (scg, so);
		return cb_sheet_object_canvas_event (view, event, so);
	}

	return FALSE;
}


/**
 * gnm_pane_object_register :
 *
 * @so : A sheet object
 * @view   : A canvas item acting as a view for @so
 *
 * Setup some standard callbacks for manipulating a view of a sheet object.
 */
void
gnm_pane_object_register (SheetObject *so, FooCanvasItem *view)
{
	g_signal_connect (G_OBJECT (view),
		"event",
		G_CALLBACK (cb_sheet_object_canvas_event), so);
	/* all gui views are gtkobjects */
	g_signal_connect (G_OBJECT (view),
		"destroy",
		G_CALLBACK (cb_sheet_object_view_destroyed), so);
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
 */
void
gnm_pane_widget_register (SheetObject *so, GtkWidget *widget,
			  FooCanvasItem *view)
{
	g_signal_connect (G_OBJECT (widget),
		"event",
		G_CALLBACK (cb_sheet_object_widget_canvas_event), view);
	gnm_pane_object_register (so, view);
}
