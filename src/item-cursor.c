/* vim: set sw=8: */
/*
 * The Cursor Canvas Item: Implements a rectangular cursor
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 *     Jody Goldberg   (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "item-cursor.h"

#include "gnumeric-canvas.h"
#include "sheet-control-gui-priv.h"
#include "style-color.h"
#include "cell.h"
#include "clipboard.h"
#include "selection.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-merge.h"
#include "value.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "gui-util.h"
#include "cmd-edit.h"
#include "commands.h"
#include "ranges.h"
#include "parse-util.h"
#include <gal/widgets/e-cursors.h>
#include <gsf/gsf-impl-utils.h>
#define GNUMERIC_ITEM "CURSOR"
#include "item-debug.h"

#define ITEM_CURSOR_CLASS(k)      (G_TYPE_CHECK_CLASS_CAST ((k), item_cursor_get_type (), ItemCursorClass))

#define AUTO_HANDLE_WIDTH	2
#define AUTO_HANDLE_SPACE	(AUTO_HANDLE_WIDTH * 2)
#define CLIP_SAFETY_MARGIN      (AUTO_HANDLE_SPACE + 5)

struct _ItemCursor {
	FooCanvasItem canvas_item;

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
	int   base_x, base_y;
	Range autofill_src;
	int   autofill_hsize, autofill_vsize;

	/* Cached values of the last bounding box information used */
	int      cached_x, cached_y, cached_w, cached_h;
	int	 drag_button;

	gboolean visible;
	gboolean use_color;
	gboolean auto_fill_handle_at_top;
	gboolean auto_fill_handle_at_left;

	GdkPixmap *stipple;
	GdkColor  color;
};

static FooCanvasItem *item_cursor_parent_class;

enum {
	ARG_0,
	ARG_SHEET_CONTROL_GUI,	/* The SheetControlGUI * argument */
	ARG_STYLE,              /* The style type */
	ARG_BUTTON,		/* The button used to drag this cursor around */
	ARG_COLOR               /* The optional color */
};

static int
cb_item_cursor_animation (ItemCursor *ic)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ic);

	ic->state = !ic->state;
	foo_canvas_item_request_update (item);
	return TRUE;
}

static void
item_cursor_destroy (GtkObject *object)
{
	ItemCursor *ic;

	ic = ITEM_CURSOR (object);

	if (ic->tip) {
		gtk_widget_destroy (gtk_widget_get_toplevel (ic->tip));
		ic->tip = NULL;
	}

	if (GTK_OBJECT_CLASS (item_cursor_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_cursor_parent_class)->destroy)(object);
}

static void
item_cursor_realize (FooCanvasItem *item)
{
	ItemCursor *ic;
	GdkWindow  *window;

	if (FOO_CANVAS_ITEM_CLASS (item_cursor_parent_class)->realize)
		(*FOO_CANVAS_ITEM_CLASS (item_cursor_parent_class)->realize)(item);

	ic = ITEM_CURSOR (item);
	window = GTK_WIDGET (item->canvas)->window;

	ic->gc = gdk_gc_new (window);

	if (ic->style == ITEM_CURSOR_ANTED) {
		g_return_if_fail (ic->animation_timer == -1);
		ic->animation_timer = g_timeout_add (
			150, (GSourceFunc)cb_item_cursor_animation,
			ic);
	}

	/*
	 * Create the stipple pattern for the drag and the autofill cursors
	 */
	if (ic->style == ITEM_CURSOR_DRAG ||
	    ic->style == ITEM_CURSOR_AUTOFILL) {
		static char stipple_data [] = { 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa };

		ic->stipple =
			gdk_bitmap_create_from_data (window, stipple_data, 8, 8);
	}
}

/*
 * Release all of the resources we allocated
 */
static void
item_cursor_unrealize (FooCanvasItem *item)
{
	ItemCursor *ic = ITEM_CURSOR (item);

	g_object_unref (G_OBJECT (ic->gc));
	ic->gc = 0;

	if (ic->stipple) {
		g_object_unref (ic->stipple);
		ic->stipple = NULL;
	}

	if (ic->animation_timer != -1) {
		g_source_remove (ic->animation_timer);
		ic->animation_timer = -1;
	}

	if (FOO_CANVAS_ITEM_CLASS (item_cursor_parent_class)->unrealize)
		(*FOO_CANVAS_ITEM_CLASS (item_cursor_parent_class)->unrealize)(item);
}

static void
item_cursor_request_redraw (ItemCursor *ic)
{
	FooCanvas *canvas = FOO_CANVAS_ITEM (ic)->canvas;
	int const x = ic->cached_x;
	int const y = ic->cached_y;
	int const w = ic->cached_w;
	int const h = ic->cached_h;

	foo_canvas_request_redraw (canvas, x - 2, y - 2, x + 2, y + h + 2);
	foo_canvas_request_redraw (canvas, x - 2, y - 2, x + w + 2, y + 2);
	foo_canvas_request_redraw (canvas, x + w - 2, y - 2, x + w + 5, y + h + 5);
	foo_canvas_request_redraw (canvas, x - 2, y + h - 2, x + w + 5, y + h + 5);
}

static void
item_cursor_update (FooCanvasItem *item, double i2w_dx, double i2w_dy, int flags)
{
	ItemCursor    *ic = ITEM_CURSOR (item);
	GnmCanvas *gcanvas = GNM_CANVAS (item->canvas);
	SheetControlGUI const * const scg = ic->scg;

	int x, y, w, h, extra;

	/* Clip the bounds of the cursor to the visible region of cells */
	int const left = MAX (gcanvas->first.col-1, ic->pos.start.col);
	int const right = MIN (gcanvas->last_visible.col+1, ic->pos.end.col);
	int const top = MAX (gcanvas->first.row-1, ic->pos.start.row);
	int const bottom = MIN (gcanvas->last_visible.row+1, ic->pos.end.row);

	/* Erase the old cursor */
	item_cursor_request_redraw (ic);

	ic->cached_x = x =
		gcanvas->first_offset.col +
		scg_colrow_distance_get (scg, TRUE, gcanvas->first.col, left);
	ic->cached_y = y =
		gcanvas->first_offset.row +
		scg_colrow_distance_get (scg, FALSE, gcanvas->first.row, top);
	ic->cached_w = w = scg_colrow_distance_get (scg, TRUE, left, right+1);
	ic->cached_h = h = scg_colrow_distance_get (scg, FALSE,top, bottom+1);

	item->x1 = x - 1;
	item->y1 = y - 1;

	/* for the autohandle */
	extra = (ic->style == ITEM_CURSOR_SELECTION) ? AUTO_HANDLE_WIDTH : 0;

	item->x2 = x + w + 2 + extra;
	item->y2 = y + h + 2 + extra;

	/* Draw the new cursor */
	item_cursor_request_redraw (ic);

#if 0
	fprintf (stderr, "update %d,%d %d,%d\n", x, y, w,h);
#endif
	if (FOO_CANVAS_ITEM_CLASS(item_cursor_parent_class)->update)
		(*FOO_CANVAS_ITEM_CLASS(item_cursor_parent_class)->update)(item, i2w_dx, i2w_dy, flags);
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
item_cursor_draw (FooCanvasItem *item, GdkDrawable *drawable,
		  GdkEventExpose *expose)
{
	ItemCursor *ic = ITEM_CURSOR (item);
	int dx0, dy0, dx1, dy1;
	GdkPoint points [5];
	int draw_thick, draw_handle;
	int premove = 0;
	GdkColor *fore = NULL, *back = NULL;
	gboolean draw_stippled, draw_center, draw_external, draw_internal;

	int const xd = ic->cached_x;
	int const yd = ic->cached_y;
	int const w = ic->cached_w;
	int const h = ic->cached_h;

#if 0
	fprintf (stderr, "draw %d,%d %d,%d\n", xd, yd, w,h);
#endif
	if (!ic->visible)
		return;

	/* Top left and bottom right corner of cursor, in pixmap coordinates */
	dx0 = xd;
	dy0 = yd;
	dx1 = dx0 + w;
	dy1 = dy0 + h;

	draw_external = FALSE;
	draw_internal = FALSE;
	draw_handle   = 0;
	draw_thick    = 1;
	draw_center   = FALSE;
	draw_stippled = FALSE;

	switch (ic->style) {
	case ITEM_CURSOR_AUTOFILL:
	case ITEM_CURSOR_DRAG:
		draw_center   = TRUE;
		draw_thick    = 3;
		draw_stippled = TRUE;
		fore          = &gs_black;
		back          = &gs_white;
		break;

	case ITEM_CURSOR_BLOCK:
		draw_center   = TRUE;
		draw_thick    = 3;
		break;

	case ITEM_CURSOR_SELECTION:
		draw_internal = TRUE;
		draw_external = TRUE;
		{
			GnmCanvas const *gcanvas = GNM_CANVAS (item->canvas);
			GnmCanvas const *gcanvas0 = scg_pane (gcanvas->simple.scg, 0);

			/* In pane */
			if (ic->pos.end.row <= gcanvas->last_full.row)
				draw_handle = 1;
			/* In pane below */
			else if ((gcanvas->pane->index == 2 || gcanvas->pane->index == 3) &&
				 ic->pos.end.row >= gcanvas0->first.row &&
				 ic->pos.end.row <= gcanvas0->last_full.row)
				draw_handle = 1;
			/* TODO : do we want to add checking for pane above ? */
			else if (ic->pos.start.row < gcanvas->first.row)
				draw_handle = 0;
			else if (ic->pos.start.row != gcanvas->first.row)
				draw_handle = 2;
			else
				draw_handle = 3;
		}
		break;

	case ITEM_CURSOR_ANTED:
		draw_center   = TRUE;
		draw_thick    = 2;
		if (ic->state) {
			fore = &gs_light_gray;
			back = &gs_dark_gray;
		} else {
			fore = &gs_dark_gray;
			back = &gs_light_gray;
		}
	};

	if (ic->use_color) {
		fore = &ic->color;
		back = &ic->color;
	}

	ic->auto_fill_handle_at_top = (draw_handle >= 2);

	gdk_gc_set_clip_rectangle (ic->gc, &expose->area);

	/* Avoid guint16 overflow during line drawing.  We can change
	 * the shape we draw, so long as no lines or parts of
	 * rectangles are moved from outside to inside the clipping
	 * region */
	dx0 = MAX (dx0, expose->area.x - CLIP_SAFETY_MARGIN);
	dy0 = MAX (dy0, expose->area.y - CLIP_SAFETY_MARGIN);
	dx1 = MIN (dx1, expose->area.x + expose->area.width + CLIP_SAFETY_MARGIN);
	dy1 = MIN (dy1, expose->area.y + expose->area.height + CLIP_SAFETY_MARGIN);

	gdk_gc_set_line_attributes (ic->gc, 1,
		GDK_LINE_SOLID, GDK_CAP_BUTT, GDK_JOIN_MITER);
	gdk_gc_set_foreground (ic->gc, &gs_black);
	gdk_gc_set_background (ic->gc, &gs_white);

	if (draw_external) {
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
		gdk_draw_lines (drawable, ic->gc, points, 5);
	}

	if (draw_external && draw_internal) {
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
		gdk_draw_lines (drawable, ic->gc, points, 5);
	}

	if (draw_handle == 1 || draw_handle == 2) {
		int const y_off = (draw_handle == 1) ? dy1 - dy0 : 0;
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    dx1 - 2,
				    dy0 + y_off - 2,
				    2, 2);
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    dx1 + 1,
				    dy0 + y_off - 2,
				    2, 2);
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    dx1 - 2,
				    dy0 + y_off + 1,
				    2, 2);
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    dx1 + 1,
				    dy0 + y_off + 1,
				    2, 2);
	} else if (draw_handle == 3) {
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    dx1 - 2,
				    dy0 + 1,
				    2, 4);
		gdk_draw_rectangle (drawable, ic->gc, TRUE,
				    dx1 + 1,
				    dy0 + 1,
				    2, 4);
	}

	if (draw_center) {
		gdk_gc_set_foreground (ic->gc, fore);
		gdk_gc_set_background (ic->gc, back);

		if (draw_stippled) {
			gdk_gc_set_fill (ic->gc, GDK_STIPPLED);
			gdk_gc_set_stipple (ic->gc, ic->stipple);
			gdk_gc_set_line_attributes (ic->gc, draw_thick,
				GDK_LINE_SOLID, GDK_CAP_BUTT, GDK_JOIN_MITER);
		} else
			gdk_gc_set_line_attributes (ic->gc, draw_thick,
				GDK_LINE_DOUBLE_DASH, GDK_CAP_BUTT, GDK_JOIN_MITER);

		/* Stay in the boundary */
		if ((draw_thick % 2) == 0) {
			dx0++;
			dy0++;
		}
		gdk_draw_rectangle (drawable, ic->gc, FALSE,
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

	foo_canvas_item_request_update (FOO_CANVAS_ITEM (ic));

	return TRUE;
}

/**
 * item_cursor_reposition : Re-compute the pixel position of the cursor.
 *
 * When a sheet is zoomed.  The pixel coords shift slightly.  The item cursor
 * must regenerate to stay in sync.
 */
void
item_cursor_reposition (ItemCursor *ic)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ic);

	g_return_if_fail (item != NULL);

	/* Request an update */
	foo_canvas_item_request_update (item);
}

static double
item_cursor_point (FooCanvasItem *item, double x, double y, int cx, int cy,
		   FooCanvasItem **actual_item)
{
	ItemCursor const *ic = ITEM_CURSOR (item);

	/* Cursor should not always receive events
	 * 1) when invisible
	 * 2) when animated
	 * 3) while a guru is up
	 */
	if (!ic->visible || ic->style == ITEM_CURSOR_ANTED ||
	    wbcg_edit_has_guru (ic->scg->wbcg))
		return DBL_MAX;

	*actual_item = NULL;

	if (cx < item->x1-3)
		return DBL_MAX;
	if (cx > item->x2+3)
		return DBL_MAX;
	if (cy < item->y1-3)
		return DBL_MAX;
	if (cy > item->y2+3)
		return DBL_MAX;

	/* FIXME: the drag handle for ITEM_CURSOR_SELECTION needs work */
	if ((cx < (item->x1 + 4)) || (cx > (item->x2 - 8)) ||
	    (cy < (item->y1 + 4)) || (cy > (item->y2 - 8))) {
		*actual_item = item;
		return 0.0;
	}
	return DBL_MAX;
}

static void
item_cursor_setup_auto_fill (ItemCursor *ic, ItemCursor const *parent, int x, int y)
{
	Sheet const *sheet = sc_sheet (SHEET_CONTROL (parent->scg));
	GSList *merges;

	ic->base_x = x;
	ic->base_y = y;

	ic->autofill_src = parent->pos;

	/* If there are arrays or merges in the region ensure that we
	 * need to ensure that an integer multiple of the original size
	 * is filled.   We could be fancy about this an allow filling as long
	 * as the merges would not be split, bu that is more work than it is
	 * worth right now (FIXME this is a nice project).
	 *
	 * We do not have to be too careful, the sheet guarantees that the
	 * cursor does not split merges, all we need is existence.
	 */
	merges = sheet_merge_get_overlap (sheet, &ic->autofill_src);
	if (merges != NULL) {
		g_slist_free (merges);
		ic->autofill_hsize = range_width (&ic->autofill_src);
		ic->autofill_vsize = range_height (&ic->autofill_src);
	} else
		ic->autofill_hsize = ic->autofill_vsize = 1;
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
item_cursor_set_cursor (FooCanvas *canvas, ItemCursor *ic, int x, int y)
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
item_cursor_selection_event (FooCanvasItem *item, GdkEvent *event)
{
	FooCanvas	*canvas = item->canvas;
	GnmCanvas   *gcanvas = GNM_CANVAS (canvas);
	ItemCursor *ic = ITEM_CURSOR (item);
	int x, y;

	switch (event->type) {
	case GDK_ENTER_NOTIFY:
		foo_canvas_w2c (
			canvas, event->crossing.x, event->crossing.y, &x, &y);

		item_cursor_set_cursor (canvas, ic, x, y);
		return TRUE;

	case GDK_MOTION_NOTIFY: {
		int style, button;
		ItemCursor *special_cursor;

		foo_canvas_w2c (
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
		gnm_simple_canvas_ungrab (item, event->button.time);

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
			foo_canvas_update_now (canvas);

		gnm_simple_canvas_grab (FOO_CANVAS_ITEM (special_cursor),
			GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK,
			NULL, event->button.time);
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
		gnm_simple_canvas_ungrab (item, event->button.time);

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
				tmp = sheet_foreach_cell_in_range (sheet,
					CELL_ITER_IGNORE_BLANK,
					x, ic->pos.start.row, x, ic->pos.end.row,
					(CellIterFunc) cb_autofill_bound,
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
			tmp = sheet_foreach_cell_in_range (sheet,
				CELL_ITER_IGNORE_BLANK,
				ic->pos.start.col, ic->pos.end.row + 1,
				ic->pos.end.col, final_row,
				(CellIterFunc) cb_autofill_bound,
				GINT_TO_POINTER (FALSE));

			if (tmp) {
				int bound = value_get_as_int (tmp);
				if (final_row >= bound)
					final_row = bound - 1;
				value_release (tmp);
			}
		}

		/* fill the row/column */
		cmd_autofill (sc->wbc, sheet, FALSE,
			      ic->pos.start.col, ic->pos.start.row,
			      ic->pos.end.col - ic->pos.start.col + 1,
			      ic->pos.end.row - ic->pos.start.row + 1,
			      final_col, final_row,
			      FALSE);

		return TRUE;
	}

	case GDK_BUTTON_PRESS:
		/* NOTE : this cannot be called while we are editing.  because
		 * the point routine excludes events.  so we do not need to
		 * call wbcg_edit_finish.
		 */

		/* scroll wheel events dont have corresponding release events */
		if (event->button.button > 3)
			return FALSE;

		/* If another button is already down ignore this one */
		if (ic->drag_button >= 0)
			return TRUE;

		if (event->button.button != 3) {
			/* prepare to create fill or drag cursors, but dont until we
			 * move.  If we did create them here there would be problems
			 * with race conditions when the new cursors pop into existence
			 * during a double-click
			 */
			ic->drag_button = event->button.button;
			gnm_simple_canvas_grab (item,
				GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK,
				NULL, event->button.time);
		} else
			scg_context_menu (ic->scg, &event->button, FALSE, FALSE);
		return TRUE;

	case GDK_BUTTON_RELEASE:
		if (ic->drag_button != (int)event->button.button)
			return TRUE;

		/* Double clicks may have already released the drag prep */
		if (ic->drag_button >= 0) {
			gnm_simple_canvas_ungrab (item, event->button.time);
			ic->drag_button = -1;
		}
		return TRUE;

	default:
		return FALSE;
	}
}

static gboolean
item_cursor_target_region_ok (ItemCursor *ic)
{
	FooCanvasItem *gci = FOO_CANVAS_ITEM (ic);
	SheetControl	*sc = (SheetControl *) ic->scg;

	g_return_val_if_fail (gci != NULL, FALSE);
	g_return_val_if_fail (gci->canvas != NULL, FALSE);

	if (sv_is_region_empty_or_selected (sc->view, &ic->pos))
		return TRUE;

	return gnumeric_dialog_question_yes_no
		(ic->scg->wbcg,
		 _("The cells dragged will overwrite the contents of the\n"
		   "existing cells in that range.  Do you want me to replace\n"
		   "the contents in this region?"),
		 TRUE);
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
item_cursor_do_action (ItemCursor *ic, ActionType action)
{
	SheetControl	*sc;
	SheetView 	*sv;
	Sheet		*sheet;
	WorkbookControl *wbc;
	PasteTarget pt;

	g_return_if_fail (ic != NULL);

	if (action == ACTION_NONE || !item_cursor_target_region_ok (ic)) {
		scg_special_cursor_stop	(ic->scg);
		return;
	}

	sc = (SheetControl *) ic->scg;
	sheet = sc_sheet (sc);
	sv = sc_view (sc);
	wbc = sc_wbc (sc);

	switch (action) {
	case ACTION_COPY_CELLS:
		if (!sv_selection_copy (sv, wbc))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_ALL_TYPES));
		break;

	case ACTION_MOVE_CELLS:
		if (!sv_selection_cut (sv, wbc))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_ALL_TYPES));
		break;

	case ACTION_COPY_FORMATS:
		if (!sv_selection_copy (sv, wbc))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_FORMATS));
		break;

	case ACTION_COPY_VALUES:
		if (!sv_selection_copy (sv, wbc))
			break;
		cmd_paste (wbc,
			   paste_target_init (&pt, sheet, &ic->pos,
					      PASTE_AS_VALUES));
		break;

	case ACTION_SHIFT_DOWN_AND_COPY:
	case ACTION_SHIFT_RIGHT_AND_COPY:
	case ACTION_SHIFT_DOWN_AND_MOVE:
	case ACTION_SHIFT_RIGHT_AND_MOVE:
		g_warning ("Operation not yet implemented.");
		break;

	default :
		g_warning ("Invalid Operation %d.", action);
	}

	scg_special_cursor_stop	(ic->scg);
}

static gboolean
context_menu_hander (GnumericPopupMenuElement const *element,
		     gpointer ic)
{
	g_return_val_if_fail (element != NULL, TRUE);

	item_cursor_do_action (ic, element->index);
	return TRUE;
}

static void
item_cursor_popup_menu (ItemCursor *ic, GdkEventButton *event)
{
	static GnumericPopupMenuElement const popup_elements[] = {
		{ N_("_Move"),		NULL,
		    0, 0, ACTION_MOVE_CELLS },

		{ N_("_Copy"),		GTK_STOCK_COPY,
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
	SheetView const *sv = ((SheetControl *) ic->scg)->view;
	Range const *target = selection_first_range (sv, NULL, NULL);

	wb_control_gui_set_status_text (ic->scg->wbcg, "");
	if (range_equal (target, &ic->pos)) {
		scg_special_cursor_stop	(ic->scg);
		return;
	}

	if (event->button == 3)
		item_cursor_popup_menu (ic, event);
	else
		item_cursor_do_action (ic, (event->state & GDK_CONTROL_MASK)
				       ? ACTION_COPY_CELLS
				       : ACTION_MOVE_CELLS);
}

static void
item_cursor_set_bounds_visibly (ItemCursor *ic,
				int visible_col,int visible_row,
				CellPos const *corner,
				int end_col, int end_row)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ic);
	GnmCanvas  *gcanvas = GNM_CANVAS (item->canvas);
	Range r;

	/* Handle visibility here rather than in the slide handler because we
	 * need to constrain movement some times.  eg no sense scrolling the
	 * canvas if the autofill cursor is not changing size.
	 */
	scg_make_cell_visible (ic->scg, visible_col, visible_row, FALSE, TRUE);

	/* FIXME FIXME FIXME
	 * Ideally we would update the bounds BEFORE we scroll, this would
	 * decrease the flicker.  Unfortunately, our optimization of clipping
	 * the cursor to the currently visible region is getting in the way.
	 * We are forced to make the region visible before we move the cursor.
	 */
	range_init (&r, corner->col, corner->row, end_col, end_row);
	if (scg_special_cursor_bound_set (ic->scg, &r))
		foo_canvas_update_now (FOO_CANVAS (gcanvas));
}

void
item_cursor_set_visibility (ItemCursor *ic, gboolean visible)
{
	g_return_if_fail (IS_ITEM_CURSOR (ic));

	if (ic->visible == visible)
		return;

	ic->visible = visible;
	foo_canvas_item_request_update (FOO_CANVAS_ITEM (ic));
}

static void
item_cursor_tip_setlabel (ItemCursor *ic)
{
	if (ic->tip == NULL) {
		ic->tip = gnumeric_create_tooltip ();
		gnumeric_position_tooltip (ic->tip, TRUE);
		gtk_widget_show_all (gtk_widget_get_toplevel (ic->tip));
	}

	g_return_if_fail (ic->tip != NULL);
	gtk_label_set_text (GTK_LABEL (ic->tip), range_name (&ic->pos));
}

static gboolean
cb_move_cursor (GnmCanvas *gcanvas, int col, int row, gpointer user_data)
{
	ItemCursor *ic = user_data;
	int const w = (ic->pos.end.col - ic->pos.start.col);
	int const h = (ic->pos.end.row - ic->pos.start.row);
	CellPos corner;

	corner.col = col - ic->col_delta;
	if (corner.col < 0)
		corner.col = 0;
	else if (corner.col >= (SHEET_MAX_COLS - w))
		corner.col = SHEET_MAX_COLS - w - 1;

	corner.row = row - ic->row_delta;
	if (corner.row < 0)
		corner.row = 0;
	else if (corner.row >= (SHEET_MAX_ROWS - h))
		corner.row = SHEET_MAX_ROWS - h - 1;

	item_cursor_tip_setlabel (ic);

	/* Make target cell visible, and adjust the cursor size */
	item_cursor_set_bounds_visibly (ic, col, row, &corner,
					corner.col + w, corner.row + h);
	return FALSE;
}

static void
item_cursor_handle_motion (ItemCursor *ic, GdkEvent *event,
			   GnmCanvasSlideHandler slide_handler)
{
	FooCanvas *canvas = FOO_CANVAS_ITEM (ic)->canvas;

	gnm_canvas_handle_motion (GNM_CANVAS (canvas),
		canvas, &event->motion,
		GNM_CANVAS_SLIDE_X | GNM_CANVAS_SLIDE_Y | GNM_CANVAS_SLIDE_AT_COLROW_BOUND,
		slide_handler, ic);
}

static gint
item_cursor_drag_event (FooCanvasItem *item, GdkEvent *event)
{
	ItemCursor *ic = ITEM_CURSOR (item);

	switch (event->type) {
	case GDK_BUTTON_RELEASE:
		/* Note : see comment below, and bug 30507 */
		if ((int)event->button.button == ic->drag_button) {
			gnm_canvas_slide_stop (GNM_CANVAS (item->canvas));
			gnm_simple_canvas_ungrab (item, event->button.time);
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
cb_autofill_scroll (GnmCanvas *gcanvas, int col, int row, gpointer user_data)
{
	ItemCursor *ic = user_data;
	Range r = ic->autofill_src;

	/* compass offsets are distances (in cells) from the edges of the
	 * selected area to the mouse cursor
	 */
	int north_offset = r.start.row - row;
	int south_offset = row - r.end.row;
	int west_offset  = r.start.col - col;
	int east_offset  = col - r.end.col;

	/* Autofill by row or by col, NOT both. */
	if ( MAX (north_offset, south_offset) > MAX (west_offset, east_offset) ) {
		if (row < r.start.row)
			r.start.row -= ic->autofill_vsize * (int)(north_offset / ic->autofill_vsize);
		else
			r.end.row   += ic->autofill_vsize * (int)(south_offset / ic->autofill_vsize);
		if (col < r.start.col)
			col = r.start.col;
		else if (col > r.end.col)
			col = r.end.col;
	} else {
		if (col < r.start.col)
			r.start.col -= ic->autofill_hsize * (int)(west_offset / ic->autofill_hsize);
		else
			r.end.col   += ic->autofill_hsize * (int)(east_offset / ic->autofill_hsize);
		if (row < r.start.row)
			row = r.start.row;
		else if (row > r.end.row)
			row = r.end.row;
	}

	item_cursor_set_bounds_visibly (ic, col, row,
		&r.start, r.end.col, r.end.row);

	return FALSE;
}

static gint
item_cursor_autofill_event (FooCanvasItem *item, GdkEvent *event)
{
	ItemCursor *ic = ITEM_CURSOR (item);
	SheetControl *sc = (SheetControl *) ic->scg;
	gboolean inverse_autofill;

	switch (event->type) {
	case GDK_BUTTON_RELEASE:
		gnm_canvas_slide_stop (GNM_CANVAS (item->canvas));

		gnm_simple_canvas_ungrab (item, event->button.time);

		inverse_autofill = (ic->pos.start.col < ic->autofill_src.start.col ||
				    ic->pos.start.row < ic->autofill_src.start.row);

		cmd_autofill (sc->wbc, sc->sheet,
			      event->button.state & GDK_CONTROL_MASK,
			      ic->pos.start.col, ic->pos.start.row,
			      range_width (&ic->autofill_src),
			      range_height (&ic->autofill_src),
			      ic->pos.end.col, ic->pos.end.row,
			      inverse_autofill);

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
item_cursor_event (FooCanvasItem *item, GdkEvent *event)
{
	ItemCursor *ic = ITEM_CURSOR (item);

	/* While editing nothing should be draggable */
	if (wbcg_is_editing (ic->scg->wbcg))
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
	FooCanvasItem *item;
	ItemCursor *ic;

	item = FOO_CANVAS_ITEM (o);
	ic = ITEM_CURSOR (o);

	switch (arg_id) {
	case ARG_SHEET_CONTROL_GUI:
		ic->scg = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_STYLE:
		ic->style = GTK_VALUE_INT (*arg);
		break;
	case ARG_BUTTON :
		ic->drag_button = GTK_VALUE_INT (*arg);
		break;
	case ARG_COLOR: {
		GdkColor color;
		char *color_name = GTK_VALUE_STRING (*arg);
		if (foo_canvas_get_color (item->canvas, color_name, &color)) {
			ic->color = color;
			ic->use_color = 1;
		}
	}
	}
}

typedef struct {
	FooCanvasItemClass parent_class;
} ItemCursorClass;

/*
 * ItemCursor class initialization
 */
static void
item_cursor_class_init (ItemCursorClass *item_cursor_class)
{
	GtkObjectClass  *object_class;
	FooCanvasItemClass *item_class;

	item_cursor_parent_class = g_type_class_peek_parent (item_cursor_class);

	object_class = (GtkObjectClass *) item_cursor_class;
	item_class = (FooCanvasItemClass *) item_cursor_class;

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

	/* FooCanvasItem method overrides */
	item_class->update      = item_cursor_update;
	item_class->realize     = item_cursor_realize;
	item_class->unrealize   = item_cursor_unrealize;
	item_class->draw        = item_cursor_draw;
	item_class->point       = item_cursor_point;
	item_class->event       = item_cursor_event;
}

static void
item_cursor_init (ItemCursor *ic)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (ic);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 1;
	item->y2 = 1;

	ic->pos.start.col = 0;
	ic->pos.end.col   = 0;
	ic->pos.start.row = 0;
	ic->pos.end.row   = 0;

	ic->col_delta = 0;
	ic->row_delta = 0;

	ic->tip = NULL;

	ic->style = ITEM_CURSOR_SELECTION;
	ic->gc = NULL;
	ic->state = 0;
	ic->animation_timer = -1;

	ic->visible = TRUE;
	ic->auto_fill_handle_at_top = FALSE;
	ic->auto_fill_handle_at_left = FALSE;
	ic->drag_button = -1;
}

GSF_CLASS (ItemCursor, item_cursor,
	   item_cursor_class_init, item_cursor_init,
	   FOO_TYPE_CANVAS_ITEM);
