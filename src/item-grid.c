#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "item-grid.h"

/* The signals we emit */
enum {
	ITEM_GRID_TEST,
	ITEM_GRID_LAST_SIGNAL
};
static guint item_grid_signals [ITEM_GRID_LAST_SIGNAL] = { 0 };

static GnomeCanvasItem *item_grid_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_DEFAULT_GRID_COLOR
};

static void
item_grid_destroy (GtkObject *object)
{
	ItemGrid *grid;

	grid = ITEM_GRID (object);

	if (GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)(object);
}

static void
item_grid_realize (GnomeCanvasItem *item)
{
	ItemGrid *item_grid;

	item_grid = ITEM_GRID (item);
	item_grid->grid_gc = gdk_gc_new (GTK_WIDGET (item->canvas)->window);
}

static void
item_grid_unrealize (GnomeCanvasItem *item)
{
	ItemGrid *item_grid = ITEM_GRID (item);
	
	gdk_gc_unref (item_grid->grid_gc);
	item_grid->grid_gc = 0;
}

static void
item_grid_reconfigure (GnomeCanvasItem *item)
{
	g_warning ("item_grid_reconfigure\n");
}

static void
item_grid_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
		int x, int y, int width, int height)
{
	ItemGrid *item_grid = ITEM_GRID (item);
	
	printf ("item_grid_draw (%d, %d) for (%d, %d)\n",
		x, y, width, height);
	
	gdk_draw_line (drawable,
		       item_grid->grid_gc,
		       x, y, x+width, y+height);
			    
}

static double
item_grid_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		 GnomeCanvasItem **actual_item)
{
	printf ("item_grid_point: %g,%g (%d,%d)\n",
		x, y, cx, cy);
	*actual_item = NULL;
	return 0.0;
}

static void
item_grid_translate (GnomeCanvasItem *item, double dx, double dy)
{
	printf ("item_grid_translate %g, %g\n", dx, dy);
}

static gint
item_grid_event (GnomeCanvasItem *item, GdkEvent *event)
{
	printf ("Event\n");
	return 0;
}

/*
 * Instance initialization
 */
static void
item_grid_init (ItemGrid *item_grid)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_grid);

	item->x1 = 0;
	item->y1 = 0;

	/* A big number for now */
	item->x2 = 9999999;
	item->y2 = 9999999;
	
	item_grid->left_col = 0;
	item_grid->top_row  = 0;
	item_grid->top_offset = 0;
	item_grid->left_offset = 0;

#if 0
	gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent),
					 item);
#endif
}

static void
item_grid_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ItemGrid *item_grid;
	GdkColor color;

	item = GNOME_CANVAS_ITEM (o);
	item_grid = ITEM_GRID (o);
	
	switch (arg_id){
	case ARG_DEFAULT_GRID_COLOR:
		if (gnome_canvas_get_color (item->canvas,
					    GTK_VALUE_STRING (*arg), &color))
			break;
		item_grid->default_grid_color = color.pixel;
	}
}

/*
 * ItemGrid class initialization
 */
static void
item_grid_class_init (ItemGridClass *item_grid_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_grid_parent_class = gtk_type_class (gnome_canvas_item_get_type());
	
	object_class = (GtkObjectClass *) item_grid_class;
	item_class = (GnomeCanvasItemClass *) item_grid_class;

	gtk_object_add_arg_type ("ItemGrid::default_grid_color",
				 GTK_TYPE_STRING,
				 GTK_ARG_WRITABLE,
				 ARG_DEFAULT_GRID_COLOR);
	
	object_class->set_arg = item_grid_set_arg;

	/* GnomeCanvasItem method overrides */
	item_class->realize     = item_grid_realize;
	item_class->unrealize   = item_grid_unrealize;
	item_class->reconfigure = item_grid_reconfigure;
	item_class->draw        = item_grid_draw;
	item_class->point       = item_grid_point;
	item_class->translate   = item_grid_translate;
	item_class->event       = item_grid_event;
}

GtkType
item_grid_get_type (void)
{
	static GtkType item_grid_type = 0;

	if (!item_grid_type) {
		GtkTypeInfo item_grid_info = {
			"ItemGrid",
			sizeof (ItemGrid),
			sizeof (ItemGridClass),
			(GtkClassInitFunc) item_grid_class_init,
			(GtkObjectInitFunc) item_grid_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		item_grid_type = gtk_type_unique (gnome_canvas_item_get_type (), &item_grid_info);
	}

	return item_grid_type;
}
