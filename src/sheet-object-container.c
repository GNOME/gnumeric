/*
 * sheet-object-container.c:
 *   SheetObject for containers (Bonobo, Graphs)
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Michael Meeks (mmeeks@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include <libgnorba/gnorba.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include "gnumeric.h"
#include "workbook-control-gui-priv.h"
#include "sheet.h"
#include "gnumeric-util.h"
#include "sheet-control-gui-priv.h" /* REMOVE when we localize the frame */
#include "gnumeric-sheet.h"
#include "sheet-object-container.h"
#include "sheet-object-widget.h"

#include <gal/util/e-util.h>
#include <bonobo/bonobo-item-container.h>
#include <bonobo/bonobo-view-frame.h>
#include <bonobo/bonobo-client-site.h>
#include <bonobo/bonobo-embeddable.h>
#include <bonobo/bonobo-object-client.h>
#include <bonobo/bonobo-object-directory.h>

static SheetObject *sheet_object_container_parent_class;

static gint
cb_user_activation_request (BonoboViewFrame *view_frame, GtkObject *so_view)
{
	SheetControlGUI *scg = sheet_object_view_control (so_view);
	SheetObject *so = sheet_object_view_obj (so_view);

	if (scg->active_object_frame) {
		bonobo_view_frame_view_deactivate (scg->active_object_frame);
		if (scg->active_object_frame != NULL)
                        bonobo_view_frame_set_covered (scg->active_object_frame, TRUE);
		scg->active_object_frame = NULL;
	}

	bonobo_view_frame_view_activate (view_frame);
	scg_mode_edit_object (scg, so);

	return FALSE;
}

static gint
cb_view_activated (BonoboViewFrame *view_frame, gboolean activated, GtkObject *so_view)
{
	SheetControlGUI *scg = sheet_object_view_control (so_view);

        if (activated) {
                if (scg->active_object_frame != NULL) {
                        g_warning ("View requested to be activated but there is already "
                                   "an active View!\n");
                        return FALSE;
                }

                /*
                 * Otherwise, uncover it so that it can receive
                 * events, and set it as the active View.
                 */
                bonobo_view_frame_set_covered (view_frame, FALSE);
                scg->active_object_frame = view_frame;
        } else {
                /*
                 * If the View is asking to be deactivated, always
                 * oblige.  We may have already deactivated it (see
                 * user_activation_request_cb), but there's no harm in
                 * doing it again.  There is always the possibility
                 * that a View will ask to be deactivated when we have
                 * not told it to deactivate itself, and that is
                 * why we cover the view here.
                 */
                bonobo_view_frame_set_covered (view_frame, TRUE);

                if (view_frame == scg->active_object_frame)
			scg->active_object_frame = NULL;
	}
	return FALSE;
}

static void
sheet_object_container_destroy (GtkObject *object)
{
	SheetObjectBonobo *sob = SHEET_OBJECT_BONOBO (object);

	if (sob != NULL && sob->client_site != NULL) {
		bonobo_object_unref (BONOBO_OBJECT (sob->client_site));
		sob->client_site = NULL;
		sob->object_server = NULL;
	}

	(*GTK_OBJECT_CLASS(sheet_object_container_parent_class)->destroy) (object);
}

static GtkObject *
sheet_object_container_new_view (SheetObject *so, SheetControlGUI *scg)
{
	/* FIXME : this is bogus */
	GnumericSheet *gsheet = scg_pane (scg, 0);
	SheetObjectContainer *soc;
	BonoboViewFrame *view_frame;
	GtkWidget	*view_widget;
	GnomeCanvasItem *view_item;

	soc = SHEET_OBJECT_CONTAINER (so);

	view_frame = bonobo_client_site_new_view (
		SHEET_OBJECT_BONOBO (so)->client_site,
		bonobo_ui_component_get_container (scg->wbcg->uic));

	if (!view_frame) {
		g_warning ("Component died");
		return NULL;
	}

	view_widget = bonobo_view_frame_get_wrapper (view_frame);
	view_item = gnome_canvas_item_new (
		gsheet->object_group,
		gnome_canvas_widget_get_type (),
		"widget", view_widget,
		"size_pixels", FALSE,
		NULL);
	gtk_signal_connect (GTK_OBJECT (view_frame), "user_activate",
			    GTK_SIGNAL_FUNC (cb_user_activation_request), view_item);
	gtk_signal_connect (GTK_OBJECT (view_frame), "activated",
			    GTK_SIGNAL_FUNC (cb_view_activated), view_item);

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
}

static void
sheet_object_container_set_active (SheetObject *so, gboolean val)
{
	GList  *l;

	for (l = so->realized_list; l; l = l->next){
	}
}

static void
sheet_object_container_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *so_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_container_parent_class = gtk_type_class (sheet_object_get_type ());

	/* SheetObject class method overrides */
	object_class->destroy = sheet_object_container_destroy;
	so_class->new_view = sheet_object_container_new_view;
	so_class->update_bounds = sheet_object_container_update_bounds;
	so_class->set_active  = sheet_object_container_set_active;
}

SheetObject *
sheet_object_container_new (Sheet *sheet)
{
	return sheet_object_container_new_object (sheet, NULL);
}

SheetObject *
sheet_object_container_new_object (Sheet *sheet, const char *object_id)
{
	SheetObjectContainer *c = gtk_type_new (SHEET_OBJECT_CONTAINER_TYPE);
	if (!sheet_object_bonobo_construct (
		SHEET_OBJECT_BONOBO (c), sheet, object_id)) {
		gtk_object_destroy (GTK_OBJECT (c));
		return NULL;
	}

	return SHEET_OBJECT (c);
}

SheetObject *
sheet_object_container_new_file (Sheet *sheet, const char *fname)
{
	SheetObject *so = NULL;
	char        *msg;
	char        *iid;
	static char const *required_ids [] = {
		"IDL:Bonobo/Embeddable:1.0",
		NULL
	};

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	iid = bonobo_directory_find_for_file (fname, required_ids, &msg);
	if (iid != NULL) {
		so = sheet_object_container_new_object (sheet, iid);
		if (so == NULL) {
			msg = g_strdup_printf (_("can't create object for '%s'"), iid);
			gnome_dialog_run_and_close (GNOME_DIALOG (gnome_error_dialog (msg)));
		} else
			sheet_object_bonobo_load_file (SHEET_OBJECT_BONOBO (so), fname);
		g_free (iid);
	} else
		gnome_dialog_run_and_close (GNOME_DIALOG (gnome_error_dialog (msg)));

	g_free (msg);

	return so;
}

E_MAKE_TYPE (sheet_object_container, "SheetObjectItem", SheetObjectContainer,
	     sheet_object_container_class_init, NULL, SHEET_OBJECT_BONOBO_TYPE);
