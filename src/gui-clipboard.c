/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gui-clipboard.c: Implements the X11 based copy/paste operations
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jody Goldberg (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "gui-clipboard.h"

#include "gui-util.h"
#include "clipboard.h"
#include "selection.h"
#include "application.h"
#include <goffice/app/io-context.h>
#include "workbook-control-gui-priv.h"
#include "workbook.h"
#include "workbook-view.h"
#include "ranges.h"
#include "sheet.h"
#include "sheet-style.h"
#include "sheet-object.h"
#include "sheet-control-gui.h"
#include "commands.h"
#include "xml-io.h"
#include "value.h"
#include "dialog-stf.h"
#include "stf-parse.h"
#include "mstyle.h"
#include "gnumeric-gconf.h"

#include <gsf/gsf-input-memory.h>
#include <gsf/gsf-output-memory.h>
#include <libxml/globals.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>

typedef struct {
	WorkbookControlGUI *wbcg;
	GnmPasteTarget        *paste_target;
	GdkAtom            image_atom;
	GdkAtom            string_atom;
} GnmGtkClipboardCtxt;

/* The name of our clipboard atom and the 'magic' info number */
#define GNUMERIC_ATOM_NAME "application/x-gnumeric"
#define GNUMERIC_ATOM_INFO 2001

#define HTML_ATOM_NAME "text/html"
#define OOO_ATOM_NAME "application/x-openoffice;windows_formatname=\"Star Embed Source (XML)\""
#define OOO11_ATOM_NAME "application/x-openoffice-embed-source-xml;windows_formatname=\"Star Embed Source (XML)\""
#define UTF8_ATOM_NAME "UTF8_STRING"
#define CTEXT_ATOM_NAME "COMPOUND_TEXT"
#define STRING_ATOM_NAME "STRING"


static GnmCellRegion *
text_to_cell_region (WorkbookControlGUI *wbcg,
		     guchar const *data, int data_len,
		     const char *opt_encoding,
		     gboolean fixed_encoding)
{
	DialogStfResult_t *dialogresult;
	GnmCellRegion *cr = NULL;
	gboolean oneline;
	char *data_converted = NULL;
	int i;

	oneline = TRUE;
	for (i = 0; i < data_len; i++)
		if (data[i] == '\n') {
			oneline = FALSE;
			break;
		}

	if (oneline && (opt_encoding == NULL || strcmp (opt_encoding, "UTF-8") != 0)) {
		size_t bytes_written;
		const char *enc = opt_encoding ? opt_encoding : "ASCII";

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

	/*
	 * See if this is a "single line + line end" or a "multiline"
	 * string. If this is _not_ the case we won't invoke the STF, it is
	 * unlikely that the user will actually need it in this case.
	 * NOTE: This is making an assumption on what the user 'wants', this
	 * is not really a good thing. We should put this in a config dialog.
	 */
	if (oneline) {
		CellCopy *ccopy;

		ccopy = g_new (CellCopy, 1);
		ccopy->type = CELL_COPY_TYPE_TEXT;
		ccopy->col_offset = 0;
		ccopy->row_offset = 0;
		ccopy->u.text = g_strndup (data, data_len);

		cr = cellregion_new (NULL);
		cr->content = g_slist_prepend (cr->content, ccopy);
		cr->cols = cr->rows = 1;

		g_free (data_converted);
	} else {
		dialogresult = stf_dialog (wbcg, opt_encoding, fixed_encoding,
					   NULL, FALSE,
					   _("clipboard"), data, data_len);

		if (dialogresult != NULL) {
			cr = stf_parse_region (dialogresult->parseoptions, dialogresult->text, NULL);
			g_return_val_if_fail (cr != NULL, cellregion_new (NULL));

			stf_dialog_result_attach_formats_to_cr (dialogresult, cr);

			stf_dialog_result_free (dialogresult);
		} else {
			return cellregion_new (NULL);
		}
	}

	return cr;
}

static void
text_content_received (GtkClipboard *clipboard,  GtkSelectionData *sel,
		       gpointer closure)
{
	GnmGtkClipboardCtxt *ctxt = closure;
	WorkbookControlGUI *wbcg = ctxt->wbcg;
	WorkbookControl	   *wbc  = WORKBOOK_CONTROL (wbcg);
	GnmPasteTarget	   *pt   = ctxt->paste_target;
	GnmCellRegion *content = NULL;

	/* Nothing on clipboard? */
	if (sel->length < 0) {
		;
	} else if (sel->target == gdk_atom_intern (UTF8_ATOM_NAME, FALSE)) {
		content = text_to_cell_region (wbcg, sel->data, sel->length, "UTF-8", TRUE);
	} else if (sel->target == gdk_atom_intern (CTEXT_ATOM_NAME, FALSE)) {
		/* COMPOUND_TEXT is icky.  Just let GTK+ do the work.  */
		char *data_utf8 = gtk_selection_data_get_text (sel);
		content = text_to_cell_region (wbcg, data_utf8, strlen (data_utf8), "UTF-8", TRUE);
		g_free (data_utf8);
	} else if (sel->target == gdk_atom_intern (STRING_ATOM_NAME, FALSE)) {
		char const *locale_encoding;
		g_get_charset (&locale_encoding);

		content = text_to_cell_region (wbcg, sel->data, sel->length, locale_encoding, FALSE);
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

/**
 * Use the file_opener plugin service to read into a temporary workbook, in
 * order to copy from it to the paste target. A temporary sheet would do just
 * as well, but the file_opener service makes workbooks, not sheets.
 *
 * We use the file_opener service by wrapping the selection data in a GsfInput,
 * and calling wb_view_new_from_input.
 **/
static GnmCellRegion *
table_cellregion_read (WorkbookControl *wbc, const char *reader_id,
		       GnmPasteTarget *pt, guchar *buffer, int length)
{
	WorkbookView *wb_view = NULL;
	Workbook *wb = NULL;
	GList *l = NULL;
	GnmCellRegion *ret = NULL;
	const GOFileOpener *reader = go_file_opener_for_id (reader_id);
	IOContext *ioc;
	GsfInput *input;

	if (!reader) {
		g_warning ("No file opener for %s", reader_id);
		return NULL;
	}

	ioc = gnumeric_io_context_new (GO_CMD_CONTEXT (wbc));
	input = gsf_input_memory_new (buffer, length, FALSE);
	wb_view = wb_view_new_from_input  (input, reader, ioc, NULL);
	if (gnumeric_io_error_occurred (ioc) || wb_view == NULL) {
		gnumeric_io_error_display (ioc);
		goto out;
	}

	wb = wb_view_workbook (wb_view);
	l = workbook_sheets (wb);
	if (l) {
		GnmRange r;
		Sheet *tmpsheet = (Sheet *) l->data;

		r.start.col = 0;
		r.start.row = 0;
		r.end.col = tmpsheet->cols.max_used;
		r.end.row = tmpsheet->rows.max_used;
		ret = clipboard_copy_range (tmpsheet, &r);
	}
out:
	if (l)
		g_list_free (l);
	if (wb_view)
		g_object_unref (wb_view);
	if (wb)
		g_object_unref (wb);
	g_object_unref (G_OBJECT (ioc));
	g_object_unref (G_OBJECT (input));

	return ret;
}

static void
image_content_received (GtkClipboard *clipboard, GtkSelectionData *sel,
			  gpointer closure)
{
	GnmGtkClipboardCtxt *ctxt = closure;
	WorkbookControlGUI *wbcg = ctxt->wbcg;
	GnmPasteTarget	   *pt   = ctxt->paste_target;

	if (sel->length > 0) {
		scg_paste_image (wbcg_cur_scg (wbcg), &pt->range, 
				 sel->data, sel->length);
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
table_content_received (GtkClipboard *clipboard, GtkSelectionData *sel,
			  gpointer closure)
{
	GnmGtkClipboardCtxt *ctxt = closure;
	WorkbookControlGUI *wbcg = ctxt->wbcg;
	WorkbookControl	   *wbc  = WORKBOOK_CONTROL (wbcg);
	GnmPasteTarget	   *pt   = ctxt->paste_target;
	GnmCellRegion *content = NULL;

	/* Nothing on clipboard? */
	if (sel->length < 0) {
		;
	} else if (sel->target == gdk_atom_intern (GNUMERIC_ATOM_NAME, 
						   FALSE)) {
		/* The data is the gnumeric specific XML interchange format */
		content = xml_cellregion_read (wbc, pt->sheet,
					       sel->data, sel->length);
	} else if ((sel->target == gdk_atom_intern (OOO_ATOM_NAME, FALSE)) ||
		   (sel->target == gdk_atom_intern (OOO11_ATOM_NAME, FALSE))) {
		content = table_cellregion_read (wbc, "Gnumeric_OpenCalc:openoffice",
						 pt, sel->data,
						 sel->length);
	} else if (sel->target == gdk_atom_intern (HTML_ATOM_NAME, FALSE)) {
		content = table_cellregion_read (wbc, "Gnumeric_html:html",
						 pt, sel->data,
						 sel->length);
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
		OOO_ATOM_NAME,
		OOO11_ATOM_NAME,
		HTML_ATOM_NAME,
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
		g_free (ctxt->paste_target);
		g_free (ctxt);
		return;
	}

#if 0
	for (j = 0; j < n_targets && table_atom == GDK_NONE; j++)
		puts (gdk_atom_name (targets[j]));
#endif
	
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
		GList *l;
		GtkTargetPair *tp;
		gboolean found = FALSE;

		gtk_target_list_add_image_targets (tl, 0, FALSE);

		/* Find an image format */
		for (i = 0 ; i < n_targets && !found; i++) {
			for (l = tl->list; l && !found; l = l->next) {
				tp = (GtkTargetPair *)l->data;
				if (tp->target == targets[i]) {
					ctxt->image_atom = targets[i];
					found = TRUE;
				}
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
table_cellregion_write (WorkbookControl *wbc, GnmCellRegion *cr,
			char * saver_id, int *size)
{
	guchar *ret = NULL;
	const GOFileSaver *saver = go_file_saver_for_id (saver_id);
	GsfOutput *output;
	IOContext *ioc;
	Workbook *wb;
	WorkbookView *wb_view;
	Sheet *sheet;
	GnmPasteTarget pt;
	GnmRange r;
	
	*size = 0;
	if (!saver)
		return NULL;

	output = gsf_output_memory_new ();
	ioc = gnumeric_io_context_new (GO_CMD_CONTEXT (wbc));
	wb = workbook_new_with_sheets (1);
	wb_view = workbook_view_new (wb);

	sheet = (Sheet *) workbook_sheets (wb)->data;
	memset (&r, 0, sizeof r);
	r.end.col = cr->cols - 1;
	r.end.row = cr->rows - 1;
	
	paste_target_init (&pt, sheet, &r, PASTE_ALL_TYPES);
	if (clipboard_paste_region (cr, &pt, GO_CMD_CONTEXT (wbc)) == FALSE) {
		go_file_saver_save (saver, ioc, wb_view, output);
		if (!gnumeric_io_error_occurred (ioc)) {
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
image_write (GnmCellRegion *cr, Sheet *sheet, gchar const *mime_type,
	     int *size)
{
	guchar *ret = NULL;
	SheetObject *so;
	char *format;
	GsfOutput *output;
	GsfOutputMemory *omem;
	gsf_off_t osize;

	*size = -1;

	g_return_val_if_fail (cr->objects != NULL, NULL);
	so = SHEET_OBJECT (cr->objects->data);
	g_return_val_if_fail (so != NULL, NULL);

	if (strncmp (mime_type, "image/", 6) != 0)
		return ret;
	if (!IS_SHEET_OBJECT_IMAGEABLE (so)) {
		g_warning ("non imageable object requested as image\n");
		return ret;
	}
	format = mime_type + 6;
	output = gsf_output_memory_new ();
	omem   = GSF_OUTPUT_MEMORY (output);
	sheet_object_write_image (so, format, output, NULL);
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

	return ret;
}

/**
 * x_clipboard_get_cb
 *
 * Callback invoked when another application requests we render the selection.
 */
static void
x_clipboard_get_cb (GtkClipboard *gclipboard, GtkSelectionData *selection_data,
		    guint info, WorkbookControlGUI *wbcg)
{
	gboolean to_gnumeric = FALSE, content_needs_free = FALSE;
	GnmCellRegion *clipboard = gnm_app_clipboard_contents_get ();
	Sheet *sheet = gnm_app_clipboard_sheet_get ();
	GnmRange const *a = gnm_app_clipboard_area_get ();
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	gchar const* target_name = gdk_atom_name (selection_data->target);

	/*
	 * Not sure how to handle this, not sure what purpose this has has
	 * (sheet being NULL). I think it is here to indicate that the selection
	 * just has been cut.
	 */
	if (!sheet)
		return;

	/*
	 * If the content was marked for a cut we need to copy it for pasting
	 * we clear it later on, because if the other application (the one that
	 * requested we render the data) is another instance of gnumeric
	 * we need the selection to remain "intact" (not cleared) so we can
	 * render it to the Gnumeric XML clipboard format
	 */
	if (clipboard == NULL) {
		content_needs_free = TRUE;
		clipboard = clipboard_copy_range (sheet, a);
	}

	g_return_if_fail (clipboard != NULL);

	/*
	 * Check whether the other application wants gnumeric XML format
	 * in fact we only have to check the 'info' variable, however
	 * to be absolutely sure I check if the atom checks out too
	 */
	if (selection_data->target == gdk_atom_intern (GNUMERIC_ATOM_NAME,
						       FALSE)) {
		int buffer_size;

		xmlChar *buffer = xml_cellregion_write (wbc, clipboard, &buffer_size);
		gtk_selection_data_set (selection_data, selection_data->target, 8,
					(guchar *) buffer, buffer_size);
		xmlFree (buffer);
		to_gnumeric = TRUE;
	} else if (selection_data->target == gdk_atom_intern (HTML_ATOM_NAME,
							      FALSE)) {
		char *saver_id = (char *) "Gnumeric_html:xhtml_range";
		int buffer_size;
		guchar *buffer = table_cellregion_write (wbc, clipboard,
							   saver_id,
							   &buffer_size);
		gtk_selection_data_set (selection_data,
					selection_data->target, 8,
					(guchar *) buffer, buffer_size);
		g_free (buffer);
	} else if (strncmp (target_name, "image/", 6) == 0) {
		int buffer_size;
		guchar *buffer = image_write (clipboard, sheet, target_name,
					      &buffer_size);
		gtk_selection_data_set (selection_data,
					selection_data->target, 8,
					(guchar *) buffer, buffer_size);
		g_free (buffer);
	} else {
		PangoContext *context = gtk_widget_get_pango_context (GTK_WIDGET (wbcg_toplevel (wbcg)));
		char *rendered_selection = cellregion_to_string (clipboard, context);

		if (rendered_selection == NULL)
			rendered_selection = g_strdup ("");
		gtk_selection_data_set_text (selection_data, 
					     (gchar *) rendered_selection,
					     strlen (rendered_selection));

		g_free (rendered_selection);
	}

	/*
	 * If this was a CUT operation we need to clear the content that was pasted
	 * into another application and release the stuff on the clipboard
	 */
	if (content_needs_free) {

		/* If the other app was a gnumeric, emulate a cut */
		if (to_gnumeric) {
			sheet_clear_region (sheet,
				a->start.col, a->start.row,
				a->end.col,   a->end.row,
				CLEAR_VALUES|CLEAR_COMMENTS|CLEAR_RECALC_DEPS,
				GO_CMD_CONTEXT (wbc));
			gnm_app_clipboard_clear (TRUE);
		}

		cellregion_unref (clipboard);
	}
}

/**
 * x_clipboard_clear_cb:
 *
 * Callback for the "we lost the X selection" signal.
 */
static gint
x_clipboard_clear_cb (GtkClipboard *clipboard,
		      gpointer      data)
{
	gnm_app_clipboard_clear (FALSE);

	return TRUE;
}

void
x_request_clipboard (WorkbookControlGUI *wbcg, GnmPasteTarget const *pt)
{
	GnmGtkClipboardCtxt *ctxt;
	GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (wbcg_toplevel (wbcg)));
	GtkClipboard *clipboard =
		gtk_clipboard_get_for_display
		(display,
		 gnm_app_prefs->prefer_clipboard_selection
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

gboolean
x_claim_clipboard (WorkbookControlGUI *wbcg)
{
	gboolean imageable = FALSE;
	GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (wbcg_toplevel (wbcg)));
	GnmCellRegion *content = gnm_app_clipboard_contents_get ();
	GtkTargetEntry *targets;
	int n_targets;

	static GtkTargetEntry const table_targets[] = {
		{ (char *) GNUMERIC_ATOM_NAME,  GTK_TARGET_SAME_WIDGET, GNUMERIC_ATOM_INFO },
		{ (char *)"text/html", 0, 0 },
		{ (char *)"UTF8_STRING", 0, 0 },
		{ (char *)"COMPOUND_TEXT", 0, 0 },
		{ (char *)"STRING", 0, 0 },
	};
	static GtkTargetEntry const image_targets[] = {
		{ (char *) GNUMERIC_ATOM_NAME,  GTK_TARGET_SAME_WIDGET, GNUMERIC_ATOM_INFO },
		{ (char *)"image/png", 0, 0 },
		{ (char *)"image/jpeg", 0, 0 },
		{ (char *)"image/bmp", 0, 0 },
	};

	if ((content->cols <= 0 || content->rows <= 0) &&
	    content->objects != NULL &&
	    IS_SHEET_OBJECT_IMAGEABLE (content->objects->data))
		imageable = TRUE;
	if (imageable) {
		targets = (GtkTargetEntry *) image_targets;
		n_targets = G_N_ELEMENTS (image_targets);
	} else {
		targets = (GtkTargetEntry *) table_targets;
		n_targets = G_N_ELEMENTS (table_targets);
	}
	return
	gtk_clipboard_set_with_owner (
		gtk_clipboard_get_for_display (display, GDK_SELECTION_CLIPBOARD),
		targets, n_targets,
		(GtkClipboardGetFunc) x_clipboard_get_cb,
		(GtkClipboardClearFunc) x_clipboard_clear_cb,
		G_OBJECT (wbcg)) &&
	gtk_clipboard_set_with_owner (
		gtk_clipboard_get_for_display (display, GDK_SELECTION_PRIMARY),
		targets, n_targets,
		(GtkClipboardGetFunc) x_clipboard_get_cb,
		(GtkClipboardClearFunc) x_clipboard_clear_cb,
		G_OBJECT (wbcg));
}
