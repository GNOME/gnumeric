#ifndef GNUMERIC_GNM_SO_GRAPHIC_H
#define GNUMERIC_GNM_SO_GRAPHIC_H

#include "sheet-object.h"
#include <pango/pango-attributes.h>

#define GNM_SO_FILLED_TYPE  (gnm_so_filled_get_type ())
#define IS_GNM_SO_FILLED(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_FILLED_TYPE))
GType gnm_so_filled_get_type (void);

#define GNM_SO_POLYGON_TYPE  (gnm_so_polygon_get_type ())
#define IS_GNM_SO_POLYGON(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_POLYGON_TYPE))
GType gnm_so_polygon_get_type   (void);

#endif /* GNUMERIC_GNM_SO_GRAPHIC_H */
