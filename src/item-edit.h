#ifndef ITEM_EDIT_H
#define ITEM_EDIT_H

#define ITEM_EDIT(obj)          (GTK_CHECK_CAST((obj), item_edit_get_type (), ItemEdit))
#define ITEM_EDIT_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_edit_get_type ()))
#define IS_ITEM_EDIT(o)         (GTK_CHECK_TYPE((o), item_edit_get_type ()))

typedef struct {
	GnomeCanvasItem canvas_item;

	/* The editor which status we reflect on the spreadsheet */
	GtkWidget  *editor;
	guint      signal;	/* the signal we connect */
	
	ItemGrid   *item_grid;
	Sheet      *sheet;

	/* Where are we */
	int        col, row, col_span;
	int        pixel_span;
} ItemEdit;

GtkType item_edit_get_type (void);

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemEditClass;


#endif
