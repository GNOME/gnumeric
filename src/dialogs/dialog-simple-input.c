/*
 * dialog-simple-input.c: Implements various dialogs for simple
 * input values
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Almer S. Tigelaar (almer@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"
#include <sheet.h>
#include <gui-util.h>
#include <workbook.h>

#include <math.h>
#include <glade/glade.h>
#include <libgnome/gnome-i18n.h>

gboolean
dialog_choose_cols_vs_rows (WorkbookControlGUI *wbcg, const char *title,
			    gboolean *is_cols)
{
	GladeXML *gui;
	GtkDialog *dialog;
	GtkToggleButton *rows;
	gboolean res = FALSE;

	gui = gnumeric_glade_xml_new (wbcg, "colrow.glade");
        if (gui == NULL)
                return FALSE;

	dialog = GTK_DIALOG (glade_xml_get_widget (gui, "dialog1"));
	if (dialog == NULL){
		g_warning ("Cannot find the `dialog1' widget in colrow.glade");
		gtk_object_destroy (GTK_OBJECT (gui));
		return FALSE;
	}

	rows = GTK_TOGGLE_BUTTON (glade_xml_get_widget (gui, "rows"));
	gtk_window_set_title (GTK_WINDOW (dialog), title);

	switch (gnumeric_dialog_run (wbcg, dialog)){
	case 1:		/* cancel */
		res = FALSE;
		break;
	case -1:		/* window manager close */
		gtk_object_destroy (GTK_OBJECT (gui));
		return FALSE;
	default:
		res = TRUE;
		*is_cols = !gtk_toggle_button_get_active (rows);
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
	gtk_object_destroy (GTK_OBJECT (gui));

	return res;
}

gboolean
dialog_get_number (WorkbookControlGUI *wbcg,
		   const char *glade_file, double *init_and_return)
{
	GladeXML *gui;
	GtkDialog *dialog;
	GtkWidget *entry;
	gboolean res = FALSE;

	gui = gnumeric_glade_xml_new (wbcg, glade_file);
        if (gui == NULL)
                return FALSE;

	dialog = GTK_DIALOG (glade_xml_get_widget (gui, "dialog1"));
	if (dialog == NULL){
		g_warning ("Cannot find the `dialog1' widget in %s", glade_file);
		gtk_object_destroy (GTK_OBJECT (gui));
		return FALSE;
	}

	entry = glade_xml_get_widget (gui, "entry1");
	if (*init_and_return != 0.0){
		char buffer[80];

		sprintf (buffer, "%g", *init_and_return);

		gtk_entry_set_text (GTK_ENTRY (entry), buffer);
	}
	gnumeric_editable_enters (GTK_WINDOW (dialog), GTK_WIDGET (entry));

	switch (gnumeric_dialog_run (wbcg, dialog)){
	case 1:			/* cancel */
		res = FALSE;
		break;
	case -1:		/* window manager close */
		gtk_object_destroy (GTK_OBJECT (gui));
		return FALSE;

	default:
		res = TRUE;
		*init_and_return = atof (gtk_entry_get_text (GTK_ENTRY (entry)));
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
	gtk_object_destroy (GTK_OBJECT (gui));

	return res;
}




