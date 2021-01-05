/*
 * stf.c : Utilizes the stf-parse engine and the dialog-stf to provide a plug-in for
 *         importing text files with a structure (CSV/fixed width)
 *
 * Copyright (C) Almer. S. Tigelaar <almer@gnome.org>
 * Copyright (C) 1999-2009 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <stf.h>
#include <stf-export.h>

#include <goffice/goffice.h>
#include <cell.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <style.h>
#include <mstyle.h>
#include <command-context.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <workbook.h>
#include <dialogs/dialog-stf.h>
#include <dialogs/dialog-stf-export.h>
#include <position.h>
#include <expr.h>
#include <value.h>
#include <gnm-format.h>
#include <selection.h>
#include <ranges.h>
#include <clipboard.h>
#include <parse-util.h>
#include <commands.h>
#include <gui-util.h>
#include <gutils.h>

#include <gsf/gsf-input.h>
#include <string.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-output-memory.h>
#include <gsf/gsf-utils.h>
#include <locale.h>

static void
stf_warning (GOIOContext *context, char const *msg)
{
	/*
	 * Using go_cmd_context_error_import will destroy the
	 * successfully imported portion.  We ought to have a
	 * way to issue a warning.
	 */
	if (GNM_IS_WBC_GTK (context->impl))
		go_gtk_notice_dialog
			(wbcg_toplevel (WBC_GTK (context->impl)),
			 GTK_MESSAGE_WARNING,
			 "%s", msg);
	else
		g_warning ("%s", msg);
}


/*
 * stf_open_and_read:
 * @filename: name of the file to open&read
 *
 * Will open filename, read the file into a g_alloced memory buffer
 *
 * NOTE : The returned buffer has to be g_freed by the calling routine.
 *
 * returns : a buffer containing the file contents
 */
static char *
stf_open_and_read (G_GNUC_UNUSED GOIOContext *context, GsfInput *input, size_t *readsize)
{
	gpointer result;
	gulong    allocsize;
	gsf_off_t size = gsf_input_size (input);

	if (gsf_input_seek (input, 0, G_SEEK_SET))
		return NULL;

	*readsize = (size_t) size;
	if ((gsf_off_t) *readsize != size) /* Check for overflow */
		return NULL;
	size++;
	allocsize = (gulong) size;
	if ((gsf_off_t) allocsize != size) /* Check for overflow */
		return NULL;
	result = g_try_malloc (allocsize);
	if (result == NULL)
		return NULL;

	*((char *)result + *readsize) = '\0';

	if (*readsize > 0 && gsf_input_read (input, *readsize, result) == NULL) {
		g_warning ("gsf_input_read failed.");
		g_free (result);
		result = NULL;
	}
	return result;
}

static char *
stf_preparse (GOIOContext *context, GsfInput *input, size_t *data_len)
{
	char *data;

	data = stf_open_and_read (context, input, data_len);

	if (!data) {
		if (context)
			go_cmd_context_error_import (GO_CMD_CONTEXT (context),
				_("Error while trying to read file"));
		return NULL;
	}

	return data;
}

static gboolean
stf_store_results (DialogStfResult_t *dialogresult,
		   Sheet *sheet, int start_col, int start_row)
{
	return stf_parse_sheet (dialogresult->parseoptions,
				dialogresult->text, NULL, sheet,
				start_col, start_row);
}

static void
resize_columns (Sheet *sheet)
{
	GnmRange r;

	if (gnm_debug_flag ("stf"))
		g_printerr ("Auto-fitting columns...\n");

	/* If we have lots of rows, auto-fitting will take a very long
	   time.  It is probably better to look at only, say, 1000 rows
	   of data.  */
	range_init_full_sheet (&r, sheet);
	r.end.row = MIN (r.end.row, 1000);

	colrow_autofit (sheet, &r, TRUE,
			TRUE, /* Ignore strings */
			TRUE, /* Don't shrink */
			TRUE, /* Don't shrink */
			NULL, NULL);
	if (gnm_debug_flag ("stf"))
		g_printerr ("Auto-fitting columns...  done\n");

	sheet_queue_respan (sheet, 0, gnm_sheet_get_last_row (sheet));
}


/*
 * stf_read_workbook:
 * @fo: file opener
 * @enc: encoding of file
 * @context: command context
 * @book: workbook
 * @input: file to read from+convert
 *
 * Main routine, handles importing a file including all dialog mumbo-jumbo
 */
static void
stf_read_workbook (G_GNUC_UNUSED GOFileOpener const *fo,  gchar const *enc,
		   GOIOContext *context, GoView *view, GsfInput *input)
{
	DialogStfResult_t *dialogresult = NULL;
	char *name, *nameutf8 = NULL;
	char *data = NULL;
	size_t data_len;
	WorkbookView *wbv = GNM_WORKBOOK_VIEW (view);

	if (!GNM_IS_WBC_GTK (context->impl)) {
		go_io_error_string (context, _("This importer can only be used with a GUI."));
		return;
	}

	name = g_path_get_basename (gsf_input_name (input));
	nameutf8 = g_filename_to_utf8 (name, -1, NULL, NULL, NULL);
	g_free (name);
	if (!nameutf8) {
		g_warning ("Failed to convert filename to UTF-8.  This shouldn't happen here.");
		goto out;
	}

	data = stf_preparse (context, input, &data_len);
	if (!data)
		goto out;

	dialogresult = stf_dialog (WBC_GTK (context->impl),
				   enc, FALSE, NULL, FALSE,
				   nameutf8, data, data_len);
	if (dialogresult != NULL) {
		Workbook *book = wb_view_get_workbook (wbv);
		int cols = dialogresult->colcount, rows = dialogresult->rowcount;
		Sheet *sheet;

		gnm_sheet_suggest_size (&cols, &rows);
		sheet = sheet_new (book, nameutf8, cols, rows);
		workbook_sheet_attach (book, sheet);
		if (stf_store_results (dialogresult, sheet, 0, 0)) {
			workbook_recalc_all (book);
			resize_columns (sheet);
			workbook_set_saveinfo
				(book,
				 GO_FILE_FL_WRITE_ONLY,
				 go_file_saver_for_id
				 ("Gnumeric_stf:stf_assistant"));
		} else {
			/* the user has cancelled */
			/* the caller should notice that we have no sheets */
			workbook_sheet_delete (sheet);
		}
	}

 out:
	g_free (nameutf8);
	g_free (data);
	if (dialogresult != NULL)
		stf_dialog_result_free (dialogresult);
}

static GnmValue *
cb_get_content (GnmCellIter const *iter, GsfOutput *buf)
{
	GnmCell *cell;

	if (NULL != (cell = iter->cell)) {
		char *tmp;
		if (gnm_cell_has_expr (cell))
			tmp = gnm_expr_top_as_string (cell->base.texpr,
				&iter->pp, iter->pp.sheet->convs);
		else if (VALUE_FMT (cell->value) != NULL)
			tmp = format_value (NULL, cell->value, -1,
				workbook_date_conv (iter->pp.wb));
		else
			tmp = value_get_as_string (cell->value);

		gsf_output_write (buf, strlen (tmp), tmp);
		g_free (tmp);
	}
	gsf_output_write (buf, 1, "\n");

	return NULL;
}

/**
 * stf_text_to_columns:
 * @wbc: The control making the request
 * @cc:
 *
 * Main routine, handles importing a file including all dialog mumbo-jumbo
 **/
void
stf_text_to_columns (WorkbookControl *wbc, GOCmdContext *cc)
{
	DialogStfResult_t *dialogresult = NULL;
	SheetView	*sv;
	Sheet		*src_sheet, *target_sheet;
	GnmRange const	*src;
	GnmRange	 target;
	GsfOutput	*buf;
	guint8 const	*data;
	size_t data_len;

	sv    = wb_control_cur_sheet_view (wbc);
	src_sheet = sv_sheet (sv);
	src = selection_first_range (sv, cc, _("Text to Columns"));
	if (src == NULL)
		return;
	if (range_width	(src) > 1) {
		go_cmd_context_error (cc, g_error_new (go_error_invalid (), 0,
			_("Only one column of input data can be parsed at a time")));
		return;
	}

	/* FIXME : how to do this cleanly ? */
	if (!GNM_IS_WBC_GTK (wbc))
		return;

#warning Add UI for this
	target_sheet = src_sheet;
	target = *src;
	range_translate (&target, target_sheet, 1, 0);

	buf = gsf_output_memory_new ();
	sheet_foreach_cell_in_range (src_sheet, CELL_ITER_ALL, src,
				     (CellIterFunc) &cb_get_content, buf);

	gsf_output_close (buf);
	data = gsf_output_memory_get_bytes (GSF_OUTPUT_MEMORY (buf));
	data_len = (size_t)gsf_output_size (buf);
	if (data_len == 0) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (cc),
					     _("There is no data "
					       "to convert"));
	} else {
		dialogresult = stf_dialog (WBC_GTK (wbc),
					   NULL, FALSE, NULL, FALSE,
					   _("Text to Columns"),
					   data, data_len);
	}
	if (dialogresult != NULL) {
		GnmCellRegion *cr = stf_parse_region (dialogresult->parseoptions,
			dialogresult->text, NULL, target_sheet->workbook);
		if (cr != NULL) {
			stf_dialog_result_attach_formats_to_cr (dialogresult, cr);
			target.end.col = target.start.col + cr->cols - 1;
			target.end.row = target.start.row + cr->rows - 1;
		}
		if (cr == NULL ||
		    cmd_text_to_columns (wbc, src, src_sheet,
					 &target, target_sheet, cr))
			go_cmd_context_error_import (GO_CMD_CONTEXT (cc),
					     _("Error while trying to "
					       "parse data into sheet"));
		stf_dialog_result_free (dialogresult);
	}

	g_object_unref (buf);
}

static void
clear_stray_NULs (GOIOContext *context, GString *utf8data)
{
	char *cpointer, *endpointer;
	int null_chars = 0;
	char const *valid_end;

	cpointer = utf8data->str;
	endpointer = utf8data->str + utf8data->len;
	while (*cpointer != 0)
		cpointer++;
	while (cpointer != endpointer) {
		null_chars++;
		*cpointer = ' ';
		while (*cpointer != 0)
			cpointer++;
	}
	if (null_chars > 0) {
		gchar const *format;
		gchar *msg;
		format = ngettext ("The file contains %d NUL character. "
				   "It has been changed to a space.",
				   "The file contains %d NUL characters. "
				   "They have been changed to spaces.",
				   null_chars);
		msg = g_strdup_printf (format, null_chars);
		stf_warning (context, msg);
		g_free (msg);
	}

	if (!g_utf8_validate (utf8data->str, utf8data->len, &valid_end)) {
		g_string_truncate (utf8data, valid_end - utf8data->str);
		stf_warning (context, _("The file contains invalid UTF-8 encoded characters and has been truncated"));
	}
}

/*
 * stf_read_workbook_auto_csvtab:
 * @fo: file opener
 * @enc: optional encoding
 * @context: command context
 * @book: workbook
 * @input: file to read from+convert
 *
 * Attempt to auto-detect CSV or tab-delimited file
 */
static void
stf_read_workbook_auto_csvtab (G_GNUC_UNUSED GOFileOpener const *fo, gchar const *enc,
			       GOIOContext *context,
			       GoView *view, GsfInput *input)
{
	Sheet *sheet;
	Workbook *book;
	char *name;
	char *data;
	GString *utf8data;
	size_t data_len;
	StfParseOptions_t *po;
	const char *gsfname;
	int cols, rows, i;
	GStringChunk *lines_chunk;
	GPtrArray *lines;
	WorkbookView *wbv = GNM_WORKBOOK_VIEW (view);

	g_return_if_fail (context != NULL);
	g_return_if_fail (wbv != NULL);

	book = wb_view_get_workbook (wbv);

	data = stf_preparse (context, input, &data_len);
	if (!data)
		return;

	enc = go_guess_encoding (data, data_len, enc, &utf8data, NULL);
	g_free (data);

	if (!enc) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (context),
				     _("That file is not in the given encoding."));
		return;
	}

	clear_stray_NULs (context, utf8data);

	/*
	 * Try to get the filename we're reading from.  This is not a
	 * great way.
	 */
	gsfname = gsf_input_name (input);

	{
		const char *ext = gsf_extension_pointer (gsfname);
		gboolean iscsv = ext && strcasecmp (ext, "csv") == 0;
		if (iscsv)
			po = stf_parse_options_guess_csv (utf8data->str);
		else
			po = stf_parse_options_guess (utf8data->str);
	}

	lines_chunk = g_string_chunk_new (100 * 1024);
	lines = stf_parse_general (po, lines_chunk,
				   utf8data->str, utf8data->str + utf8data->len);
	rows = lines->len;
	cols = 0;
	for (i = 0; i < rows; i++) {
		GPtrArray *line = g_ptr_array_index (lines, i);
		cols = MAX (cols, (int)line->len);
	}
	gnm_sheet_suggest_size (&cols, &rows);
	stf_parse_general_free (lines);
	g_string_chunk_free (lines_chunk);

	name = g_path_get_basename (gsfname);
	sheet = sheet_new (book, name, cols, rows);
	g_free (name);
	workbook_sheet_attach (book, sheet);

	if (stf_parse_sheet (po, utf8data->str, NULL, sheet, 0, 0)) {
		gboolean is_csv;
		workbook_recalc_all (book);
		resize_columns (sheet);
		if (po->cols_exceeded || po->rows_exceeded) {
			stf_warning (context,
				     _("Some data did not fit on the "
				       "sheet and was dropped."));
		}
		is_csv = po->sep.chr && po->sep.chr[0] == ',';
		workbook_set_saveinfo
			(book,
			 GO_FILE_FL_WRITE_ONLY,
			 go_file_saver_for_id
			 (is_csv ? "Gnumeric_stf:stf_csv" : "Gnumeric_stf:stf_assistant"));
	} else {
		workbook_sheet_delete (sheet);
		go_cmd_context_error_import (GO_CMD_CONTEXT (context),
			_("Parse error while trying to parse data into sheet"));
	}


	stf_parse_options_free (po);
	g_string_free (utf8data, TRUE);
}

/***********************************************************************************/

static void
stf_write_csv (GOFileSaver const *fs, GOIOContext *context,
	       GoView const *view, GsfOutput *output)
{
	GPtrArray *sheets;
	WorkbookView *wbv = GNM_WORKBOOK_VIEW (view);

	GnmStfExport *config = g_object_new
		(GNM_STF_EXPORT_TYPE,
		 "sink", output,
		 "quoting-triggers", ", \t\n\"",
		 NULL);

	sheets = gnm_file_saver_get_sheets (fs, wbv, FALSE);
	if (sheets) {
		unsigned ui;
		for (ui = 0; ui < sheets->len; ui++) {
			Sheet *sheet = g_ptr_array_index (sheets, ui);
			gnm_stf_export_options_sheet_list_add (config, sheet);
		}
	}

	if (gnm_stf_export (config) == FALSE)
		go_cmd_context_error_import (GO_CMD_CONTEXT (context),
			_("Error while trying to write CSV file"));

	g_object_unref (config);
}

static gboolean
csv_tsv_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl)
{
	/* Rough and ready heuristic.  If the first N bytes have no
	 * unprintable characters this may be text */
	const gsf_off_t N = 512;

	if (pl == GO_FILE_PROBE_CONTENT) {
		guint8 const *header;
		gsf_off_t i;
		char const *enc = NULL;
		GString *header_utf8;
		char const *p;
		gboolean ok = TRUE;

		if (gsf_input_seek (input, 0, G_SEEK_SET))
			return FALSE;
		i = gsf_input_remaining (input);

		/* If someone ships us an empty file, accept it only if
		   it has a proper name.  */
		if (i == 0)
			return csv_tsv_probe (fo, input, GO_FILE_PROBE_FILE_NAME);

		if (i > N) i = N;
		if (NULL == (header = gsf_input_read (input, i, NULL)))
			return FALSE;

		enc = go_guess_encoding (header, i, NULL, &header_utf8, NULL);
		if (!enc)
			return FALSE;

		for (p = header_utf8->str; *p; p = g_utf8_next_char (p)) {
			gunichar uc = g_utf8_get_char (p);
			/* isprint might not be true for these: */
			if (uc == '\n' || uc == '\t' || uc == '\r')
				continue;
			/* Also, ignore a byte-order mark which may be used to
			 * indicate UTF-8; see
			 * http://en.wikipedia.org/wiki/Byte_Order_Mark for
			 * background.
			 */
			if (p == header_utf8->str && uc == 0x0000FEFF) {
				continue;
			}
			if (!g_unichar_isprint (uc)) {
				ok = FALSE;
				break;
			}
		}

		g_string_free (header_utf8, TRUE);
		return ok;
	} else {
		char const *name = gsf_input_name (input);
		if (name == NULL)
			return FALSE;
		name = gsf_extension_pointer (name);
		return (name != NULL &&
			(g_ascii_strcasecmp (name, "csv") == 0 ||
			 g_ascii_strcasecmp (name, "tsv") == 0 ||
			 g_ascii_strcasecmp (name, "txt") == 0));
	}
}

/**
 * stf_init: (skip)
 */
void
stf_init (void)
{
	GSList *suffixes = go_slist_create (
		g_strdup ("csv"),
		g_strdup ("tsv"),
		g_strdup ("txt"),
		NULL);
	GSList *mimes = go_slist_create (
		g_strdup ("application/tab-separated-values"),
		g_strdup ("text/comma-separated-values"),
		g_strdup ("text/csv"),
		g_strdup ("text/x-csv"),
		g_strdup ("text/spreadsheet"),
		g_strdup ("text/tab-separated-values"),
		NULL);
	GSList *mimes_txt = go_slist_create (
		g_strdup ("text/plain"),
		g_strdup ("text/csv"),
		g_strdup ("text/x-csv"),
		g_strdup ("text/comma-separated-values"),
		g_strdup ("text/tab-separated-values"),
		NULL);
	GOFileSaver *saver;
	GOFileOpener *opener;

	opener = go_file_opener_new_with_enc (
		"Gnumeric_stf:stf_csvtab",
		_("Comma or tab separated values (CSV/TSV)"),
		suffixes, mimes,
		csv_tsv_probe, stf_read_workbook_auto_csvtab);
	go_file_opener_register (opener, 0);
	g_object_unref (opener);

	opener = go_file_opener_new_with_enc (
		"Gnumeric_stf:stf_assistant",
		_("Text import (configurable)"),
		NULL, mimes_txt,
		NULL, stf_read_workbook);
	g_object_set (G_OBJECT (opener), "interactive-only", TRUE, NULL);
	go_file_opener_register (opener, 0);
	g_object_unref (opener);

	saver = gnm_stf_file_saver_create ("Gnumeric_stf:stf_assistant");
	/* Unlike the opener, the saver doesn't require interaction.  */
	go_file_saver_register (saver);
	g_object_unref (saver);

	saver = go_file_saver_new (
		"Gnumeric_stf:stf_csv", "csv",
		_("Comma separated values (CSV)"),
		GO_FILE_FL_MANUAL_REMEMBER, stf_write_csv);
	go_file_saver_set_save_scope (saver, GO_FILE_SAVE_SHEET);
	g_object_set (G_OBJECT (saver), "sheet-selection", TRUE, NULL);
	go_file_saver_register (saver);
	g_object_unref (saver);
}

/**
 * stf_shutdown: (skip)
 */
void
stf_shutdown (void)
{
	go_file_saver_unregister
		(go_file_saver_for_id ("Gnumeric_stf:stf_assistant"));
	go_file_saver_unregister
		(go_file_saver_for_id ("Gnumeric_stf:stf_csv"));

	go_file_opener_unregister
		(go_file_opener_for_id ("Gnumeric_stf:stf_csvtab"));
	go_file_opener_unregister
		(go_file_opener_for_id ("Gnumeric_stf:stf_assistant"));
}
