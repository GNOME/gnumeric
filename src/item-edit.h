#ifndef GNUMERIC_ITEM_EDIT_H
#define GNUMERIC_ITEM_EDIT_H

#include "sheet.h"
#include "style.h"
#include "item-grid.h"

#define ITEM_EDIT(obj)          (GTK_CHECK_CAST((obj), item_edit_get_type (), ItemEdit))
#define ITEM_EDIT_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_edit_get_type (), ItemEditClass))
#define IS_ITEM_EDIT(o)         (GTK_CHECK_TYPE((o), item_edit_get_type ()))

typedef struct {
	GnomeCanvasItem canvas_item;

	/* The editor which status we reflect on the spreadsheet */
	GtkWidget  *editor;
	guint      signal;	/* the signal we connect */
	guint      signal2;	/* the other signal we connect */

	ItemGrid   *item_grid;
	Sheet      *sheet;

	/* Where are we */
	int        col, row, col_span, row_span;
	int        pixel_span;

	MStyle    *mstyle;

	/*
	 * When editing, if the cursor is inside a cell name, or a
	 * cell range, we highlight this on the spreadsheet.
	 */
	GnomeCanvasItem *feedback_cursor;
	Range            feedback_region;
	gboolean         feedback_disabled;
} ItemEdit;

GtkType item_edit_get_type (void);

void    item_edit_disable_highlight (ItemEdit *item_edit);
void    item_edit_enable_highlight  (ItemEdit *item_edit);

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemEditClass;

#endif /* GNUMERIC_ITEM_EDIT_H */
