#ifndef GNUMERIC_PREVIEW_GRID_H
#define GNUMERIC_PREVIEW_GRID_H

#include "gnumeric.h"

#define PREVIEW_GRID(obj)          (GTK_CHECK_CAST((obj), preview_grid_get_type (), PreviewGrid))
#define PREVIEW_GRID_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), preview_grid_get_type (), PreviewGridClass))
#define IS_PREVIEW_GRID(o)         (GTK_CHECK_TYPE((o), preview_grid_get_type ()))

typedef Cell*    (* PGridGetCell) (int row, int col, gpointer data);

typedef int      (* PGridGetRowOffset) (int y, int* row_origin, gpointer data);
typedef int      (* PGridGetColOffset) (int x, int* col_origin, gpointer data);

typedef int      (* PGridGetColWidth) (int col, gpointer data);
typedef int      (* PGridGetRowHeight) (int row, gpointer data);

typedef MStyle * (* PGridGetStyle) (int row, int col, gpointer data);

typedef struct {
	GnomeCanvasItem canvas_item;

	GdkGC      *grid_gc;	/* Draw grid gc */
	GdkGC      *fill_gc;	/* Default background fill gc */
	GdkGC      *gc;		/* Color used for the cell */
	GdkGC      *empty_gc;	/* GC used for drawing empty cells */

	GdkColor   background;
	GdkColor   grid_color;
	GdkColor   default_color;

	PGridGetCell get_cell_cb;

	PGridGetRowOffset get_row_offset_cb;
	PGridGetColOffset get_col_offset_cb;

	PGridGetColWidth get_col_width_cb;
	PGridGetRowHeight get_row_height_cb;

	PGridGetStyle get_style_cb;

	gpointer cb_data;

	gboolean gridlines;
} PreviewGrid;

GtkType preview_grid_get_type (void);

typedef struct {
	GnomeCanvasItemClass parent_class;
} PreviewGridClass;

#endif /* GNUMERIC_PREVIEW_GRID_H */

