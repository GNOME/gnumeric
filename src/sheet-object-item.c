/*
 * sheet-object-item.c: Implements the Bonobo-based canvas items
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "sheet-object-item.h"

static SheetObject *sheet_object_item_parent_class;

static GnomeCanvasItem *
sheet_object_item_new_view (SheetObject *so, SheetView *sheet_view)
{
	GnomeCanvasItem *item = NULL;

	/*
	 * Create item/view-frame
	 */
	item = bonobo_client_site_new_item (
		SHEET_OBJECT_BONOBO (so)->client_site,
		sheet_view->object_group);
	
	return item;
}

static void
sheet_object_item_update_bounds (SheetObject *sheet_object)
{
}

static void
sheet_object_item_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_item_parent_class = gtk_type_class (sheet_object_get_type ());

	/* SheetObject class method overrides */
	sheet_object_class->new_view = sheet_object_item_new_view;
	sheet_object_class->update_bounds = sheet_object_item_update_bounds;
}

GtkType
sheet_object_item_get_type (void)
{
	static GtkType type = 0;

	if (!type) {
		GtkTypeInfo info = {
			"SheetObjectItem",
			sizeof (SheetObjectItem),
			sizeof (SheetObjectItemClass),
			(GtkClassInitFunc) sheet_object_item_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (sheet_object_bonobo_get_type (), &info);
	}

	return type;
}
