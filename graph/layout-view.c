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

static GnomeViewClass *layout_view_parent_class;

static void
layout_view_destroy (GtkObject *object)
{
	GTK_OBJECT_CLASS (layout_view_parent_class)->destroy (object);
}

/*
 * For now we just cover everything
 */
static void
layout_view_size_allocate (GtkWidget *widget, GtkAllocation *allocation, LayoutView *layout_view)
{
	ArtIRect bbox;

	bbox.x0 = allocation->x;
	bbox.y0 = allocation->y;
	bbox.x1 = allocation->width;
	bbox.y1 = allocation->height;
	
	graph_view_set_bbox (layout_view->graph_view, &bbox);
}

GnomeView *
layout_view_new (Layout *layout)
{
	LayoutView *layout_view;
	GNOME_View corba_layout_view;
	
	layout_view = gtk_type_new (LAYOUT_VIEW_TYPE);

	corba_layout_view = gnome_view_corba_object_create (GNOME_OBJECT (layout_view));
	if (corba_layout_view == CORBA_OBJECT_NIL){
		gtk_object_destroy (GTK_OBJECT (corba_layout_view));
		return NULL;
	}
	
	layout_view->canvas = GNOME_CANVAS (gnome_canvas_new ());
	gtk_widget_show (GTK_WIDGET (layout_view->canvas));

	gnome_view_construct (GNOME_VIEW (layout_view), corba_layout_view, GTK_WIDGET (layout_view->canvas));
	layout_view->graph_view = GRAPH_VIEW (gnome_canvas_item_new (
		gnome_canvas_root (layout_view->canvas),
		graph_view_get_type (),
		NULL));

	graph_view_set_graph (layout_view->graph_view, layout->graph);

	gtk_signal_connect (GTK_OBJECT (layout_view->canvas), "size_allocate",
			    GTK_SIGNAL_FUNC (layout_view_size_allocate), layout_view);

	return GNOME_VIEW (layout_view);
}

static void
layout_view_class_init (GtkObjectClass *object_class)
{
	object_class->destroy = layout_view_destroy;

	layout_view_parent_class = gtk_type_class (gnome_view_get_type ());
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

		type = gtk_type_unique (gnome_view_get_type (), &info);
	}

	return type;
}
