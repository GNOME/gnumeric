/*
 * sheet-object-container.c:
 *   SheetObject for containers (Bonobo, Graphs)
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Michael Meeks (mmeeks@gnu.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "sheet-object-container.h"

#include "workbook-control-gui-priv.h"
#include "workbook-private.h"
#include "sheet.h"
#include "gui-util.h"
#include "sheet-control-gui.h"
#include "gnumeric-canvas.h"
#include "sheet-object-widget.h"

#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <libgnome/gnome-i18n.h>
#include <gal/util/e-util.h>
#include <bonobo/bonobo-item-container.h>
#include <bonobo/bonobo-view-frame.h>
#include <bonobo/bonobo-client-site.h>
#include <bonobo/bonobo-embeddable.h>
#include <bonobo/bonobo-object-client.h>
#include <bonobo/bonobo-object-directory.h>
#include <bonobo/bonobo-exception.h>

static void
cb_user_activation_request (BonoboViewFrame *view_frame, GtkObject *so_view)
{
	SheetControlGUI *scg = sheet_object_view_control (so_view);
	SheetObject *so = sheet_object_view_obj (so_view);

	scg_mode_edit_object (scg, so);
}

static GtkObject *
sheet_object_container_new_view (SheetObject *so, SheetControlGUI *scg)
{
	/* FIXME : this is bogus */
	GnumericCanvas *gcanvas = scg_pane (scg, 0);
	WorkbookControlGUI *wbcg = scg_get_wbcg	(scg);
	SheetObjectContainer *soc;
	BonoboViewFrame *view_frame;
	GtkWidget	*view_widget;
	GnomeCanvasItem *view_item;

	g_return_val_if_fail (IS_SHEET_OBJECT_CONTAINER (so), NULL);

	soc = SHEET_OBJECT_CONTAINER (so);

	view_frame = bonobo_client_site_new_view_full (
		SHEET_OBJECT_BONOBO (so)->control_frame,
		bonobo_ui_component_get_container (wbcg->uic), FALSE, FALSE);
	if (!view_frame) {
		g_warning ("Component died");
		return NULL;
	}

	view_widget = bonobo_view_frame_get_wrapper (view_frame);
	view_item = gnome_canvas_item_new (
		gcanvas->object_group,
		gnome_canvas_widget_get_type (),
		"widget", view_widget,
		"size_pixels", FALSE,
		NULL);
	gtk_object_set_data (GTK_OBJECT (view_item), "view_frame", view_frame);
	gtk_signal_connect (GTK_OBJECT (view_frame), "user_activate",
			    GTK_SIGNAL_FUNC (cb_user_activation_request),
			    view_item);

	scg_object_widget_register (so, view_widget, view_item);
	gtk_widget_show (view_widget);

	return GTK_OBJECT (view_item);
}

/*
 * This implemenation moves the widget rather than
 * destroying/updating/creating the views
 */
static void
sheet_object_container_update_bounds (SheetObject *so, GtkObject *view,
		                     SheetControlGUI *scg)
{
	double coords [4];

	/* NOTE : far point is EXCLUDED so we add 1 */
	scg_object_view_position (scg, so, coords);
	gnome_canvas_item_set (GNOME_CANVAS_ITEM (view),
		"x", coords [0], "y", coords [1],
		"width",  coords [2] - coords [0] + 1.,
		"height", coords [3] - coords [1] + 1.,
		NULL);

	if (so->is_visible)
		gnome_canvas_item_show (GNOME_CANVAS_ITEM (view));
	else
		gnome_canvas_item_hide (GNOME_CANVAS_ITEM (view));
}

static void
sheet_object_container_set_active (SheetObject *so, GtkObject *view, 
				   gboolean active)
{
	BonoboViewFrame *view_frame;

	g_return_if_fail (IS_SHEET_OBJECT_CONTAINER (so));

	/*
	 * We need to ref the view_frame on activation because the view
	 * can get destroyed. Sure, we will be called during destruction, but
	 * at this point, the view_frame will already be gone and we won't 
	 * be able to correctly deactivate.
	 */
	view_frame = gtk_object_get_data (GTK_OBJECT (view), "view_frame");

	if (active) {
		bonobo_object_ref (BONOBO_OBJECT (view_frame));
		bonobo_view_frame_view_activate (view_frame);
		bonobo_view_frame_set_covered (view_frame, FALSE);
	} else {
		bonobo_view_frame_view_deactivate (view_frame);
		bonobo_view_frame_set_covered (view_frame, TRUE);
		bonobo_object_unref (BONOBO_OBJECT (view_frame));
	}
}

static void
sheet_object_container_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *so_class;

	/* SheetObject class method overrides */
	so_class = SHEET_OBJECT_CLASS (object_class);
	so_class->new_view 	= sheet_object_container_new_view;
	so_class->update_bounds = sheet_object_container_update_bounds;
	so_class->set_active    = sheet_object_container_set_active;
}

SheetObject *
sheet_object_container_new (Workbook *wb)
{
	return sheet_object_container_new_object (wb, NULL);
}

SheetObject *
sheet_object_container_new_object (Workbook *wb, char const *object_id)
{
	SheetObjectContainer *c = g_object_new (SHEET_OBJECT_CONTAINER_TYPE, NULL);
	if (!sheet_object_bonobo_construct (
		SHEET_OBJECT_BONOBO (c), wb->priv->bonobo_container, object_id)) {
		gtk_object_destroy (GTK_OBJECT (c));
		return NULL;
	}

	return SHEET_OBJECT (c);
}

SheetObject *
sheet_object_container_new_file (Workbook *wb, char const *fname)
{
	CORBA_Environment ev;
	SheetObject *so = NULL;
	char        *msg;
	char        *iid;
	static char const *required_ids [] = {
		"IDL:Bonobo/Embeddable:1.0",
		NULL
	};

	iid = bonobo_directory_find_for_file (fname, required_ids, &msg);
	if (iid != NULL) {
		so = sheet_object_container_new_object (wb, iid);
		if (so == NULL) {
			msg = g_strdup_printf (_("can't create object for '%s'"), iid);
			gnome_dialog_run_and_close (GNOME_DIALOG (gnome_error_dialog (msg)));
		} else {
			CORBA_exception_init (&ev);
			sheet_object_bonobo_load_file (SHEET_OBJECT_BONOBO (so),
						       fname, &ev);
			if (BONOBO_EX (&ev)) {
				msg = g_strdup_printf (
					_("Could not load file: %s"),
					bonobo_exception_get_text (&ev));
				gnome_dialog_run_and_close (GNOME_DIALOG (gnome_error_dialog (msg)));
			}
			CORBA_exception_free (&ev);
		}
		g_free (iid);
	} else
		gnome_dialog_run_and_close (GNOME_DIALOG (gnome_error_dialog (msg)));

	g_free (msg);

	return so;
}

E_MAKE_TYPE (sheet_object_container, "SheetObjectContainer", SheetObjectContainer,
	sheet_object_container_class_init, NULL, SHEET_OBJECT_BONOBO_TYPE);
