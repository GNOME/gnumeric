#ifndef GNM_SO_POLYGON_H
#define GNM_SO_POLYGON_H

#include   <glib-object.h>

#define GNM_SO_POLYGON_TYPE  (gnm_so_polygon_get_type ())
#define IS_GNM_SO_POLYGON(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_POLYGON_TYPE))
GType gnm_so_polygon_get_type (void);

#endif /* GNM_SO_POLYGON_H */
