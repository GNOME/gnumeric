#ifndef GNUMERIC_SHEET_CONTROL_CORBA_H
#define GNUMERIC_SHEET_CONTROL_CORBA_H

#include "sheet-control.h"


#define SHEET_TYPE_CONTROL        (sheet_control_get_type ())
#define SHEET_CONTROL(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), SHEET_TYPE_CONTROL, SheetControl))
#define SHEET_CONTROL_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), SHEET_TYPE_CONTROL, SheetControlClass))
#define SHEET_IS_CONTROL(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), SHEET_TYPE_CONTROL))
#define SHEET_IS_CONTROL_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), SHEET_TYPE_CONTROL))

typedef struct _SheetControlPrivate SheetControlPrivate;

struct _SheetControl {
	BonoboObject base;

	/*
	 *  Of course, proxy objects are a painful pile of
	 * typing, but then using CORBA types internally is
	 * perhaps harder ?
	 */
	SheetView *view;
};

typedef struct {
	BonoboObjectClass      parent_class;

	POA_GNOME_Gnumeric_Sheet__epv epv;
} SheetControlClass;

GType         sheet_control_corba_get_type (void);
SheetControl *sheet_control_corba_new      (SheetView *view);

#endif /* GNUMERIC_SHEET_CONTROL_CORBA_H */
