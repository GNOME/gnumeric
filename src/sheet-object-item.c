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

static void
cb_item_update_bounds (SheetObject *so, FooCanvasItem *view)
{
	/* FIXME : what goes here ?? */

	if ((so->flags & SHEET_OBJECT_IS_VISIBLE))
		foo_canvas_item_show (view);
	else
		foo_canvas_item_hide (view);
}

static GObject *
sheet_object_item_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnmCanvas *gcanvas = ((GnmPane *)key)->gcanvas;
	FooCanvasItem *view = bonobo_client_site_new_item (
		SHEET_OBJECT_BONOBO (so)->control_frame,
		bonobo_ui_component_get_container (scg->wbcg->uic),
		gcanvas->object_views);

	scg_object_register (so, view);
	gnm_pane_object_register (so, item, &cb_item_update_bounds);
	return G_OBJECT (view);
}

static void
sheet_object_item_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_item_parent_class = g_type_class_peek_parent (());

	/* SheetObject class method overrides */
	sheet_object_class->new_view = sheet_object_item_new_view;
}

GSF_CLASS (SheetObjectItem, sheet_object_item,
	   sheet_object_item_class_init, NULL,
	   SHEET_OBJECT_BONOBO_TYPE);
#endif
