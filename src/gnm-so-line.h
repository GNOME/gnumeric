#ifndef GNM_SO_LINE_H
#define GNM_SO_LINE_H

#include <glib-object.h>

#define GNM_SO_LINE_TYPE	(gnm_so_line_get_type ())
#define IS_GNM_SO_LINE(o)	(G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_LINE_TYPE))
GType gnm_so_line_get_type (void);

#endif /* GNM_SO_LINE_H */
