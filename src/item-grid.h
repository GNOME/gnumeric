#ifndef GNUMERIC_ITEM_GRID_H
#define GNUMERIC_ITEM_GRID_H

#include "gnumeric.h"
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

	ItemGridSelectionType selecting;
	
	GdkGC      *grid_gc;	/* Draw grid gc */
	GdkGC      *fill_gc;	/* Default background fill gc */
	GdkGC      *gc;		/* Color used for the cell */
	GdkGC      *empty_gc;	/* GC used for drawing empty cells */
	
	GdkColor   background;
	GdkColor   grid_color;
	GdkColor   default_color;

	int        visual_is_paletted;
} ItemGrid;

GtkType item_grid_get_type (void);
void    item_grid_popup_menu (SheetView *sheet_view, GdkEventButton *event,
			      gboolean is_col,
			      gboolean is_row);

void    item_grid_draw_border (GdkDrawable *drawable, MStyle *mstyle,
			       int x, int y, int w, int h,
			       gboolean const extended_left);
			       
typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemGridClass;

#endif /* GNUMERIC_ITEM_GRID_H */
