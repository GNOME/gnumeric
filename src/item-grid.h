#ifndef _GNM_ITEM_GRID_H_
#define _GNM_ITEM_GRID_H_

#include <gnumeric-fwd.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_ITEM_GRID(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj), gnm_item_grid_get_type (), GnmItemGrid))
#define GNM_IS_ITEM_GRID(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), gnm_item_grid_get_type ()))

GType gnm_item_grid_get_type (void);

G_END_DECLS

#endif /* _GNM_ITEM_GRID_H_ */
