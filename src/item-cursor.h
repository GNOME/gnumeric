#ifndef ITEM_CURSOR_H
#define ITEM_CURSOR_H

#define ITEM_CURSOR(obj)          (GTK_CHECK_CAST((obj), item_cursor_get_type (), ItemCursor))
#define ITEM_CURSOR_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_cursor_get_type ()))
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
} ItemCursor;

GtkType item_cursor_get_type (void);

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemCursorClass;

void item_cursor_set_bounds (ItemCursor *item_cursor,
			     int start_col, int start_row,
			     int end_col, int end_row);

#endif

