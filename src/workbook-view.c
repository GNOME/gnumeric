/*
 * workbook-view.c: View functions for the workbook
 *
 * This is actually broken, as there is no such separation right now
 *
 * Authors:
 *   Jody Goldberg
 *   Miguel de Icaza
 */
#include <config.h>
#include "workbook-view.h"
#include "command-context.h"
#include "command-context-gui.h"
#include "workbook.h"
#include "workbook-private.h"
#include "gnumeric-util.h"

void
workbook_view_set_paste_special_state (Workbook *wb, gboolean enable)
{
	g_return_if_fail (wb != NULL);

#ifndef ENABLE_BONOBO
	gtk_widget_set_sensitive (
		wb->priv->menu_item_paste_special, enable);
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
	gtk_widget_set_sensitive (wb->priv->menu_item_undo, has_undos);
	gtk_widget_set_sensitive (wb->priv->menu_item_redo, has_redos);
#else
	/* gnome_ui_handler_menu_set_sensitivity (); */
#endif
}

