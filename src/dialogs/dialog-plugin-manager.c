/*
 * plugin-manager.c: Dialog used to load plugins into the Gnumeric
 * spreadsheet
 *
 * Authors:
 *   Tom Dyas (tdyas@romulus.rutgers.edu)
 *   Dom Lachowicz - libglade stuff (dominicl@seas.upenn.edu)
 *                 - look for a new version of this dialog RSN
 */
#include <config.h>
#include <glib.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "plugin.h"
#include "workbook.h"
#include <glade/glade.h>

#define GLADE_FILE "plugin-manager.glade"

typedef struct 
{
	Workbook  *workbook;
	GtkWidget *dialog;
	GtkWidget *scrollwin;
	GtkWidget *clist;
} PluginManager;

enum { BUTTON_ADD = 0, BUTTON_REMOVE, BUTTON_CLOSE };

static void
add_to_clist (PluginData *pd, GtkWidget *clist)
{
	gchar *data[2];
	gint row;

	data [0] = plugin_data_get_title (pd);
	data [1] = plugin_data_get_filename (pd);

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
add_cb (PluginManager *pm)
{
	char *modfile = dialog_query_load_file (pm->workbook);
	PluginData *pd;

	if (!modfile)
		return; /* user hit 'cancel' */

	pd = plugin_load (workbook_command_context_gui (pm->workbook), modfile);
	populate_clist (pm);
}

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
	else
		gnome_dialog_set_sensitive (GNOME_DIALOG (pm->dialog),
					    BUTTON_REMOVE, FALSE);
}

static void
row_cb (GtkWidget * clist, gint row, gint col,
	GdkEvent *event,  PluginManager *pm)
{
	GtkCList *list = GTK_CLIST (clist);
	gnome_dialog_set_sensitive (GNOME_DIALOG (pm->dialog),
				    BUTTON_REMOVE, list->selection != NULL);
}

static gint
pm_key_event (GtkWidget *pm, GdkEventKey *event)
{
	if (event->keyval == GDK_Escape) {
		gtk_widget_destroy (pm);
		return 1;
	}
	return 0;
}

static void
dialog_plugin_manager_impl (Workbook *wb, GladeXML *gui)
{
	PluginManager *pm = g_new0 (PluginManager, 1);
	int bval = BUTTON_ADD;
	g_return_if_fail (pm != NULL);

	pm->workbook = wb;
	pm->dialog = glade_xml_get_widget (gui, "dialog");
	pm->scrollwin = glade_xml_get_widget (gui, "scrollwin");
	pm->clist = glade_xml_get_widget (gui, "clist");

	gtk_widget_realize (pm->clist);
	populate_clist (pm);

	if (GTK_CLIST (pm->clist)->rows > 0) {
		gtk_widget_grab_focus (pm->clist);
		gtk_clist_select_row (GTK_CLIST (pm->clist), 0, 0);
	} else
		gnome_dialog_set_sensitive (GNOME_DIALOG (pm->dialog),
					    BUTTON_REMOVE, FALSE);

	gtk_signal_connect (GTK_OBJECT (pm->clist), "select_row",
			    GTK_SIGNAL_FUNC (row_cb), pm);

	gtk_signal_connect (GTK_OBJECT (pm->clist), "unselect_row",
			    GTK_SIGNAL_FUNC (row_cb), pm);

	gtk_signal_connect (GTK_OBJECT (pm->dialog), "key_press_event",
			    GTK_SIGNAL_FUNC (pm_key_event), NULL);

	gnome_dialog_set_default (GNOME_DIALOG (pm->dialog), BUTTON_ADD);
	gtk_window_set_policy (GTK_WINDOW (pm->dialog), FALSE, TRUE, FALSE);

	gtk_widget_show_all (GNOME_DIALOG (pm->dialog)->vbox);

	do {
		bval = gnumeric_dialog_run (pm->workbook, GNOME_DIALOG (pm->dialog));
		switch (bval) {

		case BUTTON_ADD:
			add_cb (pm);
			break;

		case BUTTON_REMOVE:
			remove_cb (pm);
			break;

		case BUTTON_CLOSE:
			break;

		case -1: /* close window */
			return;

		default: /* should never happen */
			break;
		}
	} while (bval != BUTTON_CLOSE);

	/* If the user canceled we have already returned */
	gnome_dialog_close (GNOME_DIALOG (pm->dialog));
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
		printf ("Could not find " GLADE_FILE "\n");
		return;
	}

	dialog_plugin_manager_impl (wb, gui);
	gtk_object_unref (GTK_OBJECT (gui));
}
