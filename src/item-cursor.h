#ifndef GNUMERIC_ITEM_CURSOR_H
#define GNUMERIC_ITEM_CURSOR_H

#define ITEM_CURSOR(obj)          (GTK_CHECK_CAST((obj), item_cursor_get_type (), ItemCursor))
#define ITEM_CURSOR_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_cursor_get_type (), ItemCursorClass))
#define IS_ITEM_CURSOR(o)         (GTK_CHECK_TYPE((o), item_cursor_get_type ()))

typedef enum {
	ITEM_CURSOR_SELECTION,
	ITEM_CURSOR_EDITING,
	ITEM_CURSOR_ANTED,
	ITEM_CURSOR_AUTOFILL,
	ITEM_CURSOR_DRAG,
} ItemCursorStyle;

typedef struct {
	GnomeCanvasItem canvas_item;

	Sheet    *sheet;
	ItemGrid *item_grid;	/* A copy of our "parent" grid */
	ColType  start_col, end_col;
	RowType  start_row, end_row;

	ItemCursorStyle style;
	GdkGC    *gc;
	int      state;
	int      tag;

	/*
	 * For the autofill mode:
	 *     Where the action started (base_x, base_y) and the
	 *     width and heigth of the selection when the autofill
	 *     started.
	 *
	 * For the anted mode:
	 *     base_col and base_row are used to keep track of where
	 *     the selection was started.
	 */
	int      base_x, base_y;
	int      base_col, base_row;
	int      base_cols, base_rows;
	
	/* Cached values of the last bounding box information used */
	int      cached_x, cached_y, cached_w, cached_h;

	int      visible;
	
	GdkPixmap *stipple;
} ItemCursor;

GtkType item_cursor_get_type (void);

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemCursorClass;

void item_cursor_set_bounds     (ItemCursor *item_cursor,
				 int start_col, int start_row,
				 int end_col, int end_row);

void item_cursor_set_visibility (ItemCursor *item_cursor,
				 int visible);

#endif /* GNUMERIC_ITEM_CURSOR_H */
