#ifndef GNUMERIC_FILE_GUI_H
#define GNUMERIC_FILE_GUI_H

#include "gnumeric.h"
#include "workbook-control-gui.h"

gboolean gui_file_save_as   (WorkbookControlGUI *wbcg, WorkbookView *);
gboolean gui_file_save      (WorkbookControlGUI *wbcg, WorkbookView *);
gboolean gui_file_import    (WorkbookControlGUI *wbcg, const char *fname);

#endif /* GNUMERIC_FILE_H */
