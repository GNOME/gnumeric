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

	SheetControlGUI *scg;
	Range     	 pos;

	CellPos	 base_corner;	/* Corner remains static when rubber banding */
	CellPos	 move_corner;	/* Corner to move when extending */

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
	 */
	CellPos	 base;
	int      base_x, base_y;
	int      base_cols, base_rows;

	/* Cached values of the last bounding box information used */
	int      cached_x, cached_y, cached_w, cached_h;

	int      visible:1;
	int      use_color:1;
	/* Location of auto fill handle */
	int      auto_fill_handle_at_top:1;
	int	 drag_button;

	GdkPixmap *stipple;
	GdkColor  color;
};

GtkType item_cursor_get_type (void);

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemCursorClass;

gboolean item_cursor_set_bounds (ItemCursor *item_cursor,
				 int base_col, int base_row,
				 int move_col, int move_row);

void item_cursor_set_visibility (ItemCursor *item_cursor,
				 gboolean const visible);
void item_cursor_reposition     (ItemCursor *item_cursor);

#endif /* GNUMERIC_ITEM_CURSOR_H */
