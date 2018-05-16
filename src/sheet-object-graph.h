#ifndef _GNM_SHEET_OBJECT_GRAPH_H_
# define _GNM_SHEET_OBJECT_GRAPH_H_

#include <sheet-object.h>
#include <gnumeric-fwd.h>
#include <goffice/goffice.h>

G_BEGIN_DECLS

#define GNM_SO_GRAPH_TYPE  (sheet_object_graph_get_type ())
#define GNM_IS_SO_GRAPH(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_GRAPH_TYPE))
#define GNM_SO_GRAPH(o)	 (G_TYPE_CHECK_INSTANCE_CAST((o), GNM_SO_GRAPH_TYPE, SheetObjectGraph))

GType	     sheet_object_graph_get_type (void);
SheetObject *sheet_object_graph_new  (GogGraph *graph);
GogGraph    *sheet_object_graph_get_gog (SheetObject *sog);
void	     sheet_object_graph_set_gog (SheetObject *sog, GogGraph *graph);

void sheet_object_graph_guru (WBCGtk *wbcg, GogGraph *graph,
			      GClosure *closure);

void	     sheet_object_graph_ensure_size (SheetObject *sog);
G_END_DECLS

#endif /* _GNM_SHEET_OBJECT_GRAPH_H_ */
