/*
 * Graph Item
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999 International GNOME Support (http://www.gnome-support.com).
 */
#include <config.h>
#include <libgnomeui/gnome-canvas.h>
#include "graph.h"

enum {
	ARG_BBOX,
	ARG_GRAPH,
};

static GnomeCanvasItemClass *graph_view_parent_class;
static GraphViewClass *graph_view_class;

static void
graph_view_destroy (GtkObject *object)
{
	GraphView *graph_view = GRAPH_VIEW (object);
	
}

static void
graph_view_canvas_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	if (graph_view_parent_class->update)
		(*graph_view_parent_class->update)(item, affine, clip_path, flags);

	for (i = 0; i < 6; i++)
		graph_view->affine [i] = affine [i];
}

static void
graph_view_realize (GnomeCanvasItem *item)
{
	GraphView *graph_view;
	GdkWindow  *window;
	
	if (graph_view_parent_class->realize)
		(*graph_view_parent_class->realize)(item);

	graph_view = GRAPH_VIEW (item);
}

static void
graph_view_unrealize (GnomeCanvasItem *item)
{
	GraphView *graph_view = GRAPH_VIEW (item);

	if (GNOME_CANVAS_ITEM_CLASS (graph_view_parent_class)->unrealize)
		(*graph_view_parent_class->unrealize)(item);
}

static void
graph_view_colbar_draw_nth (GraphView *graph_view, GdkDrawable *drawable, int n, int base_x, int base_y)
{
	
}

static void
graph_view_colbar_draw (GraphView *graph_view, GdkDrawable *drawable, int x, int y, int width, int height)
{
	int yl = graph_view->bbox.y1 - graph_view->bbox.y0;
	int xl = graph_view->bbox.x1 - graph_view->bbox.x0;
	int first, last, i;
	double units_per_slot;
	
	if (graph_view->graph->direction == GNOME_GRAPH_DIR_BAR){
		units_per_slot = yl / graph_view->graph->divisions;
		first = y / units_per_slot;
		last = (y + width) / units_per_slot;

		for (i = first; i <= last; i++){
			HERE IS STAYED
		}
		
	} else {
		units_per_slot = xl / graph_view->graph->divisions;
		first = x / units_per_slot;
		last = (x + width) / units_per_slot;

		for (i = first; i <= last; i++){
			int base = i * units_per_slot;
			
			graph_view_colbar_draw_nth (drawable, i, base_x - x, y);
		};
	}

}

static void
graph_view_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	GraphView *graph_view = GRAPH_VIEW (item);
	Graph *graph = graph_view->graph;

	if (graph->plot_mode == GNOME_Graph_PLOT_COLBAR){
		graph_view_colbar_draw (graph_view, drawable, x, y, width, height);
		return;
	}
}

static double
graph_view_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		  GnomeCanvasItem **actual_item)
{
}

static gint
item_cursor_event (GnomeCanvasItem *item, GdkEvent *event)
{
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
	item_class->translate = graph_view_translate;
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
	
	if (graph_view->dirty & (DIRTY_BBOX|DIRTY_SHAPE)){
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
	
	if (graph_view->bbox == *bbox)
		return;
	
	graph_view->bbox = *bbox;
	graph_view_update (graph_view);
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
	graph_view->dirty  = TRUE;
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
			     

