#ifndef GNUMERIC_ITEM_GRID_H
#define GNUMERIC_ITEM_GRID_H

#include "gui-gnumeric.h"

#define ITEM_GRID(obj)          (GTK_CHECK_CAST((obj), item_grid_get_type (), ItemGrid))
#define IS_ITEM_GRID(o)         (GTK_CHECK_TYPE((o), item_grid_get_type ()))

GtkType item_grid_get_type (void);

#endif /* GNUMERIC_ITEM_GRID_H */
