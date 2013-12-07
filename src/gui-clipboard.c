/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gui-clipboard.c: Implements the X11 based copy/paste operations
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gui-clipboard.h"

#include "gui-util.h"
#include "clipboard.h"
#include "command-context-stderr.h"
#include "selection.h"
#include "application.h"
#include "workbook-control.h"
#include "wbc-gtk.h"
#include "workbook-priv.h"
#include "workbook.h"
#include "workbook-view.h"
#include "ranges.h"
#include "sheet.h"
#include "sheet-style.h"
#include "sheet-object.h"
#include "sheet-control-gui.h"
#include "sheet-view.h"
#include "commands.h"
#include "value.h"
#include "number-match.h"
#include "dialog-stf.h"
#include "stf-parse.h"
#include "mstyle.h"
#include "gnm-format.h"
#include "gnumeric-conf.h"
#include "xml-sax.h"
#include "gutils.h"

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

static gboolean
debug_clipboard (void)
{
	static gboolean d_clipboard;
	static gboolean inited = FALSE;

	if (!inited) {
		inited = TRUE;
		d_clipboard = gnm_debug_flag ("clipboard");
	}

	return d_clipboard;
}

typedef struct {
	WBCGtk *wbcg;
	GnmPasteTarget        *paste_target;
	GdkAtom            image_atom;
	GdkAtom            string_atom;
} GnmGtkClipboardCtxt;

/* The name of our clipboard atom and the 'magic' info number */
#define GNUMERIC_ATOM_NAME "application/x-gnumeric"
#define GNUMERIC_ATOM_INFO 2001
#define GOFFICE_GRAPH_ATOM_NAME "application/x-goffice-graph"

/*
 * Emacs hack:
 * (x-get-selection-internal 'CLIPBOARD 'TARGETS)
 */

/* From MS Excel */
#define BIFF8_ATOM_NAME	"Biff8"
#define BIFF8_ATOM_NAME_CITRIX "_CITRIX_Biff8"
#define BIFF5_ATOM_NAME	"Biff5"
#define BIFF4_ATOM_NAME	"Biff4"
#define BIFF3_ATOM_NAME	"Biff3"
#define BIFF_ATOM_NAME	"Biff"

#define HTML_ATOM_NAME_UNIX "text/html"
#define HTML_ATOM_NAME_WINDOWS "HTML Format"
#ifdef G_OS_WIN32
#define HTML_ATOM_NAME HTML_ATOM_NAME_WINDOWS
#else
#define HTML_ATOM_NAME HTML_ATOM_NAME_UNIX
#endif

#define OOO_ATOM_NAME "application/x-openoffice;windows_formatname=\"Star Embed Source (XML)\""
#define OOO11_ATOM_NAME "application/x-openoffice-embed-source-xml;windows_formatname=\"Star Embed Source (XML)\""
#define OOO_ATOM_NAME_WINDOWS "Star Embed Source (XML)"

#define UTF8_ATOM_NAME "UTF8_STRING"
#define CTEXT_ATOM_NAME "COMPOUND_TEXT"
#define STRING_ATOM_NAME "STRING"

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
	Workbook *wb = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
	DialogStfResult_t *dialogresult;
	GnmCellRegion *cr = NULL;
	gboolean oneline;
	char *data_converted = NULL;

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
text_content_received (GtkClipboard *clipboard,  GtkSelectionData *sel,
		       gpointer closure)
{
	GnmGtkClipboardCtxt *ctxt = closure;
	WBCGtk *wbcg = ctxt->wbcg;
	WorkbookControl	   *wbc  = WORKBOOK_CONTROL (wbcg);
	GnmPasteTarget	   *pt   = ctxt->paste_target;
	GnmCellRegion *content = NULL;
	GdkAtom target = gtk_selection_data_get_target (sel);

	if (debug_clipboard ()) {
		int maxlen = 1024;
		char *name = gdk_atom_name (gtk_selection_data_get_target (sel));
		g_printerr ("Received %d bytes of text for target %s\n",
			    gtk_selection_data_get_length (sel),
			    name);
		g_free (name);
		if (gtk_selection_data_get_length (sel) > 0) {
			gsf_mem_dump (gtk_selection_data_get_data (sel), MIN (gtk_selection_data_get_length (sel), maxlen));
			if (gtk_selection_data_get_length (sel) > maxlen)
				g_printerr ("...\n");
		}
	}

	/* Nothing on clipboard? */
	if (gtk_selection_data_get_length (sel) < 0) {
		;
	} else if (target == gdk_atom_intern (UTF8_ATOM_NAME, FALSE)) {
		content = text_to_cell_region (wbcg, (const char *)gtk_selection_data_get_data (sel),
					       gtk_selection_data_get_length (sel), "UTF-8", TRUE);
	} else if (target == gdk_atom_intern (CTEXT_ATOM_NAME, FALSE)) {
		/* COMPOUND_TEXT is icky.  Just let GTK+ do the work.  */
		char *data_utf8 = (char *)gtk_selection_data_get_text (sel);
		content = text_to_cell_region (wbcg, data_utf8, strlen (data_utf8), "UTF-8", TRUE);
		g_free (data_utf8);
	} else if (target == gdk_atom_intern (STRING_ATOM_NAME, FALSE)) {
		char const *locale_encoding;
		g_get_charset (&locale_encoding);

		content = text_to_cell_region (wbcg, (const char *)gtk_selection_data_get_data (sel),
					       gtk_selection_data_get_length (sel), locale_encoding, FALSE);
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
	g_free (ctxt->paste_target);
	g_free (ctxt);
}

static void
utf8_content_received (GtkClipboard *clipboard,  const gchar *text,
		       gpointer closure)
{
	GnmGtkClipboardCtxt *ctxt = closure;
	WBCGtk *wbcg = ctxt->wbcg;
	WorkbookControl	   *wbc  = WORKBOOK_CONTROL (wbcg);
	GnmPasteTarget	   *pt   = ctxt->paste_target;
	GnmCellRegion *content = NULL;

	/* Nothing on clipboard? */
	if (!text || strlen(text) == 0) {
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
	g_free (ctxt->paste_target);
	g_free (ctxt);
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
			r.start.col = 0;
			r.start.row = 0;
			r.end.col = tmpsheet->cols.max_used;
			r.end.row = tmpsheet->rows.max_used;
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
	GnmPasteTarget	   *pt   = ctxt->paste_target;

	if (debug_clipboard ()) {
		int maxlen = 1024;
		char *name = gdk_atom_name (gtk_selection_data_get_target (sel));
		g_printerr ("Received %d bytes of image for target %s\n",
			    gtk_selection_data_get_length (sel),
			    name);
		g_free (name);
		if (gtk_selection_data_get_length (sel) > 0) {
			gsf_mem_dump (gtk_selection_data_get_data (sel), MIN (gtk_selection_data_get_length (sel), maxlen));
			if (gtk_selection_data_get_length (sel) > maxlen)
				g_printerr ("...\n");
		}
	}

	if (gtk_selection_data_get_length (sel) > 0) {
		scg_paste_image (wbcg_cur_scg (wbcg), &pt->range,
				 gtk_selection_data_get_data (sel), gtk_selection_data_get_length (sel));
		g_free (ctxt->paste_target);
		g_free (ctxt);
	} else  if (ctxt->string_atom !=  GDK_NONE) {
		gtk_clipboard_request_contents (clipboard, ctxt->string_atom,
						text_content_received, ctxt);
	} else {
		g_free (ctxt->paste_target);
		g_free (ctxt);
	}
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

		if (debug_clipboard ())
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
	WorkbookControl	   *wbc  = WORKBOOK_CONTROL (wbcg);
	GnmPasteTarget	   *pt   = ctxt->paste_target;
	GnmCellRegion *content = NULL;
	GdkAtom target = gtk_selection_data_get_target (sel);

	if (debug_clipboard ()) {
		int maxlen = 1024;
		char *name = gdk_atom_name (gtk_selection_data_get_target (sel));
		g_printerr ("Received %d bytes of table for target %s\n",
			    gtk_selection_data_get_length (sel),
			    name);
		g_free (name);
		if (gtk_selection_data_get_length (sel) > 0) {
			gsf_mem_dump (gtk_selection_data_get_data (sel), MIN (gtk_selection_data_get_length (sel), maxlen));
			if (gtk_selection_data_get_length (sel) > maxlen)
				g_printerr ("...\n");
		}
	}

	/* Nothing on clipboard? */
	if (gtk_selection_data_get_length (sel) < 0) {
		;
	} else if (target == gdk_atom_intern (GNUMERIC_ATOM_NAME, FALSE)) {
		/* The data is the gnumeric specific XML interchange format */
		GOIOContext *io_context =
			go_io_context_new (GO_CMD_CONTEXT (wbcg));
		content = gnm_xml_cellregion_read
			(wbc, io_context,
			 pt->sheet,
			 (const char *)gtk_selection_data_get_data (sel), gtk_selection_data_get_length (sel));
		g_object_unref (io_context);
	} else if (target == gdk_atom_intern (OOO_ATOM_NAME, FALSE) ||
		   target == gdk_atom_intern (OOO_ATOM_NAME_WINDOWS, FALSE) ||
		   target == gdk_atom_intern (OOO11_ATOM_NAME, FALSE)) {
		content = table_cellregion_read (wbc, "Gnumeric_OpenCalc:openoffice",
						 pt, gtk_selection_data_get_data (sel),
						 gtk_selection_data_get_length (sel));
	} else if (target == gdk_atom_intern (HTML_ATOM_NAME_UNIX, FALSE) ||
		   target == gdk_atom_intern (HTML_ATOM_NAME_WINDOWS, FALSE)) {
		size_t length = gtk_selection_data_get_length (sel);
		size_t start = 0, end = length;

		if (target == gdk_atom_intern (HTML_ATOM_NAME_WINDOWS, FALSE)) {
			/* See bug 143084 */
			parse_ms_headers (gtk_selection_data_get_data (sel), length, &start, &end);
		}

		content = table_cellregion_read (wbc, "Gnumeric_html:html",
						 pt,
						 gtk_selection_data_get_data (sel) + start,
						 end - start);
	} else if ((target == gdk_atom_intern ( BIFF8_ATOM_NAME, FALSE)) ||
		   (target == gdk_atom_intern ( BIFF8_ATOM_NAME_CITRIX, FALSE)) ||
		   (target == gdk_atom_intern ( BIFF5_ATOM_NAME, FALSE)) ||
		   (target == gdk_atom_intern ( BIFF4_ATOM_NAME, FALSE)) ||
		   (target == gdk_atom_intern ( BIFF3_ATOM_NAME, FALSE)) ||
		   (target == gdk_atom_intern ( BIFF_ATOM_NAME,  FALSE))) {
		content = table_cellregion_read (wbc, "Gnumeric_Excel:excel",
						 pt, gtk_selection_data_get_data (sel),
						 gtk_selection_data_get_length (sel));
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
		g_free (ctxt->paste_target);
		g_free (ctxt);
	} else if (ctxt->image_atom != GDK_NONE) {
		gtk_clipboard_request_contents (clipboard, ctxt->image_atom,
						image_content_received, ctxt);
	} else if (ctxt->string_atom != GDK_NONE) {
		gtk_clipboard_request_contents (clipboard, ctxt->string_atom,
						text_content_received, ctxt);
	} else {
		g_free (ctxt->paste_target);
		g_free (ctxt);
	}
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
	GdkAtom table_atom = GDK_NONE;
	gint i, j;

	/* in order of preference */
	static char const *table_fmts [] = {
		GNUMERIC_ATOM_NAME,

		BIFF8_ATOM_NAME,
		BIFF8_ATOM_NAME_CITRIX,
		BIFF5_ATOM_NAME,
		BIFF4_ATOM_NAME,
		BIFF3_ATOM_NAME,
		BIFF_ATOM_NAME,

		OOO_ATOM_NAME,
		OOO11_ATOM_NAME,
		OOO_ATOM_NAME_WINDOWS,

		HTML_ATOM_NAME_UNIX,
		HTML_ATOM_NAME_WINDOWS,
		NULL
	};
	static char const *string_fmts [] = {
		UTF8_ATOM_NAME,
		STRING_ATOM_NAME,
		CTEXT_ATOM_NAME,
		NULL
	};

	/* Nothing on clipboard? */
	if (targets == NULL || n_targets == 0) {
		gtk_clipboard_request_text (clipboard, utf8_content_received,
					    ctxt);
		return;
	}

	if (debug_clipboard ()) {
		int j;

		for (j = 0; j < n_targets; j++)
			g_printerr ("Clipboard target %d is %s\n",
				    j, gdk_atom_name (targets[j]));
	}

	/* The data is a list of atoms */
	/* Find the best table format offered */
	for (i = 0 ; table_fmts[i] && table_atom == GDK_NONE ; i++) {
		/* Look for one we can use */
		GdkAtom atom = gdk_atom_intern (table_fmts[i], FALSE);
		/* is it on offer? */
		for (j = 0; j < n_targets && table_atom == GDK_NONE; j++) {
			if (targets [j] == atom)
				table_atom = atom;
		}
	}

	if (table_atom == GDK_NONE) {
		GtkTargetList *tl = gtk_target_list_new (NULL, 0);
		gboolean found = FALSE;

		gtk_target_list_add_image_targets (tl, 0, FALSE);

		/* Find an image format */
		for (i = 0 ; i < n_targets && !found; i++) {
			if (gtk_target_list_find (tl, targets[i], NULL)) {
				ctxt->image_atom = targets[i];
				found = TRUE;
			}
		}
		gtk_target_list_unref (tl);
	}

	/* Find a string format to fall back to */
	for (i = 0 ; string_fmts[i] && ctxt->string_atom == GDK_NONE ; i++) {
		/* Look for one we can use */
		GdkAtom atom = gdk_atom_intern (string_fmts[i],	FALSE);
		/* is it on offer? */
		for (j = 0; j < n_targets && ctxt->string_atom == GDK_NONE;
		     j++) {
			if (targets [j] == atom)
				ctxt->string_atom = atom;
		}
		if (ctxt->string_atom != GDK_NONE)
			break;
	}

	if (table_atom != GDK_NONE)
		gtk_clipboard_request_contents (clipboard, table_atom,
						table_content_received, ctxt);
	else if (ctxt->image_atom != GDK_NONE)
		gtk_clipboard_request_contents (clipboard, ctxt->image_atom,
						image_content_received, ctxt);
	else if (ctxt->string_atom != GDK_NONE)
		gtk_clipboard_request_contents (clipboard, ctxt->string_atom,
						text_content_received, ctxt);
	else {
		g_free (ctxt->paste_target);
		g_free (ctxt);
	}
}

/* Cheezy implementation: paste into a temporary workbook, save that. */
static guchar *
table_cellregion_write (GOCmdContext *ctx, GnmCellRegion *cr,
			char * saver_id, int *size)
{
	guchar *ret = NULL;
	const GOFileSaver *saver = go_file_saver_for_id (saver_id);
	GsfOutput *output;
	GOIOContext *ioc;
	Workbook *wb;
	WorkbookView *wb_view;
	Sheet *sheet;
	GnmPasteTarget pt;
	GnmRange r;

	*size = 0;
	if (!saver)
		return NULL;

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
	memset (&r, 0, sizeof r);
	r.end.col = cr->cols - 1;
	r.end.row = cr->rows - 1;

	paste_target_init (&pt, sheet, &r,
			   PASTE_AS_VALUES | PASTE_FORMATS |
			   PASTE_COMMENTS | PASTE_OBJECTS);
	if (clipboard_paste_region (cr, &pt, ctx) == FALSE) {
		go_file_saver_save (saver, ioc, GO_VIEW (wb_view), output);
		if (!go_io_error_occurred (ioc)) {
			GsfOutputMemory *omem = GSF_OUTPUT_MEMORY (output);
			gsf_off_t osize = gsf_output_size (output);

			*size = osize;
			if (*size == osize) {
				ret = g_malloc (*size);
				memcpy (ret,
					gsf_output_memory_get_bytes (omem),
					*size);
			} else {
				g_warning ("Overflow");	/* Far fetched! */
			}
		}
	}
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
	so = SHEET_OBJECT (cr->objects->data);
	g_return_val_if_fail (so != NULL, NULL);

	if (strncmp (mime_type, "image/", 6) != 0)
		return ret;
	for (l = cr->objects; l != NULL; l = l->next) {
		if (IS_SHEET_OBJECT_IMAGEABLE (SHEET_OBJECT (l->data))) {
			so = SHEET_OBJECT (l->data);
			break;
		}
	}
	if (so == NULL) {
		g_warning ("non imageable object requested as image\n");
		return ret;
	}

	format = go_mime_to_image_format (mime_type);
	if (!format) {
		g_warning ("No image format for %s\n", mime_type);
		g_free (format);
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
	so = SHEET_OBJECT (cr->objects->data);
	g_return_val_if_fail (so != NULL, NULL);

	for (l = cr->objects; l != NULL; l = l->next) {
		if (IS_SHEET_OBJECT_EXPORTABLE (SHEET_OBJECT (l->data))) {
			so = SHEET_OBJECT (l->data);
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
		ret = g_memdup (gsf_output_memory_get_bytes (omem), *size);
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
x_clipboard_get_cb (GtkClipboard *gclipboard, GtkSelectionData *selection_data,
		    guint info, GObject *app)
{
	gboolean to_gnumeric = FALSE, content_needs_free = FALSE;
	GnmCellRegion *clipboard = gnm_app_clipboard_contents_get ();
	Sheet *sheet = gnm_app_clipboard_sheet_get ();
	GnmRange const *a = gnm_app_clipboard_area_get ();
	GOCmdContext *ctx = cmd_context_stderr_new ();
	GdkAtom target = gtk_selection_data_get_target (selection_data);
	gchar *target_name = gdk_atom_name (target);

	if (debug_clipboard ())
		g_printerr ("clipboard target=%s\n", target_name);

	/*
	 * There are 4 cases. What variables are valid depends on case:
	 * source is
	 *   a cut: clipboard NULL, sheet, area non NULL.
         *   a copy: clipboard, sheet, area all non NULL.
	 *   a cut, source closed: clipboard, sheet, area all NULL.
	 *   a copy, source closed: clipboard non NULL, sheet, area non NULL.
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
	if (target == gdk_atom_intern (GNUMERIC_ATOM_NAME, FALSE)) {
		GsfOutputMemory *output  = gnm_cellregion_to_xml (clipboard);
		if (output) {
			gsf_off_t size = gsf_output_size (GSF_OUTPUT (output));
			if (debug_clipboard ())
				g_printerr ("clipboard .gnumeric of %d bytes\n",
					   (int)size);
			gtk_selection_data_set
				(selection_data, target, 8,
				 gsf_output_memory_get_bytes (output),
				 size);
			g_object_unref (output);
			to_gnumeric = TRUE;
		}
	} else if (target == gdk_atom_intern (HTML_ATOM_NAME, FALSE)) {
		char *saver_id = (char *) "Gnumeric_html:xhtml_range";
		int buffer_size;
		guchar *buffer = table_cellregion_write (ctx, clipboard,
							 saver_id,
							 &buffer_size);
		if (debug_clipboard ())
			g_message ("clipboard html of %d bytes",
				    buffer_size);
		gtk_selection_data_set (selection_data,
					target, 8,
					(guchar *) buffer, buffer_size);
		g_free (buffer);
	} else if (strcmp (target_name, "application/x-goffice-graph") == 0 ||
	           g_slist_find_custom (go_components_get_mime_types (), target_name, (GCompareFunc) strcmp) != NULL) {
		int buffer_size;
		guchar *buffer = object_write (clipboard, target_name,
					      &buffer_size);
		if (debug_clipboard ())
			g_message ("clipboard graph of %d bytes",
				   buffer_size);
		gtk_selection_data_set (selection_data,
					target, 8,
					(guchar *) buffer, buffer_size);
		g_free (buffer);
	} else if (strncmp (target_name, "image/", 6) == 0) {
		int buffer_size;
		guchar *buffer = image_write (clipboard, target_name,
					      &buffer_size);
		if (debug_clipboard ())
			g_message ("clipboard image of %d bytes",
				    buffer_size);
		gtk_selection_data_set (selection_data,
					target, 8,
					(guchar *) buffer, buffer_size);
		g_free (buffer);
	} else if (strcmp (target_name, "SAVE_TARGETS") == 0) {
		/* We implicitly registered this when calling
		 * gtk_clipboard_set_can_store. We're supposed to
		 * ignore it. */
	} else if (clipboard->origin_sheet) {
		Workbook *wb = clipboard->origin_sheet->workbook;
		GString *res = cellregion_to_string (clipboard,
			TRUE, workbook_date_conv (wb));
		if (res != NULL) {
			if (debug_clipboard ())
				g_message ("clipboard text of %d bytes",
					   (int)res->len);
			gtk_selection_data_set_text (selection_data,
				res->str, res->len);
			g_string_free (res, TRUE);
		} else {
			if (debug_clipboard ())
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
static gint
x_clipboard_clear_cb (GtkClipboard *clipboard,
		      GObject *obj)
{
	if (debug_clipboard ())
		g_printerr ("Lost clipboard ownership.\n");

	gnm_app_clipboard_clear (FALSE);

	return TRUE;
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
	ctxt->paste_target = g_new (GnmPasteTarget, 1);
	*ctxt->paste_target = *pt;
	ctxt->image_atom = GDK_NONE;
	ctxt->string_atom = GDK_NONE;

	/* Query the formats, This will callback x_targets_received */
	gtk_clipboard_request_targets (clipboard,
				       x_targets_received, ctxt);
}

/* Restrict the	set of formats offered to clipboard manager. */
/* We include bmp in the whitelist because that's the only image format
 * we share with OOo over clipboard (!) */
static void
set_clipman_targets (GdkDisplay *disp, GtkTargetEntry *targets, guint n_targets)
{
	static GtkTargetEntry clipman_whitelist[] = {
		{ (char *) GNUMERIC_ATOM_NAME, 0, GNUMERIC_ATOM_INFO },
		{ (char *) HTML_ATOM_NAME, 0, 0 },
		{ (char *)"UTF8_STRING", 0, 0 },
		{ (char *)"application/x-goffice-graph", 0, 0 },
		{ (char *)"image/svg+xml", 0, 0 },
		{ (char *)"image/x-wmf", 0, 0 },
		{ (char *)"image/x-emf", 0, 0 },
		{ (char *)"image/png", 0, 0 },
		{ (char *)"image/jpeg", 0, 0 },
		{ (char *)"image/bmp", 0, 0 },
	};
	guint n_whitelist = G_N_ELEMENTS (clipman_whitelist);
	int n_allowed;
	GtkTargetList *tl = gtk_target_list_new (NULL, 0);
	GtkTargetEntry *t, *wt, *t_allowed;

	for (t = targets; t < targets + n_targets; t++) {
		for (wt = clipman_whitelist;
		     wt < clipman_whitelist + n_whitelist; wt++) {
			if (strcmp(t->target, wt->target) == 0) {
				gtk_target_list_add
					(tl, gdk_atom_intern (t->target, FALSE),
					 t->flags, t->info);
				break;
			}
		}
	}

	t_allowed = gtk_target_table_new_from_list (tl, &n_allowed);
	gtk_target_list_unref (tl);

	gtk_clipboard_set_can_store (
			gtk_clipboard_get_for_display (
				disp, GDK_SELECTION_CLIPBOARD),
				t_allowed, n_allowed);
	gtk_target_table_free (t_allowed, n_allowed);
	t_allowed = NULL;
}

gboolean
gnm_x_claim_clipboard (GdkDisplay *display)
{
	GnmCellRegion *content = gnm_app_clipboard_contents_get ();
	SheetObject *imageable = NULL, *exportable = NULL;
	GtkTargetEntry *targets = NULL;
	int n_targets;
	gboolean ret;
	GObject *app = gnm_app_get_app ();

	static GtkTargetEntry const table_targets[] = {
		{ (char *) GNUMERIC_ATOM_NAME, 0, GNUMERIC_ATOM_INFO },
		{ (char *) HTML_ATOM_NAME, 0, 0 },
		{ (char *)"UTF8_STRING", 0, 0 },
		{ (char *)"COMPOUND_TEXT", 0, 0 },
		{ (char *)"STRING", 0, 0 },
	};

	targets = (GtkTargetEntry *) table_targets;
	n_targets = G_N_ELEMENTS (table_targets);
	if (content &&
	    (content->cols <= 0 || content->rows <= 0)) {
		GSList *ptr;
		for (ptr = content->objects; ptr != NULL;
		     ptr = ptr->next) {
			SheetObject *candidate = SHEET_OBJECT (ptr->data);
			if (exportable == NULL && IS_SHEET_OBJECT_EXPORTABLE (candidate))
				exportable = candidate;
			if (imageable == NULL && IS_SHEET_OBJECT_IMAGEABLE (candidate))
				imageable = candidate;
		}
		/* Currently, we can't render sheet objects as text or html */
		n_targets = 1;
	}
	if (exportable) {
		GtkTargetList *tl =
			sheet_object_exportable_get_target_list (exportable);
		/* _add_table prepends to target_list */
		gtk_target_list_add_table (tl, table_targets, 1);
		targets = gtk_target_table_new_from_list (tl, &n_targets);
		gtk_target_list_unref (tl);
	}
	if (imageable) {
		GtkTargetList *tl =
			sheet_object_get_target_list (imageable);
		/* _add_table prepends to target_list */
		gtk_target_list_add_table (tl, targets, (exportable)? n_targets: 1);
		targets = gtk_target_table_new_from_list (tl, &n_targets);
		gtk_target_list_unref (tl);
	}
	/* Register a x_clipboard_clear_cb only for CLIPBOARD, not for
	 * PRIMARY */
	ret = gtk_clipboard_set_with_owner (
		gtk_clipboard_get_for_display (display, GDK_SELECTION_CLIPBOARD),
		targets, n_targets,
		(GtkClipboardGetFunc) x_clipboard_get_cb,
		(GtkClipboardClearFunc) x_clipboard_clear_cb,
		app);
	if (ret) {
		if (debug_clipboard ())
			g_printerr ("Clipboard successfully claimed.\n");

		g_object_set_data_full (app, APP_CLIP_DISP_KEY,
					g_slist_prepend (g_object_steal_data (app, APP_CLIP_DISP_KEY),
							 display),
					(GDestroyNotify)g_slist_free);


		set_clipman_targets (display, targets, n_targets);
		(void)gtk_clipboard_set_with_owner (
			gtk_clipboard_get_for_display (display,
						       GDK_SELECTION_PRIMARY),
			targets, n_targets,
			(GtkClipboardGetFunc) x_clipboard_get_cb,
			NULL,
			app);
	} else {
		if (debug_clipboard ())
			g_printerr ("Failed to claim clipboard.\n");
	}
	if (exportable || imageable)
		gtk_target_table_free (targets, n_targets);

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

	g_return_if_fail (IS_WORKBOOK (wb));

	if (sheet && sheet->workbook == wb) {
		WORKBOOK_FOREACH_CONTROL (wb, view, control, {
			if (IS_WBC_GTK (control)) {
				wbcg = WBC_GTK (control);
			}
		});

		if (wbcg) {
			GtkClipboard *clip = gtk_clipboard_get_for_display
				(gtk_widget_get_display
				 (GTK_WIDGET (wbcg_toplevel (wbcg))),
				 GDK_SELECTION_CLIPBOARD);
			if (gtk_clipboard_get_owner (clip) == gnm_app_get_app ()) {
				if (debug_clipboard ())
					g_printerr ("Handing off clipboard\n");
				gtk_clipboard_store (clip);
			}
		}
	}
}

