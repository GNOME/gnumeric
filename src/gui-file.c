#include <config.h>
#include <errno.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "gui-file.h"
#include "sheet.h"
#include "application.h"
#include "io-context.h"
#include "command-context.h"
#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include "workbook.h"
#include <sys/stat.h>

static gint
file_opener_description_cmp (gconstpointer a, gconstpointer b)
{
	const GnumFileOpener *fo_a = a, *fo_b = b;

	return strcoll (gnum_file_opener_get_description (fo_a),
	                gnum_file_opener_get_description (fo_b));
}

static gint
file_saver_description_cmp (gconstpointer a, gconstpointer b)
{
	const GnumFileSaver *fs_a = a, *fs_b = b;

	return strcoll (gnum_file_saver_get_description (fs_a),
	                gnum_file_saver_get_description (fs_b));
}

static void
cb_select (GtkWidget *clist, gint row, gint column,
           GdkEventButton *event, GtkWidget *dialog)
{
	/* If the filter is double-clicked we proceed with importing and
	   dismiss chooser. */
	if (event && event->type == GDK_2BUTTON_PRESS) {
		gtk_signal_emit_by_name (GTK_OBJECT (dialog), "clicked", 0);
	}
}

/*
 * Lets the user choose an import filter for @filename, and
 * uses that to load the file
 */
gboolean
gui_file_import (WorkbookControlGUI *wbcg, const char *filename)
{
	GladeXML *gui;
	GtkWidget *dialog;
	GtkCList *clist;
	GnumFileOpener *fo = NULL;
	int row;
	GList *importers, *l;
	gint ret;

	gui = gnumeric_glade_xml_new (wbcg, "import.glade");
	if (gui == NULL) {
		return FALSE;
	}

	dialog = glade_xml_get_widget (gui, "import-dialog");
	gnome_dialog_set_default (GNOME_DIALOG (dialog), 0);

	clist = GTK_CLIST (glade_xml_get_widget (gui, "import-clist"));

	importers = g_list_sort (g_list_copy (get_file_importers ()),
	                         file_opener_description_cmp);
	for (l = importers, row = 0; l != NULL; l = l->next, row++) {
		GnumFileOpener *fo = l->data;
		char *text[1];

		text[0] = (gchar *) gnum_file_opener_get_description (fo);
		gtk_clist_append (clist, text);
		gtk_clist_set_row_data (clist, row, l->data);
	}
	if (row > 0) {
		gtk_clist_select_row (clist, 0, 0);
	}
	g_list_free (importers);

	gtk_signal_connect (GTK_OBJECT(clist), "select_row",
	                    GTK_SIGNAL_FUNC (cb_select), (gpointer) dialog);

	gtk_widget_grab_focus (GTK_WIDGET (clist));

	ret = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (ret == 0 && clist->selection != NULL) {
		fo = gtk_clist_get_row_data (clist, GPOINTER_TO_INT (clist->selection->data));
	}
	if (ret != -1) {
		gnome_dialog_close (GNOME_DIALOG (dialog));
	}
	gtk_object_unref (GTK_OBJECT (gui));

	if (fo != NULL) {
		return wb_view_open_custom (wb_control_view (WORKBOOK_CONTROL (wbcg)),
		                            WORKBOOK_CONTROL (wbcg), fo, filename);
	} else {
		return FALSE;
	}
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
saver_activate (GtkMenuItem *item, GnumFileSaver *saver)
{
	WorkbookControlGUI *wbcg;

	wbcg = gtk_object_get_data (GTK_OBJECT (item), "wbcg");
	wbcg->current_saver = saver;
}

static void
fill_save_menu (WorkbookControlGUI *wbcg, GtkOptionMenu *omenu, GtkMenu *menu)
{
	GList *savers, *l;

	savers = g_list_sort (g_list_copy (get_file_savers ()),
	                      file_saver_description_cmp);
	for (l = savers; l != NULL; l = l->next) {
		GtkWidget *menu_item;
		GnumFileSaver *fs = l->data;

		menu_item = gtk_menu_item_new_with_label (gnum_file_saver_get_description (fs));
		gtk_object_set_data (GTK_OBJECT (menu_item), "wbcg", wbcg);
		gtk_widget_show (menu_item);
		gtk_menu_append (menu, menu_item);
		gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
		                    GTK_SIGNAL_FUNC (saver_activate), fs);
	}
	gtk_option_menu_set_menu (GTK_OPTION_MENU (omenu), GTK_WIDGET (menu));

	if (wbcg->current_saver == NULL ||
	    g_list_find (savers, wbcg->current_saver) == NULL) {
		wbcg->current_saver = get_default_file_saver ();
	}
	gtk_option_menu_set_history (omenu,
	                             g_list_index (savers, wbcg->current_saver));

	g_list_free (savers);
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
	gboolean result = TRUE;
	gchar *msg;

	if (name == NULL || name[0] == '\0') {
		result = FALSE;
	} else if (name [strlen (name) - 1] == '/' ||
	    g_file_test (name, G_FILE_TEST_ISDIR)) {
		msg = g_strdup_printf (_("%s\nis a directory name"), name);
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, msg);
		g_free (msg);
		result = FALSE;
	} else if (access (name, W_OK) != 0 && errno != ENOENT) {
		msg = g_strdup_printf (
		      _("You do not have permission to save to\n%s"),
		      name);
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, msg);
		g_free (msg);
		result = FALSE;
	} else if (g_file_exists (name)) {
		msg = g_strdup_printf (
		      _("Workbook %s already exists.\n"
		      "Do you want to save over it?"), name);
		result = gnumeric_dialog_question_yes_no (wbcg, msg,
		         gnome_config_get_bool_with_default (
		         "Gnumeric/File/FileOverwriteDefaultAnswer=false", NULL));
		g_free (msg);
	}

	return result;
}

static gboolean
do_save_as (WorkbookControlGUI *wbcg, WorkbookView *wb_view, const char *name)
{
	char *filename;
	gboolean success = FALSE;

	if (*name == 0 || name [strlen (name) - 1] == '/') {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Please enter a file name,\nnot a directory"));
		return FALSE;
	}

	filename = gnum_file_saver_fix_file_name (wbcg->current_saver, name);
	if (!can_try_save_to (wbcg, filename)) {
		g_free (filename);
		return FALSE;
	}

	wb_view_preferred_size (wb_view, GTK_WIDGET (wbcg->notebook)->allocation.width,
				GTK_WIDGET (wbcg->notebook)->allocation.height);

	if (gnum_file_saver_get_save_scope (wbcg->current_saver) == FILE_SAVE_SHEET &&
	    gnome_config_get_bool_with_default ("Gnumeric/File/AskBeforeSavingOneSheet=true", NULL)) {
		gboolean accepted = TRUE;
		GList *sheets;
		gchar *msg = _("Selected file format doesn't support "
		               "saving multiple sheets in one file.\n"
		               "If you want to save all sheets, save them "
		               "in separate files or select different file format.\n"
		               "Do you want to save only current sheet?");

		sheets = workbook_sheets (wb_view_workbook (wb_view));
		if (g_list_length (sheets) > 1) {
			accepted = gnumeric_dialog_question_yes_no (wbcg, msg, TRUE);
		}
		g_list_free (sheets);
		if (!accepted) {
			g_free (filename);
			return FALSE;
		}
	}

	success = wb_view_save_as (wb_view, WORKBOOK_CONTROL (wbcg),
	                           wbcg->current_saver, filename);
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

	if (accepted) {
		success = do_save_as (wbcg, wb_view,
		                      gtk_file_selection_get_filename (fsel));
	}

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
		return wb_view_save (wb_view, WORKBOOK_CONTROL (wbcg));
	}
}
