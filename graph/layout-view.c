/*
 * layout-view.c: A view of the layout.  It is a canvas object that
 * contains the other pieces of the view
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999 International GNOME Support
 */
#include <config.h>
#include "Graph.h"
#include "layout-view.h"
#include "graph-view.h"

static GnomeCanvasClass *layout_view_parent_class;

static void
layout_view_destroy (GtkObject *object)
{
	GTK_OBJECT_CLASS (layout_view_parent_class)->destroy (object);
}

GtkWidget *
layout_view_new (Layout *layout)
{
	LayoutView *layout_view;
	
	layout_view = gtk_type_new (LAYOUT_VIEW_TYPE);

	layout_view->graph_view = GRAPH_VIEW (gnome_canvas_item_new (
		gnome_canvas_root (GNOME_CANVAS (layout_view)),
		graph_view_get_type (),
		NULL));

	graph_view_set_graph (layout_view->graph_view, layout->graph);
	
	return GTK_WIDGET (layout);
}

/*
 * For now we just cover everything
 */
static void
layout_view_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	LayoutView *layout_view = LAYOUT_VIEW (widget);
	ArtIRect bbox;

	bbox.x0 = allocation->x;
	bbox.y0 = allocation->y;
	bbox.x1 = allocation->width;
	bbox.y1 = allocation->height;
	
	graph_view_set_bbox (layout_view->graph_view, &bbox);
}

static void
layout_view_class_init (GtkObjectClass *object_class)
{
	GtkWidgetClass *widget_class = (GtkWidgetClass *) object_class;
	
	object_class->destroy = layout_view_destroy;
	widget_class->size_allocate = layout_view_size_allocate;
}

static void
layout_view_init (GtkObject *object)
{
}

GtkType
layout_view_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"GraphLayoutViewGnumeric",
			sizeof (LayoutView),
			sizeof (LayoutViewClass),
			(GtkClassInitFunc) layout_view_class_init,
			(GtkObjectInitFunc) layout_view_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gnome_canvas_get_type (), &info);
	}

	return type;
}
