#ifndef ITEM_BAR_H
#define ITEM_BAR_H

#define ITEM_BAR(obj)          (GTK_CHECK_CAST((obj), item_bar_get_type (), ItemBar))
#define ITEM_BAR_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_bar_get_type ()))
#define IS_ITEM_BAR(o)         (GTK_CHECK_TYPE((o), item_bar_get_type ()))

typedef struct {
	GnomeCanvasItem canvas_item;
	Sheet           *sheet;
	int             first_element;
	GtkOrientation  orientation;	/* horizontal, vertical */
	GdkGC           *gc;		/* Draw gc */
	GdkCursor       *normal_cursor;
	GdkCursor       *change_cursor;
} ItemBar;

GtkType item_bar_get_type (void);

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemBarClass;

#endif
