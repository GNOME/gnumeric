#ifndef GNUMERIC_ITEM_EDIT_H
#define GNUMERIC_ITEM_EDIT_H

#include "gui-gnumeric.h"
#include <gtk/gtkentry.h>

#define ITEM_EDIT(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), item_edit_get_type (), ItemEdit))
#define ITEM_EDIT_CLASS(k)      (G_TYPE_CHECK_CLASS_CAST ((k), item_edit_get_type (), ItemEditClass))
#define IS_ITEM_EDIT(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), item_edit_get_type ()))

struct _ItemEdit {
	GnomeCanvasItem canvas_item;

	SheetControlGUI *scg;
	ItemGrid   	*item_grid;
	GtkEntry   	*entry;		/* Utility pointer to the workbook entry */

	guint      signal_changed;	/* ::changed signal in the GtkEntry */
	guint      signal_key_press;	/* ::key-press-event signal in the GtkWidget */
	guint      signal_button_press;	/* ::button-press-event signal in the GtkWidget */

	/* Where are we */
	CellPos	    pos;

	StyleFont *style_font;
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

GType item_edit_get_type (void);

void    item_edit_disable_highlight (ItemEdit *item_edit);
void    item_edit_enable_highlight  (ItemEdit *item_edit);

typedef struct {
	GnomeCanvasItemClass parent_class;
} ItemEditClass;

#endif /* GNUMERIC_ITEM_EDIT_H */
