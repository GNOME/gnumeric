#ifndef GNUMERIC_WORKBOOK_VIEW_H
#define GNUMERIC_WORKBOOK_VIEW_H

#include "gnumeric.h"

/*
 * Actions on the workbooks UI
 * 
 * These are the embryonic form of signals that will change the
 * workbook-view.  I amsure that this is the wrong place and interface
 * but it is a starting point of the list.
 *
 */
void workbook_view_set_paste_special_state (Workbook *wb, gboolean enable);

void workbook_view_set_undo_redo_state (Workbook const * const wb,
					gboolean const has_undos,
					gboolean const has_redos);

#endif /* GNUMERIC_WORKBOOK_VIEW_H */
