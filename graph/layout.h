/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GRAPH_LAYOUT_H_
#define GRAPH_LAYOUT_H_

#include <libgnome/gnome-defs.h>
#include <bonobo/gnome-embeddable.h>
#include "graph-vector.h"

BEGIN_GNOME_DECLS

typedef struct _Layout Layout;
#include "graph.h"

#define LAYOUT_TYPE        (layout_get_type ())
#define LAYOUT(o)          (GTK_CHECK_CAST ((o), LAYOUT_TYPE, Layout))
#define LAYOUT_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), LAYOUT_TYPE, LayoutClass))
#define IS_LAYOUT(o)       (GTK_CHECK_TYPE ((o), LAYOUT_TYPE))
#define IS_LAYOUT_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), LAYOUT_TYPE))

struct _Layout {
	GnomeEmbeddable parent;

	Graph  *graph;

	/*
	 * Number of series we hold
	 */
	int          n_series;
	GraphVector **vectors;
};

typedef struct {
	GnomeEmbeddableClass parent_class;
} LayoutClass;

GtkType      layout_get_type      (void);
Layout      *layout_new           (void);

void         layout_bind_view     (Layout *layout, void *layout_view);

void         layout_get_n_series  (Layout *layout);
GraphVector *layout_get_series    (Layout *layout, int n);


END_GNOME_DECLS

#endif /* LAYOUT_GRAPH_H_ */
