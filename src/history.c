/*
 * History.c: Implements file menu file history
 *
 * Author:
 *  Mike Kestner (mkestner@ameritech.net)
 *
 */
#include <config.h>
#include <gnome.h>

#include "application.h"
#include "history.h"
#include "workbook.h"

/* Command callback called on activation of a file history menu item. */
#ifndef ENABLE_BONOBO

#define UGLY_GNOME_UI_KEY "HistoryFilename"

static void
file_history_cmd (GtkWidget *widget, Workbook *wb)
{
	Workbook *new_wb;
	gchar    *filename;

	filename = gtk_object_get_data (GTK_OBJECT (widget), UGLY_GNOME_UI_KEY);

	new_wb = workbook_read (workbook_command_context_gui (wb), filename);

	if (new_wb != NULL)
		gtk_widget_show (new_wb->toplevel);
}

#else

static void
file_history_cmd (BonoboUIHandler *uih, Workbook *wb, const char *path)
{
	BonoboUIHandlerMenuItem *item;
	Workbook *new_wb;

	item = bonobo_ui_handler_menu_fetch_one	(uih, path);

	new_wb = workbook_read (workbook_command_context_gui (wb), item->hint);

	if (new_wb != NULL) {
		gtk_widget_show (new_wb->toplevel);

		if (workbook_is_pristine (wb))
			bonobo_object_unref (BONOBO_OBJECT (wb));
	}
}
#endif

/* Returns a newly allocated string containing the base name of the file 
 * with any .gnumeric extension stripped off. */ 
static gchar*
history_item_label (const gchar *name)
{
	int   i;
	char *basename = g_strdup (g_basename (name));

	for (i = strlen (basename) - 1; i >= 0; i--)
		if (basename [i] == '.') {
			basename [i] = '\0';
			break;
		}

	return basename;
}

/* Create a history menu item in a menu at a given position. */
static void
history_menu_item_create (Workbook *wb, gchar *name, GtkWidget *menu, gint pos)
{
	GnomeUIInfo info[] = {
		{ GNOME_APP_UI_ITEM, "", name, file_history_cmd, wb },
		GNOMEUIINFO_END
	};

	info [0].label = history_item_label (name);

#ifndef ENABLE_BONOBO
	gnome_app_fill_menu (GTK_MENU_SHELL (menu), info, NULL, FALSE, pos);
	gtk_object_set_data (GTK_OBJECT (info [0].widget), UGLY_GNOME_UI_KEY, name);
	gnome_app_install_menu_hints (GNOME_APP (wb->toplevel), info);
#else
	{
		gchar *path;

		path = g_strconcat (_("/File/"), info[0].label, NULL);
		bonobo_ui_handler_menu_new_item (wb->uih, path, info[0].label, name, 
						pos, BONOBO_UI_HANDLER_PIXMAP_NONE, 
						NULL, 0, GDK_SHIFT_MASK, 
						(BonoboUIHandlerCallbackFunc)file_history_cmd, 
						wb);
		g_free (path);
	}
#endif

	g_free (info[0].label);
}

/* Adds the items contained in name list to the file menu for the app specified. */
void
history_menu_setup (Workbook *wb, GList *name_list)
{
	GtkWidget *menu = NULL;
	gint   pos;
	GList *l;

	g_return_if_fail (name_list != NULL);

	/* Get menu and position information for new items and add separator. */
#ifndef ENABLE_BONOBO
	{
		GtkWidget *item;
		menu = gnome_app_find_menu_pos (GNOME_APP (wb->toplevel)->menubar, 
						_("File/Print preview"), &pos);
		item = gtk_menu_item_new ();
		gtk_widget_show (item);
		gtk_menu_shell_insert (GTK_MENU_SHELL (menu), item, pos++);
	}
#else
	pos = bonobo_ui_handler_menu_get_pos (wb->uih, "/File/Close");
	bonobo_ui_handler_menu_new_separator (wb->uih, "/File/histsep", pos++);
#endif

	/* Add a new menu item for each item in the history list */
	for (l = name_list; l; l = l->next)
		history_menu_item_create (wb, (gchar *)l->data, menu, pos++);
}

/* Update the menus for the workbooks in wl by adding a menu item for the
 * add_name. If this is the first history item, need_sep should be set to cause a 
 * separator to be added as well. */
void
history_insert_menu_item (GList *wl, gchar *add_name, gboolean need_sep)
{ 
	GList *l;

	/* Update the menus in all open workbooks */
	for (l = wl; l; l = g_list_next (l)) {
		Workbook  *wb = WORKBOOK (l->data);
		GtkWidget *menu = NULL;
		gint       pos;

#ifndef ENABLE_BONOBO
		/* Get menu and position */
		menu = gnome_app_find_menu_pos (GNOME_APP (wb->toplevel)->menubar,
						_("File/Print preview"), &pos);

		/* Check if separator is needed and insert one. */
		if (need_sep) {
			GtkWidget *item;
			item =	gtk_menu_item_new ();
			gtk_widget_show (item);
			gtk_menu_shell_insert (GTK_MENU_SHELL (menu), item, pos++);
		} else
			pos++;
#else
		/* Check if separator is needed and insert one. */
		if (need_sep) {
			pos = bonobo_ui_handler_menu_get_pos (wb->uih, "/File/Close");
			bonobo_ui_handler_menu_new_separator (wb->uih, "/File/histsep", pos++);
		} else
			pos = bonobo_ui_handler_menu_get_pos (wb->uih, "/File/histsep") + 1;
#endif
		history_menu_item_create (WORKBOOK (l->data), add_name,
					  menu, pos); 
	}
}

/* Remove the menu item associated with name from the workbook menus */
void
history_remove_menu_item (GList *wl, gchar *name)
{
	Workbook *wb;
	gchar *path, *label;

	g_return_if_fail (name != NULL);

	label = history_item_label (name);

#ifndef ENABLE_BONOBO
	path = g_strconcat ("File/", label, NULL);
#else
	path = g_strconcat (_("/File/"), label, NULL);
#endif

	g_free (label);

	while (wl)  {

		wb = (Workbook *)wl->data;

#ifndef ENABLE_BONOBO
		gnome_app_remove_menus (GNOME_APP (wb->toplevel), path, 1);
#else
		bonobo_ui_handler_menu_remove (wb->uih, path);
#endif

		wl = g_list_next (wl);
	}
	g_free (path);
}
