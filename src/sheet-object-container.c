/*
 * sheet-object-container.c:
 *   SheetObject for containers (Bonobo, Graphics)
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
#include "gnumeric-util.h"
#include "sheet-object-container.h"
#include "sheet-object-widget.h"
#include <bonobo/bonobo-item-container.h>
#include <bonobo/bonobo-view-frame.h>
#include <bonobo/bonobo-client-site.h>
#include <bonobo/bonobo-embeddable.h>
#include <bonobo/bonobo-object-client.h>
#include <bonobo/bonobo-object-directory.h>

static SheetObject *sheet_object_container_parent_class;

static GnomeCanvasItem *
make_container_item (SheetObject *so, SheetView *sheet_view, GtkWidget *w)
{
	GnomeCanvasItem *item;
	double x1, y1, x2, y2;

	sheet_object_get_bounds (so, &x1, &y1, &x2, &y2);
	item = gnome_canvas_item_new (
		sheet_view->object_group,
		gnome_canvas_widget_get_type (),
		"widget", w,
		"x",      x1,
		"y",      y1,
		"width",  x2 - x1 + 1,
		"height", y2 - y1 + 1,
		"size_pixels", FALSE,
		NULL);
	sheet_object_widget_handle (so, w, item);
	gtk_widget_show (w);

	return item;
}

static gint
user_activation_request_cb (BonoboViewFrame *view_frame, SheetObject *so)
{
	Sheet *sheet = so->sheet;

	printf ("user activation request\n");
	if (sheet->active_object_frame){
		bonobo_view_frame_view_deactivate (sheet->active_object_frame);
		if (sheet->active_object_frame != NULL)
                        bonobo_view_frame_set_covered (sheet->active_object_frame, TRUE);
		sheet->active_object_frame = NULL;
	}

	bonobo_view_frame_view_activate (view_frame);
	sheet_mode_edit_object (so);

	return FALSE;
}

static gint
view_activated_cb (BonoboViewFrame *view_frame, gboolean activated, SheetObject *so)
{
	Sheet *sheet = so->sheet;

        if (activated) {
                if (sheet->active_object_frame != NULL) {
                        g_warning ("View requested to be activated but there is already "
                                   "an active View!\n");
                        return FALSE;
                }

                /*
                 * Otherwise, uncover it so that it can receive
                 * events, and set it as the active View.
                 */
                bonobo_view_frame_set_covered (view_frame, FALSE);
                sheet->active_object_frame = view_frame;
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

                if (view_frame == sheet->active_object_frame)
			sheet->active_object_frame = NULL;
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

static GnomeCanvasItem *
sheet_object_container_new_view (SheetObject *so, SheetView *sheet_view)
{
	SheetObjectContainer *soc;
	BonoboViewFrame *view_frame;
	GtkWidget *view_widget;

	soc = SHEET_OBJECT_CONTAINER (so);

	view_frame = bonobo_client_site_new_view (
		SHEET_OBJECT_BONOBO (so)->client_site,
		bonobo_ui_component_get_container (sheet_view->wbcg->uic));

	if (!view_frame) {
		g_warning ("Component died");
		return NULL;
	}

	gtk_signal_connect (GTK_OBJECT (view_frame), "user_activate",
			    GTK_SIGNAL_FUNC (user_activation_request_cb), so);
	gtk_signal_connect (GTK_OBJECT (view_frame), "activated",
			    GTK_SIGNAL_FUNC (view_activated_cb), so);

	view_widget = bonobo_view_frame_get_wrapper (view_frame);
	return make_container_item (so, sheet_view, view_widget);
}

/*
 * This implemenation moves the widget rather than
 * destroying/updating/creating the views
 */
static void
sheet_object_container_update_bounds (SheetObject *so)
{
	GList *l;
	double x1, y1, x2, y2;

	sheet_object_get_bounds (so, &x1, &y1, &x2, &y2);

	for (l = so->realized_list; l; l = l->next){
		GnomeCanvasItem *item = l->data;

		gnome_canvas_item_set (
			item,
			"x",      x1,
			"y",      y1,
			"width",  x2 - x1 + 1,
			"height", y2 - y1 + 1,
			NULL);
	}
}

static void
sheet_object_container_class_init (GtkObjectClass *object_class)
{
	SheetObjectClass *sheet_object_class = SHEET_OBJECT_CLASS (object_class);

	sheet_object_container_parent_class = gtk_type_class (sheet_object_get_type ());

	/* SheetObject class method overrides */
	object_class->destroy = sheet_object_container_destroy;
	sheet_object_class->new_view = sheet_object_container_new_view;
	sheet_object_class->update_bounds = sheet_object_container_update_bounds;
}

GtkType
sheet_object_container_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"SheetObjectContainer",
			sizeof (SheetObjectContainer),
			sizeof (SheetObjectContainerClass),
			(GtkClassInitFunc) sheet_object_container_class_init,
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
sheet_object_container_new_bonobo (Sheet *sheet, BonoboClientSite *client_site)
{
	SheetObjectContainer *c;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	c = gtk_type_new (sheet_object_container_get_type ());

	sheet_object_construct (SHEET_OBJECT (c), sheet);

	SHEET_OBJECT_BONOBO (c)->object_server =
		bonobo_client_site_get_embeddable (client_site);
	SHEET_OBJECT_BONOBO (c)->client_site = client_site;

	return SHEET_OBJECT (c);
}

SheetObject *
sheet_object_container_new_object (Sheet *sheet,
				   const char *object_id)
{
	SheetObjectContainer *c;

	g_return_val_if_fail (sheet != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (object_id != NULL, NULL);

	c = gtk_type_new (sheet_object_container_get_type ());

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
	const char  *required_ids [] = {
		"IDL:Bonobo/Embeddable:1.0",
		NULL
	};

	g_return_val_if_fail (sheet != NULL, NULL);

	iid = bonobo_directory_find_for_file (fname, required_ids, &msg);

	if (!iid)
		gnome_dialog_run_and_close (GNOME_DIALOG (gnome_error_dialog (msg)));

	else {
		so = sheet_object_container_new_object (sheet, iid);
		if (so == NULL) {
			msg = g_strdup_printf (_("can't create object for '%s'"), iid);
			gnome_dialog_run_and_close (GNOME_DIALOG (gnome_error_dialog (msg)));
		} else {
			if (sheet_object_bonobo_load_from_file (SHEET_OBJECT_BONOBO (so), fname))
				sheet_object_realize (SHEET_OBJECT (so));
		}

	}

	g_free (iid);
	g_free (msg);

	return so;
}
