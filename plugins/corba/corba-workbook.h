#ifndef GNUMERIC_WORKBOOK_CONTROL_CORBA_H
#define GNUMERIC_WORKBOOK_CONTROL_CORBA_H

#include <gnumeric.h>
#include "GNOME_Gnumeric.h"

#define WORKBOOK_CONTROL_CORBA_TYPE     (workbook_control_corba_get_type ())
#define WORKBOOK_CONTROL_CORBA(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), WORKBOOK_CONTROL_CORBA_TYPE, WorkbookControlCORBA))
#define IS_WORKBOOK_CONTROL_CORBA(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), WORKBOOK_CONTROL_CORBA_TYPE))

GType            workbook_control_corba_get_type  (void);
WorkbookControl *workbook_control_corba_new       (WorkbookView *optional_view,
						   Workbook	*optional_wb);
GNOME_Gnumeric_Workbook workbook_control_corba_obj(WorkbookControl *wbc);

#endif /* GNUMERIC_WORKBOOK_CONTROL_CORBA_H */
