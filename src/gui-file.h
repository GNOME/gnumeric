#ifndef GNUMERIC_GUI_FILE_H
#define GNUMERIC_GUI_FILE_H

#include "gui-gnumeric.h"

gboolean gui_file_save_as   (WorkbookControlGUI *wbcg, WorkbookView *);
gboolean gui_file_save      (WorkbookControlGUI *wbcg, WorkbookView *);
void     gui_file_open      (WorkbookControlGUI *wbcg, 
			     char const *default_format);
gboolean gui_file_read	    (WorkbookControlGUI *wbcg, char const *file_name,
			     GnmFileOpener const *optional_format,
			     gchar const *optional_encoding);
char *   gui_image_file_select (WorkbookControlGUI *wbcg, const char *initial);

#endif /* GNUMERIC_GUI_FILE_H */
