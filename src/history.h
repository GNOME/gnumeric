#ifndef GNUMERIC_HISTORY_H
#define GNUMERIC_HISTORY_H

#include <gnome.h>
#include "workbook.h"

void history_menu_setup (Workbook *, GList *);
void history_menu_fill  (GList *wl, GList *name_list, gboolean need_sep);
void history_menu_flush (GList *wl, GList *name_list);

#endif
