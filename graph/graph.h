/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GRAPH_GRAPH_H_
#define GRAPH_GRAPH_H_

#include <libgnome/gnome-defs.h>
#include <bonobo/gnome-object.h>

BEGIN_GNOME_DECLS

#define GRAPH_TYPE        (graph_get_type ())
#define GRAPH(o)          (GTK_CHECK_CAST ((o), GRAPH_TYPE, Graph))
#define GRAPH_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), GRAPH_TYPE, GraphClass))
#define IS_GRAPH(o)       (GTK_CHECK_TYPE ((o), GRAPH_TYPE))
#define IS_GRAPH_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), GRAPH_TYPE))

struct Graph {
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
};

struct GraphClass {
	GnomeObjectClass parent_class;
};

GtkType     graph_get_type (void);

END_GNOME_DECLS

#endif /* GRAH_GRAPH_H_ */
