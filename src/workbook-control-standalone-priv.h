#ifndef GNUMERIC_WORKBOOK_CONTROL_STANDALONE_PRIV_H
#define GNUMERIC_WORKBOOK_CONTROL_STANDALONE_PRIV_H

#include "gnumeric.h"
#include "workbook-control-standalone.h"
#include "workbook-control-gui-priv.h"

struct _WorkbookControlStandalone {
	WorkbookControlGUI wb_control_gui;
};

typedef struct {
	WorkbookControlGUIClass   wb_control_gui_class;
} WorkbookControlStandaloneClass;

#endif /* GNUMERIC_WORKBOOK_CONTROL_STANDALONE_PRIV_H */
