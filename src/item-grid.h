#ifndef GNUMERIC_ITEM_GRID_H
#define GNUMERIC_ITEM_GRID_H

#include "gui-gnumeric.h"

#define ITEM_GRID(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), item_grid_get_type (), ItemGrid))
#define IS_ITEM_GRID(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), item_grid_get_type ()))

GType item_grid_get_type (void);

#endif /* GNUMERIC_ITEM_GRID_H */
