#include <config.h>
#include "workbook-view.h"

void
workbook_view_set_paste_state (Workbook *wb, int const state)
{
	g_return_if_fail (wb != NULL);
	g_return_if_fail (state >= 0);
	g_return_if_fail (state <= 2);

	gtk_widget_set_sensitive (wb->menu_item_paste, state > 0);
	gtk_widget_set_sensitive (wb->menu_item_paste_special, state > 1);
}
