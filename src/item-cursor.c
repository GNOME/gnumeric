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

/* The signals we emit */
enum {
	ITEM_CURSOR_LAST_SIGNAL
};

static guint item_cursor_signals [ITEM_CURSOR_LAST_SIGNAL] = { 0 };

static GnomeCanvasItem *item_cursor_parent_class;

/* The argument we take */
enum {
	ARG_0,
	ARG_SHEET,		/* The Sheet * argument */
	ARG_ITEM_GRID		/* The ItemGrid * argument */
};

static void
item_cursor_destroy (GtkObject *object)
{
	ItemCursor *item_cursor;

	item_cursor = ITEM_CURSOR (object);

	if (GTK_OBJECT_CLASS (item_cursor_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_cursor_parent_class)->destroy)(object);
}

static void
item_cursor_realize (GnomeCanvasItem *item)
{
	ItemCursor *item_cursor;
	GdkWindow  *window;
	GdkGC      *gc;
	GdkColor   black;
	
	item_cursor = ITEM_CURSOR (item);
	window = GTK_WIDGET (item->canvas)->window;

	gc = item_cursor->gc = gdk_gc_new (window);
#if 0
	gdk_color_black (item->canvas->colormap, &black);
	gdk_gc_set_foreground (gc, &black);
#endif
}

static void
item_cursor_unrealize (GnomeCanvasItem *item)
{
	ItemCursor *item_cursor = ITEM_CURSOR (item);

	gdk_gc_unref (item_cursor->gc);
	item_cursor->gc = 0;
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

	*w = sheet_col_get_distance (sheet, item_cursor->start_col, item_cursor->end_col);
	*h = sheet_row_get_distance (sheet, item_cursor->start_row, item_cursor->end_row);
}

static void
item_cursor_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemCursor *item_cursor = ITEM_CURSOR (item);
	int xd, yd, dx, dy;
	int cursor_width, cursor_height;
	GdkPoint points [40];
	int draw_external, draw_internal, draw_handle, draw_center;
	int premove;
	
	item_cursor_get_pixel_coords (item_cursor, &xd, &yd,
				      &cursor_width, &cursor_height);

	dx = xd - x;
	dy = yd - y;

	draw_external = draw_internal = draw_handle = draw_center = 0;
	switch (item_cursor->style){
	case ITEM_CURSOR_SELECTION:
		draw_internal = 1;
		draw_external = 1;
		draw_center   = 0;
		draw_handle   = 1;
		break;

	case ITEM_CURSOR_EDITING:
		draw_internal = 0;
		draw_handle   = 0;
		draw_center   = 0;
		draw_external = 1;

	case ITEM_CURSOR_ANTED:
		draw_internal = 0;
		draw_handle   = 0;
		draw_center   = 1;
		draw_external = 0;
	};

	if (draw_handle)
		premove = 5;
	else
		premove = 0;

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
item_cursor_set_bounds (ItemCursor *item_cursor, int c1, int c2, int r1, int r2)
{
	item_cursor_request_redraw (item_cursor);
	
	item_cursor->start_col = c1;
	item_cursor->end_col = c2;
	item_cursor->start_row = r1;
	item_cursor->end_row = r2;

	item_cursor_request_redraw (item_cursor);
}

static double
item_cursor_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		   GnomeCanvasItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

static void
item_cursor_translate (GnomeCanvasItem *item, double dx, double dy)
{
	printf ("item_cursor_translate %g, %g\n", dx, dy);
}

static gint
item_cursor_event (GnomeCanvasItem *item, GdkEvent *event)
{
	printf ("Cursor event\n");
	return 0;
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
	item_cursor->end_col   = 1;
	item_cursor->start_row = 0;
	item_cursor->end_row   = 1;
	item_cursor->start_row = ITEM_CURSOR_SELECTION;
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
	
	object_class->set_arg = item_cursor_set_arg;

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
