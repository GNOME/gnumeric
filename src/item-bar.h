#ifndef GNUMERIC_ITEM_BAR_H
#define GNUMERIC_ITEM_BAR_H

#include "sheet.h"

#define ITEM_BAR(obj)          (GTK_CHECK_CAST((obj), item_bar_get_type (), ItemBar))
#define ITEM_BAR_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_bar_get_type (), ItemBarType))
#define IS_ITEM_BAR(o)         (GTK_CHECK_TYPE((o), item_bar_get_type ()))

#define ITEM_BAR_RESIZING(x) (ITEM_BAR(x)->resize_pos != -1)

typedef enum {
	ITEM_BAR_NO_SELECTION,
	ITEM_BAR_PARTIAL_SELECTION,
	ITEM_BAR_FULL_SELECTION
} ItemBarSelectionType;

typedef struct {
	GnomeCanvasItem canvas_item;
	SheetView       *sheet_view;
	int             first_element;
	GtkOrientation  orientation;	/* horizontal, vertical */
	GdkGC           *gc;		/* Draw gc */
	GdkCursor       *normal_cursor;
	GdkCursor       *change_cursor;

	int             resize_pos;
	int 	        resize_width;
	int             resize_start_pos;
	GtkObject       *resize_guide;
	int             resize_guide_offset;
	
	int             dragging : 1;

	/* Where the selection started */
	int             start_selection;
} ItemBar;

#define ITEM_BAR_IS_SELECTING(ib) ((ib)->start_selection != -1)

GtkType item_bar_get_type (void);

typedef struct {
	GnomeCanvasItemClass parent_class;

	/* Signals emited */
	void (* selection_changed) (ItemBar *, int column, int modifiers);
	void (* size_changed)      (ItemBar *, int column, int new_width);
} ItemBarClass;

#endif /* GNUMERIC_ITEM_BAR_H */
