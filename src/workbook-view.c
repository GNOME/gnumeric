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
	/* FIXME : How to avoid hard coding the menu name here. */
	gnome_ui_handler_menu_set_sensitivity (wb->uih,
		"/Edit/Paste Special...", enable);
#endif
}

static void
change_menu_label (
#ifndef ENABLE_BONOBO
		   GtkWidget *menu_item,
#else
		   Workbook const * const wb,
		   char const * const path,
#endif
		   char const * const prefix,
		   char const * suffix)
{
	gchar    *text;
#ifndef ENABLE_BONOBO
	GtkBin   *bin = GTK_BIN(menu_item);
	GtkLabel *label = GTK_LABEL(bin->child);

	g_return_if_fail (label != NULL);

	gtk_widget_set_sensitive (menu_item, suffix != NULL);
#else
	gnome_ui_handler_menu_set_sensitivity (wb->uih, path, suffix != NULL);
#endif

	if (suffix == NULL)
		suffix = _("Nothing");

	/* Limit the size of the descriptor to 30 characters */
	text = g_strdup_printf ("%s : %s", prefix, suffix);
#ifndef ENABLE_BONOBO
	gtk_label_set_text (label, text);
#else
	gnome_ui_handler_menu_set_label (wb->uih, path, text);
#endif
	g_free (text);
}

void
workbook_view_set_undo_redo_state (Workbook const * const wb,
				   char const * const undo_suffix,
				   char const * const redo_suffix)
{
	g_return_if_fail (wb != NULL);

#ifndef ENABLE_BONOBO
	change_menu_label (wb->priv->menu_item_undo, _("Undo"), undo_suffix);
	change_menu_label (wb->priv->menu_item_redo, _("Redo"), redo_suffix);
#else
	/* FIXME : How to avoid hard coding the menu name here. */
	change_menu_label (wb, "/Edit/Undo", _("Undo"), undo_suffix);
	change_menu_label (wb, "/Edit/Redo", _("Redo"), redo_suffix);
#endif
}

