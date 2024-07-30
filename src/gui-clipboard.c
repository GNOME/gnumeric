/*
 * gui-clipboard.c: Implements the X11 based copy/paste operations
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gui-clipboard.h>

#include <gui-util.h>
#include <clipboard.h>
#include <command-context-stderr.h>
#include <selection.h>
#include <application.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <workbook-priv.h>
#include <workbook.h>
#include <workbook-view.h>
#include <ranges.h>
#include <sheet.h>
#include <sheet-style.h>
#include <sheet-object.h>
#include <sheet-control-gui.h>
#include <sheet-view.h>
#include <commands.h>
#include <value.h>
#include <number-match.h>
#include <dialogs/dialog-stf.h>
#include <stf-parse.h>
#include <mstyle.h>
#include <gnm-format.h>
#include <gnumeric-conf.h>
#include <xml-sax.h>
#include <gutils.h>

#include <goffice/goffice.h>
#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-output-memory.h>
#include <gsf/gsf-utils.h>
#include <glib/gi18n-lib.h>
#include <libxml/globals.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>

#define APP_CLIP_DISP_KEY "clipboard-displays"

#define EXCEL_FILE_OPENER "Gnumeric_Excel:excel"
#define EXCEL_FILE_SAVER "Gnumeric_Excel:excel_biff8"
#define HTML_FILE_OPENER "Gnumeric_html:html"
#define HTML_FILE_SAVER "Gnumeric_html:xhtml_range"
#define OOO_FILE_OPENER "Gnumeric_OpenCalc:openoffice"

// ----------------------------------------------------------------------------

static gboolean debug_clipboard;
static gboolean debug_clipboard_dump;
static gboolean debug_clipboard_undump;

// ----------------------------------------------------------------------------

enum {
	ATOM_GNUMERIC,
	ATOM_GOFFICE_GRAPH,
	// ----------
	ATOM_UTF8_STRING,
	ATOM_TEXT_PLAIN_UTF8,
	ATOM_STRING,
	ATOM_COMPOUND_TEXT,
	ATOM_TEXT_HTML,
	ATOM_TEXT_HTML_WINDOWS,
	// ----------
	ATOM_BIFF8,
	ATOM_BIFF8_OO,
	ATOM_BIFF8_CITRIX,
	ATOM_BIFF5,
	ATOM_BIFF,
	// ----------
	ATOM_OOO,
	ATOM_OOO_WINDOWS,
	ATOM_OOO11,
	// ----------
	ATOM_IMAGE_SVGXML,
	ATOM_IMAGE_XWMF,
	ATOM_IMAGE_XEMF,
	ATOM_IMAGE_PNG,
	ATOM_IMAGE_JPEG,
	ATOM_IMAGE_BMP,
	// ----------
	ATOM_TEXT_URI_LIST,
	ATOM_GNOME_COPIED_FILES,
	ATOM_KDE_CUT_FILES,
	// ----------
	ATOM_SAVE_TARGETS,
};

static const char *const atom_names[] = {
	"application/x-gnumeric",
	"application/x-goffice-graph",
	// ----------
	"UTF8_STRING",
	"text/plain;charset=utf-8",
	"STRING",
	"COMPOUND_TEXT",
	"text/html",
	"HTML Format",
	// ----------
	"Biff8",
	"application/x-openoffice-biff-8;windows_formatname=\"Biff8\"",
	"_CITRIX_Biff8",
	"Biff5",
	"Biff",
	// ----------
	"application/x-openoffice;windows_formatname=\"Star Embed Source (XML)\"",
	"Star Embed Source (XML)",
	"application/x-openoffice-embed-source-xml;windows_formatname=\"Star Embed Source (XML)\"",
	// ----------
	"image/svg+xml",
	"image/x-wmf",
	"image/x-emf",
	"image/png",
	"image/jpeg",
	"image/bmp",
	// ----------
	"text/uri-list",
	"x-special/gnome-copied-files",
	"application/x-kde-cutselection",
	// ----------
	"SAVE_TARGETS",
};

static GdkAtom atoms[G_N_ELEMENTS(atom_names)];

typedef enum {
	INFO_UNKNOWN,
	INFO_GNUMERIC,
	INFO_EXCEL,
	INFO_OOO,
	INFO_GENERIC_TEXT,
	INFO_HTML,
	INFO_OBJECT,
	INFO_IMAGE,
} AtomInfoType;

static GtkTargetList *generic_text_targets;
static GtkTargetList *image_targets;

// ----------------------------------------------------------------------------

typedef struct {
	WBCGtk *wbcg;
	GnmPasteTarget *paste_target;
} GnmGtkClipboardCtxt;

static void
gnm_gtk_clipboard_context_free (GnmGtkClipboardCtxt *ctxt)
{
	g_free (ctxt->paste_target);
	g_free (ctxt);
}

/*
 * Emacs hack:
 * (x-get-selection-internal 'CLIPBOARD 'TARGETS)
 */

static gboolean
has_file_opener (const char *id)
{
	return go_file_opener_for_id (id) != NULL;
}

static gboolean
has_file_saver (const char *id)
{
	return go_file_saver_for_id (id) != NULL;
}



static void
paste_from_gnumeric (GtkSelectionData *selection_data, GdkAtom target,
		     gconstpointer data, gssize size)
{
	if (size < 0)
		size = 0;

	if (debug_clipboard_dump) {
		g_file_set_contents ("paste-from-gnumeric.dat",
				     data, size, NULL);
	}

	if (debug_clipboard) {
		char *target_name = gdk_atom_name (target);
		g_printerr ("clipboard %s of %d bytes\n",
			    target_name, (int)size);
		g_free (target_name);
	}

	gtk_selection_data_set (selection_data, target, 8, data, size);
}

static void
paste_to_gnumeric (GtkSelectionData *sel, const char *typ)
{
	GdkAtom target = gtk_selection_data_get_target (sel);
	gconstpointer buffer = gtk_selection_data_get_data (sel);
	int sel_len = gtk_selection_data_get_length (sel);

	if (sel_len < 0)
		sel_len = 0;

	if (debug_clipboard) {
		int maxlen = 1024;
		char *name = gdk_atom_name (target);
		g_printerr ("Received %d bytes of %s for target %s\n",
			    sel_len, typ, name);
		g_free (name);
		if (sel_len > 0) {
			gsf_mem_dump (buffer, MIN (sel_len, maxlen));
			if (sel_len > maxlen)
				g_printerr ("...\n");
		}
	}

	if (debug_clipboard_dump) {
		g_file_set_contents ("paste-to-gnumeric.dat",
				     buffer, sel_len, NULL);
	}
}


/* See if this is a "single line + line end", a "multiline" or a "tab separated"
 * string. If this is _not_ the case we won't invoke the STF, it is
 * unlikely that the user will actually need it in this case. */
static gboolean
text_is_single_cell (gchar const *data, int data_len)
{
	int i;

	for (i = 0; i < data_len; i++)
		if (data[i] == '\n' || data[i] == '\t')
			return FALSE;
	return TRUE;
}


static GnmCellRegion *
text_to_cell_region (WBCGtk *wbcg,
		     gchar const *data, int data_len,
		     char const *opt_encoding,
		     gboolean fixed_encoding)
{
	Workbook *wb = wb_control_get_workbook (GNM_WBC (wbcg));
	DialogStfResult_t *dialogresult;
	GnmCellRegion *cr = NULL;
	gboolean oneline;
	char *data_converted = NULL;

	if (!data) {
		/*
		 * See Redhat #1160975.
		 *
		 * I'm unsure why someone gets NULL here, but this is better
		 * than a crash.
		 */
		data = "";
		data_len = 0;
	}

	oneline = text_is_single_cell (data, data_len);

	if (oneline && (opt_encoding == NULL || strcmp (opt_encoding, "UTF-8") != 0)) {
		size_t bytes_written;
		char const *enc = opt_encoding ? opt_encoding : "ASCII";

		data_converted = g_convert (data, data_len,
					    "UTF-8", enc,
					    NULL, &bytes_written, NULL);
		if (data_converted) {
			data = data_converted;
			data_len = bytes_written;
		} else {
			/* Force STF import since we don't know the charset.  */
			oneline = FALSE;
			fixed_encoding = FALSE;
		}
	}

	if (oneline) {
		GODateConventions const *date_conv = workbook_date_conv (wb);
		GnmCellCopy *cc = gnm_cell_copy_new (
			(cr = gnm_cell_region_new (NULL)), 0, 0);
		char *tmp = g_strndup (data, data_len);

		g_free (data_converted);

		cc->val = format_match (tmp, NULL, date_conv);
		if (cc->val)
			g_free (tmp);
		else
			cc->val = value_new_string_nocopy (tmp);
		cc->texpr = NULL;

		cr->cols = cr->rows = 1;
	} else {
		dialogresult = stf_dialog (wbcg, opt_encoding, fixed_encoding,
					   NULL, FALSE,
					   _("clipboard"), data, data_len);

		if (dialogresult != NULL) {
			cr = stf_parse_region (dialogresult->parseoptions,
					       dialogresult->text, NULL, wb);
			g_return_val_if_fail (cr != NULL, gnm_cell_region_new (NULL));

			stf_dialog_result_attach_formats_to_cr (dialogresult, cr);

			stf_dialog_result_free (dialogresult);
		} else
			cr = gnm_cell_region_new (NULL);
	}

	return cr;
}

static void
text_content_received (GtkClipboard *clipboard, GtkSelectionData *sel,
		       gpointer closure)
{
	GnmGtkClipboardCtxt *ctxt = closure;
	WBCGtk *wbcg = ctxt->wbcg;
	WorkbookControl	   *wbc  = GNM_WBC (wbcg);
	GnmPasteTarget	   *pt   = ctxt->paste_target;
	GnmCellRegion *content = NULL;
	GdkAtom target = gtk_selection_data_get_target (sel);
	int sel_len = gtk_selection_data_get_length (sel);

	paste_to_gnumeric (sel, "text");

	/* Nothing on clipboard? */
	if (sel_len < 0) {
		;
	} else if (target == atoms[ATOM_UTF8_STRING] ||
		   target == atoms[ATOM_TEXT_PLAIN_UTF8]) {
		content = text_to_cell_region (wbcg, (const char *)gtk_selection_data_get_data (sel),
					       sel_len, "UTF-8", TRUE);
	} else if (target == atoms[ATOM_COMPOUND_TEXT]) {
		/* COMPOUND_TEXT is icky.  Just let GTK+ do the work.  */
		char *data_utf8 = (char *)gtk_selection_data_get_text (sel);
		content = text_to_cell_region (wbcg, data_utf8, strlen (data_utf8), "UTF-8", TRUE);
		g_free (data_utf8);
	} else if (target == atoms[ATOM_STRING]) {
		char const *locale_encoding;
		g_get_charset (&locale_encoding);

		content = text_to_cell_region (wbcg, (const char *)gtk_selection_data_get_data (sel),
					       sel_len, locale_encoding, FALSE);
	}
	if (content) {
		/*
		 * if the conversion from the X selection -> a cellregion
		 * was canceled this may have content sized -1,-1
		 */
		if (content->cols > 0 && content->rows > 0)
			cmd_paste_copy (wbc, pt, content);

		/* Release the resources we used */
		cellregion_unref (content);
	}

	gnm_gtk_clipboard_context_free (ctxt);
}

static void
utf8_content_received (GtkClipboard *clipboard, const gchar *text,
		       gpointer closure)
{
	GnmGtkClipboardCtxt *ctxt = closure;
	WBCGtk *wbcg = ctxt->wbcg;
	WorkbookControl	   *wbc  = GNM_WBC (wbcg);
	GnmPasteTarget	   *pt   = ctxt->paste_target;
	GnmCellRegion *content = NULL;

	/* Nothing on clipboard? */
	if (!text || *text == 0) {
		;
	} else {
		content = text_to_cell_region (wbcg, text, strlen(text), "UTF-8", TRUE);
	}
	if (content) {
		/*
		 * if the conversion from the X selection -> a cellregion
		 * was canceled this may have content sized -1,-1
		 */
		if (content->cols > 0 && content->rows > 0)
			cmd_paste_copy (wbc, pt, content);

		/* Release the resources we used */
		cellregion_unref (content);
	}

	gnm_gtk_clipboard_context_free (ctxt);
}

/*
 * Use the file_opener plugin service to read into a temporary workbook, in
 * order to copy from it to the paste target. A temporary sheet would do just
 * as well, but the file_opener service makes workbooks, not sheets.
 *
 * We use the file_opener service by wrapping the selection data in a GsfInput,
 * and calling workbook_view_new_from_input.
 **/
static GnmCellRegion *
table_cellregion_read (WorkbookControl *wbc, char const *reader_id,
		       GnmPasteTarget *pt, const guchar *buffer, int length)
{
	WorkbookView *wb_view = NULL;
	Workbook *wb = NULL;
	GnmCellRegion *ret = NULL;
	const GOFileOpener *reader = go_file_opener_for_id (reader_id);
	GOIOContext *ioc;
	GsfInput *input;

	if (!reader) {
		// Likely cause: plugin not loaded
		g_warning ("No file opener for %s", reader_id);
		return NULL;
	}

	ioc = go_io_context_new (GO_CMD_CONTEXT (wbc));
	input = gsf_input_memory_new (buffer, length, FALSE);
	wb_view = workbook_view_new_from_input  (input, NULL, reader, ioc, NULL);
	if (go_io_error_occurred (ioc) || wb_view == NULL) {
		go_io_error_display (ioc);
		goto out;
	}

	wb = wb_view_get_workbook (wb_view);
	if (workbook_sheet_count (wb) > 0) {
		GnmRange r;
		Sheet *tmpsheet = workbook_sheet_by_index (wb, 0);
		GnmRange *rp = g_object_get_data (G_OBJECT (tmpsheet),
						  "DIMENSION");
		if (rp) {
			r = *rp;
		} else {
			// File format didn't tell us the range being
			// pasted.  Looking at you, LibreOffice!
			// Make a guess.

			GnmRange fullr;
			GPtrArray *col_defaults =
				sheet_style_most_common (tmpsheet, TRUE);

			range_init_full_sheet (&fullr, tmpsheet);

			r = sheet_get_cells_extent (tmpsheet);
			sheet_style_get_nondefault_extent
				(tmpsheet, &r, &fullr, col_defaults);

			g_ptr_array_free (col_defaults, TRUE);

			// Just in case there was absolutely nothing in
			// tmpsheet:
			if (r.start.col > r.end.col)
				range_init (&r, 0, 0, 0, 0);
		}
		ret = clipboard_copy_range (tmpsheet, &r);
	}

	/* This isn't particularly right, but we are going to delete
	   the workbook shortly.  See #490479.  */
	WORKBOOK_FOREACH_SHEET (wb, sheet, {
		cellregion_invalidate_sheet (ret, sheet);
	});

out:
	if (wb_view)
		g_object_unref (wb_view);
	if (wb)
		g_object_unref (wb);
	g_object_unref (ioc);
	g_object_unref (input);

	return ret;
}

static void
image_content_received (GtkClipboard *clipboard, GtkSelectionData *sel,
			gpointer closure)
{
	GnmGtkClipboardCtxt *ctxt = closure;
	WBCGtk *wbcg = ctxt->wbcg;
	GnmPasteTarget *pt = ctxt->paste_target;
	int sel_len = gtk_selection_data_get_length (sel);

	paste_to_gnumeric (sel, "image");

	if (sel_len > 0) {
		scg_paste_image (wbcg_cur_scg (wbcg), &pt->range,
				 gtk_selection_data_get_data (sel), sel_len);
	}

	gnm_gtk_clipboard_context_free (ctxt);
}

static void
urilist_content_received (GtkClipboard *clipboard, GtkSelectionData *sel,
			  gpointer closure)
{
	GnmGtkClipboardCtxt *ctxt = closure;
	WBCGtk *wbcg = ctxt->wbcg;
	GnmPasteTarget *pt = ctxt->paste_target;
	int sel_len = gtk_selection_data_get_length (sel);

	paste_to_gnumeric (sel, "urilist");

	if (sel_len > 0) {
		char *text = g_strndup (gtk_selection_data_get_data (sel), sel_len);
		GSList *uris = go_file_split_urls (text);
		GSList *l;
		g_free (text);

		for (l = uris; l; l = l->next) {
			const char *uri = l->data;
			GsfInput *input;
			gsf_off_t size;
			gconstpointer data;
			char *mime;
			gboolean qimage;

			if (g_str_equal (uri, "copy"))
				continue;
			mime = go_get_mime_type (uri);
			qimage = (strncmp (mime, "image/", 6) == 0);
			g_free (mime);
			if (!qimage)
				continue;

			input = go_file_open (uri, NULL);
			if (!input)
				continue;
			size = gsf_input_size (input);
			data = gsf_input_read (input, size, NULL);
			if (data)
				scg_paste_image (wbcg_cur_scg (wbcg), &pt->range,
						 data, size);
			g_object_unref (input);
		}

		g_slist_free_full (uris, g_free);

	}

	gnm_gtk_clipboard_context_free (ctxt);
}

static void
parse_ms_headers (const char *data, size_t length, size_t *start, size_t *end)
{
	GHashTable *headers = g_hash_table_new_full
		(g_str_hash, g_str_equal, g_free, g_free);
	size_t limit = length;
	size_t i = 0;
	char *key = NULL;
	char *value = NULL;
	long sf, ef;
	const char *v;

	while (i < limit && data[i] != '<') {
		size_t j, k;

		for (j = i; j < limit; j++) {
			if (data[j] == ':') {
				key = g_strndup (data + i, j - i);
				break;
			}
			if (g_ascii_isspace (data[j]))
				goto bad;
		}
		if (j >= limit)
			goto bad;
		j++;

		for (k = j; k < limit; k++) {
			if (data[k] == '\n' || data[k] == '\r') {
				value = g_strndup (data + j, k - j);
				break;
			}
		}
		if (k >= limit)
			goto bad;
		while (g_ascii_isspace (data[k]))
			k++;

		i = k;

		if (debug_clipboard)
			g_printerr ("MS HTML Header [%s] => [%s]\n", key, value);

		if (strcmp (key, "StartHTML") == 0) {
			long l = strtol (value, NULL, 10);
			limit = MIN (limit, (size_t)MAX (0, l));
		}

		g_hash_table_replace (headers, key, value);
		key = value = NULL;
	}

	v = g_hash_table_lookup (headers, "StartFragment");
	sf = v ? strtol (v, NULL, 10) : -1;
	if (sf < (long)limit)
		goto bad;

	v = g_hash_table_lookup (headers, "EndFragment");
	ef = v ? strtol (v, NULL, 10) : -1;
	if (ef < sf || ef > (long)length)
		goto bad;

	*start = sf;
	*end = ef;
	goto out;

 bad:
	g_free (key);
	g_free (value);
	*start = 0;
	*end = length;

 out:
	g_hash_table_destroy (headers);
}

static void
table_content_received (GtkClipboard *clipboard, GtkSelectionData *sel,
			gpointer closure)
{
	GnmGtkClipboardCtxt *ctxt = closure;
	WBCGtk *wbcg = ctxt->wbcg;
	WorkbookControl	   *wbc  = GNM_WBC (wbcg);
	GnmPasteTarget	   *pt   = ctxt->paste_target;
	GnmCellRegion *content = NULL;
	GdkAtom target = gtk_selection_data_get_target (sel);
	const guint8 *buffer = gtk_selection_data_get_data (sel);
	int sel_len = gtk_selection_data_get_length (sel);

	paste_to_gnumeric (sel, "table");

	/* Nothing on clipboard? */
	if (sel_len < 0) {
		;
	} else if (target == atoms[ATOM_GNUMERIC]) {
		/* The data is the gnumeric specific XML interchange format */
		GOIOContext *io_context =
			go_io_context_new (GO_CMD_CONTEXT (wbcg));
		content = gnm_xml_cellregion_read
			(wbc, io_context,
			 pt->sheet,
			 (const char *)buffer, sel_len);
		g_object_unref (io_context);
	} else if (target == atoms[ATOM_OOO] ||
		   target == atoms[ATOM_OOO_WINDOWS] ||
		   target == atoms[ATOM_OOO11]) {
		content = table_cellregion_read (wbc, OOO_FILE_OPENER,
						 pt, buffer,
						 sel_len);
	} else if (target == atoms[ATOM_TEXT_HTML] ||
		   target == atoms[ATOM_TEXT_HTML_WINDOWS]) {
		size_t start = 0, end = sel_len;

		if (target == atoms[ATOM_TEXT_HTML_WINDOWS]) {
			/* See bug 143084 */
			parse_ms_headers (buffer, sel_len, &start, &end);
		}

		content = table_cellregion_read (wbc, HTML_FILE_OPENER,
						 pt,
						 buffer + start,
						 end - start);
	} else if (target == atoms[ATOM_BIFF8] ||
		   target == atoms[ATOM_BIFF8_CITRIX] ||
		   target == atoms[ATOM_BIFF8_OO] ||
		   target == atoms[ATOM_BIFF5] ||
		   target == atoms[ATOM_BIFF]) {
		content = table_cellregion_read (wbc, EXCEL_FILE_OPENER,
						 pt, buffer,
						 sel_len);
	}
	if (content) {
		/*
		 * if the conversion from the X selection -> a cellregion
		 * was canceled this may have content sized -1,-1
		 */
		if ((content->cols > 0 && content->rows > 0) ||
		    content->objects != NULL)
			cmd_paste_copy (wbc, pt, content);

		/* Release the resources we used */
		cellregion_unref (content);
	}

	gnm_gtk_clipboard_context_free (ctxt);
}

static gboolean
find_in_table (GdkAtom *targets, int n, GdkAtom a)
{
	int i;
	for (i = 0; i < n; i++)
		if (targets[i] == a)
			return TRUE;
	return FALSE;
}

/**
 * x_targets_received:
 *
 * Invoked when the selection has been received by our application.
 * This is triggered by a call we do to gtk_clipboard_request_contents.
 *
 * We try to import a spreadsheet/table, next an image, and finally fall back
 * to a string format if the others fail, e.g. for html which does not
 * contain a table.
 */
static void
x_targets_received (GtkClipboard *clipboard, GdkAtom *targets,
		    gint n_targets, gpointer closure)
{
	GnmGtkClipboardCtxt *ctxt = closure;
	int i;
	unsigned ui;

	// In order of preference
	static const struct {
		int a;
		const char *opener_id;
	} table_fmts[] = {
		{ ATOM_GNUMERIC, NULL },
		{ ATOM_BIFF8, EXCEL_FILE_OPENER },
		{ ATOM_BIFF8_CITRIX, EXCEL_FILE_OPENER },
		{ ATOM_OOO, OOO_FILE_OPENER },
		{ ATOM_OOO11, OOO_FILE_OPENER },
		{ ATOM_OOO_WINDOWS, OOO_FILE_OPENER },
		{ ATOM_BIFF5, EXCEL_FILE_OPENER },
		{ ATOM_BIFF, EXCEL_FILE_OPENER },
		{ ATOM_TEXT_HTML, HTML_FILE_OPENER },
		{ ATOM_TEXT_HTML_WINDOWS, HTML_FILE_OPENER },
	};

	// In order of preference
	static const int uri_list_fmts[] = {
		ATOM_TEXT_URI_LIST,
		ATOM_GNOME_COPIED_FILES,
		ATOM_KDE_CUT_FILES,
	};

	// In order of preference
	static const int string_fmts[] = {
		ATOM_UTF8_STRING,
		ATOM_TEXT_PLAIN_UTF8,
		ATOM_STRING,
		ATOM_COMPOUND_TEXT
	};

	// Nothing on clipboard?  Try text.
	if (targets == NULL || n_targets == 0) {
		gtk_clipboard_request_text (clipboard, utf8_content_received,
					    ctxt);
		return;
	}

	if (debug_clipboard) {
		int j;

		for (j = 0; j < n_targets; j++) {
			char *name = gdk_atom_name (targets[j]);
			g_printerr ("Clipboard target %d is %s\n",
				    j, name);
			g_free (name);
		}
	}

	// First look for anything that can be considered a spreadsheet
	for (ui = 0; ui < G_N_ELEMENTS(table_fmts); ui++) {
		GdkAtom atom = atoms[table_fmts[ui].a];
		const char *opener = table_fmts[ui].opener_id;
		if ((opener == NULL || has_file_opener (opener)) &&
		    find_in_table (targets, n_targets, atom)) {
			gtk_clipboard_request_contents (clipboard, atom,
							table_content_received,
							ctxt);
			return;
		}
	}

	// Try an image format
	for (i = 0; i < n_targets; i++) {
		GdkAtom atom = targets[i];
		if (gtk_target_list_find (image_targets, atom, NULL)) {
			gtk_clipboard_request_contents (clipboard, atom,
							image_content_received,
							ctxt);
			return;
		}
	}

	// Try a uri list format
	for (ui = 0; ui < G_N_ELEMENTS (uri_list_fmts); ui++) {
		GdkAtom atom = atoms[uri_list_fmts[ui]];
		if (find_in_table (targets, n_targets, atom)) {
			gtk_clipboard_request_contents (clipboard, atom,
							urilist_content_received,
							ctxt);
			return;
		}
	}

	// Try a string format
	for (ui = 0; ui < G_N_ELEMENTS (string_fmts); ui++) {
		GdkAtom atom = atoms[string_fmts[ui]];
		if (find_in_table (targets, n_targets, atom)) {
			gtk_clipboard_request_contents (clipboard, atom,
							text_content_received,
							ctxt);
			return;
		}
	}

	// Give up
	gnm_gtk_clipboard_context_free (ctxt);
}

/* Cheezy implementation: paste into a temporary workbook, save that. */
static guchar *
table_cellregion_write (GOCmdContext *ctx, GnmCellRegion *cr,
			const char *saver_id, int *size)
{
	guchar *ret = NULL;
	const GOFileSaver *saver;
	GsfOutput *output;
	GOIOContext *ioc;
	Workbook *wb;
	WorkbookView *wb_view;
	Sheet *sheet;
	GnmPasteTarget pt;
	GnmRange r;

	if (debug_clipboard_undump) {
		gsize siz;
		gchar *contents;
		if (g_file_get_contents ("paste-from-gnumeric.dat", &contents,
					 &siz, NULL)) {
			g_printerr ("Sending %d prepackaged bytes.\n",
				    (int)siz);
			*size = siz;
			return (guchar *)contents;
		}
	}

	*size = 0;

	saver = go_file_saver_for_id (saver_id);
	if (!saver) {
		// Likely cause: plugin not loaded
		g_printerr ("Failed to get saver for %s for clipboard use.\n",
			    saver_id);
		return NULL;
	}

	output = gsf_output_memory_new ();
	ioc = go_io_context_new (ctx);

	{
		int cols = cr->cols;
		int rows = cr->rows;
		gnm_sheet_suggest_size (&cols, &rows);
		wb = workbook_new ();
		workbook_sheet_add (wb, -1, cols, rows);
	}

	wb_view = workbook_view_new (wb);

	sheet = workbook_sheet_by_index (wb, 0);
	range_init (&r, 0, 0, cr->cols - 1, cr->rows - 1);

	paste_target_init (&pt, sheet, &r,
			   PASTE_AS_VALUES | PASTE_FORMATS |
			   PASTE_COMMENTS | PASTE_OBJECTS);
	if (clipboard_paste_region (cr, &pt, ctx) == FALSE) {
		go_file_saver_save (saver, ioc, GO_VIEW (wb_view), output);
		if (!go_io_error_occurred (ioc)) {
			GsfOutputMemory *omem = GSF_OUTPUT_MEMORY (output);
			gsf_off_t osize = gsf_output_size (output);
			const guint8 *data = gsf_output_memory_get_bytes (omem);

			*size = osize;
			if (*size == osize) {
				ret = go_memdup (data, *size);
			} else {
				g_warning ("Overflow");	/* Far fetched! */
			}
		}
	}
	if (!gsf_output_is_closed (output))
		gsf_output_close (output);
	g_object_unref (wb_view);
	g_object_unref (wb);
	g_object_unref (ioc);
	g_object_unref (output);

	return ret;
}

static guchar *
image_write (GnmCellRegion *cr, gchar const *mime_type, int *size)
{
	guchar *ret = NULL;
	SheetObject *so = NULL;
	char *format;
	GsfOutput *output;
	GsfOutputMemory *omem;
	gsf_off_t osize;
	GSList *l;

	*size = -1;

	g_return_val_if_fail (cr->objects != NULL, NULL);
	so = GNM_SO (cr->objects->data);
	g_return_val_if_fail (so != NULL, NULL);

	for (l = cr->objects; l != NULL; l = l->next) {
		if (GNM_IS_SO_IMAGEABLE (GNM_SO (l->data))) {
			so = GNM_SO (l->data);
			break;
		}
	}
	if (so == NULL) {
		// This shouldn't happen
		g_warning ("non-imageable object requested as image\n");
		return ret;
	}

	format = go_mime_to_image_format (mime_type);
	if (!format) {
		// This shouldn't happen
		g_warning ("No image format for %s\n", mime_type);
		return ret;
	}

	output = gsf_output_memory_new ();
	omem   = GSF_OUTPUT_MEMORY (output);
	sheet_object_write_image (so, format, 150.0, output, NULL);
	osize = gsf_output_size (output);

	*size = osize;
	if (*size == osize) {
		ret = g_malloc (*size);
		memcpy (ret, gsf_output_memory_get_bytes (omem), *size);
	} else {
		g_warning ("Overflow");	/* Far fetched! */
	}
	gsf_output_close (output);
	g_object_unref (output);
	g_free (format);

	return ret;
}

static guchar *
object_write (GnmCellRegion *cr, gchar const *mime_type, int *size)
{
	guchar *ret = NULL;
	SheetObject *so = NULL;
	GsfOutput *output;
	GsfOutputMemory *omem;
	gsf_off_t osize;
	GSList *l;

	*size = -1;

	g_return_val_if_fail (cr->objects != NULL, NULL);
	so = GNM_SO (cr->objects->data);
	g_return_val_if_fail (so != NULL, NULL);

	for (l = cr->objects; l != NULL; l = l->next) {
		if (GNM_IS_SO_EXPORTABLE (GNM_SO (l->data))) {
			so = GNM_SO (l->data);
			break;
		}
	}
	if (so == NULL) {
		g_warning ("non exportable object requested\n");
		return ret;
	}
	output = gsf_output_memory_new ();
	omem   = GSF_OUTPUT_MEMORY (output);
	sheet_object_write_object (so, mime_type, output, NULL,
				   gnm_conventions_default);
	osize = gsf_output_size (output);

	*size = osize;
	if (*size == osize)
		ret = go_memdup (gsf_output_memory_get_bytes (omem), *size);
	else
		g_warning ("Overflow");	/* Far fetched! */
	gsf_output_close (output);
	g_object_unref (output);

	return ret;
}

/*
 * x_clipboard_get_cb
 *
 * Callback invoked when another application requests we render the selection.
 */
static void
x_clipboard_get_cb (GtkClipboard *gclipboard,
		    GtkSelectionData *selection_data,
		    guint info_, G_GNUC_UNUSED gpointer app)
{
	gboolean to_gnumeric = FALSE, content_needs_free = FALSE;
	GnmCellRegion *clipboard = gnm_app_clipboard_contents_get ();
	Sheet *sheet = gnm_app_clipboard_sheet_get ();
	GnmRange const *a = gnm_app_clipboard_area_get ();
	GOCmdContext *ctx = gnm_cmd_context_stderr_new ();
	GdkAtom target = gclipboard
		? gtk_selection_data_get_target (selection_data)
		: gtk_selection_data_get_data_type (selection_data); // testing
	AtomInfoType info = info_;
	gchar *target_name = gdk_atom_name (target);

	if (debug_clipboard)
		g_printerr ("clipboard requested, target=%s\n", target_name);

	/*
	 * There are 4 cases. What variables are valid depends on case:
	 * source is
	 *   a cut: clipboard NULL, sheet, area non-NULL.
         *   a copy: clipboard, sheet, area all non-NULL.
	 *   a cut, source closed: clipboard, sheet, area all NULL.
	 *   a copy, source closed: clipboard non-NULL, sheet, area non-NULL.
	 *
	 * If the source is a cut, we copy it for pasting.  We
	 * postpone clearing it until after the selection has been
	 * rendered to the requested format.
	 */
	if (clipboard == NULL && sheet != NULL) {
		content_needs_free = TRUE;
		clipboard = clipboard_copy_range (sheet, a);
	}

	if (clipboard == NULL)
		goto out;

	/* What format does the other application want? */
	if (target == atoms[ATOM_GNUMERIC]) {
		GsfOutputMemory *output  = gnm_cellregion_to_xml (clipboard);
		if (output) {
			gsf_off_t size = gsf_output_size (GSF_OUTPUT (output));
			gconstpointer data = gsf_output_memory_get_bytes (output);

			paste_from_gnumeric (selection_data, target,
					     data, size);
			g_object_unref (output);
			to_gnumeric = TRUE;
		}
	} else if (info == INFO_HTML) {
		int size;
		guchar *buffer = table_cellregion_write (ctx, clipboard,
							 HTML_FILE_SAVER,
							 &size);
		paste_from_gnumeric (selection_data, target, buffer, size);
		g_free (buffer);
	} else if (info == INFO_EXCEL) {
		int size;
		guchar *buffer = table_cellregion_write (ctx, clipboard,
							 EXCEL_FILE_SAVER,
							 &size);
		paste_from_gnumeric (selection_data, target, buffer, size);
		g_free (buffer);
	} else if (target == atoms[ATOM_GOFFICE_GRAPH] ||
	           g_slist_find_custom (go_components_get_mime_types (), target_name, (GCompareFunc) strcmp) != NULL) {
		int size;
		guchar *buffer = object_write (clipboard, target_name, &size);
		paste_from_gnumeric (selection_data, target, buffer, size);
		g_free (buffer);
	} else if (info == INFO_IMAGE) {
		int size;
		guchar *buffer = image_write (clipboard, target_name, &size);
		paste_from_gnumeric (selection_data, target, buffer, size);
		g_free (buffer);
	} else if (target == atoms[ATOM_SAVE_TARGETS]) {
		// We implicitly registered this target when calling
		// gtk_clipboard_set_can_store. We're supposed to ignore it.
	} else if (info == INFO_GENERIC_TEXT) {
		Workbook *wb = clipboard->origin_sheet->workbook;
		GString *res = cellregion_to_string (clipboard,
			TRUE, workbook_date_conv (wb));
		if (res != NULL) {
			if (debug_clipboard)
				g_message ("clipboard text of %d bytes",
					   (int)res->len);
			gtk_selection_data_set_text (selection_data,
						     res->str, res->len);
			g_string_free (res, TRUE);
		} else {
			if (debug_clipboard)
				g_message ("clipboard empty text");
			gtk_selection_data_set_text (selection_data, "", 0);
		}
	} else
		gtk_selection_data_set_text (selection_data, "", 0);

	/*
	 * If this was a CUT operation we need to clear the content that
	 * was pasted into another application and release the stuff on
	 * the clipboard
	 */
	if (content_needs_free) {
		/* If the other app was a gnumeric, emulate a cut */
		if (to_gnumeric) {
			GOUndo *redo, *undo;
			GnmSheetRange *sr    = gnm_sheet_range_new (sheet, a);
			SheetView const *sv  = gnm_app_clipboard_sheet_view_get ();
			SheetControl *sc     = g_ptr_array_index (sv->controls, 0);
			WorkbookControl *wbc = sc_wbc (sc);
			char *name;
			char *text;

			redo = sheet_clear_region_undo
				(sr,
				 CLEAR_VALUES|CLEAR_COMMENTS|CLEAR_RECALC_DEPS);
			undo = clipboard_copy_range_undo (sheet, a);
			name = undo_range_name (sheet, a);
			text = g_strdup_printf (_("Cut of %s"), name);
			g_free (name);
			cmd_generic (wbc, text, undo, redo);
			g_free (text);
			gnm_app_clipboard_clear (TRUE);
		}

		cellregion_unref (clipboard);
	}
 out:
	g_free (target_name);
	g_object_unref (ctx);
}

/**
 * x_clipboard_clear_cb:
 *
 * Callback for the "we lost the X selection" signal.
 */
static void
x_clipboard_clear_cb (GtkClipboard *clipboard, gpointer app_)
{
	if (debug_clipboard)
		g_printerr ("Lost clipboard ownership.\n");

	gnm_app_clipboard_clear (FALSE);
}

void
gnm_x_request_clipboard (WBCGtk *wbcg, GnmPasteTarget const *pt)
{
	GnmGtkClipboardCtxt *ctxt;
	GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (wbcg_toplevel (wbcg)));
	GtkClipboard *clipboard =
		gtk_clipboard_get_for_display
		(display,
		 gnm_conf_get_cut_and_paste_prefer_clipboard ()
		 ? GDK_SELECTION_CLIPBOARD
		 : GDK_SELECTION_PRIMARY);

	ctxt = g_new (GnmGtkClipboardCtxt, 1);
	ctxt->wbcg = wbcg;
	ctxt->paste_target = go_memdup (pt, sizeof (*pt));

	/* Query the formats, This will callback x_targets_received */
	gtk_clipboard_request_targets (clipboard,
				       x_targets_received, ctxt);
}

static void
cb_clear_target_entry (gpointer te_)
{
	GtkTargetEntry *te = te_;
	g_free (te->target);
}

static void
add_target (GArray *targets, const char *target, int flags, AtomInfoType info)
{
	GtkTargetEntry t;
	t.target = g_strdup (target);
	t.flags = flags;
	t.info = info;
	g_array_append_val (targets, t);
}

static gboolean
is_clipman_target (const char *target)
{
	return (g_str_equal (target, atom_names[ATOM_GNUMERIC]) ||
		g_str_equal (target, atom_names[ATOM_GOFFICE_GRAPH]) ||
		g_str_equal (target, atom_names[ATOM_TEXT_HTML]) ||
		g_str_equal (target, atom_names[ATOM_UTF8_STRING]) ||
		g_str_equal (target, atom_names[ATOM_TEXT_PLAIN_UTF8]) ||
		g_str_equal (target, atom_names[ATOM_BIFF8_OO]) ||
		g_str_equal (target, atom_names[ATOM_IMAGE_SVGXML]) ||
		g_str_equal (target, atom_names[ATOM_IMAGE_XWMF]) ||
		g_str_equal (target, atom_names[ATOM_IMAGE_XEMF]) ||
		g_str_equal (target, atom_names[ATOM_IMAGE_PNG]) ||
		g_str_equal (target, atom_names[ATOM_IMAGE_JPEG]));
}

/* Restrict the	set of formats offered to clipboard manager. */
static void
set_clipman_targets (GdkDisplay *disp, GArray *targets)
{
	GArray *allowed = g_array_new (FALSE, FALSE, sizeof (GtkTargetEntry));
	unsigned ui;

	g_array_set_clear_func (allowed, cb_clear_target_entry);

	for (ui = 0; ui < targets->len; ui++) {
		GtkTargetEntry *te = &g_array_index (targets, GtkTargetEntry, ui);
		if (is_clipman_target (te->target))
			add_target (allowed, te->target, te->flags, te->info);
	}

	gtk_clipboard_set_can_store
		(gtk_clipboard_get_for_display
		 (disp, GDK_SELECTION_CLIPBOARD),
		 &g_array_index (allowed, GtkTargetEntry, 0),
		 allowed->len);

	g_array_free (allowed, TRUE);
}

static void
add_target_list (GArray *targets, GtkTargetList *src, AtomInfoType info)
{
	int i, n;
	GtkTargetEntry *entries = gtk_target_table_new_from_list (src, &n);

	for (i = 0; i < n; i++) {
		GtkTargetEntry *te = entries + i;
		add_target (targets, te->target, te->flags,
			    info == INFO_UNKNOWN ? te->info : info);
	}

	gtk_target_table_free (entries, n);
}

gboolean
gnm_x_claim_clipboard (GdkDisplay *display)
{
	GnmCellRegion *content = gnm_app_clipboard_contents_get ();
	SheetObject *imageable = NULL, *exportable = NULL;
	GArray *targets = g_array_new (FALSE, FALSE, sizeof (GtkTargetEntry));
	gboolean ret;
	GObject *app = gnm_app_get_app ();
	gboolean no_cells = (!content) || (content->cols <= 0 || content->rows <= 0);

	g_array_set_clear_func (targets, cb_clear_target_entry);

	if (no_cells) {
		GSList *ptr = content ? content->objects : NULL;

		add_target (targets, atom_names[ATOM_GNUMERIC], 0, INFO_GNUMERIC);

		for (; ptr != NULL; ptr = ptr->next) {
			SheetObject *candidate = GNM_SO (ptr->data);
			if (exportable == NULL && GNM_IS_SO_EXPORTABLE (candidate))
				exportable = candidate;
			if (imageable == NULL && GNM_IS_SO_IMAGEABLE (candidate))
				imageable = candidate;
		}
	} else {
		add_target (targets, atom_names[ATOM_GNUMERIC], 0, INFO_GNUMERIC);
		if (has_file_saver (EXCEL_FILE_SAVER)) {
			add_target (targets, atom_names[ATOM_BIFF8], 0, INFO_EXCEL);
			add_target (targets, atom_names[ATOM_BIFF8_CITRIX], 0, INFO_EXCEL);
			add_target (targets, atom_names[ATOM_BIFF8_OO], 0, INFO_EXCEL);
		}
		if (has_file_saver (HTML_FILE_SAVER)) {
#ifdef G_OS_WIN32
			add_target (targets, atom_names[ATOM_TEXT_HTML_WINDOWS], 0, INFO_HTML);
#else
			add_target (targets, atom_names[ATOM_TEXT_HTML], 0, INFO_HTML);
#endif
		}
		add_target (targets, atom_names[ATOM_UTF8_STRING], 0, INFO_GENERIC_TEXT);
		add_target (targets, atom_names[ATOM_TEXT_PLAIN_UTF8], 0, INFO_GENERIC_TEXT);
		add_target (targets, atom_names[ATOM_COMPOUND_TEXT], 0, INFO_GENERIC_TEXT);
		add_target (targets, atom_names[ATOM_STRING], 0, INFO_GENERIC_TEXT);
	}

	if (exportable) {
		GtkTargetList *tl =
			sheet_object_exportable_get_target_list (exportable);
		add_target_list (targets, tl, INFO_OBJECT);
		gtk_target_list_unref (tl);
	}

	if (imageable) {
		GtkTargetList *tl =
			sheet_object_get_target_list (imageable);
		add_target_list (targets, tl, INFO_IMAGE);
		gtk_target_list_unref (tl);
	}

	/* Register a x_clipboard_clear_cb only for CLIPBOARD, not for
	 * PRIMARY */
	ret = gtk_clipboard_set_with_owner (
		gtk_clipboard_get_for_display (display, GDK_SELECTION_CLIPBOARD),
		&g_array_index(targets,GtkTargetEntry,0), targets->len,
		x_clipboard_get_cb,
		x_clipboard_clear_cb,
		app);
	if (ret) {
		if (debug_clipboard) {
			unsigned ui;
			g_printerr ("Clipboard successfully claimed.\n");
			g_printerr ("Clipboard targets offered: ");
			for (ui = 0; ui < targets->len; ui++) {
				g_printerr ("%s%s",
					    (ui ? ", " : ""),
					    g_array_index(targets,GtkTargetEntry,ui).target);
			}
			g_printerr ("\n");
		}

		g_object_set_data_full (app, APP_CLIP_DISP_KEY,
					g_slist_prepend (g_object_steal_data (app, APP_CLIP_DISP_KEY),
							 display),
					(GDestroyNotify)g_slist_free);

		set_clipman_targets (display, targets);
		(void)gtk_clipboard_set_with_owner (
			gtk_clipboard_get_for_display (display,
						       GDK_SELECTION_PRIMARY),
			&g_array_index(targets,GtkTargetEntry,0), targets->len,
			x_clipboard_get_cb,
			NULL,
			app);
	} else {
		if (debug_clipboard)
			g_printerr ("Failed to claim clipboard.\n");
	}

	g_array_free (targets, TRUE);

	return ret;
}

void
gnm_x_disown_clipboard (void)
{
	GObject *app = gnm_app_get_app ();
	GSList *displays = g_object_steal_data (app, APP_CLIP_DISP_KEY);
	GSList *l;

	for (l = displays; l; l = l->next) {
		GdkDisplay *display = l->data;
		gtk_selection_owner_set_for_display (display, NULL,
						     GDK_SELECTION_PRIMARY,
						     GDK_CURRENT_TIME);
		gtk_selection_owner_set_for_display (display, NULL,
						     GDK_SELECTION_CLIPBOARD,
						     GDK_CURRENT_TIME);
	}
	g_slist_free (displays);
}

/* Hand clipboard off to clipboard manager. To be called before workbook
 * object is destroyed.
 */
void
gnm_x_store_clipboard_if_needed (Workbook *wb)
{
	Sheet *sheet = gnm_app_clipboard_sheet_get ();
	WBCGtk *wbcg = NULL;

	g_return_if_fail (GNM_IS_WORKBOOK (wb));

	if (sheet && sheet->workbook == wb) {
		WORKBOOK_FOREACH_CONTROL (wb, view, control, {
			if (GNM_IS_WBC_GTK (control)) {
				wbcg = WBC_GTK (control);
			}
		});

		if (wbcg) {
			GtkClipboard *clip = gtk_clipboard_get_for_display
				(gtk_widget_get_display
				 (GTK_WIDGET (wbcg_toplevel (wbcg))),
				 GDK_SELECTION_CLIPBOARD);
			if (gtk_clipboard_get_owner (clip) == gnm_app_get_app ()) {
				if (debug_clipboard)
					g_printerr ("Handing off clipboard\n");
				gtk_clipboard_store (clip);
			}
		}
	}
}

GBytes *
gui_clipboard_test (const char *fmt)
{
	GtkClipboard *gclipboard = NULL;
	gpointer app = NULL;
	GtkSelectionData *selection_data;
	guint info;
	unsigned ui;
	GdkAtom atom = NULL;
	const guchar *data;
	gint len;
	GBytes *res;

	for (ui = 0; ui < G_N_ELEMENTS (atom_names); ui++) {
		if (g_str_equal (fmt, atom_names[ui])) {
			atom = atoms[ui];
			break;
		}
	}
	if (!atom)
		return NULL;

	switch (ui) {
	case ATOM_GNUMERIC:
		info = INFO_GNUMERIC;
		break;
	case ATOM_UTF8_STRING:
	case ATOM_TEXT_PLAIN_UTF8:
	case ATOM_STRING:
	case ATOM_COMPOUND_TEXT:
		info = INFO_GENERIC_TEXT;
		break;
	case ATOM_TEXT_HTML:
	case ATOM_TEXT_HTML_WINDOWS:
		info = INFO_HTML;
		break;
	case ATOM_BIFF8:
	case ATOM_BIFF8_OO:
	case ATOM_BIFF8_CITRIX:
	case ATOM_BIFF5:
	case ATOM_BIFF:
		info = INFO_EXCEL;
		break;
	case ATOM_OOO:
	case ATOM_OOO_WINDOWS:
	case ATOM_OOO11:
		info = INFO_OOO;
		break;
	case ATOM_IMAGE_SVGXML:
	case ATOM_IMAGE_XWMF:
	case ATOM_IMAGE_XEMF:
	case ATOM_IMAGE_PNG:
	case ATOM_IMAGE_JPEG:
	case ATOM_IMAGE_BMP:
		info = INFO_IMAGE;
		break;
	default:
		g_printerr ("Unknown info type\n");
		info = INFO_UNKNOWN;
	}

	{
		// This is more than a little bit dirty.  There is no good
		// way to create a GtkSelectionData.
		void *empty = g_new0 (char, 1000000);
		selection_data = gtk_selection_data_copy (empty);
		g_free (empty);
	}

	gtk_selection_data_set (selection_data, atom, 8, NULL, 0);
	// No way to set target???

	x_clipboard_get_cb (gclipboard, selection_data, info, app);
	data = gtk_selection_data_get_data_with_length (selection_data, &len);
	res = g_bytes_new (data, len);
	gtk_selection_data_free (selection_data);
	return res;
}


/**
 * gui_clipboard_init: (skip)
 */
void
gui_clipboard_init (void)
{
	unsigned ui;

	debug_clipboard = gnm_debug_flag ("clipboard");
	debug_clipboard_dump = gnm_debug_flag ("clipboard-dump");
	debug_clipboard_undump = gnm_debug_flag ("clipboard-undump");

	for (ui = 0; ui < G_N_ELEMENTS (atoms); ui++)
		atoms[ui] = gdk_atom_intern_static_string (atom_names[ui]);

	generic_text_targets = gtk_target_list_new (NULL, 0);
	gtk_target_list_add_text_targets (generic_text_targets, INFO_GENERIC_TEXT);

	image_targets = gtk_target_list_new (NULL, 0);
	gtk_target_list_add_image_targets (image_targets, 0, FALSE);
}

/**
 * gui_clipboard_shutdown: (skip)
 */
void
gui_clipboard_shutdown (void)
{
	gtk_target_list_unref (generic_text_targets);
	gtk_target_list_unref (image_targets);
}
