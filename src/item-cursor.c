/*
 * The Cursor Canvas Item: Implements a rectangular cursor
 *
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "item-grid.h"
#include "item-cursor.h"
#include "item-debug.h"
#include "gnumeric-sheet.h"
#include "gnumeric-util.h"

static GnomeCanvasItem *item_cursor_parent_class;

static void item_cursor_request_redraw (ItemCursor *item_cursor);

/* The argument we take */
enum {
	ARG_0,
	ARG_SHEET,		/* The Sheet * argument */
	ARG_ITEM_GRID,		/* The ItemGrid * argument */
	ARG_STYLE               /* The style type */
};

static void
item_cursor_animation_callback (ItemCursor *item_cursor)
{
	item_cursor->state = !item_cursor->state;
	item_cursor_request_redraw (item_cursor);
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
		300, (GtkFunction)(item_cursor_animation_callback),
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
	GdkGC      *gc;
	
	if (GNOME_CANVAS_ITEM_CLASS (item_cursor_parent_class)->realize)
		(*GNOME_CANVAS_ITEM_CLASS (item_cursor_parent_class)->realize)(item);
	
	item_cursor = ITEM_CURSOR (item);
	window = GTK_WIDGET (item->canvas)->window;

	gc = item_cursor->gc = gdk_gc_new (window);

	gnumeric_sheet_color_alloc (item->canvas);

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
item_cursor_reconfigure (GnomeCanvasItem *item)
{
}

/*
 * Returns the bounding box cordinates for box delimited by the cursor
 */
static void
item_cursor_get_pixel_coords (ItemCursor *item_cursor, int *x, int *y, int *w, int *h)
{
	ItemGrid *item_grid = item_cursor->item_grid;
	Sheet *sheet = item_cursor->sheet;

	*x = sheet_col_get_distance (sheet, item_grid->left_col, item_cursor->start_col);
	*y = sheet_row_get_distance (sheet, item_grid->top_row, item_cursor->start_row);

	*w = sheet_col_get_distance (sheet, item_cursor->start_col, item_cursor->end_col+1);
	*h = sheet_row_get_distance (sheet, item_cursor->start_row, item_cursor->end_row+1);
}

static void
item_cursor_configure_bounds (ItemCursor *item_cursor)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_cursor);
	int x, y, w, h, extra;

	item_cursor_get_pixel_coords (item_cursor, &x, &y, &w, &h);

	item_cursor->cached_x = x;
	item_cursor->cached_y = y;
	item_cursor->cached_w = w;
	item_cursor->cached_h = h;
	
	item->x1 = x - 1;
	item->y1 = y - 1;

	if (item_cursor->style == ITEM_CURSOR_SELECTION)
		extra = 1;
	else
		extra = 0;
	item->x2 = x + w + 2 + extra;
	item->y2 = y + h + 2 + extra;

	gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent), item);
}

static void
item_cursor_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemCursor *item_cursor = ITEM_CURSOR (item);
	int xd, yd, dx, dy;
	int cursor_width, cursor_height, w, h;
	GdkPoint points [40];
	int draw_external, draw_internal, draw_handle, draw_center, draw_thick;
	int premove;
	GdkColor *fore = NULL, *back = NULL;
	
	item_cursor_get_pixel_coords (item_cursor, &xd, &yd,
				      &cursor_width, &cursor_height);

	w = cursor_width;
	h = cursor_height;
	
	/* determine if we need to recompute the bounding box */
	if (xd != item_cursor->cached_x ||
	    yd != item_cursor->cached_y ||
	    w  != item_cursor->cached_w ||
	    h  != item_cursor->cached_h){
		item_cursor_configure_bounds (item_cursor);
	}
	
	dx = xd - x;
	dy = yd - y;

	draw_external = 0;
	draw_internal = 0;
	draw_handle   = 0;
	draw_center   = 0;
	draw_thick    = 0;
	
	switch (item_cursor->style){
	case ITEM_CURSOR_AUTOFILL:
	case ITEM_CURSOR_DRAG:
		draw_center   = 1;
		draw_thick    = 1;
		fore          = &gs_black;
		back          = &gs_white;
		break;
		
	case ITEM_CURSOR_SELECTION:
		draw_internal = 1;
		draw_external = 1;
		draw_handle   = 1;
		break;

	case ITEM_CURSOR_EDITING:
		draw_external = 1;

	case ITEM_CURSOR_ANTED:
		draw_center   = 1;
		if (item_cursor->state){
			fore = &gs_light_gray;
			back = &gs_dark_gray;
		} else {
			fore = &gs_dark_gray;
			back = &gs_light_gray;
		}
	};

	if (draw_handle)
		premove = 5;
	else
		premove = 0;

	gdk_gc_set_line_attributes (item_cursor->gc, 1,
				    GDK_LINE_SOLID, -1, -1);
	gdk_gc_set_foreground (item_cursor->gc, &gs_black);
	gdk_gc_set_background (item_cursor->gc, &gs_white);
	if (draw_external){
		points [0].x = dx + cursor_width + 1;
		points [0].y = dy + cursor_height + 1 - premove;
		points [1].x = points [0].x;
		points [1].y = dy - 1;
		points [2].x = dx - 1;
		points [2].y = dy - 1;
		points [3].x = dx - 1;
		points [3].y = dy + cursor_height + 1;
		points [4].x = dx + cursor_width + 1 - premove;
		points [4].y = points [3].y;
		gdk_draw_lines (drawable, item_cursor->gc, points, 5);
	}

	if (draw_external && draw_internal){
		points [0].x -= 2;
		points [1].x -= 2;
		points [1].y += 2;
		points [2].x += 2;
		points [2].y += 2;
		points [3].x += 2;
		points [3].y -= 2;
		points [4].y -= 2;
		gdk_draw_lines (drawable, item_cursor->gc, points, 5);
	}

	if (draw_handle){
		gdk_draw_rectangle (drawable, item_cursor->gc, TRUE,
				    dx + cursor_width - 2,
				    dy + cursor_height - 2,
				    2, 2);
		gdk_draw_rectangle (drawable, item_cursor->gc, TRUE,
				    dx + cursor_width + 1,
				    dy + cursor_height - 2,
				    2, 2);
		gdk_draw_rectangle (drawable, item_cursor->gc, TRUE,
				    dx + cursor_width - 2,
				    dy + cursor_height + 1,
				    2, 2);
		gdk_draw_rectangle (drawable, item_cursor->gc, TRUE,
				    dx + cursor_width + 1,
				    dy + cursor_height + 1,
				    2, 2);
	}

	if (draw_center){
		gdk_gc_set_foreground (item_cursor->gc, fore);
		gdk_gc_set_background (item_cursor->gc, back);

		if (draw_thick){
			gdk_gc_set_fill (item_cursor->gc, GDK_STIPPLED);
			gdk_gc_set_stipple (item_cursor->gc, item_cursor->stipple);
			gdk_gc_set_line_attributes (item_cursor->gc, 3,
						    GDK_LINE_SOLID, -1, -1);
		} else {
			gdk_gc_set_line_attributes (item_cursor->gc, 1,
						    GDK_LINE_DOUBLE_DASH, -1, -1);
		}

		gdk_draw_rectangle (drawable, item_cursor->gc, FALSE,
				    dx, dy,
				    cursor_width, cursor_height);
	}
}

static void
item_cursor_request_redraw (ItemCursor *item_cursor)
{
	GnomeCanvas *canvas = GNOME_CANVAS_ITEM (item_cursor)->canvas;
	int x, y, w, h;

	item_cursor_get_pixel_coords (item_cursor, &x, &y, &w, &h);
	gnome_canvas_request_redraw (canvas, x - 2, y - 2, x + w + 5, y + h + 5);
}

void
item_cursor_set_bounds (ItemCursor *item_cursor, int start_col, int start_row, int end_col, int end_row)
{
	GnomeCanvasItem *item;
	
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);
	g_return_if_fail (item_cursor != NULL);
	g_return_if_fail (IS_ITEM_CURSOR (item_cursor));

	item = GNOME_CANVAS_ITEM (item_cursor);
	item_cursor_request_redraw (item_cursor);

	item_cursor->start_col = start_col;
	item_cursor->end_col   = end_col;
	item_cursor->start_row = start_row;
	item_cursor->end_row   = end_row;

	item_cursor_request_redraw (item_cursor);

	item_cursor_configure_bounds (item_cursor);
}

static double
item_cursor_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		   GnomeCanvasItem **actual_item)
{
	*actual_item = NULL;

	if (cx < item->x1)
		return INT_MAX;
	if (cx > item->x2)
		return INT_MAX;
	if (cy < item->y1)
		return INT_MAX;
	if (cy > item->y2)
		return INT_MAX;

	/* FIXME: this needs to handle better the small little square case
	 * for ITEM_CURSOR_SELECTION style
	 */
	if ((cx < (item->x1 + 3)) ||
	    (cx > (item->x2 - 3)) ||
	    (cy < (item->y1 + 3)) ||
	    (cy > (item->y2 - 3))){
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

#define convert(c,sx,sy,x,y) gnome_canvas_w2c (c,sx,sy,x,y)

static gint
item_cursor_selection_event (GnomeCanvasItem *item, GdkEvent *event)
{
	GnomeCanvas *canvas = item->canvas;
	GnomeCanvasItem *new_item;
	ItemCursor *item_cursor = ITEM_CURSOR (item);
	int x, y;

	switch (event->type){
	case GDK_BUTTON_PRESS: {
		GnomeCanvasGroup *group;
		int style;

		printf ("cursor: got event\n");
		convert (canvas, event->button.x, event->button.y, &x, &y);
		
		group = GNOME_CANVAS_GROUP (canvas->root);

		/* determine which part of the cursor was clicked:
		 * the border or the handlebox
		 */
		if ((x > item->x2 - 6) && (y > item->y2 - 6))
			style = ITEM_CURSOR_AUTOFILL;
		else
			style = ITEM_CURSOR_DRAG;
		
		new_item = gnome_canvas_item_new (
			group,
			item_cursor_get_type (),
			"ItemCursor::Sheet", item_cursor->sheet,
			"ItemCursor::Grid",  item_cursor->item_grid,
			"ItemCursor::Style", style,
			NULL);
		item_cursor_set_bounds (
			ITEM_CURSOR (new_item),
			item_cursor->start_col, item_cursor->start_row,
			item_cursor->end_col,   item_cursor->end_row);
		
		printf ("Creating new cursor!\n");
		
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
	/* FIXME: check the destination range and if any cell
	 * has values, ask for confirmation
	 */
	return TRUE;
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
item_cursor_do_action (ItemCursor *item_cursor, ActionType action)
{
	Sheet *sheet = item_cursor->sheet;
	int   col = item_cursor->start_col;
	int   row = item_cursor->start_row;
	
	switch (action){
	case ACTION_NONE:
		return;
		
	case ACTION_COPY_CELLS:
		if (!item_cursor_target_region_ok (item_cursor))
			return;
		if (!sheet_selection_copy (sheet))
			return;
		sheet_selection_paste (sheet, col, row, PASTE_ALL_TYPES);
		return;
		
	case ACTION_MOVE_CELLS:
		if (!item_cursor_target_region_ok (item_cursor))
			return;
		if (!sheet_selection_cut (sheet))
			return;
		sheet_selection_paste (sheet, col, row, PASTE_ALL_TYPES);
		return;
		
	case ACTION_COPY_FORMATS:
		if (!item_cursor_target_region_ok (item_cursor))
			return;
		if (!sheet_selection_copy (sheet))
			return;
		sheet_selection_paste (sheet, col, row, PASTE_FORMATS);
		return;
		
	case ACTION_COPY_VALUES:
		if (!item_cursor_target_region_ok (item_cursor))
			return;
		if (!sheet_selection_copy (sheet))
			return;
		sheet_selection_paste (sheet, col, row, PASTE_VALUES);
		return;
		
	case ACTION_SHIFT_DOWN_AND_COPY:
	case ACTION_SHIFT_RIGHT_AND_COPY:
	case ACTION_SHIFT_DOWN_AND_MOVE:
	case ACTION_SHIFT_RIGHT_AND_MOVE:
		g_warning ("Operation not yet implemeneted\n");
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
item_cursor_do_drop (ItemCursor *item_cursor, GdkEvent *event)
{
	ActionType action;

	/* Find out what to do */
	if (event->button.button == 3)
		action = (ActionType) run_popup_menu (event, drop_context_actions);
	else if (event->button.state & GDK_CONTROL_MASK)
		action = ACTION_COPY_CELLS;
	else
		action = ACTION_MOVE_CELLS;

	item_cursor_do_action (item_cursor, action);
}

static void
item_cursor_set_bounds_visibly (ItemCursor *item_cursor,
				int start_col, int start_row,
				int end_col,   int end_row)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_cursor);
	GnumericSheet   *gsheet = GNUMERIC_SHEET (item->canvas);
	int watch_col, watch_row;
	
	item_cursor_set_bounds (item_cursor, start_col, start_row, end_col, end_row);

	/* Now, make the range visible as well as we can guess */
	if (start_col < item_cursor->start_col)
		watch_col = start_col;
	else 
		watch_col = end_col;
	
	if (start_row < item_cursor->start_row)
		watch_row = start_row;
	else 
		watch_row = end_row;

	gnumeric_sheet_make_cell_visible (gsheet, watch_col, watch_row);
	gnome_canvas_update_now (GNOME_CANVAS (gsheet));
}

static gint
item_cursor_drag_event (GnomeCanvasItem *item, GdkEvent *event)
{
	GnomeCanvas *canvas = GNOME_CANVAS_ITEM (item)->canvas;
	ItemCursor *item_cursor = ITEM_CURSOR (item);
	int x, y, w, h;
	int col, row;
		
	switch (event->type){
	case GDK_BUTTON_RELEASE:
		printf ("button release\n");
		gnome_canvas_item_ungrab (item, event->button.time);
		item_cursor_do_drop (item_cursor, event);
		gtk_object_destroy (GTK_OBJECT (item));
		return TRUE;

	case GDK_BUTTON_PRESS:
		/* We actually never get this event: this kind of
		 * cursor is created and grabbed
		 */
		return TRUE;

	case GDK_MOTION_NOTIFY:
		convert (canvas, event->button.x, event->button.y, &x, &y);
		if (x < 0)
			x = 0;
		if (y < 0)
			y = 0;
		col = item_grid_find_col (item_cursor->item_grid, x, NULL);
		row = item_grid_find_row (item_cursor->item_grid, y, NULL);
		
		w   = (item_cursor->end_col - item_cursor->start_col);
		h   = (item_cursor->end_row - item_cursor->start_row);

		item_cursor_set_bounds_visibly (item_cursor, col, row, col + w, row + h);
		return TRUE;

	default:
		return FALSE;
	}
}

static gint
item_cursor_autofill (GnomeCanvasItem *item, GdkEvent *event)
{
	switch (event->type){
	case GDK_BUTTON_RELEASE:
		gnome_canvas_item_ungrab (item, event->button.time);
		gtk_object_destroy (GTK_OBJECT (item));
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
		return item_cursor_autofill (item, event);
		
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

	item_cursor->start_col = 0;
	item_cursor->end_col   = 0;
	item_cursor->start_row = 0;
	item_cursor->end_row   = 0;
	item_cursor->start_row = ITEM_CURSOR_SELECTION;
	item_cursor->tag = -1;
}

static void
item_cursor_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ItemCursor *item_cursor;

	item = GNOME_CANVAS_ITEM (o);
	item_cursor = ITEM_CURSOR (o);

	switch (arg_id){
	case ARG_SHEET:
		item_cursor->sheet = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_ITEM_GRID:
		item_cursor->item_grid = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_STYLE:
		item_cursor->style = GTK_VALUE_INT (*arg);
		break;
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

	item_cursor_parent_class = gtk_type_class (gnome_canvas_item_get_type());
	
	object_class = (GtkObjectClass *) item_cursor_class;
	item_class = (GnomeCanvasItemClass *) item_cursor_class;

	gtk_object_add_arg_type ("ItemCursor::Sheet", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET);
	gtk_object_add_arg_type ("ItemCursor::Grid", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_ITEM_GRID);
	gtk_object_add_arg_type ("ItemCursor::Style", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_STYLE);

	object_class->set_arg = item_cursor_set_arg;
	object_class->destroy = item_cursor_destroy;

	/* GnomeCanvasItem method overrides */
	item_class->realize     = item_cursor_realize;
	item_class->unrealize   = item_cursor_unrealize;
	item_class->reconfigure = item_cursor_reconfigure;
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
