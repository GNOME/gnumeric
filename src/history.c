/*
 * History.c: Implements file menu file history
 *
 * Author:
 *  Mike Kestner (mkestner@ameritech.net)
 *
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "history.h"

#include "application.h"
#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include "workbook.h"

#include <gtk/gtkmenuitem.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-app-helper.h>
#ifdef WITH_BONOBO
#include <bonobo.h>
#endif

/* Command callback called on activation of a file history menu item. */
#ifndef WITH_BONOBO

#define UGLY_GNOME_UI_KEY "HistoryFilename"

static void
file_history_cmd (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	gchar *filename = gtk_object_get_data (GTK_OBJECT (widget), UGLY_GNOME_UI_KEY);
	(void) wb_view_open (wb_control_view (WORKBOOK_CONTROL (wbcg)),
	                     WORKBOOK_CONTROL (wbcg), filename, TRUE);
}

#else
static void
file_history_cmd (BonoboUIComponent *uic, WorkbookControlGUI *wbcg, const char *path)
{
	char *fullpath = g_strconcat ("/menu/File/FileHistory/", path, NULL);
	char *filename = bonobo_ui_component_get_prop (wbcg->uic, fullpath,
						       "tip", NULL);

	g_free (fullpath);
	(void) wb_view_open (wb_control_view (WORKBOOK_CONTROL (wbcg)),
	                     WORKBOOK_CONTROL (wbcg), filename, TRUE);
	g_free (filename);
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
#ifndef WITH_BONOBO
static void
history_menu_item_create (WorkbookControlGUI *wbcg, gchar *name, gint accel_number,
			  GtkWidget *menu, gint pos)
{
	char *label;
	GnomeUIInfo info[] = {
		{ GNOME_APP_UI_ITEM, NULL, NULL, file_history_cmd, NULL },
		GNOMEUIINFO_END
	};

	info [0].hint = name;
	info [0].label = label = history_item_label (name, accel_number);
	info [0].user_data = wbcg;

	gnome_app_fill_menu (GTK_MENU_SHELL (menu), info,
			     GNOME_APP (wbcg->toplevel)->accel_group, TRUE,
			     pos);
	gtk_object_set_data (GTK_OBJECT (info [0].widget), UGLY_GNOME_UI_KEY, name);
	gnome_app_install_menu_hints (GNOME_APP (wbcg->toplevel), info);

	g_free (label);
}
#endif

#ifndef WITH_BONOBO
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
static void
history_menu_locate_separator (WorkbookControlGUI *wbcg, MenuPos *res)
{
/*
 * xgettext:
 * This string must translate to exactly the same strings as the
 * 'Print Preview...' item in the
 * 'File' menu
 */
	char const *menu_name = _("_File/Print Pre_view...");
	res->menu = gnome_app_find_menu_pos (GNOME_APP (wbcg->toplevel)->menubar,
					    menu_name, &res->pos);
	if (res->menu == NULL)
		g_warning ("Probable mis-translation. '%s' : was not found. "
			   "Does this match the '_File/Print Pre_view...' menu exactly ?",
			   menu_name);
}

/* Insert the history separator. Return its position.  */
static void
history_menu_insert_separator (WorkbookControlGUI *wbcg, MenuPos *res)
{
	GtkWidget *item;

	history_menu_locate_separator (wbcg, res);
	if (res->menu != NULL) {
		item = gtk_menu_item_new ();
		gtk_widget_show (item);
		gtk_menu_shell_insert (GTK_MENU_SHELL (res->menu), item, res->pos);
		gtk_widget_set_sensitive(item, FALSE);
	}
}

/*
 * Add the history items list to the file menu for the workbook specified.
 */
static void
history_menu_insert_items (WorkbookControlGUI *wbcg, GList *name_list, MenuPos *mp)
{
	gint  accel_number;
	GList *l;

	g_return_if_fail (name_list != NULL);

	/* Add a new menu item for each item in the history list */
	accel_number = 1;
	for (l = name_list; l; l = l->next)
		history_menu_item_create (wbcg, (gchar *)l->data, accel_number++,
					  mp->menu, (mp->pos)++);
}
#else
static void
history_menu_insert_items (WorkbookControlGUI *wbcg, GSList *name_list)
{
	CORBA_Environment ev;
	gint  accel_number;
	GSList *l;

	g_return_if_fail (name_list != NULL);

	CORBA_exception_init (&ev);

	/* Add a new menu item for each item in the history list */
	accel_number = 1;
	bonobo_ui_component_freeze (wbcg->uic, &ev);
	for (l = name_list; l; l = l->next) {
		char *id, *str, *label, *filename;

		id = g_strdup_printf ("FileHistory%d", accel_number);
		str = history_item_label (l->data, accel_number);
		label = bonobo_ui_util_encode_str (str);
		g_free (str);
		filename = bonobo_ui_util_encode_str ((char *) l->data);
		str = g_strdup_printf ("<menuitem name=\"%s\" "
				       "verb=\"%s\" "
				       "label=\"%s\" "
				       "tip=\"%s\"/>\n",
				       id, id, label, filename);
		bonobo_ui_component_set (wbcg->uic,
					 "/menu/File/FileHistory", str, &ev);
		bonobo_ui_component_add_verb (
			wbcg->uic, id, (BonoboUIVerbFn) file_history_cmd, wbcg);
		g_free (id);
		g_free (str);
		g_free (filename);
		g_free (label);
		accel_number++;
	}
	bonobo_ui_component_thaw (wbcg->uic, &ev);
	CORBA_exception_free (&ev);
}
#endif

#ifndef WITH_BONOBO
/*
 * Remove the history list items from the file menu of the workbook
 * specified.
 */
static void
history_menu_remove_items (WorkbookControlGUI *wbcg, GSList *name_list)
{
	gint  accel_number = 1;

	if (name_list) {
		char *label, *path;

		label = history_item_label ((gchar *)name_list->data, accel_number);
		path = g_strconcat (_("File/"), label, NULL);
		gnome_app_remove_menus (GNOME_APP (wbcg->toplevel), path,
					g_slist_length (name_list));
		g_free (label);
		g_free (path);
	}
}

#else
/*
 * Remove the history list items from the file menu of the workbook
 * specified.
 */
static void
history_menu_remove_items (WorkbookControlGUI *wbcg, GSList *name_list)
{
	gint  accel_number = 1;
	GSList *l;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	bonobo_ui_component_freeze (wbcg->uic, &ev);
	for (l = name_list; l; l = l->next) {
		char *path;

		path = g_strdup_printf ("/menu/File/FileHistory/FileHistory%d",
					accel_number++);
		bonobo_ui_component_rm (wbcg->uic,
					path, &ev);
		g_free (path);
	}
	bonobo_ui_component_thaw (wbcg->uic, &ev);
	CORBA_exception_free (&ev);
}
#endif

/*
 * Remove the history list items from the file menus of all workbooks.
 *
 * All changes require that the list is removed and a new one built, because we
 * add a digit in front of the file name.
 */
void
history_menu_flush (GList *wl, GSList *name_list)
{
	GList *l;

	/* Update the menus in all open workbooks */
	for (l = wl; l; l = g_list_next (l)) {
		Workbook  *wb = WORKBOOK (l->data);

		WORKBOOK_FOREACH_CONTROL (wb, view, control,
		{
			/* ICK : Should this be virtual ?
			 * seems too specific to do that.
			 * kludge for now
			 */
			if (IS_WORKBOOK_CONTROL_GUI (control))
				history_menu_remove_items (WORKBOOK_CONTROL_GUI (control), name_list);
		});
	}
}
/*
 * Add the items contained in name list to the file menu for the app
 * specified.
 */
void
history_menu_setup (WorkbookControlGUI *wbcg, GSList *name_list)
{
	g_return_if_fail (name_list != NULL);

#ifdef WITH_BONOBO
	/* Insert the items */
	history_menu_insert_items (wbcg, name_list);
#else
	{
	MenuPos   mp;

	/* Insert separator and get its position */
	history_menu_insert_separator (wbcg, &mp);
	(mp.pos)++;
	/* Insert the items */
	history_menu_insert_items (wbcg, name_list, &mp);
	}
#endif
}

/*
 * A quick utility routine.  This could be a virtual in wb-control.
 * but the code is too messy to both with just now.
 */
static void
history_control_fill (WorkbookControl *control, GSList *name_list, gboolean need_sep)
{
	if (IS_WORKBOOK_CONTROL_GUI (control)) {
		WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (control);
		if (need_sep)
			history_menu_setup (wbcg, name_list);
		else {
#ifndef WITH_BONOBO
			MenuPos mp;
			history_menu_locate_separator (wbcg, &mp);
			(mp.pos)++;
			/* Insert the items */
			history_menu_insert_items (wbcg, name_list, &mp);
#else
			history_menu_insert_items (wbcg, name_list);
#endif
		}
	}
}
/*
 * Make the menus for the workbooks in wl show the new history list. If this is
 * the first history item, need_sep should be set to cause a separator to be
 * added as well.
 *
 * Precondition - the old entries have been removed with history_menu_flush.
 */
void
history_menu_fill (GList *wl, GSList *name_list,  gboolean need_sep)
{
	GList *l;

	/* Update the menus in all open workbooks */
	for (l = wl; l; l = g_list_next (l)) {
		Workbook *wb = WORKBOOK (l->data);
		WORKBOOK_FOREACH_CONTROL (wb, view, control,
			history_control_fill (control, name_list, need_sep););
	}
}
