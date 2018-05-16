#ifndef _GNM_SO_POLYGON_H_
# define _GNM_SO_POLYGON_H_

#include   <glib-object.h>

G_BEGIN_DECLS

#define GNM_SO_POLYGON_TYPE  (gnm_so_polygon_get_type ())
#define GNM_IS_SO_POLYGON(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_POLYGON_TYPE))
GType gnm_so_polygon_get_type (void);

G_END_DECLS

#endif /* _GNM_SO_POLYGON_H_ */
