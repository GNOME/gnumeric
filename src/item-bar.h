#ifndef GNUMERIC_ITEM_BAR_H
#define GNUMERIC_ITEM_BAR_H

#include "gui-gnumeric.h"

#define ITEM_BAR(obj)          (GTK_CHECK_CAST((obj), item_bar_get_type (), ItemBar))
#define IS_ITEM_BAR(o)         (GTK_CHECK_TYPE((o), item_bar_get_type ()))

GtkType  item_bar_get_type	(void);
int      item_bar_calc_size	(ItemBar *ib);
int      item_bar_group_size	(ItemBar const *ib, int max_outline);
int      item_bar_indent	(ItemBar const *ib);
StyleFont const *item_bar_normal_font	(ItemBar const *ib);

#endif /* GNUMERIC_ITEM_BAR_H */
