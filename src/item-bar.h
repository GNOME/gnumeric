#ifndef GNUMERIC_ITEM_BAR_H
#define GNUMERIC_ITEM_BAR_H

#include "gui-gnumeric.h"

#define ITEM_BAR(obj)          (GTK_CHECK_CAST((obj), item_bar_get_type (), ItemBar))
#define IS_ITEM_BAR(o)         (GTK_CHECK_TYPE((o), item_bar_get_type ()))

struct _ItemBar {
	GnomeCanvasItem  canvas_item;

	SheetControlGUI *scg;
	int              dragging : 1;
	int              is_col_header : 1;
	GdkGC           *gc, *lines;		/* Draw gc */
	GdkCursor       *normal_cursor;
	GdkCursor       *change_cursor;
	GdkCursor       *guru_cursor;

	StyleFont	*normal_font, *bold_font;
	GtkWidget       *tip;			/* Tip for scrolling */

	int                indent, cell_width, cell_height;
	int                first_element;
	int		   start_selection;	/* Where selection started */

	int		   resize_pos;
	int		   resize_width;
	int		   resize_start_pos;
	GtkObject         *resize_guide;
	GtkObject         *resize_start;
	GnomeCanvasPoints *resize_points;
};

GtkType item_bar_get_type  (void);
int     item_bar_calc_size (ItemBar *ib);

#endif /* GNUMERIC_ITEM_BAR_H */
