/*
 * file.c: File loading and saving routines
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "xml-io.h"
#include "file.h"
#include "workbook.h"
#include "command-context.h"
#include <locale.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

struct _FileOpener {
	int             priority;
	char           *format_description;
	FileFormatProbe probe;
	FileFormatOpen  open;
};
struct _FileSaver {
	char            *extension;
	char            *format_description;
	FileFormatLevel level;
	FileFormatSave  save;
};

/* A GList of FileOpener structures */
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
 *
 * If the probe_fn is NULL, we consider it a format importer, so that
 * it gets only listed in the "Import..." menu, and it is not auto-probed
 * at file open time.
 */
void
file_format_register_open (int priority, const char *desc,
			   FileFormatProbe probe_fn, FileFormatOpen open_fn)
{
	FileOpener *fo = g_new (FileOpener, 1);

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
 * @level: The file format level 
 * @save_fn: A function that should be used to save
 *
 * This routine registers a file format save routine with Gnumeric
 */
void
file_format_register_save (char *extension, const char *format_description,
			   FileFormatLevel level, FileFormatSave save_fn)
{
	FileSaver *fs = g_new (FileSaver, 1);

	g_return_if_fail (save_fn != NULL);

	fs->extension = extension;
	fs->format_description =
		format_description ? g_strdup (format_description) : NULL;
	fs->level = level;
	fs->save  = save_fn;

	gnumeric_file_savers = g_list_append (gnumeric_file_savers, fs);
}

/**
 * cb_unregister_save:
 * @wb:   Workbook
 * @save: The format saver which will be removed
 *
 * Set file format level to manual for workbooks which had this saver set.
 */
static void
cb_unregister_save (Workbook *wb, FileFormatSave save)
{
	if (wb->file_save_fn == save) {
		wb->file_format_level = FILE_FL_MANUAL;
		wb->file_save_fn = NULL;
	}
}

/**
 * file_format_unregister_save:
 * @save: The routine that was used to save
 *
 * This function is used to remove a registered format saver from gnumeric
 */
void
file_format_unregister_save (FileFormatSave save)
{
	GList *l;

	workbook_foreach ((WorkbookCallback) cb_unregister_save, save);
	
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

/**
 * workbook_load_from:
 * @workbook: A blank workbook to load into.
 * @filename: gives the URI of the file.
 * 
 * This function attempts to read the file into the supplied
 * workbook.
 * 
 * Return value: success : 0
 *               failure : -1
 **/
int
workbook_load_from (CommandContext *context, Workbook *wb,
		    const char *filename)
{
	GList *l;

	for (l = gnumeric_file_openers; l; l = l->next) {
		FileOpener const * const fo = l->data;

		if (fo->probe != NULL && (*fo->probe) (filename)) {
			int result = (*fo->open) (context, wb, filename);
			if (result == 0)
				workbook_set_dirty (wb, FALSE);
			return result;
		}
	}
	gnumeric_error_read (context, NULL);
	return -1;
}

/**
 * workbook_try_read:
 * @filename: gives the URI of the file.
 * 
 * This function attempts to read the file
 * 
 * Return value: a loaded workbook on success or
 *               NULL on failure.
 **/
Workbook *
workbook_try_read (CommandContext *context, const char *filename)
{
	Workbook *wb;
	int result;

	g_return_val_if_fail (filename != NULL, NULL);

	wb = workbook_new ();
	result = workbook_load_from (context, wb, filename);
	if (result != 0) {
		gtk_object_destroy   (GTK_OBJECT (wb));
		wb = NULL;
	}

	return wb;
}

static Workbook *
file_finish_load (Workbook *wb)
{
	if (wb != NULL) {
		workbook_recalc (wb);

		/*
		 * render and calc size of unrendered cells,
		 * then calc spans for everything
		 */
		workbook_calc_spans (wb, SPANCALC_RENDER|SPANCALC_RESIZE);

		/* FIXME : This should not be needed */
		workbook_set_dirty (wb, FALSE);

		sheet_update (wb->current_sheet);
	}

	return wb;
}

/**
 * workbook_read:
 * @filename: the file's URI
 * 
 * This attempts to read a workbook, if the file doesn't
 * exist it will create a blank 1 sheet workbook, otherwise
 * it will flag a user error message.
 * 
 * Return value: a pointer to a Workbook or NULL.
 **/
Workbook *
workbook_read (CommandContext *context, const char *filename)
{
	Workbook *wb = NULL;
	char *template;

	g_return_val_if_fail (filename != NULL, NULL);

	if (g_file_exists (filename)) {
		template = g_strdup_printf (_("Could not read file %s\n%%s"),
					    filename);
		command_context_push_template (context, template);
		wb = workbook_try_read (context, filename);
		command_context_pop_template (context);
		g_free (template);
	} else {
		wb = workbook_new_with_sheets (1);
		workbook_set_saveinfo (wb, filename, FILE_FL_NEW,
				       gnumeric_xml_write_workbook);
	}

	return file_finish_load (wb);
}

static void
cb_select (GtkWidget *clist, gint row, gint column,
	   GdkEventButton *event, GtkWidget *dialog)
{
	/* If the filter is double-clicked we proceed with importing and
           dismiss chooser. */
	if (event && event->type == GDK_2BUTTON_PRESS)
		gtk_signal_emit_by_name (GTK_OBJECT (dialog), "clicked", 0);
}

/*
 * Lets the user choose an import filter for @filename, and
 * uses that to load the file
 */
Workbook *
workbook_import (CommandContext *context, Workbook *parent,
		 const char *filename)
{
	Workbook *wb = NULL;
	GladeXML *gui;
	GtkWidget *dialog;
	GtkCList *clist;
	FileOpener *fo = NULL;
	int ret, row;
	GList *l;
	
	gui = gnumeric_glade_xml_new (context, "import.glade");
	if (gui == NULL)
		return NULL;

	dialog = glade_xml_get_widget (gui, "import-dialog");

	gnome_dialog_set_default (GNOME_DIALOG (dialog), 0);

	clist = GTK_CLIST (glade_xml_get_widget (gui, "import-clist"));
	gtk_clist_set_selection_mode (clist, GTK_SELECTION_SINGLE);
	
	for (row = 0, l = gnumeric_file_openers; l; l = l->next){
		FileOpener *fo = l->data;
		char *text [1];
		
		if (fo->probe != NULL)
			continue;

		text [0] = fo->format_description;
		gtk_clist_append (clist, text);
		gtk_clist_set_row_data (clist, row, l->data);
		if (row == 0)
			gtk_clist_select_row (clist, 0, 0);

		row++;
	}
	gtk_signal_connect (GTK_OBJECT(clist), "select_row",
			    GTK_SIGNAL_FUNC(cb_select), (gpointer) dialog);
		
	gtk_widget_grab_focus (GTK_WIDGET (clist));

	ret = gnumeric_dialog_run (parent, GNOME_DIALOG (dialog));

	if (ret == 0 && clist->selection) {
		int sel_row;
		
		sel_row = GPOINTER_TO_INT (clist->selection->data);
		fo = gtk_clist_get_row_data (clist, sel_row);
	}
	
	if (ret != -1)
		gnome_dialog_close (GNOME_DIALOG (dialog));
		
	if (fo) {
		char *template;

		wb = workbook_new ();
		template = g_strdup_printf (_("Could not import file %s\n%%s"),
					    filename);
		command_context_push_template (context, template);
		ret = fo->open (context, wb, filename);
		command_context_pop_template (context);
		g_free (template);
		if (ret != 0) {
			gtk_object_destroy   (GTK_OBJECT (wb));
			wb = NULL;
		} else
			workbook_set_dirty (wb, FALSE);
	}

	gtk_object_unref (GTK_OBJECT (gui));

	return file_finish_load (wb);
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
				    GTK_SIGNAL_FUNC (saver_activate), fs);
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

static gint
fs_key_event (GtkFileSelection *fsel, GdkEventKey *event)
{
	if (event->keyval == GDK_Escape) {
		gtk_button_clicked (GTK_BUTTON (fsel->cancel_button));
		return 1;
	} else
		return 0;
}

/*
 * fs_set_filename
 * @fsel  file selection dialog
 * @wb    workbook
 * Set default filename in the file selection dialog.
 * Set it to the workbook file name sans extension.
 */
static void
fs_set_filename (GtkFileSelection *fsel, Workbook *wb)
{
	char *name = g_strdup (wb->filename);
	char *p = strrchr (name, '.');

	if (p)
		*p = '\0';
	gtk_file_selection_set_filename (fsel, name);
	g_free (name);
}

/*
 * Returns true if user confirmed that existing workbook should be
 * overwritten.
 *
 * FIXME: The dialog should really be a child of the file selector,
 * not the workbook.
 */
static gboolean
wants_to_overwrite (Workbook *wb, const char *name)
{
	GtkWidget *d, *button_no;
	char *message = g_strdup_printf
		(_("Workbook %s already exists.\nDo you want to save over it?"),
		 name);
	
	d = gnome_message_box_new (
		message, GNOME_MESSAGE_BOX_QUESTION,
		GNOME_STOCK_BUTTON_YES,
		GNOME_STOCK_BUTTON_NO,
		NULL);
	g_free (message);
	button_no = g_list_last (GNOME_DIALOG (d)->buttons)->data;
	gtk_widget_grab_focus (button_no);
	
	return (gnumeric_dialog_run (wb, GNOME_DIALOG (d)) == 0);
}

/*
 * Check if it makes sense to try saving.
 * If it's an existing file and writable for us, ask if we want to overwrite.
 * We check for other problems, but if we miss any, the saver will report.
 * So it doesn't have to be bulletproof.
 *
 * FIXME: The message boxes should really be children of the file selector,
 * not the workbook.
 */
static gboolean
can_try_save_to (Workbook *wb, const char *name)
{
	struct stat sb;
	char *err_str;
	gboolean dir_entered = FALSE;
	gboolean file_exists = FALSE;

	if (*name == 0)
		return FALSE;
	else if (name [strlen (name) - 1] == '/') {
		dir_entered = TRUE;
	} else if ((stat (name, &sb) == 0)) {
		if (S_ISDIR (sb.st_mode))
			dir_entered = TRUE;
		else
			file_exists = TRUE;
	}
	if (dir_entered) {
		err_str = g_strdup_printf (_("%s\nis a directory name"), name);
		gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR, err_str);
		g_free (err_str);
		return FALSE;
	}
	if (file_exists) {
		if (access (name, W_OK) == 0)
			return wants_to_overwrite (wb, name);
		else {
			err_str = g_strdup_printf (_("You do not have permission to save to\n%s"),
						   name);
			gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR, err_str);
			g_free (err_str);
			return FALSE;
		}
	} else
		return TRUE;
}

/*
 * Handle the selection made in the save as dialog.
 *
 * FIXME: The message boxes should really be children of the file selector,
 * not the workbook.
 */
static gboolean
do_save_as (CommandContext *context, Workbook *wb, const char *name)
{
	char *template, *filename;
	const char *base;
	gboolean success = FALSE;

#ifndef ENABLE_BONOBO
	if (*name == 0 || name [strlen (name) - 1] == '/') {
		gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("Please enter a file name,\nnot a directory"));
		return FALSE;
	}
#endif

	current_saver = insure_saver (current_saver);
	if (!current_saver) {
		gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("Sorry, there are no file savers loaded,\nI cannot save"));
		return FALSE;
	}

	base = g_basename (name);

	if (strchr (base, '.') == NULL)
		filename = g_strconcat (name, current_saver->extension, NULL);
	else
		filename = g_strdup (name);
		
	if (!can_try_save_to (wb, filename)) {
		g_free (filename);
		return FALSE;
	}

	template = g_strdup_printf (_("Could not save to file %s\n%%s"), name);
	command_context_push_template (context, template);
	/* Files are expected to be in standard C format.  */
	if (current_saver->save (context, wb, filename) == 0) {
		workbook_set_saveinfo (wb, name, current_saver->level,
				       current_saver->save);
		workbook_set_dirty (wb, FALSE);
		success = TRUE;
	}

	command_context_pop_template (context);
	g_free (template);
	g_free (filename);

	return success;
}

gboolean
workbook_save_as (CommandContext *context, Workbook *wb)
{
	GtkFileSelection *fsel;
	gboolean accepted = FALSE;
	GtkWidget *format_selector;
	gboolean success  = FALSE;

	g_return_val_if_fail (wb != NULL, FALSE);

	fsel = GTK_FILE_SELECTION
		(gtk_file_selection_new (_("Save workbook as")));

	gtk_window_set_modal (GTK_WINDOW (fsel), TRUE);
	gtk_window_set_transient_for (GTK_WINDOW (fsel), 
				      GTK_WINDOW (workbook_get_toplevel (wb)));
	if (wb->filename)
		fs_set_filename (fsel, wb);

	/* Choose the format */
	format_selector = make_format_chooser ();
	gtk_box_pack_start (GTK_BOX (fsel->action_area), format_selector,
			    FALSE, TRUE, 0);

	/* Connect the signals for Ok and Cancel */
	gtk_signal_connect (GTK_OBJECT (fsel->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (set_ok), &accepted);
	gtk_signal_connect (GTK_OBJECT (fsel->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
	gtk_signal_connect (GTK_OBJECT (fsel), "key_press_event",
			    GTK_SIGNAL_FUNC (fs_key_event), NULL);
	
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

	if (accepted)
		success = do_save_as (context, wb,
				      gtk_file_selection_get_filename (fsel));
	gtk_widget_destroy (GTK_WIDGET (fsel));

	return success;
}

gboolean
workbook_save (CommandContext *context, Workbook *wb)
{
	char *template;
	gboolean ret;
	FileFormatSave save_fn;
	
	g_return_val_if_fail (wb != NULL, FALSE);

	if (wb->file_format_level < FILE_FL_AUTO)
		return workbook_save_as (context, wb);

	template = g_strdup_printf (_("Could not save to file %s\n%%s"),
				    wb->filename);
	command_context_push_template (context, template);
	save_fn = wb->file_save_fn;
	if (!save_fn)
		save_fn = gnumeric_xml_write_workbook;
	ret = ((save_fn) (context, wb, wb->filename) == 0);
	if (ret == TRUE) 
		workbook_set_dirty (wb, FALSE);

	command_context_pop_template (context);
	g_free (template);

	return ret;
}

char *
dialog_query_load_file (Workbook *wb)
{
	GtkFileSelection *fsel;
	gboolean accepted = FALSE;
	char *result;

	fsel = GTK_FILE_SELECTION (gtk_file_selection_new (_("Load file")));
	gtk_window_set_modal (GTK_WINDOW (fsel), TRUE);

	gtk_window_set_transient_for (GTK_WINDOW (fsel),
				      GTK_WINDOW (workbook_get_toplevel (wb)));

	/* Connect the signals for Ok and Cancel */
	gtk_signal_connect (GTK_OBJECT (fsel->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (set_ok), &accepted);
	gtk_signal_connect (GTK_OBJECT (fsel->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
	gtk_signal_connect (GTK_OBJECT (fsel), "key_press_event",
			    GTK_SIGNAL_FUNC (fs_key_event), NULL);
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

	if (accepted) {
		char *name = gtk_file_selection_get_filename (fsel);

#ifndef ENABLE_BONOBO
		if (*name && name [strlen (name) - 1] == '/')
			result = NULL;
		else
#endif
			result = g_strdup (name);
	} else
		result = NULL;

	gtk_widget_destroy (GTK_WIDGET (fsel));

	return result;
}
