#ifndef GNUMERIC_ITEM_CURSOR_H
#define GNUMERIC_ITEM_CURSOR_H

#include "gui-gnumeric.h"

#define ITEM_CURSOR(obj)          (GTK_CHECK_CAST((obj), item_cursor_get_type (), ItemCursor))
#define ITEM_CURSOR_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_cursor_get_type (), ItemCursorClass))
#define IS_ITEM_CURSOR(o)         (GTK_CHECK_TYPE((o), item_cursor_get_type ()))

typedef enum {
	ITEM_CURSOR_SELECTION,
	ITEM_CURSOR_EDITING,
	ITEM_CURSOR_ANTED,
	ITEM_CURSOR_AUTOFILL,
	ITEM_CURSOR_DRAG,
	ITEM_CURSOR_BLOCK
} ItemCursorStyle;

struct _ItemCursor {
	GnomeCanvasItem canvas_item;

	SheetControlGUI    *sheet_view;
	ItemGrid *item_grid;	/* A copy of our "parent" grid */
	Range     pos;

	/* Offset of dragging cell from top left of pos */
	int col_delta, row_delta;

	/* Tip for movement */
	GtkWidget        *tip;

	ItemCursorStyle style;
	GdkGC    *gc;
	int      state;
	int      animation_timer;

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

	int      visible:1;
	int      use_color:1;
	/* Location of auto fill handle */
	int      auto_fill_handle_at_top:1;
	int      prepared_to_drag:1;

	GdkPixmap *stipple;
	GdkColor  color;
};

GtkType item_cursor_get_type (void);

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemCursorClass;

gboolean item_cursor_set_bounds (ItemCursor *item_cursor,
				 int start_col, int start_row,
				 int end_col, int end_row);

void item_cursor_set_spin_base  (ItemCursor *item_cursor,
				 int col, int row);

void item_cursor_set_visibility (ItemCursor *item_cursor,
				 gboolean const visible);
void item_cursor_reposition     (ItemCursor *item_cursor);

#endif /* GNUMERIC_ITEM_CURSOR_H */
