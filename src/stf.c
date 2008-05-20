/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * stf.c : Utilizes the stf-parse engine and the dialog-stf to provide a plug-in for
 *         importing text files with a structure (CSV/fixed width)
 *
 * Copyright (C) Almer. S. Tigelaar <almer@gnome.org>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "stf.h"

#include <goffice/app/file.h>
#include "cell.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "style.h"
#include "mstyle.h"
#include <goffice/app/io-context-priv.h>
#include <goffice/utils/go-glib-extras.h>
#include "command-context.h"
#include "wbc-gtk.h"
#include "workbook-view.h"
#include "workbook.h"
#include "dialog-stf.h"
#include "dialog-stf-export.h"
#include "position.h"
#include "expr.h"
#include "value.h"
#include "gnm-format.h"
#include "selection.h"
#include "ranges.h"
#include "clipboard.h"
#include "parse-util.h"
#include "commands.h"
#include "gui-util.h"

#include <gsf/gsf-input.h>
#include <string.h>
#include <glade/glade.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-output-memory.h>
#include <gsf/gsf-utils.h>
#include <locale.h>


/**
 * stf_open_and_read
 * @filename : name of the file to open&read
 *
 * Will open filename, read the file into a g_alloced memory buffer
 *
 * NOTE : The returned buffer has to be g_freed by the calling routine.
 *
 * returns : a buffer containing the file contents
 **/
static char *
stf_open_and_read (GsfInput *input, size_t *readsize)
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
stf_preparse (GOCmdContext *context, GsfInput *input, size_t *data_len)
{
	char *data;

	data = stf_open_and_read (input, data_len);

	if (!data) {
		if (context)
			go_cmd_context_error_import (context,
				_("Error while trying to read file"));
		return NULL;
	}

	return data;
}

static void
stf_apply_formats (StfParseOptions_t *parseoptions,
		   Sheet *sheet, int col, int start_row, int end_row)
{
	unsigned int ui;
	GnmRange range;

	/* If we didn't use the stf dialog, then formats will be NULL */
	if (parseoptions->formats == NULL)
		return;

	range.start.col = col;
	range.start.row = start_row;
	range.end.col   = col;
	range.end.row   = end_row;

	for (ui = 0; ui < parseoptions->formats->len; ui++) {
		if (parseoptions->col_import_array == NULL ||
		    parseoptions->col_import_array_len <= ui ||
		    parseoptions->col_import_array[ui]) {
			GnmStyle *style = gnm_style_new ();
			GOFormat *sf = g_ptr_array_index
				(parseoptions->formats, ui);
			gnm_style_set_format (style, sf);
			sheet_style_apply_range (sheet, &range, style);
			range.start.col++;
			range.end.col++;
		}
	}
}

static gboolean
stf_store_results (DialogStfResult_t *dialogresult,
		   Sheet *sheet, int start_col, int start_row)
{
	stf_apply_formats (dialogresult->parseoptions,
			   sheet, start_col, start_row,
			   start_row + dialogresult->rowcount - 1);
	return stf_parse_sheet (dialogresult->parseoptions,
				dialogresult->text, NULL, sheet,
				start_col, start_row);
}

/**
 * stf_read_workbook
 * @fo       : file opener
 * @enc      : encoding of file
 * @context  : command context
 * @book     : workbook
 * @input    : file to read from+convert
 *
 * Main routine, handles importing a file including all dialog mumbo-jumbo
 **/
static void
stf_read_workbook (GOFileOpener const *fo,  gchar const *enc,
		   IOContext *context, gpointer wbv, GsfInput *input)
{
	DialogStfResult_t *dialogresult = NULL;
	char *name, *nameutf8;
	char *data;
	size_t data_len;
	Sheet *sheet;
	Workbook *book;

	/* FIXME : how to do this cleanly ? */
	if (!IS_WBC_GTK (context->impl))
		return;

	name = g_path_get_basename (gsf_input_name (input));
	nameutf8 = g_filename_to_utf8 (name, -1, NULL, NULL, NULL);
	g_free (name);
	if (!nameutf8) {
		g_warning ("Failed to convert filename to UTF-8.  This shouldn't happen here.");
		return;
	}

	data = stf_preparse (GO_CMD_CONTEXT (context), input, &data_len);
	if (!data) {
		g_free (nameutf8);
		return;
	}

	/* Add Sheet */
	book = wb_view_get_workbook (wbv);
	sheet = sheet_new (book, nameutf8);
	workbook_sheet_attach (book, sheet);

	dialogresult = stf_dialog (WBC_GTK (context->impl),
				   enc, FALSE, NULL, FALSE,
				   nameutf8, data, data_len);
	if (dialogresult != NULL && stf_store_results (dialogresult, sheet, 0, 0)) {
		workbook_recalc_all (book);
		sheet_queue_respan (sheet, 0, gnm_sheet_get_max_rows (sheet)-1);
	} else {
		/* the user has cancelled */
                /* the caller should notice that we have no sheets */
		workbook_sheet_delete (sheet);
	}

	g_free (data);
	g_free (nameutf8);
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
			tmp = format_value (NULL, cell->value, NULL, -1,
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
 * stf_text_to_columns
 * @wbc  : The control making the request
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
			_("Only one column of <b>input</b> data can be parsed at a time")));
		return;
	}

	/* FIXME : how to do this cleanly ? */
	if (!IS_WBC_GTK (wbc))
		return;

#warning Add UI for this
	target_sheet = src_sheet;
	target = *src;
	range_translate (&target, 1, 0);

	buf = gsf_output_memory_new ();
	sheet_foreach_cell_in_range (src_sheet,
		CELL_ITER_ALL,
		src->start.col, src->start.row,
		src->end.col, src->end.row,
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

	g_object_unref (G_OBJECT (buf));
}

/**
 * stf_read_workbook_auto_csvtab
 * @fo       : file opener
 * @enc      : optional encoding
 * @context  : command context
 * @book     : workbook
 * @input    : file to read from+convert
 *
 * Attempt to auto-detect CSV or tab-delimited file
 **/
static void
stf_read_workbook_auto_csvtab (GOFileOpener const *fo, gchar const *enc,
			       IOContext *context,
			       gpointer wbv, GsfInput *input)
{
	Sheet *sheet;
	Workbook *book;
	char *name;
	char *data, *utf8data;
	size_t data_len;
	StfParseOptions_t *po;

	g_return_if_fail (context != NULL);
	g_return_if_fail (wbv != NULL);

	book = wb_view_get_workbook (wbv);
	data = stf_preparse (GO_CMD_CONTEXT (context), input, &data_len);
	if (!data)
		return;

	enc = go_guess_encoding (data, data_len, enc, &utf8data);
	g_free (data);

	if (!enc) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (context),
				     _("That file is not in the given encoding."));
		return;
	}

        po = stf_parse_options_guess (utf8data);

	name = g_path_get_basename (gsf_input_name (input));
	sheet = sheet_new (book, name);
	g_free (name);
	workbook_sheet_attach (book, sheet);

	if (stf_parse_sheet (po, utf8data, NULL, sheet, 0, 0)) {
		workbook_recalc_all (book);
		sheet_queue_respan (sheet, 0, gnm_sheet_get_max_rows (sheet)-1);
		if (po->cols_exceeded) {
			const char *msg =
				_("Some columns of data were"
				  " dropped since they exceeded"
				  " the available sheet size.");
			/*
			 * Using go_cmd_context_error_import will destroy the
			 * successfully imported portion.  We ought to have a
			 * way to issue a warning.
			 */
			if (IS_WBC_GTK (context->impl))
				go_gtk_notice_dialog
					(wbcg_toplevel (WBC_GTK (context->impl)),
					 GTK_MESSAGE_WARNING,
					 msg);
			else
				g_warning (msg);
#if 0
				go_cmd_context_error_import
					(GO_CMD_CONTEXT (context),
					 msg);
#endif
		}
	} else {
		workbook_sheet_delete (sheet);
		go_cmd_context_error_import (GO_CMD_CONTEXT (context),
			_("Parse error while trying to parse data into sheet"));
	}


	stf_parse_options_free (po);
	g_free (utf8data);
}

/***********************************************************************************/

static void
stf_write_csv (GOFileSaver const *fs, IOContext *context,
	       gconstpointer wbv, GsfOutput *output)
{
	Sheet *sheet;
	GnmRangeRef const *range;

	GnmStfExport *config = g_object_new
		(GNM_STF_EXPORT_TYPE,
		 "sink", output,
		 "quoting-triggers", ", \t\n\"",
		 NULL);

	/* FIXME: this is crap in both branches of the "if".  */
	range = g_object_get_data (G_OBJECT (wb_view_get_workbook (wbv)), "ssconvert-range");
	if (range && range->a.sheet)
		sheet = range->a.sheet;
	else
		sheet = wb_view_cur_sheet (wbv);

	gnm_stf_export_options_sheet_list_add (config, sheet);

	if (gnm_stf_export (config) == FALSE)
		go_cmd_context_error_import (GO_CMD_CONTEXT (context),
			_("Error while trying to write CSV file"));

	g_object_unref (config);
}

static gboolean
csv_tsv_probe (GOFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	/* Rough and ready heuristic.  If the first N bytes have no
	 * unprintable characters this may be text */
	const gsf_off_t N = 512;

	if (pl == FILE_PROBE_CONTENT) {
		guint8 const *header;
		gsf_off_t i;
		char const *enc = NULL;
		char *header_utf8;
		char const *p;
		int try;
		gboolean ok = TRUE;

		if (gsf_input_seek (input, 0, G_SEEK_SET))
			return FALSE;
		i = gsf_input_remaining (input);

		/* If someone ships us an empty file, accept it only if
		   it has a proper name.  */
		if (i == 0)
			return csv_tsv_probe (fo, input, FILE_PROBE_FILE_NAME);

		if (i > N) i = N;
		if (NULL == (header = gsf_input_read (input, i, NULL)))
			return FALSE;

		/*
		 * It is conceivable that encoding guessing could fail
		 * if our truncated buffer had partial characters.  We
		 * really need go_guess_encoding_truncated, but for now
		 * let's just try cutting a byte away at a time.
		 */
		for (try = 0; !enc && try < MIN (i, 6); try++)
			enc = go_guess_encoding (header, i - try, NULL, &header_utf8);
		if (!enc)
			return FALSE;

		for (p = header_utf8; *p; p = g_utf8_next_char (p)) {
			gunichar uc = g_utf8_get_char (p);
			/* isprint might not be true for these: */
			if (uc == '\n' || uc == '\t' || uc == '\r')
				continue;
			if (!g_unichar_isprint (uc)) {
				ok = FALSE;
				break;
			}
		}

		g_free (header_utf8);
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

void
stf_init (void)
{
	GSList *suffixes = go_slist_create (
		g_strdup ("csv"),
		g_strdup ("tsv"),
		g_strdup ("txt"),
		NULL);
	GSList *mimes = go_slist_create (
		g_strdup ("application/csv"),
		g_strdup ("application/tab-separated-values"),
		g_strdup ("text/comma-separated-values"),
		g_strdup ("text/csv"),
		g_strdup ("text/spreadsheet"),
		g_strdup ("text/tab-separated-values"),
		g_strdup ("text/x-comma-separated-values"),
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
		NULL, NULL,
		NULL, stf_read_workbook);
	go_file_opener_register (opener, 0);
	g_object_unref (opener);

	saver = gnm_stf_file_saver_new ("Gnumeric_stf:stf_assistant");
	go_file_saver_register (saver);
	g_object_unref (saver);

	saver = go_file_saver_new (
		"Gnumeric_stf:stf_csv", "csv",
		_("Comma separated values (CSV)"),
		FILE_FL_WRITE_ONLY, stf_write_csv);
	go_file_saver_set_save_scope (saver, FILE_SAVE_SHEET);
	go_file_saver_register (saver);
	g_object_unref (saver);
}

void
stf_shutdown (void)
{
	go_file_saver_unregister
		(go_file_saver_for_id ("Gnumeric_stf:stf_assistant"));

	go_file_opener_unregister
		(go_file_opener_for_id ("Gnumeric_stf:stf_csvtab"));
	go_file_opener_unregister
		(go_file_opener_for_id ("Gnumeric_stf:stf_assistant"));
}
