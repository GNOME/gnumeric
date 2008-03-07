/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_ITEM_BAR_H_
# define _GNM_ITEM_BAR_H_

#include "gui-gnumeric.h"
#include <glib-object.h>
#include <pango/pango-font.h>

G_BEGIN_DECLS

#define ITEM_BAR(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), item_bar_get_type (), ItemBar))
#define IS_ITEM_BAR(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), item_bar_get_type ()))

GType    item_bar_get_type	(void);
int      item_bar_calc_size	(ItemBar *ib);
int      item_bar_group_size	(ItemBar const *ib, int max_outline);
int      item_bar_indent	(ItemBar const *ib);
PangoFont *item_bar_normal_font	(ItemBar const *ib);

G_END_DECLS

#endif /* _GNM_ITEM_BAR_H_ */
