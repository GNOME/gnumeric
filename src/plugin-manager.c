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
#include "plugin.h"

typedef struct 
{
	Workbook  *workbook;
	GtkWidget *dialog;
	GtkWidget *hbox;
	GtkWidget *vbbox;
	GtkWidget *scrollwin;
	GtkWidget *clist;
	GtkWidget *button_add;
	GtkWidget *button_remove;
	GtkWidget *button_close;
} PluginManager;

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
	gtk_widget_set_sensitive (pm->button_remove, 0);
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
	
	pd = plugin_load (pm->workbook, modfile);
	populate_clist (pm);
}

static void
remove_cb (GtkWidget *button, PluginManager *pm)
{
	GList *selection = GTK_CLIST (pm->clist)->selection;
	gint row = GPOINTER_TO_INT (g_list_nth_data (selection, 0));
	PluginData *pd = gtk_clist_get_row_data (GTK_CLIST (pm->clist), row);
	
	plugin_unload (pm->workbook, pd);
	populate_clist (pm);
}

static void
row_cb (GtkWidget * clist, gint row, gint col,
	       GdkEvent *event,  PluginManager *pm)
{
	if (GTK_CLIST(clist)->selection != NULL)
		gtk_widget_set_sensitive (pm->button_remove, 1);
	else
		gtk_widget_set_sensitive (pm->button_remove, 0);
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
	pm->dialog = gtk_window_new (GTK_WINDOW_DIALOG);
	
	pm->hbox = gtk_hbox_new (0, 0);
	gtk_container_add (GTK_CONTAINER (pm->dialog), pm->hbox);

	pm->scrollwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (pm->scrollwin),
					GTK_POLICY_AUTOMATIC,
					GTK_POLICY_AUTOMATIC); 
	gtk_box_pack_start (GTK_BOX (pm->hbox), pm->scrollwin, 1, 1, 5);

	titles [0] = _(n_titles [0]);
	titles [1] = _(n_titles [1]);
	
	pm->clist = gtk_clist_new_with_titles (2, titles);
	gtk_clist_column_titles_passive (GTK_CLIST (pm->clist));
	gtk_widget_set_usize (pm->clist, 300, 120);
	gtk_container_add (GTK_CONTAINER (pm->scrollwin), pm->clist);
	
	pm->vbbox = gtk_vbutton_box_new ();
	gtk_button_box_set_layout (GTK_BUTTON_BOX (pm->vbbox), GTK_BUTTONBOX_START);
	gtk_box_pack_start (GTK_BOX (pm->hbox), pm->vbbox, 0, 0, 5);
	
	pm->button_close = gtk_button_new_with_label (_("Close"));
	gtk_container_add (GTK_CONTAINER(pm->vbbox), pm->button_close);
	
	pm->button_add = gtk_button_new_with_label (_("Add..."));
	gtk_container_add (GTK_CONTAINER (pm->vbbox), pm->button_add);
	
	pm->button_remove = gtk_button_new_with_label (_("Remove"));
	gtk_container_add (GTK_CONTAINER (pm->vbbox), pm->button_remove);
	
	gtk_widget_realize (pm->clist);
	populate_clist (pm);
	
	gtk_signal_connect (GTK_OBJECT (pm->button_close), "clicked",
			    GTK_SIGNAL_FUNC (close_cb), pm);
	
	gtk_signal_connect(GTK_OBJECT (pm->button_add), "clicked",
			   GTK_SIGNAL_FUNC (add_cb), pm);
	
	gtk_signal_connect (GTK_OBJECT (pm->button_remove), "clicked",
			   GTK_SIGNAL_FUNC (remove_cb), pm);
	
	gtk_signal_connect(GTK_OBJECT (pm->clist), "select_row",
			   GTK_SIGNAL_FUNC (row_cb), pm);
	
	gtk_signal_connect(GTK_OBJECT (pm->clist), "unselect_row",
			   GTK_SIGNAL_FUNC (row_cb), pm);
	
	gtk_widget_show_all (pm->hbox);
	
	return pm->dialog;
}
