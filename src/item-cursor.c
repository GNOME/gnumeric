/*
 * The Cursor Canvas Item: Implements a rectangular cursor
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "item-cursor.h"
#include "gnumeric-sheet.h"
#include "color.h"
#include "clipboard.h"
#include "cursors.h"
#include "selection.h"
#include "workbook.h"
#include "workbook-edit.h"
#include "gnumeric-util.h"
#include "cmd-edit.h"
#include "commands.h"

static GnomeCanvasItem *item_cursor_parent_class;

#define AUTO_HANDLE_SPACE	4
#define CLIP_SAFETY_MARGIN      (AUTO_HANDLE_SPACE + 5)
#define IS_LITTLE_SQUARE(item,x,y) \
	(((x) > (item)->canvas_item.x2 - 6) && \
	 ((item->auto_fill_handle_at_top && ((y) < (item)->canvas_item.y1 + 6)) || \
	  ((y) > (item)->canvas_item.y2 - 6)))

/* The argument we take */
enum {
	ARG_0,
	ARG_SHEETVIEW,		/* The SheetView * argument */
	ARG_ITEM_GRID,		/* The ItemGrid * argument */
	ARG_STYLE,              /* The style type */
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
	if (item_cursor->tag == -1)
		return;

	gtk_timeout_remove (item_cursor->tag);
	item_cursor->tag = -1;
}

static void
item_cursor_start_animation (ItemCursor *item_cursor)
{
	item_cursor->tag = gtk_timeout_add (
		150, (GtkFunction)(item_cursor_animation_callback),
		item_cursor);
}

static void
item_cursor_destroy (GtkObject *object)
{
	ItemCursor *item_cursor;

	item_cursor = ITEM_CURSOR (object);

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
	GnumericSheet *gsheet = GNUMERIC_SHEET (item->canvas);
	Sheet         *sheet = item_cursor->sheet_view->sheet;

	int x, y, w, h, extra;

	/* Clip the bounds of the cursor to the visible region of cells */
	int const left = MAX (gsheet->col.first-1, item_cursor->pos.start.col);
	int const right = MIN (gsheet->col.last_visible, item_cursor->pos.end.col);
	int const top = MAX (gsheet->row.first-1, item_cursor->pos.start.row);
	int const bottom = MIN (gsheet->row.last_visible, item_cursor->pos.end.row);

	/* Erase the old cursor */
	item_cursor_request_redraw (item_cursor);

	item_cursor->cached_x = x =
	    gsheet->col_offset.first +
	    sheet_col_get_distance_pixels (sheet, gsheet->col.first, left);
	item_cursor->cached_y = y =
	    gsheet->row_offset.first +
	    sheet_row_get_distance_pixels (sheet, gsheet->row.first, top);
	item_cursor->cached_w = w =
	    sheet_col_get_distance_pixels (sheet, left, right+1);
	item_cursor->cached_h = h =
	    sheet_row_get_distance_pixels (sheet, top, bottom+1);

	item->x1 = x - 1;
	item->y1 = y - 1;

	if (item_cursor->style == ITEM_CURSOR_SELECTION)
		extra = 1;
	else
		extra = 0;
	item->x2 = x + w + 2 + extra;
	item->y2 = y + h + 2 + extra;

	gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent), item);

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
			GnumericSheet   *gsheet = GNUMERIC_SHEET (item->canvas);
			if (item_cursor->pos.end.row <= gsheet->row.last_full)
				draw_handle = 1;
			else if (item_cursor->pos.start.row < gsheet->row.first)
				draw_handle = 0;
			else if (item_cursor->pos.start.row != gsheet->row.first)
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
item_cursor_set_bounds (ItemCursor *item_cursor, int start_col, int start_row, int end_col, int end_row)
{
	GnomeCanvasItem *item;

	g_return_val_if_fail (start_col <= end_col, FALSE);
	g_return_val_if_fail (start_row <= end_row, FALSE);
	g_return_val_if_fail (start_col >= 0, FALSE);
	g_return_val_if_fail (start_row >= 0, FALSE);
	g_return_val_if_fail (end_col < SHEET_MAX_COLS, FALSE);
	g_return_val_if_fail (end_row < SHEET_MAX_ROWS, FALSE);
	g_return_val_if_fail (IS_ITEM_CURSOR (item_cursor), FALSE);

	/* Nothing changed */
	if (item_cursor->pos.start.col == start_col &&
	    item_cursor->pos.end.col   == end_col &&
	    item_cursor->pos.start.row == start_row &&
	    item_cursor->pos.end.row   == end_row)
		return FALSE;

	/* Move to the new area */
	item_cursor->pos.start.col = start_col;
	item_cursor->pos.end.col   = end_col;
	item_cursor->pos.start.row = start_row;
	item_cursor->pos.end.row   = end_row;

	/* Request an update */
	item = GNOME_CANVAS_ITEM (item_cursor);
	gnome_canvas_item_request_update (item);

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
	return INT_MAX;
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
	item_cursor->base_col = parent->pos.start.col;
	item_cursor->base_row = parent->pos.start.row;
}

static void
item_cursor_set_cursor (GnomeCanvas *canvas, GnomeCanvasItem *item, int x, int y)
{
	ItemCursor *item_cursor = ITEM_CURSOR (item);
	int cursor;

	if (IS_LITTLE_SQUARE (item_cursor, x, y))
		cursor = GNUMERIC_CURSOR_THIN_CROSS;
	else
		cursor = GNUMERIC_CURSOR_ARROW;

	cursor_set_widget (canvas, cursor);
}

static gint
item_cursor_selection_event (GnomeCanvasItem *item, GdkEvent *event)
{
	GnomeCanvas	*canvas = item->canvas;
	GnumericSheet   *gsheet = GNUMERIC_SHEET (canvas);
	GnomeCanvasItem *new_item;
	ItemCursor *ic = ITEM_CURSOR (item);
	int x, y;

	switch (event->type){
	case GDK_ENTER_NOTIFY:
		gnome_canvas_w2c (
			canvas, event->crossing.x, event->crossing.y, &x, &y);

		item_cursor_set_cursor (canvas, item, x, y);
		return TRUE;

	case GDK_MOTION_NOTIFY:
		gnome_canvas_w2c (
			canvas, event->motion.x, event->motion.y, &x, &y);

		item_cursor_set_cursor (canvas, item, x, y);
		return TRUE;

	case GDK_BUTTON_PRESS: {
		GnomeCanvasGroup *group;
		int style;

		gnome_canvas_w2c (
			canvas, event->button.x, event->button.y, &x, &y);
		group = GNOME_CANVAS_GROUP (canvas->root);

		/*
		 * determine which part of the cursor was clicked:
		 * the border or the handlebox
		 */
		if (IS_LITTLE_SQUARE (ic, x, y))
			style = ITEM_CURSOR_AUTOFILL;
		else
			style = ITEM_CURSOR_DRAG;

		new_item = gnome_canvas_item_new (
			group,
			item_cursor_get_type (),
			"ItemCursor::SheetView", ic->sheet_view,
			"ItemCursor::Grid",  ic->item_grid,
			"ItemCursor::Style", style,
			NULL);

		if (style == ITEM_CURSOR_AUTOFILL)
			item_cursor_setup_auto_fill (
				ITEM_CURSOR (new_item), ic, x, y);

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
			int d_col =
			    gnumeric_sheet_find_col (gsheet, x, NULL) -
			    ic->pos.start.col;
			int d_row =
			    gnumeric_sheet_find_row (gsheet, y, NULL) -
			    ic->pos.start.row;

			if (d_col < 0)
				d_col = 0;
			else {
				int tmp = ic->pos.end.col - ic->pos.start.col;
				if (d_col > tmp)
					d_col = tmp;
			}
			ITEM_CURSOR (new_item)->col_delta = d_col;

			if (d_row < 0)
				d_row = 0;
			else {
				int tmp = ic->pos.end.row - ic->pos.start.row;
				if (d_row > tmp)
					d_row = tmp;
			}
			ITEM_CURSOR (new_item)->row_delta = d_row;
		}

		if (item_cursor_set_bounds (ITEM_CURSOR (new_item),
					    ic->pos.start.col, ic->pos.start.row,
					    ic->pos.end.col,   ic->pos.end.row))
			gnome_canvas_update_now (canvas);

		gnome_canvas_item_grab (
			new_item,
			GDK_POINTER_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
			NULL,
			event->button.time);

		return TRUE;
	}
	default:
		return FALSE;
	}

}

static gboolean
item_cursor_target_region_ok (ItemCursor *item_cursor)
{
	GtkWidget *message;
	GnomeCanvas *canvas = GNOME_CANVAS_ITEM (item_cursor)->canvas;
	GtkWidget *window;
	int v;

	v = sheet_is_region_empty_or_selected (
		item_cursor->sheet_view->sheet,
		item_cursor->pos.start.col, item_cursor->pos.start.row,
		item_cursor->pos.end.col, item_cursor->pos.end.row);

	if (v)
		return TRUE;

	message = gnome_message_box_new (
		_("The cells dragged will overwrite the contents of the\n"
		  "existing cells in that range.  Do you want me to replace\n"
		  "the contents in this region?"),
		GNOME_MESSAGE_BOX_WARNING,
		GNOME_STOCK_BUTTON_YES,
		GNOME_STOCK_BUTTON_NO,
		NULL);
	window = gtk_widget_get_toplevel (GTK_WIDGET (canvas));
	v = gnumeric_dialog_run (item_cursor->sheet_view->sheet->workbook, GNOME_DIALOG (message));

	if (v == 0)
		return TRUE;
	return FALSE;
}

typedef enum {
	ACTION_NONE = -1,
	ACTION_COPY_CELLS,
	ACTION_MOVE_CELLS,
	ACTION_COPY_FORMATS,
	ACTION_COPY_VALUES,
	ACTION_SHIFT_DOWN_AND_COPY,
	ACTION_SHIFT_RIGHT_AND_COPY,
	ACTION_SHIFT_DOWN_AND_MOVE,
	ACTION_SHIFT_RIGHT_AND_MOVE
} ActionType;

static void
item_cursor_do_action (ItemCursor *item_cursor, ActionType action, guint32 time)
{
	Sheet *sheet = item_cursor->sheet_view->sheet;
	Workbook *wb = sheet->workbook;
	PasteTarget pt;

	switch (action){
	case ACTION_NONE:
		return;

	case ACTION_COPY_CELLS:
		if (!item_cursor_target_region_ok (item_cursor))
			return;
		if (!sheet_selection_copy (workbook_command_context_gui (wb), sheet))
			return;
		cmd_paste (workbook_command_context_gui (wb),
			   paste_target_init (&pt, sheet, &item_cursor->pos, PASTE_ALL_TYPES),
			   time);
		return;

	case ACTION_MOVE_CELLS:
		if (!item_cursor_target_region_ok (item_cursor))
			return;
		if (!sheet_selection_cut (workbook_command_context_gui (wb), sheet))
			return;
		cmd_paste (workbook_command_context_gui (wb),
			   paste_target_init (&pt, sheet, &item_cursor->pos, PASTE_ALL_TYPES),
			   time);
		return;

	case ACTION_COPY_FORMATS:
		if (!item_cursor_target_region_ok (item_cursor))
			return;
		if (!sheet_selection_copy (workbook_command_context_gui (wb), sheet))
			return;
		cmd_paste (workbook_command_context_gui (wb),
			   paste_target_init (&pt, sheet, &item_cursor->pos, PASTE_FORMATS),
			   time);
		return;

	case ACTION_COPY_VALUES:
		if (!item_cursor_target_region_ok (item_cursor))
			return;
		if (!sheet_selection_copy (workbook_command_context_gui (wb), sheet))
			return;
		cmd_paste (workbook_command_context_gui (wb),
			   paste_target_init (&pt, sheet, &item_cursor->pos, PASTE_VALUES),
			   time);
		return;

	case ACTION_SHIFT_DOWN_AND_COPY:
	case ACTION_SHIFT_RIGHT_AND_COPY:
	case ACTION_SHIFT_DOWN_AND_MOVE:
	case ACTION_SHIFT_RIGHT_AND_MOVE:
		g_warning ("Operation not yet implemented\n");
	}
}

static char *drop_context_actions [] = {
	N_("Copy"),
	N_("Move"),
	N_("Copy formats"),
	N_("Copy values"),
	N_("Shift cells down and copy"),
	N_("Shift cells right and copy"),
	N_("Shift cells down and move"),
	N_("Shift cells right and move"),
	NULL,
};

/*
 * Invoked when the item has been dropped
 */
static void
item_cursor_do_drop (ItemCursor *item_cursor, GdkEventButton *event)
{
	ActionType action;

	/* Find out what to do */
	if (event->button == 3)
		action = (ActionType) run_popup_menu ((GdkEvent *)event, 1, drop_context_actions);
	else if (event->state & GDK_CONTROL_MASK)
		action = ACTION_COPY_CELLS;
	else
		action = ACTION_MOVE_CELLS;

	item_cursor_do_action (item_cursor, action, event->time);
}

static void
item_cursor_set_bounds_visibly (ItemCursor *item_cursor,
				int const visible_col,	int const visible_row,
				int const start_col,	int const start_row,
				int const end_col,	int const end_row)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_cursor);
	GnumericSheet   *gsheet = GNUMERIC_SHEET (item->canvas);

	/*
	 * FIXME FIXME FIXME
	 * Ideally we would update the bounds BEFORE we scroll,
	 * this would decrease the flicker.  Unfortunately, our optimization
	 * of clipping the cursor to the currently visible region is getting in the way.
	 * We are forced to make the region visible before we move the cursor.
	 */
	gnumeric_sheet_make_cell_visible (gsheet, visible_col, visible_row, FALSE);
	if (item_cursor_set_bounds (item_cursor, start_col, start_row, end_col, end_row))
		gnome_canvas_update_now (GNOME_CANVAS (gsheet));
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

void
item_cursor_set_spin_base (ItemCursor *item_cursor, int col, int row)
{
	g_return_if_fail (IS_ITEM_CURSOR (item_cursor));

	item_cursor->base_col = col;
	item_cursor->base_row = row;
}

#if 0
/*
 * This can not be used yet.  It exercises a bug in GtkLayout
 */
static void
item_cursor_tip_setlabel (ItemCursor *item_cursor)
{
	char buffer [32];
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
	tmp = snprintf (buffer, sizeof (buffer), "%s%d",
			col_name (src->start.col),
			src->start.row + 1);

	if (src->start.col != src->end.col || src->start.row != src->end.row)
		snprintf (buffer+tmp, sizeof (buffer)-tmp, ":%s%d",
			  col_name (src->end.col),
			  src->end.row + 1);

	gtk_label_set_text (GTK_LABEL (item_cursor->tip), buffer);
}
#endif

static gboolean
cb_move_cursor (SheetView *sheet_view, int col, int row, gpointer user_data)
{
	ItemCursor *item_cursor = user_data;
	int const w = (item_cursor->pos.end.col - item_cursor->pos.start.col);
	int const h = (item_cursor->pos.end.row - item_cursor->pos.start.row);
	int corner_left = col - item_cursor->col_delta;
	int corner_top = row - item_cursor->row_delta;

	if (corner_left < 0)
		corner_left = 0;
	else if (corner_left >= (SHEET_MAX_COLS - w))
		corner_left = SHEET_MAX_COLS - w;

	if (corner_top < 0)
		corner_top = 0;
	else if (corner_top >= (SHEET_MAX_ROWS - h))
		corner_top = SHEET_MAX_ROWS - h;

#if 0
	/*
	 * Leave this disabled until GtkLayout correctly handles
	 * Windows with SaveUnder set. (Speak to quartic for details).
	 */
	item_cursor_tip_setlabel (item_cursor);
#endif

	/* Make target cell visible, and adjust the cursor size */
	item_cursor_set_bounds_visibly (item_cursor, col, row,
					corner_left, corner_top,
					corner_left + w, corner_top + h);
	return FALSE;
}

static void
item_cursor_handle_motion (ItemCursor *item_cursor, GdkEvent *event,
			   SheetViewSlideHandler slide_handler)
{
	GnomeCanvas *canvas = GNOME_CANVAS_ITEM (item_cursor)->canvas;
	GnumericSheet *gsheet = GNUMERIC_SHEET (canvas);
	int x, y;
	int left, top;
	int width, height;
	int col, row;

	gnome_canvas_w2c (canvas, event->button.x, event->button.y, &x, &y);
	gnome_canvas_get_scroll_offsets (canvas, &left, &top);

	width = GTK_WIDGET (canvas)->allocation.width;
	height = GTK_WIDGET (canvas)->allocation.height;

	col = gnumeric_sheet_find_col (gsheet, x, NULL);
	row = gnumeric_sheet_find_row (gsheet, y, NULL);

	if (x < left || y < top || x >= left + width || y >= top + height) {
		int dx = 0, dy = 0;

		if (x < left)
			dx = x - left;
		else if (x >= left + width)
			dx = x - width - left;

		if (y < top)
			dy = y - top;
		else if (y >= top + height)
			dy = y - height - top;

		if (sheet_view_start_sliding (item_cursor->sheet_view,
					      slide_handler, item_cursor,
					      col, row, dx, dy))
			return;
	}
	sheet_view_stop_sliding (item_cursor->sheet_view);

	(*slide_handler) (item_cursor->sheet_view, col, row, item_cursor);
}

static gint
item_cursor_drag_event (GnomeCanvasItem *item, GdkEvent *event)
{
	ItemCursor *item_cursor = ITEM_CURSOR (item);

	switch (event->type){
	case GDK_BUTTON_RELEASE:
		sheet_view_stop_sliding (item_cursor->sheet_view);

		gnome_canvas_item_ungrab (item, event->button.time);
		item_cursor_do_drop (item_cursor, (GdkEventButton *) event);

		if (item_cursor->tip) {
			gtk_widget_destroy (gtk_widget_get_toplevel (item_cursor->tip));
			item_cursor->tip = NULL;
		}

		gtk_object_destroy (GTK_OBJECT (item));
		return TRUE;

	case GDK_BUTTON_PRESS:
		/* We actually never get this event: this kind of
		 * cursor is created and grabbed
		 */
		return TRUE;

	case GDK_MOTION_NOTIFY:
		item_cursor_handle_motion (item_cursor, event, &cb_move_cursor);
		return TRUE;

	default:
		return FALSE;
	}
}

static gboolean
cb_autofill_scroll (SheetView *sheet_view, int col, int row, gpointer user_data)
{
	ItemCursor *item_cursor = user_data;
	int bottom = item_cursor->base_row + item_cursor->base_rows;
	int right = item_cursor->base_col + item_cursor->base_cols;

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
	if (row < item_cursor->base_row)
		row = item_cursor->base_row;
	if (col < item_cursor->base_col)
		col = item_cursor->base_col;

	item_cursor_set_bounds_visibly (item_cursor, col, row,
					item_cursor->base_col,
					item_cursor->base_row,
					right, bottom);
	return FALSE;
}

static gint
item_cursor_autofill_event (GnomeCanvasItem *item, GdkEvent *event)
{
	ItemCursor *item_cursor = ITEM_CURSOR (item);

	switch (event->type){
	case GDK_BUTTON_RELEASE: {
		Sheet *sheet = item_cursor->sheet_view->sheet;

		sheet_view_stop_sliding (item_cursor->sheet_view);

		/*
		 * We flush after the ungrab, to have the ungrab take
		 * effect inmediately (the copy operation might take
		 * long, and we do not want the mouse to be grabbed
		 * all this time).
		 */
		gnome_canvas_item_ungrab (item, event->button.time);
		gdk_flush ();

		workbook_finish_editing (sheet->workbook, TRUE);
		cmd_autofill (workbook_command_context_gui (sheet->workbook), sheet,
			      item_cursor->base_col,    item_cursor->base_row,
			      item_cursor->base_cols+1, item_cursor->base_rows+1,
			      item_cursor->pos.end.col, item_cursor->pos.end.row);

		gtk_object_destroy (GTK_OBJECT (item));

		return TRUE;
	}

	case GDK_MOTION_NOTIFY:
		item_cursor_handle_motion (item_cursor, event, &cb_autofill_scroll);
		return TRUE;

	default:
		return FALSE;
	}
}

static gint
item_cursor_event (GnomeCanvasItem *item, GdkEvent *event)
{
	ItemCursor *item_cursor = ITEM_CURSOR (item);

	switch (item_cursor->style){
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

/*
 * Instance initialization
 */
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
	item_cursor->tip       = NULL;

	item_cursor->style = ITEM_CURSOR_SELECTION;
	item_cursor->tag = -1;
	item_cursor->auto_fill_handle_at_top = FALSE;
	item_cursor->visible = 1;
}

static void
item_cursor_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ItemCursor *item_cursor;
	
	item = GNOME_CANVAS_ITEM (o);
	item_cursor = ITEM_CURSOR (o);

	switch (arg_id){
	case ARG_SHEETVIEW:
		item_cursor->sheet_view = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_ITEM_GRID:
		item_cursor->item_grid = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_STYLE:
		item_cursor->style = GTK_VALUE_INT (*arg);
		break;

	case ARG_COLOR: {
		GdkColor color;
		char *color_name;
		
		color_name = GTK_VALUE_STRING (*arg);
		if (gnome_canvas_get_color (item->canvas, color_name, &color)){
			item_cursor->color = color;
			item_cursor->use_color = 1;
		}
	}
	}
}

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

	gtk_object_add_arg_type ("ItemCursor::SheetView", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEETVIEW);
	gtk_object_add_arg_type ("ItemCursor::Grid", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_ITEM_GRID);
	gtk_object_add_arg_type ("ItemCursor::Style", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_STYLE);
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

GtkType
item_cursor_get_type (void)
{
	static GtkType item_cursor_type = 0;

	if (!item_cursor_type) {
		GtkTypeInfo item_cursor_info = {
			"ItemCursor",
			sizeof (ItemCursor),
			sizeof (ItemCursorClass),
			(GtkClassInitFunc) item_cursor_class_init,
			(GtkObjectInitFunc) item_cursor_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		item_cursor_type = gtk_type_unique (gnome_canvas_item_get_type (), &item_cursor_info);
	}

	return item_cursor_type;
}
