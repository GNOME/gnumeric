#ifndef _GNM_SO_LINE_H_
# define _GNM_SO_LINE_H_

#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_SO_LINE_TYPE	(gnm_so_line_get_type ())
#define GNM_IS_SO_LINE(o)	(G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_LINE_TYPE))
GType gnm_so_line_get_type (void);

G_END_DECLS

#endif /* _GNM_SO_LINE_H_ */
