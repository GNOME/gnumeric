#ifndef GNUMERIC_ITEM_GRID_H
#define GNUMERIC_ITEM_GRID_H

#include "sheet.h"
#include "sheet-view.h"

#define ITEM_GRID(obj)          (GTK_CHECK_CAST((obj), item_grid_get_type (), ItemGrid))
#define ITEM_GRID_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_grid_get_type (), ItemGridClass))
#define IS_ITEM_GRID(o)         (GTK_CHECK_TYPE((o), item_grid_get_type ()))

typedef enum {
	ITEM_GRID_NO_SELECTION,
	ITEM_GRID_SELECTING_CELL_RANGE,
	ITEM_GRID_SELECTING_FORMULA_RANGE
} ItemGridSelectionType;

typedef struct {
	GnomeCanvasItem canvas_item;

	SheetView *sheet_view;
	Sheet     *sheet;

	/* The first displayed column and row */
	int        left_col;
	int        top_row;

	ItemGridSelectionType selecting;
	
	/* Offset from spreadsheet origin in units */
	long       top_offset;
	long       left_offset;

	GdkGC      *grid_gc;	/* Draw grid gc */
	GdkGC      *fill_gc;	/* Default background fill gc */
	GdkGC      *gc;		/* Color used for the cell */
	GdkGC      *empty_gc;	/* GC used for drawing empty cells */
	
	GdkColor   background;
	GdkColor   grid_color;
	GdkColor   default_color;

	int        visual_is_paletted;

	/* Sliding scroll */
	int        sliding;	/* a gtk_timeout tag, -1 means not set */
	int        sliding_col, sliding_row;
	int        sliding_x, sliding_y;
} ItemGrid;

GtkType item_grid_get_type (void);
int     item_grid_find_col (ItemGrid *item_grid, int x, int *col_origin);
int     item_grid_find_row (ItemGrid *item_grid, int y, int *row_origin);
void    item_grid_popup_menu (Sheet *sheet, GdkEvent *event,
			      int col, int row,
			      gboolean const is_col,
			      gboolean const is_row);

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemGridClass;

#endif /* GNUMERIC_ITEM_GRID_H */
