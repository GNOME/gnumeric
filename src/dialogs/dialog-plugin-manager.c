/*
 * plugin-manager.c: Dialog used to load plugins into the Gnumeric
 * spreadsheet
 *
 * Authors:
 *  Old plugin manager:
 *   Dom Lachowicz (dominicl@seas.upenn.edu)
 *   Tom Dyas (tdyas@romulus.rutgers.edu)
 *  New plugin manager:
 *   Zbigniew Chyla (cyba@gnome.pl)
 *   Andreas J. Guelzow (aguelzow@taliesin.ca)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gutils.h>
#include <gui-util.h>
#include <command-context.h>
#include <workbook-control.h>
#include <workbook.h>
#include <plugin.h>
#include <gnumeric-gconf.h>
#include <application.h>

#include <glade/glade.h>
#include <gsf/gsf-impl-utils.h>
#include <gal/util/e-util.h>

#include <libgnome/gnome-i18n.h>
#include <string.h>

typedef struct {
	WorkbookControlGUI *wbcg;
	GtkDialog *dialog_pm;
	GtkNotebook *gnotebook;
	GtkListStore  *model_plugins;
	GtkTreeView   *list_plugins;
	GtkTreeSelection   *selection;
	GtkButton *button_rescan_directories;
	GtkButton *button_directory_add, *button_directory_delete;
	GtkButton *button_activate_all, *button_deactivate_all;
	GtkCheckButton *checkbutton_install_new;
	GtkWidget *frame_mark_for_deactivation;
	GtkWidget *checkbutton_mark_for_deactivation;
	GtkEntry *entry_name, *entry_directory, *entry_id;
	GtkTextBuffer *text_description;
	GtkListStore  *model_extra_info;
	GtkListStore  *model_directories;
	GtkTreeView   *list_directories;
	GtkTreeSelection   *selection_directory;
	guint directories_changed_notification;
} PluginManagerGUI;

enum {
	PLUGIN_NAME,
	PLUGIN_ACTIVE,
	PLUGIN_SWITCHABLE,
	PLUGIN_POINTER,
	NUM_COLMNS
};
enum {
	EXTRA_NAME,
	EXTRA_VALUE,
	EXTRA_NUM_COLMNS
};
enum {
	DIR_NAME,
	DIR_IS_SYSTEM,
	DIR_NUM_COLMNS
};


static int
plugin_compare_name (gconstpointer a, gconstpointer b)
{
	GnmPlugin *plugin_a = (GnmPlugin *) a, *plugin_b = (GnmPlugin *) b;

	return strcoll (gnm_plugin_get_name (plugin_a),
	                gnm_plugin_get_name (plugin_b));
}

static gboolean
model_get_plugin_iter (GtkTreeModel *model, gpointer plugin, GtkTreeIter *ret_iter)
{
	gboolean has_iter;
      
	for (has_iter = gtk_tree_model_get_iter_first (model, ret_iter);
	     has_iter; has_iter = gtk_tree_model_iter_next (model, ret_iter)) {
		gpointer current;
	
		gtk_tree_model_get (model, ret_iter, PLUGIN_POINTER, &current, -1);
		if (current == plugin) {
			return TRUE;
		}
	}

	return FALSE;
}

static void
cb_plugin_changed (GnmPlugin *plugin, PluginManagerGUI *pm_gui)
{
	GtkTreeIter iter;

	if (model_get_plugin_iter (GTK_TREE_MODEL (pm_gui->model_plugins), plugin, &iter)) {
		gtk_list_store_set (
			pm_gui->model_plugins, &iter,
			PLUGIN_ACTIVE, gnm_plugin_is_active (plugin),
			PLUGIN_SWITCHABLE, !gnm_plugin_is_active (plugin) || gnm_plugin_can_deactivate (plugin),
			-1);
	}
}

static void
cb_plugin_destroyed (PluginManagerGUI *pm_gui, GObject *ex_plugin)
{
	GtkTreeIter iter;

	if (model_get_plugin_iter (GTK_TREE_MODEL (pm_gui->model_plugins), ex_plugin, &iter)) {
		gtk_list_store_remove (pm_gui->model_plugins, &iter);
	}
}

static void
set_plugin_model_row (PluginManagerGUI *pm_gui, GtkTreeIter *iter, GnmPlugin *plugin)
{
	gtk_list_store_set (
		pm_gui->model_plugins, iter,
		PLUGIN_NAME,  gnm_plugin_get_name (plugin),
		PLUGIN_ACTIVE, gnm_plugin_is_active (plugin),
		PLUGIN_SWITCHABLE, !gnm_plugin_is_active (plugin) || gnm_plugin_can_deactivate (plugin),
		PLUGIN_POINTER, plugin,
		-1);
	g_signal_connect (
		G_OBJECT (plugin), "state_changed",
		G_CALLBACK (cb_plugin_changed), pm_gui);
	g_signal_connect (
		G_OBJECT (plugin), "can_deactivate_changed",
		G_CALLBACK (cb_plugin_changed), pm_gui);
	g_object_weak_ref (
		G_OBJECT (plugin), (GWeakNotify) cb_plugin_destroyed, pm_gui);
}

static void
cb_pm_button_rescan_directories_clicked (GtkButton *button, PluginManagerGUI *pm_gui)
{
	ErrorInfo *error;
	GSList *new_plugins, *l;
	GtkTreeModel *model = GTK_TREE_MODEL (pm_gui->model_plugins);
	GtkTreeIter iter, new_iter;
	gboolean has_iter;

	plugins_rescan (&error, &new_plugins);
	if (error != NULL) {
		gnumeric_error_error_info (COMMAND_CONTEXT (pm_gui->wbcg), error);
		error_info_free (error);
	}
	GNM_SLIST_SORT (new_plugins, plugin_compare_name);
	for (has_iter = gtk_tree_model_get_iter_first (model, &iter), l = new_plugins;
	     has_iter && l != NULL;
	     has_iter = gtk_tree_model_iter_next (model, &iter)) {
		GnmPlugin *old_plugin, *new_plugin;
	
		gtk_tree_model_get (model, &iter, PLUGIN_POINTER, &old_plugin, -1);
		while (new_plugin = l->data, plugin_compare_name (old_plugin, new_plugin) > 0) {
			gtk_list_store_insert_before (pm_gui->model_plugins, &new_iter, &iter);
			set_plugin_model_row (pm_gui, &new_iter, new_plugin);
			l = l->next;
			if (l == NULL)
				break;
		}
	}
	while (l != NULL) {
		gtk_list_store_append (pm_gui->model_plugins, &new_iter);
		set_plugin_model_row (pm_gui, &new_iter, GNM_PLUGIN (l->data));
		l = l->next;
	}
	g_slist_free (new_plugins);
}

static void
cb_pm_checkbutton_install_new_toggled (GtkCheckButton *checkbutton, PluginManagerGUI *pm_gui)
{
	gnm_gconf_set_activate_new_plugins (
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton)));
}

static void
pm_delete_dir (char *dir_name)
{
	GSList *plugin_dirs;
	GSList *directory;
	
	plugin_dirs = gnm_gconf_get_plugin_extra_dirs ();
	directory = g_slist_find_custom (plugin_dirs, dir_name, g_str_compare);
	g_free (dir_name);
	if (directory) {
		plugin_dirs = g_slist_delete_link (plugin_dirs, directory);
		gnm_gconf_set_plugin_extra_dirs (plugin_dirs);
		e_free_string_slist (plugin_dirs);
	}
}

static void
pm_add_dir (char *dir_name)
{
	GSList *plugin_dirs;
	
	plugin_dirs = gnm_gconf_get_plugin_extra_dirs ();
	if (g_slist_find_custom (plugin_dirs, dir_name, g_str_compare)) 
		g_free (dir_name);
	else {
		GNM_SLIST_PREPEND (plugin_dirs, dir_name);
		GNM_SLIST_SORT (plugin_dirs, g_str_compare);
		gnm_gconf_set_plugin_extra_dirs (plugin_dirs);
		e_free_string_slist (plugin_dirs);
	}
}

static void
cb_pm_button_directory_add_clicked (GtkButton *button, PluginManagerGUI *pm_gui)
{
	GtkFileSelection *fs;
	char *dir_name;

	fs = GTK_FILE_SELECTION (gtk_file_selection_new ("Select directory"));
	if (gnumeric_dialog_dir_selection (pm_gui->wbcg, fs)) {
		dir_name = g_path_get_dirname (gtk_file_selection_get_filename (fs));
		pm_add_dir (dir_name);
	}
	gtk_widget_destroy (GTK_WIDGET (fs));
}

static void
cb_pm_button_directory_delete_clicked (GtkButton *button, PluginManagerGUI *pm_gui)
{
	GtkTreeIter iter;
	char *name = NULL;
	gboolean is_system = TRUE;
	if (gtk_tree_selection_get_selected (pm_gui->selection_directory, NULL, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (pm_gui->model_directories),
				    &iter, 
				    DIR_NAME, &name,
				    DIR_IS_SYSTEM, &is_system,
				    -1);
		if (is_system) 
			g_free (name);
		else
			pm_delete_dir (name);
	}
}

static void
cb_checkbutton_mark_for_deactivation_toggled (GtkCheckButton *cbtn, GnmPlugin *plugin)
{
	plugin_db_mark_plugin_for_deactivation (
		plugin, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (cbtn)));
}

static void
cb_pm_selection_changed (GtkTreeSelection *selection, PluginManagerGUI *pm_gui)
{
	GnmPlugin *pinfo;
	GtkTreeIter iter;
	gint n_extra_info_items, i;
	GSList *extra_info_keys, *extra_info_values, *lkey, *lvalue;
	const char *plugin_desc;

	g_return_if_fail (pm_gui != NULL);

	g_signal_handlers_disconnect_matched (
		pm_gui->checkbutton_mark_for_deactivation,
		G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
		cb_checkbutton_mark_for_deactivation_toggled, NULL);

	gtk_list_store_clear (pm_gui->model_extra_info);
	if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_entry_set_text (pm_gui->entry_name, "");
		gtk_entry_set_text (pm_gui->entry_directory, "");
		gtk_entry_set_text (pm_gui->entry_id, "");
		gtk_text_buffer_set_text (pm_gui->text_description, "", 0);
		gtk_widget_hide (pm_gui->frame_mark_for_deactivation);
	} else {
		gtk_tree_model_get (GTK_TREE_MODEL (pm_gui->model_plugins),
		                    &iter, PLUGIN_POINTER, &pinfo, -1);
		gtk_entry_set_text (pm_gui->entry_name, gnm_plugin_get_name (pinfo));
		gtk_entry_set_text (pm_gui->entry_directory, gnm_plugin_get_dir_name (pinfo));
		gtk_entry_set_text (pm_gui->entry_id, gnm_plugin_get_id (pinfo));
		plugin_desc = gnm_plugin_get_description (pinfo);
		if (plugin_desc == NULL) {
			plugin_desc = "";
		}
		gtk_text_buffer_set_text (
			pm_gui->text_description, plugin_desc, strlen (plugin_desc));

		n_extra_info_items = gnm_plugin_get_extra_info_list
			(pinfo, &extra_info_keys, &extra_info_values);
		if (n_extra_info_items > 0) {
			for (i = 0, lkey = extra_info_keys, lvalue = extra_info_values;
			     i < n_extra_info_items;
			     i++, lkey = lkey->next, lvalue = lvalue->next ) {
				gtk_list_store_prepend (pm_gui->model_extra_info, &iter);
				gtk_list_store_set (pm_gui->model_extra_info, &iter,
						    EXTRA_NAME, (gchar *) lkey->data,
						    EXTRA_VALUE, (gchar *) lvalue->data,
						    -1);
			}
			e_free_string_slist (extra_info_keys);
			e_free_string_slist (extra_info_values);
		}

		if (gnm_plugin_is_active (pinfo) && !gnm_plugin_can_deactivate (pinfo)) {
			gtk_toggle_button_set_active (
				GTK_TOGGLE_BUTTON (pm_gui->checkbutton_mark_for_deactivation),
				plugin_db_is_plugin_marked_for_deactivation (pinfo));
			g_signal_connect (
				pm_gui->checkbutton_mark_for_deactivation, "toggled",
				G_CALLBACK (cb_checkbutton_mark_for_deactivation_toggled),
				pinfo);
			gtk_widget_show (pm_gui->frame_mark_for_deactivation);
		} else {
			gtk_widget_hide (pm_gui->frame_mark_for_deactivation);
		}
	}
}

static void
pm_dialog_cleanup (GObject *dialog, PluginManagerGUI *pm_gui)
{
	GtkTreeModel *model = GTK_TREE_MODEL (pm_gui->model_plugins);
	GtkTreeIter iter;
	gboolean has_iter;
      
	for (has_iter = gtk_tree_model_get_iter_first (model, &iter);
	     has_iter; has_iter = gtk_tree_model_iter_next (model, &iter)) {
		gpointer plugin;
	
		gtk_tree_model_get (model, &iter, PLUGIN_POINTER, &plugin, -1);
		g_signal_handlers_disconnect_by_func (
			G_OBJECT (plugin), G_CALLBACK (cb_plugin_changed), pm_gui);
		g_signal_handlers_disconnect_by_func (
			G_OBJECT (plugin), G_CALLBACK (cb_plugin_changed), pm_gui);
		g_object_weak_unref (
			G_OBJECT (plugin), (GWeakNotify) cb_plugin_destroyed, pm_gui);
	}
}

static void
cb_pm_button_activate_all_clicked (GtkButton *button, PluginManagerGUI *pm_gui)
{
	ErrorInfo *activation_error, *error;

	plugin_db_activate_plugin_list (
		plugins_get_available_plugins (), &activation_error);
	if (activation_error != NULL) {
		error = error_info_new_str_with_details (
			_("Errors while activating plugins"), activation_error);
		gnumeric_error_info_dialog_show (pm_gui->wbcg, error);
		error_info_free (error);
	}
}

static void
cb_pm_button_deactivate_all_clicked (GtkButton *button, PluginManagerGUI *pm_gui)
{
	ErrorInfo *deactivation_error, *error;

	plugin_db_deactivate_plugin_list (
		plugins_get_available_plugins (), &deactivation_error);
	if (deactivation_error != NULL) {
		error = error_info_new_str_with_details (
			_("Errors while deactivating plugins"), deactivation_error);
		gnumeric_error_info_dialog_show (pm_gui->wbcg, error);
		error_info_free (error);
	}
}

static void
pm_dialog_init (PluginManagerGUI *pm_gui)
{
	GSList *sorted_plugin_list;
	GtkTreeIter iter;

	g_signal_connect (G_OBJECT (pm_gui->button_activate_all),
		"clicked",
		G_CALLBACK (cb_pm_button_activate_all_clicked), pm_gui);
	g_signal_connect (G_OBJECT (pm_gui->button_deactivate_all),
		"clicked",
		G_CALLBACK (cb_pm_button_deactivate_all_clicked), pm_gui);
	g_signal_connect (G_OBJECT (pm_gui->button_rescan_directories),
		"clicked",
		G_CALLBACK (cb_pm_button_rescan_directories_clicked), pm_gui);
	g_signal_connect (G_OBJECT (pm_gui->button_directory_add),
		"clicked",
		G_CALLBACK (cb_pm_button_directory_add_clicked), pm_gui);
	g_signal_connect (G_OBJECT (pm_gui->button_directory_delete),
		"clicked",
		G_CALLBACK (cb_pm_button_directory_delete_clicked), pm_gui);
	g_signal_connect (G_OBJECT (pm_gui->checkbutton_install_new),
		"toggled",
		G_CALLBACK (cb_pm_checkbutton_install_new_toggled), pm_gui);
	g_signal_connect (G_OBJECT (pm_gui->dialog_pm),
		"destroy",
		G_CALLBACK (pm_dialog_cleanup), pm_gui);
	
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pm_gui->checkbutton_install_new),
				      gnm_gconf_get_activate_new_plugins ());

	/* initialize plugin list */
	gtk_list_store_clear (pm_gui->model_plugins);
	sorted_plugin_list = g_slist_sort (
		g_slist_copy (plugins_get_available_plugins ()),
		&plugin_compare_name);
	GNM_SLIST_FOREACH (sorted_plugin_list, GnmPlugin, plugin,
		gtk_list_store_append (pm_gui->model_plugins, &iter);
		set_plugin_model_row (pm_gui, &iter, plugin);
	);
	g_slist_free (sorted_plugin_list);

	cb_pm_selection_changed (pm_gui->selection, pm_gui);
}

static void
pm_gui_load_directories (PluginManagerGUI *pm_gui, GSList *plugin_dirs, gboolean is_conf)
{
	for (; plugin_dirs; plugin_dirs = plugin_dirs->next) {
		GtkTreeIter iter;
		gtk_list_store_append (pm_gui->model_directories, &iter);
		gtk_list_store_set (pm_gui->model_directories, &iter,
				    DIR_NAME, (char *) plugin_dirs->data,
				    DIR_IS_SYSTEM, !is_conf,
				    -1);
	}
}

static void
pm_gui_load_directory_page (PluginManagerGUI *pm_gui)
{
	GtkTreeIter iter;
	char * sys_plugins = gnumeric_sys_plugin_dir ();
	char * usr_plugins = gnumeric_usr_plugin_dir ();
	GSList *plugin_dirs;
	gchar const *plugin_path_env;

	gtk_list_store_clear (pm_gui->model_directories);

	gtk_list_store_append (pm_gui->model_directories, &iter);
	gtk_list_store_set (pm_gui->model_directories, &iter,
			    DIR_NAME, sys_plugins,
			    DIR_IS_SYSTEM, TRUE,
			    -1);
	g_free (sys_plugins);
	gtk_list_store_append (pm_gui->model_directories, &iter);
	gtk_list_store_set (pm_gui->model_directories, &iter,
			    DIR_NAME, usr_plugins,
			    DIR_IS_SYSTEM, TRUE,
			    -1);
	g_free (usr_plugins);
	
	plugin_path_env = g_getenv ("GNUMERIC_PLUGIN_PATH");
	if (plugin_path_env != NULL) {
		plugin_dirs = g_strsplit_to_slist (plugin_path_env, ":");
		pm_gui_load_directories (pm_gui, plugin_dirs, FALSE);
		e_free_string_slist (plugin_dirs);
	}
	plugin_dirs = gnm_gconf_get_plugin_extra_dirs ();
	pm_gui_load_directories (pm_gui, plugin_dirs, TRUE);
	e_free_string_slist (plugin_dirs);
}

static void
cb_pm_dir_selection_changed (GtkTreeSelection *ignored, PluginManagerGUI *pm_gui)
{
	GtkTreeIter  iter;
	gboolean is_system;

	if (!gtk_tree_selection_get_selected (pm_gui->selection_directory, NULL, &iter)) {
		gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_directory_delete), FALSE);
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (pm_gui->model_directories), &iter,
			    DIR_IS_SYSTEM, &is_system, -1);

	gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_directory_delete), !is_system);
}

static void
cb_dir_changed_notification (GConfClient *gconf, guint cnxn_id, GConfEntry *entry, 
			    PluginManagerGUI *pm_gui)
{
	pm_gui_load_directory_page (pm_gui);	
	cb_pm_button_rescan_directories_clicked (NULL, pm_gui);
}

static void
cb_active_toggled (GtkCellRendererToggle *celltoggle, char *path,
                   PluginManagerGUI *pm_gui)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GnmPlugin *plugin;
	ErrorInfo *error;

	model = gtk_tree_view_get_model (pm_gui->list_plugins);
	gtk_tree_model_get_iter_from_string (model, &iter, path);
	gtk_tree_model_get (model, &iter, PLUGIN_POINTER, &plugin, -1);
	g_assert (plugin != NULL);
	if (gnm_plugin_is_active (plugin)) {
		gnm_plugin_deactivate (plugin, &error);
	} else {
		GSList *dep_ids;
		int n_inactive_deps = 0;
		gboolean want_activate = TRUE;

		dep_ids = gnm_plugin_get_dependencies_ids (plugin);
		if (dep_ids != NULL) {
			GString *s;

			s = g_string_new (_("The following extra plugins must be activated in order to activate this one:\n\n"));
			GNM_SLIST_FOREACH (dep_ids, char, plugin_id,
				GnmPlugin *plugin;

				plugin = plugins_get_plugin_by_id (plugin_id);
				if (plugin == NULL) {
					g_string_append_printf (s, _("Unknown plugin with id=\"%s\"\n"), plugin_id);
				} else if (!gnm_plugin_is_active (plugin)) {
					g_string_append (s, gnm_plugin_get_name (plugin));
					g_string_append_c (s, '\n');
					n_inactive_deps++;
				}
			);
			g_string_append (s, _("\nDo you want to activate this plugin together with its dependencies?"));
			if (n_inactive_deps > 0) {
				want_activate = gnumeric_dialog_question_yes_no (pm_gui->wbcg, s->str, TRUE);
			}
			g_string_free (s, TRUE);
		}
		g_slist_free_custom (dep_ids, g_free);

		if (want_activate) {
			gnm_plugin_activate (plugin, &error);
		} else {
			error = NULL;
		}
	}
	if (error != NULL) {
		ErrorInfo *new_error;

		if (gnm_plugin_is_active) {
			new_error = error_info_new_printf (
				_("Error while deactivating plugin \"%s\"."),
				gnm_plugin_get_name (plugin));
		} else {
			new_error = error_info_new_printf (
				_("Error while activating plugin \"%s\"."),
				gnm_plugin_get_name (plugin));
		}
		error_info_add_details (new_error, error);
		gnumeric_error_error_info (COMMAND_CONTEXT (pm_gui->wbcg), new_error);
	}
}

void
dialog_plugin_manager (WorkbookControlGUI *wbcg)
{
	PluginManagerGUI *pm_gui;
	GladeXML *gui;
	GtkWidget *scrolled;
	GtkWidget *scrolled_extra;
	GtkWidget *scrolled_directories;
	GtkWidget *table;
	GtkTreeViewColumn *column;
	GtkTreeView   *extra_list_view;
	GtkCellRenderer *rend;

	g_return_if_fail (wbcg != NULL);

	gui = gnumeric_glade_xml_new (wbcg, "plugin-manager.glade");
	g_return_if_fail (gui != NULL);

	pm_gui = g_new (PluginManagerGUI, 1);
	pm_gui->wbcg = wbcg;
	pm_gui->dialog_pm = GTK_DIALOG (glade_xml_get_widget (gui, "dialog_plugin_manager"));

	/* Set-up plugin list  page */

	pm_gui->button_activate_all = 
		GTK_BUTTON (glade_xml_get_widget (gui, "button_activate_all"));
	pm_gui->button_deactivate_all =
		GTK_BUTTON (glade_xml_get_widget (gui, "button_deactivate_all"));
	pm_gui->button_rescan_directories = GTK_BUTTON (glade_xml_get_widget
						    (gui, "button_rescan_directories"));
	pm_gui->checkbutton_install_new = GTK_CHECK_BUTTON (glade_xml_get_widget
							    (gui, "checkbutton_install_new"));

	pm_gui->model_plugins = gtk_list_store_new (
		NUM_COLMNS, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_POINTER);
	pm_gui->list_plugins = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (pm_gui->model_plugins)));
	pm_gui->selection = gtk_tree_view_get_selection (pm_gui->list_plugins);
	gtk_tree_selection_set_mode (pm_gui->selection, GTK_SELECTION_BROWSE);
	g_signal_connect (G_OBJECT (pm_gui->selection),
		"changed",
		G_CALLBACK (cb_pm_selection_changed), pm_gui);

	rend = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (rend),
		"toggled", G_CALLBACK (cb_active_toggled), pm_gui);
	column = gtk_tree_view_column_new_with_attributes (
		_("Active"), rend,
		"active", PLUGIN_ACTIVE,
		"activatable", PLUGIN_SWITCHABLE,
		NULL);
	gtk_tree_view_append_column (pm_gui->list_plugins, column);
	column = gtk_tree_view_column_new_with_attributes (_("Plugin name"),
							   gtk_cell_renderer_text_new (),
							   "text", PLUGIN_NAME, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PLUGIN_NAME);
	gtk_tree_view_append_column (pm_gui->list_plugins, column);
	scrolled = glade_xml_get_widget (gui, "scrolled_plugin_list");
	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (pm_gui->list_plugins));

	/* Set-up plugin details page */

	pm_gui->entry_name = GTK_ENTRY (glade_xml_get_widget (gui, "entry_name"));
	pm_gui->entry_directory = GTK_ENTRY (glade_xml_get_widget (gui, "entry_directory"));
	pm_gui->text_description = gtk_text_view_get_buffer (GTK_TEXT_VIEW (
				           glade_xml_get_widget (gui, "text_description")));
	pm_gui->entry_id = GTK_ENTRY (glade_xml_get_widget (gui, "entry_id"));

	scrolled_extra = glade_xml_get_widget (gui, "scrolled_extra_info");
	pm_gui->frame_mark_for_deactivation =
		glade_xml_get_widget (gui, "frame_mark_for_deactivation");
	pm_gui->checkbutton_mark_for_deactivation = 
		glade_xml_get_widget (gui, "checkbutton_mark_for_deactivation");

	pm_gui->model_extra_info = gtk_list_store_new (EXTRA_NUM_COLMNS, G_TYPE_STRING,
						       G_TYPE_STRING);
	extra_list_view = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (pm_gui->model_extra_info)));
	column = gtk_tree_view_column_new_with_attributes (_("Name"),
							   gtk_cell_renderer_text_new (),
							   "text", EXTRA_NAME, NULL);
	gtk_tree_view_column_set_sort_column_id (column, EXTRA_NAME);
	gtk_tree_view_append_column (extra_list_view, column);
	column = gtk_tree_view_column_new_with_attributes (_("Value"),
							   gtk_cell_renderer_text_new (),
							   "text", EXTRA_VALUE, NULL);
	gtk_tree_view_column_set_sort_column_id (column, EXTRA_VALUE);
	gtk_tree_view_append_column (extra_list_view, column);
	gtk_container_add (GTK_CONTAINER (scrolled_extra), GTK_WIDGET (extra_list_view));

	/* Set-up directories page */

	table = glade_xml_get_widget (gui, "directory-table");
	pm_gui->model_directories = gtk_list_store_new (DIR_NUM_COLMNS, G_TYPE_STRING, 
							G_TYPE_BOOLEAN);
	pm_gui->list_directories = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (pm_gui->model_directories)));
	pm_gui->selection_directory = gtk_tree_view_get_selection (pm_gui->list_directories);
	gtk_tree_selection_set_mode (pm_gui->selection_directory, GTK_SELECTION_BROWSE);
	column = gtk_tree_view_column_new_with_attributes (_("Directory"),
							   gtk_cell_renderer_text_new (),
							   "text", DIR_NAME, 
							   NULL);
	gtk_tree_view_column_set_sort_column_id (column, DIR_NAME);
	gtk_tree_view_append_column (pm_gui->list_directories, column);
	scrolled_directories = glade_xml_get_widget (gui, "scrolled_directories");
	gtk_container_add (GTK_CONTAINER (scrolled_directories), 
			   GTK_WIDGET (pm_gui->list_directories));

	pm_gui->button_directory_add = GTK_BUTTON (glade_xml_get_widget
						  (gui, "button_directory_add"));
	gtk_button_stock_alignment_set (GTK_BUTTON (pm_gui->button_directory_add),
					0., .5, 0., 0.);
	pm_gui->button_directory_delete = GTK_BUTTON (glade_xml_get_widget
						  (gui, "button_directory_delete"));
	gtk_button_stock_alignment_set (GTK_BUTTON (pm_gui->button_directory_delete),
					0., .5, 0., 0.);

	cb_pm_dir_selection_changed (NULL, pm_gui);
	g_signal_connect (pm_gui->selection_directory,
		"changed",
		G_CALLBACK (cb_pm_dir_selection_changed), pm_gui);

	/* Done setting up pages */

	pm_gui->gnotebook = GTK_NOTEBOOK (glade_xml_get_widget (gui, "notebook1"));

	gtk_widget_show_all (GTK_WIDGET (pm_gui->gnotebook));

	pm_gui_load_directory_page (pm_gui);
	pm_gui->directories_changed_notification = gnm_gconf_add_notification_plugin_directories (
			(GConfClientNotifyFunc) cb_dir_changed_notification, pm_gui);
	pm_dialog_init (pm_gui);
	(void) gnumeric_dialog_run (wbcg, pm_gui->dialog_pm);

	pm_gui->directories_changed_notification = gnm_gconf_rm_notification 
		(pm_gui->directories_changed_notification);
	
	g_free (pm_gui);
	g_object_unref (G_OBJECT (gui));
}
