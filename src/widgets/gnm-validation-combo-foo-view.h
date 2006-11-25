#ifndef GNM_VALIDATION_COMBO_FOO_VIEW_H
#define GNM_VALIDATION_COMBO_FOO_VIEW_H

#include "gnumeric.h"

SheetObjectView *gnm_validation_combo_foo_view_new	(SheetObject *so,
							 SheetObjectViewContainer *container);
void		 gnm_validation_combo_foo_view_popdown	(SheetObjectView *sov);

#endif /* GNM_VALIDATION_COMBO_FOO_VIEW_H */
