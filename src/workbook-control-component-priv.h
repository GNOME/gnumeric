#ifndef GNUMERIC_WORKBOOK_CONTROL_COMPONENT_PRIV_H
#define GNUMERIC_WORKBOOK_CONTROL_COMPONENT_PRIV_H

#include "gnumeric.h"
#include "workbook-control-component.h"
#include "workbook-control-gui-priv.h"

#define WBC_KEY "WBC"

struct _WorkbookControlComponent {
	WorkbookControlGUI wb_control_gui;
	BonoboControl      *bcontrol;
};

typedef struct {
	WorkbookControlGUIClass   wb_control_gui_class;
} WorkbookControlComponentClass;

#endif /* GNUMERIC_WORKBOOK_CONTROL_COMPONENT_PRIV_H */
