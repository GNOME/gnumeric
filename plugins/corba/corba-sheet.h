#ifndef GNUMERIC_SHEET_CONTROL_CORBA_H
#define GNUMERIC_SHEET_CONTROL_CORBA_H

#include "sheet-control.h"

#define SHEET_CONTROL_CORBA_TYPE     (sheet_control_corba_get_type ())
#define SHEET_CONTROL_CORBA(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), SHEET_CONTROL_CORBA_TYPE, SheetControlCORBA))
#define IS_SHEET_CONTROL_CORBA(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), SHEET_CONTROL_CORBA_TYPE))

GType         sheet_control_corba_get_type  (void);
SheetControl *sheet_control_corba_new       (SheetView *view);

#endif /* GNUMERIC_SHEET_CONTROL_CORBA_H */
