#ifndef GNUMERIC_WORKBOOK_CONTROL_COMPONENT_H
#define GNUMERIC_WORKBOOK_CONTROL_COMPONENT_H

#include "workbook-control-gui.h"
#include "gui-gnumeric.h"

#define WORKBOOK_CONTROL_COMPONENT_TYPE \
    (workbook_control_component_get_type ())
#define WORKBOOK_CONTROL_COMPONENT(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), WORKBOOK_CONTROL_COMPONENT_TYPE, \
    WorkbookControlComponent))
#define WORKBOOK_CONTROL_COMPONENT_CLASS(k) \
    (G_TYPE_CHECK_CLASS_CAST ((k), WORKBOOK_CONTROL_COMPONENT_TYPE, \
    WorkbookControlClassCOMPONENT))
#define IS_WORKBOOK_CONTROL_COMPONENT(o) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((o), WORKBOOK_CONTROL_COMPONENT_TYPE))

GType            workbook_control_component_get_type  (void);
WorkbookControl *
workbook_control_component_new (WorkbookView *optional_view,
				Workbook *optional_wb);
#endif /* GNUMERIC_WORKBOOK_CONTROL_COMPONENT_H */
