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
	bonobo_ui_handler_menu_set_sensitivity (wb->uih,
		"/Edit/Paste special...", enable);
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
	bonobo_ui_handler_menu_set_sensitivity (wb->uih, path, suffix != NULL);
#endif

	if (suffix == NULL)
		suffix = _("Nothing");

	/* Limit the size of the descriptor to 30 characters */
	text = g_strdup_printf ("%s : %s", prefix, suffix);
#ifndef ENABLE_BONOBO
	gtk_label_set_text (label, text);
#else
	bonobo_ui_handler_menu_set_label (wb->uih, path, text);
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

void
workbook_view_set_size (Workbook const * const wb,
			int width_in_points,
			int height_in_points)
{
	int const screen_width = gdk_screen_width ();
	int const screen_height = gdk_screen_height ();

	printf ("%dx%d mm, %dx%d pixels\n",
		gdk_screen_width_mm (),
		gdk_screen_height_mm (),
		gdk_screen_width (),
		gdk_screen_height ());
	gtk_window_set_default_size (GTK_WINDOW (wb->toplevel),
				     MIN (screen_width - 64, width_in_points),
				     MIN (screen_height - 64, height_in_points));
}

/**
 * workbook_view_set_title:
 * @wb: the workbook to modify
 * @title: the title for the toplevel window
 *
 * Sets the toplevel window title of @wb to be @title
 */
void
workbook_view_set_title (Workbook const * const wb,
			 char const * const title)
{
	char *full_title;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (title != NULL);

	full_title = g_strconcat (title, _(" : Gnumeric"), NULL);

 	gtk_window_set_title (GTK_WINDOW (wb->toplevel), full_title);
	g_free (full_title);
}

static void
cb_update_sheet_view_prefs (gpointer key, gpointer value, gpointer user_data)
{
	Sheet *sheet = value;
	sheet_adjust_preferences (sheet);
}

void
workbook_view_pref_visibility (Workbook const * const wb)
{
	g_hash_table_foreach (wb->sheets, &cb_update_sheet_view_prefs, NULL);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (wb->notebook),
				    wb->show_notebook_tabs);
}
