#ifndef GNUMERIC_GUI_FILE_H
#define GNUMERIC_GUI_FILE_H

#include "gui-gnumeric.h"
#ifdef WITH_BONOBO
#include <bonobo/bonobo-stream.h>
#endif

gboolean gui_file_save_as   (WorkbookControlGUI *wbcg, WorkbookView *);
gboolean gui_file_save      (WorkbookControlGUI *wbcg, WorkbookView *);
void     gui_file_import    (WorkbookControlGUI *wbcg);
void     gui_file_open      (WorkbookControlGUI *wbcg);

#ifdef WITH_BONOBO
void gui_file_save_to_stream (BonoboStream *stream, WorkbookControlGUI *wbcg,
		              WorkbookView *wb_view, const gchar *mime_type,
			      CORBA_Environment *ev);
#endif

#endif /* GNUMERIC_GUI_FILE_H */
