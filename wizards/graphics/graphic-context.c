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
#include "workbook.h"
#include <glade/glade.h>
#include <bonobo.h>
#include "graphic-context.h"

#define GRAPH_GOADID "GOADID:embeddable:Graph:Layout"

static BonoboObjectClient *
get_graphics_component (void)
{
	BonoboObjectClient *object_server;

	object_server = bonobo_object_activate_with_goad_id (NULL, GRAPH_GOADID, 0, NULL);

	return object_server;
}

static void
graphic_context_load_pointers (WizardGraphicContext *gc, GladeXML *gui)
{
	gc->dialog_toplevel = glade_xml_get_widget (gui, "graphics-wizard-dialog");
	gc->steps_notebook = GTK_NOTEBOOK (glade_xml_get_widget (gui, "main-notebook"));
}

WizardGraphicContext *
graphic_context_new (Workbook *wb, GladeXML *gui)
{
	WizardGraphicContext *gc;
	BonoboClientSite *client_site;
	BonoboObjectClient *object_server;
	
	g_return_val_if_fail (wb != NULL, NULL);

	/*
	 * Configure our container end
	 */
	client_site = bonobo_client_site_new (wb->bonobo_container);
	bonobo_container_add (wb->bonobo_container, BONOBO_OBJECT (client_site));
	
	/*
	 * Launch server
	 */
	object_server = get_graphics_component ();
	if (!object_server)
		goto error_activation;

	/*
	 * Bind them together
	 */
	if (!bonobo_client_site_bind_embeddable (client_site, object_server))
		goto error_binding;

	/*
	 * Create the graphic context
	 */
	gc = g_new0 (WizardGraphicContext, 1);
	gc->workbook = wb;
	gc->signature = GC_SIGNATURE;
	gc->current_page = 0;
	gc->gui = gui;
	gc->last_graphic_type_page = -1;
	
	graphic_context_load_pointers (gc, gui);
	
	gc->client_site = client_site;
	gc->graphics_server = object_server;
		
	return gc;

error_binding:
	bonobo_object_unref (BONOBO_OBJECT (object_server));
	
error_activation:
	bonobo_object_unref (BONOBO_OBJECT (client_site));

	return NULL;
}

void
graphic_context_destroy (WizardGraphicContext *gc)
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

	bonobo_object_unref (BONOBO_OBJECT (gc->graphics_server));
	
	g_free (gc);
}

void
graphic_context_data_range_add (WizardGraphicContext *gc, DataRange *data_range)
{
	g_return_if_fail (gc != NULL);
	g_return_if_fail (IS_GRAPHIC_CONTEXT (gc));
	g_return_if_fail (data_range != NULL);

	gc->data_range_list = g_list_prepend (gc->data_range_list, data_range);
}

void
graphic_context_data_range_remove (WizardGraphicContext *gc, const char *range_name)
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
graphics_context_data_range_clear (WizardGraphicContext *gc)
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
graphic_context_set_data_range (WizardGraphicContext *gc,
				const char *data_range_spec,
				gboolean vertical)
{
	
	g_return_if_fail (gc != NULL);
	g_return_if_fail (data_range_spec != NULL);

	graphics_context_data_range_clear (gc);

}

DataRange *
data_range_new (const char *name, const char *expression)
{
	DataRange *data_range;
	ExprTree *tree;
	gchar * expr;
	char *error;

	/* Hack:
	 * Create a valid expression, parse as expression, and pull the
	 * parsed arguments.
	 */
	expr = g_strconcat ("=SELECTION(", expression, ")", NULL);
	tree = expr_parse_string (expr, NULL, NULL, &error);
	g_free (expr);
	
	if (tree == NULL)
		return NULL;

	g_assert (tree->oper == OPER_FUNCALL);

	data_range = g_new (DataRange, 1);
	data_range->name = string_get (name);
	data_range->base_tree = tree;
	
	data_range->list_of_expressions = tree->u.function.arg_list;

	return data_range;
}

void
data_range_destroy (DataRange *data_range)
{
	string_unref (data_range->name);
	expr_tree_unref (data_range->base_tree);
	g_free (data_range);
}
