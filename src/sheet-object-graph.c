/*
 * sheet-object-graph.c: Wrapper for GNOME Office graphs in gnumeric
 *
 * Copyright (C) 2003-2005 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include <gnumeric.h>
#include <sheet-object-graph.h>

#include <gnm-pane-impl.h>
#include <sheet-control-gui.h>
#include <gui-util.h>
#include <gui-file.h>
#include <gnm-graph-window.h>
#include <style-color.h>
#include <sheet-object-impl.h>
#include <wbc-gtk.h>
#include <commands.h>
#include <application.h>
#include <sheet.h>
#include <print-info.h>
#include <workbook.h>
#include <workbook-view.h>
#include <gutils.h>
#include <graph.h>

#include <goffice/goffice.h>
#include <goffice/canvas/goc-graph.h>

#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <gsf/gsf-libxml.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <string.h>

static void
so_graph_view_set_bounds (SheetObjectView *sov, double const *coords, gboolean visible)
{
	GocItem *item = sheet_object_view_get_item (sov);
	double scale = goc_canvas_get_pixels_per_unit (item->canvas);

	if (visible) {
		goc_item_set (GOC_ITEM (sov),
			"x", MIN (coords [0], coords[2]) / scale,
			"y", MIN (coords [3], coords[1]) / scale,
			NULL);
		goc_item_set (item,
			"width", (fabs (coords [2] - coords [0]) + 1.) / scale,
			"height", (fabs (coords [3] - coords [1]) + 1.) / scale,
			NULL);

		goc_item_show (item);
	} else
		goc_item_hide (item);
}

static void
so_graph_goc_view_class_init (SheetObjectViewClass *sov_klass)
{
	sov_klass->set_bounds	= so_graph_view_set_bounds;
}

typedef SheetObjectView		SOGraphGocView;
typedef SheetObjectViewClass	SOGraphGocViewClass;

static GSF_CLASS (SOGraphGocView, so_graph_goc_view,
	so_graph_goc_view_class_init, NULL,
	GNM_SO_VIEW_TYPE)

/****************************************************************************/
#define SHEET_OBJECT_CONFIG_KEY "sheet-object-graph-key"

#define SHEET_OBJECT_GRAPH_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNM_SO_GRAPH_TYPE, SheetObjectGraphClass))

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
	g_object_set (sog->graph, "document", ((sheet)? sheet->workbook: NULL), NULL);
}

static void
gnm_sog_finalize (GObject *obj)
{
	SheetObjectGraph *sog = GNM_SO_GRAPH (obj);

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

static void
cb_graph_size_changed (GocItem *item, GtkAllocation *allocation)
{
	GogRenderer *rend;
	GogGraph *graph;
	SheetObject *so = sheet_object_view_get_so (GNM_SO_VIEW (item->parent));
	GnmPrintInformation *pi = so->sheet->print_info;
	double top, bottom, left, right, edge_to_below_header, edge_to_above_footer, w, h, x = 0., y = 0.;
	w = print_info_get_paper_width (pi, GTK_UNIT_POINTS);
	h = print_info_get_paper_height (pi, GTK_UNIT_POINTS);
	print_info_get_margins (pi, &top, &bottom, &left, &right, &edge_to_below_header, &edge_to_above_footer);
	w -= left + right;
	h -= edge_to_above_footer + edge_to_below_header;
	g_object_get (item, "renderer", &rend, NULL);
	g_object_get (rend, "model", &graph, NULL);
	gog_graph_set_size (graph, w, h);
	if (w / allocation->width > h / allocation->height) {
		h = allocation->width * h / w;
		w = allocation->width;
		y = (allocation->height - h) / 2.;
	} else {
		w = allocation->height * w / h;
		h = allocation->height;
		x = (allocation->width - w) / 2.;
	}
	goc_item_set (item, "x", x, "width", w, "y", y, "height", h, NULL);
	g_object_unref (graph);
	g_object_unref (rend);
}

static gboolean
cb_post_new_view (GocItem *item)
{
	GtkAllocation alloc;
	alloc.width = goc_canvas_get_width (item->canvas);
	alloc.height = goc_canvas_get_height (item->canvas);
	cb_graph_size_changed (item, &alloc);
	return FALSE;
}

static SheetObjectView *
gnm_sog_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	GnmPane *pane;
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);
	if (GNM_IS_PANE (container)) {
		GocItem *view;
		pane = GNM_PANE (container);
		view = goc_item_new (pane->object_views,
			so_graph_goc_view_get_type (),
			NULL);
		goc_item_new (GOC_GROUP (view),
			      GOC_TYPE_GRAPH,
			      "renderer", sog->renderer,
			      NULL);
		return gnm_pane_object_register (so, view, TRUE);
	} else {
		GocCanvas *canvas = GOC_CANVAS (container);
		GocItem *view = goc_item_new (goc_canvas_get_root (canvas),
			so_graph_goc_view_get_type (),
			NULL);
		GocItem *item = goc_item_new (GOC_GROUP (view),
			      GOC_TYPE_GRAPH,
			      "renderer", sog->renderer,
			      NULL);
		g_idle_add ((GSourceFunc) cb_post_new_view, item);
		g_signal_connect_swapped (canvas, "size_allocate", G_CALLBACK (cb_graph_size_changed), item);
		return (SheetObjectView *) view;
	}
}

static GtkTargetList *
gnm_sog_get_target_list (G_GNUC_UNUSED SheetObject const *so)
{
	GtkTargetList *tl = gtk_target_list_new (NULL, 0);
	char *mime_str = go_image_format_to_mime ("svg");
	GSList *mimes, *ptr;

	mimes = go_strsplit_to_slist (mime_str, ',');
	for (ptr = mimes; ptr != NULL; ptr = ptr->next) {
		const char *mime = ptr->data;

		if (mime != NULL && *mime != '\0')
			gtk_target_list_add (tl, gdk_atom_intern (mime, FALSE),
					     0, 0);
	}
	g_free (mime_str);
	g_slist_free_full (mimes, g_free);
	/* No need to eliminate duplicates. */
	gtk_target_list_add_image_targets (tl, 0, TRUE);

	return tl;
}

static GtkTargetList *
gnm_sog_get_object_target_list (G_GNUC_UNUSED SheetObject const *so)
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
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);
	gboolean res = FALSE;
	double coords[4];
	double w, h;
	GOImageFormat imfmt;

	if (so->sheet) {
		sheet_object_position_pts_get (GNM_SO (sog), coords);
		w = fabs (coords[2] - coords[0]) + 1.;
		h = fabs (coords[3] - coords[1]) + 1.;
	} else {
		w = GPOINTER_TO_UINT
			(g_object_get_data (G_OBJECT (so), "pt-width-at-copy"));
		h = GPOINTER_TO_UINT
			(g_object_get_data (G_OBJECT (so), "pt-height-at-copy"));
	}

	g_return_if_fail (w > 0 && h > 0);

	imfmt = go_image_get_format_from_name (format);
	if (imfmt == GO_IMAGE_FORMAT_UNKNOWN) {
		res = FALSE;
		if (err)
			*err = g_error_new (gsf_output_error_id (), 0,
					    _("Unknown image format"));
	} else {
		res = gog_graph_export_image (sog->graph, imfmt,
					      output, resolution, resolution);
	}

	if (!res && err && *err == NULL)
		*err = g_error_new (gsf_output_error_id (), 0,
				    _("Unknown failure while saving image"));
}

static void
gnm_sog_write_object (SheetObject const *so, char const *format,
		      GsfOutput *output, G_GNUC_UNUSED GError **err,
		      GnmConventions const *convs)
{
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);
	GsfXMLOut *xout;
	GogObject *graph;

	g_return_if_fail (strcmp (format, "application/x-goffice-graph") == 0);

	graph = gog_object_dup (GOG_OBJECT (sog->graph),
		NULL, gog_dataset_dup_to_simple);
	xout = gsf_xml_out_new (output);
	gog_object_write_xml_sax (GOG_OBJECT (graph), xout, (gpointer)convs);
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
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);
	double resolution;

	g_return_if_fail (sog != NULL);

	l = gog_graph_get_supported_image_formats ();
	g_return_if_fail (l != NULL);
	selected_format = GPOINTER_TO_UINT (l->data);

#warning "This violates model gui barrier"
	wbcg = scg_wbcg (GNM_SCG (sc));
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
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);
	SheetControlGUI *scg = GNM_SCG (sc);
	WBCGtk *wbcg = scg_wbcg (scg);
	GtkWidget *window;
	double coords[4];

	g_return_if_fail (sog != NULL);

	scg_object_anchor_to_coords (scg, sheet_object_get_anchor (so), coords);
	window = gnm_graph_window_new (sog->graph,
				       floor (fabs (coords[2] - coords[0]) + 0.5),
				       floor (fabs (coords[3] - coords[1]) + 0.5));
	gtk_window_set_screen (GTK_WINDOW (window),
			       gtk_window_get_screen (wbcg_toplevel (wbcg)));
	gtk_window_present (GTK_WINDOW (window));
	g_signal_connect (window, "delete-event",
			  G_CALLBACK (gtk_widget_destroy),
			  NULL);
}

static void
sog_cb_copy_to_new_sheet (SheetObject *so, SheetControl *sc)
{
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);
	SheetControlGUI *scg = GNM_SCG (sc);
	WorkbookControl *wbc = scg_wbc (scg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	GogGraph *graph = GOG_GRAPH (gog_object_dup (GOG_OBJECT (sog->graph), NULL, NULL));
	WorkbookSheetState *old_state = workbook_sheet_state_new (wb_control_get_workbook (wbc));
	Sheet *new_sheet = workbook_sheet_add_with_type (
		wb_control_get_workbook (wbc),
		GNM_SHEET_OBJECT, -1,
		gnm_sheet_get_max_cols (sheet),
		gnm_sheet_get_max_rows (sheet));
	SheetObject *new_sog = sheet_object_graph_new (graph);
	print_info_set_paper_orientation (new_sheet->print_info, GTK_PAGE_ORIENTATION_LANDSCAPE);
	sheet_object_set_sheet (new_sog, new_sheet);
	wb_view_sheet_focus (wb_control_view (wbc), new_sheet);
	cmd_reorganize_sheets (wbc, old_state, sheet);
	g_object_unref (graph);
	g_object_unref (new_sog);
}

static void
gnm_sog_populate_menu (SheetObject *so, GPtrArray *actions)
{
	static SheetObjectAction const sog_actions[] = {
		{ "document-save-as", N_("_Save As Image"),           NULL, 0, sog_cb_save_as,            NULL },
		{ NULL,               N_("Open in _New Window"),      NULL, 0, sog_cb_open_in_new_window, NULL },
		{ NULL,               N_("Copy to New Graph S_heet"), NULL, 0, sog_cb_copy_to_new_sheet,  NULL }
	};

	unsigned int i;

	GNM_SO_CLASS (parent_klass)->populate_menu (so, actions);

	for (i = 0; i < G_N_ELEMENTS (sog_actions); i++)
		g_ptr_array_insert (actions, 1 + i, (gpointer) (sog_actions + i));
}

static void
gnm_sog_write_xml_sax (SheetObject const *so, GsfXMLOut *output,
		       GnmConventions const *convs)
{
	SheetObjectGraph const *sog = GNM_SO_GRAPH (so);
	gog_object_write_xml_sax (GOG_OBJECT (sog->graph), output,
				  (gpointer)convs);
}

static void
sog_xml_finish (GogObject *graph, SheetObject *so)
{
	sheet_object_graph_set_gog (so, GOG_GRAPH (graph));
	g_object_unref (graph);
}

static void gnm_sogg_prep_sax_parser (SheetObject *so, GsfXMLIn *xin,
                                      xmlChar const **attrs,
                                      GnmConventions const *convs);
static void
gnm_sog_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs,
			 GnmConventions const *convs)
{
	if (strcmp (xin->node->name, "GnmGraph"))
		gog_object_sax_push_parser (xin, attrs,
					    (GogObjectSaxHandler) sog_xml_finish,
					    (gpointer)convs, so);
	else
		gnm_sogg_prep_sax_parser (so, xin, attrs, convs);
}

static void
gnm_sog_copy (SheetObject *dst, SheetObject const *src)
{
	SheetObjectGraph const *sog = GNM_SO_GRAPH (src);
	GogGraph *graph = gog_graph_dup (sog->graph);
	sheet_object_graph_set_gog (dst, graph);
	g_object_unref (graph);
}

static void
gnm_sog_draw_cairo (SheetObject const *so, cairo_t *cr,
			  double width, double height)
{
	gog_graph_render_to_cairo (GNM_SO_GRAPH (so)->graph,
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
			     G_OBJECT (GNM_SO_GRAPH (data->so)->graph));
}

static void
gnm_sog_user_config (SheetObject *so, SheetControl *sc)
{
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);
	WBCGtk *wbcg;
	gnm_sog_user_config_t *data;
	GClosure *closure;

	g_return_if_fail (sog != NULL);
	g_return_if_fail (sc != NULL);

	wbcg = scg_wbcg (GNM_SCG (sc));

	data = g_new0 (gnm_sog_user_config_t, 1);
	data->so = so;
	data->wbc = GNM_WBC (wbcg);

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
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);
	GSList *ptr = gog_graph_get_data (sog->graph);
	for (; ptr != NULL ; ptr = ptr->next)
		sog_data_foreach_dep (so, ptr->data, func, user);
}

static void
sog_update_graph_size (SheetObjectGraph *sog)
{
	double coords[4];
	SheetObject *so = GNM_SO (sog);

	if (sog->graph == NULL || so->sheet == NULL ||
	    so->sheet->sheet_type != GNM_SHEET_DATA)
		return;

	sheet_object_position_pts_get (so, coords);
	gog_graph_set_size (sog->graph,
			    fabs (coords[2] - coords[0]),
			    fabs (coords[3] - coords[1]));
}

static gboolean
gnm_sog_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);

	if (sog->graph != NULL) {
		sog_datas_set_sheet (sog, sheet);
		sog_update_graph_size (sog);
	}

	return FALSE;
}

static gboolean
gnm_sog_remove_from_sheet (SheetObject *so)
{
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);
	if (sog->graph != NULL)
		sog_datas_set_sheet (sog, NULL);
	return FALSE;
}

static void
gnm_sog_default_size (G_GNUC_UNUSED SheetObject const *so, double *w, double *h)
{
	*w = GO_CM_TO_PT ((double)12);
	*h = GO_CM_TO_PT ((double)8);
}

static void
gnm_sog_bounds_changed (SheetObject *so)
{
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);

	/* If it has not been realized there is no renderer yet */
	if (sog->renderer != NULL)
		sog_update_graph_size (sog);
}

static void
gnm_sog_class_init (GObjectClass *klass)
{
	SheetObjectClass	*so_class  = GNM_SO_CLASS (klass);

	parent_klass = g_type_class_peek_parent (klass);

	/* Object class method overrides */
	klass->finalize = gnm_sog_finalize;

	/* SheetObject class method overrides */
	so_class->new_view		= gnm_sog_new_view;
	so_class->bounds_changed	= gnm_sog_bounds_changed;
	so_class->populate_menu		= gnm_sog_populate_menu;
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
	SheetObject *so = GNM_SO (obj);
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
		gnm_sog_init, GNM_SO_TYPE, 0,
		GSF_INTERFACE (sog_imageable_init, GNM_SO_IMAGEABLE_TYPE) \
		GSF_INTERFACE (sog_exportable_init, GNM_SO_EXPORTABLE_TYPE))

/**
 * sheet_object_graph_new:
 * @graph: (allow-none): #GogGraph
 *
 * Adds a reference to @graph and creates a gnumeric sheet object wrapper
 * Returns: (transfer full): the newly allocated #SheetObject.
 **/
SheetObject *
sheet_object_graph_new (GogGraph *graph)
{
	SheetObject *sog = g_object_new (GNM_SO_GRAPH_TYPE, NULL);
	GnmGraphDataClosure *data = graph
		? (GnmGraphDataClosure *) g_object_get_data (G_OBJECT (graph), "data-closure")
		: NULL;
	sheet_object_graph_set_gog (sog, graph);
	if (data != NULL)
		sog->anchor.mode = data->anchor_mode;

	return sog;
}

/**
 * sheet_object_graph_get_gog:
 * @sog: #SheetObject
 *
 * Returns: (transfer none): the embedded #GogGraph or %NULL on error.
 **/

GogGraph *
sheet_object_graph_get_gog (SheetObject *sog)
{
	g_return_val_if_fail (GNM_IS_SO_GRAPH (sog), NULL);

	return ((SheetObjectGraph *)sog)->graph;
}

/**
 * sheet_object_graph_set_gog:
 * @sog: #SheetObjectGraph
 * @graph: (allow-none): #GogGraph
 *
 * If @graph is non-%NULL add a reference to it, otherwise create a new graph.
 * Assign the graph to its SheetObjectGraph wrapper and initialize the
 * handlers, disconnecting any existing connection for the preceding graph.
 **/
void
sheet_object_graph_set_gog (SheetObject *so, GogGraph *graph)
{
	SheetObjectGraph *sog = GNM_SO_GRAPH (so);

	g_return_if_fail (GNM_IS_SO_GRAPH (so));

	if (graph != NULL) {
		if (sog->graph == graph)
			return;

		g_object_ref (graph);
	} else
		graph = g_object_new (GOG_TYPE_GRAPH, NULL);

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

	sog_update_graph_size (sog);
}

static void
cb_graph_guru_done (WBCGtk *wbcg)
{
	wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);
}

static void
cb_graph_data_closure_done (GnmGraphDataClosure *data)
{
	if (data->obj)
		g_object_set_data (data->obj,"data-closure", NULL);
	g_free (data);
}

static void
cb_selection_mode_changed (GtkComboBox *box, GnmGraphDataClosure *data)
{
	GogObject *graph = (GogObject *) g_object_get_data (data->obj, "graph");
	data->colrowmode = gtk_combo_box_get_active (box);
	if (graph) {
		GogObject *gobj = gog_object_get_child_by_name (graph, "Chart");
		gobj = gog_object_get_child_by_name (gobj, "Plot");
		if (!gobj)
			return;
		gog_plot_clear_series (GOG_PLOT (gobj));
		gog_data_allocator_allocate (data->dalloc, GOG_PLOT (gobj));
	}
}

static void
cb_shared_mode_changed (GtkToggleButton *btn, GnmGraphDataClosure *data)
{
	GogObject *graph = (GogObject *) g_object_get_data (data->obj, "graph");
	data->share_x = gtk_toggle_button_get_active (btn);
	if (graph) {
		GogObject *gobj = gog_object_get_child_by_name (graph, "Chart");
		gobj = gog_object_get_child_by_name (gobj, "Plot");
		if (!gobj)
			return;
		gog_plot_clear_series (GOG_PLOT (gobj));
		gog_data_allocator_allocate (data->dalloc, GOG_PLOT (gobj));
	}
}

static void
cb_sheet_target_changed (GtkToggleButton *btn, GnmGraphDataClosure *data)
{
	data->new_sheet = gtk_toggle_button_get_active (btn);
}

void
sheet_object_graph_guru (WBCGtk *wbcg, GogGraph *graph,
			 GClosure *closure)
{
	GtkWidget *dialog = gog_guru (graph, GOG_DATA_ALLOCATOR (wbcg),
		GO_CMD_CONTEXT (wbcg), closure);
	if (!graph) {
		GnmGraphDataClosure *data = (GnmGraphDataClosure *) g_new0 (GnmGraphDataClosure, 1);
		GtkWidget *custom = gtk_grid_new (), *w;
		GObject *object;

		data->dalloc = GOG_DATA_ALLOCATOR (wbcg);
		g_object_set (custom,
		              "row-spacing", 6,
		              "column-spacing", 12,
		              "margin-top", 6,
		              NULL);
		w = gtk_label_new (_("Series as:"));
		g_object_set (w, "xalign", 0., NULL);
		gtk_grid_attach (GTK_GRID (custom), w, 0, 0, 1, 1);
		w = gtk_combo_box_text_new ();
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (w), _("Auto"));
		/*Translators: Series as "Columns"*/
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (w), C_("graph", "Columns"));
		/*Translators: Series as "Rows"*/
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (w), C_("graph", "Rows"));
		gtk_combo_box_set_active (GTK_COMBO_BOX (w), 0);
		g_signal_connect (G_OBJECT (w), "changed", G_CALLBACK (cb_selection_mode_changed), data);
		gtk_grid_attach (GTK_GRID (custom), w, 1, 0, 1, 1);
		w = gtk_check_button_new_with_label (_("Use first series as shared abscissa"));
		g_signal_connect (G_OBJECT (w), "toggled", G_CALLBACK (cb_shared_mode_changed), data);
		gtk_grid_attach (GTK_GRID (custom), w, 0, 1, 2, 1);
		w = gtk_check_button_new_with_label (_("New graph sheet"));
		g_signal_connect (G_OBJECT (w), "toggled", G_CALLBACK (cb_sheet_target_changed), data);
		gtk_grid_attach (GTK_GRID (custom), w, 0, 2, 2, 1);
		data->obj = G_OBJECT (custom);
		data->anchor_mode = GNM_SO_ANCHOR_ONE_CELL; /* don't resize graphs with cells, see # */
		gog_guru_add_custom_widget (dialog, custom);
		object = (GObject*) g_object_get_data (data->obj, "graph");
		if (object)
			g_object_set_data (object, "data-closure", data);
		g_object_set_data_full (G_OBJECT (custom), "data-closure", data, (GDestroyNotify) cb_graph_data_closure_done);
	}
	gnm_init_help_button (
		gog_guru_get_help_button (dialog),
		"chapter-graphs");
	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (dialog),
		wbcg, GNM_DIALOG_DESTROY_SHEET_REMOVED);
	gnm_keyed_dialog (wbcg, GTK_WINDOW (dialog), "graph-guru");
	g_object_set_data_full (G_OBJECT (dialog),
		"guru", wbcg, (GDestroyNotify) cb_graph_guru_done);
	wbc_gtk_attach_guru (wbcg, dialog);
	gtk_widget_show (dialog);
}

/**
 * sheet_object_graph_ensure_size:
 * @sog: #SheetObject
 *
 * Updates the size of the graph item in the canvas for graph sheets objects.
 */
void
sheet_object_graph_ensure_size (SheetObject *sog)
{
	GList *ptr = sog->realized_list;
	while (ptr) {
		SheetObjectView *sov = ptr->data;
		cb_post_new_view (sheet_object_view_get_item (sov));
		ptr = ptr->next;
	}
}

/*****************************************************************************/
/* Support for Guppi graphs */

typedef struct {
	GnmConventions const *convs;
	GogGraph *graph;
	GogObject *chart;
	GogPlot *plot;
	GogObject *cur;
	GogStyle *style;
	GPtrArray *data;
	unsigned cur_index, max_data;
} GuppiReadState;

static void
vector_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	GuppiReadState *state = (GuppiReadState *) xin->user_state;
	int i;
	for (i = 0; attrs != NULL && attrs[i] && attrs[i+1] ; i += 2)
		if (0 == strcmp (attrs[i], "ID"))
			state->cur_index = strtoul (attrs[i+1], NULL, 10);
	if (state->cur_index < 256 && state->cur_index >= state->max_data) {
		state->max_data += 10;
		g_ptr_array_set_size (state->data, state->max_data);
	}
}

static void
vector_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *unknown)
{
	GuppiReadState *state = (GuppiReadState *) xin->user_state;
	GOData *data;
	if (state->cur_index >= state->max_data)
		return;
	data = g_object_new (GNM_GO_DATA_VECTOR_TYPE, NULL);
	go_data_unserialize (data, xin->content->str, (void*) state->convs);
	g_ptr_array_index (state->data, state->cur_index) = data;
}

static void
plot_type_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	GuppiReadState *state = (GuppiReadState *) xin->user_state;
	int i;
	char const *name = NULL;
	for (i = 0; attrs != NULL && attrs[i] && attrs[i+1] ; i += 2)
		if (0 == strcmp (attrs[i], "name"))
			name = attrs[i+1];
	if (name) {
		if (0 == strcmp (name, "Scatter")) {
			state->plot = gog_plot_new_by_name ("GogXYPlot");
			g_object_set (G_OBJECT (state->plot),
			              "default-style-has-markers", FALSE,
			              "default-style-has-lines", FALSE,
			              NULL);
			gog_object_add_by_name (state->chart, "Backplane", NULL);
		}
		else if (0 == strcmp (name, "Pie"))
			state->plot = gog_plot_new_by_name ("GogPiePlot");
		else if (0 == strcmp (name, "Bar")) {
			state->plot = gog_plot_new_by_name ("GogBarColPlot");
			gog_object_add_by_name (state->chart, "Backplane", NULL);
		} else if (0 == strcmp (name, "Line")) {
			state->plot = gog_plot_new_by_name ("GogLinePlot");
			g_object_set (G_OBJECT (state->plot),
			              "default-style-has-markers", FALSE,
			              NULL);
			gog_object_add_by_name (state->chart, "Backplane", NULL);
		}
		if (state->plot)
			gog_object_add_by_name (GOG_OBJECT (state->chart), "Plot", GOG_OBJECT (state->plot));
	}
}

static void
legend_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	GuppiReadState *state = (GuppiReadState *) xin->user_state;
	state->cur = gog_object_add_by_name (state->chart, "Legend", NULL);
}

static void
position_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *unknown)
{
	GuppiReadState *state = (GuppiReadState *)xin->user_state;
	if (!xin->content->str)
		return;
	if (0 == strcmp (xin->content->str, "east"))
		g_object_set (G_OBJECT (state->cur), "compass", "right", NULL);
	if (0 == strcmp (xin->content->str, "west"))
		g_object_set (G_OBJECT (state->cur), "compass", "left", NULL);
	if (0 == strcmp (xin->content->str, "north"))
		g_object_set (G_OBJECT (state->cur), "compass", "top", NULL);
	if (0 == strcmp (xin->content->str, "south"))
		g_object_set (G_OBJECT (state->cur), "compass", "bottom", NULL);
}

static void
series_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	GuppiReadState *state = (GuppiReadState *) xin->user_state;
	int i;
	char *name = NULL;
	GError *err = NULL;
	GOData *data;

	state->cur = GOG_OBJECT (gog_plot_new_series (state->plot));
	for (i = 0; attrs != NULL && attrs[i] && attrs[i+1] ; i += 2)
		if (0 == strcmp (attrs[i], "name"))
			name = g_strdup_printf ("\"%s\"", attrs[i+1]);
	if (name) {
		data = g_object_new (GNM_GO_DATA_SCALAR_TYPE, NULL);
		go_data_unserialize (data, name, (void*) state->convs);
		gog_dataset_set_dim (GOG_DATASET (state->cur), -1,
		                     data, &err);
		g_free (name);
	}
	if (err)
		g_error_free (err);
}

static void
dim_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	GuppiReadState *state = (GuppiReadState *) xin->user_state;
	unsigned i, id = 0;
	char const *name = "?";
	GogMSDimType type = GOG_MS_DIM_LABELS;
	GogPlotDesc const *desc = gog_plot_description (state->plot);
	GError *err = NULL;
	for (i = 0; attrs != NULL && attrs[i] && attrs[i+1] ; i += 2)
		if (0 == strcmp (attrs[i], "dim_name"))
			name = attrs[i+1];
		else if (0 == strcmp (attrs[i], "ID"))
			id = strtoul (attrs[i+1], NULL, 10);
	if (!desc ||
	    id >= state->data->len || g_ptr_array_index (state->data, id) == NULL)
		return;
	if (0 == strcmp (name, "values"))
		type = GOG_MS_DIM_VALUES;
	else if (0 == strcmp (name, "categories"))
		type = GOG_MS_DIM_CATEGORIES;
	else if (0 == strcmp (name, "bubbles"))
		type = GOG_MS_DIM_BUBBLES;
	for (i = 0; i < desc->series.num_dim; i++)
		if (desc->series.dim[i].ms_type == type) {
			GOData *data = g_object_ref (g_ptr_array_index (state->data, id));
			gog_dataset_set_dim (GOG_DATASET (state->cur), i,data,
			                     &err);
			break;
		}
	if (err)
		g_error_free (err);
}

static void
marker_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *unknown)
{
	GuppiReadState *state = (GuppiReadState *)xin->user_state;
	if (xin->content->str && 0 == strcmp (xin->content->str, "true"))
		g_object_set (G_OBJECT (state->plot), "default-style-has-markers", TRUE, NULL);
}

static void
linear_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	GuppiReadState *state = (GuppiReadState *)xin->user_state;
	g_object_set (G_OBJECT (state->plot),
	              "default-style-has-lines", TRUE,
	              NULL);
}

static void
cubic_start (GsfXMLIn *xin, G_GNUC_UNUSED xmlChar const **attrs)
{
	GuppiReadState *state = (GuppiReadState *)xin->user_state;
	g_object_set (G_OBJECT (state->plot),
	              "default-style-has-lines", TRUE,
	              "use-splines", TRUE,
	              NULL);
}

static void
horiz_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *unknown)
{
	GuppiReadState *state = (GuppiReadState *)xin->user_state;
	if (xin->content->str)
		g_object_set (G_OBJECT (state->plot), "horizontal", 0 == strcmp (xin->content->str, "true"), NULL);
}

static void
stacked_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *unknown)
{
	GuppiReadState *state = (GuppiReadState *)xin->user_state;
	if (xin->content->str && 0 == strcmp (xin->content->str, "true"))
		g_object_set (G_OBJECT (state->plot), "type", "stacked", NULL);
}

static void
percent_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *unknown)
{
	GuppiReadState *state = (GuppiReadState *)xin->user_state;
	if (xin->content->str && 0 == strcmp (xin->content->str, "true"))
		g_object_set (G_OBJECT (state->plot), "type", "as_percentage", NULL);
}

static void
separation_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *unknown)
{
	GuppiReadState *state = (GuppiReadState *)xin->user_state;
	if (xin->content->str) {
		double separation = g_ascii_strtod (xin->content->str, NULL);
		g_object_set (G_OBJECT (state->plot),
			      "default-separation", separation / 100.,
			      NULL);
	}
}

static void
bubble_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *unknown)
{
	GuppiReadState *state = (GuppiReadState *)xin->user_state;
	if (xin->content->str && 0 == strcmp (xin->content->str, "true")) {
		g_object_unref (state->plot);
		state->plot = gog_plot_new_by_name ("GogBubblePlot");
		gog_object_add_by_name (state->chart, "Backplane", NULL);
	}
}

static void
gnm_sogg_sax_parser_done (G_GNUC_UNUSED GsfXMLIn *xin, GuppiReadState *state)
{
	unsigned i;
	GObject *obj;
	g_object_unref (state->graph);
	for (i = 0; i < state->max_data; i++) {
		obj = (GObject *) g_ptr_array_index (state->data, i);
		if (obj)
			g_object_unref (obj);
	}
	g_ptr_array_free (state->data, TRUE);
	g_free (state);
}

static void
gnm_sogg_prep_sax_parser (SheetObject *so, GsfXMLIn *xin, xmlChar const **attrs,
			 GnmConventions const *convs)
{
	static GsfXMLInNode const dtd[] = {
	  GSF_XML_IN_NODE (GRAPH, GRAPH, -1, "GmrGraph", GSF_XML_NO_CONTENT, NULL, NULL),
	    GSF_XML_IN_NODE (GRAPH, GUPPI_VECTORS, -1, "gmr:Vectors", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (GUPPI_VECTORS, GUPPI_VECTOR, -1, "gmr:Vector", GSF_XML_CONTENT, vector_start, vector_end),
	    GSF_XML_IN_NODE (GRAPH, GUPPI_GRAPH, -1, "graph:Graph", GSF_XML_NO_CONTENT, NULL, NULL),
	      GSF_XML_IN_NODE (GUPPI_GRAPH, GUPPI_LEGEND, -1, "graph:Legend", GSF_XML_NO_CONTENT, legend_start, NULL),
		GSF_XML_IN_NODE (GUPPI_LEGEND, GUPPI_LEGEND_POS, -1, "graph:Position", GSF_XML_CONTENT, NULL, position_end),
	      GSF_XML_IN_NODE (GUPPI_GRAPH, GUPPI_PLOTS, -1, "graph:Plots", GSF_XML_NO_CONTENT, NULL, NULL),
		GSF_XML_IN_NODE (GUPPI_PLOTS, GUPPI_PLOT, -1, "graph:Plot", GSF_XML_NO_CONTENT, NULL, NULL),
		  GSF_XML_IN_NODE (GUPPI_PLOT, GUPPI_PLOT_TYPE, -1, "Type", GSF_XML_NO_CONTENT, plot_type_start, NULL),
		    GSF_XML_IN_NODE (GUPPI_PLOT_TYPE, GUPPI_MARKER, -1, "with_marker", GSF_XML_CONTENT, NULL, marker_end),
		    GSF_XML_IN_NODE (GUPPI_PLOT_TYPE, GUPPI_MARKERS, -1, "with_markers", GSF_XML_CONTENT, NULL, marker_end),
		    GSF_XML_IN_NODE (GUPPI_PLOT_TYPE, GUPPI_LINES, -1, "with_line", GSF_XML_NO_CONTENT, NULL, NULL),
		      GSF_XML_IN_NODE (GUPPI_LINES, GUPPI_PLOT_LINEAR, -1, "Linear", GSF_XML_NO_CONTENT, linear_start, NULL),
		      GSF_XML_IN_NODE (GUPPI_LINES, GUPPI_PLOT_CUBIC, -1, "Cubic", GSF_XML_NO_CONTENT, cubic_start, NULL),
		    GSF_XML_IN_NODE (GUPPI_PLOT_TYPE, GUPPI_HORIZONTAL, -1, "horizontal", GSF_XML_CONTENT, NULL, horiz_end),
		    GSF_XML_IN_NODE (GUPPI_PLOT_TYPE, GUPPI_STACKED, -1, "stacked", GSF_XML_CONTENT, NULL, stacked_end),
		    GSF_XML_IN_NODE (GUPPI_PLOT_TYPE, GUPPI_AS_PERCENT, -1, "as_percentage", GSF_XML_CONTENT, NULL, percent_end),
		    GSF_XML_IN_NODE (GUPPI_PLOT_TYPE, GUPPI_SEPARATION, -1, "separation_percent_of_radius", GSF_XML_CONTENT, NULL, separation_end),
		    GSF_XML_IN_NODE (GUPPI_PLOT_TYPE, GUPPI_BUBBLE, -1, "auto_allocate_bubble_size", GSF_XML_CONTENT, NULL, bubble_end),
		  GSF_XML_IN_NODE (GUPPI_PLOT, GUPPI_PLOT_GRAPH_TYPE, -1, "graph:Type", GSF_XML_NO_CONTENT, plot_type_start, NULL),
		    GSF_XML_IN_NODE (GUPPI_PLOT_GRAPH_TYPE, GUPPI_MARKER, -1, "with_marker", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (GUPPI_PLOT_GRAPH_TYPE, GUPPI_MARKERS, -1, "with_markers", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (GUPPI_PLOT_GRAPH_TYPE, GUPPI_LINES, -1, "with_line", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (GUPPI_PLOT_GRAPH_TYPE, GUPPI_HORIZONTAL, -1, "horizontal", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (GUPPI_PLOT_GRAPH_TYPE, GUPPI_STACKED, -1, "stacked", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (GUPPI_PLOT_GRAPH_TYPE, GUPPI_AS_PERCENT, -1, "as_percentage", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (GUPPI_PLOT_GRAPH_TYPE, GUPPI_SEPARATION, -1, "separation_percent_of_radius", GSF_XML_2ND, NULL, NULL),
		    GSF_XML_IN_NODE (GUPPI_PLOT_GRAPH_TYPE, GUPPI_BUBBLE, -1, "auto_allocate_bubble_size", GSF_XML_2ND, NULL, NULL),
		  GSF_XML_IN_NODE (GUPPI_PLOT, GUPPI_DATA, -1, "graph:Data", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (GUPPI_DATA, GUPPI_SERIES, -1, "graph:Series", GSF_XML_NO_CONTENT, series_start, NULL),
		    GSF_XML_IN_NODE (GUPPI_SERIES, GUPPI_SERIES_DIM, -1, "graph:Dimension", GSF_XML_NO_CONTENT, dim_start, NULL),
		  GSF_XML_IN_NODE (GUPPI_PLOT, GUPPI_DATA_LAYOUT, -1, "graph:DataLayout", GSF_XML_NO_CONTENT, NULL, NULL),
		    GSF_XML_IN_NODE (GUPPI_DATA_LAYOUT, GUPPI_DIMENSION, -1, "graph:Dimension", GSF_XML_NO_CONTENT, NULL, NULL),
	  GSF_XML_IN_NODE_END
	};
	static GsfXMLInDoc *doc = NULL;
	GuppiReadState *state;
	GogTheme *theme = gog_theme_registry_lookup ("Guppi");

	if (NULL == doc) {
		doc = gsf_xml_in_doc_new (dtd, NULL);
		gnm_xml_in_doc_dispose_on_exit (&doc);
	}
	state = g_new0 (GuppiReadState, 1);
	state->graph = g_object_new (GOG_TYPE_GRAPH, NULL);
	gog_graph_set_theme (state->graph, theme);
	state->chart = gog_object_add_by_name (GOG_OBJECT (state->graph), "Chart", NULL);
	state->convs = convs;
	state->data = g_ptr_array_new ();
	state->max_data = 10;
	g_ptr_array_set_size (state->data, state->max_data);

	sheet_object_graph_set_gog (so, state->graph);
	gsf_xml_in_push_state (xin, doc, state,
		(GsfXMLInExtDtor) gnm_sogg_sax_parser_done, attrs);
}
