/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_ITEM_CURSOR_H_
# define _GNM_ITEM_CURSOR_H_

#include "gui-gnumeric.h"
#include <glib-object.h>

G_BEGIN_DECLS

#define ITEM_CURSOR(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), item_cursor_get_type (), ItemCursor))
#define IS_ITEM_CURSOR(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), item_cursor_get_type ()))

typedef enum {
	ITEM_CURSOR_SELECTION,
	ITEM_CURSOR_ANTED,
	ITEM_CURSOR_AUTOFILL,
	ITEM_CURSOR_DRAG,
	ITEM_CURSOR_BLOCK
} ItemCursorStyle;

GType item_cursor_get_type (void);

gboolean item_cursor_bound_set	    (ItemCursor *ic, GnmRange const *bound);
void     item_cursor_set_visibility (ItemCursor *ic, gboolean visible);
void     item_cursor_reposition     (ItemCursor *ic);

G_END_DECLS

#endif /* _GNM_ITEM_CURSOR_H_ */
