/*
 * dialog-simple-input.c: Implements various dialogs for simple
 * input values
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <math.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"

gboolean
dialog_get_number (Workbook *wb, const char *glade_file, double *init_and_return)
{
	GladeXML *gui;
	GnomeDialog *dialog;
	GtkWidget *entry;
	char *f;

	f = g_concat_dir_and_file (GNUMERIC_GLADEDIR, glade_file);
	gui = glade_xml_new (f, NULL);
	g_free (f);
	if (!gui)
		return FALSE;

	dialog = GNOME_DIALOG (glade_xml_get_widget (gui, "dialog1"));
	if (dialog == NULL){
		g_warning ("Can not find the `dialog1' widget in %s", glade_file);
		return FALSE;
	}

	entry = glade_xml_get_widget (gui, "entry1");
	if (*init_and_return != 0.0){
		char buffer [80];
		
		sprintf (buffer, "%g", *init_and_return);
		
		gtk_entry_set_text (GTK_ENTRY (entry), buffer);
	}
	
	gnome_dialog_set_parent (dialog, GTK_WINDOW (wb->toplevel));
	switch (gnome_dialog_run_and_close (dialog)){
	case 1:			/* cancel */
	case -1:		/* window manager close */
		return FALSE;

	default:
		*init_and_return = atof (gtk_entry_get_text (GTK_ENTRY (entry)));
	}
	
	gtk_object_destroy (GTK_OBJECT (gui));

	return TRUE;
}

char *
dialog_get_sheet_name (Workbook *wb, const char *current)
{
	GladeXML *gui;
	GnomeDialog *dialog;
	GtkWidget *entry;
	char *str;
	
	gui = glade_xml_new (GNUMERIC_GLADEDIR "/sheet-rename.glade", NULL);
	if (!gui)
		return NULL;

	dialog = GNOME_DIALOG (glade_xml_get_widget (gui, "dialog"));
	if (dialog == NULL){
		g_warning ("Can not find the `dialog' widget in sheet-rename.glade");
		return NULL;
	}

	entry = glade_xml_get_widget (gui, "entry");
	gtk_entry_set_text (GTK_ENTRY (entry), current);
	
	gnome_dialog_set_parent (dialog, GTK_WINDOW (wb->toplevel));
	switch (gnome_dialog_run_and_close (dialog)){
	case 1:			/* cancel */
	case -1:		/* window manager close */
		return NULL;

	default:
		str = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
	}
	
	gtk_object_destroy (GTK_OBJECT (gui));

	return str;
}


