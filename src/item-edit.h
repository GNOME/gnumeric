#ifndef GNUMERIC_ITEM_EDIT_H
#define GNUMERIC_ITEM_EDIT_H

#include "gui-gnumeric.h"

#define ITEM_EDIT(obj)          (GTK_CHECK_CAST((obj), item_edit_get_type (), ItemEdit))
#define ITEM_EDIT_CLASS(k)      (GTK_CHECK_CLASS_CAST ((k), item_edit_get_type (), ItemEditClass))
#define IS_ITEM_EDIT(o)         (GTK_CHECK_TYPE((o), item_edit_get_type ()))

struct _ItemEdit {
	GnomeCanvasItem canvas_item;

	guint      signal_changed;	/* ::changed signal in the GtkEntry */
	guint      signal_key_press;	/* ::key-press-event signal in the GtkWidget */
	guint      signal_button_press;	/* ::button-press-event signal in the GtkWidget */

	ItemGrid   *item_grid;
	SheetControlGUI  *sheet_view;
	GtkEntry   *entry;		/* Utility pointer to the workbook entry */

	/* Where are we */
	int         col, row, col_span, lines, ignore_lines;
	GSList	   *text_offsets;

	GdkFont  *font;
	int	  font_height;
	gboolean  cursor_visible;
	int       blink_timer;
	int       auto_entry;

	/*
	 * When editing, if the cursor is inside a cell name, or a
	 * cell range, we highlight this on the spreadsheet.
	 */
	GnomeCanvasItem *feedback_cursor;
	Range            feedback_region;
	gboolean         feedback_disabled;
};

GtkType item_edit_get_type (void);

void    item_edit_disable_highlight (ItemEdit *item_edit);
void    item_edit_enable_highlight  (ItemEdit *item_edit);

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemEditClass;

#endif /* GNUMERIC_ITEM_EDIT_H */
