/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_ITEM_GRID_H_
# define _GNM_ITEM_GRID_H_

#include "gui-gnumeric.h"
#include <glib-object.h>

G_BEGIN_DECLS

#define ITEM_GRID(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), item_grid_get_type (), ItemGrid))
#define IS_ITEM_GRID(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), item_grid_get_type ()))

GType item_grid_get_type (void);

G_END_DECLS

#endif /* _GNM_ITEM_GRID_H_ */
