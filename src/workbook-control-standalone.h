#ifndef GNUMERIC_WORKBOOK_CONTROL_STANDALONE_H
#define GNUMERIC_WORKBOOK_CONTROL_STANDALONE_H

#include "workbook-control-gui.h"
#include "gui-gnumeric.h"

#define WORKBOOK_CONTROL_STANDALONE_TYPE \
    (workbook_control_standalone_get_type ())
#define WORKBOOK_CONTROL_STANDALONE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), WORKBOOK_CONTROL_STANDALONE_TYPE, \
    WorkbookControlStandalone))
#define WORKBOOK_CONTROL_STANDALONE_CLASS(k) \
    (G_TYPE_CHECK_CLASS_CAST ((k), WORKBOOK_CONTROL_STANDALONE_TYPE, \
    WorkbookControlClassSTANDALONE))
#define IS_WORKBOOK_CONTROL_STANDALONE(o) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((o), WORKBOOK_CONTROL_STANDALONE_TYPE))

GType            workbook_control_standalone_get_type  (void);

#endif /* GNUMERIC_WORKBOOK_CONTROL_STANDALONE_H */
