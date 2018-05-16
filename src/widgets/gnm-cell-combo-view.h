#ifndef GNM_CELL_COMBO_VIEW_H
#define GNM_CELL_COMBO_VIEW_H

#include <gnumeric.h>
#include <glib-object.h>

SheetObjectView *gnm_cell_combo_view_new	 (SheetObject *so, GType type,
						  SheetObjectViewContainer *container);
void		 gnm_cell_combo_view_popdown (SheetObjectView *sov, guint32 activate_time);

#endif /* GNM_CELL_COMBO_VIEW_H */

