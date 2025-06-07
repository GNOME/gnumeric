/*
 * gui-file.c:
 *
 * Authors:
 *    Jon K Hellan (hellan@acm.org)
 *    Zbigniew Chyla (cyba@gnome.pl)
 *    Andreas J. Guelzow (aguelzow@pyrshep.ca)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <gui-file.h>

#include <gui-util.h>
#include <gutils.h>
#include <dialogs/dialogs.h>
#include <sheet.h>
#include <application.h>
#include <command-context.h>
#include <wbc-gtk-impl.h>
#include <workbook-view.h>
#include <workbook-priv.h>
#include <gnumeric-conf.h>
#include <application.h>

#include <goffice/goffice.h>

#include <unistd.h>
#include <string.h>

typedef struct {
	GOCharmapSel *go_charmap_sel;
	GtkWidget *charmap_label;
	GList *openers;
} file_opener_format_changed_cb_data;



static gint
file_opener_description_cmp (gconstpointer a, gconstpointer b)
{
	GOFileOpener const *fo_a = a, *fo_b = b;

	return g_utf8_collate (go_file_opener_get_description (fo_a),
			       go_file_opener_get_description (fo_b));
}

static gint
file_saver_description_cmp (gconstpointer a, gconstpointer b)
{
	GOFileSaver const *fs_a = a, *fs_b = b;

	return g_utf8_collate (go_file_saver_get_description (fs_a),
			       go_file_saver_get_description (fs_b));
}

static void
make_format_chooser (GList *list, GtkComboBox *combo)
{
	GList *l;
	GtkComboBoxText *bt = GTK_COMBO_BOX_TEXT (combo);

	/* Make format chooser */
	for (l = list; l != NULL; l = l->next) {
		GObject *obj = l->data;
		gchar const *descr;

		if (!obj)
			descr = _("Automatically detected");
		else if (GO_IS_FILE_OPENER (obj))
			descr = go_file_opener_get_description (GO_FILE_OPENER (obj));
		else
			descr = go_file_saver_get_description (GO_FILE_SAVER (obj));

		gtk_combo_box_text_append_text (bt, descr);
	}
}

/* Show view in a wbcg. Use current or new wbcg according to policy */
void
gui_wb_view_show (WBCGtk *wbcg, WorkbookView *wbv)
{
	WBCGtk *new_wbcg = NULL;
	Workbook *tmp_wb = wb_control_get_workbook (GNM_WBC (wbcg));

	if (go_doc_is_pristine (GO_DOC (tmp_wb))) {
		g_object_ref (wbcg);
		g_object_unref (tmp_wb);
		wb_control_set_view (GNM_WBC (wbcg), wbv, NULL);
		wb_control_init_state (GNM_WBC (wbcg));
	} else {
		GdkScreen *screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
		WorkbookControl *new_wbc =
			workbook_control_new_wrapper (GNM_WBC (wbcg),
						wbv, NULL, screen);
		new_wbcg = WBC_GTK (new_wbc);

		wbcg_copy_toolbar_visibility (new_wbcg, wbcg);
	}

	sheet_update (wb_view_cur_sheet	(wbv));
}

/**
 * gui_file_read:
 *
 * Returns: (transfer none): the new #WorkbookView for the file read.
 **/
WorkbookView *
gui_file_read (WBCGtk *wbcg, char const *uri,
	       GOFileOpener const *optional_format, gchar const *optional_encoding)
{
	GOIOContext *io_context;
	WorkbookView *wbv;

	go_cmd_context_set_sensitive (GO_CMD_CONTEXT (wbcg), FALSE);
	io_context = go_io_context_new (GO_CMD_CONTEXT (wbcg));
	wbv = workbook_view_new_from_uri (uri, optional_format, io_context,
					  optional_encoding);

	if (go_io_error_occurred (io_context) ||
	    go_io_warning_occurred (io_context))
		go_io_error_display (io_context);

	g_object_unref (io_context);
	go_cmd_context_set_sensitive (GO_CMD_CONTEXT (wbcg), TRUE);

	if (wbv != NULL) {
		gui_wb_view_show (wbcg, wbv);
		workbook_update_history (wb_view_get_workbook (wbv), GNM_FILE_SAVE_AS_STYLE_SAVE);
	} else {
		/* Somehow fixes #625687.  Don't know why.  */
		wbcg_focus_cur_scg (wbcg);
	}

	return wbv;
}

gboolean
gnm_gui_file_template (WBCGtk *wbcg, char const *uri)
{
	GOIOContext *io_context;
	WorkbookView *wbv;
	GOFileOpener const *optional_format = NULL;
	gchar const *optional_encoding = NULL;

	go_cmd_context_set_sensitive (GO_CMD_CONTEXT (wbcg), FALSE);
	io_context = go_io_context_new (GO_CMD_CONTEXT (wbcg));
	wbv = workbook_view_new_from_uri (uri, optional_format, io_context,
				    optional_encoding);

	if (go_io_error_occurred (io_context) ||
	    go_io_warning_occurred (io_context))
		go_io_error_display (io_context);

	g_object_unref (io_context);
	go_cmd_context_set_sensitive (GO_CMD_CONTEXT (wbcg), TRUE);

	if (wbv != NULL) {
		Workbook *wb = wb_view_get_workbook (wbv);
		workbook_set_saveinfo (wb, GO_FILE_FL_NEW, NULL);
		gui_wb_view_show (wbcg, wbv);
		return TRUE;
	}
	return FALSE;
}

static void
file_opener_format_changed_cb (GtkComboBox *format_combo,
			       file_opener_format_changed_cb_data *data)
{
	GOFileOpener *fo = g_list_nth_data (data->openers,
		gtk_combo_box_get_active (format_combo));
	gboolean is_sensitive = fo != NULL && go_file_opener_is_encoding_dependent (fo);

	gtk_widget_set_sensitive (GTK_WIDGET (data->go_charmap_sel), is_sensitive);
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
		if (GO_IS_FILE_OPENER (l->data) &&
		    strcmp (id, go_file_opener_get_id(l->data)) == 0)
			return i;
	}

	return 0;
}

static void
cb_advanced_clicked (GtkButton *advanced, GtkFileChooser *fsel)
{
	GtkWidget *extra = g_object_get_data (G_OBJECT (advanced), "extra");

	gtk_button_set_use_underline (advanced, TRUE);
	if (gtk_file_chooser_get_extra_widget (fsel)) {
		 /* xgettext: If possible try to use the same mnemonic for
		  * Advanced and Simple */
		gtk_button_set_label (advanced, _("Advanc_ed"));
		gtk_file_chooser_set_extra_widget (fsel, NULL);
	} else {
		gtk_button_set_label (advanced, _("Simpl_e"));
		gtk_file_chooser_set_extra_widget (fsel, extra);
	}
}

/*
 * Suggests automatic file type recognition, but lets the user choose an
 * import filter for selected file.
 */
void
gui_file_open (WBCGtk *wbcg, GnmFileOpenStyle type, char const *default_format)
{
	GList *openers = NULL, *all_openers, *l;
	GtkFileChooser *fsel;
	GtkWidget *advanced_button;
	GtkComboBox *format_combo;
	GtkWidget *go_charmap_sel;
	file_opener_format_changed_cb_data data;
	gint opener_default;
	char const *title = NULL;
	GSList *uris = NULL;
	char const *encoding = NULL;
	GOFileOpener *fo = NULL;
	Workbook *workbook = wb_control_get_workbook (GNM_WBC (wbcg));

	all_openers = go_get_file_openers ();

	if (default_format != NULL) {
		fo = go_file_opener_for_id (default_format);
	}

	if (fo != NULL)
		openers = g_list_prepend (NULL, fo);
	else {
		for (l = all_openers; l; l = l->next)
			if (l->data != NULL) {
				GOFileOpener *fo = l->data;
				GSList const *mimes = go_file_opener_get_mimes (fo);
				GSList *fsavers = NULL, *fl;

				for (; mimes; mimes = mimes->next) {
					GOFileSaver *fs = go_file_saver_for_mime_type
						(mimes->data);
					if (fs != NULL)
						fsavers = g_slist_prepend (fsavers, fs);
				}
				switch (type) {
				case GNM_FILE_OPEN_STYLE_OPEN:
					for (fl = fsavers; fl; fl = fl->next) {
						GOFileSaver *fs = GO_FILE_SAVER (fl->data);
						if ((go_file_saver_get_save_scope (fs)
						     != GO_FILE_SAVE_RANGE) &&
						    (go_file_saver_get_format_level (fs)
						     == GO_FILE_FL_AUTO)) {
							openers = g_list_prepend
								(openers, fo);
							break;
						}
					}
					break;
				case GNM_FILE_OPEN_STYLE_IMPORT:
					{
						gboolean is_open = FALSE;
						for (fl = fsavers; fl; fl = fl->next) {
							GOFileSaver *fs = GO_FILE_SAVER
								(fl->data);
							if ((go_file_saver_get_save_scope
							     (fs)
							     != GO_FILE_SAVE_RANGE) &&
							    (go_file_saver_get_format_level
							     (fs)
							     == GO_FILE_FL_AUTO)) {
								is_open = TRUE;
								break;
							}
						}
						if (!(is_open))
							openers = g_list_prepend
								(openers, fo);
						break;
					}
				}
				g_slist_free (fsavers);
			}
		openers = g_list_sort (openers, file_opener_description_cmp);
		/* NULL represents automatic file type recognition */
		openers = g_list_prepend (openers, NULL);
	}

	opener_default = file_opener_find_by_id (openers, default_format);

	if (opener_default != 0)
		title = (go_file_opener_get_description
			 (g_list_nth_data (openers, opener_default)));
	if (title == NULL)
		switch (type) {
		case GNM_FILE_OPEN_STYLE_OPEN:
			title = _("Open Spreadsheet File");
			break;
		case GNM_FILE_OPEN_STYLE_IMPORT:
			title = _("Import Data File");
			break;
		}
	data.openers = openers;

	/* Make charmap chooser */
	go_charmap_sel = go_charmap_sel_new (GO_CHARMAP_SEL_TO_UTF8);
	data.go_charmap_sel = GO_CHARMAP_SEL(go_charmap_sel);
	data.charmap_label = gtk_label_new_with_mnemonic (_("Character _encoding:"));

	/* Make format chooser */
	format_combo = GTK_COMBO_BOX (gtk_combo_box_text_new ());
	make_format_chooser (openers, format_combo);
	g_signal_connect (G_OBJECT (format_combo), "changed",
			  G_CALLBACK (file_opener_format_changed_cb), &data);
	gtk_combo_box_set_active (format_combo, opener_default);
	gtk_widget_set_sensitive (GTK_WIDGET (format_combo), opener_default == 0);
	file_opener_format_changed_cb (format_combo, &data);

	fsel = GTK_FILE_CHOOSER
		(g_object_new (GTK_TYPE_FILE_CHOOSER_DIALOG,
			       "action", GTK_FILE_CHOOSER_ACTION_OPEN,
			       "local-only", FALSE,
			       "title", title,
			       "select-multiple", TRUE,
			       NULL));

	advanced_button = gtk_button_new_with_mnemonic (_("Advanc_ed"));
	gtk_widget_show (advanced_button);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_action_area (GTK_DIALOG (fsel))),
			    advanced_button, FALSE, TRUE, 6);
	gtk_dialog_add_buttons (GTK_DIALOG (fsel),
				GNM_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GNM_STOCK_OPEN, GTK_RESPONSE_OK,
				NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (fsel), GTK_RESPONSE_OK);

	/* Add Templates bookmark */
	{
		char *templates = g_build_filename (gnm_sys_data_dir (), "templates", NULL);
		gtk_file_chooser_add_shortcut_folder (fsel, templates, NULL);
		g_free (templates);
	}

	/* Start in the same directory as the current workbook.  */
	gtk_file_chooser_select_uri (fsel, go_doc_get_uri (GO_DOC (workbook)));
	gtk_file_chooser_unselect_all (fsel);

	/* Filters */
	{
		GtkFileFilter *filter;
		char const *filter_name = NULL;

		filter = gtk_file_filter_new ();
		gtk_file_filter_set_name (filter, _("All Files"));
		gtk_file_filter_add_pattern (filter, "*");
		gtk_file_chooser_add_filter (fsel, filter);

		filter = gnm_app_create_opener_filter (openers);
		if (default_format != NULL) {
			if (0 == strcmp (default_format,
					 "Gnumeric_stf:stf_assistant"))
				filter_name = _("Text Files");
		}
		if (filter_name == NULL)
			switch (type) {
			case GNM_FILE_OPEN_STYLE_OPEN:
				filter_name = _("Spreadsheets");
				break;
			case GNM_FILE_OPEN_STYLE_IMPORT:
				filter_name = _("Data Files");
				break;
			}
		gtk_file_filter_set_name (filter, filter_name);
		gtk_file_chooser_add_filter (fsel, filter);
		/* Make this filter the default */
		gtk_file_chooser_set_filter (fsel, filter);
	}

	{
		GtkWidget *label;
		GtkWidget *grid = gtk_grid_new ();

		g_object_set (grid,
		              "column-spacing", 12,
		              "row-spacing", 6,
		              NULL);
		gtk_widget_set_hexpand (GTK_WIDGET (format_combo), TRUE);
		gtk_grid_attach (GTK_GRID (grid),
		                 GTK_WIDGET (format_combo),
				 1, 0, 1, 1);
		label = gtk_label_new_with_mnemonic (_("File _type:"));
		gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (format_combo));

		gtk_widget_set_hexpand (go_charmap_sel, TRUE);
		gtk_grid_attach (GTK_GRID (grid), go_charmap_sel, 1, 1, 1, 1);
		gtk_grid_attach (GTK_GRID (grid), data.charmap_label, 0, 1, 1, 1);
		gtk_label_set_mnemonic_widget (GTK_LABEL (data.charmap_label),
					       go_charmap_sel);

		g_object_ref_sink (grid);
		g_object_set_data_full (G_OBJECT (advanced_button), "extra",
					grid, g_object_unref);
		gtk_widget_show_all (grid);
		g_signal_connect (G_OBJECT (advanced_button),
				  "clicked",
				  G_CALLBACK (cb_advanced_clicked),
				  fsel);
	}

	/* Show file selector */
	if (!go_gtk_file_sel_dialog (wbcg_toplevel (wbcg), GTK_WIDGET (fsel)))
		goto out;

	uris = gtk_file_chooser_get_uris (fsel);
	encoding = go_charmap_sel_get_encoding (GO_CHARMAP_SEL (go_charmap_sel));
	fo = g_list_nth_data (openers, gtk_combo_box_get_active (format_combo));

 out:
	gtk_widget_destroy (GTK_WIDGET (fsel));
	g_list_free (openers);

	while (uris) {
		char *uri = uris->data;
		GSList *hook = uris;

		/* Make sure dialog goes away right now.  */
		while (g_main_context_iteration (NULL, FALSE));

		gui_file_read (wbcg, uri, fo, encoding);
		g_free (uri);

		uris = uris->next;
		g_slist_free_1 (hook);
	}
}

static gboolean
check_multiple_sheet_support_if_needed (GOFileSaver *fs,
					GtkWindow *parent,
					WorkbookView *wb_view)
{
	gboolean ret_val = TRUE;

	if (go_file_saver_get_save_scope (fs) != GO_FILE_SAVE_WORKBOOK &&
	    gnm_conf_get_core_file_save_single_sheet ()) {
		Workbook *wb = wb_view_get_workbook (wb_view);
		const char *msg =
			_("Selected file format doesn't support "
			  "saving multiple sheets in one file.\n"
			  "If you want to save all sheets, save them "
			  "in separate files or select different file format.\n"
			  "Do you want to save only current sheet?");
		if (workbook_sheet_count (wb) > 1) {
			ret_val = go_gtk_query_yes_no (parent, TRUE, "%s", msg);
		}
	}
	return (ret_val);
}

static gboolean
extension_check_disabled (GOFileSaver *fs)
{
	GSList *list = gnm_conf_get_core_file_save_extension_check_disabled ();
	char const *id = go_file_saver_get_id (fs);

	return (NULL != g_slist_find_custom (list, id, go_str_compare));
}

typedef struct {
	GtkFileChooser *fsel;
	GList *savers;
} file_saver_format_changed_cb_data;

/*
 * Change or add the extension for the newly chosen saver to the file
 * name in the file chooser.
 */
static void
file_saver_format_changed_cb (GtkComboBox *format_combo,
			      file_saver_format_changed_cb_data *data)
{
	GOFileSaver *fs = g_list_nth_data (data->savers, gtk_combo_box_get_active (format_combo));
	char *uri = gtk_file_chooser_get_uri (data->fsel);
	char *basename = NULL, *newname = NULL, *dot;
	char const *ext = go_file_saver_get_extension (fs);

	if (!uri || !ext)
		goto out;

	basename = go_basename_from_uri (uri);
	if (!basename)
		goto out;

	dot = strchr (basename, '.');
	if (dot)
		*dot = 0;

	newname = g_strconcat (basename, ".", ext, NULL);
	gtk_file_chooser_set_current_name (data->fsel, newname);

out:
	g_free (uri);
	g_free (basename);
	g_free (newname);
}

gboolean
gui_file_save_as (WBCGtk *wbcg, WorkbookView *wb_view, GnmFileSaveAsStyle type,
		  char const *default_format, gboolean from_save)
{
	GList *savers = NULL, *l;
	GtkFileChooser *fsel;
	GtkComboBox *format_combo;
	GOFileSaver *fs;
	file_saver_format_changed_cb_data data;
	gboolean success  = FALSE;
	gchar const *wb_uri;
	char *uri;
	Workbook *wb;
	WBCGtk *wbcg2;
	char const *title = (type == GNM_FILE_SAVE_AS_STYLE_SAVE) ? _("Save the current workbook as")
		: _("Export the current workbook or sheet to");

	g_return_val_if_fail (wbcg != NULL, FALSE);

	wb = wb_view_get_workbook (wb_view);
	wbcg2 = wbcg_find_for_workbook (wb, wbcg, NULL, NULL);

	for (l = go_get_file_savers (); l; l = l->next)
		switch (type) {
		case GNM_FILE_SAVE_AS_STYLE_SAVE:
			if ((l->data == NULL) ||
			    ((go_file_saver_get_save_scope (GO_FILE_SAVER (l->data))
			      != GO_FILE_SAVE_RANGE) &&
			     (go_file_saver_get_format_level (GO_FILE_SAVER (l->data))
			      == GO_FILE_FL_AUTO)))
				savers = g_list_prepend (savers, l->data);
			break;
		case GNM_FILE_SAVE_AS_STYLE_EXPORT:
		default:
			if ((l->data == NULL) ||
			    ((go_file_saver_get_save_scope (GO_FILE_SAVER (l->data))
			      != GO_FILE_SAVE_RANGE) &&
			     (go_file_saver_get_format_level (GO_FILE_SAVER (l->data))
			      != GO_FILE_FL_AUTO)))
				savers = g_list_prepend (savers, l->data);
			break;
	}
	data.savers = savers = g_list_sort (savers, file_saver_description_cmp);

	data.fsel = fsel = GTK_FILE_CHOOSER
		(g_object_new (GTK_TYPE_FILE_CHOOSER_DIALOG,
			       "action", GTK_FILE_CHOOSER_ACTION_SAVE,
			       "local-only", FALSE,
			       "title", title,
			       NULL));
	gtk_dialog_add_buttons (GTK_DIALOG (fsel),
				GNM_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GNM_STOCK_SAVE, GTK_RESPONSE_OK,
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
		for (l = savers; l; l = l->next) {
			GOFileSaver *fs = l->data;
			char const *ext = go_file_saver_get_extension (fs);
			char const *mime = go_file_saver_get_mime_type (fs);

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
		GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
		GtkWidget *label = gtk_label_new_with_mnemonic (_("File _type:"));
		format_combo = GTK_COMBO_BOX (gtk_combo_box_text_new ());
		make_format_chooser (savers, format_combo);
		g_signal_connect (G_OBJECT (format_combo), "changed",
				  G_CALLBACK (file_saver_format_changed_cb), &data);

		gtk_box_pack_start (GTK_BOX (box), label, FALSE, TRUE, 6);
		gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (format_combo), FALSE, TRUE, 6);
		gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (format_combo));

		gtk_widget_show_all (box);
		gtk_file_chooser_set_extra_widget (fsel, box);
	}

	/* Set default file saver */
	if (type == GNM_FILE_SAVE_AS_STYLE_SAVE) {
		fs = workbook_get_file_saver (wb);
		if (!fs || g_list_find (savers, fs) == NULL)
			fs = go_file_saver_get_default ();
	} else {
		if (default_format)
			fs = go_file_saver_for_id (default_format);
		else
			fs = workbook_get_file_exporter (wb);
		if (!fs || g_list_find (savers, fs) == NULL)
			fs = go_file_saver_for_id ("Gnumeric_html:latex_table");
	}

	gtk_combo_box_set_active (format_combo, g_list_index (savers, fs));

	/* Set default file name */
	if (type == GNM_FILE_SAVE_AS_STYLE_EXPORT) {
		char *basename, *dot, *newname;
		char const *ext = go_file_saver_get_extension (fs);

		wb_uri = workbook_get_last_export_uri (wb);
		if (!wb_uri || fs !=  workbook_get_file_exporter (wb))
			wb_uri = go_doc_get_uri (GO_DOC (wb));
		if (!wb_uri) wb_uri = _("Untitled");
		if (!ext) ext = "txt";

		basename = go_basename_from_uri (wb_uri);
		dot = strrchr (basename, '.');
		if (dot) *dot = 0;
		newname = g_strconcat (basename, ".", ext, NULL);

		gtk_file_chooser_set_uri (fsel, wb_uri);
		gtk_file_chooser_set_current_name (fsel, newname);

		g_free (basename);
		g_free (newname);
	} else {
		char *basename;
		char *wb_uri2 = NULL;
		const char *ext;

		wb_uri = go_doc_get_uri (GO_DOC (wb));
		ext = go_file_saver_get_extension (fs);
		if (wb_uri && ext && from_save &&
		    !go_url_check_extension (wb_uri, ext, NULL)) {
			// We came from plain save with a format we cannot
			// save.  Adjust the extension to match what we
			// are defaulting to save
			const char *rdot = strrchr (wb_uri, '.');
			if (rdot)
				wb_uri = wb_uri2 = g_strdup_printf
					("%-.*s.%s",
					 (int)(rdot - wb_uri),
					 wb_uri,
					 ext);
		}
		if (!wb_uri) wb_uri = _("Untitled");
		basename = go_basename_from_uri (wb_uri);

		/*
		 * If the file exists, the following is dominated by the
		 * final set_uri.  If the file does not exist, we get the
		 * directory from the first set_uri and the basename set
		 * with set_current_name.
		 */
		gtk_file_chooser_set_uri (fsel, wb_uri);
		gtk_file_chooser_set_current_name (fsel, basename);
		gtk_file_chooser_set_uri (fsel, wb_uri);

		g_free (basename);
		g_free (wb_uri2);
	}

	while (1) {
		char *uri2 = NULL;

		/* Show file selector */
		if (!go_gtk_file_sel_dialog (wbcg_toplevel (wbcg), GTK_WIDGET (fsel)))
			goto out;
		fs = g_list_nth_data (savers, gtk_combo_box_get_active (format_combo));
		if (!fs)
			goto out;
		uri = gtk_file_chooser_get_uri (fsel);
		if (!go_url_check_extension (uri,
					     go_file_saver_get_extension (fs),
					     &uri2) &&
		    !extension_check_disabled (fs) &&
		    !go_gtk_query_yes_no (GTK_WINDOW (fsel),
					  TRUE,
					  _("The given file extension does not match the"
					    " chosen file type. Do you want to use this name"
					    " anyway?"))) {
			g_free (uri);
			g_free (uri2);
			uri = NULL;
			continue;
		}

		g_free (uri);
		uri = uri2;

		if (go_gtk_url_is_writeable (GTK_WINDOW (fsel), uri,
					     gnm_conf_get_core_file_save_def_overwrite ()))
			break;

		g_free (uri);
	}

	if (wbcg2) {
		GtkWidget *nb = GTK_WIDGET (wbcg2->notebook_area);
		GtkAllocation a;
		gtk_widget_get_allocation (nb, &a);
		wb_view_preferred_size (wb_view, a.width, a.height);
        }

	success = check_multiple_sheet_support_if_needed (fs, GTK_WINDOW (fsel), wb_view);
	if (success) {
		/* Destroy early so no-one can repress the Save button.  */
		gtk_widget_destroy (GTK_WIDGET (fsel));
		fsel = NULL;
		if (workbook_view_save_as (wb_view, fs, uri, GO_CMD_CONTEXT (wbcg)))
			workbook_update_history (wb, type);
	}

	g_free (uri);

 out:
	if (fsel)
		gtk_widget_destroy (GTK_WIDGET (fsel));
	g_list_free (savers);

	return success;
}

static gboolean
warn_about_overwrite (WBCGtk *wbcg,
		      GDateTime *modtime,
		      GDateTime *known_modtime)
{
	GtkWidget *dialog;
	int response;
	char *shortname, *filename, *longname, *duri, *modtxt;
	Workbook *wb = wb_control_get_workbook (GNM_WBC (wbcg));
	const char *uri;
	GDateTime *modtime_local;

	uri = go_doc_get_uri (GO_DOC (wb));
	filename = go_filename_from_uri (uri);
	if (filename) {
		shortname = g_filename_display_basename (filename);
	} else {
		shortname = g_filename_display_basename (uri);
	}

	duri = g_uri_unescape_string (uri, NULL);
	longname = duri
		? g_filename_display_name (duri)
		: g_strdup (uri);

	modtime_local = g_date_time_to_local (modtime);
	modtxt = g_date_time_format (modtime_local, _("%F %T"));
	g_date_time_unref (modtime_local);

	dialog = gtk_message_dialog_new_with_markup
		(wbcg_toplevel (wbcg),
		 GTK_DIALOG_DESTROY_WITH_PARENT,
		 GTK_MESSAGE_WARNING,
		 GTK_BUTTONS_NONE,
		 _("The file you are about to save has changed on disk. If you continue, you will overwrite someone else's changes.\n\n"
		   "File: <b>%s</b>\n"
		   "Location: %s\n\n"
		   "Last modified: <b>%s</b>\n"),
		 shortname, longname, modtxt);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
				_("Overwrite"), GTK_RESPONSE_YES,
				_("Cancel"), GTK_RESPONSE_NO,
				NULL);
	g_free (shortname);
	g_free (longname);
	g_free (duri);
	g_free (filename);
	g_free (modtxt);

	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_NO);
	response = go_gtk_dialog_run (GTK_DIALOG (dialog),
				      wbcg_toplevel (wbcg));
	return response == GTK_RESPONSE_YES;
}

gboolean
gui_file_save (WBCGtk *wbcg, WorkbookView *wb_view)
{
	Workbook *wb = wb_view_get_workbook (wb_view);
	WBCGtk *wbcg2 =
		wbcg_find_for_workbook (wb, wbcg, NULL, NULL);

	if (wbcg2) {
		GtkWidget *nb = GTK_WIDGET (wbcg2->notebook_area);
		GtkAllocation a;
		gtk_widget_get_allocation (nb, &a);
		wb_view_preferred_size (wb_view, a.width, a.height);
	}

	if (wb->file_format_level < GO_FILE_FL_AUTO)
		return gui_file_save_as (wbcg, wb_view,
					 GNM_FILE_SAVE_AS_STYLE_SAVE, NULL,
					 TRUE);
	else {
		gboolean ok = TRUE;
		const char *uri = go_doc_get_uri (GO_DOC (wb));
		GDateTime *known_modtime = go_doc_get_modtime (GO_DOC (wb));
		GDateTime *modtime = go_file_get_modtime (uri);
		gboolean debug_modtime = gnm_debug_flag ("modtime");

		/* We need a ref because a Ctrl-Q at the wrong time will
		   cause the workbook to disappear at the end of the
		   save.  */
		g_object_ref (wb);

		if (modtime && known_modtime) {
			if (g_date_time_equal (known_modtime, modtime)) {
				if (debug_modtime)
					g_printerr ("Modtime match\n");
			} else {
				if (debug_modtime)
					g_printerr ("Modtime mismatch\n");
				ok = warn_about_overwrite (wbcg, modtime, known_modtime);
			}
		}

		if (ok)
			ok = workbook_view_save (wb_view, GO_CMD_CONTEXT (wbcg));
		if (ok)
			workbook_update_history (wb, GNM_FILE_SAVE_AS_STYLE_SAVE);
		g_object_unref (wb);

		if (modtime)
			g_date_time_unref (modtime);

		return ok;
	}
}

gboolean
gui_file_export_repeat (WBCGtk *wbcg)
{
	WorkbookView *wb_view = wb_control_view (GNM_WBC (wbcg));
	Workbook *wb = wb_view_get_workbook (wb_view);
	GOFileSaver *fs = workbook_get_file_exporter (wb);
	gchar const *last_uri = workbook_get_last_export_uri (wb);

	if (fs != NULL && last_uri != NULL) {
		char const *msg;
		GtkWidget *dialog;

		if (go_file_saver_get_save_scope (fs) != GO_FILE_SAVE_WORKBOOK)
			msg = _("Do you want to export the <b>current sheet</b> of this "
				"workbook to the location '<b>%s</b>' "
				"using the '<b>%s</b>' exporter?");
		else
			msg = _("Do you want to export this workbook to the "
				"location '<b>%s</b>' "
				"using the '<b>%s</b>' exporter?");

		/* go_gtk_query_yes_no does not handle markup ... */
		dialog = gtk_message_dialog_new_with_markup (wbcg_toplevel (wbcg),
							     GTK_DIALOG_DESTROY_WITH_PARENT,
							     GTK_MESSAGE_QUESTION,
							     GTK_BUTTONS_YES_NO,
							     msg, last_uri,
							     go_file_saver_get_description (fs));
		gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

		if (GTK_RESPONSE_YES ==
		    go_gtk_dialog_run (GTK_DIALOG (dialog), wbcg_toplevel (wbcg))) {
			/* We need to copy wb->last_export_uri since it will be reset during saving */
			gchar *uri = g_strdup (last_uri);
			if(workbook_view_save_as (wb_view, fs, uri, GO_CMD_CONTEXT (wbcg))) {
				workbook_update_history (wb, GNM_FILE_SAVE_AS_STYLE_EXPORT);
				g_free (uri);
				return TRUE;
			}
			g_free (uri);
		}
		return FALSE;
	} else {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				      _("Unable to repeat export since no previous "
					"export information has been saved in this "
					"session."));
		return FALSE;
	}
}
