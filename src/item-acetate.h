#ifndef GNUMERIC_ITEM_ACETATE_H
#define GNUMERIC_ITEM_ACETATE_H

#include "gui-gnumeric.h"
#include <glib-object.h>

#define ITEM_ACETATE(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), item_acetate_get_type (), ItemAcetate))
#define IS_ITEM_ACETATE(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), item_acetate_get_type ()))

GType item_acetate_get_type (void);

#endif /* GNUMERIC_ITEM_ACETATE_H */
