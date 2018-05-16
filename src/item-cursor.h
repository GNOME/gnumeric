#ifndef _GNM_ITEM_CURSOR_H_
#define _GNM_ITEM_CURSOR_H_

#include <gnumeric-fwd.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_ITEM_CURSOR(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), gnm_item_cursor_get_type (), GnmItemCursor))
#define GNM_IS_ITEM_CURSOR(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), gnm_item_cursor_get_type ()))

typedef enum {
	GNM_ITEM_CURSOR_SELECTION,
	GNM_ITEM_CURSOR_ANTED,
	GNM_ITEM_CURSOR_AUTOFILL,
	GNM_ITEM_CURSOR_DRAG,
	GNM_ITEM_CURSOR_EXPR_RANGE
} GnmItemCursorStyle;

GType gnm_item_cursor_get_type (void);

gboolean gnm_item_cursor_bound_set      (GnmItemCursor *ic, GnmRange const *bound);
void     gnm_item_cursor_set_visibility (GnmItemCursor *ic, gboolean visible);
void     gnm_item_cursor_reposition     (GnmItemCursor *ic);

G_END_DECLS

#endif /* _GNM_ITEM_CURSOR_H_ */
