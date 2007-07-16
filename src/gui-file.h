#ifndef GNUMERIC_GUI_FILE_H
#define GNUMERIC_GUI_FILE_H

#include "gui-gnumeric.h"

gboolean gui_file_save_as   (WorkbookControlGUI *wbcg, WorkbookView *wbv);
gboolean gui_file_save      (WorkbookControlGUI *wbcg, WorkbookView *wbv);
void     gui_file_open      (WorkbookControlGUI *wbcg, 
			     char const *default_format);
void     gui_wb_view_show   (WorkbookControlGUI *wbcg, WorkbookView *wbv);
gboolean gui_file_read	    (WorkbookControlGUI *wbcg, char const *file_name,
			     GOFileOpener const *optional_format,
			     gchar const *optional_encoding);

#endif /* GNUMERIC_GUI_FILE_H */
