/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
#include <glib/gi18n.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gutils.h>
#include <gui-util.h>
#include <command-context.h>
#include <workbook-control.h>
#include <workbook.h>
#include <plugin.h>
#include <plugin-service.h>
#include <gnumeric-gconf.h>
#include <application.h>

#include <glade/glade.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtktreestore.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkcellrenderertoggle.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkliststore.h>
#include <gtk/gtkcheckbutton.h>
#include <gtk/gtkfilechooserdialog.h>
#include <gtk/gtkstock.h>
#include <gsf/gsf-impl-utils.h>

#include <string.h>

#define PLUGIN_MANAGER_DIALOG_KEY "zoom-dialog"

typedef struct {
	WorkbookControlGUI *wbcg;
	GladeXML *gui;
	GtkDialog *dialog_pm;
	GtkNotebook *gnotebook;
	GtkListStore  *model_plugins;
	GtkTreeView   *list_plugins;
	GtkTreeStore  *model_details;
	GtkTreeView   *view_details;
	GtkTreeSelection   *selection;
	GtkButton *button_rescan_directories;
	GtkButton *button_directory_add, *button_directory_delete;
	GtkButton *button_activate_all, *button_deactivate_all;
	GtkCheckButton *checkbutton_install_new;
	GtkWidget *frame_mark_for_deactivation;
	GtkWidget *checkbutton_mark_for_deactivation;
	GtkEntry *entry_directory;
	GtkTextBuffer *text_description;
	GtkListStore  *model_directories;
	GtkTreeView   *list_directories;
	GtkTreeSelection   *selection_directory;
} PluginManagerGUI;

enum {
	PLUGIN_NAME,
	PLUGIN_ACTIVE,
	PLUGIN_SWITCHABLE,
	PLUGIN_POINTER,
	NUM_COLMNS
};
enum {
	DIR_NAME,
	DIR_IS_SYSTEM,
	DIR_NUM_COLMNS
};
enum {
	DETAILS_DESC,
	DETAILS_ID,
	DETAILS_NUM_COLMNS
};


static int
plugin_compare_name (gconstpointer a, gconstpointer b)
{
	GnmPlugin *plugin_a = (GnmPlugin *) a, *plugin_b = (GnmPlugin *) b;

	return g_utf8_collate (gnm_plugin_get_name (plugin_a),
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
		PLUGIN_NAME,  _(gnm_plugin_get_name (plugin)),
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
cb_pm_button_rescan_directories_clicked (PluginManagerGUI *pm_gui)
{
	ErrorInfo *error;
	GSList *new_plugins, *l;
	GtkTreeModel *model = GTK_TREE_MODEL (pm_gui->model_plugins);
	GtkTreeIter iter, new_iter;
	gboolean has_iter;

	plugins_rescan (&error, &new_plugins);
	if (error != NULL) {
		gnm_cmd_context_error_info (GNM_CMD_CONTEXT (pm_gui->wbcg), error);
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
cb_pm_checkbutton_install_new_toggled (GtkCheckButton *checkbutton,
				       G_GNUC_UNUSED PluginManagerGUI *pm_gui)
{
	gnm_gconf_set_activate_new_plugins (
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton)));
}

static void
pm_gui_load_directories (PluginManagerGUI *pm_gui,
			 GSList const *plugin_dirs, gboolean is_conf)
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
	char * sys_plugins = gnm_sys_plugin_dir ();
	char * usr_plugins = gnm_usr_plugin_dir ();
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
		plugin_dirs = gnm_strsplit_to_slist (plugin_path_env, ":");
		pm_gui_load_directories (pm_gui, plugin_dirs, FALSE);
		g_slist_foreach (plugin_dirs, (GFunc)g_free, NULL);
		g_slist_free (plugin_dirs);
	}
	pm_gui_load_directories (pm_gui, gnm_app_prefs->plugin_extra_dirs, TRUE);
}

static void
cb_pm_button_directory_add_clicked (G_GNUC_UNUSED GtkButton *button,
				    PluginManagerGUI *pm_gui)
{
	GtkFileChooser *fsel;

	fsel = GTK_FILE_CHOOSER
		(g_object_new (GTK_TYPE_FILE_CHOOSER_DIALOG,
			       "action", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
			       "title", _("Select Directory"),
			       /* We need to force local-only as plugins
				  won't work over the network.  */
			       "local-only", TRUE,
			       NULL));
	gtk_dialog_add_buttons (GTK_DIALOG (fsel),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_ADD, GTK_RESPONSE_OK,
				NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (fsel), GTK_RESPONSE_OK);

	if (gnumeric_dialog_file_selection (pm_gui->wbcg, GTK_WIDGET (fsel))) {
		char *path = gtk_file_chooser_get_filename (fsel);

		if (!g_file_test (path, G_FILE_TEST_IS_DIR)) {
			char *dir_name = g_path_get_dirname (path);
			g_free (path);
			path = dir_name;
		}

		if (g_slist_find_custom ((GSList *)gnm_app_prefs->plugin_extra_dirs,
					 path, gnm_str_compare) == NULL) {
			GSList *extra_dirs = gnm_string_slist_copy (
				gnm_app_prefs->plugin_extra_dirs);
			GNM_SLIST_PREPEND (extra_dirs, path);
			GNM_SLIST_SORT (extra_dirs, gnm_str_compare);

			gnm_gconf_set_plugin_extra_dirs (extra_dirs);
			pm_gui_load_directory_page (pm_gui);
			cb_pm_button_rescan_directories_clicked (pm_gui);
		} else
			g_free (path);
	}

	gtk_widget_destroy (GTK_WIDGET (fsel));
}

static void
cb_pm_button_directory_delete_clicked (G_GNUC_UNUSED GtkButton *button,
				       PluginManagerGUI *pm_gui)
{
	GtkTreeIter iter;
	char     *dir_name = NULL;
	gboolean  is_system = TRUE;

	if (!gtk_tree_selection_get_selected (pm_gui->selection_directory, NULL, &iter))
		return;

	gtk_tree_model_get (GTK_TREE_MODEL (pm_gui->model_directories), &iter,
			    DIR_NAME,		&dir_name,
			    DIR_IS_SYSTEM,	&is_system,
			    -1);

	if (!is_system
	    && g_slist_find_custom ((GSList *)gnm_app_prefs->plugin_extra_dirs,
				    dir_name, gnm_str_compare) != NULL) {

		GSList *extra_dirs = gnm_string_slist_copy (gnm_app_prefs->plugin_extra_dirs);
		GSList *res = g_slist_find_custom (extra_dirs, dir_name, gnm_str_compare);

		g_free (res->data);
		extra_dirs = g_slist_remove (extra_dirs, res->data);

		gnm_gconf_set_plugin_extra_dirs (extra_dirs);
		pm_gui_load_directory_page (pm_gui);
		cb_pm_button_rescan_directories_clicked (pm_gui);
	}
	g_free (dir_name);
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
	const char *plugin_desc;

	g_return_if_fail (pm_gui != NULL);

	g_signal_handlers_disconnect_matched (
		pm_gui->checkbutton_mark_for_deactivation,
		G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
		cb_checkbutton_mark_for_deactivation_toggled, NULL);

	if (!gtk_tree_selection_get_selected (selection, NULL, &iter)) {
		gtk_text_buffer_set_text (pm_gui->text_description, "", 0);
		gtk_entry_set_text (pm_gui->entry_directory, "");
		gtk_tree_store_clear (pm_gui->model_details);
		gtk_widget_hide (pm_gui->frame_mark_for_deactivation);
	} else {
		GtkTreeIter iter2, iter3;
		GSList *dep_ids, *services;

		gtk_tree_model_get (GTK_TREE_MODEL (pm_gui->model_plugins),
		                    &iter, PLUGIN_POINTER, &pinfo, -1);
		plugin_desc = _(gnm_plugin_get_description (pinfo));
		if (plugin_desc == NULL) {
			plugin_desc = "";
		}
		gtk_text_buffer_set_text (
			pm_gui->text_description, plugin_desc, strlen (plugin_desc));
		gtk_entry_set_text (pm_gui->entry_directory, gnm_plugin_get_dir_name (pinfo));

		gtk_tree_store_clear (pm_gui->model_details);
		gtk_tree_store_append (pm_gui->model_details, &iter, NULL);
		gtk_tree_store_set (
			pm_gui->model_details, &iter,
			DETAILS_DESC, gnm_plugin_get_name (pinfo),
			DETAILS_ID, gnm_plugin_get_id (pinfo),
			-1);
		dep_ids = gnm_plugin_get_dependencies_ids (pinfo);
		if (dep_ids != NULL) {
			gtk_tree_store_append (pm_gui->model_details, &iter2, &iter);
			gtk_tree_store_set (
				pm_gui->model_details, &iter2,
				DETAILS_DESC, _("Plugin dependencies"),
				DETAILS_ID, "",
				-1);
			GNM_SLIST_FOREACH (dep_ids, char, dep_id,
				GnmPlugin *dep_plugin;
				const char *name;

				dep_plugin = plugins_get_plugin_by_id (dep_id);
				name =  dep_plugin != NULL ? (char *) gnm_plugin_get_name (dep_plugin) : _("Unknown plugin");
				gtk_tree_store_append (pm_gui->model_details, &iter3, &iter2);
				gtk_tree_store_set (
					pm_gui->model_details, &iter3,
					DETAILS_DESC, name,
					DETAILS_ID, dep_id,
					-1);
			);
		}
		gnm_slist_free_custom (dep_ids, g_free);

		gtk_tree_store_append (pm_gui->model_details, &iter2, &iter);
		gtk_tree_store_set (
			pm_gui->model_details, &iter2,
			DETAILS_DESC, _("Plugin services"),
			DETAILS_ID, "",
			-1);
		services = gnm_plugin_get_services (pinfo);
		GNM_SLIST_FOREACH (services, GnmPluginService, service,
			gtk_tree_store_append (pm_gui->model_details, &iter3, &iter2);
			gtk_tree_store_set (
				pm_gui->model_details, &iter3,
				DETAILS_DESC, plugin_service_get_description (service),
				DETAILS_ID, plugin_service_get_id (service),
				-1);
		);
		gtk_tree_view_expand_all (pm_gui->view_details);

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
pm_dialog_cleanup (G_GNUC_UNUSED GObject *dialog,
		   PluginManagerGUI *pm_gui)
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

	if (pm_gui->gui != NULL) {
		g_object_unref (G_OBJECT (pm_gui->gui));
		pm_gui->gui = NULL;
	}

	pm_gui->dialog_pm = NULL;
	g_free (pm_gui);
}

static void
cb_pm_button_activate_all_clicked (G_GNUC_UNUSED GtkButton *button,
				   PluginManagerGUI *pm_gui)
{
	ErrorInfo *activation_error, *error;

	plugin_db_activate_plugin_list (
		plugins_get_available_plugins (), &activation_error);
	if (activation_error != NULL) {
		error = error_info_new_str_with_details (
			_("Errors while activating plugins"), activation_error);
		gnumeric_error_info_dialog_show (
			GTK_WINDOW (pm_gui->dialog_pm), error);
		error_info_free (error);
	}
}

static void
cb_pm_button_deactivate_all_clicked (G_GNUC_UNUSED GtkButton *button,
				     PluginManagerGUI *pm_gui)
{
	ErrorInfo *deactivation_error, *error;

	plugin_db_deactivate_plugin_list (
		plugins_get_available_plugins (), &deactivation_error);
	if (deactivation_error != NULL) {
		error = error_info_new_str_with_details (
			_("Errors while deactivating plugins"), deactivation_error);
		gnumeric_error_info_dialog_show (
			GTK_WINDOW (pm_gui->dialog_pm), error);
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
	g_signal_connect_swapped (G_OBJECT (pm_gui->button_rescan_directories),
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
				      gnm_app_prefs->activate_new_plugins);

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
cb_pm_dir_selection_changed (G_GNUC_UNUSED GtkTreeSelection *ignored,
			     PluginManagerGUI *pm_gui)
{
	GtkTreeIter  iter;
	gboolean is_system;

	if (!gtk_tree_selection_get_selected (pm_gui->selection_directory, NULL, &iter)) {
		gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_directory_delete), FALSE);
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (pm_gui->model_directories), &iter,
			    DIR_IS_SYSTEM, &is_system,
			    -1);

	gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_directory_delete), !is_system);
}


static void
cb_active_toggled (G_GNUC_UNUSED GtkCellRendererToggle *celltoggle,
		   char *path,
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
				want_activate = gnumeric_dialog_question_yes_no (GTK_WINDOW (pm_gui->dialog_pm), s->str, TRUE);
			}
			g_string_free (s, TRUE);
		}
		gnm_slist_free_custom (dep_ids, g_free);

		if (want_activate) {
			gnm_plugin_activate (plugin, &error);
		} else {
			error = NULL;
		}
	}
	if (error != NULL) {
		ErrorInfo *new_error;

		if (gnm_plugin_is_active (plugin)) {
			new_error = error_info_new_printf (
				_("Error while deactivating plugin \"%s\"."),
				gnm_plugin_get_name (plugin));
		} else {
			new_error = error_info_new_printf (
				_("Error while activating plugin \"%s\"."),
				gnm_plugin_get_name (plugin));
		}
		error_info_add_details (new_error, error);
		gnm_cmd_context_error_info (GNM_CMD_CONTEXT (pm_gui->wbcg), new_error);
	}
}

static void
cb_pm_close_clicked (G_GNUC_UNUSED GtkWidget *button,
			PluginManagerGUI *pm_gui)
{
	gtk_widget_destroy (GTK_WIDGET(pm_gui->dialog_pm));
	return;
}

void
dialog_plugin_manager (WorkbookControlGUI *wbcg)
{
	PluginManagerGUI *pm_gui;
	GladeXML *gui;
	GtkWidget *scrolled;
	GtkWidget *scrolled_directories;
	GtkWidget *hbox;
	GtkTreeViewColumn *column;
	GtkCellRenderer *rend;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	if (gnumeric_dialog_raise_if_exists (wbcg, PLUGIN_MANAGER_DIALOG_KEY))
		return;

	gui = gnm_glade_xml_new (GNM_CMD_CONTEXT (wbcg),
		"plugin-manager.glade", NULL, NULL);
	if (gui == NULL)
		return;

	pm_gui = g_new (PluginManagerGUI, 1);
	pm_gui->wbcg = wbcg;
	pm_gui->gui = gui;
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

	pm_gui->text_description = gtk_text_view_get_buffer (GTK_TEXT_VIEW (
				           glade_xml_get_widget (gui, "textview_plugin_description")));
	pm_gui->entry_directory = GTK_ENTRY (glade_xml_get_widget (gui, "entry_directory"));

	pm_gui->model_details = gtk_tree_store_new (
		DETAILS_NUM_COLMNS, G_TYPE_STRING, G_TYPE_STRING);
	pm_gui->view_details = GTK_TREE_VIEW (
		gtk_tree_view_new_with_model (GTK_TREE_MODEL (pm_gui->model_details)));
	column = gtk_tree_view_column_new_with_attributes (
		_("Description"), gtk_cell_renderer_text_new (),
		"text", DETAILS_DESC, NULL);
	gtk_tree_view_append_column (pm_gui->view_details, column);
	column = gtk_tree_view_column_new_with_attributes (
		_("ID"), gtk_cell_renderer_text_new (),
		"text", DETAILS_ID, NULL);
	gtk_tree_view_append_column (pm_gui->view_details, column);
	scrolled = glade_xml_get_widget (gui, "scrolled_plugin_details");
	gtk_container_add (GTK_CONTAINER (scrolled), GTK_WIDGET (pm_gui->view_details));

	pm_gui->frame_mark_for_deactivation =
		glade_xml_get_widget (gui, "frame_mark_for_deactivation");
	pm_gui->checkbutton_mark_for_deactivation =
		glade_xml_get_widget (gui, "checkbutton_mark_for_deactivation");

	/* Set-up directories page */

	hbox = glade_xml_get_widget (gui, "directory-box");
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
	gtk_button_set_alignment (GTK_BUTTON (pm_gui->button_directory_add), 0., .5);
	pm_gui->button_directory_delete = GTK_BUTTON (glade_xml_get_widget
						  (gui, "button_directory_delete"));
	gtk_button_set_alignment (GTK_BUTTON (pm_gui->button_directory_delete), 0., .5);

	cb_pm_dir_selection_changed (NULL, pm_gui);
	g_signal_connect (pm_gui->selection_directory,
		"changed",
		G_CALLBACK (cb_pm_dir_selection_changed), pm_gui);

	/* Done setting up pages */

	pm_gui->gnotebook = GTK_NOTEBOOK (glade_xml_get_widget (gui, "notebook1"));

	gtk_widget_show_all (GTK_WIDGET (pm_gui->gnotebook));

	pm_gui_load_directory_page (pm_gui);

	pm_dialog_init (pm_gui);
	gnumeric_init_help_button (
		glade_xml_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_PLUGIN_MANAGER);
	g_signal_connect (glade_xml_get_widget (gui, "button_close_manager"),
		"clicked",
		G_CALLBACK (cb_pm_close_clicked), pm_gui);

	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (pm_gui->dialog_pm),
			       PLUGIN_MANAGER_DIALOG_KEY);
	gtk_widget_show (GTK_WIDGET (pm_gui->dialog_pm));
}
