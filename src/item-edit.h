#ifndef GNUMERIC_ITEM_EDIT_H
#define GNUMERIC_ITEM_EDIT_H

#include "gui-gnumeric.h"
#include <glib-object.h>

#define ITEM_EDIT(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), item_edit_get_type (), ItemEdit))
#define IS_ITEM_EDIT(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), item_edit_get_type ()))

GType item_edit_get_type (void);

#endif /* GNUMERIC_ITEM_EDIT_H */
