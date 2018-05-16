#ifndef _GNM_GRAPH_WINDOW_H_
# define _GNM_GRAPH_WINDOW_H_

#include <goffice/goffice.h>

G_BEGIN_DECLS

#define GNM_TYPE_GRAPH_WINDOW             (gnm_graph_window_get_type ())
#define GNM_GRAPH_WINDOW(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GNM_TYPE_GRAPH_WINDOW, GnmGraphWindow))
#define GNM_IS_GRAPH_WINDOW(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GNM_TYPE_GRAPH_WINDOW))

typedef struct _GnmGraphWindow      GnmGraphWindow;
typedef struct _GnmGraphWindowClass GnmGraphWindowClass;

GType      gnm_graph_window_get_type (void);

GtkWidget *gnm_graph_window_new (GogGraph *graph,
				 double    graph_width,
				 double    graph_height);

G_END_DECLS

#endif /* _GNM_GRAPH_WINDOW_H_ */
