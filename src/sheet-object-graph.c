/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet-object-graph.c: Wrapper for GNOME Office graphs in gnumeric
 *
 * Copyright (C) 2003-2004 Jody Goldberg (jody@gnome.org)
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
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "sheet-object-graph.h"

#include "sheet-control-gui.h"
#include "gnumeric-canvas.h"
#include "gnumeric-pane.h"
#include "str.h"
#include "gui-util.h"
#include "gui-file.h"
#include "style-color.h"
#include "sheet-object-impl.h"
#include "workbook-edit.h"
#include "commands.h"
#include "application.h"
#include "xml-io.h"
#include <graph.h>

#include <goffice/graph/gog-graph.h>
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-object-xml.h>
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/gog-renderer-gnome-print.h>
#include <goffice/graph/gog-renderer-pixbuf.h>
#include <goffice/graph/gog-renderer-svg.h>
#include <goffice/graph/gog-control-foocanvas.h>
#include <goffice/utils/go-file.h>
#include <goffice/utils/go-units.h>
#include <goffice/utils/go-libxml-extras.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/app/go-cmd-context.h>

#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkimagemenuitem.h>
#include <gtk/gtkstock.h>
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
	NULL, NULL,
	GOG_CONTROL_FOOCANVAS_TYPE, 0,
	GSF_INTERFACE (so_graph_foo_view_init, SHEET_OBJECT_VIEW_TYPE))

/****************************************************************************/
#define SHEET_OBJECT_CONFIG_KEY "sheet-object-graph-key"

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

	parent_klass->finalize (obj);
}

static SheetObjectView *
sheet_object_graph_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmCanvas *gcanvas = ((GnmPane *)container)->gcanvas;
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	FooCanvasItem *view = foo_canvas_item_new (gcanvas->object_views,
		so_graph_foo_view_get_type (),
		"renderer",	sog->renderer,
		NULL);
	return gnm_pane_object_register (so, view, TRUE);
}

static gboolean
sog_gsf_gdk_pixbuf_save (const gchar *buf,
			 gsize count,
			 GError **error,
			 gpointer data)
{
	GsfOutput *output = GSF_OUTPUT (data);
	gboolean ok = gsf_output_write (output, count, buf);

	if (!ok && error)
		*error = g_error_copy (gsf_output_error (output));

	return ok;
}

static void
sheet_object_graph_write_image (SheetObject const *so, const char *format,
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

	if (strcmp (format, "svg") == 0) {
		res = gog_graph_export_to_svg (sog->graph, output, w, h, 1.0);
	} else {
		GogRendererPixbuf *prend = GOG_RENDERER_PIXBUF (sog->renderer);
		GdkPixbuf *pixbuf = gog_renderer_pixbuf_get (prend);

		if (!pixbuf) {
			gog_renderer_pixbuf_update (prend, w, h, 1.);
			pixbuf = gog_renderer_pixbuf_get (prend);
		}
		res = gdk_pixbuf_save_to_callback (pixbuf,
						   sog_gsf_gdk_pixbuf_save,
						   output, format,
						   err, NULL);
	}
	if (!res && err && *err == NULL)
		*err = g_error_new (gsf_output_error_id (), 0,
				    _("Unknown failure while saving image"));
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
sog_cb_save_as (SheetObject *so, SheetControl *sc)
{
	static GOImageType const fmts[] = {
		{(char *) "svg",  (char *) N_("SVG (vector graphics)"), (char *) "svg", FALSE},
		{(char *) "png",  (char *) N_("PNG (raster graphics)"), (char *) "png", TRUE},
		{(char *) "jpeg", (char *) N_("JPEG (photograph)"),     (char *) "jpg", TRUE}
	};

	WorkbookControlGUI *wbcg;
	char *uri;
	GError *err = NULL;
	GsfOutput *output;
	GSList *l = NULL;
	GOImageType const *sel_fmt = &fmts[0];
	guint i;
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);

	g_return_if_fail (sog != NULL);

	for (i = 0; i < G_N_ELEMENTS (fmts); i++)
		l = g_slist_prepend (l, (gpointer) (fmts + i));
	l = g_slist_reverse (l);

	wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));

#warning "This violates model gui barrier"
	uri = gui_get_image_save_info (wbcg_toplevel (wbcg), l, &sel_fmt);
	if (!uri)
		goto out;
	output = go_file_create (uri, &err);
	if (!output)
		goto out;
	sheet_object_write_image (so, sel_fmt->name, output, &err);
	g_object_unref (output);
		
	if (err != NULL)
		go_cmd_context_error (GO_CMD_CONTEXT (wbcg), err);

out:
	g_free (uri);
	g_slist_free (l);
}

static void
sheet_object_graph_populate_menu (SheetObject *so, GPtrArray *actions)
{
	static SheetObjectAction const sog_action =
		{ GTK_STOCK_SAVE_AS, N_("_Save as image"), NULL, 0, sog_cb_save_as };
	SHEET_OBJECT_CLASS (parent_klass)->populate_menu (so, actions);
	go_ptr_array_insert (actions, (gpointer) &sog_action, 1);
}

static gboolean
sheet_object_graph_read_xml_dom (SheetObject *so, char const *typename,
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
sheet_object_graph_write_xml_dom (SheetObject const *so,
				  XmlParseContext const *ctxt, xmlNode *parent)
{
	SheetObjectGraph *sog = SHEET_OBJECT_GRAPH (so);
	xmlNode *res = gog_object_write_xml (GOG_OBJECT (sog->graph), ctxt->doc);
	if (res != NULL)
		xmlAddChild (parent, res);
	return FALSE;
}
static void
sheet_object_graph_write_xml_sax (SheetObject const *so, GsfXMLOut *output)
{
	SheetObjectGraph const *sog = SHEET_OBJECT_GRAPH (so);
	gog_object_write_xml_sax (GOG_OBJECT (sog->graph), output);
}

static void
sheet_object_graph_copy (SheetObject *dst, SheetObject const *src)
{
	SheetObjectGraph const *sog = SHEET_OBJECT_GRAPH (src);
	GogGraph *graph   = gog_graph_dup (sog->graph);
	sheet_object_graph_set_gog (dst, graph);
	g_object_unref (graph);
}

static void
sheet_object_graph_print (SheetObject const *so, GnomePrintContext *ctx,
			  double width, double height)
{
	gog_graph_print_to_gnome_print (
		SHEET_OBJECT_GRAPH (so)->graph, ctx, width, height);
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
 
	closure = g_cclosure_new (G_CALLBACK (cb_update_graph), data,
		(GClosureNotify) sheet_object_graph_user_config_free_data);
	
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
	*w = GO_CM_TO_PT ((double)12);
	*h = GO_CM_TO_PT ((double)8);
}

static void
sheet_object_graph_bounds_changed (SheetObject *so)
{
	/* If it has not been realized there is no renderer yet */
	if (SHEET_OBJECT_GRAPH (so)->renderer != NULL) {
		double coords [4];
		sheet_object_position_pts_get (so, coords);
		g_object_set (SHEET_OBJECT_GRAPH (so)->renderer,
			"logical-width-pts",  fabs (coords[2] - coords[0]),
			"logical-height-pts", fabs (coords[3] - coords[1]),
			NULL);
	}
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
	so_class->bounds_changed     = sheet_object_graph_bounds_changed;
	so_class->populate_menu	     = sheet_object_graph_populate_menu;
	so_class->read_xml_dom	     = sheet_object_graph_read_xml_dom;
	so_class->write_xml_dom	     = sheet_object_graph_write_xml_dom;
	so_class->write_xml_sax	     = sheet_object_graph_write_xml_sax;
	so_class->copy               = sheet_object_graph_copy;
	so_class->user_config        = sheet_object_graph_user_config;
	so_class->assign_to_sheet    = sheet_object_graph_set_sheet;
	so_class->remove_from_sheet  = sheet_object_graph_remove_from_sheet;
	so_class->print		     = sheet_object_graph_print;
	so_class->default_size	     = sheet_object_graph_default_size;

	so_class->rubber_band_directly = FALSE;
}

static void
sheet_object_graph_init (GObject *obj)
{
	SheetObject *so = SHEET_OBJECT (obj);
	so->anchor.direction = SO_DIR_DOWN_RIGHT;
}

static void
sog_imageable_init (SheetObjectImageableIface *soi_iface)
{
	soi_iface->write_image	= sheet_object_graph_write_image;
}

GSF_CLASS_FULL (SheetObjectGraph, sheet_object_graph,
		sheet_object_graph_class_init, sheet_object_graph_init,
		SHEET_OBJECT_TYPE, 0,
		GSF_INTERFACE (sog_imageable_init, SHEET_OBJECT_IMAGEABLE_TYPE));

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
	wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);
}

void
sheet_object_graph_guru (WorkbookControlGUI *wbcg, GogGraph *graph,
			 GClosure *closure)
{
	GtkWidget *dialog = gog_guru (graph, GOG_DATA_ALLOCATOR (wbcg),
		       GO_CMD_CONTEXT (wbcg), wbcg_toplevel (wbcg),
		       closure);
	wbcg_edit_attach_guru (wbcg, dialog);
	g_object_set_data_full (G_OBJECT (dialog),
		"guru", wbcg, (GDestroyNotify) cb_graph_guru_done);
}
