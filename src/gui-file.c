#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "xml-io.h"
#include "gui-file.h"
#include "sheet.h"
#include "application.h"
#include "io-context.h"
#include "command-context.h"
#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include "workbook.h"
#include <sys/stat.h>

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
WorkbookView *
gui_file_import (WorkbookControlGUI *wbcg, const char *filename)
{
	WorkbookView *new_view = NULL;
	Workbook *new_wb;
	char *template;

	GladeXML *gui;
	GtkWidget *dialog;
	GtkCList *clist;
	FileOpener *fo = NULL;
	int ret, row;
	GList *l;

	gui = gnumeric_glade_xml_new (wbcg, "import.glade");
	if (gui == NULL)
		return NULL;

	dialog = glade_xml_get_widget (gui, "import-dialog");

	gnome_dialog_set_default (GNOME_DIALOG (dialog), 0);

	clist = GTK_CLIST (glade_xml_get_widget (gui, "import-clist"));

	for (row = 0, l = file_format_get_openers (); l; l = l->next){
		FileOpener *fo = l->data;
		char *text[1];

		if (file_opener_has_probe (fo))
			continue;

		text[0] = (gchar *) file_opener_get_format_description (fo);
		gtk_clist_append (clist, text);
		gtk_clist_set_row_data (clist, row, l->data);
		if (row == 0)
			gtk_clist_select_row (clist, 0, 0);

		row++;
	}
	gtk_signal_connect (GTK_OBJECT(clist), "select_row",
			    GTK_SIGNAL_FUNC(cb_select), (gpointer) dialog);

	gtk_widget_grab_focus (GTK_WIDGET (clist));

	ret = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));

	if (ret == 0 && clist->selection) {
		int sel_row;

		sel_row = GPOINTER_TO_INT (clist->selection->data);
		fo = gtk_clist_get_row_data (clist, sel_row);
	}

	if (ret != -1)
		gnome_dialog_close (GNOME_DIALOG (dialog));
	gtk_object_unref (GTK_OBJECT (gui));

	if (fo == NULL)
		return NULL;

	new_view = workbook_view_new (NULL);
	new_wb = wb_view_workbook (new_view);

	template = g_strdup_printf (_("Could not import file %s\n%%s"),
				    filename);
	command_context_push_err_template (COMMAND_CONTEXT (wbcg), template);
	{
		IOContext *io_context;

		io_context = gnumeric_io_context_new (WORKBOOK_CONTROL (wbcg));
		ret = file_opener_open (fo, io_context, new_view, filename) ? 0 : -1;
		if (ret == 0 && workbook_sheet_count (new_wb) <= 0) {
			ret = -1;
			gnumeric_error_read (COMMAND_CONTEXT (wbcg), _("No sheets found"));
		}
		gnumeric_io_context_free (io_context);
	}
	command_context_pop_err_template (COMMAND_CONTEXT (wbcg));
	g_free (template);

	if (ret != 0) {
		gtk_object_destroy (GTK_OBJECT (new_wb));
		new_view = NULL;
	} else
		workbook_set_dirty (new_wb, FALSE);

	return file_finish_load (WORKBOOK_CONTROL (wbcg), new_view);
}

/*
 * Check that a given file saver is present in the file saver list.
 */
static gboolean
is_saver_registered (FileSaver *saver)
{
	GList *l;

	for (l = file_format_get_savers (); l; l = l->next) 
		if (l->data == saver)
			return TRUE;
	return FALSE;
}

static void
handle_ok (GtkWidget *widget, gboolean *dialog_result)
{
	struct stat sb;
	GtkFileSelection *fsel;
	char *name;

	fsel = GTK_FILE_SELECTION (
		gtk_widget_get_ancestor (widget, GTK_TYPE_FILE_SELECTION));
	name = gtk_file_selection_get_filename (fsel);

	/* Change into directory if that's what user selected */
	if ((stat (name, &sb) == 0) && S_ISDIR (sb.st_mode)) {
		char *last_slash = strrchr (name, '/');
		gchar *dirname;
		/* The file selector needs a '/' at the end of a
		   directory name */
		if (!last_slash || *(last_slash + 1) != '\0')
			dirname = g_strconcat (name, "/", NULL);
		else
			dirname = g_strdup (name);
		gtk_file_selection_set_filename (fsel, dirname);
		g_free (dirname);
	} else {
		*dialog_result = TRUE;
		gtk_main_quit ();
	}
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
	WorkbookControlGUI *wbcg;

	wbcg = gtk_object_get_data (GTK_OBJECT (item), "wbcg");
	for (l = file_format_get_savers (); l; l = l->next){
		FileSaver *fs = l->data;

		if (fs == saver)
			wbcg->current_saver = saver;
	}
}

/**
 * file_saver_is_default_format:
 *
 * Returns TRUE if @saver is the default file save format.
 */
static gboolean
file_saver_is_default_format (WorkbookControlGUI *wbcg, FileSaver *saver)
{
	if (wbcg->current_saver == saver)
		return TRUE;

	if (wbcg->current_saver == NULL) {
		const gchar *extension = file_saver_get_extension (saver);

		if (extension != NULL && strcmp (extension, "gnumeric") == 0) {
			wbcg->current_saver = saver;
			return TRUE;
		}
	}

	return FALSE;
}

static void
fill_save_menu (WorkbookControlGUI *wbcg,  GtkOptionMenu *omenu, GtkMenu *menu)
{
	GList *l;
	int i, selected=-1;

	for (i = 0, l = file_format_get_savers (); l; l = l->next, i++){
		GtkWidget *menu_item;
		FileSaver *fs = l->data;

		menu_item = gtk_menu_item_new_with_label (file_saver_get_format_description (fs));
		gtk_object_set_data (GTK_OBJECT (menu_item), "wbcg", wbcg);
		gtk_widget_show (menu_item);
		gtk_menu_append (menu, menu_item);

		if (file_saver_is_default_format (wbcg, fs))
			selected = i;

		gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
				    GTK_SIGNAL_FUNC (saver_activate), fs);
	}

	gtk_option_menu_set_menu (GTK_OPTION_MENU (omenu), GTK_WIDGET (menu));
	if (selected > 0)
		gtk_option_menu_set_history (omenu, selected);
}

static GtkWidget *
make_format_chooser (WorkbookControlGUI *wbcg)
{
	GtkWidget *box, *label;
	GtkWidget *omenu, *menu;

	box = gtk_hbox_new (0, GNOME_PAD);
	label = gtk_label_new (_("File format:"));
	omenu = gtk_option_menu_new ();
	menu = gtk_menu_new ();

	fill_save_menu (wbcg, GTK_OPTION_MENU (omenu), GTK_MENU (menu));

	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, GNOME_PAD);
	gtk_box_pack_start (GTK_BOX (box), omenu, FALSE, TRUE, GNOME_PAD);
	gtk_widget_show_all (box);

	return box;
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
wants_to_overwrite (WorkbookControlGUI *wbcg, const char *name)
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

	return (gnumeric_dialog_run (wbcg, GNOME_DIALOG (d)) == 0);
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
can_try_save_to (WorkbookControlGUI *wbcg, const char *name)
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
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, err_str);
		g_free (err_str);
		return FALSE;
	}
	if (file_exists) {
		if (access (name, W_OK) == 0)
			return wants_to_overwrite (wbcg, name);
		else {
			err_str = g_strdup_printf (_("You do not have permission to save to\n%s"),
						   name);
			gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, err_str);
			g_free (err_str);
			return FALSE;
		}
	} else
		return TRUE;
}

static gboolean
do_save_as (WorkbookControlGUI *wbcg, WorkbookView *wb_view, const char *name)
{
	char *filename;
	gboolean success;
	const gchar *extension;

	if (*name == 0 || name [strlen (name) - 1] == '/') {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Please enter a file name,\nnot a directory"));
		return FALSE;
	}

	extension = file_saver_get_extension (wbcg->current_saver);
	if (strchr (g_basename (name), '.') == NULL && extension != NULL) {
		filename = g_strdup_printf ("%s.%s", name, extension);
	} else {
		filename = g_strdup (name);
	}
	if (!can_try_save_to (wbcg, filename)) {
		g_free (filename);
		return FALSE;
	}

	wb_view_preferred_size (wb_view, GTK_WIDGET (wbcg->notebook)->allocation.width,
				GTK_WIDGET (wbcg->notebook)->allocation.height);
	success = workbook_save_as (WORKBOOK_CONTROL (wbcg), wb_view, filename,
				    wbcg->current_saver);
	g_free (filename);
	return success;
}

gboolean
gui_file_save_as (WorkbookControlGUI *wbcg, WorkbookView *wb_view)
{
	GtkFileSelection *fsel;
	gboolean accepted = FALSE;
	GtkWidget *format_selector;
	gboolean success  = FALSE;
	Workbook *wb = wb_view_workbook (wb_view);

	g_return_val_if_fail (wbcg != NULL, FALSE);

	/* Verify that wbcg->current_saver is still registered */
	if (!is_saver_registered (wbcg->current_saver))
		wbcg->current_saver = NULL;

	fsel = GTK_FILE_SELECTION
		(gtk_file_selection_new (_("Save workbook as")));

	gtk_window_set_modal (GTK_WINDOW (fsel), TRUE);
	gnumeric_set_transient (wbcg, GTK_WINDOW (fsel));
	if (wb->filename)
		fs_set_filename (fsel, wb);

	/* Choose the format */
	format_selector = make_format_chooser (wbcg);
	gtk_box_pack_start (GTK_BOX (fsel->action_area), format_selector,
			    FALSE, TRUE, 0);

	/* Connect the signals for Ok and Cancel */
	gtk_signal_connect (GTK_OBJECT (fsel->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (handle_ok), &accepted);
	gtk_signal_connect (GTK_OBJECT (fsel->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
	gtk_signal_connect (GTK_OBJECT (fsel), "key_press_event",
			    GTK_SIGNAL_FUNC (fs_key_event), NULL);

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
		success = do_save_as (wbcg, wb_view,
				      gtk_file_selection_get_filename (fsel));
	gtk_widget_destroy (GTK_WIDGET (fsel));

	return success;
}

char *
dialog_query_load_file (WorkbookControlGUI *wbcg)
{
	GtkFileSelection *fsel;
	gboolean accepted = FALSE;
	char *result;
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	
	fsel = GTK_FILE_SELECTION (gtk_file_selection_new (_("Load file")));
	/* Select current directory if we have one */
	if (wb && wb->filename && (strchr (wb->filename, '/') != NULL)) {
		gchar *tmp = g_dirname (wb->filename);
		gchar *dirname = g_strconcat (tmp, "/", NULL);

		g_free (tmp);
		gtk_file_selection_set_filename (fsel, dirname);
		g_free (dirname);
	}
	gtk_window_set_modal (GTK_WINDOW (fsel), TRUE);
	gnumeric_set_transient (wbcg, GTK_WINDOW (fsel));

	/* Connect the signals for Ok and Cancel */
	gtk_signal_connect (GTK_OBJECT (fsel->ok_button), "clicked",
			    GTK_SIGNAL_FUNC (handle_ok), &accepted);
	gtk_signal_connect (GTK_OBJECT (fsel->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
	gtk_signal_connect (GTK_OBJECT (fsel), "key_press_event",
			    GTK_SIGNAL_FUNC (fs_key_event), NULL);

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

gboolean
gui_file_save (WorkbookControlGUI *wbcg, WorkbookView *wb_view)
{
	Workbook *wb = wb_view_workbook (wb_view);

	if (wb->file_format_level < FILE_FL_AUTO) {
		return gui_file_save_as (wbcg, wb_view);
	} else {
		wb_view_preferred_size (wb_view,
					GTK_WIDGET (wbcg->notebook)->allocation.width,
					GTK_WIDGET (wbcg->notebook)->allocation.height);
		return workbook_save (WORKBOOK_CONTROL (wbcg), wb_view);
	}
}
