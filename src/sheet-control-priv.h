#ifndef GNUMERIC_SHEET_CONTROL_PRIV_H
#define GNUMERIC_SHEET_CONTROL_PRIV_H

#include "sheet-control.h"

struct _SheetControl {
	GtkObject object;
};

typedef struct {
	GtkObjectClass   object_class;

	void (*init_state) (SheetControl *sc);
} SheetControlClass;

#define SHEET_CONTROL_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), SHEET_CONTROL_TYPE, SheetControlClass))

#endif /* GNUMERIC_SHEET_CONTROL_PRIV_H */
