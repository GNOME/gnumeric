#ifndef GNM_SO_FILLED_H
#define GNM_SO_FILLED_H

#include   <glib-object.h>

#define GNM_SO_FILLED_TYPE  (gnm_so_filled_get_type ())
#define IS_GNM_SO_FILLED(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_FILLED_TYPE))
GType gnm_so_filled_get_type (void);

#endif /* GNM_SO_FILLED_H */
