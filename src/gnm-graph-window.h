/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_GRAPH_WINDOW_H_
# define _GNM_GRAPH_WINDOW_H_

#include <gtk/gtkwidget.h>
#include <goffice/graph/gog-graph.h>

G_BEGIN_DECLS

#define GNM_TYPE_GRAPH_WINDOW             (gnm_graph_window_get_type ())
#define GNM_GRAPH_WINDOW(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_TYPE_GRAPH_WINDOW, GnmGraphWindow))
#define GNM_GRAPH_WINDOW_CLASS(vtable)    (G_TYPE_CHECK_CLASS_CAST ((vtable), GNM_TYPE_GRAPH_WINDOW, GnmGraphWindowClass))
#define IS_GNM_GRAPH_WINDOW(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNM_TYPE_GRAPH_WINDOW))
#define IS_GNM_GRAPH_WINDOW_CLASS(vtable) (G_TYPE_CHECK_CLASS_TYPE ((vtable), GNM_TYPE_GRAPH_WINDOW))
#define GNM_GRAPH_WINDOW_GET_CLASS(inst)  (G_TYPE_INSTANCE_GET_CLASS ((inst), GNM_TYPE_GRAPH_WINDOW, GnmGraphWindowClass))

typedef struct _GnmGraphWindow      GnmGraphWindow;
typedef struct _GnmGraphWindowClass GnmGraphWindowClass;

GType      gnm_graph_window_get_type (void);

GtkWidget *gnm_graph_window_new (GogGraph *graph,
				 double    graph_width,
				 double    graph_height);

G_END_DECLS

#endif /* _GNM_GRAPH_WINDOW_H_ */
