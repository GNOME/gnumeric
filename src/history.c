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

/*
 * Returns a newly allocated string containing the base name of the file 
 * with any .gnumeric extension stripped off.
 */ 
static gchar*
history_item_label (const gchar *name, const gint accel_number)
{
	int   i;
	char *label = g_strdup_printf ("_%d %s", accel_number,
					  g_basename (name));

	for (i = strlen (label) - 1; i >= 0; i--)
		if (label [i] == '.') {
			if (strcmp (label + i, ".gnumeric") == 0)
				label [i] = '\0';
			break;
		}

	return label;
}

/*
 * Create a history menu item in a menu at a given position.
 */
static void
history_menu_item_create (Workbook *wb, gchar *name, gint accel_number,
			  GtkWidget *menu, gint pos)
{
	GnomeUIInfo info[] = {
		{ GNOME_APP_UI_ITEM, "", name, file_history_cmd, wb },
		GNOMEUIINFO_END
	};

	info [0].label = history_item_label (name, accel_number);

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

typedef struct {
	GtkWidget *menu;
	gint      pos;
} MenuPos;

/*
 * Locate the menu and position of history separator. They may not yet exist.
 *
 * NOTE:
 * gnome_app_find_menu_pos returns the position *after* the menu item given.
 * bonobo_ui_handler_menu_get_pos returns the position *of* the menu item.
 */
static MenuPos
history_menu_locate_separator (Workbook *wb)
{
	MenuPos ret;
	
#ifndef ENABLE_BONOBO
	ret.menu = gnome_app_find_menu_pos (GNOME_APP (wb->toplevel)->menubar, 
					    _("File/Print preview"), &ret.pos);
#else
	ret.menu = NULL;
	ret.pos = bonobo_ui_handler_menu_get_pos (wb->uih,
						   "/File/Print preview");
	ret.pos++; /* see NOTE above */
#endif
	return ret;
}

/*
 * Insert the history separator. Return its position.
 */
static MenuPos
history_menu_insert_separator (Workbook *wb)
{
	MenuPos ret;
	
#ifndef ENABLE_BONOBO
	GtkWidget *item;

	ret.menu = gnome_app_find_menu_pos (GNOME_APP (wb->toplevel)->menubar, 
					    _("File/Print preview"), &ret.pos);
	item = gtk_menu_item_new ();
	gtk_widget_show (item);
	gtk_menu_shell_insert (GTK_MENU_SHELL (ret.menu), item, ret.pos);
#else
	ret = history_menu_locate_separator (wb);
	bonobo_ui_handler_menu_new_separator (wb->uih, "/File/histsep",
					      ret.pos);
#endif
	return ret;
}

/*
 * Add the history items list to the file menu for the workbook specified.
 */
static void
history_menu_insert_items (Workbook *wb, MenuPos *mp, GList *name_list)
{
	gint  accel_number;
	GList *l;

	g_return_if_fail (name_list != NULL);

	/* Add a new menu item for each item in the history list */
	accel_number = 1;
	for (l = name_list; l; l = l->next)
		history_menu_item_create (wb, (gchar *)l->data, accel_number++,
					  mp->menu, (mp->pos)++);
}

/*
 * Remove the history list items from the file menu of the workbook
 * specified.
 */
static void
history_menu_remove_items (Workbook *wb, GList *name_list)
{
	gint  accel_number = 1;
#ifdef ENABLE_BONOBO
	GList *l;
#endif
	gchar *label, *path;

#ifndef ENABLE_BONOBO
	label = history_item_label ((gchar *)name_list->data, accel_number);
	path = g_strconcat (_("File/"), label, NULL);
	gnome_app_remove_menus (GNOME_APP (wb->toplevel), path,
				g_list_length (name_list));
#else
	for (l = name_list; l; l = l->next) {
		label = history_item_label ((gchar *)l->data, accel_number++);
		path = g_strconcat ("/File/", label, NULL);
		bonobo_ui_handler_menu_remove (wb->uih, path);
		g_free (label);
		g_free (path);
	}
#endif
}
	
/*
 * Add the items contained in name list to the file menu for the app
 * specified.
 */
void
history_menu_setup (Workbook *wb, GList *name_list)
{
	MenuPos   mp;

	g_return_if_fail (name_list != NULL);

	/* Insert separator and get its position */
	mp = history_menu_insert_separator (wb);
	(mp.pos)++;
	/* Insert the items */
	history_menu_insert_items (wb, &mp, name_list);
}

/*
 * Make the menus for the workbooks in wl show the new history list. If this is
 * the first history item, need_sep should be set to cause a separator to be
 * added as well.
 *
 * Precondition - the old entries have been removed with history_menu_flush.
 */
void
history_menu_fill (GList *wl, GList *name_list,  gboolean need_sep)
{ 
	GList *l;
	
	/* Update the menus in all open workbooks */
	for (l = wl; l; l = g_list_next (l)) {
		Workbook  *wb = WORKBOOK (l->data);
		MenuPos mp;

		if (need_sep)
			history_menu_setup (wb, name_list);
		else {
			mp = history_menu_locate_separator (wb);
			(mp.pos)++;
			/* Insert the items */
			history_menu_insert_items (wb, &mp, name_list);
		}		
	}
}

/*
 * Remove the history list items from the file menus of all workbooks.
 *
 * All changes require that the list is removed and a new one built, because we
 * add a digit in front of the file name.
 */
void
history_menu_flush (GList *wl, GList *name_list)
{
	GList *l;

	/* Update the menus in all open workbooks */
	for (l = wl; l; l = g_list_next (l)) {
		Workbook  *wb = WORKBOOK (l->data);

		history_menu_remove_items (wb, name_list);
	}
}
