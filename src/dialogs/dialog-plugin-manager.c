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

#include <gui-util.h>
#include <command-context.h>
#include <workbook-control.h>
#include <workbook.h>
#include <plugin.h>
#include <gnumeric-gconf.h>
#include <application.h>

#include <libgnome/gnome-i18n.h>
#include <string.h>
#include <glade/glade.h>
#include <gal/util/e-util.h>
#include <stdio.h>

typedef struct {
	WorkbookControlGUI *wbcg;
	GtkDialog *dialog_pm;
	GtkNotebook *gnotebook;
	GtkListStore  *model_plugins;
	GtkTreeView   *list_plugins;
	GtkTreeSelection   *selection;
	GtkButton *button_activate_plugin, *button_deactivate_plugin,
	          *button_activate_all, *button_deactivate_all,
	          *button_rescan_directories;
	GtkButton *button_directory_add, *button_directory_delete;
	GtkCheckButton *checkbutton_install_new;
	GtkEntry *entry_name, *entry_directory, *entry_id;
	GtkTextBuffer *text_description;
	GtkListStore  *model_extra_info;
	gchar *current_plugin_id;
	GtkListStore  *model_directories;
	GtkTreeView   *list_directories;
	GtkTreeSelection   *selection_directory;
	guint directories_changed_notification;
} PluginManagerGUI;

enum {
	PLUGIN_NAME,
	PLUGIN_STATE_STR,
	PLUGIN_STATE,
	PLUGIN_ID,
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

typedef enum {
	PLUGIN_STATE_IN_MEMORY = 0,
	PLUGIN_STATE_DEACTIVATING = 1,
	PLUGIN_STATE_DEACTIVATING_IN_MEMORY = 2,
	PLUGIN_STATE_INACTIVE = 3,
	PLUGIN_STATE_ACTIVE = 4
} plugin_state_t;
static const char *activity_description[] = {
	N_("in memory"),
	N_("deactivating"),
	N_("in memory, deactivating"),
	N_("inactive"),
	N_("active"),
	0
};

static void update_plugin_manager_view (PluginManagerGUI *pm_gui);
static void update_plugin_details_view (PluginManagerGUI *pm_gui);

static void
cb_pm_button_activate_plugin_clicked (GtkButton *button, PluginManagerGUI *pm_gui)
{
	PluginInfo *pinfo;
	const gchar *loader_type_str;
	gboolean loader_available = FALSE;

	g_return_if_fail (pm_gui != NULL);
	g_return_if_fail (pm_gui->current_plugin_id != NULL);

	pinfo = plugin_db_get_plugin_info_by_plugin_id (pm_gui->current_plugin_id);
	if (plugin_info_is_active (pinfo)) {
		plugin_db_mark_plugin_for_deactivation (pinfo, FALSE);
		update_plugin_manager_view (pm_gui);
		return;
	}
	loader_type_str = plugin_info_peek_loader_type_str (pinfo);
	if (plugin_loader_is_available_by_id (loader_type_str)) {
		loader_available = TRUE;
	} else {
		GSList *l;
		PluginInfo *loader_pinfo = NULL;

		for (l = plugin_db_get_available_plugin_info_list (); l != NULL; l = l->next) {
			loader_pinfo = (PluginInfo *) l->data;
			if (!plugin_info_is_active (loader_pinfo) &&
			    plugin_info_provides_loader_by_type_str (loader_pinfo, loader_type_str)) {
				break;
			}
		}
		if (l != NULL) {
			gchar *msg;
			gboolean want_loader;
			ErrorInfo *error;


			msg = g_strdup_printf (
			      _("This plugin depends on loader of type \"%s\".\n"
			        "Do you want to activate appropriate plugin "
				"together with this one?\n"
			        "(otherwise, plugin won't be loaded)"),
			       loader_type_str);
			want_loader = gnumeric_dialog_question_yes_no (pm_gui->wbcg, msg, TRUE);
			g_free (msg);
			if (want_loader) {
				activate_plugin (loader_pinfo, &error);
				if (error != NULL) {
					error = error_info_new_str_with_details (
					        _("Error while activating plugin loader"),
					        error);
					gnumeric_error_info_dialog_show (pm_gui->wbcg, error);
					error_info_free (error);
				}
				if (plugin_info_is_active (loader_pinfo)) {
					plugin_db_update_saved_active_plugin_id_list ();
					update_plugin_manager_view (pm_gui);
					loader_available = TRUE;
				}
			}
		} else {
			gchar *msg;

			msg = g_strdup_printf (_("Loader for selected plugin (type: \"%s\") is not available."),
			                       plugin_info_peek_loader_type_str (pinfo));
			gnumeric_error_plugin (COMMAND_CONTEXT (pm_gui->wbcg), msg);
			g_free (msg);
		}
	}

	if (loader_available) {
		ErrorInfo *error;

		activate_plugin (pinfo, &error);
		if (error != NULL) {
			error = error_info_new_str_with_details (
			        _("Error while activating plugin"),
			        error);
			gnumeric_error_info_dialog_show (pm_gui->wbcg, error);
			error_info_free (error);
		}
		if (plugin_info_is_active (pinfo)) {
			plugin_db_update_saved_active_plugin_id_list ();
			update_plugin_manager_view (pm_gui);
		}
	}
}

static void
cb_pm_button_deactivate_plugin_clicked (GtkButton *button, PluginManagerGUI *pm_gui)
{
	PluginInfo *pinfo;

	g_return_if_fail (pm_gui != NULL);
	g_return_if_fail (pm_gui->current_plugin_id != NULL);

	pinfo = plugin_db_get_plugin_info_by_plugin_id (pm_gui->current_plugin_id);
	if (plugin_can_deactivate (pinfo)) {
		ErrorInfo *error;

		deactivate_plugin (pinfo, &error);
		if (error != NULL) {
			error = error_info_new_str_with_details (
			        _("Error while deactivating plugin"),
			        error);
			gnumeric_error_info_dialog_show (pm_gui->wbcg, error);
			error_info_free (error);
		}
		if (!plugin_info_is_active (pinfo)) {
			plugin_db_update_saved_active_plugin_id_list ();
			update_plugin_manager_view (pm_gui);
		}
	} else {
		if (plugin_db_is_plugin_marked_for_deactivation (pinfo)) {
			gnumeric_error_plugin (COMMAND_CONTEXT (pm_gui->wbcg), _("Plugin is still in use."));
		} else {
			gboolean mark_for_deactivation;

			mark_for_deactivation = gnumeric_dialog_question_yes_no (
			                        pm_gui->wbcg,
			                        _("Plugin cannot be deactivated because it's still in use.\n"
			                        "Do you want to mark it for deactivation such that it will be inactive after restarting Gnumeric?"),
			                        FALSE);
			if (mark_for_deactivation) {
				plugin_db_mark_plugin_for_deactivation (pinfo, TRUE);
				update_plugin_manager_view (pm_gui);
			}
		}
	}
}

static void
cb_pm_button_activate_all_clicked (GtkButton *button, PluginManagerGUI *pm_gui)
{
	ErrorInfo *error;

	g_return_if_fail (pm_gui != NULL);
	plugin_db_activate_plugin_list (plugin_db_get_available_plugin_info_list (), &error);
	update_plugin_manager_view (pm_gui);
	plugin_db_update_saved_active_plugin_id_list ();
	if (error != NULL) {
		error = error_info_new_str_with_details (
		        _("Errors while activating plugins"),
		        error);
		gnumeric_error_info_dialog_show (pm_gui->wbcg, error);
		error_info_free (error);
	}
}

static void
cb_pm_button_deactivate_all_clicked (GtkButton *button, PluginManagerGUI *pm_gui)
{
	ErrorInfo *error;

	g_return_if_fail (pm_gui != NULL);
	plugin_db_deactivate_plugin_list (plugin_db_get_available_plugin_info_list (), &error);
	update_plugin_manager_view (pm_gui);
	plugin_db_update_saved_active_plugin_id_list ();
	if (error != NULL) {
		error = error_info_new_str_with_details (
		        _("Errors while deactivating plugins"),
		        error);
		gnumeric_error_info_dialog_show (pm_gui->wbcg, error);
		error_info_free (error);
	}
}

static void
free_plugin_id (gpointer data)
{
	g_free (data);
}

static void
cb_pm_button_rescan_directories_clicked (GtkButton *button, PluginManagerGUI *pm_gui)
{
	plugin_db_rescan ();
	update_plugin_manager_view (pm_gui);
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
		plugin_dirs = g_slist_prepend (plugin_dirs, dir_name);
		plugin_dirs = g_slist_sort (plugin_dirs, g_str_compare);
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
cb_pm_selection_changed (GtkTreeSelection *selection, PluginManagerGUI *pm_gui)
{
	PluginInfo *pinfo;
	GtkTreeIter iter;
	GValue value = {0, };

	g_return_if_fail (pm_gui != NULL);

	free_plugin_id (pm_gui->current_plugin_id);
	if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		pm_gui->current_plugin_id = NULL;
		gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_activate_plugin),
					  FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_deactivate_plugin),
					  FALSE);
	} else {
		gboolean is_marked, is_active;

		gtk_tree_model_get_value (GTK_TREE_MODEL (pm_gui->model_plugins),
					  &iter, PLUGIN_ID, &value);
		pm_gui->current_plugin_id = g_strdup(g_value_get_string (&value));
		g_value_unset (&value);

		pinfo = plugin_db_get_plugin_info_by_plugin_id (pm_gui->current_plugin_id);
		g_return_if_fail (pinfo != NULL);

		is_active = plugin_info_is_active (pinfo);
		is_marked = plugin_db_is_plugin_marked_for_deactivation (pinfo);

		gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_activate_plugin),
					  is_marked || !is_active);
		gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_deactivate_plugin),
					  is_active && !is_marked);
	}
	update_plugin_details_view (pm_gui);
}

static void
pm_dialog_init (PluginManagerGUI *pm_gui)
{
	g_signal_connect (G_OBJECT (pm_gui->button_activate_plugin),
		"clicked",
		G_CALLBACK (cb_pm_button_activate_plugin_clicked), pm_gui);
	g_signal_connect (G_OBJECT (pm_gui->button_deactivate_plugin),
		"clicked",
		G_CALLBACK (cb_pm_button_deactivate_plugin_clicked), pm_gui);
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
	
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pm_gui->checkbutton_install_new),
				      gnm_gconf_get_activate_new_plugins ());
	update_plugin_manager_view (pm_gui);
}

static int
plugin_compare_name (gconstpointer a, gconstpointer b)
{
	PluginInfo *plugin_a = (PluginInfo *) a, *plugin_b = (PluginInfo *) b;

	return - strcoll (plugin_info_peek_name (plugin_a),
	                plugin_info_peek_name (plugin_b));
}

static void
update_plugin_manager_view (PluginManagerGUI *pm_gui)
{
	GSList *sorted_plugin_list, *l;
	gint n_active_plugins, n_inactive_plugins, n_plugins;
	GtkTreeIter iter, *select_iter = NULL;
	plugin_state_t status;
	gchar *id;
	gchar *last_selected_plugin;

	last_selected_plugin = (pm_gui->current_plugin_id != NULL) ?
		g_strdup (pm_gui->current_plugin_id) : NULL;

	gtk_list_store_clear (pm_gui->model_plugins);

	sorted_plugin_list = g_slist_sort (g_slist_copy
					  (plugin_db_get_available_plugin_info_list ()),
	                                  &plugin_compare_name);

	n_active_plugins = 0;
	n_inactive_plugins = 0;
	for (l = sorted_plugin_list; l != NULL; l = l->next) {
		PluginInfo *pinfo = (PluginInfo *) l->data;

		gtk_list_store_prepend (pm_gui->model_plugins, &iter);

		if (plugin_info_is_active (pinfo)) {
			gboolean is_in_mem, is_marked;

			n_active_plugins++;
			is_in_mem = plugin_info_is_loaded (pinfo);
			is_marked = plugin_db_is_plugin_marked_for_deactivation (pinfo);
			if (is_in_mem && is_marked) {
				status = PLUGIN_STATE_DEACTIVATING_IN_MEMORY;
			} else if (is_in_mem) {
				status = PLUGIN_STATE_IN_MEMORY;
			} else if (is_marked) {
				status = PLUGIN_STATE_DEACTIVATING;
			} else {
				status = PLUGIN_STATE_ACTIVE;
			}
		} else {
			n_inactive_plugins++;
			status = PLUGIN_STATE_INACTIVE;
		}
		id = plugin_info_get_id (pinfo);
		gtk_list_store_set (pm_gui->model_plugins, &iter,
				    PLUGIN_NAME,  plugin_info_peek_name (pinfo),
				    PLUGIN_STATE_STR, _(activity_description[status]),
				    PLUGIN_STATE, status,
				    PLUGIN_ID, g_strdup (id),
				    PLUGIN_POINTER, NULL,
				    -1);
		if (last_selected_plugin != NULL && select_iter == NULL) {
			if (strcoll (id, last_selected_plugin) == 0) {
				select_iter = gtk_tree_iter_copy(&iter);
			}
		}
	}
	n_plugins = n_active_plugins + n_inactive_plugins;

	g_slist_free (sorted_plugin_list);

	free_plugin_id (pm_gui->current_plugin_id);
	pm_gui->current_plugin_id = NULL;
	if (last_selected_plugin != NULL)
		g_free (last_selected_plugin);

	if (n_plugins > 0) {
		if (select_iter == NULL)
			select_iter = gtk_tree_iter_copy(&iter);
		gtk_tree_selection_select_iter (pm_gui->selection, select_iter);
		gtk_tree_iter_free (select_iter);
	}

	gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_activate_all),
				  n_inactive_plugins > 0);
	gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_deactivate_all),
				  n_active_plugins > 0);
	cb_pm_selection_changed (pm_gui->selection, pm_gui);
}

static void
update_plugin_details_view (PluginManagerGUI *pm_gui)
{
	PluginInfo *pinfo;
	gint n_extra_info_items, i;
	GSList *extra_info_keys, *extra_info_values, *lkey, *lvalue;
	GtkTreeIter iter;

	g_return_if_fail (pm_gui != NULL);

	gtk_list_store_clear (pm_gui->model_extra_info);
	if (pm_gui->current_plugin_id != NULL) {
		pinfo = plugin_db_get_plugin_info_by_plugin_id (pm_gui->current_plugin_id);
		gtk_entry_set_text (pm_gui->entry_name, plugin_info_peek_name (pinfo));
		gtk_entry_set_text (pm_gui->entry_directory, plugin_info_peek_dir_name (pinfo));
		gtk_entry_set_text (pm_gui->entry_id, plugin_info_peek_id (pinfo));

		gtk_text_buffer_set_text (pm_gui->text_description,
					  plugin_info_peek_description (pinfo),
					  strlen (plugin_info_peek_description (pinfo)));

		n_extra_info_items = plugin_info_get_extra_info_list
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
	} else {
		gtk_entry_set_text (pm_gui->entry_name, "");
		gtk_entry_set_text (pm_gui->entry_directory, "");
		gtk_entry_set_text (pm_gui->entry_id, "");
		gtk_text_buffer_set_text (pm_gui->text_description, "", 0);
	}
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

	g_return_if_fail (wbcg != NULL);

	gui = gnumeric_glade_xml_new (wbcg, "plugin-manager.glade");
	g_return_if_fail (gui != NULL);

	pm_gui = g_new (PluginManagerGUI, 1);
	pm_gui->wbcg = wbcg;
	pm_gui->dialog_pm = GTK_DIALOG (glade_xml_get_widget (gui, "dialog_plugin_manager"));

	/* Set-up plugin list  page */

	pm_gui->button_activate_plugin = GTK_BUTTON (glade_xml_get_widget
						     (gui, "button_activate_plugin"));
	pm_gui->button_deactivate_plugin = GTK_BUTTON (glade_xml_get_widget
						       (gui, "button_deactivate_plugin"));
	pm_gui->button_activate_all = GTK_BUTTON (glade_xml_get_widget
						  (gui, "button_activate_all"));
	pm_gui->button_deactivate_all = GTK_BUTTON (glade_xml_get_widget
						    (gui, "button_deactivate_all"));
	pm_gui->button_rescan_directories = GTK_BUTTON (glade_xml_get_widget
						    (gui, "button_rescan_directories"));
	pm_gui->checkbutton_install_new = GTK_CHECK_BUTTON (glade_xml_get_widget
							    (gui, "checkbutton_install_new"));

	pm_gui->model_plugins = gtk_list_store_new (NUM_COLMNS, G_TYPE_STRING, G_TYPE_STRING,
						    G_TYPE_INT,
						    G_TYPE_STRING, G_TYPE_POINTER);
	pm_gui->list_plugins = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (pm_gui->model_plugins)));
	pm_gui->selection = gtk_tree_view_get_selection (pm_gui->list_plugins);
	gtk_tree_selection_set_mode (pm_gui->selection, GTK_SELECTION_BROWSE);
	g_signal_connect (G_OBJECT (pm_gui->selection),
		"changed",
		G_CALLBACK (cb_pm_selection_changed), pm_gui);
	column = gtk_tree_view_column_new_with_attributes (_("State"),
							   gtk_cell_renderer_text_new (),
							   "text", PLUGIN_STATE_STR, NULL);
	gtk_tree_view_column_set_sort_column_id (column, PLUGIN_STATE_STR);
	gtk_tree_view_append_column (pm_gui->list_plugins, column);
	column = gtk_tree_view_column_new_with_attributes (_("Plugins"),
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

	pm_gui->current_plugin_id = NULL;
	pm_gui_load_directory_page (pm_gui);
	pm_gui->directories_changed_notification = gnm_gconf_add_notification_plugin_directories (
			(GConfClientNotifyFunc) cb_dir_changed_notification, pm_gui);
	pm_dialog_init (pm_gui);
	(void) gnumeric_dialog_run (wbcg, pm_gui->dialog_pm);
	pm_gui->directories_changed_notification = gnm_gconf_rm_notification 
		(pm_gui->directories_changed_notification);
	
	free_plugin_id (pm_gui->current_plugin_id);
	g_free (pm_gui);

	g_object_unref (G_OBJECT (gui));
}
