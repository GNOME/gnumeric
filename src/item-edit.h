#ifndef GNUMERIC_ITEM_EDIT_H
#define GNUMERIC_ITEM_EDIT_H

#include "gui-gnumeric.h"
#include <gtk/gtkentry.h>
#include <libfoocanvas/foo-canvas.h>

#define ITEM_EDIT(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), item_edit_get_type (), ItemEdit))
#define ITEM_EDIT_CLASS(k)      (G_TYPE_CHECK_CLASS_CAST ((k), item_edit_get_type (), ItemEditClass))
#define IS_ITEM_EDIT(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), item_edit_get_type ()))

GType item_edit_get_type (void);

void    item_edit_disable_highlight (ItemEdit *item_edit);
void    item_edit_enable_highlight  (ItemEdit *item_edit);

typedef struct {
	FooCanvasItemClass parent_class;
} ItemEditClass;

#endif /* GNUMERIC_ITEM_EDIT_H */
