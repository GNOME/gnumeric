#include <config.h>
#include "workbook-view.h"
#include "command-context.h"
#include "command-context-gui.h"
#include "workbook.h"
#include "gnumeric-util.h"

/* enable/disable paste/paste_special
 * 0 = both disabled
 * 1 = paste enabled
 * 2 = both enabled
 */
void
workbook_view_set_paste_state (Workbook *wb, int const state)
{
	g_return_if_fail (wb != NULL);
	g_return_if_fail (state >= 0);
	g_return_if_fail (state <= 2);

#ifndef ENABLE_BONOBO
	gtk_widget_set_sensitive (wb->menu_item_paste, state > 0);
	gtk_widget_set_sensitive (wb->menu_item_paste_special, state > 1);
#else
	/* gnome_ui_handler_menu_set_sensitivity (); */
#endif
}

void
workbook_view_set_undo_redo_state (Workbook const * const wb,
				   gboolean const has_undos,
				   gboolean const has_redos)
{
	g_return_if_fail (wb != NULL);

#ifndef ENABLE_BONOBO
	gtk_widget_set_sensitive (wb->menu_item_undo, has_undos);
	gtk_widget_set_sensitive (wb->menu_item_redo, has_redos);
#else
	/* gnome_ui_handler_menu_set_sensitivity (); */
#endif
}

