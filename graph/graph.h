/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GRAPH_GRAPH_H_
#define GRAPH_GRAPH_H_

#include <libgnome/gnome-defs.h>
#include <bonobo/gnome-object.h>
#include "Graph.h"

BEGIN_GNOME_DECLS

#define GRAPH_TYPE        (graph_get_type ())
#define GRAPH(o)          (GTK_CHECK_CAST ((o), GRAPH_TYPE, Graph))
#define GRAPH_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), GRAPH_TYPE, GraphClass))
#define IS_GRAPH(o)       (GTK_CHECK_TYPE ((o), GRAPH_TYPE))
#define IS_GRAPH_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), GRAPH_TYPE))

typedef struct _GraphView GraphView;

typedef struct {
	GnomeObject base;

	GNOME_Graph_ChartType     chart_type;
	GNOME_Graph_ScaleType     scale_type;
	GNOME_Graph_PlotMode      plot_mode;
	GNOME_Graph_ColBarMode    col_bar_mode;
	GNOME_Graph_DirMode       direction;
	GNOME_Graph_LineMode      line_mode;
	GNOME_Graph_PieMode       pie_mode;
	GNOME_Graph_PieDimension  pie_dim;
	GNOME_Graph_ScatterPoints scatter_mode;
	GNOME_Graph_ScatterConn   scatter_conn;
	GNOME_Graph_SurfaceMode   surface_mode;

	GSList *views;

	int      frozen;
	int      dirty_flags;

	/*
	 * Number of series we hold
	 */
	int         n_series;
	GraphVector *vectors;

	/*
	 * Series boundings
	 */
	double      low, high;
	double      real_low, real_high;
} Graph;

typedef struct {
	GnomeObjectClass parent_class;
} GraphClass;

#define DIRTY_BBOX  1
#define DIRTY_TYPE  2
#define DIRTY_SHAPE 4

GtkType     graph_get_type      (void);
Graph      *graph_new           (void);

void        graph_bind_view     (Graph *graph, GraphView *graph_view);

void        graph_get_n_series  (Graph *graph);
Series     *graph_get_series    (Graph *graph, int n);

END_GNOME_DECLS

#endif /* GRAH_GRAPH_H_ */
