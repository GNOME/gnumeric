#ifndef GNUMERIC_GUI_CLIPBOARD_H
#define GNUMERIC_GUI_CLIPBOARD_H

#include "workbook-control-gui.h"
#include <gtk/gtkwindow.h>

int  x_clipboard_bind_workbook (WorkbookControlGUI *wbcg);
void x_request_clipboard (WorkbookControlGUI *wbcg,
			  PasteTarget const *pt, guint32 time);

#endif /* GNUMERIC_GUI_CLIPBOARD_H */
