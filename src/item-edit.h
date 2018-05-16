#ifndef _GNM_ITEM_EDIT_H_
#define _GNM_ITEM_EDIT_H_

#include <gnumeric-fwd.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GNM_ITEM_EDIT(obj)    (G_TYPE_CHECK_INSTANCE_CAST((obj), gnm_item_edit_get_type (), GnmItemEdit))
#define GNM_IS_ITEM_EDIT(o)   (G_TYPE_CHECK_INSTANCE_TYPE((o), gnm_item_edit_get_type ()))

GType gnm_item_edit_get_type (void);

G_END_DECLS

#endif /* _GNM_ITEM_EDIT_H_ */
