/* vim: set sw=8: */
/*
 * The Cursor Canvas Item: Implements a rectangular cursor
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg   (jgoldberg@home.com)
 */
#include <config.h>

#include "item-cursor.h"
#define GNUMERIC_ITEM "CURSOR"
#include "item-debug.h"
#include "gnumeric-canvas.h"
#include "sheet-control-gui-priv.h"
#include "style-color.h"
#include "cell.h"
#include "clipboard.h"
#include "selection.h"
#include "sheet.h"
#include "value.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "gnumeric-util.h"
#include "cmd-edit.h"
#include "commands.h"
#include "ranges.h"
#include "parse-util.h"
#include <gal/widgets/e-cursors.h>
#include <gal/util/e-util.h>

#define ITEM_CURSOR_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_cursor_get_type (), ItemCursorClass))

#define AUTO_HANDLE_WIDTH	2
#define AUTO_HANDLE_SPACE	(AUTO_HANDLE_WIDTH * 2)
#define CLIP_SAFETY_MARGIN      (AUTO_HANDLE_SPACE + 5)

struct _ItemCursor {
	GnomeCanvasItem canvas_item;

	SheetControlGUI *scg;
	Range		 pos;

	/* Offset of dragging cell from top left of pos */
	int col_delta, row_delta;

	/* Tip for movement */
	GtkWidget        *tip;

	ItemCursorStyle style;
	GdkGC    *gc;
	int      state;
	int      animation_timer;

	/*
	 * For the autofill mode:
	 *     Where the action started (base_x, base_y) and the
	 *     width and heigth of the selection when the autofill
	 *     started.
	 */
	CellPos	 base;
	int      base_x, base_y;
	int      base_cols, base_rows;

	/* Cached values of the last bounding box information used */
	int      cached_x, cached_y, cached_w, cached_h;
	int	 drag_button;

	gboolean visible:1;
	gboolean use_color:1;
	gboolean auto_fill_handle_at_top:1;
	gboolean auto_fill_handle_at_left:1;

	GdkPixmap *stipple;
	GdkColor  color;
};

static GnomeCanvasItem *item_cursor_parent_class;

enum {
	ARG_0,
	ARG_SHEET_CONTROL_GUI,	/* The SheetControlGUI * argument */
	ARG_STYLE,              /* The style type */
	ARG_BUTTON,		/* The button used to drag this cursor around */
	ARG_COLOR,              /* The optional color */
};

static int
item_cursor_animation_callback (ItemCursor *item_cursor)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_cursor);

	item_cursor->state = !item_cursor->state;
	gnome_canvas_item_request_update (item);
	return TRUE;
}

static void
item_cursor_stop_animation (ItemCursor *item_cursor)
{
	if (item_cursor->animation_timer == -1)
		return;

	gtk_timeout_remove (item_cursor->animation_timer);
	item_cursor->animation_timer = -1;
}

static void
item_cursor_start_animation (ItemCursor *item_cursor)
{
	item_cursor->animation_timer = gtk_timeout_add (
		150, (GtkFunction)(item_cursor_animation_callback),
		item_cursor);
}

static void
item_cursor_destroy (GtkObject *object)
{
	ItemCursor *item_cursor;

	item_cursor = ITEM_CURSOR (object);

	if (item_cursor->tip) {
		gtk_widget_destroy (gtk_widget_get_toplevel (item_cursor->tip));
		item_cursor->tip = NULL;
	}

	item_cursor_stop_animation (item_cursor);
	if (GTK_OBJECT_CLASS (item_cursor_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_cursor_parent_class)->destroy)(object);
}

static void
item_cursor_realize (GnomeCanvasItem *item)
{
	ItemCursor *item_cursor;
	GdkWindow  *window;

	if (GNOME_CANVAS_ITEM_CLASS (item_cursor_parent_class)->realize)
		(*GNOME_CANVAS_ITEM_CLASS (item_cursor_parent_class)->realize)(item);

	item_cursor = ITEM_CURSOR (item);
	window = GTK_WIDGET (item->canvas)->window;

	item_cursor->gc = gdk_gc_new (window);

	if (item_cursor->style == ITEM_CURSOR_ANTED)
		item_cursor_start_animation (item_cursor);

	/*
	 * Create the stipple pattern for the drag and the autofill cursors
	 */
	if (item_cursor->style == ITEM_CURSOR_DRAG || item_cursor->style == ITEM_CURSOR_AUTOFILL){
		static char stipple_data [] = { 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa };

		item_cursor->stipple =
			gdk_bitmap_create_from_data (window, stipple_data, 8, 8);
	}
}

/*
 * Release all of the resources we allocated
 */
static void
item_cursor_unrealize (GnomeCanvasItem *item)
{
	ItemCursor *item_cursor = ITEM_CURSOR (item);

	gdk_gc_unref (item_cursor->gc);
	item_cursor->gc = 0;

	if (item_cursor->stipple){
		gdk_pixmap_unref (item_cursor->stipple);
		item_cursor->stipple = NULL;
	}

	if (GNOME_CANVAS_ITEM_CLASS (item_cursor_parent_class)->unrealize)
		(*GNOME_CANVAS_ITEM_CLASS (item_cursor_parent_class)->unrealize)(item);
}

static void
item_cursor_request_redraw (ItemCursor *item_cursor)
{
	GnomeCanvas *canvas = GNOME_CANVAS_ITEM (item_cursor)->canvas;
	int const x = item_cursor->cached_x;
	int const y = item_cursor->cached_y;
	int const w = item_cursor->cached_w;
	int const h = item_cursor->cached_h;

	gnome_canvas_request_redraw (canvas, x - 2, y - 2, x + 2, y + h + 2);
	gnome_canvas_request_redraw (canvas, x - 2, y - 2, x + w + 2, y + 2);
	gnome_canvas_request_redraw (canvas, x + w - 2, y - 2, x + w + 5, y + h + 5);
	gnome_canvas_request_redraw (canvas, x - 2, y + h - 2, x + w + 5, y + h + 5);
}

static void
item_cursor_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	ItemCursor    *item_cursor = ITEM_CURSOR (item);
	GnumericCanvas *gcanvas = GNUMERIC_CANVAS (item->canvas);
	SheetControlGUI const * const scg = item_cursor->scg;

	int x, y, w, h, extra;

	/* Clip the bounds of the cursor to the visible region of cells */
	int const left = MAX (gcanvas->first.col-1, item_cursor->pos.start.col);
	int const right = MIN (gcanvas->last_visible.col+1, item_cursor->pos.end.col);
	int const top = MAX (gcanvas->first.row-1, item_cursor->pos.start.row);
	int const bottom = MIN (gcanvas->last_visible.row+1, item_cursor->pos.end.row);

	/* Erase the old cursor */
	item_cursor_request_redraw (item_cursor);

	item_cursor->cached_x = x =
		gcanvas->first_offset.col +
		scg_colrow_distance_get (scg, TRUE, gcanvas->first.col, left);
	item_cursor->cached_y = y =
		gcanvas->first_offset.row +
		scg_colrow_distance_get (scg, FALSE, gcanvas->first.row, top);
	item_cursor->cached_w = w = scg_colrow_distance_get (scg, TRUE, left, right+1);
	item_cursor->cached_h = h = scg_colrow_distance_get (scg, FALSE,top, bottom+1);

	item->x1 = x - 1;
	item->y1 = y - 1;

	/* for the autohandle */
	extra = (item_cursor->style == ITEM_CURSOR_SELECTION) ? AUTO_HANDLE_WIDTH : 0;

	item->x2 = x + w + 2 + extra;
	item->y2 = y + h + 2 + extra;

	/* Draw the new cursor */
	item_cursor_request_redraw (item_cursor);

#if 0
	fprintf (stderr, "update %d,%d %d,%d\n", x, y, w,h);
#endif
	if (GNOME_CANVAS_ITEM_CLASS(item_cursor_parent_class)->update)
		(*GNOME_CANVAS_ITEM_CLASS(item_cursor_parent_class)->update)(item, affine, clip_path, flags);
}

/*
 * item_cursor_draw
 *
 * Draw an item of this type.  (x, y) are the upper-left canvas pixel
 * coordinates of the drawable, a temporary pixmap, where things get
 * drawn.  (width, height) are the dimensions of the drawable.
 *
 * In other words - a clipping region.
 */
static void
item_cursor_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemCursor *item_cursor = ITEM_CURSOR (item);
	int dx0, dy0, dx1, dy1;
	GdkPoint points [40];
	int draw_external, draw_internal, draw_handle;
	int draw_stippled, draw_center, draw_thick;
	int premove = 0;
	GdkColor *fore = NULL, *back = NULL;
	GdkRectangle clip_rect;

	int const xd = item_cursor->cached_x;
	int const yd = item_cursor->cached_y;
	int const w = item_cursor->cached_w;
	int const h = item_cursor->cached_h;

#if 0
	fprintf (stderr, "draw %d,%d %d,%d\n", xd, yd, w,h);
#endif
	if (!item_cursor->visible)
		return;

	/* Top left and bottom right corner of cursor, in pixmap coordinates */
	dx0 = xd - x;
	dy0 = yd - y;
	dx1 = dx0 + w;
	dy1 = dy0 + h;

	draw_external = 0;
	draw_internal = 0;
	draw_handle   = 0;
	draw_center   = 0;
	draw_thick    = 1;
	draw_stippled = 0;

	switch (item_cursor->style){
	case ITEM_CURSOR_AUTOFILL:
	case ITEM_CURSOR_DRAG:
		draw_center   = 1;
		draw_thick    = 3;
		draw_stippled = 1;
		fore          = &gs_black;
		back          = &gs_white;
		break;

	case ITEM_CURSOR_BLOCK:
		draw_center   = 1;
		draw_thick    = 1;
		draw_internal = 0;
		draw_external = 1;
		draw_stippled = 0;
		break;

	case ITEM_CURSOR_SELECTION:
		draw_internal = 1;
		draw_external = 1;
		{
			GnumericCanvas const *gcanvas = GNUMERIC_CANVAS (item->canvas);
			GnumericCanvas const *gcanvas0 = scg_pane (gcanvas->scg, 0);

			/* In pane */
			if (item_cursor->pos.end.row <= gcanvas->last_full.row)
				draw_handle = 1;
			/* In pane below */
			else if ((gcanvas->pane->index == 2 || gcanvas->pane->index == 3) &&
				 item_cursor->pos.end.row >= gcanvas0->first.row &&
				 item_cursor->pos.end.row <= gcanvas0->last_full.row)
				draw_handle = 1;
			/* TODO : do we want to add checking for pane above ? */
			else if (item_cursor->pos.start.row < gcanvas->first.row)
				draw_handle = 0;
			else if (item_cursor->pos.start.row != gcanvas->first.row)
				draw_handle = 2;
			else
				draw_handle = 3;
		}
		break;

	case ITEM_CURSOR_EDITING:
		draw_external = 1;

	case ITEM_CURSOR_ANTED:
		draw_center   = 1;
		draw_thick    = 2;
		if (item_cursor->state){
			fore = &gs_light_gray;
			back = &gs_dark_gray;
		} else {
			fore = &gs_dark_gray;
			back = &gs_light_gray;
		}
	};

	if (item_cursor->use_color){
		fore = &item_cursor->color;
		back = &item_cursor->color;
	}

	item_cursor->auto_fill_handle_at_top = (draw_handle >= 2);

	clip_rect.x = 0;
	clip_rect.y = 0;
	clip_rect.width = width;
	clip_rect.height= height;
	gdk_gc_set_clip_rectangle (item_cursor->gc, &clip_rect);

	/* Avoid guint16 overflow during line drawing.  We can change
	 * the shape we draw, so long as no lines or parts of
	 * rectangles are moved from outside to inside the clipping
	 * region */
	dx0 = MAX (dx0, - CLIP_SAFETY_MARGIN);
	dy0 = MAX (dy0, - CLIP_SAFETY_MARGIN);
	dx1 = MIN (dx1, width + CLIP_SAFETY_MARGIN);
	dy1 = MIN (dy1, height + CLIP_SAFETY_MARGIN);

	gdk_gc_set_line_attributes (item_cursor->gc, 1,
				    GDK_LINE_SOLID, -1, -1);
	gdk_gc_set_foreground (item_cursor->gc, &gs_black);
	gdk_gc_set_background (item_cursor->gc, &gs_white);

	if (draw_external){
		switch (draw_handle) {
		/* Auto handle at bottom */
		case 1 :
			premove = AUTO_HANDLE_SPACE;
			/* Fall through */

		/* No auto handle */
		case 0 :
			points [0].x = dx1 + 1;
			points [0].y = dy1 + 1 - premove;
			points [1].x = points [0].x;
			points [1].y = dy0 - 1;
			points [2].x = dx0 - 1;
			points [2].y = dy0 - 1;
			points [3].x = dx0 - 1;
			points [3].y = dy1 + 1;
			points [4].x = dx1 + 1 - premove;
			points [4].y = points [3].y;
			break;

		/* Auto handle at top */
		case 2 : premove = AUTO_HANDLE_SPACE;
			 /* Fall through */

		/* Auto handle at top of sheet */
		case 3 :
			points [0].x = dx1 + 1;
			points [0].y = dy0 - 1 + AUTO_HANDLE_SPACE;
			points [1].x = points [0].x;
			points [1].y = dy1 + 1;
			points [2].x = dx0 - 1;
			points [2].y = points [1].y;
			points [3].x = points [2].x;
			points [3].y = dy0 - 1;
			points [4].x = dx1 + 1 - premove;
			points [4].y = points [3].y;
			break;

		default :
			g_assert_not_reached ();
		};
		gdk_draw_lines (drawable, item_cursor->gc, points, 5);
	}

	if (draw_external && draw_internal){
		if (draw_handle < 2) {
			points [0].x -= 2;
			points [1].x -= 2;
			points [1].y += 2;
			points [2].x += 2;
			points [2].y += 2;
			points [3].x += 2;
			points [3].y -= 2;
			points [4].y -= 2;
		} else {
			points [0].x -= 2;
			points [1].x -= 2;
			points [1].y -= 2;
			points [2].x += 2;
			points [2].y -= 2;
			points [3].x += 2;
			points [3].y += 2;
			points [4].y += 2;
		}
		gdk_draw_lines (drawable, item_cursor->gc, points, 5);
	}

	if (draw_handle == 1 || draw_handle == 2) {
		int const y_off = (draw_handle == 1) ? dy1 - dy0 : 0;
		gdk_draw_rectangle (drawable, item_cursor->gc, TRUE,
				    dx1 - 2,
				    dy0 + y_off - 2,
				    2, 2);
		gdk_draw_rectangle (drawable, item_cursor->gc, TRUE,
				    dx1 + 1,
				    dy0 + y_off - 2,
				    2, 2);
		gdk_draw_rectangle (drawable, item_cursor->gc, TRUE,
				    dx1 - 2,
				    dy0 + y_off + 1,
				    2, 2);
		gdk_draw_rectangle (drawable, item_cursor->gc, TRUE,
				    dx1 + 1,
				    dy0 + y_off + 1,
				    2, 2);
	} else if (draw_handle == 3) {
		gdk_draw_rectangle (drawable, item_cursor->gc, TRUE,
				    dx1 - 2,
				    dy0 + 1,
				    2, 4);
		gdk_draw_rectangle (drawable, item_cursor->gc, TRUE,
				    dx1 + 1,
				    dy0 + 1,
				    2, 4);
	}

	if (draw_center){
		gdk_gc_set_foreground (item_cursor->gc, fore);
		gdk_gc_set_background (item_cursor->gc, back);

		if (draw_stippled){
			gdk_gc_set_fill (item_cursor->gc, GDK_STIPPLED);
			gdk_gc_set_stipple (item_cursor->gc, item_cursor->stipple);
			gdk_gc_set_line_attributes (item_cursor->gc, draw_thick,
						    GDK_LINE_SOLID, -1, -1);
		} else {
			gdk_gc_set_line_attributes (item_cursor->gc, draw_thick,
						    GDK_LINE_DOUBLE_DASH, -1, -1);
		}

		gdk_draw_rectangle (drawable, item_cursor->gc, FALSE,
				    dx0, dy0,
				    dx1 - dx0, dy1 - dy0);
	}
}

gboolean
item_cursor_bound_set (ItemCursor *ic, Range const *new_bound)
{
	g_return_val_if_fail (IS_ITEM_CURSOR (ic), FALSE);
	g_return_val_if_fail (range_is_sane (new_bound), FALSE);

	if (range_equal (&ic->pos, new_bound))
		return FALSE;

	ic->pos = *new_bound;

	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM (ic));

	return TRUE;
}

/**
 * item_cursor_reposition : Re-compute the pixel position of the cursor.
 *
 * When a sheet is zoomed.  The pixel coords shift slightly.  The item cursor
 * must regenerate to stay in sync.
 */
void
item_cursor_reposition (ItemCursor *item_cursor)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_cursor);

	g_return_if_fail (item != NULL);

	/* Request an update */
	gnome_canvas_item_request_update (item);
}

static double
item_cursor_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		   GnomeCanvasItem **actual_item)
{
	ItemCursor *item_cursor = ITEM_CURSOR (item);

	/* Ensure that animated cursors do not receive events */
	if (item_cursor->style == ITEM_CURSOR_ANTED)
		return DBL_MAX;

	*actual_item = NULL;

	if (cx < item->x1-3)
		return INT_MAX;
	if (cx > item->x2+3)
		return INT_MAX;
	if (cy < item->y1-3)
		return INT_MAX;
	if (cy > item->y2+3)
		return INT_MAX;

	/* FIXME: this needs to handle better the small little square case
	 * for ITEM_CURSOR_SELECTION style
	 */
	if ((cx < (item->x1 + 4)) ||
	    (cx > (item->x2 - 8)) ||
	    (cy < (item->y1 + 4)) ||
	    (cy > (item->y2 - 8))){
		*actual_item = item;
		return 0.0;
	}
	return DBL_MAX;
}

static void
item_cursor_translate (GnomeCanvasItem *item, double dx, double dy)
{
	printf ("item_cursor_translate %g, %g\n", dx, dy);
}


static void
item_cursor_setup_auto_fill (ItemCursor *item_cursor, ItemCursor const *parent, int x, int y)
{
	item_cursor->base_x = x;
	item_cursor->base_y = y;

	item_cursor->base_cols = parent->pos.end.col - parent->pos.start.col;
	item_cursor->base_rows = parent->pos.end.row - parent->pos.start.row;
	item_cursor->base.col = parent->pos.start.col;
	item_cursor->base.row = parent->pos.start.row;
}

static inline gboolean
item_cursor_in_drag_handle (ItemCursor *ic, int x, int y)
{
	int const y_test = ic->auto_fill_handle_at_top 
		? ic->canvas_item.y1 + AUTO_HANDLE_WIDTH
		: ic->canvas_item.y2 - AUTO_HANDLE_WIDTH;

	if ((y_test-AUTO_HANDLE_SPACE) <= y &&
	    y <= (y_test+AUTO_HANDLE_SPACE)) {
		int const x_test = ic->auto_fill_handle_at_left 
			? ic->canvas_item.x1 + AUTO_HANDLE_WIDTH
			: ic->canvas_item.x2 - AUTO_HANDLE_WIDTH;
		return (x_test-AUTO_HANDLE_SPACE) <= x &&
			x <= (x_test+AUTO_HANDLE_SPACE);
	 }
	return FALSE;
}

static void
item_cursor_set_cursor (GnomeCanvas *canvas, ItemCursor *ic, int x, int y)
{
	int cursor;

	if (item_cursor_in_drag_handle (ic, x, y))
		cursor = E_CURSOR_THIN_CROSS;
	else
		cursor = E_CURSOR_ARROW;

	e_cursor_set_widget (canvas, cursor);
}

static Value *
cb_autofill_bound (Sheet *sheet, int col, int row,
		   Cell *cell, gpointer want_cols)
{
	gboolean cols = GPOINTER_TO_INT (want_cols);
	
	if (!cell_is_blank (cell))
		return cols ? value_new_int (col) : value_new_int (row);
	else
		return NULL;
}

static gint
item_cursor_selection_event (GnomeCanvasItem *item, GdkEvent *event)
{
	GnomeCanvas	*canvas = item->canvas;
	GnumericCanvas   *gcanvas = GNUMERIC_CANVAS (canvas);
	ItemCursor *ic = ITEM_CURSOR (item);
	int x, y;

	switch (event->type){
	case GDK_ENTER_NOTIFY:
		gnome_canvas_w2c (
			canvas, event->crossing.x, event->crossing.y, &x, &y);

		item_cursor_set_cursor (canvas, ic, x, y);
		return TRUE;

	case GDK_MOTION_NOTIFY: {
		int style, button;
		ItemCursor *special_cursor;

		gnome_canvas_w2c (
			canvas, event->motion.x, event->motion.y, &x, &y);

		if (ic->drag_button < 0) {
			item_cursor_set_cursor (canvas, ic, x, y);
			return TRUE;
		}

		/*
		 * determine which part of the cursor was clicked:
		 * the border or the handlebox
		 */
		if (item_cursor_in_drag_handle (ic, x, y))
			style = ITEM_CURSOR_AUTOFILL;
		else
			style = ITEM_CURSOR_DRAG;

		button = ic->drag_button;
		ic->drag_button = -1;
		gnome_canvas_item_ungrab (item, event->button.time);

		scg_special_cursor_start (ic->scg, style, button);
		special_cursor = gcanvas->pane->cursor.special;
		if (style == ITEM_CURSOR_AUTOFILL)
			item_cursor_setup_auto_fill (
				special_cursor, ic, x, y);

		if (x < 0)
			x = 0;
		if (y < 0)
			y = 0;
		/*
		 * Capture the offset of the current cell relative to
		 * the upper left corner.  Be careful handling the position
		 * of the cursor.  it is possible to select the exterior or
		 * interior of the cursor edge which behaves as if the cursor
		 * selection was offset by one.
		 */
		{
			int d_col = gnm_canvas_find_col (gcanvas, x, NULL) -
				ic->pos.start.col;
			int d_row = gnm_canvas_find_row (gcanvas, y, NULL) -
				ic->pos.start.row;

			if (d_col >= 0) {
				int tmp = ic->pos.end.col - ic->pos.start.col;
				if (d_col > tmp)
					d_col = tmp;
			} else
				d_col = 0;
			special_cursor->col_delta = d_col;

			if (d_row >= 0) {
				int tmp = ic->pos.end.row - ic->pos.start.row;
				if (d_row > tmp)
					d_row = tmp;
			} else
				d_row = 0;
			special_cursor->row_delta = d_row;
		}

		if (scg_special_cursor_bound_set (ic->scg, &ic->pos))
			gnome_canvas_update_now (canvas);

		gnome_canvas_item_grab (
			GNOME_CANVAS_ITEM (special_cursor),
			GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK,
			NULL,
			event->button.time);
		gnm_canvas_slide_init (gcanvas);

		/*
		 * We flush after the grab to ensure that the new item-cursor
		 * gets created.  If it is not ready in time double click
		 * events will be disrupted and it will appear as if we are
		 * doing an button_press with a missing release.
		 */
		gdk_flush ();
		return TRUE;
	}

	case GDK_2BUTTON_PRESS: {
		SheetControl *sc = (SheetControl *) ic->scg;
		Sheet *sheet = sc->sheet;
		int final_col = ic->pos.end.col;
		int final_row = ic->pos.end.row;
		Value *tmp = NULL;

		if (ic->drag_button != (int)event->button.button)
			return TRUE;

		ic->drag_button = -1;

		/*
		 * We flush after the ungrab, to have the ungrab take
		 * effect immediately (the copy operation might take
		 * long, and we do not want the mouse to be grabbed
		 * all this time).
		 */
		gnome_canvas_item_ungrab (item, event->button.time);
		gdk_flush ();

		wbcg_edit_finish (ic->scg->wbcg, TRUE);

		if (sheet_is_region_empty (sheet, &ic->pos))
			return TRUE;

		/* TODO : This is still different from XL.
		 * We probably do not want to use find_boundary
		 * what we really want is to walk forward through
		 * template_{col,row} as long as it has content
		 * AND the region to fill is empty.
		 */
		if (event->button.state & GDK_MOD1_MASK) {
			int template_col = ic->pos.end.col + 1;
			int template_row = ic->pos.start.row - 1;

			if (template_row < 0 ||
			    sheet_is_cell_empty (sheet, ic->pos.start.col,
						 template_row)) {

				template_row = ic->pos.end.row + 1;
				if (template_row >= SHEET_MAX_ROWS ||
				    sheet_is_cell_empty (sheet, ic->pos.end.col,
							 template_row))
					return TRUE;
			}

			if (template_col >= SHEET_MAX_COLS ||
			    sheet_is_cell_empty (sheet, template_col,
						 template_row))
				return TRUE;
			final_col = sheet_find_boundary_horizontal (sheet,
				ic->pos.end.col, template_row,
				template_row, 1, TRUE);
			if (final_col <= ic->pos.end.col)
				return TRUE;

			/* Make sure we don't overwrite the contents of the fill target */
			for (x = ic->pos.end.col + 1; x <= final_col; x++) {
				tmp = sheet_foreach_cell_in_range (
					sheet, TRUE, x, ic->pos.start.row, x,
					ic->pos.end.row, (ForeachCellCB) cb_autofill_bound,
					GINT_TO_POINTER (TRUE));

				if (tmp) {
					int bound = value_get_as_int (tmp);
					if (final_col >= bound)
						final_col = bound - 1;
					value_release (tmp);
					break;
				}
			}
		} else {
			int template_row = ic->pos.end.row + 1;
			int template_col = ic->pos.start.col - 1;
			
			if (template_col < 0 ||
			    sheet_is_cell_empty (sheet, template_col,
						 ic->pos.start.row)) {

				template_col = ic->pos.end.col + 1;
				if (template_col >= SHEET_MAX_COLS ||
				    sheet_is_cell_empty (sheet, template_col,

							 ic->pos.start.row))
					return TRUE;
			}

			if (template_row >= SHEET_MAX_ROWS ||
			    sheet_is_cell_empty (sheet, template_col,
						 template_row))
				return TRUE;
			final_row = sheet_find_boundary_vertical (sheet,
				template_col, ic->pos.end.row,
				template_col, 1, TRUE);
			if (final_row <= ic->pos.end.row)
				return TRUE;

			/*
			 * Make sure we don't overwrite the contents of the fill target
			 * NOTE : We assume the traversal order is 'do all cols in a row and
			 *        move on to the next row' here!
			 */
			tmp = sheet_foreach_cell_in_range (
				sheet, TRUE, ic->pos.start.col, ic->pos.end.row + 1,
				ic->pos.end.col, final_row, (ForeachCellCB) cb_autofill_bound,
				GINT_TO_POINTER (FALSE));

			if (tmp) {
				int bound = value_get_as_int (tmp);
				if (final_row >= bound)
					final_row = bound - 1;
				value_release (tmp);
			}
		}

		/* fill the row/column */
		cmd_autofill (sc->wbc, sheet,
			      ic->pos.start.col, ic->pos.start.row,
			      ic->pos.end.col - ic->pos.start.col + 1,
			      ic->pos.end.row - ic->pos.start.row + 1,
			      final_col, final_row);

		return TRUE;
	}

	case GDK_BUTTON_PRESS:
		/* scroll wheel events dont have corresponding release events */
		if (event->button.button > 3)
			return FALSE;

		/* If another button is already down ignore this one */
		if (ic->drag_button >= 0)
			return TRUE;

		ic->drag_button = event->button.button;

		/* prepare to create fill or drag cursors, but dont until we
		 * move.  If we did create them here there would be problems
		 * with race conditions when the new cursors pop into existence
		 * during a double-click
		 */
		gnome_canvas_item_grab (
			item,
			GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK,
			NULL,
			event->button.time);

		/* Be extra paranoid.  Ensure that the grab is registered */
		gdk_flush ();
		return TRUE;

	case GDK_BUTTON_RELEASE:
		if (ic->drag_button != (int)event->button.button)
			return TRUE;

		/* Double clicks may have already released the drag prep */
		if (ic->drag_button >= 0) {
			ic->drag_button = -1;
			gnome_canvas_item_ungrab (item, event->button.time);
			gdk_flush ();
		}
		return TRUE;

	default:
		return FALSE;
	}
}

static gboolean
item_cursor_target_region_ok (ItemCursor *ic)
{
	int v;
	GtkWidget	*message;
	GnomeCanvasItem *gci = GNOME_CANVAS_ITEM (ic);
	SheetControl	*sc = (SheetControl *) ic->scg;

	g_return_val_if_fail (gci != NULL, FALSE);
	g_return_val_if_fail (gci->canvas != NULL, FALSE);

	if (sheet_is_region_empty_or_selected (sc->sheet, &ic->pos))
		return TRUE;

	message = gnome_message_box_new (
		_("The cells dragged will overwrite the contents of the\n"
		  "existing cells in that range.  Do you want me to replace\n"
		  "the contents in this region?"),
		GNOME_MESSAGE_BOX_WARNING,
		GNOME_STOCK_BUTTON_YES,
		GNOME_STOCK_BUTTON_NO,
		NULL);
	v = gnumeric_dialog_run (ic->scg->wbcg, GNOME_DIALOG (message));

	if (v == 0)
		return TRUE;
	return FALSE;
}

typedef enum {
	ACTION_NONE = 1,
	ACTION_MOVE_CELLS,
	ACTION_COPY_CELLS,
	ACTION_COPY_FORMATS,
	ACTION_COPY_VALUES,
	ACTION_SHIFT_DOWN_AND_COPY,
	ACTION_SHIFT_RIGHT_AND_COPY,
	ACTION_SHIFT_DOWN_AND_MOVE,
	ACTION_SHIFT_RIGHT_AND_MOVE
} ActionType;

static void
item_cursor_do_action (ItemCursor *ic, ActionType action, guint32 time)
{
	SheetControl *sc;
	Sheet *sheet;
	WorkbookControl *wbc;
	PasteTarget pt;

	g_return_if_fail (ic != NULL);

	if (action == ACTION_NONE || !item_cursor_target_region_ok (ic)) {
		scg_special_cursor_stop	(ic->scg);
		return;
	}

	sc = (SheetControl *) ic->scg;
	sheet = sc->sheet;
	wbc = sc->wbc;

	switch (action) {
	case ACTION_COPY_CELLS:
		if (!sheet_selection_copy (wbc, sheet))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_ALL_TYPES),
			   time);
		break;

	case ACTION_MOVE_CELLS:
		if (!sheet_selection_cut (wbc, sheet))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_ALL_TYPES),
			   time);
		break;

	case ACTION_COPY_FORMATS:
		if (!sheet_selection_copy (wbc, sheet))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_FORMATS),
			   time);
		break;

	case ACTION_COPY_VALUES:
		if (!sheet_selection_copy (wbc, sheet))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_AS_VALUES),
			   time);
		break;

	case ACTION_SHIFT_DOWN_AND_COPY:
	case ACTION_SHIFT_RIGHT_AND_COPY:
	case ACTION_SHIFT_DOWN_AND_MOVE:
	case ACTION_SHIFT_RIGHT_AND_MOVE:
		g_warning ("Operation not yet implemented\n");
		break;

	default :
		g_warning ("Invalid Operation %d\n", action);
	}

	scg_special_cursor_stop	(ic->scg);
}

static gboolean
context_menu_hander (GnumericPopupMenuElement const *element,
		     gpointer ic)
{
	g_return_val_if_fail (element != NULL, TRUE);

	item_cursor_do_action (ic, element->index, GDK_CURRENT_TIME);
	return TRUE;
}

static void
item_cursor_popup_menu (ItemCursor *ic, GdkEventButton *event)
{
	static GnumericPopupMenuElement const popup_elements[] = {
		{ N_("_Move"),		NULL,
		    0, 0, ACTION_MOVE_CELLS },

		{ N_("_Copy"),		GNOME_STOCK_PIXMAP_COPY,
		    0, 0, ACTION_COPY_CELLS },

		{ N_("Copy _Formats"),		NULL,
		    0, 0, ACTION_COPY_FORMATS },
		{ N_("Copy _Values"),		NULL,
		    0, 0, ACTION_COPY_VALUES },

		{ "", NULL, 0, 0, 0 },

		{ N_("Shift _Down and Copy"),		NULL,
		    0, 0, ACTION_SHIFT_DOWN_AND_COPY },
		{ N_("Shift _Right and Copy"),		NULL,
		    0, 0, ACTION_SHIFT_RIGHT_AND_COPY },
		{ N_("Shift Dow_n and Move"),		NULL,
		    0, 0, ACTION_SHIFT_DOWN_AND_MOVE },
		{ N_("Shift Righ_t and Move"),		NULL,
		    0, 0, ACTION_SHIFT_RIGHT_AND_MOVE },

		{ "", NULL, 0, 0, 0 },

		{ N_("C_ancel"),		NULL,
		    0, 0, ACTION_NONE },

		{ NULL, NULL, 0, 0, 0 }
	};

	gnumeric_create_popup_menu (popup_elements,
				    &context_menu_hander, ic,
				    0, 0, event);
}

/*
 * Invoked when the item has been dropped
 */
static void
item_cursor_do_drop (ItemCursor *ic, GdkEventButton *event)
{
	/* Only do the operation if something moved */
	Sheet const *sheet = ((SheetControl *) ic->scg)->sheet;
	Range const *target = selection_first_range (sheet, NULL, NULL);

	wb_control_gui_set_status_text (ic->scg->wbcg, "");
	if (range_equal (target, &ic->pos)) {
		scg_special_cursor_stop	(ic->scg);
		return;
	}

	if (event->button == 3)
		item_cursor_popup_menu (ic, event);
	else
		item_cursor_do_action (ic, (event->state & GDK_CONTROL_MASK)
				       ? ACTION_COPY_CELLS : ACTION_MOVE_CELLS,
				       event->time);
}

static void
item_cursor_set_bounds_visibly (ItemCursor *ic,
				int col,int row,
				CellPos const *corner,
				int end_col, int end_row)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (ic);
	GnumericCanvas  *gcanvas = GNUMERIC_CANVAS (item->canvas);
	Range r;

	/* Handle visibility here rather than in the slide handler because we
	 * need to constrain movement some times.  eg no sense scrolling the
	 * canvas if the autofill cursor is not changing size.
	 */
	scg_make_cell_visible (ic->scg, end_col, end_row, FALSE, TRUE);

	/* FIXME FIXME FIXME
	 * Ideally we would update the bounds BEFORE we scroll, this would
	 * decrease the flicker.  Unfortunately, our optimization of clipping
	 * the cursor to the currently visible region is getting in the way.
	 * We are forced to make the region visible before we move the cursor.
	 */
	range_init (&r, corner->col, corner->row, end_col, end_row);
	if (scg_special_cursor_bound_set (ic->scg, &r))
		gnome_canvas_update_now (GNOME_CANVAS (gcanvas));
}

void
item_cursor_set_visibility (ItemCursor *item_cursor, gboolean visible)
{
	g_return_if_fail (IS_ITEM_CURSOR (item_cursor));

	if (item_cursor->visible == visible)
		return;

	item_cursor->visible = visible;
	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM (item_cursor));
}

#if 0
/*
 * This cannot be used yet.  It exercises a bug in GtkLayout
 */
static void
item_cursor_tip_setlabel (ItemCursor *item_cursor)
{
	char buffer [32]; /* What if SHEET_MAX_ROWS or SHEET_MAX_COLS changes? */
	int tmp;
	Range const * src = &item_cursor->pos;

	if (item_cursor->tip == NULL) {
		item_cursor->tip = gnumeric_create_tooltip ();
		gnumeric_position_tooltip (item_cursor->tip, TRUE);
		gtk_widget_show_all (gtk_widget_get_toplevel (item_cursor->tip));
	}

	g_return_if_fail (item_cursor->tip != NULL);

	/*
	 * keep these as 2 print statements, because
	 * col_name uses a static buffer
	 */
	tmp = snprintf (buffer, sizeof (buffer), "%s%s",
			col_name (src->start.col),
			row_name (src->start.row));

	if (src->start.col != src->end.col || src->start.row != src->end.row)
		snprintf (buffer+tmp, sizeof (buffer)-tmp, ":%s%s",
			  col_name (src->end.col),
			  row_name (src->end.row));

	gtk_label_set_text (GTK_LABEL (item_cursor->tip), buffer);
}
#endif

static void
item_cursor_tip_setstatus (ItemCursor *item_cursor)
{
	char buffer [32]; /* What if SHEET_MAX_ROWS or SHEET_MAX_COLS changes? */
	int tmp;
	Range const * src = &item_cursor->pos;

	/*
	 * keep these as 2 print statements, because
	 * col_name uses a static buffer
	 */
	tmp = snprintf (buffer, sizeof (buffer), "%s%s",
			col_name (src->start.col),
			row_name (src->start.row));

	if (src->start.col != src->end.col || src->start.row != src->end.row)
		snprintf (buffer+tmp, sizeof (buffer)-tmp, ":%s%s",
			  col_name (src->end.col),
			  row_name (src->end.row));
			  
	wb_control_gui_set_status_text (item_cursor->scg->wbcg, buffer);
}

static gboolean
cb_move_cursor (GnumericCanvas *gcanvas, int col, int row, gpointer user_data)
{
	ItemCursor *item_cursor = user_data;
	int const w = (item_cursor->pos.end.col - item_cursor->pos.start.col);
	int const h = (item_cursor->pos.end.row - item_cursor->pos.start.row);
	CellPos corner;
	
	corner.col = col - item_cursor->col_delta;
	if (corner.col < 0)
		corner.col = 0;
	else if (corner.col >= (SHEET_MAX_COLS - w))
		corner.col = SHEET_MAX_COLS - w - 1;

	corner.row = row - item_cursor->row_delta;
	if (corner.row < 0)
		corner.row = 0;
	else if (corner.row >= (SHEET_MAX_ROWS - h))
		corner.row = SHEET_MAX_ROWS - h - 1;

#if 0
	/* Leave this disabled until GtkLayout correctly handles
	 * Windows with SaveUnder set. (Speak to federico for details).
	 */
	item_cursor_tip_setlabel (item_cursor);
#endif
	item_cursor_tip_setstatus (item_cursor);
	
	/* Make target cell visible, and adjust the cursor size */
	item_cursor_set_bounds_visibly (item_cursor, col, row, &corner,
					corner.col + w, corner.row + h);
	return FALSE;
}

static void
item_cursor_handle_motion (ItemCursor *item_cursor, GdkEvent *event,
			   GnumericCanvasSlideHandler slide_handler)
{
	GnomeCanvas *canvas = GNOME_CANVAS_ITEM (item_cursor)->canvas;

	gnm_canvas_handle_motion (GNUMERIC_CANVAS (canvas),
		canvas, &event->motion,
		GNM_SLIDE_X | GNM_SLIDE_Y | GNM_SLIDE_AT_COLROW_BOUND,
		slide_handler, item_cursor);
}

static gint
item_cursor_drag_event (GnomeCanvasItem *item, GdkEvent *event)
{
	ItemCursor *ic = ITEM_CURSOR (item);

	switch (event->type){
	case GDK_BUTTON_RELEASE:
		/* Note : see comment below, and bug 30507 */
		if ((int)event->button.button == ic->drag_button) {
			gnm_canvas_slide_stop (GNUMERIC_CANVAS (item->canvas));
			gnome_canvas_item_ungrab (item, event->button.time);
			item_cursor_do_drop (ic, (GdkEventButton *) event);
		}
		return TRUE;

	case GDK_BUTTON_PRESS:
		/* This kind of cursor is created and grabbed.  Then destroyed
		 * when the button is released.  If we are seeing a press it
		 * means that someone has pressed another button WHILE THE
		 * FIRST IS STILL DOWN.  Ignore ths event.
		 */
		return TRUE;

	case GDK_MOTION_NOTIFY:
		item_cursor_handle_motion (ic, event, &cb_move_cursor);
		return TRUE;

	default:
		return FALSE;
	}
}

static gboolean
cb_autofill_scroll (GnumericCanvas *gcanvas, int col, int row, gpointer user_data)
{
	ItemCursor *item_cursor = user_data;
	int bottom = item_cursor->base.row + item_cursor->base_rows;
	int right = item_cursor->base.col + item_cursor->base_cols;

	/* Autofill by row or by col, NOT both. */
	if ((right - col) > (bottom - row)){
		/* FIXME : We do not support inverted auto-fill */
		if (bottom < row)
			bottom = row;
	} else {
		/* FIXME : We do not support inverted auto-fill */
		if (right < col)
			right = col;
	}

	/* Do not auto scroll past the start */
	if (row < item_cursor->base.row)
		row = item_cursor->base.row;
	if (col < item_cursor->base.col)
		col = item_cursor->base.col;

	item_cursor_set_bounds_visibly (item_cursor, col, row,
					&item_cursor->base, right, bottom);
	return FALSE;
}

static gint
item_cursor_autofill_event (GnomeCanvasItem *item, GdkEvent *event)
{
	ItemCursor *ic = ITEM_CURSOR (item);
	SheetControl *sc = (SheetControl *) ic->scg;
	
	switch (event->type){

	case GDK_BUTTON_RELEASE:
		gnm_canvas_slide_stop (GNUMERIC_CANVAS (item->canvas));

		/* We flush after the ungrab, to have the ungrab take effect
		 * immediately (the fill operation might take a while, and we
		 * do not want the mouse to be grabbed the entire time).
		 */
		gnome_canvas_item_ungrab (item, event->button.time);
		gdk_flush ();

		wbcg_edit_finish (ic->scg->wbcg, TRUE);
		cmd_autofill (sc->wbc, sc->sheet,
			      ic->base.col,    ic->base.row,
			      ic->base_cols+1, ic->base_rows+1,
			      ic->pos.end.col, ic->pos.end.row);

		scg_special_cursor_stop	(ic->scg);

		return TRUE;

	case GDK_MOTION_NOTIFY:
		item_cursor_handle_motion (ic, event, &cb_autofill_scroll);
		return TRUE;

	default:
		return FALSE;
	}
}

static gint
item_cursor_event (GnomeCanvasItem *item, GdkEvent *event)
{
	ItemCursor *ic = ITEM_CURSOR (item);

	/* While editing nothing should be draggable */
	if (wb_control_gui_is_editing (ic->scg->wbcg))
		return TRUE;

#if 0
	switch (event->type)
	{
	case GDK_BUTTON_RELEASE: printf ("release %d\n", ic->style); break;
	case GDK_BUTTON_PRESS: printf ("press %d\n", ic->style); break;
	case GDK_2BUTTON_PRESS: printf ("2press %d\n", ic->style); break;
	default :
	    break;
	};
#endif
	switch (ic->style) {
	case ITEM_CURSOR_ANTED:
		g_warning ("Animated cursors should not receive events, "
			   "the point method should preclude that");
		return FALSE;

	case ITEM_CURSOR_SELECTION:
		return item_cursor_selection_event (item, event);

	case ITEM_CURSOR_DRAG:
		return item_cursor_drag_event (item, event);

	case ITEM_CURSOR_AUTOFILL:
		return item_cursor_autofill_event (item, event);

	default:
		return FALSE;
	}
}

static void
item_cursor_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ItemCursor *item_cursor;

	item = GNOME_CANVAS_ITEM (o);
	item_cursor = ITEM_CURSOR (o);

	switch (arg_id){
	case ARG_SHEET_CONTROL_GUI:
		item_cursor->scg = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_STYLE:
		item_cursor->style = GTK_VALUE_INT (*arg);
		break;
	case ARG_BUTTON :
		item_cursor->drag_button = GTK_VALUE_INT (*arg);
		break;
	case ARG_COLOR: {
		GdkColor color;
		char *color_name = GTK_VALUE_STRING (*arg);
		if (gnome_canvas_get_color (item->canvas, color_name, &color)){
			item_cursor->color = color;
			item_cursor->use_color = 1;
		}
	}
	}
}

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemCursorClass;

/*
 * ItemCursor class initialization
 */
static void
item_cursor_class_init (ItemCursorClass *item_cursor_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_cursor_parent_class = gtk_type_class (gnome_canvas_item_get_type ());

	object_class = (GtkObjectClass *) item_cursor_class;
	item_class = (GnomeCanvasItemClass *) item_cursor_class;

	gtk_object_add_arg_type ("ItemCursor::SheetControlGUI", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET_CONTROL_GUI);
	gtk_object_add_arg_type ("ItemCursor::Style", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_STYLE);
	gtk_object_add_arg_type ("ItemCursor::Button", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_BUTTON);
	gtk_object_add_arg_type ("ItemCursor::Color", GTK_TYPE_STRING,
				 GTK_ARG_WRITABLE, ARG_COLOR);

	object_class->set_arg = item_cursor_set_arg;
	object_class->destroy = item_cursor_destroy;

	/* GnomeCanvasItem method overrides */
	item_class->update      = item_cursor_update;
	item_class->realize     = item_cursor_realize;
	item_class->unrealize   = item_cursor_unrealize;
	item_class->draw        = item_cursor_draw;
	item_class->point       = item_cursor_point;
	item_class->translate   = item_cursor_translate;
	item_class->event       = item_cursor_event;
}

static void
item_cursor_init (ItemCursor *item_cursor)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_cursor);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 1;
	item->y2 = 1;

	item_cursor->pos.start.col = 0;
	item_cursor->pos.end.col   = 0;
	item_cursor->pos.start.row = 0;
	item_cursor->pos.end.row   = 0;

	item_cursor->col_delta = 0;
	item_cursor->row_delta = 0;

	item_cursor->tip = NULL;

	item_cursor->style = ITEM_CURSOR_SELECTION;
	item_cursor->gc = NULL;
	item_cursor->state = 0;
	item_cursor->animation_timer = -1;

	item_cursor->visible = TRUE;
	item_cursor->auto_fill_handle_at_top = FALSE;
	item_cursor->auto_fill_handle_at_left = FALSE;
	item_cursor->drag_button = -1;
}

E_MAKE_TYPE (item_cursor, "ItemCursor", ItemCursor,
	     item_cursor_class_init, item_cursor_init,
	     GNOME_TYPE_CANVAS_ITEM);
