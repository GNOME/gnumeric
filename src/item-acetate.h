#ifndef GNUMERIC_ITEM_ACETATE_H
#define GNUMERIC_ITEM_ACETATE_H

#include "gui-gnumeric.h"

#define ITEM_ACETATE(obj)          (GTK_CHECK_CAST((obj), item_acetate_get_type (), ItemAcetate))
#define IS_ITEM_ACETATE(o)         (GTK_CHECK_TYPE((o), item_acetate_get_type ()))

GtkType item_acetate_get_type (void);

#endif /* GNUMERIC_ITEM_ACETATE_H */
