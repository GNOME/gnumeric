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
	
	item_cursor = ITEM_CURSOR (item);
	window = GTK_WIDGET (item->canvas)->window;

	gc = item_cursor->gc = gdk_gc_new (window);
	gdk_gc_set_function (gc, GDK_INVERT);
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
	g_warning ("item_cursor_reconfigure\n");
}

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
	Sheet *sheet;
	int xd, yd, dx, dy;
	int cursor_width, cursor_height;
	GdkPoint points [5];
	
	item_cursor_get_pixel_coords (item_cursor, &xd, &yd,
				      &cursor_width, &cursor_height);

	dx = xd - x;
	dy = yd - y;

	switch (item_cursor->style){
	case ITEM_CURSOR_SELECTION:
		gdk_gc_set_line_attributes (item_cursor->gc, 3,
					    GDK_LINE_SOLID, GDK_CAP_PROJECTING,
					    GDK_JOIN_MITER);
		points [0].x = xd + cursor_width - 1;
		points [0].y = yd + cursor_height + 4;
		points [1].x = points [0].x;
		points [1].y = yd - 1;
		points [2].x = xd - 1;
		points [2].y = points [1].y;
		points [3].x = points [2].x;
		points [3].y = yd + cursor_height + 1;
		points [4].x = xd + cursor_width - 4;
		points [4].y = points [3].y;
		
		gdk_draw_lines (drawable, item_cursor->gc, points, 5);

	default:
	}
	
}

static void
item_cursor_request_redraw (ItemCursor *item_cursor)
{
	GnomeCanvas *canvas = GNOME_CANVAS_ITEM (item_cursor)->canvas;
	int x, y, w, h;

	item_cursor_get_pixel_coords (item_cursor, &x, &y, &w, &h);
	gnome_canvas_request_redraw (canvas, x-1, y-1, x+w+1, y+h+1);
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
