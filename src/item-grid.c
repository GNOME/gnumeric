#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "item-grid.h"

/* The signals we emit */
enum {
	ITEM_GRID_LAST_SIGNAL
};
static guint item_grid_signals [ITEM_GRID_LAST_SIGNAL] = { 0 };

static GnomeCanvasItem *item_grid_parent_class;

static void
item_grid_destroy (GtkObject *object)
{
	ItemGrid *grid;

	grid = ITEM_GRID (object);

	if (GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)(object);
}

static void
item_grid_class_init (ItemGridClass *item_grid_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItem *item_class;

	object_class = (GtkObjectClass *) item_grid_class;
	item_class = (GnomeCanvasItemClass *) item_grid_class;

	gtk_object_add_arg_type
}

GtkType
item_grid_get_type (void)
{
	static GtkType item_grid_type = 0;

	if (!re_type) {
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

	return re_type;
}
