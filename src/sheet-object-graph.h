#ifndef GNUMERIC_SHEET_OBJECT_GRAPH_H
#define GNUMERIC_SHEET_OBJECT_GRAPH_H

#include "sheet-object.h"
#include "gui-gnumeric.h"
#include <goffice/graph/goffice-graph.h>
#include <goffice/graph/gog-guru.h>

#define SHEET_OBJECT_GRAPH_TYPE  (sheet_object_graph_get_type ())
#define IS_SHEET_OBJECT_GRAPH(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_GRAPH_TYPE))

GType	     sheet_object_graph_get_type (void);
SheetObject *sheet_object_graph_new  (GogGraph *graph);
GogGraph    *sheet_object_graph_get_gog (SheetObject *sog);
void	     sheet_object_graph_set_gog (SheetObject *sog, GogGraph *graph);

void sheet_object_graph_guru (WorkbookControlGUI *wbcg, GogGraph *graph,
			      GogGuruRegister handler, gpointer handler_data);

#endif /* GNUMERIC_SHEET_OBJECT_GRAPH_H */
