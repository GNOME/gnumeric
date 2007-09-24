#ifndef GNM_CELL_COMBO_FOO_VIEW_H
#define GNM_CELL_COMBO_FOO_VIEW_H

#include "gnumeric.h"
#include <glib-object.h>

SheetObjectView *gnm_cell_combo_foo_view_new	 (SheetObject *so, GType t,
						  SheetObjectViewContainer *container);
void		 gnm_cell_combo_foo_view_popdown (SheetObjectView *sov, guint32 activate_time);

#endif /* GNM_CELL_COMBO_FOO_VIEW_H */
