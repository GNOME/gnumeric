#ifndef GNUMERIC_PREVIEW_GRID_H
#define GNUMERIC_PREVIEW_GRID_H

#include "gnumeric.h"
#include <glib-object.h>

#define PREVIEW_GRID(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), preview_grid_get_type (), PreviewGrid))
#define IS_PREVIEW_GRID(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), preview_grid_get_type ()))

typedef struct _PreviewGrid PreviewGrid;
GType preview_grid_get_type (void);

#endif /* GNUMERIC_PREVIEW_GRID_H */

