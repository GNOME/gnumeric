/*
 * file.c: File loading and saving routines
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "dialogs.h"
#include "xml-io.h"

static void
set_ok (GtkWidget *widget, gboolean *dialog_result)
{
	*dialog_result = TRUE;
	gtk_main_quit ();
}

void
workbook_save_as (Workbook *wb)
{
	GtkFileSelection *fsel;
	gboolean accepted = FALSE;
	
	g_return_if_fail (wb != NULL);

	fsel = (GtkFileSelection *)gtk_file_selection_new (_("Save workbook as"));
	if (wb->filename)
		gtk_file_selection_set_filename (fsel, wb->filename);
	
	/* Connect the signals for Ok and Cancel */
	gtk_signal_connect (GTK_OBJECT (fsel->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (set_ok), &accepted);
	gtk_signal_connect (GTK_OBJECT (fsel->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
	gtk_window_position (GTK_WINDOW (fsel), GTK_WIN_POS_MOUSE);
	
	/* Run the dialog */
	gtk_widget_show (GTK_WIDGET (fsel));
	gtk_grab_add (GTK_WIDGET (fsel));
	gtk_main ();

	if (accepted){
		char *name = gtk_file_selection_get_filename (fsel);

		if (name [strlen (name)-1] != '/'){
			workbook_set_filename (wb, name);
				
			gnumericWriteXmlWorkbook (wb, wb->filename);
		}
	}
	gtk_widget_destroy (GTK_WIDGET (fsel));
}

void
workbook_save (Workbook *wb)
{
	g_return_if_fail (wb != NULL);
	
	if (!wb->filename){
		workbook_save_as (wb);
		return;
	}

	gnumericWriteXmlWorkbook (wb, wb->filename);
}

char *
dialog_query_load_file (void)
{
	GtkFileSelection *fsel;
	gboolean accepted;
	char *result;
	
	fsel = (GtkFileSelection *) gtk_file_selection_new (_("Load file"));

	/* Connect the signals for Ok and Cancel */
	gtk_signal_connect (GTK_OBJECT (fsel->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (set_ok), &accepted);
	gtk_signal_connect (GTK_OBJECT (fsel->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
	gtk_window_position (GTK_WINDOW (fsel), GTK_WIN_POS_MOUSE);

	/* Run the dialog */
	gtk_widget_show (GTK_WIDGET (fsel));
	gtk_grab_add (GTK_WIDGET (fsel));
	gtk_main ();

	if (accepted){
		char *name = gtk_file_selection_get_filename (fsel);

		if (name [strlen (name)-1] == '/')
			result = NULL;
		else
			result = g_strdup (name);
	} else
		result = NULL;

	gtk_widget_destroy (GTK_WIDGET (fsel));

	return result;
}
