/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SHEET_OBJECT_GRAPH_H_
# define _GNM_SHEET_OBJECT_GRAPH_H_

#include "sheet-object.h"
#include "gui-gnumeric.h"
#include <goffice/graph/goffice-graph.h>
#include <goffice/graph/gog-guru.h>

G_BEGIN_DECLS

#define SHEET_OBJECT_GRAPH_TYPE  (sheet_object_graph_get_type ())
#define IS_SHEET_OBJECT_GRAPH(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), SHEET_OBJECT_GRAPH_TYPE))
#define SHEET_OBJECT_GRAPH(o)	 (G_TYPE_CHECK_INSTANCE_CAST((o), SHEET_OBJECT_GRAPH_TYPE, SheetObjectGraph))

GType	     sheet_object_graph_get_type (void);
SheetObject *sheet_object_graph_new  (GogGraph *graph);
GogGraph    *sheet_object_graph_get_gog (SheetObject *sog);
void	     sheet_object_graph_set_gog (SheetObject *sog, GogGraph *graph);

void sheet_object_graph_guru (WBCGtk *wbcg, GogGraph *graph,
			      GClosure *closure);

G_END_DECLS

#endif /* _GNM_SHEET_OBJECT_GRAPH_H_ */
