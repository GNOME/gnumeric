/*
 * plugin-manager.c: Dialog used to load plugins into the Gnumeric
 * spreadsheet
 *
 * Author:
 *   Tom Dyas (tdyas@romulus.rutgers.edu)
 */
#include <config.h>
#include <glib.h>
#include <gnome.h>
#include "gnumeric.h"
#include "dialogs.h"
#include "workbook.h"
#include "plugin.h"

typedef struct 
{
	Workbook  *workbook;
	GtkWidget *dialog;
	GtkWidget *scrollwin;
	GtkWidget *clist;
} PluginManager;

#define BUTTON_ADD     0
#define BUTTON_REMOVE  BUTTON_ADD + 1
#define BUTTON_CLOSE   BUTTON_REMOVE + 1

static void
add_to_clist (PluginData *pd, GtkWidget *clist)
{
	gchar *data[2];
	gint row;
	
	data [0] = pd->title;
	data [1] = g_module_name (pd->handle);
	
	row = gtk_clist_append (GTK_CLIST (clist), data);
	gtk_clist_set_row_data (GTK_CLIST (clist), row, pd);
}

static void
populate_clist (PluginManager *pm)
{
	gtk_clist_freeze (GTK_CLIST (pm->clist));
	gtk_clist_clear (GTK_CLIST (pm->clist));
	g_list_foreach (plugin_list, (GFunc) add_to_clist, pm->clist);
	gtk_clist_thaw (GTK_CLIST (pm->clist));
}

static void
close_cb (GtkWidget *button, PluginManager *pm)
{
	gtk_widget_destroy (pm->dialog);
}

static void
add_cb (GtkWidget *button, PluginManager *pm)
{
	char *modfile = dialog_query_load_file (pm->workbook);
	PluginData *pd;
	
	if (!modfile)
		return;
	
	pd = plugin_load (workbook_command_context_gui (pm->workbook),
			  modfile);
	populate_clist (pm);
}

static void
remove_cb (GtkWidget *button, PluginManager *pm)
{
	GList *selection = GTK_CLIST (pm->clist)->selection;
	gint row = GPOINTER_TO_INT (g_list_nth_data (selection, 0));
	PluginData *pd = gtk_clist_get_row_data (GTK_CLIST (pm->clist), row);
	
	plugin_unload (workbook_command_context_gui (pm->workbook), pd);
	populate_clist (pm);
	if (GTK_CLIST (pm->clist)->rows > row)
		gtk_clist_select_row(GTK_CLIST (pm->clist), row, 0);
	else
		gnome_dialog_set_sensitive (GNOME_DIALOG (pm->dialog),
					    BUTTON_REMOVE, FALSE);
}

static void
row_cb (GtkWidget * clist, gint row, gint col,
	       GdkEvent *event,  PluginManager *pm)
{
	if (GTK_CLIST (clist)->selection != NULL)
		gnome_dialog_set_sensitive (GNOME_DIALOG (pm->dialog),
					    BUTTON_REMOVE, TRUE);
	else
		gnome_dialog_set_sensitive (GNOME_DIALOG (pm->dialog),
					    BUTTON_REMOVE, FALSE);
}

static gint
pm_key_event (GtkWidget *pm, GdkEventKey *event)
{
	if (event->keyval == GDK_Escape) {
		gtk_widget_destroy (pm);
		return 1;
	} else
		return 0;
}

GtkWidget *
plugin_manager_new (Workbook *wb)
{
	PluginManager *pm;
	gchar *n_titles[2] = { N_("Name"), N_("File") };
	gchar *titles[2] = { N_("Name"), N_("File") };
	
	pm = g_new0 (PluginManager, 1);
	if (!pm)
		return NULL;

	pm->workbook = wb;
	pm->dialog = gnome_dialog_new(_("Plug-in Manager"),
				      _("Add"),
				      _("Remove"),
				      GNOME_STOCK_BUTTON_CLOSE,
				      NULL);

	pm->scrollwin = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (pm->scrollwin),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (pm->dialog)->vbox),
			    pm->scrollwin, 1, 1, 5);

	titles [0] = _(n_titles [0]);
	titles [1] = _(n_titles [1]);
	
	pm->clist = gtk_clist_new_with_titles (2, titles);
	gtk_clist_column_titles_passive (GTK_CLIST (pm->clist));
	gtk_widget_set_usize (pm->clist, 500, 120);
	gtk_container_add (GTK_CONTAINER (pm->scrollwin), pm->clist);
	
	gtk_widget_realize (pm->clist);
	populate_clist (pm);

	if (GTK_CLIST (pm->clist)->rows > 0) {
		gtk_widget_grab_focus (pm->clist);
		gtk_clist_select_row(GTK_CLIST (pm->clist), 0, 0);
	} else {
		gnome_dialog_set_sensitive (GNOME_DIALOG (pm->dialog),
					    BUTTON_REMOVE, FALSE);
	}
	
	gnome_dialog_button_connect(GNOME_DIALOG (pm->dialog), BUTTON_ADD,
				    GTK_SIGNAL_FUNC (add_cb), pm);
				    
	gnome_dialog_button_connect(GNOME_DIALOG (pm->dialog), BUTTON_REMOVE,
				    GTK_SIGNAL_FUNC (remove_cb), pm);
					
	gnome_dialog_button_connect(GNOME_DIALOG (pm->dialog), BUTTON_CLOSE,
				    GTK_SIGNAL_FUNC (close_cb), pm);
						
	gtk_signal_connect (GTK_OBJECT (pm->clist), "select_row",
			    GTK_SIGNAL_FUNC (row_cb), pm);
	
	gtk_signal_connect (GTK_OBJECT (pm->clist), "unselect_row",
			    GTK_SIGNAL_FUNC (row_cb), pm);
	
	gtk_signal_connect (GTK_OBJECT (pm->dialog), "key_press_event",
			    GTK_SIGNAL_FUNC (pm_key_event), NULL);

	gnome_dialog_set_default (GNOME_DIALOG (pm->dialog), BUTTON_ADD);
	gtk_window_set_policy (GTK_WINDOW (pm->dialog), FALSE, TRUE, FALSE);

	gtk_widget_show_all (GNOME_DIALOG (pm->dialog)->vbox);
	
	return pm->dialog;
}
