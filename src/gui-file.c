/* vim: set sw=8: */
/*
 * GUI-file.c:
 *
 * Authors:
 *    Jon K Hellan (hellan@acm.org)
 *    Zbigniew Chyla (cyba@gnome.pl)
 *    Andreas J. Guelzow (aguelzow@taliesin.ca)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
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
#include "workbook-priv.h"
#include "gnumeric-gconf.h"
#include "widgets/widget-charmap-selector.h"
#include <goffice/utils/go-file.h>

#include <gtk/gtkcombobox.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktable.h>
#include <gtk/gtkcombo.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkfilechooserdialog.h>
#include <glade/glade.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#define PREVIEW_HSIZE 150
#define PREVIEW_VSIZE 150

typedef struct 
{
	CharmapSelector *charmap_selector;
	GtkWidget	*charmap_label;
	GList *openers;
} file_format_changed_cb_data;

 

static gint
file_opener_description_cmp (gconstpointer a, gconstpointer b)
{
	GnmFileOpener const *fo_a = a, *fo_b = b;

	return g_utf8_collate (gnm_file_opener_get_description (fo_a),
			       gnm_file_opener_get_description (fo_b));
}

static gint
file_saver_description_cmp (gconstpointer a, gconstpointer b)
{
	GnmFileSaver const *fs_a = a, *fs_b = b;

	return g_utf8_collate (gnm_file_saver_get_description (fs_a),
			       gnm_file_saver_get_description (fs_b));
}

static void
make_format_chooser (GList *list, GtkComboBox *combo)
{
	GList *l;

	/* Make format chooser */
	for (l = list; l != NULL; l = l->next) {
		gchar const *descr;

		if (!l->data)
			descr = _("Automatically detected");
		else if (IS_GNM_FILE_OPENER (l->data))
			descr = gnm_file_opener_get_description (
						GNM_FILE_OPENER (l->data));
		else
			descr = gnm_file_saver_get_description (
				                GNM_FILE_SAVER (l->data));

		gtk_combo_box_append_text (combo, descr);
	}
}

/* Show view in a wbcg. Use current or new wbcg according to policy */
void
gui_wb_view_show (WorkbookControlGUI *wbcg, WorkbookView *wbv)
{
	WorkbookControlGUI *new_wbcg = NULL;
	Workbook *tmp_wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));

	if (workbook_is_pristine (tmp_wb)) {
		g_object_ref (G_OBJECT (wbcg));
		workbook_unref (tmp_wb);
		wb_control_set_view (WORKBOOK_CONTROL (wbcg), wbv, NULL);
		wb_control_init_state (WORKBOOK_CONTROL (wbcg));
	} else {
		GdkScreen *screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
		WorkbookControl *new_wbc =
			wb_control_wrapper_new (WORKBOOK_CONTROL (wbcg),
						wbv, NULL, screen);
		new_wbcg = WORKBOOK_CONTROL_GUI (new_wbc);

		wbcg_copy_toolbar_visibility (new_wbcg, wbcg);
	}

	sheet_update (wb_view_cur_sheet	(wbv));
}

gboolean
gui_file_read (WorkbookControlGUI *wbcg, char const *uri,
	       GnmFileOpener const *optional_format, gchar const *optional_encoding)
{
	IOContext *io_context;
	WorkbookView *wbv;

	gnm_cmd_context_set_sensitive (GNM_CMD_CONTEXT (wbcg), FALSE);
	io_context = gnumeric_io_context_new (GNM_CMD_CONTEXT (wbcg));
	wbv = wb_view_new_from_uri (uri, optional_format, io_context, 
				    optional_encoding);

	if (gnumeric_io_error_occurred (io_context) ||
	    gnumeric_io_warning_occurred (io_context))
		gnumeric_io_error_display (io_context);

	g_object_unref (G_OBJECT (io_context));
	gnm_cmd_context_set_sensitive (GNM_CMD_CONTEXT (wbcg), TRUE);

	if (wbv != NULL) {
		gui_wb_view_show (wbcg, wbv);
		return TRUE;
	}
	return FALSE;
}

static void
file_format_changed_cb (GtkComboBox *format_combo,
			file_format_changed_cb_data *data)
{
	GnmFileOpener *fo = g_list_nth_data (data->openers,
		gtk_combo_box_get_active (format_combo));
	gboolean is_sensitive = fo != NULL && gnm_file_opener_is_encoding_dependent (fo);

	gtk_widget_set_sensitive (GTK_WIDGET (data->charmap_selector), is_sensitive);
	gtk_widget_set_sensitive (data->charmap_label, is_sensitive);
}


static gint
file_opener_find_by_id (GList *openers, char const *id)
{
	GList *l;
	gint i = 0;

	if (id == NULL)
		return 0;
	
	for (l = openers; l != NULL; l = l->next, i++) {
		if (IS_GNM_FILE_OPENER (l->data) &&
		    strcmp (id, gnm_file_opener_get_id(l->data)) == 0)
			return i;
	}

	return 0;
}

/*
 * Suggests automatic file type recognition, but lets the user choose an
 * import filter for selected file.
 */
void
gui_file_open (WorkbookControlGUI *wbcg, char const *default_format)
{
	GList *openers;
	GtkFileChooser *fsel;
	GtkComboBox *format_combo;
	GtkWidget *charmap_selector;
	file_format_changed_cb_data data;
	gint opener_default;
	char const *title;
	char *uri = NULL;
	const char *encoding = NULL;
	GnmFileOpener *fo = NULL;
	Workbook *workbook = wb_control_workbook (WORKBOOK_CONTROL (wbcg));

	openers = g_list_sort (g_list_copy (get_file_openers ()),
			       file_opener_description_cmp);
	/* NULL represents automatic file type recognition */
	openers = g_list_prepend (openers, NULL);
	opener_default = file_opener_find_by_id (openers, default_format);
	title = (opener_default == 0)
		? _("Load file") 
		: (gnm_file_opener_get_description 
		   (g_list_nth_data (openers, opener_default)));
	data.openers = openers;

	/* Make charmap chooser */
	charmap_selector = charmap_selector_new (CHARMAP_SELECTOR_TO_UTF8);
	data.charmap_selector = CHARMAP_SELECTOR(charmap_selector);
	data.charmap_label = gtk_label_new_with_mnemonic (_("Character _encoding:"));

	/* Make format chooser */
	format_combo = GTK_COMBO_BOX (gtk_combo_box_new_text ());
	make_format_chooser (openers, format_combo);
	g_signal_connect (G_OBJECT (format_combo), "changed",
                          G_CALLBACK (file_format_changed_cb), &data);
	gtk_combo_box_set_active (format_combo, opener_default);
	gtk_widget_set_sensitive (GTK_WIDGET (format_combo), opener_default == 0);
	file_format_changed_cb (format_combo, &data);

	fsel = GTK_FILE_CHOOSER
		(g_object_new (GTK_TYPE_FILE_CHOOSER_DIALOG,
			       "action", GTK_FILE_CHOOSER_ACTION_OPEN,
			       "local-only", FALSE,
			       "title", _("Select a file"),
			       NULL));
	gtk_dialog_add_buttons (GTK_DIALOG (fsel),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OPEN, GTK_RESPONSE_OK,
				NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (fsel), GTK_RESPONSE_OK);

	/* Start in the same directory as the current workbook.  */
	gtk_file_chooser_select_uri (fsel, workbook_get_uri (workbook));
	gtk_file_chooser_unselect_all (fsel);

	/* Filters */
	{	
		GtkFileFilter *filter;
		GList *l;

		filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (filter, _("All Files"));
		gtk_file_filter_add_pattern (filter, "*");
		gtk_file_chooser_add_filter (fsel, filter);

		filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (filter, _("Spreadsheets"));
		for (l = openers->next; l; l = l->next) {
			GnmFileOpener *o = l->data;
			/* FIXME: add all known extensions.  */
		}
#warning "FIXME: make extension discovery above work and delete these"
		/* Use _SAVERS'_ extension for lack of better.  */
		for (l = get_file_savers ()->next; l; l = l->next) {
			GnmFileSaver *fs = l->data;
			const char *ext = gnm_file_saver_get_extension (fs);
			const char *mime = gnm_file_saver_get_mime_type (fs);

			if (mime)
				gtk_file_filter_add_mime_type (filter, mime);

			if (ext) {
				char *pattern = g_strconcat ("*.", ext, NULL);
				gtk_file_filter_add_pattern (filter, pattern);
				g_free (pattern);
			}
		}
		/* FIXME: delete these also when we can ask the openers.  */
		gtk_file_filter_add_pattern (filter, "*.csv");
		gtk_file_filter_add_pattern (filter, "*.tsv");

		gtk_file_chooser_add_filter (fsel, filter);
		/* Make this filter the default */
		gtk_file_chooser_set_filter (fsel, filter);
	}

	{
		GtkWidget *label;
		GtkWidget *box = gtk_table_new (2, 2, FALSE);

		gtk_table_attach (GTK_TABLE (box),
				  GTK_WIDGET (format_combo),
				  1, 2, 0, 1, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 5, 2);
		label = gtk_label_new_with_mnemonic (_("File _type:"));
		gtk_table_attach (GTK_TABLE (box), label,
				  0, 1, 0, 1, GTK_SHRINK | GTK_FILL, GTK_SHRINK, 5, 2);
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (format_combo));

		gtk_table_attach (GTK_TABLE (box),
				  charmap_selector,
				  1, 2, 1, 2, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 5, 2);
		gtk_table_attach (GTK_TABLE (box), data.charmap_label,
				  0, 1, 1, 2, GTK_SHRINK | GTK_FILL, GTK_SHRINK, 5, 2);
		gtk_label_set_mnemonic_widget (GTK_LABEL (data.charmap_label),
					       charmap_selector);

		gtk_file_chooser_set_extra_widget (fsel, box);
	}

	/* Show file selector */
	if (!gnumeric_dialog_file_selection (wbcg, GTK_WIDGET (fsel)))
		goto out;

	uri = gtk_file_chooser_get_uri (fsel);
	encoding = charmap_selector_get_encoding (CHARMAP_SELECTOR (charmap_selector));
	fo = g_list_nth_data (openers, gtk_combo_box_get_active (format_combo));

 out:
	gtk_widget_destroy (GTK_WIDGET (fsel));
	g_list_free (openers);

	if (uri) {
		/* Make sure dialog goes away right now.  */
		while (g_main_context_iteration (NULL, FALSE));

		gui_file_read (wbcg, uri, fo, encoding);
		g_free (uri);
	}
}

static void
update_preview_cb (GtkFileChooser *chooser)
{
	gchar *filename = gtk_file_chooser_get_preview_filename (chooser);
	GtkWidget *label = g_object_get_data (G_OBJECT (chooser), "label-widget");
	GtkWidget *image = g_object_get_data (G_OBJECT (chooser), "image-widget");

	if (filename == NULL) {
		gtk_widget_hide (image);
		gtk_widget_hide (label);
	} else if (g_file_test (filename, G_FILE_TEST_IS_DIR)) {
		/* Not quite sure what to do here.  */
		gtk_widget_hide (image);
		gtk_widget_hide (label);
	} else {
		GdkPixbuf *buf;
		gboolean dummy;

		buf = gdk_pixbuf_new_from_file (filename, NULL);
		if (buf) {
			dummy = FALSE;
		} else {
			buf = gnm_app_get_pixbuf ("unknown_image");
			g_object_ref (buf);
			dummy = TRUE;
		}

		if (buf) {
			GdkPixbuf *pixbuf = gnm_pixbuf_intelligent_scale (buf, PREVIEW_HSIZE, PREVIEW_VSIZE);
			gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
			g_object_unref (pixbuf);
			gtk_widget_show (image);

			if (dummy)
				gtk_label_set_text (GTK_LABEL (label), "");
			else {
				int w = gdk_pixbuf_get_width (buf);
				int h = gdk_pixbuf_get_height (buf);
				char *size = g_strdup_printf (_("%d x %d"), w, h);
				gtk_label_set_text (GTK_LABEL (label), size);
				g_free (size);
			}
			gtk_widget_show (label);

			g_object_unref (buf);
		}

		g_free (filename);
	}
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
go_file_is_writable (char const *uri, GtkWindow *parent)
{
	gboolean result = TRUE;
	char *filename;

	if (uri == NULL || uri[0] == '\0')
		result = FALSE;

	filename = go_filename_from_uri (uri);
	if (!filename)
		return TRUE;  /* Just assume writable.  */

#ifndef G_IS_DIR_SEPARATOR
/* Recent glib 2.6 addition.  */
#define G_IS_DIR_SEPARATOR(c) ((c) == G_DIR_SEPARATOR || (c) == '/')
#endif
	if (G_IS_DIR_SEPARATOR (filename [strlen (filename) - 1]) ||
	    g_file_test (filename, G_FILE_TEST_IS_DIR)) {
		char *msg = g_strdup_printf (_("%s\nis a directory name"), uri);
		gnumeric_notice (parent, GTK_MESSAGE_ERROR, msg);
		g_free (msg);
		result = FALSE;
	} else if (access (filename, W_OK) != 0 && errno != ENOENT) {
		char *msg = g_strdup_printf (
		      _("You do not have permission to save to\n%s"),
		      uri);
		gnumeric_notice (parent, GTK_MESSAGE_ERROR, msg);
		g_free (msg);
		result = FALSE;
	} else if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		char *dirname = go_dirname_from_uri (uri, TRUE);
		char *basename = go_basename_from_uri (uri);
		char *msg = g_markup_printf_escaped (
			_("A file called <i>%s</i> already exists in %s.\n\n"
			  "Do you want to save over it?"),
			basename, dirname);
		GtkWidget *dialog = gtk_message_dialog_new_with_markup
			(parent,
			 GTK_DIALOG_DESTROY_WITH_PARENT,
			 GTK_MESSAGE_WARNING,
			 GTK_BUTTONS_OK_CANCEL,
			 msg);
		gtk_dialog_set_default_response (GTK_DIALOG (dialog),
						 gnm_app_prefs->file_overwrite_default_answer
						 ? GTK_RESPONSE_OK
						 : GTK_RESPONSE_CANCEL);
		result = gnumeric_dialog_run (parent, GTK_DIALOG (dialog))
			== GTK_RESPONSE_OK;
		g_free (dirname);
		g_free (basename);
		g_free (msg);
	}

	g_free (filename);
	return result;
}

static gboolean
filter_images (const GtkFileFilterInfo *filter_info, gpointer data)
{
	return filter_info->mime_type &&
		strncmp (filter_info->mime_type, "image/", 6) == 0;
}

static GtkFileChooser *
gui_image_chooser_new (WorkbookControlGUI *wbcg, gboolean is_save)
{
	GtkFileChooser *fsel;

	fsel = GTK_FILE_CHOOSER
		(g_object_new (GTK_TYPE_FILE_CHOOSER_DIALOG,
			       "action", is_save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
			       "local-only", FALSE,
			       "use-preview-label", FALSE,
			       NULL));
	gtk_dialog_add_buttons (GTK_DIALOG (fsel),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				is_save ? GTK_STOCK_SAVE : GTK_STOCK_OPEN, 
				GTK_RESPONSE_OK,
				NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (fsel), GTK_RESPONSE_OK);
	/* Filters */
	{	
		GtkFileFilter *filter;

		filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (filter, _("All Files"));
		gtk_file_filter_add_pattern (filter, "*");
		gtk_file_chooser_add_filter (fsel, filter);

		filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (filter, _("Images"));
		gtk_file_filter_add_custom (filter, GTK_FILE_FILTER_MIME_TYPE,
					    filter_images, NULL, NULL);
		gtk_file_chooser_add_filter (fsel, filter);
		/* Make this filter the default */
		gtk_file_chooser_set_filter (fsel, filter);
	}

	/* Preview */
	{
		GtkWidget *vbox = gtk_vbox_new (FALSE, 2);
		GtkWidget *preview_image = gtk_image_new ();
		GtkWidget *preview_label = gtk_label_new ("");

		g_object_set_data (G_OBJECT (fsel), "image-widget", preview_image);
		g_object_set_data (G_OBJECT (fsel), "label-widget", preview_label);

		gtk_widget_set_size_request (vbox, PREVIEW_HSIZE, -1);

		gtk_box_pack_start (GTK_BOX (vbox), preview_image, FALSE, FALSE, 0);
		gtk_box_pack_start (GTK_BOX (vbox), preview_label, FALSE, FALSE, 0);
		gtk_file_chooser_set_preview_widget (fsel, vbox);
		g_signal_connect (fsel, "update-preview",
				  G_CALLBACK (update_preview_cb), NULL);
		update_preview_cb (fsel);
	}
	return fsel;
}

char *
gui_get_image_save_info (WorkbookControlGUI *wbcg,
			 GSList *formats, 
			 GnmImageFormat **ret_format)
{
	GtkFileChooser *fsel;
	GnmImageFormat *sel_format = NULL;
	GtkComboBox *format_combo = NULL;
	char *uri = NULL;

	fsel = gui_image_chooser_new (wbcg, TRUE);
	g_object_set (G_OBJECT (fsel), "title", _("Save as"), NULL);
	
	/* Make format chooser */
	if (formats && ret_format) {
		GtkWidget *label;
		GtkWidget *box = gtk_hbox_new (FALSE, 5);
		GSList *l;
		int i;

		format_combo = GTK_COMBO_BOX (gtk_combo_box_new_text ());
		if (*ret_format)
			sel_format = *ret_format;
		for (l = formats, i = 0; l != NULL; l = l->next, i++) {
			gtk_combo_box_append_text 
				(format_combo, 
				 ((GnmImageFormat *) (l->data))->desc);
			if (l->data == (void *)sel_format)
				gtk_combo_box_set_active (format_combo, i);
		}
		if (gtk_combo_box_get_active (format_combo) < 0)
			gtk_combo_box_set_active (format_combo, 0);
		
		label = gtk_label_new_with_mnemonic (_("File _type:"));
		gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 0);
		gtk_box_pack_start (GTK_BOX (box),  GTK_WIDGET (format_combo),
				    TRUE, TRUE, 0);
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), 
					       GTK_WIDGET (format_combo));
		gtk_file_chooser_set_extra_widget (fsel, box);
	}

	/* Show file selector */
 loop:
	if (!gnumeric_dialog_file_selection (wbcg, GTK_WIDGET (fsel)))
		goto out;
	uri = gtk_file_chooser_get_uri (fsel);
	if (!go_file_is_writable (uri, GTK_WINDOW (fsel))) {
		g_free (uri);
		goto loop;
	}
	if (format_combo) {
		char *new_uri = NULL;

		sel_format = g_slist_nth_data (
			formats, gtk_combo_box_get_active (format_combo));
		if (!gnm_vrfy_uri_ext (sel_format->ext,
				       uri, &new_uri) &&
		    !gnumeric_dialog_question_yes_no 
		    (GTK_WINDOW (fsel),
		     _("The given file extension does not match the"
		       " chosen file type. Do you want to use this name"
		       " anyway?"), TRUE)) {
			g_free (new_uri);
			g_free (uri);
			uri = NULL;
			goto out;
		} else {
			g_free (uri);
			uri = new_uri;
		}
		*ret_format = sel_format;
	}
 out:
	gtk_widget_destroy (GTK_WIDGET (fsel));
	return uri;
}

char *
gui_image_file_select (WorkbookControlGUI *wbcg, const char *initial)
{
	GtkFileChooser *fsel;
	char *uri = NULL;

	fsel = gui_image_chooser_new (wbcg, FALSE);
	if (initial)
		gtk_file_chooser_set_uri (fsel, initial);
	g_object_set (G_OBJECT (fsel), "title", _("Select an Image"), NULL);
	
	/* Show file selector */
	if (gnumeric_dialog_file_selection (wbcg, GTK_WIDGET (fsel)))
		uri = gtk_file_chooser_get_uri (fsel);
	gtk_widget_destroy (GTK_WIDGET (fsel));
	return uri;
}

static gboolean
check_multiple_sheet_support_if_needed (GnmFileSaver *fs,
					GtkWindow *parent,
					WorkbookView *wb_view)
{
	gboolean ret_val = TRUE;

	if (gnm_file_saver_get_save_scope (fs) != FILE_SAVE_WORKBOOK &&
	    gnm_app_prefs->file_ask_single_sheet_save) {
		GList *sheets;
		gchar *msg = _("Selected file format doesn't support "
			       "saving multiple sheets in one file.\n"
			       "If you want to save all sheets, save them "
			       "in separate files or select different file format.\n"
			       "Do you want to save only current sheet?");

		sheets = workbook_sheets (wb_view_workbook (wb_view));
		if (g_list_length (sheets) > 1) {
			ret_val = gnumeric_dialog_question_yes_no (
				parent, msg, TRUE);
		}
		g_list_free (sheets);
	}
	return (ret_val);
}

static gboolean
do_save_as (WorkbookControlGUI *wbcg, WorkbookView *wb_view,
            GnmFileSaver *fs, char const *uri, GtkWindow *parent)
{
	gboolean success = FALSE;

	wb_view_preferred_size (wb_view, GTK_WIDGET (wbcg->notebook)->allocation.width,
				GTK_WIDGET (wbcg->notebook)->allocation.height);

	success = check_multiple_sheet_support_if_needed (fs, parent, wb_view);
	if (!success) goto out;

	success = wb_view_save_as (wb_view, fs, uri, GNM_CMD_CONTEXT (wbcg));

out:
	return success;
}

gboolean
gui_file_save_as (WorkbookControlGUI *wbcg, WorkbookView *wb_view)
{
	GList *savers = NULL, *l;
	GtkFileChooser *fsel;
	GtkComboBox *format_combo;
	GnmFileSaver *fs;
	gboolean success  = FALSE;
	gchar const *wb_uri;
	char *uri;

	g_return_val_if_fail (wbcg != NULL, FALSE);

	for (l = get_file_savers (); l; l = l->next) {
		if ((l->data == NULL) || 
		    (gnm_file_saver_get_save_scope (GNM_FILE_SAVER (l->data)) 
		     != FILE_SAVE_RANGE))
			savers = g_list_prepend (savers, l->data);
	}
	savers = g_list_sort (savers, file_saver_description_cmp);

	fsel = GTK_FILE_CHOOSER
		(g_object_new (GTK_TYPE_FILE_CHOOSER_DIALOG,
			       "action", GTK_FILE_CHOOSER_ACTION_SAVE,
			       "title", _("Select a file"),
			       NULL));
	gtk_dialog_add_buttons (GTK_DIALOG (fsel),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_SAVE, GTK_RESPONSE_OK,
				NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (fsel), GTK_RESPONSE_OK);

	/* Filters */
	{	
		GtkFileFilter *filter;
		GList *l;

		filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (filter, _("All Files"));
		gtk_file_filter_add_pattern (filter, "*");
		gtk_file_chooser_add_filter (fsel, filter);

		filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (filter, _("Spreadsheets"));
		for (l = savers->next; l; l = l->next) {
			GnmFileSaver *fs = l->data;
			const char *ext = gnm_file_saver_get_extension (fs);
			const char *mime = gnm_file_saver_get_mime_type (fs);

			if (mime)
				gtk_file_filter_add_mime_type (filter, mime);

#warning "FIXME: do we get all extensions?"
			/* Well, we don't get things we cannot save.  */
			if (ext) {
				char *pattern = g_strconcat ("*.", ext, NULL);
				gtk_file_filter_add_pattern (filter, pattern);
				g_free (pattern);
			}
		}
		gtk_file_chooser_add_filter (fsel, filter);
		/* Make this filter the default */
		gtk_file_chooser_set_filter (fsel, filter);
	}

	{
		GtkWidget *box = gtk_hbox_new (FALSE, 2);
		GtkWidget *label = gtk_label_new_with_mnemonic (_("File _type:"));
		format_combo = GTK_COMBO_BOX (gtk_combo_box_new_text ());
		make_format_chooser (savers, format_combo);

		gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 6);
		gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (format_combo), FALSE, TRUE, 6);
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (format_combo));

		gtk_file_chooser_set_extra_widget (fsel, box);
	}

	/* Set default file saver */
	fs = wbcg->current_saver;
	if (fs == NULL)
		fs = workbook_get_file_saver (wb_view_workbook (wb_view));
	if (fs == NULL || g_list_find (savers, fs) == NULL)
		fs = gnm_file_saver_get_default ();

	gtk_combo_box_set_active (format_combo, g_list_index (savers, fs));

	/* Set default file name */
	wb_uri = workbook_get_uri (wb_view_workbook (wb_view));
	if (wb_uri != NULL) {
		char *basename = go_basename_from_uri (wb_uri);
		char *dot = strrchr (basename, '.');

		gtk_file_chooser_set_uri (fsel, wb_uri);
		gtk_file_chooser_unselect_all (fsel);

		/* Remove extension.  */
		if (dot && dot != basename)
			*dot = 0;
		gtk_file_chooser_set_current_name (fsel, basename);
		g_free (basename);
	}

	while (1) {
		char *uri2 = NULL;

		/* Show file selector */
		if (!gnumeric_dialog_file_selection (wbcg, GTK_WIDGET (fsel)))
			goto out;
		fs = g_list_nth_data (savers, gtk_combo_box_get_active (format_combo));
		if (!fs)
			goto out;
		uri = gtk_file_chooser_get_uri (fsel);
		if (!gnm_vrfy_uri_ext (gnm_file_saver_get_extension (fs), 
				       uri, &uri2) &&
		    !gnumeric_dialog_question_yes_no (GTK_WINDOW (fsel),
						      _("The given file extension does not match the"
							" chosen file type. Do you want to use this name"
							" anyway?"), TRUE)) {
			g_free (uri);
			g_free (uri2);
			goto out;
		}

		g_free (uri);
		uri = uri2;

		if (go_file_is_writable (uri, GTK_WINDOW (fsel)))
			break;

		g_free (uri);
	}

	success = do_save_as (wbcg, wb_view, fs, uri, GTK_WINDOW (fsel));
	if (success)
		wbcg->current_saver = fs;

	g_free (uri);

 out:
	gtk_widget_destroy (GTK_WIDGET (fsel));
	g_list_free (savers);

	return success;
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
		return wb_view_save (wb_view, GNM_CMD_CONTEXT (wbcg));
}
