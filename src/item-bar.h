#ifndef GNUMERIC_ITEM_BAR_H
#define GNUMERIC_ITEM_BAR_H

#include "gui-gnumeric.h"

#define ITEM_BAR(obj)          (GTK_CHECK_CAST((obj), item_bar_get_type (), ItemBar))
#define ITEM_BAR_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_bar_get_type (), ItemBarType))
#define IS_ITEM_BAR(o)         (GTK_CHECK_TYPE((o), item_bar_get_type ()))

struct _ItemBar {
	GnomeCanvasItem  canvas_item;
	SheetControlGUI       *sheet_view;
	int              first_element;
	GtkOrientation   orientation;	/* horizontal, vertical */
	GdkGC           *gc;		/* Draw gc */
	GdkCursor       *normal_cursor;
	GdkCursor       *change_cursor;

	StyleFont	*normal_font, *bold_font;

	int		   resize_pos;
	int		   resize_width;
	int		   resize_start_pos;
	GtkObject         *resize_guide;
	GtkObject         *resize_start;
	GnomeCanvasPoints *resize_points;

	int             dragging : 1;

	/* Tip for scrolling */
	GtkWidget        *tip; /* currently disabled */

	/* Where the selection started */
	int             start_selection;
};

#define ITEM_BAR_IS_SELECTING(ib) ((ib)->start_selection != -1)

GtkType item_bar_get_type (void);
void    item_bar_fonts_init (ItemBar *item_bar);

typedef struct {
	GnomeCanvasItemClass parent_class;

	/* Signals emited */
	void (* selection_changed) (ItemBar *, int column, int modifiers);
	void (* size_changed)      (ItemBar *, int column, int new_width);
} ItemBarClass;

#endif /* GNUMERIC_ITEM_BAR_H */
