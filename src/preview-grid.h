#ifndef GNUMERIC_PREVIEW_GRID_H
#define GNUMERIC_PREVIEW_GRID_H

#include "gnumeric.h"
#include <gtk/gtk.h>

#define PREVIEW_GRID(obj)          (GTK_CHECK_CAST((obj), preview_grid_get_type (), PreviewGrid))
#define IS_PREVIEW_GRID(o)         (GTK_CHECK_TYPE((o), preview_grid_get_type ()))

typedef struct _PreviewGrid PreviewGrid;
GtkType preview_grid_get_type (void);

#endif /* GNUMERIC_PREVIEW_GRID_H */

