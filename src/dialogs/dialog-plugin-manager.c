/* -*- mode: c; c-basic-offset: 8 -*- */
/*
 * plugin-manager.c: Dialog used to load plugins into the Gnumeric
 * spreadsheet
 *
 * Author:
 *   Dom Lachowicz (dominicl@seas.upenn.edu)
 *   Tom Dyas (tdyas@romulus.rutgers.edu)
 */

#include <config.h>
#include <glib.h>
#include <gnome.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <glade/glade.h>

#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "plugin.h"
#include "workbook.h"

#define GLADE_FILE "plugin-manager.glade"

typedef struct 
{
	Workbook  *workbook;
        GtkWidget *dialog;
        GtkWidget *clist;
	GtkWidget *file_name_lbl;
        GtkWidget *desc_text;	
        GtkWidget *size_lbl;	
        GtkWidget *modified_lbl;
} PluginManager;

enum { BUTTON_ADD = 0, BUTTON_REMOVE, BUTTON_CLOSE };

/*
 * Add one plugin's title to the clist
 */
static void
add_to_clist (PluginData *pd, GtkWidget *clist)
{
	const gchar *data[1];
	gint         row;
	
	data [0] = plugin_data_get_title (pd);
	row = gtk_clist_append (GTK_CLIST (clist), (gchar **)data);
	gtk_clist_set_row_data (GTK_CLIST (clist), row, pd);
}

/*
 * Add all of the plugins in plugin_list to the clist
 */
static void
populate_clist (PluginManager *pm)
{
        GtkCList *clist = GTK_CLIST (pm->clist);
	
	gtk_clist_freeze (clist);
	gtk_clist_clear  (clist);
	g_list_foreach   (plugin_list, (GFunc) add_to_clist, clist);
	gtk_clist_thaw   (clist);
}

/*
 * Handle key events
 * Currently we only handle "ESC" - should destroy the widget
 * But we may expand in the future to have keybindings
 */
static gint
key_event_cb (GtkWidget *dialog, GdkEventKey *event)
{
	if (event->keyval == GDK_Escape) {
		gtk_widget_destroy (dialog);
		return 1;
	}
	return 0;
}

/*
 * Refresh/update the info in the right frame
 * If pd is NULL, reset everything to its default value
 */
static void
refresh_right_frame (PluginData *pd, PluginManager *pm)
{
	gchar *str;

	if (!pd)
		str = g_strdup (_("File name:"));
	else
		str = g_strdup_printf (_("File name: %s"), plugin_data_get_filename (pd));
	gtk_label_set_text (GTK_LABEL (pm->file_name_lbl), str);
	g_free (str);
	
	if (!pd)
		str = g_strdup (_("Size:"));
	else
		str = g_strdup_printf (_("Size: %ld bytes"), plugin_data_get_size (pd));
	gtk_label_set_text (GTK_LABEL (pm->size_lbl), str);
	g_free (str);
	
	if (!pd)
		str = g_strdup (_("Modified:"));
	else {
		long l = plugin_data_last_modified (pd);
		str = g_strdup_printf (_("Modified: %s"), ctime (&l));
		str [strlen (str) - 1] = '\0'; /* get rid of '\n' */
	}
	gtk_label_set_text (GTK_LABEL (pm->modified_lbl), str);
	g_free (str);
	
	gtk_text_set_editable (GTK_TEXT (pm->desc_text), TRUE);
	gtk_text_freeze (GTK_TEXT (pm->desc_text));
	gtk_text_backward_delete (GTK_TEXT (pm->desc_text), gtk_text_get_length (GTK_TEXT (pm->desc_text)));
	
	if (pd && plugin_data_get_descr (pd))
		gtk_text_insert (GTK_TEXT (pm->desc_text), NULL, NULL, NULL,
				 plugin_data_get_descr (pd), -1);
	
	gtk_text_thaw (GTK_TEXT (pm->desc_text));
	gtk_text_set_editable (GTK_TEXT (pm->desc_text), FALSE);
}

/*
 * User hit the "Add" button, so we'll try to have gnumeric load
 * the given plugin and add it to our dialog
 */
static void
add_cb (PluginManager *pm)
{
	char *modfile = dialog_query_load_file (pm->workbook);
	PluginData *pd = NULL;
	
	if (!modfile)
		return; /* user hit 'cancel' */
	
	pd = plugin_load (workbook_command_context_gui (pm->workbook), modfile);
	
	if (pd)
	        populate_clist (pm);
	
	gtk_clist_select_row (GTK_CLIST (pm->clist), 0, 0);
}

/*
 * User hit the "Remove" button
 */
static void
remove_cb (PluginManager *pm)
{
	GList *selection = GTK_CLIST (pm->clist)->selection;
	gint row = GPOINTER_TO_INT (g_list_nth_data (selection, 0));
	PluginData *pd = gtk_clist_get_row_data (GTK_CLIST (pm->clist), row);
	
	plugin_unload (workbook_command_context_gui (pm->workbook), pd);
	populate_clist (pm);
	
	if (GTK_CLIST (pm->clist)->rows > row)
		gtk_clist_select_row (GTK_CLIST (pm->clist), row, 0);
	else {
		gnome_dialog_set_sensitive (GNOME_DIALOG (pm->dialog),
					    BUTTON_REMOVE, FALSE);
		refresh_right_frame (NULL, pm);
	}
}

/*
 * Callback to either enable or disable the 'Remove' button
 * Depending on whether something is highlighted or not
 */
static void
row_cb (GtkWidget * clist, gint row, gint col,
	GdkEvent *event,  PluginManager *pm)
{
	GtkCList *list = GTK_CLIST (clist);
	PluginData *pd = gtk_clist_get_row_data (list, row);
	gboolean is_selected = (list->selection != NULL);
	
	gnome_dialog_set_sensitive (GNOME_DIALOG (pm->dialog),
				    BUTTON_REMOVE, is_selected);
	if (is_selected)
	        refresh_right_frame (pd, pm);
}

/*
 * Creates the plugin manager dialog
 */
static void
dialog_plugin_manager_impl (Workbook *wb, GladeXML *gui)
{
	PluginManager pm;
	int bval;
	
	pm.workbook = wb;
	
	/* load the dialog from our glade file */
	pm.dialog        = glade_xml_get_widget (gui, "dialog");
	pm.clist         = glade_xml_get_widget (gui, "plugin_clist");
	pm.file_name_lbl = glade_xml_get_widget (gui, "name_lbl");
	pm.desc_text     = glade_xml_get_widget (gui, "desc_text");
	pm.size_lbl      = glade_xml_get_widget (gui, "size_lbl");
	pm.modified_lbl  = glade_xml_get_widget (gui, "modified_lbl");

	if (!pm.dialog || !pm.clist || !pm.file_name_lbl || !pm.desc_text ||
	    !pm.size_lbl || !pm.modified_lbl) {
		g_warning ("Stale glade file");
		return;
	}
	
	populate_clist (&pm);
	
	gtk_signal_connect (GTK_OBJECT (pm.clist), "select_row",
			    GTK_SIGNAL_FUNC (row_cb), &pm);
	
	gtk_signal_connect (GTK_OBJECT (pm.clist), "unselect_row",
			    GTK_SIGNAL_FUNC (row_cb), &pm);
	
	gtk_signal_connect (GTK_OBJECT (pm.dialog), "key_press_event",
			    GTK_SIGNAL_FUNC (key_event_cb), NULL);
	
	gtk_text_set_word_wrap (GTK_TEXT (pm.desc_text), TRUE);
	gtk_clist_column_titles_passive (GTK_CLIST (pm.clist));	

	if (GTK_CLIST (pm.clist)->rows > 0) {
		gtk_widget_grab_focus (pm.clist);
		gtk_clist_select_row (GTK_CLIST (pm.clist), 0, 0);
	} else
		gnome_dialog_set_sensitive (GNOME_DIALOG (pm.dialog),
					    BUTTON_REMOVE, FALSE);
	
	gnome_dialog_set_default (GNOME_DIALOG (pm.dialog), BUTTON_ADD);
	
	gtk_widget_show_all (GNOME_DIALOG (pm.dialog)->vbox);
	
	do {
		bval = gnumeric_dialog_run (pm.workbook, GNOME_DIALOG (pm.dialog));

		switch (bval) {
			
		case BUTTON_ADD:
			add_cb (&pm);
			break;
			
		case BUTTON_REMOVE:
			remove_cb (&pm);
			break;
			
		case -1: /* close window */
		        return;
			
		case BUTTON_CLOSE:
		default:
			break;
		}
	} while (bval != BUTTON_CLOSE);
	
	/* If the user canceled we have already returned */
	gnome_dialog_close (GNOME_DIALOG (pm.dialog));
}

/*
 * Wrapper around plugin_manager_new_impl
 * To libglade'ify it
 */
void
dialog_plugin_manager (Workbook *wb)
{
	GladeXML *gui;
	
	g_return_if_fail (wb != NULL);
	
	gui = glade_xml_new (GNUMERIC_GLADEDIR "/" GLADE_FILE , NULL);
	if (!gui) {
		g_warning ("Could not find " GLADE_FILE "\n");
		return;
	}
	
	dialog_plugin_manager_impl (wb, gui);
	gtk_object_unref (GTK_OBJECT (gui));
}
