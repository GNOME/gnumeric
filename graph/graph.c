/*
 * graph.c: Implements the core of the graphics engine
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
 */
#include <config.h>
#include <gtk/gtksignal.h>
#include "graph.h"
#include "graph-vector.h"
#include "graph-view.h"

static BonoboObjectClass *graph_parent_class;

/* The entry point vectors for the server we provide */
POA_GNOME_Graph_Chart__epv  graph_epv;
POA_GNOME_Graph_Chart__vepv graph_vepv;

#define graph_from_servant(x) GRAPH (bonobo_object_from_servant (x))

static void
graph_destroy (GtkObject *object)
{
	GTK_OBJECT_CLASS (graph_parent_class)->destroy (object);
}

static GNOME_Graph_ChartType
impl_graph_get_chart_type (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	return graph->chart_type;
}

static void
graph_compute_divisions (Graph *graph)
{
	const int n = graph->layout->n_series;
	int i;
	int len = 0;

	/* Compute max lenght */
	for (i = 0; i < n; i++){
		int l;

		l = graph_vector_count (graph->layout->vectors [i]);

		if (l > len)
			len = l;
	}
	if (len == 0)
		len = 1;
	
	graph->divisions = len;
}

static void
graph_set_low_high (Graph *graph, double low, double high)
{
	graph->low = low;
	graph->high = high;
	graph->real_low = low;
	graph->real_high = high;

	graph->y_size = graph->high - graph->low;
}

static void
graph_set_scatter_x_low_high (Graph *graph, double low, double high)
{
	graph->x_low = low;
	graph->x_high = high;
	graph->x_real_low = low;
	graph->x_real_high = high;

	graph->x_size = graph->x_high - graph->x_low;
}

static void
graph_compute_dimensions (Graph *graph, int start_series)
{
	const int n = graph->layout->n_series;
	double low = 0.0, high = 0.0;
	int i;

	graph_compute_divisions (graph);
	
	for (i = start_series; i < n; i++){
		double l, h;
		
		graph_vector_low_high (graph->layout->vectors [i], &l, &h);
		if (l < low)
			low = l;
		if (h > high)
			high = h;
	}
	graph_set_low_high (graph, low, high);
}

static void
graph_compute_stacked_dimensions (Graph *graph)
{
	const int n = graph->layout->n_series;
	int len, x;
	double high, low;

	graph_compute_divisions (graph);
	high = low = 0.0;

	len = graph->divisions;
	
	for (x = 0; x < len; x++){
		double s_high;
		double s_low;
		int i;
		
		s_high = s_low = 0.0;
		
		for (i = 0; i < n; i++){
			double v;
			
			v = graph_vector_get_double (graph->layout->vectors [i], x);

			if (v < 0)
				s_low += v;
			else
				s_high += v;
		}
		if (s_low < low)
			low = s_low;
		if (s_high > high)
			high = s_high;
	}
	graph_set_low_high (graph, low, high);
}

static void
graph_compute_scatter_dimensions (Graph *graph)
{
	GraphVector *x_vector;
	double vmin = 0.0, vmax = 0.0;
	gboolean boundaries_set;
	int count, xi;
	
	x_vector = graph->layout->vectors [0];
	g_assert (x_vector != NULL);
	
	count = graph_vector_count (x_vector);
	boundaries_set = FALSE;
	for (xi = 0; xi < count; xi++){
		double val;
		
		val = graph_vector_get_double (x_vector, xi);

		if (!boundaries_set){
			boundaries_set = TRUE;
			vmin = val;
			vmax = val;
			continue;
		}
		
		if (val < vmin)
			vmin = val;
		if (val > vmax)
			vmax = val;
	}
	graph_set_scatter_x_low_high (graph, vmin, vmax);
}

void
graph_update_dimensions (Graph *graph)
{
	switch (graph->chart_type){
	case GNOME_Graph_CHART_TYPE_CLUSTERED:
		graph_compute_dimensions (graph, 0);
		break;
		
	case GNOME_Graph_CHART_TYPE_STACKED:
		graph_compute_stacked_dimensions (graph);
		break;
		
	case GNOME_Graph_CHART_TYPE_STACKED_FULL:
		graph_compute_stacked_dimensions (graph);
		break;

	case GNOME_Graph_CHART_TYPE_SCATTER:
		graph_compute_scatter_dimensions (graph);
		graph_compute_dimensions (graph, 1);
		break;
		
	default:
		break;
	}
	
}	

static void
impl_graph_set_chart_type (PortableServer_Servant servant,
			   GNOME_Graph_ChartType value,
			   CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	if (graph->chart_type == value)
		return;
	
	graph->chart_type = value;

	graph_update_dimensions (graph);
	graph_update (graph, DIRTY_TYPE | DIRTY_SHAPE);
}

static GNOME_Graph_PlotMode
impl_graph_get_plot_mode (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	return graph->plot_mode;
}

static void
impl_graph_set_plot_mode (PortableServer_Servant servant,
			  GNOME_Graph_PlotMode value,
			  CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	if (graph->plot_mode != value){
		graph->plot_mode = value;
		graph_update (graph, DIRTY_SHAPE | DIRTY_TYPE);
	}
}

static GNOME_Graph_ColBarMode
impl_graph_get_col_bar_mode (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	return graph->col_bar_mode;
}

static void
impl_graph_set_col_bar_mode (PortableServer_Servant servant,
			     GNOME_Graph_ColBarMode value,
			     CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	if (graph->col_bar_mode != value){
		graph->col_bar_mode = value;
		graph_update (graph, DIRTY_SHAPE);
	}
}

static GNOME_Graph_DirMode
impl_graph_get_direction (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	return graph->direction;
}

static void
impl_graph_set_direction (PortableServer_Servant servant,
			  GNOME_Graph_DirMode value,
			  CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	if (graph->direction != value){
		graph->direction = value;
		graph_update (graph, DIRTY_SHAPE);
	}
}

static GNOME_Graph_LineMode
impl_graph_get_line_mode (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	return graph->line_mode;
}

static void
impl_graph_set_line_mode (PortableServer_Servant servant,
			  GNOME_Graph_LineMode value,
			  CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	if (graph->line_mode != value){
		graph->line_mode = value;
		graph_update (graph, DIRTY_SHAPE);
	}
}

static GNOME_Graph_PieMode
impl_graph_get_pie_mode (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	return graph->pie_mode;
}

static void
impl_graph_set_pie_mode (PortableServer_Servant servant,
			 GNOME_Graph_PieMode value,
			 CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	if (graph->pie_mode != value){
		graph->pie_mode = value;
		graph_update (graph, DIRTY_SHAPE);
	}
}

static GNOME_Graph_PieDimension
impl_graph_get_pie_dim (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	return graph->pie_dim;
}

static void
impl_graph_set_pie_dim (PortableServer_Servant servant,
			GNOME_Graph_PieDimension value,
			CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	if (graph->pie_dim != value){
		graph->pie_dim = value;
		graph_update (graph, DIRTY_SHAPE);
	}
}

static GNOME_Graph_ScatterPoints
impl_graph_get_scatter_mode (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	return graph->scatter_mode;
}

static void
impl_graph_set_scatter_mode (PortableServer_Servant servant,
			     GNOME_Graph_ScatterPoints value,
			     CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	if (graph->scatter_mode != value){
		graph->scatter_mode = value;
		graph_update (graph, DIRTY_SHAPE);
	}
}

static GNOME_Graph_ScatterConn
impl_graph_get_scatter_conn (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	return graph->scatter_conn;
}

static void
impl_graph_set_scatter_conn (PortableServer_Servant servant,
			     GNOME_Graph_ScatterConn value,
			     CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	if (graph->scatter_conn != value){
		graph->scatter_conn = value;
		graph_update (graph, DIRTY_SHAPE);
	}
}

static GNOME_Graph_SurfaceMode
impl_graph_get_surface_mode (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	return graph->surface_mode;
}

static void
impl_graph_set_surface_mode (PortableServer_Servant servant, GNOME_Graph_SurfaceMode value, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	if (graph->surface_mode != value){
		graph->surface_mode = value;
		graph_update (graph, DIRTY_SHAPE);
	}
}

static void
impl_graph_freeze (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	graph->frozen++;
}

static void
impl_graph_thaw (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	graph->frozen--;
	if ((graph->frozen == 0) && (graph->dirty_flags != 0))
		graph_update (graph, 0);
}

static CORBA_boolean
impl_get_with_labels (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	return (graph->first == 1);
}

static void
impl_set_with_labels (PortableServer_Servant servant, CORBA_boolean with_labels, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	graph->first = with_labels ? 1 : 0;

	graph_update (graph, DIRTY_SHAPE);
}

static void
init_graph_corba_class (void)
{
	graph_epv._get_chart_type = &impl_graph_get_chart_type;
	graph_epv._set_chart_type = &impl_graph_set_chart_type;
	graph_epv._get_plot_mode = &impl_graph_get_plot_mode;
	graph_epv._set_plot_mode = &impl_graph_set_plot_mode;
	graph_epv._get_col_bar_mode = &impl_graph_get_col_bar_mode;
	graph_epv._set_col_bar_mode = &impl_graph_set_col_bar_mode;
	graph_epv._get_direction = &impl_graph_get_direction;
	graph_epv._set_direction = &impl_graph_set_direction;
	graph_epv._get_line_mode = &impl_graph_get_line_mode;
	graph_epv._set_line_mode = &impl_graph_set_line_mode;
	graph_epv._get_pie_mode = &impl_graph_get_pie_mode;
	graph_epv._set_pie_mode = &impl_graph_set_pie_mode;
	graph_epv._get_pie_dim = &impl_graph_get_pie_dim;
	graph_epv._set_pie_dim = &impl_graph_set_pie_dim;
	graph_epv._get_scatter_mode = &impl_graph_get_scatter_mode;
	graph_epv._set_scatter_mode = &impl_graph_set_scatter_mode;
	graph_epv._get_scatter_conn = &impl_graph_get_scatter_conn;
	graph_epv._set_scatter_conn = &impl_graph_set_scatter_conn;
	graph_epv._get_surface_mode = &impl_graph_get_surface_mode;
	graph_epv._set_surface_mode = &impl_graph_set_surface_mode;
	graph_epv._get_with_labels = &impl_get_with_labels;
	graph_epv._set_with_labels = &impl_set_with_labels;

	graph_epv.freeze = &impl_graph_freeze;
	graph_epv.thaw   = &impl_graph_thaw;

	/*
	 * The Vepv
	 */
	graph_vepv.Bonobo_Unknown_epv = bonobo_object_get_epv ();
	graph_vepv.GNOME_Graph_Chart_epv = &graph_epv;
}

static void
graph_class_init (GtkObjectClass *object_class)
{
	graph_parent_class = gtk_type_class (bonobo_object_get_type ());
	
	object_class->destroy = graph_destroy;
	
	init_graph_corba_class ();
}

static void
graph_init (GtkObject *object)
{
	Graph *graph = GRAPH (object);
	
	graph->dirty_flags = 0;
}

GtkType
graph_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"GraphGnumeric",
			sizeof (Graph),
			sizeof (GraphClass),
			(GtkClassInitFunc) graph_class_init,
			(GtkObjectInitFunc) graph_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (bonobo_object_get_type (), &info);
	}

	return type;
}

static void
graph_view_destroyed (GraphView *graph_view, Graph *graph)
{
	graph->views = g_slist_remove (graph->views, graph_view);
}

void
graph_bind_view (Graph *graph, GraphView *graph_view)
{
	g_return_if_fail (graph != NULL);
	g_return_if_fail (IS_GRAPH (graph));
	g_return_if_fail (graph_view != NULL);
	g_return_if_fail (IS_GRAPH_VIEW (graph_view));

	graph_view_set_graph (graph_view, graph);
	graph->views = g_slist_prepend (graph->views, graph_view);

	gtk_signal_connect (GTK_OBJECT (graph_view), "destroy",
			    GTK_SIGNAL_FUNC (graph_view_destroyed), graph);
}

GNOME_Graph_Chart
graph_corba_object_create (BonoboObject *object)
{
	POA_GNOME_Graph_Chart *servant;
	CORBA_Environment ev;
	
	servant = (POA_GNOME_Graph_Chart *) g_new0 (BonoboObjectServant, 1);
	servant->vepv = &graph_vepv;

	CORBA_exception_init (&ev);
	POA_GNOME_Graph_Chart__init ((PortableServer_Servant) servant, &ev);
	if (ev._major != CORBA_NO_EXCEPTION){
		g_free (servant);
		CORBA_exception_free (&ev);
		return CORBA_OBJECT_NIL;
	}

	CORBA_exception_free (&ev);
	return (Bonobo_View) bonobo_object_activate_servant (object, servant);
}

Graph *
graph_new (Layout *layout)
{
	Graph *graph;
	GNOME_Graph_Chart graph_corba;
		
	graph = gtk_type_new (graph_get_type ());

	graph_corba = graph_corba_object_create (BONOBO_OBJECT (graph));
	if (graph_corba == CORBA_OBJECT_NIL){
		gtk_object_destroy (GTK_OBJECT (graph));
		return NULL;
	}
	bonobo_object_construct (BONOBO_OBJECT (graph), graph_corba);
	
	graph->layout = layout;

	return graph;
}

void
graph_update (Graph *graph, int dirty_flags)
{
	GSList *l;

	graph->dirty_flags |= dirty_flags;

	if (graph->frozen)
		return;

	if (graph->dirty_flags & DIRTY_DATA)
		graph_update_dimensions (graph);
		
	for (l = graph->views; l; l = l->next){
		GraphView *graph_view = GRAPH_VIEW (l->data);

		graph_view_update (graph_view, graph->dirty_flags);
	}
	graph->dirty_flags = 0;
}

