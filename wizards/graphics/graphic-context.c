/*
 * Gnumeric, the GNOME spreadhseet
 *
 * Graphics Wizard's Graphic Context manager
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include <glade/glade.h>
#include <bonobo/gnome-bonobo.h>
#include "graphic-context.h"

#define GUPPI_ID "Guppi_component"

static GnomeObject *
launch_guppi (void)
{
	GnomeObject     *object_server;

	object_server = gnome_object_activate_with_goad_id (NULL, GUPPI_ID, 0, NULL);

	return object_server;
}

GraphicContext *
graphic_context_new (Workbook *wb, GladeXML *gui)
{
	GraphicContext *gc;
	GnomeClientSite *client_site;
	GnomeContainer *container;
	GnomeObject *object_server;
	
	g_return_val_if_fail (wb != NULL, NULL);

	/*
	 * Configure our container end
	 */
	container = GNOME_CONTAINER (gnome_container_new ());
	client_site = gnome_client_site_new (container);
	gnome_container_add (container, client_site);
	
	/*
	 * Launch server
	 */
	object_server = launch_guppi ();
	if (!object_server)
		return NULL;

	/*
	 * Bind them together
	 */
	if (!gnome_client_site_bind_component (client_site, object_server)){
		gtk_object_unref (GTK_OBJECT (client_site));
		gtk_object_unref (GTK_OBJECT (container));
		gtk_object_unref (GTK_OBJECT (object_server));

		return NULL;
	}

	/*
	 * Create the graphic context
	 */
	gc = g_new0 (GraphicContext, 1);
	gc->workbook = wb;
	gc->signature = GC_SIGNATURE;
	gc->current_page = 0;
	gc->gui = gui;
	gc->dialog_toplevel = glade_xml_get_widget (gui, "graphics-wizard-dialog");

	gc->container = container;
	gc->client_site = client_site;
	gc->guppi = object_server;
		
	return gc;
}

void
graphic_context_destroy (GraphicContext *gc)
{
	GList *l;
	
	g_return_if_fail (gc != NULL);
	g_return_if_fail (IS_GRAPHIC_CONTEXT (gc));

	gtk_object_unref (GTK_OBJECT (gc->dialog_toplevel));
	gtk_object_unref (GTK_OBJECT (gc->gui));

	if (gc->data_range)
		string_unref (gc->data_range);
	if (gc->x_axis_label)
		string_unref (gc->x_axis_label);
	if (gc->plot_title)
		string_unref (gc->plot_title);
	if (gc->y_axis_label)
		string_unref (gc->y_axis_label);

	for (l = gc->data_range_list; l; l = l->next){
		DataRange *data_range = l->data;

		data_range_destroy (data_range);
	}
	g_list_free (gc->data_range_list);

	gtk_object_unref (GTK_OBJECT (gc->guppi));
	
	g_free (gc);
}

void
graphic_context_data_range_add (GraphicContext *gc, DataRange *data_range)
{
	g_return_if_fail (gc != NULL);
	g_return_if_fail (IS_GRAPHIC_CONTEXT (gc));
	g_return_if_fail (data_range != NULL);

	gc->data_range_list = g_list_prepend (gc->data_range_list, data_range);
}

void
graphic_context_data_range_remove (GraphicContext *gc, const char *range_name)
{
	GList *l;
	
	g_return_if_fail (gc != NULL);
	g_return_if_fail (IS_GRAPHIC_CONTEXT (gc));
	g_return_if_fail (range_name != NULL);

	for (l = gc->data_range_list; l; l = l->next){
		DataRange *data_range = l->data;

		if (strcmp (data_range->name->str, range_name) != 0)
			continue;

		gc->data_range_list = g_list_remove (gc->data_range_list, data_range);
	}
}


