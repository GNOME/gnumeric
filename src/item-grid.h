#ifndef ITEM_GRID_H
#define ITEM_GRID_H

#define ITEM_GRID(obj)          (GTK_CHECK_CAST((obj), item_grid_get_type (), ItemGrid))
#define ITEM_GRID_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_grid_get_type ()))
#define IS_ITEM_GRID(o)         (GTK_CHECK_TYPE((o), item_grid_get_type ()))

typedef struct {
	GnomeCanvasItem canvas_item;

	Sheet    *sheet;

	/* The first displayed column and row */
	ColType  left_col;
	RowType  top_row;

	/* Offset from spreadsheet origin in units */
	long     top_offset;
	long     left_offset;

	GdkGC    *grid_gc;
	gulong   default_grid_color;
	
} ItemGrid;

GtkType item_grid_get_type (void);

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemGridClass;

#endif
