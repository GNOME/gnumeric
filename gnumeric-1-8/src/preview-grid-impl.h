/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_PREVIEW_GRID_IMPL_H_
# define _GNM_PREVIEW_GRID_IMPL_H_

#include "preview-grid.h"
#include <goffice/cut-n-paste/foocanvas/foo-canvas.h>

G_BEGIN_DECLS

struct _PreviewGrid {
	FooCanvasGroup base;

	Sheet *sheet;

	struct { /* Gc's */
		GdkGC *fill;	/* Default background fill gc */
		GdkGC *cell;	/* Color used for the cell */
		GdkGC *empty;	/* GC used for drawing empty cells */
	} gc;

	struct {
		int     col_width;
		int     row_height;
		GnmStyle *style;
		GnmValue  *value;
	} defaults;

	gboolean gridlines;
};

typedef struct {
	FooCanvasGroupClass parent_class;

	/* Virtuals */
	GnmStyle * (* get_cell_style) (PreviewGrid *pg, int col, int row);
	GnmValue*(* get_cell_value) (PreviewGrid *pg, int col, int row);
} PreviewGridClass;

#define PREVIEW_GRID_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), preview_grid_get_type (), PreviewGridClass))

G_END_DECLS

#endif /* _GNM_PREVIEW_GRID_IMPL_H_ */
