#ifndef GRAPH_GRAPH_VIEW_H_
#define GRAPH_GRAPH_VIEW_H_

#include <libgnome/gnome-defs.h>
#include <libgnomeui/gnome-canvas.h>

#define GRAPH_VIEW_TYPE        (graph_view_get_type ())
#define GRAPH_VIEW(o)          (GTK_CHECK_CAST ((o), GRAPH_VIEW_TYPE, GraphView))
#define GRAPH_VIEW_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), GRAPH_VIEW_TYPE, GraphViewClass))
#define IS_GRAPH_VIEW(o)       (GTK_CHECK_TYPE ((o), GRAPH_VIEW_TYPE))
#define IS_GRAPH_VIEW_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), GRAPH_VIEW_TYPE))

struct _GraphView {
	GnomeCanvasItem base;

	/*
	 * Display control
	 */
	int       dirty_flags;
	int       frozen;

	/*
	 * Bounding box for the graph
	 */
	ArtIRect  bbox;

	/*
	 * Affine transform that applies to graph now
	 */
	double    affine [6];
	
	/*
	 * The Graphic repository
	 */
	Graph    *graph;

	GdkGC    *fill_gc, *outline_gc;
};

typedef struct {
	GnomeCanvasItemClass parent_class;
} GraphViewClass;

GtkType graph_view_get_type  (void);
void    graph_view_update    (GraphView *graph_view, int dirty_flags);
void    graph_view_set_graph (GraphView *graph_view, Graph *graph);
void    graph_view_set_bbox  (GraphView *graph_view, ArtIRect *bbox);

#endif /* GRAPH_GRAPH_VIEW_H_ */
