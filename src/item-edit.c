#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "item-grid.h"
#include "item-edit.h"
#include "item-debug.h"

static GnomeCanvasItem *item_edit_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET,		/* The Sheet * argument */
	ARG_ITEM_GRID,		/* The ItemGrid * argument */
	ARG_GTK_ENTRY,		/* The GtkEntry * argument */
	ARG_COL,		/* The column where this should edit */
	ARG_ROW,		/* The row where this should edit */
};

/*
 * Returns the cordinates for the editor bounding box
 */
static void
item_edit_get_pixel_coords (ItemEdit *item_edit, int *x, int *y, int *w, int *h)
{
	ItemGrid *item_grid = item_edit->item_grid;
	Sheet *sheet = item_edit->sheet;

	*x = sheet_col_get_distance (sheet, item_grid->left_col, item_edit->col);
	*y = sheet_row_get_distance (sheet, item_grid->top_row, item_edit->row);

	*w = sheet_col_get_distance (sheet, item_edit->col, item_edit->col + item_edit->col_span);
	*h = sheet_row_get_distance (sheet, item_edit->row, item_edit->row + 1);
}

static void
item_edit_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	GtkWidget *canvas = GTK_WIDGET (item->canvas);
	ItemEdit *item_edit = ITEM_EDIT (item);
	int xd, yd, wd, hd, dx, dy;

	if (!item_edit->sheet)
		return;
	item_edit_get_pixel_coords (item_edit, &xd, &yd, &wd, &hd);

	dx = xd - x;
	dy = yd - y;

	gdk_draw_rectangle (drawable, canvas->style->white_gc,
			    TRUE,
			    dx + 1, dy + 1, wd - 1, hd - 1);
}

static double
item_edit_point (GnomeCanvasItem *item, double c_x, double c_y, int cx, int cy,
		   GnomeCanvasItem **actual_item)
{
	int x, y, w, h;
	
	item_edit_get_pixel_coords (ITEM_EDIT (item), &x, &y, &w, &h);

	*actual_item = NULL;
	if ((cx < x) || (cy < y) || (cx > x+w) || (cy > y+w))
		return 10000.0;
	
	*actual_item = item;
	return 0.0;
}

static void
item_edit_translate (GnomeCanvasItem *item, double dx, double dy)
{
	printf ("item_cursor_translate %g, %g\n", dx, dy);
}

static int
item_edit_event (GnomeCanvasItem *item, GdkEvent *event)
{
	printf ("Editor event!\n");
	
	return 0;
}

static void
item_edit_reconfigure (GnomeCanvasItem *item)
{
	int x, y, w, h;

	item_edit_get_pixel_coords (ITEM_EDIT (item), &x, &y, &w, &h);
	item->x1 = x;
	item->y1 = y;
	item->x2 = x + w;
	item->y2 = y + h;

	gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent), item);
}

/*
 * Instance initialization
 */
static void
item_edit_init (ItemEdit *item_edit)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 1;
	item->y2 = 1;

	item_edit->col_span = 1;
	item_edit->sheet = 0;
}

static void
item_edit_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ItemEdit *item_edit;

	item = GNOME_CANVAS_ITEM (o);
	item_edit = ITEM_EDIT (o);

	switch (arg_id){
	case ARG_SHEET:
		item_edit->sheet = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_ITEM_GRID:
		item_edit->item_grid = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_GTK_ENTRY:
		item_edit->editor = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_COL:
		item_edit->col = GTK_VALUE_INT (*arg);
		break;
	case ARG_ROW:
		item_edit->row = GTK_VALUE_INT (*arg);
		break;
	}
}

/*
 * ItemEdit class initialization
 */
static void
item_edit_class_init (ItemEditClass *item_edit_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_edit_parent_class = gtk_type_class (gnome_canvas_item_get_type());
	
	object_class = (GtkObjectClass *) item_edit_class;
	item_class = (GnomeCanvasItemClass *) item_edit_class;

	gtk_object_add_arg_type ("ItemEdit::Sheet", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET);
	gtk_object_add_arg_type ("ItemEdit::Grid", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_ITEM_GRID);
	gtk_object_add_arg_type ("ItemEdit::GtkEntry", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_GTK_ENTRY);
	gtk_object_add_arg_type ("ItemEdit::Col", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_COL);
	gtk_object_add_arg_type ("ItemEdit::Row", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_ROW);
	
	object_class->set_arg = item_edit_set_arg;

	/* GnomeCanvasItem method overrides */
	item_class->draw        = item_edit_draw;
	item_class->point       = item_edit_point;
	item_class->translate   = item_edit_translate;
	item_class->event       = item_edit_event;
	item_class->reconfigure = item_edit_reconfigure;
}

GtkType
item_edit_get_type (void)
{
	static GtkType item_edit_type = 0;

	if (!item_edit_type) {
		GtkTypeInfo item_edit_info = {
			"ItemEdit",
			sizeof (ItemEdit),
			sizeof (ItemEditClass),
			(GtkClassInitFunc) item_edit_class_init,
			(GtkObjectInitFunc) item_edit_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		item_edit_type = gtk_type_unique (gnome_canvas_item_get_type (), &item_edit_info);
	}

	return item_edit_type;
}
