/*
 * gui-file.c:
 *
 * Authors:
 *    Jon K Hellan (hellan@acm.org)
 *    Zbigniew Chyla (cyba@gnome.pl)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gui-file.h"

#include "gui-util.h"
#include "dialogs.h"
#include "sheet.h"
#include "application.h"
#include "io-context.h"
#include "command-context.h"
#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include "workbook.h"

#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-config.h>
#include <libgnome/gnome-util.h>
#include <errno.h>
#include <sys/stat.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

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

static GtkWidget *
make_format_chooser (GList *list, GtkOptionMenu *omenu)
{
	GList *l;
	GtkBox *box;
	GtkMenu *menu;

	/* Make format chooser */
	box = GTK_BOX (gtk_hbox_new (0, GNOME_PAD));
	menu = GTK_MENU (gtk_menu_new ());
	for (l = list; l != NULL; l = l->next) {
		GtkWidget *item;
		const gchar *descr;

		if (IS_GNUM_FILE_OPENER (l->data))
			descr = gnum_file_opener_get_description (
						GNUM_FILE_OPENER (l->data));
		else
			descr = gnum_file_saver_get_description (
						GNUM_FILE_SAVER (l->data));
		item = gtk_menu_item_new_with_label (descr);
		gtk_widget_show (item);
		gtk_menu_append (menu, item);
	}
	gtk_option_menu_set_menu (omenu, GTK_WIDGET (menu));
	gtk_box_pack_start (box, gtk_label_new (_("File format:")),
			    FALSE, FALSE, GNOME_PAD);
	gtk_box_pack_start (box, GTK_WIDGET (omenu), FALSE, TRUE, GNOME_PAD);

	return (GTK_WIDGET (box));
}

/*
 * Lets the user choose an import filter for selected file, and
 * uses that to load the file.
 */
void
gui_file_import (WorkbookControlGUI *wbcg)
{
	GList *importers;
	GtkFileSelection *fsel;
	GtkOptionMenu *omenu;
	GtkWidget *format_chooser;
	GnumFileOpener *fo = NULL;
	gchar const *file_name;

	if (gnome_config_get_bool_with_default (
	    "Gnumeric/File/ImportUsesAllOpeners=false", NULL)) {
		importers = get_file_openers ();
	} else {
		importers = get_file_importers ();
	}
	importers = g_list_copy (importers);
	importers = g_list_sort (importers, file_opener_description_cmp);

	/* Make format chooser */
	omenu = GTK_OPTION_MENU (gtk_option_menu_new ());
	format_chooser = make_format_chooser (importers, omenu);

	/* Pack it into file selector */
	fsel = GTK_FILE_SELECTION (gtk_file_selection_new (_("Import file")));
	gtk_file_selection_hide_fileop_buttons (fsel);
	gtk_box_pack_start (GTK_BOX (fsel->action_area), format_chooser,
	                    FALSE, TRUE, 0);

	/* Set default importer */
	fo = get_default_file_importer ();
	gtk_option_menu_set_history (omenu, g_list_index (importers, fo));

	/* Show file selector */
	if (!gnumeric_dialog_file_selection (wbcg, fsel)) {
		g_list_free (importers);
		gtk_object_destroy (GTK_OBJECT (fsel));
		return;
	}

	fo = g_list_nth_data (importers, gnumeric_option_menu_get_selected_index (omenu));
	file_name = gtk_file_selection_get_filename (fsel);
	if (fo != NULL) {
		(void) wb_view_open_custom (wb_control_view (WORKBOOK_CONTROL (wbcg)),
		                            WORKBOOK_CONTROL (wbcg), fo, file_name, TRUE);
	}
	gtk_object_destroy (GTK_OBJECT (fsel));
	g_list_free (importers);
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
check_multiple_sheet_support_if_needed (GnumFileSaver *fs,
					WorkbookControlGUI *wbcg, 
					WorkbookView *wb_view)
{
	gboolean ret_val = TRUE;

	if (gnum_file_saver_get_save_scope (fs) == FILE_SAVE_SHEET &&
	    gnome_config_get_bool_with_default ("Gnumeric/File/AskBeforeSavingOneSheet=true", NULL)) {
		GList *sheets;
		gchar *msg = _("Selected file format doesn't support "
			       "saving multiple sheets in one file.\n"
			       "If you want to save all sheets, save them "
			       "in separate files or select different file format.\n"
			       "Do you want to save only current sheet?"); 
		
		sheets = workbook_sheets (wb_view_workbook (wb_view));
		if (g_list_length (sheets) > 1) {
			ret_val = gnumeric_dialog_question_yes_no (wbcg, msg, TRUE);
		}
		g_list_free (sheets);
	}
	return (ret_val);
}

static gboolean
do_save_as (WorkbookControlGUI *wbcg, WorkbookView *wb_view,
            GnumFileSaver *fs, const char *name)
{
	char *filename;
	gboolean success = FALSE;

	if (*name == 0 || name [strlen (name) - 1] == '/') {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Please enter a file name,\nnot a directory"));
		return FALSE;
	}

	filename = gnum_file_saver_fix_file_name (fs, name);
	if (!can_try_save_to (wbcg, filename)) {
		g_free (filename);
		return FALSE;
	}

	wb_view_preferred_size (wb_view, GTK_WIDGET (wbcg->notebook)->allocation.width,
				GTK_WIDGET (wbcg->notebook)->allocation.height);

	success = check_multiple_sheet_support_if_needed (fs, wbcg, wb_view);
	if (!success) {
		g_free (filename);
		return FALSE;
	}

	success = wb_view_save_as (wb_view, fs, filename, COMMAND_CONTEXT (wbcg));
	g_free (filename);
	return success;
}

gboolean
gui_file_save_as (WorkbookControlGUI *wbcg, WorkbookView *wb_view)
{
	GList *savers;
	GtkFileSelection *fsel;
	GtkOptionMenu *omenu;
	GtkWidget *format_chooser;
	GnumFileSaver *fs;
	gboolean success  = FALSE;
	const gchar *wb_file_name;

	g_return_val_if_fail (wbcg != NULL, FALSE);

	savers = g_list_copy (get_file_savers ());
	savers = g_list_sort (savers, file_saver_description_cmp);

	/* Make format chooser */
	omenu = GTK_OPTION_MENU (gtk_option_menu_new ());
	format_chooser = make_format_chooser (savers, omenu);

	/* Pack it into file selector */
	fsel = GTK_FILE_SELECTION (gtk_file_selection_new (_("Save workbook as")));
	gtk_box_pack_start (GTK_BOX (fsel->action_area), format_chooser,
	                    FALSE, TRUE, 0);

	/* Set default file saver */
	fs = wbcg->current_saver;
	if (fs == NULL) {
		fs = (GnumFileSaver *) workbook_get_file_saver (wb_view_workbook (wb_view));
	}
	if (fs == NULL || g_list_find (savers, fs) == NULL) {
		fs = get_default_file_saver ();
	}
	gtk_option_menu_set_history (omenu, g_list_index (savers, fs));

	/* Set default file name */
	wb_file_name = workbook_get_filename (wb_view_workbook (wb_view));
	if (wb_file_name != NULL) {
		gchar *tmp_name, *p;

		tmp_name = g_strdup (wb_file_name);
		p = strrchr (tmp_name, '.');
		if (p != NULL) {
			*p = '\0';
		}
		gtk_file_selection_set_filename (fsel, tmp_name);
		g_free (tmp_name);
	}

	/* Show file selector */
	if (gnumeric_dialog_file_selection (wbcg, fsel)) {
		fs = g_list_nth_data (savers, gnumeric_option_menu_get_selected_index (omenu));
		if (fs != NULL) {
			success = do_save_as (wbcg, wb_view, fs,
			                      gtk_file_selection_get_filename (fsel));
			if (success) {
				wbcg->current_saver = fs;
			}
		} else {
			success = FALSE;
		}
	}

	gtk_widget_destroy (GTK_WIDGET (fsel));
	g_list_free (savers);

	return success;
}

void
gui_file_open (WorkbookControlGUI *wbcg)
{
	GtkFileSelection *fsel;
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Workbook *wb;
	const gchar *wb_file_name;
	
	fsel = GTK_FILE_SELECTION (gtk_file_selection_new (_("Load file")));
	gtk_file_selection_hide_fileop_buttons (fsel);

	/* Select current directory if we have one */
	wb = wb_control_workbook (wbc);
	wb_file_name = wb != NULL ? workbook_get_filename (wb) : NULL;
	if (wb_file_name != NULL && strchr (wb_file_name, '/') != NULL) {
		gchar *tmp, *dir_name;

		tmp = g_dirname (wb_file_name);
		dir_name = g_strconcat (tmp, "/", NULL);
		gtk_file_selection_set_filename (fsel, dir_name);
		g_free (dir_name);
		g_free (tmp);
	}

	if (gnumeric_dialog_file_selection (wbcg, fsel))
		(void) wb_view_open (wb_control_view (wbc), wbc,
			gtk_file_selection_get_filename (fsel), TRUE);

	gtk_widget_destroy (GTK_WIDGET (fsel));
}

gboolean
gui_file_save (WorkbookControlGUI *wbcg, WorkbookView *wb_view)
{
	Workbook *wb;
	
	wb_view_preferred_size (wb_view,
	                        GTK_WIDGET (wbcg->notebook)->allocation.width,
	                        GTK_WIDGET (wbcg->notebook)->allocation.height);

	wb = wb_view_workbook (wb_view);
	if (wb->file_format_level < FILE_FL_AUTO)
		return gui_file_save_as (wbcg, wb_view);
	else
		return wb_view_save (wb_view, COMMAND_CONTEXT (wbcg));
}

#ifdef ENABLE_BONOBO
static GnumFileSaver *
ask_for_file_saver (WorkbookControlGUI *wbcg, WorkbookView *wb_view)
{
	GtkWidget *dialog;
	GtkWidget *format_chooser;
	GtkOptionMenu *omenu;
	GList *savers;
	GnumFileSaver *fs;
	const gchar *buttons[] = {GNOME_STOCK_BUTTON_OK,
		                  GNOME_STOCK_BUTTON_CANCEL, NULL}; 

	dialog = gnome_message_box_newv (_("Which file format would you like?"),
					 GNOME_MESSAGE_BOX_QUESTION, buttons); 
	gnome_dialog_set_close (GNOME_DIALOG (dialog), FALSE);
	
	/* Add the format chooser */
	savers = g_list_copy (get_file_savers ());
	savers = g_list_sort (savers, file_saver_description_cmp);
	omenu = GTK_OPTION_MENU (gtk_option_menu_new ());
	format_chooser = make_format_chooser (savers, omenu);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox),
			    format_chooser, FALSE, FALSE, 0);
	
	/* Set default file saver */

	fs = wbcg->current_saver;
	if (fs == NULL) {
		fs = (GnumFileSaver *) workbook_get_file_saver (
						wb_view_workbook (wb_view));
	}
	if (fs == NULL || g_list_find (savers, fs) == NULL) {
		fs = get_default_file_saver ();
	}
	gtk_option_menu_set_history (omenu, g_list_index (savers, fs));
	gtk_widget_show_all (dialog);
	
	switch (gnome_dialog_run (GNOME_DIALOG (dialog))) {
	case 0: /* Ok */
		fs = g_list_nth_data (savers,
			gnumeric_option_menu_get_selected_index (omenu)); 
		break;
	default: /* Cancel */
		fs = NULL;
		break;
	}
	gnome_dialog_close (GNOME_DIALOG (dialog));
	g_list_free (savers);
	
	return (fs);
}

void
gui_file_save_to_stream (BonoboStream *stream, WorkbookControlGUI *wbcg,
			 WorkbookView *wb_view, const gchar *mime_type,
			 CORBA_Environment *ev)
{
	GnumFileSaver *fs = NULL;
	IOContext *io_context;

	/* If no mime type is given, we need to ask. */
	if (!mime_type) {
		fs = ask_for_file_saver (wbcg, wb_view);
		if (!fs) {
			CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
					     ex_Bonobo_IOError, NULL);
			return;
		}
	} else {
		fs = get_file_saver_for_mime_type (mime_type);
		if (!fs) {
			CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
					     ex_Bonobo_Stream_NotSupported,
					     NULL);
			return;
		}
	}

	if (!check_multiple_sheet_support_if_needed (fs, wbcg, wb_view)) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_Bonobo_IOError, NULL);
		return;
	}

	io_context = gnumeric_io_context_new (COMMAND_CONTEXT (wbcg));
	gnum_file_saver_save_to_stream (fs, io_context, wb_view, stream, ev);
	gtk_object_destroy (GTK_OBJECT (io_context));
}
#endif

