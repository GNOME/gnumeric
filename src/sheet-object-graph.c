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

#ifdef NEW_GRAPHS
#include <goffice/graph/go-graph-item.h>
#include <goffice/graph/go-graph.h>
#include <graph.h>
#endif

#include <gdk/gdkkeysyms.h>
#include <gsf/gsf-impl-utils.h>
#include <gal/widgets/widget-color-combo.h>
#include <libfoocanvas/foo-canvas-line.h>
#include <libfoocanvas/foo-canvas-rect-ellipse.h>
#include <libfoocanvas/foo-canvas-polygon.h>
#include <libfoocanvas/foo-canvas-text.h>
#include <math.h>

#define SHEET_OBJECT_CONFIG_KEY "sheet-object-graph-key"

#define SHEET_OBJECT_GRAPH(o)       (G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_OBJECT_GRAPH_TYPE, SheetObjectGraph))
#define SHEET_OBJECT_GRAPH_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_OBJECT_GRAPH_TYPE, SheetObjectGraphClass))

typedef struct {
	SheetObject  base;
#ifdef NEW_GRAPHS
	GOGraph     *graph;
#endif
} SheetObjectGraph;
typedef struct {
	SheetObjectClass base;
} SheetObjectGraphClass;

static GObjectClass *parent_klass;

#ifdef NEW_GRAPHS
static void
sog_data_set_sheet (SheetObjectGraph *graph, GOData *data, Sheet *sheet)
{
	if (IS_GNM_GO_DATA_SCALAR (data))
		gnm_go_data_scalar_set_sheet (GNM_GO_DATA_SCALAR (data), sheet);
	else if (IS_GNM_GO_DATA_VECTOR (data))
		gnm_go_data_vector_set_sheet (GNM_GO_DATA_VECTOR (data), sheet);
}

static void
cb_graph_add_data (G_GNUC_UNUSED GOGraph *graph,
		   GOData *data, SheetObjectGraph *sog)
{
	sog_data_set_sheet (sog, data, sog->base.sheet);
}

static void
cb_graph_remove_data (G_GNUC_UNUSED GOGraph *graph,
		      GOData *data, SheetObjectGraph *sog)
{
	sog_data_set_sheet (sog, data, NULL);
}

/**
 * sheet_object_graph_new :
 * @graph : #GOGraph
 *
 * Adds a reference to @graph and creates a gnumeric sheet object wrapper
 **/
SheetObject *
sheet_object_graph_new (GOGraph *graph)
{
	SheetObjectGraph *so = g_object_new (SHEET_OBJECT_GRAPH_TYPE, NULL);
	so->graph = graph;
	g_object_ref (G_OBJECT (so->graph));

	g_signal_connect_object (G_OBJECT (graph),
		"add_data",
		G_CALLBACK (cb_graph_add_data), G_OBJECT (so), 0);
	g_signal_connect_object (G_OBJECT (graph),
		"remove_data",
		G_CALLBACK (cb_graph_remove_data), G_OBJECT (so), 0);

	return SHEET_OBJECT (so);
}
#endif

static void
sheet_object_graph_finalize (GObject *obj)
{
	SheetObjectGraph *graph = SHEET_OBJECT_GRAPH (obj);

#ifdef NEW_GRAPHS
	if (graph->graph != NULL) {
		g_object_unref (graph->graph);
		graph->graph = NULL;
	}
#endif

	if (parent_klass && parent_klass->finalize)
		parent_klass->finalize (obj);
}

static GObject *
sheet_object_graph_new_view (SheetObject *so, SheetControl *sc, gpointer key)
{
#ifdef NEW_GRAPHS
	GnmCanvas *gcanvas = ((GnumericPane *)key)->gcanvas;
	SheetObjectGraph *graph = SHEET_OBJECT_GRAPH (so);
	FooCanvasItem *item = go_graph_item_new_view  (GO_GRAPH_ITEM (graph->graph),
		(gpointer)gcanvas->sheet_object_group);
	foo_canvas_item_raise_to_top (FOO_CANVAS_ITEM (gcanvas->sheet_object_group));

	gnm_pane_object_register (so, item);
	return G_OBJECT (item);
#else
	return NULL;
#endif
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
		"x", coords [0], "y", coords [1],
		"w", coords [2] - coords [0] + 1.,
		"h", coords [3] - coords [1] + 1.,
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
	return FALSE;
}

static gboolean
sheet_object_graph_write_xml (SheetObject const *so,
			      XmlParseContext const *ctxt, xmlNodePtr tree)
{
	return FALSE;
}

static SheetObject *
sheet_object_graph_clone (SheetObject const *so, Sheet *sheet)
{
	SheetObjectGraph *graph;
	SheetObjectGraph *new_graph;

	g_return_val_if_fail (IS_SHEET_OBJECT_GRAPH (so), NULL);
	graph = SHEET_OBJECT_GRAPH (so);

	new_graph = g_object_new (G_OBJECT_TYPE (so), NULL);

	return SHEET_OBJECT (new_graph);
}

static void
sheet_object_graph_print (SheetObject const *so, GnomePrintContext *ctx,
			    double base_x, double base_y)
{
	SheetObjectGraph *graph;
	double coords [4];

	g_return_if_fail (IS_SHEET_OBJECT_GRAPH (so));
	g_return_if_fail (GNOME_IS_PRINT_CONTEXT (ctx));
	graph = SHEET_OBJECT_GRAPH (so);

	sheet_object_position_pts_get (so, coords);

	gnome_print_gsave (ctx);

	gnome_print_grestore (ctx);
}

static void
sheet_object_graph_user_config (SheetObject *so, SheetControl *sc)
{
	SheetObjectGraph *graph = SHEET_OBJECT_GRAPH (so);
	WorkbookControlGUI *wbcg = scg_get_wbcg (SHEET_CONTROL_GUI (sc));

	g_return_if_fail (graph != NULL);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SHEET_OBJECT_CONFIG_KEY))
		return;
}

static gboolean
sheet_object_graph_set_sheet (SheetObject *so, Sheet *sheet)
{
	SheetObjectGraph *graph = SHEET_OBJECT_GRAPH (so);

#ifdef NEW_GRAPHS
	if (graph->graph != NULL) {
		GSList *ptr = go_graph_get_data (graph->graph);
		for (; ptr != NULL ; ptr = ptr->next)
			sog_data_set_sheet (graph, ptr->data, sheet);
	}
#endif

	return FALSE;
}

static void
sheet_object_graph_default_size (SheetObject const *so, double *w, double *h)
{
#ifdef NEW_GRAPHS
	g_object_get (G_OBJECT (SHEET_OBJECT_GRAPH (so)->graph),
		"width_pts", w,
		"height_pts", h,
		NULL);
#else
	*w = 200;
	*h = 200;
#endif
}

static void
sheet_object_graph_class_init (GObjectClass *klass)
{
	SheetObjectClass	*so_class  = SHEET_OBJECT_CLASS (klass);

	parent_klass = g_type_class_peek_parent (klass);

	/* Object class method overrides */
	klass->finalize = sheet_object_graph_finalize;

	/* SheetObject class method overrides */
	so_class->new_view	= sheet_object_graph_new_view;
	so_class->update_view_bounds = sheet_object_graph_update_bounds;
	so_class->read_xml	= sheet_object_graph_read_xml;
	so_class->write_xml	= sheet_object_graph_write_xml;
	so_class->clone         = sheet_object_graph_clone;
	so_class->user_config   = sheet_object_graph_user_config;
	so_class->assign_to_sheet = sheet_object_graph_set_sheet;
	so_class->print         = sheet_object_graph_print;
	so_class->rubber_band_directly = FALSE;
	so_class->default_size  = sheet_object_graph_default_size;
}

static void
sheet_object_graph_init (GObject *obj)
{
	SheetObjectGraph *graph;
	SheetObject *so;

	graph = SHEET_OBJECT_GRAPH (obj);

	so = SHEET_OBJECT (obj);
	so->anchor.direction = SO_DIR_DOWN_RIGHT;
}

GSF_CLASS (SheetObjectGraph, sheet_object_graph,
	   sheet_object_graph_class_init, sheet_object_graph_init,
	   SHEET_OBJECT_TYPE);

