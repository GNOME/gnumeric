/*
 * sheet-object-item.c: Implements the Bonobo-based canvas items
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "sheet-object-item.h"

#include "gui-util.h"
#include "gnumeric-canvas.h"
#include "sheet-control-gui-priv.h"
#include "workbook-control-gui-priv.h"
#include "dialogs.h"

#include <gdk/gdkkeysyms.h>
#include <bonobo/bonobo-ui-component.h>
#include <gsf/gsf-impl-utils.h>

#ifdef GNOME2_CONVERSION_COMPLETE
static SheetObject *sheet_object_item_parent_class;

static GtkObject *
sheet_object_item_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnumericCanvas *gcanvas = ((GnumericPan *)key)->gcanvas;
	GnomeCanvasItem *so_view = NULL;

	gnome_canvas_item_raise_to_top (GNOME_CANVAS_ITEM (gcanvas->sheet_object_group));
	so_view = bonobo_client_site_new_item (
		SHEET_OBJECT_BONOBO (so)->control_frame,
		bonobo_ui_component_get_container (scg->wbcg->uic),
		gcanvas->sheet_object_group);

	scg_object_register (so, so_view);
	return GTK_OBJECT (so_view);
}

static void
sheet_object_item_update_bounds (SheetObject *so, GtkObject *view,
				 SheetControlGUI *s_control)
{
	/* FIXME : what goes here ?? */

	if (so->is_visible)
		gnome_canvas_item_show (GNOME_CANVAS_ITEM (view));
	else
		gnome_canvas_item_hide (GNOME_CANVAS_ITEM (view));
}

static void
sheet_object_item_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_item_parent_class = g_type_class_peek (sheet_object_get_type ());

	/* SheetObject class method overrides */
	sheet_object_class->new_view = sheet_object_item_new_view;
	sheet_object_class->update_bounds = sheet_object_item_update_bounds;
}

GSF_CLASS (SheetObjectItem, sheet_object_item,
	   sheet_object_item_class_init, NULL,
	   SHEET_OBJECT_BONOBO_TYPE);
#endif
