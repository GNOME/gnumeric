/*
 * layout.c: Implements the layout for the graphics.
 * It keeps track of the graphic, axis, background, etc.
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
 */
#include <config.h>
#include <bonobo.h>
#include "idl/Graph.h"
#include "layout.h"
#include "layout-view.h"

 static ObjDim default_graph_dim = { 0.7, 0.7, 0.15, 0.15 };
/* static ObjDim default_graph_dim = { 1.0, 1.0, 0, 0 }; */

static BonoboEmbeddableClass *layout_parent_class;

/* The entry point vectors for the server we provide */
POA_GNOME_Graph_Layout__epv  layout_epv;
POA_GNOME_Graph_Layout__vepv layout_vepv;

#define layout_from_servant(x) LAYOUT (bonobo_object_from_servant (x))

static void
layout_destroy (GtkObject *object)
{
	Layout *layout = LAYOUT (object);

	if (layout->graph)
		bonobo_object_unref (BONOBO_OBJECT (layout->graph));
	
	GTK_OBJECT_CLASS (layout_parent_class)->destroy (object);
}

static GNOME_Graph_Chart 
impl_get_chart (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Layout *layout = layout_from_servant (servant);
	BonoboObject *graph = BONOBO_OBJECT (layout->graph);
	GNOME_Graph_Chart graph_corba;

	bonobo_object_ref (graph);
	graph_corba = bonobo_object_corba_objref (graph);
	
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
	g_free (layout->vectors);
	layout->vectors = NULL;
	layout->n_series = 0;
}

static void
vector_data_changed (GraphVector *gc, CORBA_short low, CORBA_short high, void *data)
{
	Layout *layout = data;

	graph_update (layout->graph, DIRTY_DATA);
}

static void
impl_add_series (PortableServer_Servant servant, const GNOME_Gnumeric_Vector vector,
		 const CORBA_char *series_name, CORBA_Environment *ev)
{
	Layout *layout = layout_from_servant (servant);
	GraphVector **v;
	GraphVector *gv;
	
	v = g_renew (GraphVector *, layout->vectors, layout->n_series+1);
	if (v == NULL){
		CORBA_exception_set_system (ev, ex_CORBA_NO_MEMORY, CORBA_COMPLETED_NO);
		return;
	}

	layout->vectors = v;

	gv = graph_vector_new (vector, vector_data_changed, layout, 0);
	layout->vectors [layout->n_series] = gv;
	layout->n_series++;
	
	GNOME_Gnumeric_Vector_set_notify (vector, gv->corba_object_reference, ev);

	graph_update (layout->graph, DIRTY_DATA);
}

static void
init_layout_corba_class (void)
{
	layout_epv.get_chart        = &impl_get_chart;
	layout_epv.get_axis         = &impl_get_axis;
	layout_epv.reset_series     = &impl_reset_series;
	layout_epv.add_series       = &impl_add_series;
	
	layout_vepv.Bonobo_Unknown_epv = bonobo_object_get_epv ();
	layout_vepv.Bonobo_Embeddable_epv = bonobo_embeddable_get_epv ();
	layout_vepv.GNOME_Graph_Layout_epv = &layout_epv;
}

static void
layout_class_init (GtkObjectClass *object_class)
{
	layout_parent_class = gtk_type_class (bonobo_embeddable_get_type ());
	init_layout_corba_class ();

	object_class->destroy = layout_destroy;
}

static void
layout_init (GtkObject *object)
{
}

static BonoboView *
layout_view_factory (BonoboEmbeddable *embeddable, const Bonobo_ViewFrame view_frame, void *data)
{
	return BONOBO_VIEW (layout_view_new (LAYOUT (embeddable)));
}

static GNOME_Graph_Layout
layout_corba_object_create (BonoboObject *layout)
{
	POA_GNOME_Graph_Layout *servant;
	CORBA_Environment ev;
	
	servant = (POA_GNOME_Graph_Layout *) g_new0 (BonoboObjectServant, 1);
	servant->vepv = &layout_vepv;

	CORBA_exception_init (&ev);

	POA_GNOME_Graph_Layout__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION){
		g_free (servant);
		CORBA_exception_free (&ev);
		return CORBA_OBJECT_NIL;
	}
	CORBA_exception_free (&ev);

	return bonobo_object_activate_servant (layout, servant);
}

Layout *
layout_new (void)
{
	Layout *layout;
	GNOME_Graph_Layout corba_layout;
	
	layout = gtk_type_new (LAYOUT_TYPE);

	corba_layout = layout_corba_object_create (BONOBO_OBJECT (layout));
	if (corba_layout == CORBA_OBJECT_NIL){
		gtk_object_destroy (GTK_OBJECT (layout));
		return NULL;
	}
	bonobo_embeddable_construct (BONOBO_EMBEDDABLE (layout),
				    (Bonobo_Embeddable) corba_layout,
				    layout_view_factory, NULL);

	layout->graph = graph_new (layout);
	layout->graph_dim = default_graph_dim;
		
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

		type = gtk_type_unique (bonobo_embeddable_get_type (), &info);
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

void
layout_dim_bbox (ArtIRect *bbox, int x, int y, int width, int height, ObjDim *dim)
{
	bbox->x0 = x + (width * dim->x_pos);
	bbox->y0 = y + (height * dim->y_pos);
	bbox->x1 = bbox->x0 + (width * dim->x_size);
	bbox->y1 = bbox->y0 + (height * dim->y_size);
}

		
