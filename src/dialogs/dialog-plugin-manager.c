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
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <command-context.h>
#include <workbook-control.h>
#include <workbook.h>
#include <plugin.h>

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-config.h>
#include <string.h>
#include <glade/glade.h>
#include <gal/util/e-util.h>

/* ---------- GnumericNotebook ---------- */

typedef struct _GnumericNotebook GnumericNotebook;
typedef struct _GnumericNotebookClass GnumericNotebookClass;

struct _GnumericNotebook
{
	GtkNotebook widget;

	GList *disabled_pages;
};

struct _GnumericNotebookClass
{
	GtkNotebookClass parent_class;
};

#define TYPE_GNUMERIC_NOTEBOOK                  (gnumeric_notebook_get_type ())
#define GNUMERIC_NOTEBOOK(obj)                  (GTK_CHECK_CAST ((obj), TYPE_GNUMERIC_NOTEBOOK, GnumericNotebook))
#define GNUMERIC_NOTEBOOK_CLASS(klass)          (GTK_CHECK_CLASS_CAST ((klass), TYPE_GNUMERIC_NOTEBOOK, GnumericNotebookClass))
#define IS_GNUMERIC_NOTEBOOK(obj)               (GTK_CHECK_TYPE ((obj), TYPE_GNUMERIC_NOTEBOOK))
#define IS_GNUMERIC_NOTEBOOK_CLASS(klass)       (GTK_CHECK_CLASS_TYPE ((klass), TYPE_GNUMERIC_NOTEBOOK))

guint gnumeric_notebook_get_type (void);

GtkWidget *gnumeric_notebook_new (void);
void       gnumeric_notebook_set_page_enabled (GnumericNotebook *mn, gint page_num, gboolean enabled);

static GtkWidgetClass *gnumeric_notebook_parent_class = NULL;

static void
gnumeric_notebook_init (GnumericNotebook *mn)
{
	mn->disabled_pages = NULL;
}

static void
gnumeric_notebook_destroy (GtkObject *obj)
{
	GnumericNotebook *mn;

	g_return_if_fail (IS_GNUMERIC_NOTEBOOK (obj));

	mn = GNUMERIC_NOTEBOOK (obj);

	g_list_free (mn->disabled_pages);

	GTK_OBJECT_CLASS (gnumeric_notebook_parent_class)->destroy (obj);
}

static void
gnumeric_notebook_switch_page (GtkNotebook *notebook, GtkNotebookPage *page, guint page_num)
{
	GnumericNotebook *mn;

	g_return_if_fail (IS_GNUMERIC_NOTEBOOK (notebook));
	
	mn = GNUMERIC_NOTEBOOK (notebook);
	if (g_list_find (mn->disabled_pages, GINT_TO_POINTER ((gint) page_num)) == NULL) {
		GTK_NOTEBOOK_CLASS (gnumeric_notebook_parent_class)->switch_page (notebook, page, page_num);
	}
}

static void
gnumeric_notebook_class_init (GnumericNotebookClass *klass)
{
	gnumeric_notebook_parent_class = gtk_type_class (gtk_notebook_get_type ());

	GTK_OBJECT_CLASS (klass)->destroy = gnumeric_notebook_destroy;
	GTK_NOTEBOOK_CLASS (klass)->switch_page = gnumeric_notebook_switch_page;
}

guint
gnumeric_notebook_get_type (void)
{
	static guint gnumeric_notebook_type = 0;
  
	if (!gnumeric_notebook_type) {
		static const GtkTypeInfo gnumeric_notebook_info =
		{
			"GnumericNotebook",
			sizeof (GnumericNotebook),
			sizeof (GnumericNotebookClass),
			(GtkClassInitFunc) gnumeric_notebook_class_init,
			(GtkObjectInitFunc) gnumeric_notebook_init,
			NULL,
			NULL,
			(GtkClassInitFunc) NULL
		};

		gnumeric_notebook_type = gtk_type_unique (gtk_notebook_get_type (), &gnumeric_notebook_info);
	}

	return gnumeric_notebook_type;
};

GtkWidget*
gnumeric_notebook_new (void)
{
	return GTK_WIDGET (gtk_type_new (gnumeric_notebook_get_type ()));
}

void
gnumeric_notebook_set_page_enabled (GnumericNotebook *mn, gint page_num, gboolean enabled)
{
	GtkWidget *tab = gtk_notebook_get_nth_page (GTK_NOTEBOOK (mn), page_num);
	GtkWidget *label;

	g_return_if_fail (IS_GNUMERIC_NOTEBOOK (mn));
	g_return_if_fail (tab != NULL);

	label = gtk_notebook_get_tab_label (GTK_NOTEBOOK (mn), tab);
	gtk_widget_set_sensitive (label, enabled);

	mn->disabled_pages = g_list_remove (mn->disabled_pages, GINT_TO_POINTER (page_num));
	if (!enabled) {
		mn->disabled_pages = g_list_prepend (mn->disabled_pages, GINT_TO_POINTER (page_num));
	}
}
                                                                               
/* ---------- */                  

typedef struct {
	WorkbookControlGUI *wbcg;
	GnomeDialog *dialog_pm;
	GnumericNotebook *gnotebook;
	gint plugin_list_page_no, plugin_details_page_no;
	GtkCList *clist_active, *clist_inactive;
	GtkButton *button_activate_plugin, *button_deactivate_plugin,
	          *button_activate_all, *button_deactivate_all,
	          *button_install_plugin;
	GtkCheckButton *checkbutton_install_new;
	GtkEntry *entry_name, *entry_directory, *entry_id;
	GtkTextView *text_description;
	GtkCList *clist_extra_info;
	gchar *current_plugin_id;
} PluginManagerGUI;


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
		GList *l;
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
			        "Do you want to activate appropriate plugin together with this one?\n"
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
cb_pm_button_install_plugin_clicked (GtkButton *button, PluginManagerGUI *pm_gui)
{
	g_warning ("Not implemented");
	/* FIXME */
}

static void
cb_pm_checkbutton_install_new_toggled (GtkCheckButton *checkbutton, PluginManagerGUI *pm_gui)
{
	gnome_config_set_bool ("Gnumeric/Plugin/ActivateNewByDefault",
	                       gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton)));
}

static void
cb_pm_clist_row_selected (GtkCList *clist, gint row_no, gint col_no, gpointer unused, PluginManagerGUI *pm_gui)
{
	PluginInfo *pinfo;

	g_return_if_fail (pm_gui != NULL);

	g_free (pm_gui->current_plugin_id);
	pm_gui->current_plugin_id = g_strdup (gtk_clist_get_row_data (clist, row_no));
	pinfo = plugin_db_get_plugin_info_by_plugin_id (pm_gui->current_plugin_id);
	g_return_if_fail (pinfo != NULL);
	if (clist == pm_gui->clist_active) {
		gtk_clist_unselect_all (pm_gui->clist_inactive);
		if (plugin_db_is_plugin_marked_for_deactivation (pinfo)) {
			gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_activate_plugin), TRUE);
		} else {
			gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_activate_plugin), FALSE);
		}
		gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_deactivate_plugin), TRUE);
	} else {
		gtk_clist_unselect_all (pm_gui->clist_active);
		gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_activate_plugin), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_deactivate_plugin), FALSE);
	}
	update_plugin_details_view (pm_gui);
	gnumeric_notebook_set_page_enabled (pm_gui->gnotebook,
	                                    pm_gui->plugin_details_page_no,
	                                    TRUE);
}

static void
cb_pm_clist_row_unselected (GtkCList *clist, gint row_no, gint col_no, gpointer unused, PluginManagerGUI *pm_gui)
{
	gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_activate_plugin), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_deactivate_plugin), FALSE);
	update_plugin_details_view (pm_gui);
	gnumeric_notebook_set_page_enabled (pm_gui->gnotebook,
	                                    pm_gui->plugin_details_page_no,
	                                    FALSE);
}

static void
pm_dialog_init (PluginManagerGUI *pm_gui)
{
	gtk_signal_connect (GTK_OBJECT (pm_gui->clist_active), "select_row",
	                    (GtkSignalFunc) cb_pm_clist_row_selected,
	                    (gpointer) pm_gui);
	gtk_signal_connect (GTK_OBJECT (pm_gui->clist_active), "unselect_row",
	                    (GtkSignalFunc) cb_pm_clist_row_unselected,
	                    (gpointer) pm_gui);
	gtk_signal_connect (GTK_OBJECT (pm_gui->clist_inactive), "select_row",
	                    (GtkSignalFunc) cb_pm_clist_row_selected,
	                    (gpointer) pm_gui);
	gtk_signal_connect (GTK_OBJECT (pm_gui->clist_inactive), "unselect_row",
	                    (GtkSignalFunc) cb_pm_clist_row_unselected,
	                    (gpointer) pm_gui);
	gtk_signal_connect (GTK_OBJECT (pm_gui->button_activate_plugin), "clicked",
	                    (GtkSignalFunc) cb_pm_button_activate_plugin_clicked,
	                    (gpointer) pm_gui);
	gtk_signal_connect (GTK_OBJECT (pm_gui->button_deactivate_plugin), "clicked",
	                    (GtkSignalFunc) cb_pm_button_deactivate_plugin_clicked,
	                    (gpointer) pm_gui);
	gtk_signal_connect (GTK_OBJECT (pm_gui->button_activate_all), "clicked",
	                    (GtkSignalFunc) cb_pm_button_activate_all_clicked,
	                    (gpointer) pm_gui);
	gtk_signal_connect (GTK_OBJECT (pm_gui->button_deactivate_all), "clicked",
	                    (GtkSignalFunc) cb_pm_button_deactivate_all_clicked,
	                    (gpointer) pm_gui);
	gtk_signal_connect (GTK_OBJECT (pm_gui->button_install_plugin), "clicked",
	                    (GtkSignalFunc) cb_pm_button_install_plugin_clicked,
	                    (gpointer) pm_gui);
	gtk_signal_connect (GTK_OBJECT (pm_gui->checkbutton_install_new), "toggled",
	                    (GtkSignalFunc) cb_pm_checkbutton_install_new_toggled,
	                    (gpointer) pm_gui);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pm_gui->checkbutton_install_new),
	                              gnome_config_get_bool_with_default ("Gnumeric/Plugin/ActivateNewByDefault=true", NULL));
	update_plugin_manager_view (pm_gui);
}

static void
free_plugin_id (gpointer data)
{
	g_free (data);
}

static gint
plugin_compare_name (gconstpointer a, gconstpointer b)
{
	PluginInfo *plugin_a = (PluginInfo *) a, *plugin_b = (PluginInfo *) b;

	return strcoll (plugin_info_peek_name (plugin_a),
	                plugin_info_peek_name (plugin_b));
}

static void
update_plugin_manager_view (PluginManagerGUI *pm_gui)
{
	GList *sorted_plugin_list, *l;
	gint n_active_plugins, n_inactive_plugins;

	sorted_plugin_list = g_list_sort (g_list_copy (plugin_db_get_available_plugin_info_list ()),
	                                  &plugin_compare_name);

	gtk_clist_freeze (pm_gui->clist_active);
	gtk_clist_freeze (pm_gui->clist_inactive);
	gtk_clist_clear (pm_gui->clist_active);
	gtk_clist_clear (pm_gui->clist_inactive);
	n_active_plugins = 0;
	n_inactive_plugins = 0;
	for (l = sorted_plugin_list; l != NULL; l = l->next) {
		PluginInfo *pinfo;
		GtkCList *clist;
		gchar *cols[] = {NULL};
		gchar *plugin_id;
		gint row_no;

		pinfo = (PluginInfo *) l->data;
		if (plugin_info_is_active (pinfo)) {
			gboolean is_in_mem, is_marked;

			clist = pm_gui->clist_active;
			n_active_plugins++;
			is_in_mem = plugin_info_is_loaded (pinfo);
			is_marked = plugin_db_is_plugin_marked_for_deactivation (pinfo);
			if (is_in_mem && is_marked) {
				cols[0] = g_strdup_printf (_("%s[in memory, marked for deactivation]"),
				          plugin_info_peek_name (pinfo));
			} else if (is_in_mem) {
				cols[0] = g_strdup_printf (_("%s[in memory]"),
				          plugin_info_peek_name (pinfo));
			} else if (is_marked) {
				cols[0] = g_strdup_printf (_("%s[marked for deactivation]"),
				          plugin_info_peek_name (pinfo));
			} else {
				cols[0] = plugin_info_get_name (pinfo);
			}
		} else {
			clist = pm_gui->clist_inactive;
			n_inactive_plugins++;
			cols[0] = plugin_info_get_name (pinfo);
		}
		row_no = gtk_clist_append (clist, cols);
		plugin_id = plugin_info_get_id (pinfo);
		gtk_clist_set_row_data_full (clist, row_no, plugin_id, &free_plugin_id);
		g_free (cols[0]);
	}
	gtk_clist_thaw (pm_gui->clist_active);
	gtk_clist_thaw (pm_gui->clist_inactive);

	g_list_free (sorted_plugin_list);

	g_free (pm_gui->current_plugin_id);
	pm_gui->current_plugin_id = NULL;
	gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_activate_plugin), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_deactivate_plugin), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_activate_all), n_inactive_plugins > 0);
	gtk_widget_set_sensitive (GTK_WIDGET (pm_gui->button_deactivate_all), n_active_plugins > 0);
	update_plugin_details_view (pm_gui);
	gnumeric_notebook_set_page_enabled (pm_gui->gnotebook,
	                                    pm_gui->plugin_details_page_no,
	                                    FALSE);
}

static void
update_plugin_details_view (PluginManagerGUI *pm_gui)
{
	PluginInfo *pinfo;
	gint txt_pos;
	gint n_extra_info_items, i;
	GList *extra_info_keys, *extra_info_values, *lkey, *lvalue;

	g_return_if_fail (pm_gui != NULL);

	if (pm_gui->current_plugin_id != NULL) {
		pinfo = plugin_db_get_plugin_info_by_plugin_id (pm_gui->current_plugin_id);
		gtk_entry_set_text (pm_gui->entry_name, plugin_info_peek_name (pinfo));
		gtk_entry_set_text (pm_gui->entry_directory, plugin_info_peek_dir_name (pinfo));
		gtk_entry_set_text (pm_gui->entry_id, plugin_info_peek_id (pinfo));
		gtk_editable_delete_text (GTK_EDITABLE (pm_gui->text_description), 0, -1);

		txt_pos = 0;
		gtk_editable_insert_text (GTK_EDITABLE (pm_gui->text_description),
		                          plugin_info_peek_description (pinfo),
		                          strlen (plugin_info_peek_description (pinfo)),
		                          &txt_pos);

		n_extra_info_items = plugin_info_get_extra_info_list (pinfo, &extra_info_keys, &extra_info_values);
		gtk_clist_clear (pm_gui->clist_extra_info);
		if (n_extra_info_items > 0) {
			gtk_clist_freeze (pm_gui->clist_extra_info);
			for (i = 0, lkey = extra_info_keys, lvalue = extra_info_values;
			     i < n_extra_info_items;
			     i++, lkey = lkey->next, lvalue = lvalue->next ) {
				gchar *row[2];

				row[0] = (gchar *) lkey->data;
				row[1] = (gchar *) lvalue->data;
				gtk_clist_append (pm_gui->clist_extra_info, row);
			}
			gtk_clist_thaw (pm_gui->clist_extra_info);
			e_free_string_list (extra_info_keys);
			e_free_string_list (extra_info_values);
		}
	} else {
		gtk_entry_set_text (pm_gui->entry_name, "");
		gtk_entry_set_text (pm_gui->entry_directory, "");
		gtk_entry_set_text (pm_gui->entry_id, "");
		gtk_editable_delete_text (GTK_EDITABLE (pm_gui->text_description), 0, -1);
		gtk_clist_clear (pm_gui->clist_extra_info);
	}
}

void
dialog_plugin_manager (WorkbookControlGUI *wbcg)
{
	PluginManagerGUI *pm_gui;
	GladeXML *gui;
	GtkWidget *page_plugin_list, *page_plugin_details;

	g_return_if_fail (wbcg != NULL);

	gui = gnumeric_glade_xml_new (wbcg, "plugin-manager.glade");
	g_return_if_fail (gui != NULL);

	pm_gui = g_new (PluginManagerGUI, 1);
	pm_gui->wbcg = wbcg;
	pm_gui->dialog_pm = GNOME_DIALOG (glade_xml_get_widget (gui, "dialog_plugin_manager"));
	pm_gui->clist_active = GTK_CLIST (glade_xml_get_widget (gui, "clist_active"));
	pm_gui->clist_inactive = GTK_CLIST (glade_xml_get_widget (gui, "clist_inactive"));
	pm_gui->button_activate_plugin = GTK_BUTTON (glade_xml_get_widget (gui, "button_activate_plugin"));
	pm_gui->button_deactivate_plugin = GTK_BUTTON (glade_xml_get_widget (gui, "button_deactivate_plugin"));
	pm_gui->button_activate_all = GTK_BUTTON (glade_xml_get_widget (gui, "button_activate_all"));
	pm_gui->button_deactivate_all = GTK_BUTTON (glade_xml_get_widget (gui, "button_deactivate_all"));
	pm_gui->button_install_plugin = GTK_BUTTON (glade_xml_get_widget (gui, "button_install_plugin"));
	pm_gui->checkbutton_install_new = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "checkbutton_install_new"));
	pm_gui->entry_name = GTK_ENTRY (glade_xml_get_widget (gui, "entry_name"));
	pm_gui->entry_directory = GTK_ENTRY (glade_xml_get_widget (gui, "entry_directory"));
	pm_gui->text_description = GTK_TEXT_VIEW (glade_xml_get_widget (gui, "text_description"));
	pm_gui->entry_id = GTK_ENTRY (glade_xml_get_widget (gui, "entry_id"));
	pm_gui->clist_extra_info = GTK_CLIST (glade_xml_get_widget (gui, "clist_extra_info"));
	page_plugin_list = glade_xml_get_widget (gui, "page_plugin_list");
	page_plugin_details = glade_xml_get_widget (gui, "page_plugin_details");
	
	g_return_if_fail (pm_gui->dialog_pm != NULL &&
	                  pm_gui->clist_active != NULL && pm_gui->clist_inactive != NULL &&
	                  pm_gui->button_activate_plugin != NULL &&
	                  pm_gui->button_deactivate_plugin != NULL &&
	                  pm_gui->button_install_plugin != NULL &&
	                  pm_gui->checkbutton_install_new != NULL &&
	                  pm_gui->entry_name != NULL && pm_gui->entry_directory != NULL &&
	                  pm_gui->text_description != NULL &&
	                  pm_gui->entry_id != NULL &&
	                  pm_gui->clist_extra_info != NULL &&
	                  page_plugin_list != NULL &&
	                  page_plugin_details != NULL);

	gtk_clist_column_titles_passive (pm_gui->clist_active);
	gtk_clist_column_titles_passive (pm_gui->clist_inactive);
	gtk_clist_column_titles_passive (pm_gui->clist_extra_info);

	pm_gui->gnotebook = GNUMERIC_NOTEBOOK (gnumeric_notebook_new ());
	gtk_widget_reparent (page_plugin_list, GTK_WIDGET (pm_gui->gnotebook));
	gtk_widget_reparent (page_plugin_details, GTK_WIDGET (pm_gui->gnotebook));
	gtk_notebook_set_tab_label (GTK_NOTEBOOK (pm_gui->gnotebook),
	                            page_plugin_list,
	                            gtk_label_new (_("Plugin list")));
	gtk_notebook_set_tab_label (GTK_NOTEBOOK (pm_gui->gnotebook),
	                            page_plugin_details,
	                            gtk_label_new (_("Plugin details")));
	pm_gui->plugin_list_page_no = 0;
	pm_gui->plugin_details_page_no = 1;
	gtk_widget_show_all (GTK_WIDGET (pm_gui->gnotebook));
	gtk_box_pack_start_defaults (GTK_BOX (pm_gui->dialog_pm->vbox),
	                             GTK_WIDGET (pm_gui->gnotebook));

	pm_gui->current_plugin_id = NULL;
	pm_dialog_init (pm_gui);
	(void) gnumeric_dialog_run (wbcg, pm_gui->dialog_pm);
	gnome_config_sync ();

	g_free (pm_gui->current_plugin_id);
	g_free (pm_gui);

	g_object_unref (G_OBJECT (gui));
}
