/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef GRAPH_LAYOUT_VIEW_H_
#define GRAPH_LAYOUT_VIEW_H_

#include <libgnome/gnome-defs.h>
#include <libgnomeui/gnome-canvas.h>
#include "layout.h"
#include "graph-view.h"

BEGIN_GNOME_DECLS

#define LAYOUT_VIEW_TYPE        (layout_view_get_type ())
#define LAYOUT_VIEW(o)          (GTK_CHECK_CAST ((o), LAYOUT_VIEW_TYPE, LayoutView))
#define LAYOUT_VIEW_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), LAYOUT_VIEW_TYPE, LayoutViewClass))
#define IS_LAYOUT_VIEW(o)       (GTK_CHECK_TYPE ((o), LAYOUT_VIEW_TYPE))
#define IS_LAYOUT_VIEW_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), LAYOUT_VIEW_TYPE))

typedef struct _LayoutView LayoutView;

struct _LayoutView {
	GnomeCanvas canvas;

	Layout    *layout;
	GraphView *graph_view;
};

typedef struct {
	GnomeCanvasClass parent_class;
} LayoutViewClass;

GtkType     layout_view_get_type      (void);
GtkWidget  *layout_view_new           (Layout *layout);

END_GNOME_DECLS

#endif /* GRAPH_LAYOUT_VIEW_H_ */
