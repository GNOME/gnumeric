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
#include "workbook-private.h"

#ifdef ENABLE_BONOBO
#include <bonobo.h>
#endif

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

	if (new_wb != NULL) {
		workbook_show (new_wb);

		if (workbook_is_pristine (wb))
			workbook_unref (wb);
	}
}

#else

static void
file_history_cmd (BonoboUIHandler *uih, Workbook *wb, const char *path)
{
#warning FIXME: broken.
/*	BonoboUIHandlerMenuItem *item;
	Workbook *new_wb;

	item = bonobo_ui_handler_menu_fetch_one	(uih, path);

	new_wb = workbook_read (workbook_command_context_gui (wb), item->hint);

	if (new_wb != NULL) {
		workbook_show (new_wb);

		if (workbook_is_pristine (wb))
			workbook_unref (wb);
	}*/
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
	char *label, *menuname, *tmp;

	/* Translate '_' to '-' so menu will not show underline.  */
	menuname = g_strdup (g_basename (name));
	for (tmp = menuname; *tmp; tmp++)
		if (*tmp == '_')
			*tmp = '-';
	label = g_strdup_printf ("_%d %s", accel_number, menuname);
	g_free (menuname);

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
#ifndef ENABLE_BONOBO
static void
history_menu_item_create (Workbook *wb, gchar *name, gint accel_number,
			  GtkWidget *menu, gint pos)
{
	GnomeUIInfo info[] = {
		{ GNOME_APP_UI_ITEM, NULL, NULL, file_history_cmd, NULL },
		GNOMEUIINFO_END
	};

	info [0].hint = name;
	info [0].label = history_item_label (name, accel_number);
	info [0].user_data = wb;

	gnome_app_fill_menu (GTK_MENU_SHELL (menu), info,
			     GNOME_APP (wb->toplevel)->accel_group, TRUE,
			     pos);
	gtk_object_set_data (GTK_OBJECT (info [0].widget), UGLY_GNOME_UI_KEY, name);
	gnome_app_install_menu_hints (GNOME_APP (wb->toplevel), info);

	g_free (info[0].label);
}
#endif

#ifndef ENABLE_BONOBO
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
/*
 * xgettext:
 * This string must translate to exactly the same strings as the
 * 'Print Preview' item in the
 * 'File' menu
 */
	char const * menu_name = _("File/Print preview");
	ret.menu = gnome_app_find_menu_pos (GNOME_APP (wb->toplevel)->menubar,
					    menu_name, &ret.pos);
	return ret;
}

/* Insert the history separator. Return its position.  */
static MenuPos
history_menu_insert_separator (Workbook *wb)
{
	MenuPos ret;

	GtkWidget *item;
	char const * menu_name = _("File/Print preview");

	ret.menu = gnome_app_find_menu_pos (GNOME_APP (wb->toplevel)->menubar,
					    menu_name, &ret.pos);
	if (ret.menu != NULL) {
		item = gtk_menu_item_new ();
		gtk_widget_show (item);
		gtk_menu_shell_insert (GTK_MENU_SHELL (ret.menu), item, ret.pos);
	} else
		g_warning ("Probable mis-translation. '%s' : was not found. "
			   "Does this match the 'File/Print preview' menu exactly ?",
			   menu_name);
	return ret;
}

/*
 * Add the history items list to the file menu for the workbook specified.
 */
static void
history_menu_insert_items (Workbook *wb, GList *name_list, MenuPos *mp)
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
#else
static void
history_menu_insert_items (Workbook *wb, GList *name_list)
{
	CORBA_Environment ev;
	gint  accel_number;
	GList *l;

	g_return_if_fail (name_list != NULL);

	CORBA_exception_init (&ev);

	/* Add a new menu item for each item in the history list */
	accel_number = 1;
	for (l = name_list; l; l = l->next) {
		char *str, *label;

		label = history_item_label (l->data, accel_number);
		str = g_strdup_printf ("<menuitem name=\"FileHistory%d\" label=\"%s\"/>\n",
				       accel_number, label);
		bonobo_ui_component_set (bonobo_ui_compat_get_component (wb->priv->uih),
					 bonobo_ui_compat_get_container (wb->priv->uih),
					 "/menu/File/FileHistory",
					 str, &ev);
		g_free (str);
		g_free (label);
		++accel_number;
	}
	CORBA_exception_free (&ev);
}
#endif

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
	CORBA_Environment ev;
#endif

#ifndef ENABLE_BONOBO
	if (name_list) {
		char *label, *path;

		label = history_item_label ((gchar *)name_list->data, accel_number);
		path = g_strconcat (_("File/"), label, NULL);
		gnome_app_remove_menus (GNOME_APP (wb->toplevel), path,
					g_list_length (name_list));
		g_free (label);
		g_free (path);
	}
#else
	CORBA_exception_init (&ev);
	for (l = name_list; l; l = l->next) {
		char *path;

		path = g_strdup_printf ("/menu/File/FileHistory%d", accel_number);
		bonobo_ui_component_rm (bonobo_ui_compat_get_component (wb->priv->uih),
					bonobo_ui_compat_get_container (wb->priv->uih),
					path, &ev);
		g_free (path);
	}
	CORBA_exception_free (&ev);
#endif
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
/*
 * Add the items contained in name list to the file menu for the app
 * specified.
 */
void
history_menu_setup (Workbook *wb, GList *name_list)
{
	g_return_if_fail (name_list != NULL);

#ifdef ENABLE_BONOBO
	/* Insert the items */
	history_menu_insert_items (wb, name_list);
#else
	{
	MenuPos   mp;

	/* Insert separator and get its position */
	mp = history_menu_insert_separator (wb);
	(mp.pos)++;
	/* Insert the items */
	history_menu_insert_items (wb, name_list, &mp);
	}
#endif
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

		if (need_sep)
			history_menu_setup (wb, name_list);
		else {
#ifndef ENABLE_BONOBO
			MenuPos mp;
			mp = history_menu_locate_separator (wb);
			(mp.pos)++;
			/* Insert the items */
			history_menu_insert_items (wb, name_list, &mp);
#else
			history_menu_insert_items (wb, name_list);
#endif
		}
	}
}
