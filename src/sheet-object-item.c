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
sheet_object_item_realize (SheetObject *so, SheetView *sheet_view)
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
	sheet_object_class->realize = sheet_object_item_realize;
	sheet_object_class->update_bounds = sheet_object_item_update_bounds;
}

GtkType
sheet_object_item_get_type (void)
{
	static GtkType type = 0;

	if (!type){
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

SheetObject *
sheet_object_item_new (Sheet *sheet,
		       double x1, double y1,
		       double x2, double y2,
		       const char *goad_id)
{
	BonoboObjectClient *object_server;
	SheetObjectItem *soi;
	
	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (goad_id != NULL, NULL);

#ifdef USING_OAF
	object_server = bonobo_object_activate_with_oaf_id (goad_id, 0);
#else
	object_server = bonobo_object_activate_with_goad_id (NULL, goad_id, 0, NULL);
#endif
	if (!object_server)
		return NULL;

	soi = gtk_type_new (sheet_object_item_get_type ());
	if (!sheet_object_bonobo_construct (
		SHEET_OBJECT_BONOBO (soi), sheet, object_server, x1, y1, x2, y2)){
		gtk_object_destroy (GTK_OBJECT (soi));
		return NULL;
	}
	return SHEET_OBJECT (soi);
}
