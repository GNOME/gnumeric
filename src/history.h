#ifndef GNUMERIC_HISTORY_H
#define GNUMERIC_HISTORY_H

#include <gnome.h>
#include "workbook.h"

void history_menu_setup       (Workbook *, GList *);
void history_insert_menu_item (GList *, gchar *, gboolean);
void history_remove_menu_item (GList *, gchar *);

#endif
