/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_PREVIEW_GRID_H_
# define _GNM_PREVIEW_GRID_H_

#include "gnumeric.h"
#include <glib-object.h>

G_BEGIN_DECLS

#define PREVIEW_GRID(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), preview_grid_get_type (), PreviewGrid))
#define IS_PREVIEW_GRID(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), preview_grid_get_type ()))

typedef struct _PreviewGrid PreviewGrid;
GType preview_grid_get_type (void);

G_END_DECLS

#endif /* _GNM_PREVIEW_GRID_H_ */
