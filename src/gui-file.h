#ifndef GNUMERIC_GUI_FILE_H
#define GNUMERIC_GUI_FILE_H

#include "gnumeric.h"
#include "workbook-control-gui.h"

gboolean gui_file_save_as   (WorkbookControlGUI *wbcg, WorkbookView *);
gboolean gui_file_save      (WorkbookControlGUI *wbcg, WorkbookView *);
void     gui_file_import    (WorkbookControlGUI *wbcg);
void     gui_file_open      (WorkbookControlGUI *wbcg);

#endif /* GNUMERIC_GUI_FILE_H */
