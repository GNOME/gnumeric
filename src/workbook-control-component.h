#ifndef GNUMERIC_WORKBOOK_CONTROL_COMPONENT_H
#define GNUMERIC_WORKBOOK_CONTROL_COMPONENT_H

#include "workbook-control-gui.h"
#include "gui-gnumeric.h"
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-zoomable.h>

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

double wbcc_get_zoom_factor (WorkbookControlComponent *wbcc);
void   wbcc_set_zoom_factor (WorkbookControlComponent *wbcc,
			     double new_zoom_factor);

void wbcc_set_bcontrol (WorkbookControlComponent *wbcc,
			BonoboControl *control);
void wbcc_set_zoomable (WorkbookControlComponent *wbcc,
			BonoboZoomable *zoomable);

WorkbookControl *
workbook_control_component_new (WorkbookView *optional_view,
				Workbook *optional_wb);

void
workbook_control_component_activate (WorkbookControlComponent *wbcc,
				     Bonobo_UIContainer ui_container);
#endif /* GNUMERIC_WORKBOOK_CONTROL_COMPONENT_H */
