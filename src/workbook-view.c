#include <config.h>
#include "workbook-view.h"

CmdContext *
command_context_gui (void)
{
	return NULL;
}

void
workbook_view_set_paste_state (Workbook *wb, int const state)
{
	g_return_if_fail (wb != NULL);
	g_return_if_fail (state >= 0);
	g_return_if_fail (state <= 2);

	/* FIXME : how to get these in the bonobo case */
	if (wb->menu_item_paste != NULL)
		gtk_widget_set_sensitive (wb->menu_item_paste, state > 0);
	if (wb->menu_item_paste_special != NULL)
		gtk_widget_set_sensitive (wb->menu_item_paste_special, state > 1);
}
