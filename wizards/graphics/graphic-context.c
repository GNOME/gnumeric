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

static GnomeObjectClient *
launch_guppi (void)
{
	GnomeObjectClient *object_server;

	object_server = gnome_object_activate_with_goad_id (NULL, GUPPI_ID, 0, NULL);

	return object_server;
}

GraphicContext *
graphic_context_new (Workbook *wb, GladeXML *gui)
{
	GraphicContext *gc;
	GnomeClientSite *client_site;
	GnomeContainer *container;
	GnomeObjectClient *object_server;
	
	g_return_val_if_fail (wb != NULL, NULL);

	/*
	 * Configure our container end
	 */
	container = GNOME_CONTAINER (gnome_container_new ());
	client_site = gnome_client_site_new (container);
	gnome_container_add (container, GNOME_OBJECT (client_site));
	
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

void
graphics_context_data_range_clear (GraphicContext *gc)
{
	GList *l;
	
	g_return_if_fail (gc != NULL);

	for (l = gc->data_range_list; l; l = l->next){
		DataRange *data_range = l->data;

		data_range_destroy (data_range);
	}
	g_list_free (gc->data_range_list);
	gc->data_range_list = NULL;
}

/**
 * graphic_context_set_data_range:
 * @gc: the graphics context
 * @data_range_spec: a string description of the cell ranges
 * @vertical: whether we need to create the data ranges in vertical mode
 *
 * This routine defines the data ranges based on the @data_range_spec, and
 * it guesses the series values using @vertical
 */
void
graphic_context_set_data_range (GraphicContext *gc, const char *data_range_spec, gboolean vertical)
{
	ExprTree *tree;
	char *p;
	char *error;
	
	g_return_if_fail (gc != NULL);
	g_return_if_fail (data_range_spec != NULL);

	graphics_context_data_range_clear (gc);

	/* Hack:
	 * Create a valid expression, parse as expression, and pull the
	 * parsed arguments.
	 */
	expr = g_strconcat ("=SELECTION(", data_range_spec, ")", NULL);
	tree = expr_parse_string (expr, NULL, 0, 0, 0, &error);
	g_free (expr);
	
	if (tree == NULL)
		return;

	assert (tree->oper == OPER_FUNCALL);

	args = tree->u.function.arg_list;

	/*
	 * Guess the data ranges.  From the selected ranges.  Simple
	 * case is just a region selected, so we can guess a number
	 * of parameters from this.
	 */
	g_warning ("Should do something more interesting with disjoint selections");
	
	expr_tree_unref (tree);
}



