#ifndef GNUMERIC_ITEM_CURSOR_H
#define GNUMERIC_ITEM_CURSOR_H

#include "gui-gnumeric.h"

#define ITEM_CURSOR(obj)          (GTK_CHECK_CAST((obj), item_cursor_get_type (), ItemCursor))
#define IS_ITEM_CURSOR(o)         (GTK_CHECK_TYPE((o), item_cursor_get_type ()))

typedef enum {
	ITEM_CURSOR_SELECTION,
	ITEM_CURSOR_ANTED,
	ITEM_CURSOR_AUTOFILL,
	ITEM_CURSOR_DRAG,
	ITEM_CURSOR_BLOCK
} ItemCursorStyle;

GtkType item_cursor_get_type (void);

gboolean item_cursor_bound_set	    (ItemCursor *ic, Range const *bound);
void     item_cursor_set_visibility (ItemCursor *ic, gboolean visible);
void     item_cursor_reposition     (ItemCursor *ic);

#endif /* GNUMERIC_ITEM_CURSOR_H */
