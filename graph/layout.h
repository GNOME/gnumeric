/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GRAPH_LAYOUT_H_
#define GRAPH_LAYOUT_H_

#include <libgnome/gnome-defs.h>
#include <bonobo/bonobo-embeddable.h>
#include "graph-vector.h"

BEGIN_GNOME_DECLS

typedef struct _Layout Layout;
#include "graph.h"

#define LAYOUT_TYPE        (layout_get_type ())
#define LAYOUT(o)          (GTK_CHECK_CAST ((o), LAYOUT_TYPE, Layout))
#define LAYOUT_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), LAYOUT_TYPE, LayoutClass))
#define IS_LAYOUT(o)       (GTK_CHECK_TYPE ((o), LAYOUT_TYPE))
#define IS_LAYOUT_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), LAYOUT_TYPE))

typedef struct {
	/*
	 * Factor: 0..1 that determines the size of the object
	 */
	double x_size, y_size;

	/*
	 * Factor: 0..1 that determines the location of the object
	 */
	double x_pos, y_pos;
} ObjDim;

struct _Layout {
	BonoboEmbeddable parent;

	ObjDim           graph_dim;
	Graph           *graph;

	/*
	 * Number of series we hold
	 */
	int          n_series;
	GraphVector **vectors;
};

typedef struct {
	BonoboEmbeddableClass parent_class;
} LayoutClass;

GtkType      layout_get_type      (void);
Layout      *layout_new           (void);

void         layout_bind_view     (Layout *layout, void *layout_view);

void         layout_get_n_series  (Layout *layout);
GraphVector *layout_get_series    (Layout *layout, int n);

void         layout_dim_bbox      (ArtIRect *bbox, int x, int y,
				   int width, int height, ObjDim *dim);

END_GNOME_DECLS

#endif /* LAYOUT_GRAPH_H_ */
