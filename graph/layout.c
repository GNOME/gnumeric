/*
 * layout.c: Implements the layout for the graphics.
 * It keeps track of the graphic, axis, background, etc.
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999 International GNOME Support
 */
#include <config.h>
#include <bonobo/gnome-bonobo.h>
#include "layout.h"
#include "layout-view.h"

static GnomeEmbeddableClass *layout_parent_class;

/* The entry point vectors for the server we provide */
POA_GNOME_Graph_Layout__epv  layout_epv;
POA_GNOME_Graph_Layout__vepv layout_vepv;

#define layout_from_servant(x) LAYOUT (gnome_object_from_servant (x))

static void
layout_destroy (GtkObject *object)
{
	Layout *layout = LAYOUT (object);

	if (layout->graph)
		gnome_object_unref (GNOME_OBJECT (layout->graph));
	
	GTK_OBJECT_CLASS (layout_parent_class)->destroy (object);
}

static GNOME_Graph_Chart 
impl_get_chart (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Layout *layout = layout_from_servant (servant);
	GnomeObject *graph = GNOME_OBJECT (layout->graph);
	GNOME_Graph_Chart graph_corba;

	gnome_object_ref (graph);
	graph_corba = gnome_object_corba_objref (graph);
	
	return CORBA_Object_duplicate (graph_corba, ev);
}

static GNOME_Graph_Axis
impl_get_axis (PortableServer_Servant servant, GNOME_Graph_AxisType axis, CORBA_Environment *ev)
{
	g_error ("Implement me");
	
	return CORBA_OBJECT_NIL;
}

static void
impl_reset_series (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Layout *layout = layout_from_servant (servant);
	const int n = layout->n_series;
	int i;
	
	for (i = 0; i < n; i++)
		graph_vector_destroy (layout->vectors [i]);
	g_free (graph->vectors);
	layout->vectors = NULL;
	layout->n_series = 0;
}

static void
impl_add_series (PortableServer_Servant servant, GNOME_Gnumeric_Vector vector, CORBA_Environment *ev)
{
	Layout *layout = layout_from_servant (servant);
	GraphVector **v;
	
	v = g_renew (GraphVector *, layout->vectors, layout->n_series+1);
	if (v == NULL){
		CORBA_exception_set_system (ev, ex_CORBA_NO_MEMORY, CORBA_COMPLETED_NO);
		return;
	}

	layout->vectors = v;
	
	layout->vectors [layout->n_series] = graph_vector_new (vector, NULL, NULL, 0);
	layout->n_series++;
}

static void
init_layout_corba_class (void)
{
	layout_epv.get_chart    = &impl_get_chart;
	layout_epv.get_axis     = &impl_get_axis;
	layout_epv.reset_series = &impl_reset_series;
	layout_epv.add_series   = &impl_add_series;

	layout_vepv.GNOME_Unknown_epv = &gnome_object_epv;
	layout_vepv.GNOME_Embeddable_epv = &gnome_embeddable_epv;
	layout_vepv.GNOME_Graph_Layout_epv = &layout_epv;
}

static void
layout_class_init (GtkObjectClass *object_class)
{
	layout_parent_class = gtk_type_class (gnome_object_get_type ());
	init_layout_corba_class ();

	object_class->destroy = layout_destroy;
}

static void
layout_init (GtkObject *object)
{
}

static GnomeView *
layout_view_factory (GnomeEmbeddable *embeddable, const GNOME_ViewFrame view_frame, void *data)
{
	return GNOME_VIEW (layout_view_new (LAYOUT (embeddable)));
}

static GNOME_Graph_Layout
layout_corba_object_create (GnomeObject *layout)
{
	POA_GNOME_Graph_Layout *servant;
	CORBA_Environment ev;
	
	servant = (POA_GNOME_Graph_Layout *) g_new0 (GnomeObjectServant, 1);
	servant->vepv = &layout_vepv;

	CORBA_exception_init (&ev);

	POA_GNOME_Graph_Layout__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION){
		g_free (servant);
		CORBA_exception_free (&ev);
		return CORBA_OBJECT_NIL;
	}
	CORBA_exception_free (&ev);

	return gnome_object_activate_servant (layout, servant);
}

Layout *
layout_new (void)
{
	Layout *layout;
	GNOME_Graph_Layout corba_layout;
	
	layout = gtk_type_new (LAYOUT_TYPE);

	corba_layout = layout_corba_object_create (GNOME_OBJECT (layout));
	if (corba_layout == CORBA_OBJECT_NIL){
		gtk_object_destroy (GTK_OBJECT (layout));
		return NULL;
	}
	gnome_embeddable_construct (GNOME_EMBEDDABLE (layout),
				    (GNOME_Embeddable) corba_layout,
				    layout_view_factory, NULL);
	layout->graph = graph_new ();
	
	return layout;
}

GtkType
layout_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"GraphLayoutGnumeric",
			sizeof (Layout),
			sizeof (LayoutClass),
			(GtkClassInitFunc) layout_class_init,
			(GtkObjectInitFunc) layout_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gnome_embeddable_get_type (), &info);
	}

	return type;
}

Graph *
layout_get_graph (Layout *layout)
{
	g_return_val_if_fail (layout != NULL, NULL);
	g_return_val_if_fail (IS_LAYOUT (layout), NULL);

	return layout->graph;
}
