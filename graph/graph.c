/*
 * graph.c: Implements the core of the graphics engine
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999 International GNOME Support
 */
#include <config.h>
#include "graph.h"
#include "graph-view.h"

static GnomeObjectClass *graph_parent_class;

/* The entry point vectors for the server we provide */
POA_GNOME_Graph_Chart__epv  graph_epv;
POA_GNOME_Graph_Chart__vepv graph_vepv;

#define graph_from_servant(x) GRAPH (gnome_object_from_servant (x))

static void
graph_destroy (GtkObject *object)
{
	GTK_OBJECT_CLASS (graph_parent_class)->destroy (object);
}

static void
graph_update (Graph *graph, int dirty_flags)
{
	GSList *l;

	graph->dirty_flags |= dirty_flags;

	if (graph->frozen)
		return;

	for (l = graph->views; l; l = l->next){
		GraphView *graph_view = GRAPH_VIEW (l->data);

		graph_view_update (graph_view, graph->dirty_flags);
	}
	graph->dirty_flags = 0;
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
	const int n = graph->n_series;
	int i;
	int len = 0;

	/* Compute max lenght */
	for (i = 0; i < n; i++){
		int l;

		l = graph_vector_count (graph->vectors [i]);

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
}

static void
graph_compute_dimensions (Graph *graph)
{
	const int n = graph->n_series;
	double low = 0.0, high = 0.0;
	int i;

	graph_compute_divisions (graph);
	
	for (i = 0; i < n; i++){
		double l, h;
		
		graph_vector_low_high (graph->vectors [i], &l, &h);
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
	const int n = graph->n_series;
	int len = 0;
	double high, low;

	graph_compute_divisions (graph);
	high = low = 0.0;
	
	for (x = 0; x < len; x++){
		double s_high;
		doublw s_low;

		s_high = s_low = 0.0;
		
		for (i = 0; i < n; i++){
			double v;
			
			v = graph_vector_get_double (graph->vectors [i], x);

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
impl_graph_set_chart_type (PortableServer_Servant servant,
			   GNOME_Graph_ChartType value,
			   CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);

	if (graph->chart_type == value)
		return;
	
	graph->chart_type = value;
	switch (value){
	case GNOME_Graph_CHART_TYPE_CLUSTERED:
		graph_compute_dimensions (graph);
		break;
		
	case GNOME_Graph_CHART_TYPE_STACKED:
		graph_compute_stacked_dimensions (graph);
		break;
		
	case GNOME_Graph_CHART_TYPE_STACKED_FULL:
		graph_compute_stacked_dimensions (graph);
		break;
	}
	
	graph_update (graph);
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
		graph_update (graph);
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
		graph_update (graph);
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
		graph_update (graph);
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
		graph_update (graph);
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
		graph_update (graph);
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
		graph_update (graph);
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
		graph_update (graph);
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
		graph_update (graph);
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
		graph_update (graph);
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

static void
impl_reset_series (PortableServer_Servant servant, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);
	const int n = graph->n_series;
	int i;
	
	for (i = 0; i < n; i++)
		graph_vector_destroy (graph->vectors [i]);
	g_free (graph->vectors);
	graph->vectors = NULL;
	graph->n_series = 0;
}

static void
impl_add_series (PortableServer_Servant servant, GNOME_Gnumeric_Vector vector, CORBA_Environment *ev)
{
	Graph *graph = graph_from_servant (servant);
	GraphVector *v;
	
	v = g_renew (GraphVector *, graph->vectors, graph->n_series+1);
	if (v == NULL){
		CORBA_exception_set_system (ev, ex_CORBA_NO_MEMORY, CORBA_COMPLETED_NO);
		return;
	}
	
	graph->series [graph->n_series] = graph_vector_new (vector, NULL, NULL, 0);
	graph->n_series++;
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

	graph_epv.freeze = &impl_graph_freeze;
	graph_epv.thaw   = &impl_graph_thaw;

	graph_epv.reset_series = &impl_reset_series;
	graph_epv.add_series   = &impl_add_series;
	
	/*
	 * The Vepv
	 */
	graph_vepv.GNOME_Unknown_epv = &gnome_object_epv;
	graph_vepv.GNOME_Graph_Chart_epv = &graph_epv;
}

static void
graph_class_init (GtkObjectClass *object_class)
{
	graph_parent_class = gtk_type_class (gnome_object_get_type ());
	
	object_class->destroy = graph_destroy;
	
	init_graph_corba_class ();
}

static void
graph_init (GtkObject *object)
{
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

		type = gtk_type_unique (gnome_object_get_type (), &info);
	}

	return type;
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
}
