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
#include <gtk/gtklabel.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkseparatormenuitem.h>
#include <gtk/gtkimage.h>
#include <gdk/gdkdisplay.h>
#include <glib/gi18n.h>
#include <math.h>
#define GNUMERIC_ITEM "GnmPane"
#include "item-debug.h"

static void
gnumeric_pane_realized (GtkWidget *widget, gpointer ignored)
{
	gdk_window_set_back_pixmap (GTK_LAYOUT (widget)->bin_window, NULL, FALSE);
}

static void
gnumeric_pane_header_init (GnmPane *pane, SheetControlGUI *scg,
			   gboolean is_col_header)
{
	FooCanvas *canvas = gnm_simple_canvas_new (scg);
	FooCanvasGroup *group = FOO_CANVAS_GROUP (canvas->root);
	FooCanvasItem *item = foo_canvas_item_new (group,
		item_bar_get_type (),
		"GnumericCanvas", pane->gcanvas,
		"IsColHeader", is_col_header,
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

static void
gnm_pane_clear_obj_size_tip (GnmPane *pane)
{
	if (pane->size_tip) {
		gtk_widget_destroy (gtk_widget_get_toplevel (pane->size_tip));
		pane->size_tip = NULL;
	}
}

static void
gnm_pane_display_obj_size_tip (GnmPane *pane, int idx)
{
	char *msg;
	double pts[4], pixels[4];
	SheetControlGUI *scg = pane->gcanvas->simple.scg;

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
	g_return_if_fail (scg->current_object != NULL);

	sheet_object_position_pts_get (scg->current_object, pts);
	sheet_object_position_pixels_get (scg->current_object,
		SHEET_CONTROL (scg), pixels);
	msg = g_strdup_printf (_("%.1f x %.1f pts\n%d x %d pixels"),
		fabs (pts[3]-pts[1]), fabs (pts[2] - pts[0]),
		(int)floor (fabs (pixels[3]-pixels[1]) + .5),
		(int)floor (fabs (pixels[2] - pixels[0]) + .5));
	gtk_label_set_text (GTK_LABEL (pane->size_tip), msg);
	g_free (msg);
}

static void
cb_pane_popup_menu (GnmPane *pane)
{
	/* the popup-menu signal is a binding. the grid almost always has focus
	 * we need to cheat to find out if the user realllllly wants a col/row
	 * header menu */
	gboolean is_col = FALSE;
	gboolean is_row = FALSE;
	GdkWindow *gdk_win = gdk_display_get_window_at_pointer (
		gtk_widget_get_display (GTK_WIDGET (pane->gcanvas)),
		NULL, NULL);

	if (gdk_win != NULL) {
		GtkWindow *gtk_win = NULL;
		gdk_window_get_user_data (gdk_win, (gpointer *) &gtk_win);
		if (gtk_win != NULL) {
			if (gtk_win == (GtkWindow *)pane->col.canvas)
				is_col = TRUE;
			else if (gtk_win == (GtkWindow *)pane->row.canvas)
				is_row = TRUE;
		}
	}
	scg_context_menu (pane->gcanvas->simple.scg, NULL, is_col, is_row);
}

void
gnm_pane_init (GnmPane *pane, SheetControlGUI *scg,
	       gboolean col_headers, gboolean row_headers, int index)
{
	FooCanvasItem	 *item;
	FooCanvasGroup *gcanvas_group;
	Sheet *sheet;
	GnmRange r;
	int i;

	g_return_if_fail (!pane->is_active);

	pane->gcanvas   = gnm_canvas_new (scg, pane);
	pane->index     = index;
	pane->is_active = TRUE;
	g_signal_connect_swapped (pane->gcanvas,
		"popup-menu",
		G_CALLBACK (cb_pane_popup_menu), pane);

	gcanvas_group = FOO_CANVAS_GROUP (FOO_CANVAS (pane->gcanvas)->root);
	item = foo_canvas_item_new (gcanvas_group,
		item_grid_get_type (),
		"SheetControlGUI", scg,
		NULL);
	pane->grid = ITEM_GRID (item);

	item = foo_canvas_item_new (gcanvas_group,
		item_cursor_get_type (),
		"SheetControlGUI", scg,
		NULL);
	pane->cursor.std = ITEM_CURSOR (item);
	gnm_pane_cursor_bound_set (pane, range_init (&r, 0, 0, 0, 0)); /* A1 */

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

	pane->drag_object = NULL;
	pane->drag_button = 0;
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
gnm_pane_colrow_resize_start (GnmPane *pane,
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
gnm_pane_colrow_resize_stop (GnmPane *pane)
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
gnm_pane_colrow_resize_move (GnmPane *pane,
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
		"style",	ITEM_CURSOR_ANTED,
		NULL);
	pane->cursor.rangesel = ITEM_CURSOR (tmp);
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
	SheetView const *sv = sc_view (SHEET_CONTROL (gcanvas->simple.scg));
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
gnm_pane_object_stop_editing (GnmPane *pane)
{
	int i = G_N_ELEMENTS (pane->control_points);
	while (i-- > 0) {
		gtk_object_destroy (GTK_OBJECT (pane->control_points [i]));
		pane->control_points [i] = NULL;
	}
	gnm_pane_clear_obj_size_tip (pane);
}

#define CTRL_PT_SIZE		4
#define CTRL_PT_OUTLINE		0
/* space for 2 halves and a full */
#define CTRL_PT_TOTAL_SIZE 	(CTRL_PT_SIZE*4 + CTRL_PT_OUTLINE*2)

static void
gnm_pane_object_move (GnmPane *pane, GObject *ctrl_pt,
		      gdouble new_x, gdouble new_y,
		      gboolean symmetric)
{
	SheetControlGUI *scg = pane->gcanvas->simple.scg;
	int i, idx = GPOINTER_TO_INT (g_object_get_data (ctrl_pt, "index"));
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
		if (symmetric) {
			new_coords [2] -= dx;
			new_coords [3] -= dy;
		}
		break;
	case 1: new_coords [1] += dy;
		if (symmetric)
			new_coords [3] -= dy;
		break;
	case 2: new_coords [1] += dy;
		new_coords [2] += dx;
		if (symmetric) {
			new_coords [3] -= dy;
			new_coords [0] -= dx;
		}
		break;
	case 3: new_coords [0] += dx;
		if (symmetric)
			new_coords [2] -= dx;
		break;
	case 4: new_coords [2] += dx;
		if (symmetric)
			new_coords [0] -= dx;
		break;
	case 5: new_coords [0] += dx;
		new_coords [3] += dy;
		if (symmetric) {
			new_coords [2] -= dx;
			new_coords [1] -= dy;
		}
		break;
	case 6: new_coords [3] += dy;
		if (symmetric)
			new_coords [1] -= dy;
		break;
	case 7: new_coords [2] += dx;
		new_coords [3] += dy;
		if (symmetric) {
			new_coords [0] -= dx;
			new_coords [1] -= dy;
		}
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
	sheet_object_direction_set (scg->current_object, new_coords);

	/* Tell the object to update its co-ordinates */
	scg_object_update_bbox (scg, scg->current_object, new_coords);
	if (idx != 8)
		gnm_pane_display_obj_size_tip (pane, idx);
}

static gboolean
cb_slide_handler (GnmCanvas *gcanvas, int col, int row, gpointer ctrl_pt)
{
	int x, y;
	gdouble new_x, new_y;
	SheetControlGUI *scg = gcanvas->simple.scg;

	x = scg_colrow_distance_get (scg, TRUE, gcanvas->first.col, col);
	x += gcanvas->first_offset.col;
	y = scg_colrow_distance_get (scg, FALSE, gcanvas->first.row, row);
	y += gcanvas->first_offset.row;
	foo_canvas_c2w (FOO_CANVAS (gcanvas), x, y, &new_x, &new_y);
	gnm_pane_object_move (gcanvas->pane, ctrl_pt, new_x, new_y, FALSE);

	return TRUE;
}

static void
cb_so_menu_activate (GObject *menu, SheetControlGUI *scg)
{
	SheetObjectAction const *a = g_object_get_data (menu, "action");
	if (a->func)
		(a->func) (scg->current_object, SHEET_CONTROL (scg));
}

static GtkWidget *
build_so_menu (GnmPane *pane, GPtrArray const *actions, unsigned *i)
{
	SheetObjectAction const *a;
	GtkWidget *item, *menu = gtk_menu_new ();
	SheetControlGUI *scg = pane->gcanvas->simple.scg;

	while (*i < actions->len) {
		a = g_ptr_array_index (actions, *i);
		(*i)++;
		if (a->submenu < 0)
			break;
		if (a->icon != NULL) {
			if (a->label != NULL) {
				item = gtk_image_menu_item_new_with_mnemonic (a->label);
				gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
					gtk_image_new_from_stock (a->icon, GTK_ICON_SIZE_MENU));
			} else
				item = gtk_image_menu_item_new_from_stock (a->icon, NULL);
		} else if (a->label != NULL)
			item = gtk_menu_item_new_with_mnemonic (a->label);
		else
			item = gtk_separator_menu_item_new ();
		if (a->submenu > 0)
			gtk_menu_item_set_submenu (GTK_MENU_ITEM (item),
				build_so_menu (pane, actions, i));
		else if (a->label != NULL || a->icon != NULL) { /* not a separator or menu */
			g_object_set_data (G_OBJECT (item), "action", (gpointer)a);
			g_signal_connect_object (G_OBJECT (item), "activate",
				G_CALLBACK (cb_so_menu_activate), scg, 0);
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

static void
display_object_menu (GnmPane *pane, SheetObject *so, GdkEvent *event)
{
	SheetControlGUI *scg = pane->gcanvas->simple.scg;
	GPtrArray *actions = g_ptr_array_new ();
	GtkWidget *menu;
	unsigned i = 0;

	scg_mode_edit_object (scg, so);
	SHEET_OBJECT_CLASS (G_OBJECT_GET_CLASS(so))->populate_menu (so, actions);

	if (actions->len == 0) {
		g_ptr_array_free (actions, TRUE);
		return;
	}

	menu = build_so_menu (pane, actions, &i);
	g_object_set_data_full (G_OBJECT (menu), "actions", actions,
		(GDestroyNotify)cb_ptr_array_free);
	gtk_widget_show_all (menu);
	gnumeric_popup_menu (GTK_MENU (menu), &event->button);
}

static void
control_point_set_cursor (SheetControlGUI const *scg, FooCanvasItem *ctrl_pt)
{
	gboolean invert_h = scg->object_coords [0] > scg->object_coords [2];
	gboolean invert_v = scg->object_coords [1] > scg->object_coords [3];
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

/* Event handler for the control points.  Index is stored user data associated
 * with the CanvasItem */
static int
cb_control_point_event (FooCanvasItem *ctrl_pt, GdkEvent *event, GnmPane *pane)
{
	GnmCanvas *gcanvas = GNM_CANVAS (ctrl_pt->canvas);
	SheetControlGUI *scg = gcanvas->simple.scg;
	SheetObject *so = scg->current_object;

	if (wbcg_edit_get_guru (scg_get_wbcg (scg)) != NULL)
		return FALSE;

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		control_point_set_cursor (scg, ctrl_pt);

		if (pane->control_points [8] != ctrl_pt) {
			foo_canvas_item_set (ctrl_pt,
				"fill_color",    "green",
				NULL);
			gnm_pane_display_obj_size_tip (pane,
				GPOINTER_TO_INT (g_object_get_data (G_OBJECT (ctrl_pt), "index")));
		}
		break;

	case GDK_LEAVE_NOTIFY:
		scg_set_display_cursor (scg);
		if (pane->control_points [8] != ctrl_pt) {
			foo_canvas_item_set (ctrl_pt,
				"fill_color",    "white",
				NULL);
			gnm_pane_clear_obj_size_tip (pane);
		}
		break;

	case GDK_BUTTON_RELEASE:
		if (pane->drag_object != so ||
		    pane->drag_button != event->button.button)
			return FALSE;

		cmd_object_move (WORKBOOK_CONTROL (scg_get_wbcg (scg)),
			so, &scg->old_anchor, scg->object_was_resized);
		gnm_canvas_slide_stop (gcanvas);
		pane->drag_object = NULL;
		pane->drag_button = 0;
		gnm_simple_canvas_ungrab (ctrl_pt, event->button.time);
		control_point_set_cursor (scg, ctrl_pt);
		sheet_object_update_bounds (so, NULL);
		break;

	case GDK_BUTTON_PRESS:
		if (pane->drag_object != NULL)
			return FALSE;

		switch (event->button.button) {
		case 1:
		case 2:
			/* ctrl-click on acetate dups the object */
			if ((event->button.state & GDK_CONTROL_MASK) != 0 &&
			    IS_ITEM_ACETATE (ctrl_pt)) {
				so = sheet_object_dup (so);
				sheet_object_set_sheet (so,
					sc_sheet (SHEET_CONTROL (scg)));
				g_object_unref (G_OBJECT (scg->current_object));
				scg->current_object = so;
				foo_canvas_item_raise_to_top (
					FOO_CANVAS_ITEM (gcanvas->object_group));
			}

			pane->drag_object = so;
			pane->drag_button = event->button.button;
			gnm_simple_canvas_grab (ctrl_pt,
				GDK_POINTER_MOTION_MASK |
				GDK_BUTTON_RELEASE_MASK,
				NULL, event->button.time);
			sheet_object_anchor_cpy (&scg->old_anchor, sheet_object_get_anchor (so));
			scg->object_was_resized = FALSE;
			scg->last_x = event->button.x;
			scg->last_y = event->button.y;
			gnm_canvas_slide_init (gcanvas);
			gnm_widget_set_cursor_type (GTK_WIDGET (ctrl_pt->canvas), GDK_HAND2);
			break;

		case 3: display_object_menu (pane, so, event);
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
			gnm_pane_object_move (pane, G_OBJECT (ctrl_pt),
				event->motion.x, event->motion.y,
				(event->button.state & GDK_CONTROL_MASK) != 0);
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

/**
 * new_control_point
 * @group:  The canvas group to which this control point belongs
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
 **/
static FooCanvasItem *
new_control_point (GnmPane *pane, int idx, double x, double y)
{
	FooCanvasItem *item;
	GnmCanvas *gcanvas = pane->gcanvas;

	item = foo_canvas_item_new (
		gcanvas->object_group,
		FOO_TYPE_CANVAS_ELLIPSE,
		"outline_color", "black",
		"fill_color",    "white",
		"width_pixels",  CTRL_PT_OUTLINE,
		NULL);

	g_signal_connect (G_OBJECT (item),
		"event",
		G_CALLBACK (cb_control_point_event), pane);

	g_object_set_data (G_OBJECT (item), "index",  GINT_TO_POINTER (idx));

	return item;
}

/**
 * set_item_x_y:
 *
 * Changes the x and y position of the idx-th control point,
 * creating the control point if necessary.
 */
static void
set_item_x_y (GnmPane *pane, GObject *so_view, int idx,
	      double x, double y, gboolean visible)
{
	if (pane->control_points [idx] == NULL)
		pane->control_points [idx] = new_control_point (
			pane, idx, x, y);
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
set_acetate_coords (GnmPane *pane, GObject *so_view,
		    double l, double t, double r, double b)
{
	FooCanvasItem *so_view_item = FOO_CANVAS_ITEM (so_view);
	GnmCanvas *gcanvas = GNM_CANVAS (so_view_item->canvas);

	normalize_high_low (r, l);
	normalize_high_low (b, t);

	l -= (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2 - 1;
	r += (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2;
	t -= (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2 - 1;
	b += (CTRL_PT_SIZE + CTRL_PT_OUTLINE) / 2;

	if (pane->control_points [8] == NULL) {
#undef WITH_STIPPLE_BORDER /* not so pretty */
#ifdef WITH_STIPPLE_BORDER
		static char const diagonal [] = { 0xcc, 0x66, 0x33, 0x99, 0xcc, 0x66, 0x33, 0x99 };
		GdkBitmap *stipple = gdk_bitmap_create_from_data (GTK_WIDGET (gcanvas)->window,
								  diagonal, 8, 8);
#endif
		FooCanvasItem *item = foo_canvas_item_new (
			gcanvas->object_group,
			item_acetate_get_type (),
			"fill_color",		NULL,
#ifdef WITH_STIPPLE_BORDER
			"width_units",		(double)(CTRL_PT_SIZE + CTRL_PT_OUTLINE),
			"outline_color",	"black",
			"outline_stipple",	stipple,
#endif
#if 0
			/* work around the screwup in canvas-item-shape that adds a large
			 * border to anything that uses miter (required for gnome-canvas
			 * not foocanvas */
			"join_style",		GDK_JOIN_ROUND,
#endif
			NULL);
#ifdef WITH_STIPPLE_BORDER
		g_object_unref (stipple);
#endif
		g_signal_connect (G_OBJECT (item),
				  "event",
				  G_CALLBACK (cb_control_point_event), pane);
		g_object_set_data (G_OBJECT (item), "index",
				   GINT_TO_POINTER (8));

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
gnm_pane_object_set_bounds (GnmPane *pane, SheetObject *so,
			    double l, double t, double r, double b)
{
	GObject *so_view_obj = sheet_object_get_view (so, pane);

	g_return_if_fail (so_view_obj != NULL);

	/* set the acetate 1st so that the other points
	 * will override it */
	set_acetate_coords (pane, so_view_obj, l, t, r, b);
	set_item_x_y (pane, so_view_obj, 0, l, t, TRUE);
	set_item_x_y (pane, so_view_obj, 1, (l + r) / 2, t,
		      fabs (r-l) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so_view_obj, 2, r, t, TRUE);
	set_item_x_y (pane, so_view_obj, 3, l, (t + b) / 2,
		      fabs (b-t) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so_view_obj, 4, r, (t + b) / 2,
		      fabs (b-t) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so_view_obj, 5, l, b, TRUE);
	set_item_x_y (pane, so_view_obj, 6, (l + r) / 2, b,
		      fabs (r-l) >= CTRL_PT_TOTAL_SIZE);
	set_item_x_y (pane, so_view_obj, 7, r, b, TRUE);
}

static int
cb_sheet_object_canvas_event (FooCanvasItem *view, GdkEvent *event,
			      SheetObject *so)
{
	g_return_val_if_fail (IS_SHEET_OBJECT (so), FALSE);

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		gnm_widget_set_cursor_type (GTK_WIDGET (view->canvas),
					    (so->type == SHEET_OBJECT_ACTION_STATIC)
					    ? GDK_ARROW
					    : GDK_HAND2);
		break;

	case GDK_BUTTON_PRESS: {
		SheetControlGUI	*scg = GNM_SIMPLE_CANVAS (view->canvas)->scg;
		GnmPane *pane = GNM_CANVAS (view->canvas)->pane;

		/* Ignore mouse wheel events */
		if (event->button.button > 3)
			return FALSE;

		if (scg->current_object != so)
			scg_mode_edit_object (scg, so);

		/* we might be protected */
		if (scg->current_object != so)
			return FALSE;

		if (event->button.button < 3) {
			g_return_val_if_fail (pane->drag_object == NULL, FALSE);
			pane->drag_object = so;
			pane->drag_button = event->button.button;

			/* grab the acetate */
			gnm_simple_canvas_grab (pane->control_points [8],
				GDK_POINTER_MOTION_MASK |
				GDK_BUTTON_RELEASE_MASK,
				NULL, event->button.time);
			sheet_object_anchor_cpy (&scg->old_anchor, sheet_object_get_anchor (so));
			scg->object_was_resized = FALSE;
			scg->last_x = event->button.x;
			scg->last_y = event->button.y;
			gnm_canvas_slide_init (GNM_CANVAS (view->canvas));
			gnm_widget_set_cursor_type (GTK_WIDGET (view->canvas), GDK_HAND2);
		} else
			display_object_menu (pane, so, event);
		break;
	}

	default:
		return FALSE;
	}
	return TRUE;
}

static void
cb_sheet_object_view_destroyed (FooCanvasItem *view, SheetObject *so)
{
	SheetControlGUI	*scg = GNM_SIMPLE_CANVAS (view->canvas)->scg;

	g_return_if_fail (IS_SHEET_OBJECT (so));

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
		SheetControlGUI	*scg = GNM_SIMPLE_CANVAS (view->canvas)->scg;

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
 * @bounds_changed : A callback to update the position of a view
 *
 * Setup some standard callbacks for manipulating a view of a sheet object.
 **/
GObject *
gnm_pane_object_register (SheetObject *so, FooCanvasItem *view,
			  GnmPaneObjectBoundsChanged bounds_changed)
{
	foo_canvas_item_raise_to_top (
		FOO_CANVAS_ITEM (GNM_CANVAS (view->canvas)->sheet_object_group));

	g_signal_connect (view, "event",
		G_CALLBACK (cb_sheet_object_canvas_event), so);
	/* all gui views are gtkobjects */
	g_signal_connect (view, "destroy",
		G_CALLBACK (cb_sheet_object_view_destroyed), so);

	(*bounds_changed) (so, view);
	g_signal_connect_object (so, "bounds-changed",
		G_CALLBACK (bounds_changed), view, 0);

	return G_OBJECT (view);
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
GObject *
gnm_pane_widget_register (SheetObject *so, GtkWidget *w, FooCanvasItem *view,
			  GnmPaneObjectBoundsChanged bounds_changed)
{
	g_signal_connect (G_OBJECT (w),
		"event",
		G_CALLBACK (cb_sheet_object_widget_canvas_event), view);
	return gnm_pane_object_register (so, view, bounds_changed);
}
