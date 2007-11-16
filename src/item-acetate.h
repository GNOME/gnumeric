/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_ITEM_ACETATE_H_
# define _GNM_ITEM_ACETATE_H_

#include "gui-gnumeric.h"
#include <glib-object.h>

G_BEGIN_DECLS

#define ITEM_ACETATE(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), item_acetate_get_type (), ItemAcetate))
#define IS_ITEM_ACETATE(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), item_acetate_get_type ()))

GType item_acetate_get_type (void);

G_END_DECLS

#endif /* _GNM_ITEM_ACETATE_H_ */
