/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef PREVIEW_GRID_IMPL_H
#define PREVIEW_GRID_IMPL_H

#include "preview-grid.h"
#include <libfoocanvas/foo-canvas.h>

struct _PreviewGrid {
	FooCanvasGroup base;

	struct { /* Gc's */
		GdkGC *fill;	/* Default background fill gc */
		GdkGC *cell;	/* Color used for the cell */
		GdkGC *empty;	/* GC used for drawing empty cells */
	} gc;

	struct {
		int     col_width;
		int     row_height;
		MStyle *style;
		Value  *value;
	} defaults;

	gboolean gridlines;
};

typedef struct {
	FooCanvasGroupClass parent_class;

	/* Virtuals */
	int      (* get_col_width)  (PreviewGrid *pg, int col);
	int      (* get_row_height) (PreviewGrid *pg, int row);
	MStyle * (* get_cell_style) (PreviewGrid *pg, int col, int row);
	Value *  (* get_cell_value) (PreviewGrid *pg, int col, int row);
} PreviewGridClass;

#define PREVIEW_GRID_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), preview_grid_get_type (), PreviewGridClass))

#endif /* PREVIEW_GRID_IMPL_H */
