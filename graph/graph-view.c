/*
 * Graph Item, the view of the Graph model.
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999 International GNOME Support (http://www.gnome-support.com).
 */
#include <config.h>
#include <math.h>
#include <libgnomeui/gnome-canvas.h>
#include <libgnomeui/gnome-canvas-util.h>
#include "graph.h"
#include "graph-view.h"
#include "graph-view-colbar.h"
#include "graph-view-plot.h"
#include "graph-view-scatter.h"
#include "graph-view-stock.h"

enum {
	ARG_BBOX,
	ARG_GRAPH,
};

static GnomeCanvasItemClass *graph_view_parent_class;
static GraphViewClass *graph_view_class;

static void
graph_view_destroy (GtkObject *object)
{
	GTK_OBJECT_CLASS (graph_view_parent_class)->destroy (object);
}

static void
graph_view_canvas_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	GraphView *graph_view = GRAPH_VIEW (item);
	int i;
	
	if (graph_view_parent_class->update)
		(*graph_view_parent_class->update)(item, affine, clip_path, flags);

	for (i = 0; i < 6; i++)
		graph_view->affine [i] = affine [i];
}

static void
graph_view_realize (GnomeCanvasItem *item)
{
	GraphView *graph_view;
	GdkWindow *window;
	
	if (graph_view_parent_class->realize)
		(*graph_view_parent_class->realize)(item);

	graph_view = GRAPH_VIEW (item);

	window = GTK_WIDGET (item->canvas)->window;
	graph_view->outline_gc = gdk_gc_new (window);
	graph_view->fill_gc = gdk_gc_new (window);
}

static void
graph_view_unrealize (GnomeCanvasItem *item)
{
	if (GNOME_CANVAS_ITEM_CLASS (graph_view_parent_class)->unrealize)
		(*graph_view_parent_class->unrealize)(item);
}

static void
graph_view_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	GraphView *graph_view = GRAPH_VIEW (item);
	Graph *graph = graph_view->graph;

	switch (graph->chart_type){
	case GNOME_Graph_CHART_TYPE_CLUSTERED:
	case GNOME_Graph_CHART_TYPE_STACKED:
	case GNOME_Graph_CHART_TYPE_STACKED_FULL:
		if (graph->plot_mode == GNOME_Graph_PLOT_COLBAR){
			graph_view_colbar_draw (
				graph_view, drawable,
				x, y, width, height);
			return;
		}
		graph_view_plot (graph_view, drawable, x, y, width, height);
		break;

	case GNOME_Graph_CHART_TYPE_BUBBLES:
		g_error ("Bubbles not implemented yet");
		break;

	case GNOME_Graph_CHART_TYPE_SCATTER:
		graph_view_scatter_plot (graph_view, drawable, x, y, width, height);
		break;

	case GNOME_Graph_CHART_TYPE_SURFACE_2D:
	case GNOME_Graph_CHART_TYPE_SURFACE_3D:
		g_error ("Surface 2D/3D not implemented yet");
		break;

	case GNOME_Graph_CHART_TYPE_3D:
		g_error ("3D charts not implemented yet");
		break;

	case GNOME_Graph_CHART_TYPE_PIE:
		g_error ("Pie charts not yet implemented");
		break;
		
	case GNOME_Graph_CHART_TYPE_STOCK_HIGH_LOW_CLOSE:
	case GNOME_Graph_CHART_TYPE_STOCK_OPEN_HIGH_LOW_CLOSE:
	case GNOME_Graph_CHART_TYPE_STOCK_VOL_HIGH_LOW_CLOSE:
	case GNOME_Graph_CHART_TYPE_STOCK_VOL_OPEN_HIGH_LOW_CLOSE:
		graph_view_stock_plot (graph_view, drawable,
				       x, y, width, height);
		break;
	}
}

static double
graph_view_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		  GnomeCanvasItem **actual_item)
{
	/* For now we are always inside the thing. */
	*actual_item = item;
	return 0.0;
}

static gint
graph_view_event (GnomeCanvasItem *item, GdkEvent *event)
{
	return 0;
}

static void
graph_view_class_init (GtkObjectClass *object_class)
{
	GnomeCanvasItemClass *item_class = (GnomeCanvasItemClass *) object_class;
	
	graph_view_parent_class = gtk_type_class (gnome_canvas_item_get_type ());
	graph_view_class = gtk_type_class (graph_view_get_type ());
	
	/*
	 * Method overrides
	 */
	object_class->destroy = graph_view_destroy;

	item_class->update    = graph_view_canvas_update;
	item_class->realize   = graph_view_realize;
	item_class->unrealize = graph_view_unrealize;
	item_class->draw      = graph_view_draw;
	item_class->point     = graph_view_point;
	item_class->event     = graph_view_event;
}

void
graph_view_update (GraphView *graph_view, int dirty_flags)
{
	g_return_if_fail (graph_view != NULL);
	g_return_if_fail (IS_GRAPH_VIEW (graph_view));

	graph_view->dirty_flags |= dirty_flags;
	if (graph_view->frozen)
		return;

	if (graph_view->dirty_flags & DIRTY_TYPE){
		graph_view->dirty_flags &= ~DIRTY_TYPE;
	}
	
	if (graph_view->dirty_flags & (DIRTY_BBOX|DIRTY_SHAPE)){
		graph_view->dirty_flags &= ~(DIRTY_BBOX | DIRTY_SHAPE);
	}
}

void
graph_view_freeze (GraphView *graph_view)
{
	g_return_if_fail (graph_view != NULL);
	g_return_if_fail (IS_GRAPH_VIEW (graph_view));
	
	graph_view->frozen++;
}

void
graph_view_thaw (GraphView *graph_view)
{
	g_return_if_fail (graph_view != NULL);
	g_return_if_fail (IS_GRAPH_VIEW (graph_view));
	
	graph_view->frozen--;
	if (graph_view->frozen != 0)
		return;

	if (graph_view->dirty_flags)
		graph_view_update (graph_view, graph_view->dirty_flags);
}

void
graph_view_set_bbox (GraphView *graph_view, ArtIRect *bbox)
{
	g_return_if_fail (graph_view != NULL);
	g_return_if_fail (IS_GRAPH_VIEW (graph_view));
	g_return_if_fail (bbox != NULL);

	if ((graph_view->bbox.x0 == bbox->x0) &&
	    (graph_view->bbox.x1 == bbox->x1) &&
	    (graph_view->bbox.y0 == bbox->y0) &&
	    (graph_view->bbox.y1 == bbox->y1))
		return;
	
	graph_view->bbox = *bbox;
	graph_view_update (graph_view, DIRTY_BBOX);
	gnome_canvas_update_bbox (
		GNOME_CANVAS_ITEM (graph_view),
		bbox->x0, bbox->y0,
		bbox->x1, bbox->y1);
}

static void
graph_view_init (GtkObject *object)
{
	GraphView *graph_view = GRAPH_VIEW (object);

	graph_view->bbox.x0 = 0;
	graph_view->bbox.y0 = 0;
	graph_view->bbox.x1 = 1;
	graph_view->bbox.y1 = 1;
	
	graph_view->frozen = 1;
	graph_view->dirty_flags = DIRTY_ALL;
}

void
graph_view_set_graph (GraphView *graph_view, Graph *graph)
{
	g_return_if_fail (graph_view != NULL);
	g_return_if_fail (IS_GRAPH_VIEW (graph_view));
	g_return_if_fail (graph != NULL);
	g_return_if_fail (IS_GRAPH (graph));

	graph_view->graph = graph;
	graph_view_thaw (graph_view);
}

GtkType
graph_view_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"GraphView",
			sizeof (GraphView),
			sizeof (GraphViewClass),
			(GtkClassInitFunc) graph_view_class_init,
			(GtkObjectInitFunc) graph_view_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gnome_canvas_item_get_type (), &info);
	}

	return type;
}
			     

