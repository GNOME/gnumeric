#ifndef GNUMERIC_SHEET_OBJECT_GRAPH_H
#define GNUMERIC_SHEET_OBJECT_GRAPH_H

#include "sheet-object.h"
#ifdef NEW_GRAPHS
#include <goffice/graph/goffice-graph.h>
#endif

#define SHEET_OBJECT_GRAPH_TYPE  (sheet_object_graph_get_type ())
#define IS_SHEET_OBJECT_GRAPH(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_GRAPH_TYPE))

GType	     sheet_object_graph_get_type (void);
#ifdef NEW_GRAPHS
SheetObject *sheet_object_graph_new (GogGraph *graph);
#endif

#endif /* GNUMERIC_SHEET_OBJECT_GRAPH_H */
