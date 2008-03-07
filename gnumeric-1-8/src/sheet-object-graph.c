/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-graph.c: Wrapper for GNOME Office graphs in gnumeric
 *
 * Copyright (C) 2003-2005 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "sheet-object-graph.h"

#include "gnm-pane-impl.h"
#include "sheet-control-gui.h"
#include "str.h"
#include "gui-util.h"
#include "gui-file.h"
#include "gnm-graph-window.h"
#include "style-color.h"
#include "sheet-object-impl.h"
#include "wbc-gtk.h"
#include "commands.h"
#include "application.h"
#include "xml-io.h"
#include <graph.h>

#include <goffice/graph/gog-graph.h>
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-object-xml.h>
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/gog-data-set.h>
#include <goffice/graph/gog-renderer.h>
#include <goffice/graph/gog-control-foocanvas.h>
#include <goffice/utils/go-file.h>
#include <goffice/utils/go-units.h>
#include <goffice/utils/go-libxml-extras.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-format.h>
#include <goffice/app/go-cmd-context.h>
#include <goffice/gtk/go-graph-widget.h>

#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-libxml.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkwindow.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-line.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-rect-ellipse.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-polygon.h>
#include <goffice/cut-n-paste/foocanvas/foo-canvas-text.h>
#include <math.h>
#include <string.h>

static void
so_graph_view_destroy (SheetObjectView *sov)
{
	gtk_object_destroy (GTK_OBJECT (sov));
}
static void
so_graph_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	FooCanvasItem *view = FOO_CANVAS_ITEM (sov);

	if (visible) {
		foo_canvas_item_set (view,
			"x", MIN (coords [0], coords[2]),
			"y", MIN (coords [3], coords[1]),
			"w", fabs (coords [2] - coords [0]) + 1.,
			"h", fabs (coords [3] - coords [1]) + 1.,
			NULL);

		foo_canvas_item_show (view);
	} else
		foo_canvas_item_hide (view);
}

static void
so_graph_foo_view_init (SheetObjectViewIface *sov_iface)
{
	sov_iface->destroy	= so_graph_view_destroy;
	sov_iface->set_bounds	= so_graph_view_set_bounds;
}
typedef GogControlFooCanvas		SOGraphFooView;
typedef GogControlFooCanvasClass	SOGraphFooViewClass;
static GSF_CLASS_FULL (SOGraphFooView, so_graph_foo_view,
	NULL, NULL, NULL, NULL,
	NULL, GOG_CONTROL_FOOCANVAS_TYPE, 0,
	GSF_INTERFACE (so_graph_foo_view_init, SHEET_OBJECT_VIEW_TYPE))

/****************************************************************************/
#define SHEET_OBJECT_CONFIG_KEY "sheet-object-graph-key"

#define SHEET_OBJECT_GRAPH_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_GRAPH_TYPE, SheetObjectGraphClass))

typedef struct {
	SheetObject  base;
	GogGraph	*graph;
	GogRenderer	*renderer;
	gulong		 add_sig, remove_sig;
} SheetObjectGraph;
typedef SheetObjectClass SheetObjectGraphClass;

static GObjectClass *parent_klass;

static void
sog_data_set_sheet (G_GNUC_UNUSED SheetObjectGraph *sog,
		    GOData *data, Sheet *sheet)
{
	gnm_go_data_set_sheet (data, sheet);
}

static void
sog_data_foreach_dep (SheetObject *so, GOData *data,
		      SheetObjectForeachDepFunc func, gpointer user)
{
	gnm_go_data_foreach_dep (data, so, func, user);
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
gnm_sog_finalize (GObject *obj)
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

	parent_klass->finalize (obj);
}

static SheetObjectView *
gnm_sog_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmPane *pane = GNM_PANE (container);
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	FooCanvasItem *view = foo_canvas_item_new (pane->object_views,
		so_graph_foo_view_get_type (),
		"renderer",	sog->renderer,
		NULL);
	return gnm_pane_object_register (so, view, TRUE);
}

static GtkTargetList *
gnm_sog_get_target_list (SheetObject const *so)
{
	GtkTargetList *tl = gtk_target_list_new (NULL, 0);
	char *mime_str = go_image_format_to_mime ("svg");
	GSList *mimes, *ptr;
	char *mime;

	mimes = go_strsplit_to_slist (mime_str, ',');
	for (ptr = mimes; ptr != NULL; ptr = ptr->next) {
		mime = (char *) ptr->data;

		if (mime != NULL && *mime != '\0')
			gtk_target_list_add (tl, gdk_atom_intern (mime, FALSE),
					     0, 0);
	}
	g_free (mime_str);
	go_slist_free_custom (mimes, g_free);
	/* No need to eliminate duplicates. */
	gtk_target_list_add_image_targets (tl, 0, TRUE);

	return tl;
}

static GtkTargetList *
gnm_sog_get_object_target_list (SheetObject const *so)
{
	GtkTargetList *tl = gtk_target_list_new (NULL, 0);
	gtk_target_list_add (tl,
		gdk_atom_intern ("application/x-goffice-graph", FALSE), 0, 0);
	return tl;
}

static void
gnm_sog_write_image (SheetObject const *so, char const *format, double resolution,
		     GsfOutput *output, GError **err)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	gboolean res = FALSE;
	double coords[4];
	double w, h;

	if (so->sheet) {
		sheet_object_position_pts_get (SHEET_OBJECT (sog), coords);
		w = fabs (coords[2] - coords[0]) + 1.;
		h = fabs (coords[3] - coords[1]) + 1.;
	} else {
		w = GPOINTER_TO_UINT
			(g_object_get_data (G_OBJECT (so), "pt-width-at-copy"));
		h = GPOINTER_TO_UINT
			(g_object_get_data (G_OBJECT (so), "pt-height-at-copy"));
	}

	g_return_if_fail (w > 0 && h > 0);

	res = gog_graph_export_image (sog->graph, go_image_get_format_from_name (format),
				      output, resolution, resolution);

	if (!res && err && *err == NULL)
		*err = g_error_new (gsf_output_error_id (), 0,
				    _("Unknown failure while saving image"));
}

static void
gnm_sog_write_object (SheetObject const *so, char const *format,
		      GsfOutput *output, GError **err)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	GsfXMLOut *xout;
	GogObject *graph;

	g_return_if_fail (strcmp (format, "application/x-goffice-graph") == 0);

	graph = gog_object_dup (GOG_OBJECT (sog->graph),
		NULL, gog_dataset_dup_to_simple);
	xout = gsf_xml_out_new (output);
	gog_object_write_xml_sax (GOG_OBJECT (graph), xout);
	g_object_unref (xout);
	g_object_unref (graph);
}

static void
sog_cb_save_as (SheetObject *so, SheetControl *sc)
{
	WBCGtk *wbcg;
	char *uri;
	GError *err = NULL;
	GsfOutput *output;
	GSList *l;
	GOImageFormat selected_format;
	GOImageFormatInfo const *format_info;
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	double resolution;

	g_return_if_fail (sog != NULL);

	l = gog_graph_get_supported_image_formats ();
	g_return_if_fail (l != NULL);
	selected_format = GPOINTER_TO_UINT (l->data);

#warning "This violates model gui barrier"
	wbcg = scg_wbcg (SHEET_CONTROL_GUI (sc));
	uri = go_gui_get_image_save_info (wbcg_toplevel (wbcg), l, &selected_format, &resolution);
	if (!uri)
		goto out;
	output = go_file_create (uri, &err);
	if (!output)
		goto out;
	format_info = go_image_get_format_info (selected_format);
	sheet_object_write_image (so, format_info->name, resolution, output, &err);
	g_object_unref (output);

	if (err != NULL)
		go_cmd_context_error (GO_CMD_CONTEXT (wbcg), err);

out:
	g_free (uri);
	g_slist_free (l);
}

static void
sog_cb_open_in_new_window (SheetObject *so, SheetControl *sc)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	SheetControlGUI *scg = SHEET_CONTROL_GUI (sc);
	GtkWidget *window;
	double coords[4];

	g_return_if_fail (sog != NULL);

	scg_object_anchor_to_coords (scg, sheet_object_get_anchor (so), coords);
	window = gnm_graph_window_new (sog->graph,
				       floor (fabs (coords[2] - coords[0]) + 0.5),
				       floor (fabs (coords[3] - coords[1]) + 0.5));
	gtk_window_present (GTK_WINDOW (window));
	g_signal_connect (window, "delete-event",
			  G_CALLBACK (gtk_widget_destroy),
			  NULL);
}

static void
gnm_sog_populate_menu (SheetObject *so, GPtrArray *actions)
{
	static SheetObjectAction const sog_actions[] = {
		{ GTK_STOCK_SAVE_AS, N_("_Save as Image"),      NULL, 0, sog_cb_save_as },
		{ NULL,              N_("Open in _New Window"), NULL, 0, sog_cb_open_in_new_window }
	};

	unsigned int i;

	SHEET_OBJECT_CLASS (parent_klass)->populate_menu (so, actions);

	for (i = 0; i < G_N_ELEMENTS (sog_actions); i++)
		go_ptr_array_insert (actions, (gpointer) (sog_actions + i), 1 + i);

}

static gboolean
gnm_sog_read_xml_dom (SheetObject *so, char const *typename,
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

static void
gnm_sog_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	SheetObjectGraph const *sog = SHEET_OBJECT_GRAPH (so);
	gog_object_write_xml_sax (GOG_OBJECT (sog->graph), output);
}

static void
sog_xml_finish (GogObject *graph, SheetObject *so)
{
	sheet_object_graph_set_gog (so, GOG_GRAPH (graph));
	g_object_unref (graph);
}

static void
gnm_sog_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs)
{
	gog_object_sax_push_parser (xin, attrs,
		(GogObjectSaxHandler) sog_xml_finish, so);
}

static void
gnm_sog_copy (SheetObject *dst, SheetObject const *src)
{
	SheetObjectGraph const *sog = SHEET_OBJECT_GRAPH (src);
	GogGraph *graph   = gog_graph_dup (sog->graph);
	sheet_object_graph_set_gog (dst, graph);
	g_object_unref (graph);
}

static void
gnm_sog_draw_cairo (SheetObject const *so, cairo_t *cr,
			  double width, double height)
{
	gog_graph_render_to_cairo (SHEET_OBJECT_GRAPH (so)->graph,
				   cr, width, height);
}

typedef struct {
	SheetObject *so;
	WorkbookControl *wbc;
} gnm_sog_user_config_t;

static void
gnm_sog_user_config_free_data (gpointer data,
					  GClosure *closure)
{
	g_free (data);
	closure->data = NULL;
}

static void
cb_update_graph (GogGraph *graph, gnm_sog_user_config_t *data)
{
	cmd_so_graph_config (data->wbc, data->so, G_OBJECT (graph),
			     G_OBJECT (SHEET_OBJECT_GRAPH (data->so)->graph));
}

static void
gnm_sog_user_config (SheetObject *so, SheetControl *sc)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	WBCGtk *wbcg;
	gnm_sog_user_config_t *data;
	GClosure *closure;

	g_return_if_fail (sog != NULL);
	g_return_if_fail (sc != NULL);

	wbcg = scg_wbcg (SHEET_CONTROL_GUI (sc));

	data = g_new0 (gnm_sog_user_config_t, 1);
	data->so = so;
	data->wbc = WORKBOOK_CONTROL (wbcg);

	closure = g_cclosure_new (G_CALLBACK (cb_update_graph), data,
		(GClosureNotify) gnm_sog_user_config_free_data);

	sheet_object_graph_guru (wbcg, sog->graph, closure);
	g_closure_sink (closure);
}

static void
gnm_sog_foreach_dep (SheetObject *so,
		     SheetObjectForeachDepFunc func,
		     gpointer user)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	GSList *ptr = gog_graph_get_data (sog->graph);
	for (; ptr != NULL ; ptr = ptr->next)
		sog_data_foreach_dep (so, ptr->data, func, user);
}

static gboolean
gnm_sog_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	if (sog->graph != NULL)
		sog_datas_set_sheet (sog, sheet);
	return FALSE;
}

static gboolean
gnm_sog_remove_from_sheet (SheetObject *so)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	if (sog->graph != NULL)
		sog_datas_set_sheet (sog, NULL);
	return FALSE;
}

static void
gnm_sog_default_size (SheetObject const *so, double *w, double *h)
{
	*w = GO_CM_TO_PT ((double)12);
	*h = GO_CM_TO_PT ((double)8);
}

static void
gnm_sog_bounds_changed (SheetObject *so)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);

	/* If it has not been realized there is no renderer yet */
	if (sog->renderer != NULL) {
		double coords [4];
		sheet_object_position_pts_get (so, coords);
		gog_graph_set_size (sog->graph, fabs (coords[2] - coords[0]),
				    fabs (coords[3] - coords[1]));
	}
}

static void
gnm_sog_class_init (GObjectClass *klass)
{
	SheetObjectClass	*so_class  = SHEET_OBJECT_CLASS (klass);

	parent_klass = g_type_class_peek_parent (klass);

	/* Object class method overrides */
	klass->finalize = gnm_sog_finalize;

	/* SheetObject class method overrides */
	so_class->new_view		= gnm_sog_new_view;
	so_class->bounds_changed	= gnm_sog_bounds_changed;
	so_class->populate_menu		= gnm_sog_populate_menu;
	so_class->read_xml_dom		= gnm_sog_read_xml_dom;
	so_class->write_xml_sax		= gnm_sog_write_xml_sax;
	so_class->prep_sax_parser	= gnm_sog_prep_sax_parser;
	so_class->copy			= gnm_sog_copy;
	so_class->user_config		= gnm_sog_user_config;
	so_class->assign_to_sheet	= gnm_sog_set_sheet;
	so_class->remove_from_sheet	= gnm_sog_remove_from_sheet;
	so_class->default_size		= gnm_sog_default_size;
	so_class->draw_cairo		= gnm_sog_draw_cairo;
	so_class->foreach_dep		= gnm_sog_foreach_dep;

	so_class->rubber_band_directly = FALSE;
}

static void
gnm_sog_init (GObject *obj)
{
	SheetObject *so = SHEET_OBJECT (obj);
	so->anchor.base.direction = GOD_ANCHOR_DIR_DOWN_RIGHT;
}

static void
sog_imageable_init (SheetObjectImageableIface *soi_iface)
{
	soi_iface->get_target_list = gnm_sog_get_target_list;
	soi_iface->write_image	   = gnm_sog_write_image;
}

static void
sog_exportable_init (SheetObjectExportableIface *soe_iface)
{
	soe_iface->get_target_list = gnm_sog_get_object_target_list;
	soe_iface->write_object	   = gnm_sog_write_object;
}

GSF_CLASS_FULL (SheetObjectGraph, sheet_object_graph,
		NULL, NULL, gnm_sog_class_init,NULL,
		gnm_sog_init, SHEET_OBJECT_TYPE, 0,
		GSF_INTERFACE (sog_imageable_init, SHEET_OBJECT_IMAGEABLE_TYPE) \
		GSF_INTERFACE (sog_exportable_init, SHEET_OBJECT_EXPORTABLE_TYPE));

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
		sog->renderer = gog_renderer_new (sog->graph);
}

static void
cb_graph_guru_done (WBCGtk *wbcg)
{
	wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);
}

void
sheet_object_graph_guru (WBCGtk *wbcg, GogGraph *graph,
			 GClosure *closure)
{
	GtkWidget *dialog = gog_guru (graph, GOG_DATA_ALLOCATOR (wbcg),
		GO_CMD_CONTEXT (wbcg), closure);
	gnumeric_init_help_button (
		gog_guru_get_help_button (dialog),
		"sect-graphics-plots");
	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (dialog),
		wbcg, GNM_DIALOG_DESTROY_SHEET_REMOVED);
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (dialog), "graph-guru");
	g_object_set_data_full (G_OBJECT (dialog),
		"guru", wbcg, (GDestroyNotify) cb_graph_guru_done);
	wbc_gtk_attach_guru (wbcg, dialog);
	gtk_widget_show (dialog);
}
