#ifndef GNUMERIC_WORKBOOK_FORMAT_TOOLBAR_H
#define GNUMERIC_WORKBOOK_FORMAT_TOOLBAR_H

#include "gnumeric.h"
#include "workbook-control-gui.h"

void workbook_create_format_toolbar (WorkbookControlGUI *wbcg);
void workbook_feedback_set (WorkbookControlGUI *wbc, MStyle *style);

#endif /* GNUMERIC_WORKBOOK_FORMAT_TOOLBAR_H */
