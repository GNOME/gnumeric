#ifndef GNUMERIC_SHEET_CONTROL_H
#define GNUMERIC_SHEET_CONTROL_H

#include "gnumeric.h"
#include <gtk/gtkobject.h>

#define SHEET_CONTROL_TYPE     (sheet_control_get_type ())
#define SHEET_CONTROL(obj)     (GTK_CHECK_CAST ((obj), SHEET_CONTROL_TYPE, SheetControl))
#define IS_SHEET_CONTROL(o)	  (GTK_CHECK_TYPE ((o), SHEET_CONTROL_TYPE))

GtkType sheet_control_get_type    (void);
void sheet_control_init_state  (SheetControl *sc);

#endif /* GNUMERIC_SHEET_CONTROL_H */
