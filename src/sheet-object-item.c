/*
 * sheet-object-item.c: Implements the Bonobo-based canvas items
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include <bonobo/bonobo-ui-component.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "sheet-control-gui-priv.h"
#include "workbook-control-gui-priv.h"
#include "dialogs.h"
#include "sheet-object-item.h"

#include <gal/util/e-util.h>

static SheetObject *sheet_object_item_parent_class;

static GtkObject *
sheet_object_item_new_view (SheetObject *so, SheetControlGUI *scg)
{
	/* FIXME : this is bogus */
	GnumericCanvas *gcanvas = scg_pane (scg, 0);
	GnomeCanvasItem *so_view = NULL;

	so_view = bonobo_client_site_new_item (
		SHEET_OBJECT_BONOBO (so)->client_site,
		bonobo_ui_component_get_container (scg->wbcg->uic),
		gcanvas->object_group);

	scg_object_register (so, so_view);
	return GTK_OBJECT (so_view);
}

static void
sheet_object_item_update_bounds (SheetObject *so, GtkObject *obj_view,
				 SheetControlGUI *s_control)
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

E_MAKE_TYPE (sheet_object_item, "SheetObjectItem", SheetObjectItem,
	     sheet_object_item_class_init, NULL,
	     SHEET_OBJECT_BONOBO_TYPE);
