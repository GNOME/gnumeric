/*
 * file.c: File loading and saving routines
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "xml-io.h"
#include "file.h"
#include <locale.h>

static GList *gnumeric_file_savers = NULL;
static GList *gnumeric_file_openers = NULL;
static FileSaver *current_saver = NULL;

static gint
file_priority_sort (gconstpointer a, gconstpointer b)
{
	const FileOpener *fa = (const FileOpener *)a;
	const FileOpener *fb = (const FileOpener *)b;

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
file_format_register_open (int priority, const char *desc,
			   FileFormatProbe probe_fn, FileFormatOpen open_fn)
{
	FileOpener *fo = g_new (FileOpener, 1);

	g_return_if_fail (probe_fn != NULL);
	g_return_if_fail (open_fn != NULL);

	fo->priority = priority;
	fo->format_description = desc ? g_strdup (desc) : NULL;
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
			g_list_free_1 (l);
			if (fo->format_description)
				g_free (fo->format_description);
			g_free (fo);
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
file_format_register_save (char *extension, const char *format_description,
			   FileFormatSave save_fn)
{
	FileSaver *fs = g_new (FileSaver, 1);

	g_return_if_fail (save_fn != NULL);

	fs->extension = extension;
	fs->format_description =
		format_description ? g_strdup (format_description) : NULL;
	fs->save = save_fn;

	gnumeric_file_savers = g_list_append (gnumeric_file_savers, fs);
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
			if (fs == current_saver)
				current_saver = NULL;

			gnumeric_file_savers = g_list_remove_link (gnumeric_file_savers, l);
			g_list_free_1 (l);
			if (fs->format_description)
				g_free (fs->format_description);
			g_free (fs);
			return;
		}
	}
}

Workbook *
workbook_read (const char *filename)
{
	GList *l;
	char *oldlocale;
	Workbook *w = NULL;

	g_return_val_if_fail (filename != NULL, NULL);

	/* Files are expected to be in standard C format.  */
	oldlocale = g_strdup (setlocale (LC_NUMERIC, "C"));

	for (l = gnumeric_file_openers; l; l = l->next){
		const FileOpener *fo = l->data;

		if ((*fo->probe) (filename)){
			w = (*fo->open) (filename);

			if (w) {
				workbook_mark_clean (w);
				break;
			}
		}
	}

	setlocale (LC_NUMERIC, oldlocale);
	g_free (oldlocale);
	return w;
}

static void
set_ok (GtkWidget *widget, gboolean *dialog_result)
{
	*dialog_result = TRUE;
	gtk_main_quit ();
}

/**
 * saver_activate:
 *
 * Callback routine to choose the current file saver
 */
static void
saver_activate (GtkMenuItem *item, FileSaver *saver)
{
	GList *l;

	for (l = gnumeric_file_savers; l; l = l->next){
		FileSaver *fs = l->data;

		if (fs == saver)
			current_saver = saver;
	}
}

/**
 * file_saver_is_default_format:
 *
 * Returns TRUE if @saver is the default file save format
 */
static gboolean
file_saver_is_default_format (FileSaver *saver)
{
	if (current_saver == saver)
		return TRUE;
	
	if (current_saver == NULL)
		if (strcmp (saver->extension, ".gnumeric") == 0) {
			current_saver = saver;
			return TRUE;
		}

	return FALSE;
}

static void
fill_save_menu (GtkOptionMenu *omenu, GtkMenu *menu)
{
	GList *l;
	int i, selected=-1;

	for (i = 0, l = gnumeric_file_savers; l; l = l->next, i++){
		GtkWidget *menu_item;
		FileSaver *fs = l->data;

		menu_item = gtk_menu_item_new_with_label (fs->format_description);
		gtk_widget_show (menu_item);

		gtk_menu_append (menu, menu_item);

		if (file_saver_is_default_format (fs))
			selected = i;

		gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
				    GTK_SIGNAL_FUNC(saver_activate), fs);
	}

	gtk_option_menu_set_menu (GTK_OPTION_MENU (omenu), GTK_WIDGET (menu));
	if (selected > 0)
		gtk_option_menu_set_history (omenu, selected);
}

static GtkWidget *
make_format_chooser (void)
{
	GtkWidget *box, *label;
	GtkWidget *omenu, *menu;

	box = gtk_hbox_new (0, GNOME_PAD);
	label = gtk_label_new (_("File format:"));
	omenu = gtk_option_menu_new ();
	menu = gtk_menu_new ();

	fill_save_menu (GTK_OPTION_MENU (omenu), GTK_MENU (menu));

	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, GNOME_PAD);
	gtk_box_pack_start (GTK_BOX (box), omenu, FALSE, TRUE, GNOME_PAD);
	gtk_widget_show_all (box);

	return box;
}

static FileSaver *
insure_saver (FileSaver *current)
{
	GList *l;

	if (current)
		return current;

	for (l = gnumeric_file_savers; l; l = l->next){
		return l->data;
	}
	g_assert_not_reached ();
	return NULL;
}

static guint
file_dialog_delete_event (GtkWidget *widget, GdkEventAny *event)
{
	gtk_main_quit ();
	return TRUE;
}

void
workbook_save_as (Workbook *wb)
{
	GtkFileSelection *fsel;
	gboolean accepted = FALSE;
	GtkWidget *format_selector;

	g_return_if_fail (wb != NULL);

	fsel = GTK_FILE_SELECTION (gtk_file_selection_new (_("Save workbook as")));

	gtk_window_set_modal (GTK_WINDOW (fsel), TRUE);
	if (wb->filename)
		gtk_file_selection_set_filename (fsel, wb->filename);

	/* Choose the format */
	format_selector = make_format_chooser ();
	gtk_box_pack_start (GTK_BOX (fsel->action_area), format_selector,
			    FALSE, TRUE, 0);

	/* Connect the signals for Ok and Cancel */
	gtk_signal_connect (GTK_OBJECT (fsel->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (set_ok), &accepted);
	gtk_signal_connect (GTK_OBJECT (fsel->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
	gtk_window_set_position (GTK_WINDOW (fsel), GTK_WIN_POS_MOUSE);

	/*
	 * Make sure that we quit the main loop if the window is destroyed 
	 */
	gtk_signal_connect (GTK_OBJECT (fsel), "delete_event",
			    GTK_SIGNAL_FUNC (file_dialog_delete_event), NULL);
									   
	/* Run the dialog */

	gtk_widget_show (GTK_WIDGET (fsel));
	gtk_grab_add (GTK_WIDGET (fsel));
	gtk_main ();

	if (accepted){
		char *name = gtk_file_selection_get_filename (fsel);

		if (name [strlen (name)-1] != '/'){
			char *base = g_basename (name);

			current_saver = insure_saver (current_saver);
			if (!current_saver)
				gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
						 _("Sorry, there are no file savers loaded, I cannot save"));
			else {
				char *oldlocale;

				if (strchr (base, '.') == NULL){
					name = g_strconcat (name, current_saver->extension, NULL);
				} else
					name = g_strdup (name);

				workbook_set_filename (wb, name);

				/* Files are expected to be in standard C format.  */
				oldlocale = g_strdup (setlocale (LC_NUMERIC, "C"));

				current_saver->save (wb, wb->filename);

				setlocale (LC_NUMERIC, oldlocale);
				g_free (oldlocale);

				g_free (name);
			}
		}
	}
	gtk_widget_destroy (GTK_WIDGET (fsel));
}

void
workbook_save (Workbook *wb)
{
	char *oldlocale;

	g_return_if_fail (wb != NULL);

	if (!wb->filename){
		workbook_save_as (wb);
		return;
	}

	/* Files are expected to be in standard C format.  */
	oldlocale = g_strdup (setlocale (LC_NUMERIC, "C"));
	gnumericWriteXmlWorkbook (wb, wb->filename);

	setlocale (LC_NUMERIC, oldlocale);
	g_free (oldlocale);
}

char *
dialog_query_load_file (Workbook *wb)
{
	GtkFileSelection *fsel;
	gboolean accepted = FALSE;
	char *result;

	fsel = GTK_FILE_SELECTION (gtk_file_selection_new (_("Load file")));
	gtk_window_set_modal (GTK_WINDOW (fsel), TRUE);

	gtk_window_set_transient_for (GTK_WINDOW (fsel), GTK_WINDOW (wb->toplevel));

	/* Connect the signals for Ok and Cancel */
	gtk_signal_connect (GTK_OBJECT (fsel->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (set_ok), &accepted);
	gtk_signal_connect (GTK_OBJECT (fsel->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
	gtk_window_set_position (GTK_WINDOW (fsel), GTK_WIN_POS_MOUSE);

	/*
	 * Make sure that we quit the main loop if the window is destroyed 
	 */
	gtk_signal_connect (GTK_OBJECT (fsel), "delete_event",
			    GTK_SIGNAL_FUNC (file_dialog_delete_event), NULL);

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
