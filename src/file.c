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
#include "file.h"

GList *gnumeric_file_savers = NULL;
GList *gnumeric_file_openers = NULL;

static gint
file_priority_sort (gconstpointer a, gconstpointer b)
{
	FileOpener *fa = (FileOpener *)a;
	FileOpener *fb = (FileOpener *)b;

	return fb->priority - fa->priority;
}

/**
 * file_format_register_open:
 * @priority: The priority at which this file open handler is registered
 * @desc: a description of this file format
 * @probe_fn: A routine that would probe if the file is of a given type.
 * @open_fn: A routine that would load the code
 *
 * The priority is used to give it a higher precendence to a format.
 * The higher the priority, the sooner it will be tried, gnumeric registers
 * its XML-based format at priority 50.
 */
void
file_format_register_open (int priority, char *desc, FileFormatProbe probe_fn, FileFormatOpen open_fn)
{
	FileOpener *fo = g_new (FileOpener, 1);

	g_return_if_fail (probe_fn != NULL);
	g_return_if_fail (open_fn != NULL);
	
	fo->priority = priority;
	fo->format_description = desc;
	fo->probe = probe_fn;
	fo->open  = open_fn;

	gnumeric_file_openers = g_list_insert_sorted (gnumeric_file_openers, fo, file_priority_sort);
}

/**
 * file_format_unregister_open:
 * @probe: The routine that was used to probe
 * @open:  The routine that was used to open
 *
 * This function is used to remove a registered format opener from gnumeric
 */
void
file_format_unregister_open (FileFormatProbe probe, FileFormatOpen open)
{
	GList *l;

	for (l = gnumeric_file_openers; l; l = l->next){
		FileOpener *fo = l->data;

		if (fo->probe == probe && fo->open == open){
			gnumeric_file_openers = g_list_remove_link (gnumeric_file_openers, l);
			return;
		}
	}
}

/**
 * file_format_register_save:
 * @extension: An extension that is usually used by this format
 * @format_description: A description of this format
 * @save_fn: A function that should be used to save
 *
 * This routine registers a file format save routine with Gnumeric
 */
void
file_format_register_save (char *extension, char *format_description, FileFormatSave save_fn)
{
	FileSaver *fs = g_new (FileSaver, 1);

	g_return_if_fail (save_fn != NULL);
	
	fs->extension = extension;
	fs->format_description = format_description;
	fs->save = save_fn;

	gnumeric_file_savers = g_list_prepend (gnumeric_file_savers, fs);
}

/**
 * file_format_unregister_save:
 * @probe: The routine that was used to save
 *
 * This function is used to remove a registered format saver from gnumeric
 */
void
file_format_unregister_save (FileFormatSave save)
{
	GList *l;

	for (l = gnumeric_file_savers; l; l = l->next){
		FileSaver *fs = l->data;

		if (fs->save == save){
			gnumeric_file_savers = g_list_remove_link (gnumeric_file_savers, l);
			return;
		}
	}
}

Workbook *
workbook_read (const char *filename)
{
	GList *l;

	g_return_val_if_fail (filename != NULL, NULL);

	for (l = gnumeric_file_openers; l; l = l->next){
		FileOpener *fo = l->data;
		Workbook *w;
		
		if ((*fo->probe) (filename)){
			w = (*fo->open) (filename);

			if (w)
				return w;
		}
	}
	return NULL;
}

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
	gtk_window_set_position (GTK_WINDOW (fsel), GTK_WIN_POS_MOUSE);
	
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
dialog_query_load_file (Workbook *wb)
{
	GtkFileSelection *fsel;
	gboolean accepted;
	char *result;
	
	fsel = (GtkFileSelection *) gtk_file_selection_new (_("Load file"));

	gtk_window_set_transient_for (GTK_WINDOW (fsel), GTK_WINDOW (wb->toplevel));
	
	/* Connect the signals for Ok and Cancel */
	gtk_signal_connect (GTK_OBJECT (fsel->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (set_ok), &accepted);
	gtk_signal_connect (GTK_OBJECT (fsel->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
	gtk_window_set_position (GTK_WINDOW (fsel), GTK_WIN_POS_MOUSE);

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
