#ifndef _GNM_SO_FILLED_H_
# define _GNM_SO_FILLED_H_

#include   <glib-object.h>

G_BEGIN_DECLS

#define GNM_SO_FILLED_TYPE  (gnm_so_filled_get_type ())
#define GNM_IS_SO_FILLED(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_FILLED_TYPE))
GType gnm_so_filled_get_type (void);

G_END_DECLS

#endif /* _GNM_SO_FILLED_H_ */
