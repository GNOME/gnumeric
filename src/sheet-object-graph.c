/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-graph.c: Wrapper for GNOME Office graphs in gnumeric
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "sheet-object-graph.h"

#include "sheet-control-gui.h"
#include "gnumeric-canvas.h"
#include "gnumeric-pane.h"
#include "str.h"
#include "gui-util.h"
#include "style-color.h"
#include "sheet-object-impl.h"
#include "workbook-edit.h"
#include "commands.h"
#include "application.h"

#include <goffice/graph/gog-graph.h>
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-object-xml.h>
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/gog-renderer-gnome-print.h>
#include <goffice/graph/gog-renderer-pixbuf.h>
#include <goffice/graph/gog-renderer-svg.h>
#include <goffice/graph/gog-control-foocanvas.h>
#include <graph.h>

#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkimage.h>
#include <libfoocanvas/foo-canvas-line.h>
#include <libfoocanvas/foo-canvas-rect-ellipse.h>
#include <libfoocanvas/foo-canvas-polygon.h>
#include <libfoocanvas/foo-canvas-text.h>
#include <math.h>
#include <string.h>

#define SHEET_OBJECT_CONFIG_KEY "sheet-object-graph-key"

#define SHEET_OBJECT_GRAPH(o)       (G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_OBJECT_GRAPH_TYPE, SheetObjectGraph))
#define SHEET_OBJECT_GRAPH_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_GRAPH_TYPE, SheetObjectGraphClass))

typedef struct {
	SheetObject  base;
	GogGraph	*graph;
	GObject		*renderer;
	gulong		 add_sig, remove_sig;
} SheetObjectGraph;
typedef SheetObjectClass SheetObjectGraphClass;

static GObjectClass *parent_klass;

static void
sog_data_set_sheet (G_GNUC_UNUSED SheetObjectGraph *sog, GOData *data, Sheet *sheet)
{
	gnm_go_data_set_sheet (data, sheet);
}
static void
cb_graph_add_data (G_GNUC_UNUSED GogGraph *graph,
		   GOData *data, SheetObjectGraph *sog)
{
	sog_data_set_sheet (sog, data, sog->base.sheet);
}
static void
cb_graph_remove_data (G_GNUC_UNUSED GogGraph *graph,
		      GOData *data, SheetObjectGraph *sog)
{
	sog_data_set_sheet (sog, data, NULL);
}

static void
sog_datas_set_sheet (SheetObjectGraph *sog, Sheet *sheet)
{
	GSList *ptr = gog_graph_get_data (sog->graph);
	for (; ptr != NULL ; ptr = ptr->next)
		sog_data_set_sheet (sog, ptr->data, sheet);
}

static void
sheet_object_graph_finalize (GObject *obj)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (obj);

	if (sog->renderer != NULL) {
		g_object_unref (sog->renderer);
		sog->renderer = NULL;
	}
	if (sog->graph != NULL) {
		g_object_unref (sog->graph);
		sog->graph = NULL;
	}

	if (parent_klass && parent_klass->finalize)
		parent_klass->finalize (obj);
}

static GObject *
sheet_object_graph_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
	GnmCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	FooCanvasItem *item = foo_canvas_item_new (gcanvas->sheet_object_group,
		GOG_CONTROL_FOOCANVAS_TYPE,
		"renderer",	sog->renderer,
		NULL);
	foo_canvas_item_raise_to_top (FOO_CANVAS_ITEM (gcanvas->sheet_object_group));

	gnm_pane_object_register (so, item);
	return G_OBJECT (item);
}

 
/* 
 * The following are useful formats to save in:
 *  png
 *  svg
 *  eps
 *
 * TODO: Possibly add an eps renderer.  We may also use a new instance of
 * pixbufrenderer to save as png. This would allow the user to specify size of
 * the saved image, if that's wanted.
 */
static void
cb_save_as (GtkWidget *widget, GObject *obj_view)
{
	SheetObjectGraph *sog;
	SheetControl *sc;
	WorkbookControlGUI *wbcg;
	GtkFileSelection *fsel;

	sog = SHEET_OBJECT_GRAPH (sheet_object_view_obj (obj_view));

	g_return_if_fail (sog != NULL);

	sc  = sheet_object_view_control (obj_view);
	wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));
	fsel = GTK_FILE_SELECTION (gtk_file_selection_new 
				   (_("Save graph as image")));
	/* Show file selector */
	if (gnumeric_dialog_file_selection (wbcg, fsel)) {
		char const *fname = gtk_file_selection_get_filename (fsel);
		char const *base = g_path_get_basename (fname);
		char const *extension = gsf_extension_pointer (base);
		GError *err = NULL;
		gboolean ret;

		if (extension == NULL)
			fname = g_strdup_printf ("%s.%s", fname, "png");

		if (g_ascii_strcasecmp (extension, "png") == 0) {
			GdkPixbuf *pixbuf = gog_renderer_pixbuf_get (
				GOG_RENDERER_PIXBUF (sog->renderer));
			ret = gdk_pixbuf_save (pixbuf, fname, "png", &err, NULL);
		} else if (g_ascii_strcasecmp (extension, "svg") == 0) {
			GsfOutputStdio *output = gsf_output_stdio_new (fname, &err);

			if (output != NULL) {
				double coords [4];
				sheet_object_position_pts_get (SHEET_OBJECT (sog), coords);
				ret = gog_graph_export_to_svg (sog->graph, GSF_OUTPUT (output),
					fabs (coords[2] - coords[0]),
					fabs (coords[3] - coords[1]),
					1. / gnm_app_dpi_to_pixels ());

				gsf_output_close (GSF_OUTPUT (output));
				g_object_unref (output);

#warning Translate when strings unfreeze
				if (!ret && err == NULL)
					err = g_error_new (gsf_output_error_id (), 0,
						"Unknown failure generating SVG for Chart");
			} else
				ret = FALSE;
		} else {
#warning WRONG It can support SVG too, fix when strings unfreeze
			gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
					 _("Sorry, gnumeric can only save graphs as png images"));
			return;
		}

		if (!ret)
			gnm_cmd_context_error (GNM_CMD_CONTEXT (wbcg), err);
		if (extension == NULL)
			g_free ((char *)fname);
	}
}

static void
sheet_object_graph_populate_menu (SheetObject *so,
				  GObject *obj_view,
				  GtkMenu *menu)
{
	GtkWidget *item, *image;

	item = gtk_image_menu_item_new_with_mnemonic (_("_Save as image"));
	image = gtk_image_new_from_stock (GTK_STOCK_SAVE_AS, 
					  GTK_ICON_SIZE_MENU);
	gtk_widget_show (image);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (cb_save_as), obj_view);
	SHEET_OBJECT_CLASS (parent_klass)->populate_menu (so, obj_view, menu);
	gtk_menu_shell_insert (GTK_MENU_SHELL (menu),  item, 1);
}

static void
sheet_object_graph_update_bounds (SheetObject *so, GObject *view_obj)
{
	double coords [4];
	FooCanvasItem   *view = FOO_CANVAS_ITEM (view_obj);
	SheetControlGUI	*scg  =
		SHEET_CONTROL_GUI (sheet_object_view_control (view_obj));

	scg_object_view_position (scg, so, coords);
	foo_canvas_item_set (view,
		"x", MIN (coords [0], coords[2]),
		"y", MIN (coords [3], coords[1]),
		"w", fabs (coords [2] - coords [0]) + 1.,
		"h", fabs (coords [3] - coords [1]) + 1.,
		NULL);

	if (so->is_visible)
		foo_canvas_item_show (view);
	else
		foo_canvas_item_hide (view);
}

static gboolean
sheet_object_graph_read_xml (SheetObject *so,
			     XmlParseContext const *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child = e_xml_get_child_by_name (tree, "GogObject");

	if (child != NULL) {
		GogObject *graph = gog_object_new_from_xml (NULL, child);
		sheet_object_graph_set_gog (so, GOG_GRAPH (graph));
		g_object_unref (graph);
	}
	return FALSE;
}

static gboolean
sheet_object_graph_write_xml (SheetObject const *so,
			      XmlParseContext const *ctxt, xmlNode *parent)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	xmlNode *res = gog_object_write_xml (GOG_OBJECT (sog->graph), ctxt->doc);
	if (res != NULL)
		xmlAddChild (parent, res);
	return FALSE;
}

static SheetObject *
sheet_object_graph_clone (SheetObject const *so, Sheet *sheet)
{
	SheetObjectGraph *sog;
	SheetObjectGraph *new_sog;
	GogGraph *graph;

	g_return_val_if_fail (IS_SHEET_OBJECT_GRAPH (so), NULL);
	sog = SHEET_OBJECT_GRAPH (so);

	new_sog = g_object_new (G_OBJECT_TYPE (so), NULL);
	graph = gog_graph_dup (sog->graph);
	sheet_object_graph_set_gog (SHEET_OBJECT (new_sog), graph);
	g_object_unref (graph);

	return SHEET_OBJECT (new_sog);
}

static void
sheet_object_graph_print (SheetObject const *so, GnomePrintContext *ctx,
			  double width, double height)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	gog_graph_print_to_gnome_print (sog->graph, ctx, width, height);
}

typedef struct {
	SheetObject *so;
	WorkbookControl *wbc;
} sheet_object_graph_user_config_t;

static void
sheet_object_graph_user_config_free_data (gpointer data,
                                             GClosure *closure)
{
	g_free (data);
	closure->data = NULL;
}

static void
cb_update_graph (GogGraph *graph, sheet_object_graph_user_config_t *data)
{
	cmd_so_graph_config (data->wbc, data->so, G_OBJECT (graph), 
			     G_OBJECT (SHEET_OBJECT_GRAPH (data->so)->graph));
}

static void
sheet_object_graph_user_config (SheetObject *so, SheetControl *sc)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	WorkbookControlGUI *wbcg;
	sheet_object_graph_user_config_t *data;
	GClosure *closure;

	g_return_if_fail (sog != NULL);
	g_return_if_fail (sc != NULL);

	wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));
	
	data = g_new0 (sheet_object_graph_user_config_t, 1);
	data->so = so;
	data->wbc = WORKBOOK_CONTROL (wbcg);
 
	closure = g_cclosure_new (G_CALLBACK (cb_update_graph),
				  data, (GClosureNotify)
				  sheet_object_graph_user_config_free_data);
	
 	sheet_object_graph_guru (wbcg, sog->graph, closure);
	g_closure_sink (closure);
}

static gboolean
sheet_object_graph_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	if (sog->graph != NULL)
		sog_datas_set_sheet (sog, sheet);
	return FALSE;
}

static gboolean
sheet_object_graph_remove_from_sheet (SheetObject *so)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	if (sog->graph != NULL)
		sog_datas_set_sheet (sog, NULL);
	return FALSE;
}

static void
sheet_object_graph_default_size (SheetObject const *so, double *w, double *h)
{
	g_object_get (SHEET_OBJECT_GRAPH (so)->renderer,
		"logical_width_pts",  w,
		"logical_height_pts", h,
		NULL);
}

static void
sheet_object_graph_position_changed (SheetObject const *so)
{
	double coords [4];

	sheet_object_position_pts_get (so, coords);
	g_object_set (SHEET_OBJECT_GRAPH (so)->renderer,
		"logical_width_pts",  fabs (coords[2] - coords[0]),
		"logical_height_pts", fabs (coords[3] - coords[1]),
		NULL);
}

static void
sheet_object_graph_class_init (GObjectClass *klass)
{
	SheetObjectClass	*so_class  = SHEET_OBJECT_CLASS (klass);

	parent_klass = g_type_class_peek_parent (klass);

	/* Object class method overrides */
	klass->finalize = sheet_object_graph_finalize;

	/* SheetObject class method overrides */
	so_class->new_view	     = sheet_object_graph_new_view;
	so_class->populate_menu	     = sheet_object_graph_populate_menu;
	so_class->update_view_bounds = sheet_object_graph_update_bounds;
	so_class->read_xml	     = sheet_object_graph_read_xml;
	so_class->write_xml	     = sheet_object_graph_write_xml;
	so_class->clone              = sheet_object_graph_clone;
	so_class->user_config        = sheet_object_graph_user_config;
	so_class->assign_to_sheet    = sheet_object_graph_set_sheet;
	so_class->remove_from_sheet  = sheet_object_graph_remove_from_sheet;
	so_class->print		     = sheet_object_graph_print;
	so_class->default_size	     = sheet_object_graph_default_size;
	so_class->position_changed   = sheet_object_graph_position_changed;
	so_class->rubber_band_directly = FALSE;
}

static void
sheet_object_graph_init (GObject *obj)
{
	SheetObject *so = SHEET_OBJECT (obj);
	so->anchor.direction = SO_DIR_DOWN_RIGHT;
}

GSF_CLASS (SheetObjectGraph, sheet_object_graph,
	   sheet_object_graph_class_init, sheet_object_graph_init,
	   SHEET_OBJECT_TYPE);

/**
 * sheet_object_graph_new :
 * @graph : #GogGraph
 *
 * Adds a reference to @graph and creates a gnumeric sheet object wrapper
 **/
SheetObject *
sheet_object_graph_new (GogGraph *graph)
{
	SheetObjectGraph *sog = g_object_new (SHEET_OBJECT_GRAPH_TYPE, NULL);
	sheet_object_graph_set_gog (SHEET_OBJECT (sog), graph);
	return SHEET_OBJECT (sog);
}

GogGraph *
sheet_object_graph_get_gog (SheetObject *sog)
{
	g_return_val_if_fail (IS_SHEET_OBJECT_GRAPH (sog), NULL);

	return ((SheetObjectGraph *)sog)->graph;
}

/**
 * sheet_object_graph_set_gog :
 * @so : #SheetObjectGraph
 * @graph : #GogGraph
 *
 * If @graph is non NULL add a reference to it, otherwise create a new graph.
 * Assign the graph to its SheetObjectGraph wrapper and initialize the
 * handlers, disconnecting any existing connection for the preceding graph.
 **/
void
sheet_object_graph_set_gog (SheetObject *so, GogGraph *graph)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);

	g_return_if_fail (IS_SHEET_OBJECT_GRAPH (so));

	if (graph != NULL) {
		if (sog->graph == graph)
			return;

		g_object_ref (G_OBJECT (graph));
	} else
		graph = g_object_new (GOG_GRAPH_TYPE, NULL);

	if (sog->graph != NULL) {
		g_signal_handler_disconnect (sog->graph, sog->add_sig);
		g_signal_handler_disconnect (sog->graph, sog->remove_sig);
		if (so->sheet != NULL)
			sog_datas_set_sheet (sog, NULL);
		g_object_unref (sog->graph);
	}

	sog->graph = graph;
	if (so->sheet != NULL)
		sog_datas_set_sheet (sog, so->sheet);
	sog->add_sig = g_signal_connect_object (G_OBJECT (graph),
		"add_data",
		G_CALLBACK (cb_graph_add_data), G_OBJECT (sog), 0);
	sog->remove_sig = g_signal_connect_object (G_OBJECT (graph),
		"remove_data",
		G_CALLBACK (cb_graph_remove_data), G_OBJECT (sog), 0);

	if (sog->renderer != NULL)
		g_object_set (sog->renderer, "model", graph, NULL);
	else
		sog->renderer = g_object_new (GOG_RENDERER_PIXBUF_TYPE,
					      "model", sog->graph,
					      NULL);
}

static void
cb_graph_guru_done (WorkbookControlGUI *wbcg)
{
	wbcg_edit_detach_guru (wbcg);
	wbcg_edit_finish (wbcg, FALSE, NULL);
}

void
sheet_object_graph_guru (WorkbookControlGUI *wbcg, GogGraph *graph,
			 GClosure *closure)
{
	GtkWidget *dialog = gog_guru (graph, GOG_DATA_ALLOCATOR (wbcg),
		       GNM_CMD_CONTEXT (wbcg), wbcg_toplevel (wbcg),
		       closure);
	wbcg_edit_attach_guru (wbcg, dialog);
	g_object_set_data_full (G_OBJECT (dialog),
		"guru", wbcg, (GDestroyNotify) cb_graph_guru_done);
}
