#ifndef GNUMERIC_HISTORY_H
#define GNUMERIC_HISTORY_H

#include "gnumeric.h"
#include "workbook-control-gui.h"

void history_menu_setup (WorkbookControlGUI *, GSList *);
void history_menu_fill  (GList *wl, GSList *name_list, gboolean need_sep);
void history_menu_flush (GList *wl, GSList *name_list);

#endif
