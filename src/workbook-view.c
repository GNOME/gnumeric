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
#include "history.h"
#include "workbook-private.h"
#include "gnumeric-util.h"
#include "application.h"
#include "sheet.h"
#include "widgets/gtk-combo-stack.h"

void
workbook_view_set_paste_special_state (Workbook *wb, gboolean enable)
{
	g_return_if_fail (wb != NULL);

#ifndef ENABLE_BONOBO
	gtk_widget_set_sensitive (
		wb->priv->menu_item_paste_special, enable);
#else
	/* FIXME : How to avoid hard coding the menu name here. */
	bonobo_ui_handler_menu_set_sensitivity (wb->priv->uih,
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
	bonobo_ui_handler_menu_set_sensitivity (wb->priv->uih, path, suffix != NULL);
#endif

	if (suffix == NULL)
		suffix = _("Nothing");

	/* Limit the size of the descriptor to 30 characters */
	text = g_strdup_printf ("%s : %s", prefix, suffix);
#ifndef ENABLE_BONOBO
	gtk_label_set_text (label, text);
#else
	bonobo_ui_handler_menu_set_label (wb->priv->uih, path, text);
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
workbook_view_push_undo (Workbook const * const wb,
			char const * const cmd_text)
{
	gtk_combo_stack_push_item (GTK_COMBO_STACK (wb->priv->undo_combo),
				   cmd_text);
}

void
workbook_view_pop_undo (Workbook const * const wb)
{
	gtk_combo_stack_remove_top (GTK_COMBO_STACK (wb->priv->undo_combo), 1);
}

void
workbook_view_clear_undo (Workbook const * const wb)
{
	gtk_combo_stack_clear (GTK_COMBO_STACK (wb->priv->undo_combo));
}

void
workbook_view_push_redo (Workbook const * const wb,
			char const * const cmd_text)
{
	gtk_combo_stack_push_item (GTK_COMBO_STACK (wb->priv->redo_combo),
				   cmd_text);
}

void
workbook_view_pop_redo (Workbook const * const wb)
{
	gtk_combo_stack_remove_top (GTK_COMBO_STACK (wb->priv->redo_combo), 1);
}

void
workbook_view_clear_redo (Workbook const * const wb)
{
	gtk_combo_stack_clear (GTK_COMBO_STACK (wb->priv->redo_combo));
}

void
workbook_view_set_size (Workbook const * const wb,
			int width_pixels,
			int height_pixels)
{
	int const screen_width = gdk_screen_width ();
	int const screen_height = gdk_screen_height ();

	/* FIXME : This should really be sizing the notebook */
	gtk_window_set_default_size (GTK_WINDOW (workbook_get_toplevel
						 ((Workbook *) wb)),
				     MIN (screen_width - 64, width_pixels),
				     MIN (screen_height - 64, height_pixels));
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

 	gtk_window_set_title (GTK_WINDOW (
		workbook_get_toplevel ((Workbook *) wb)), full_title);
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
	g_hash_table_foreach (wb->sheet_hash_private,
			      &cb_update_sheet_view_prefs, NULL);
	gtk_notebook_set_show_tabs (GTK_NOTEBOOK (wb->notebook),
				    wb->show_notebook_tabs);
}

void
workbook_view_history_setup (Workbook *wb)
{
	GList *hl;

	hl = application_history_get_list ();

	if (hl)
		history_menu_setup (wb, hl);
}

/* 
 * We introduced numbers in front of the the history file names for two
 * reasons:  
 * 1. Bonobo won't let you make 2 entries with the same label in the same
 *    menu. But that's what happens if you e.g. access worksheets with the
 *    same name from 2 different directories.
 * 2. The numbers are useful accelerators.
 * 3. Excel does it this way.
 *
 * Because numbers are reassigned with each insertion, we have to remove all
 * the old entries and insert new ones.
 */
void
workbook_view_history_update (GList *wl, gchar *filename)
{
	gchar   *del_name;
	gboolean add_sep;
	GList   *hl;

	/* Get the history list */
	hl = application_history_get_list ();

	/* If List is empty, a separator will be needed too. */
	add_sep = (hl == NULL);

	if (hl && strcmp ((gchar *)hl->data, filename) == 0)
		/* Do nothing if filename already at head of list */
		return;

	history_menu_flush (wl, hl); /* Remove the old entries */

	/* Update the history list */
	del_name = application_history_update_list (filename);
	g_free (del_name);

	/* Fill the menus */
	hl = application_history_get_list ();
	history_menu_fill (wl, hl, add_sep);
}

/*
 * This function will be used by the options dialog when the list size is
 * reduced by the user.
 */
void
workbook_view_history_shrink (GList *wl, gint new_max)
{
	GList *hl;
	gint   length;
	gchar *del_name;

	/* Check if the list needs to be shrunk. */
	hl = application_history_get_list ();
	length = g_list_length (hl);

	if (length <= new_max)
		return;

	history_menu_flush (wl, hl); /* Remove the old entries */
	for (; length > new_max; length--) {
		del_name = application_history_list_shrink ();
		g_free (del_name);
	}
	hl = application_history_get_list ();
	history_menu_fill (wl, hl, FALSE);	
}
