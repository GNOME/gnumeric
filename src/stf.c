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
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "stf.h"

#include "plugin-util.h"
#include "file.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "style.h"
#include "mstyle.h"
#include "io-context-priv.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "dialog-stf.h"
#include "dialog-stf-export.h"
#include "position.h"
#include "expr.h"
#include "value.h"
#include "format.h"
#include "selection.h"
#include "ranges.h"
#include "clipboard.h"
#include "parse-util.h"
#include "commands.h"

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
stf_preparse (CommandContext *context, GsfInput *input, size_t *data_len)
{
	char *data;

	data = stf_open_and_read (input, data_len);

	if (!data) {
		if (context)
			gnumeric_error_read (context,
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
	Range range;

	/* If we didn't use the stf dialog, then formats will be NULL */
	if (parseoptions->formats == NULL)
		return;

	range.start.col = col;
	range.start.row = start_row;
	range.end.col   = col;
	range.end.row   = end_row;

	for (ui = 0; ui < parseoptions->formats->len; ui++) {
		if (parseoptions->col_import_array == NULL ||
		    parseoptions->col_import_array[ui]) {
			MStyle *style = mstyle_new ();
			StyleFormat *sf = g_ptr_array_index 
				(parseoptions->formats, ui);
			mstyle_set_format (style, sf);
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
stf_read_workbook (GnmFileOpener const *fo,  gchar const *enc,
		   IOContext *context, WorkbookView *wbv, GsfInput *input)
{
	DialogStfResult_t *dialogresult = NULL;
	char *name, *nameutf8;
	char *data;
	size_t data_len;
	Sheet *sheet;
	Workbook *book;

	/* FIXME : how to do this cleanly ? */
	if (!IS_WORKBOOK_CONTROL_GUI (context->impl))
		return;

	name = g_path_get_basename (gsf_input_name (input));
	nameutf8 = g_filename_to_utf8 (name, -1, NULL, NULL, NULL);
	g_free (name);
	if (!nameutf8) {
		g_warning ("Failed to convert filename to UTF-8.  This shouldn't happen here.");
		return;
	}

	data = stf_preparse (COMMAND_CONTEXT (context), input, &data_len);
	if (!data) {
		g_free (nameutf8);
		return;
	}

	/* Add Sheet */
	book = wb_view_workbook (wbv);
	sheet = sheet_new (book, nameutf8);
	workbook_sheet_attach (book, sheet, NULL);

	dialogresult = stf_dialog (WORKBOOK_CONTROL_GUI (context->impl),
				   enc, FALSE, NULL, FALSE, 
				   nameutf8, data, data_len);
	if (dialogresult != NULL && stf_store_results (dialogresult, sheet, 0, 0)) {
		workbook_recalc (book);
		sheet_queue_respan (sheet, 0, SHEET_MAX_ROWS-1);
	} else {
		/* the user has cancelled */
                /* the caller should notice that we have no sheets */
		workbook_sheet_detach (book, sheet, TRUE);
	}

	g_free (data);
	g_free (nameutf8);
	if (dialogresult != NULL)
		stf_dialog_result_free (dialogresult);
}

static Value *
cb_get_content (Sheet *sheet, int col, int row,
		Cell *cell, GsfOutput *buf)
{
	char *tmp;
	if (cell_has_expr (cell)) {
		ParsePos pp;
		tmp = gnm_expr_as_string (cell->base.expression,
			parse_pos_init_cell (&pp, cell),
			gnm_expr_conventions_default);
	} else if (VALUE_FMT (cell->value) != NULL)
		tmp = format_value (NULL, cell->value, NULL, -1,
				    workbook_date_conv (sheet->workbook));
	else
		tmp = value_get_as_string (cell->value);

	gsf_output_write (buf, strlen (tmp), tmp);
	gsf_output_write (buf, 1, "\n");
	g_free (tmp);

	return NULL;
}

/**
 * stf_text_to_columns
 * @wbc  : The control making the request
 *
 * Main routine, handles importing a file including all dialog mumbo-jumbo
 **/
void
stf_text_to_columns (WorkbookControl *wbc, CommandContext *cc)
{
	DialogStfResult_t *dialogresult = NULL;
	SheetView	*sv;
	Sheet		*src_sheet, *target_sheet;
	Range const	*src;
	Range		 target;
	GsfOutputMemory	*buf;
	guint8 const	*data;
	size_t data_len;

	sv    = wb_control_cur_sheet_view (wbc);
	src_sheet = sv_sheet (sv);
	src = selection_first_range (sv, cc, _("Text to Columns"));
	if (src == NULL)
		return;
	if (range_width	(src) > 1) {
		cmd_context_error (cc, g_error_new (gnm_error_invalid (), 0,
			_("Only 1 one column of <b>input</b> data can be parsed at a time, not %d"),
			range_width (src)));
		return;
	}

	/* FIXME : how to do this cleanly ? */
	if (!IS_WORKBOOK_CONTROL_GUI (wbc))
		return;

#warning Add UI for this
	target_sheet = src_sheet;
	target = *src;
	range_translate (&target, 1, 0);

	buf = gsf_output_memory_new ();
	sheet_foreach_cell_in_range (src_sheet,
		CELL_ITER_IGNORE_BLANK | CELL_ITER_IGNORE_HIDDEN,
		src->start.col, src->start.row,
		src->end.col, src->end.row,
		(CellIterFunc) &cb_get_content, buf);

	gsf_output_close (GSF_OUTPUT (buf));
	data = gsf_output_memory_get_bytes (buf);
	data_len = (size_t)gsf_output_size (GSF_OUTPUT (buf));
	if (data_len == 0) {
		gnumeric_error_read (COMMAND_CONTEXT (cc),
					     _("There is no data "
					       "to convert"));
	} else {
		dialogresult = stf_dialog (WORKBOOK_CONTROL_GUI (wbc),
					   NULL, FALSE, NULL, FALSE,
					   _("Text to Columns"), 
					   data, data_len);
	}
	if (dialogresult != NULL) {
		CellRegion *cr = NULL;

		cr = stf_parse_region (dialogresult->parseoptions, dialogresult->text, NULL);
		if (cr != NULL) {
			stf_dialog_result_attach_formats_to_cr (dialogresult, cr);
			target.end.col = target.start.col + cr->cols - 1;
		}
		if (cr == NULL || cmd_text_to_columns (wbc, src, src_sheet, 
						      &target, target_sheet, cr))
			gnumeric_error_read (COMMAND_CONTEXT (cc),
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
stf_read_workbook_auto_csvtab (GnmFileOpener const *fo, gchar const *enc,
			       IOContext *context,
			       WorkbookView *wbv, GsfInput *input)
{
	Sheet *sheet;
	Workbook *book;
	char *name;
	char *data, *utf8data;
	size_t data_len;
	StfParseOptions_t *po;

	g_return_if_fail (context != NULL);
	g_return_if_fail (wbv != NULL);

	book = wb_view_workbook (wbv);
	data = stf_preparse (COMMAND_CONTEXT (context), input, &data_len);
	if (!data)
		return;

	enc = gnm_guess_encoding (data, data_len, enc, &utf8data);
	g_free (data);

	if (!enc) {
		gnumeric_error_read (COMMAND_CONTEXT (context),
				     _("That file is not in the given encoding."));
		return;
	}

        po = stf_parse_options_guess (utf8data);

	name = g_path_get_basename (gsf_input_name (input));
	sheet = sheet_new (book, name);
	g_free (name);
	workbook_sheet_attach (book, sheet, NULL);

	if (stf_parse_sheet (po, utf8data, NULL, sheet, 0, 0)) {
		workbook_recalc (book);
		sheet_queue_respan (sheet, 0, SHEET_MAX_ROWS-1);
	} else {
		workbook_sheet_detach (book, sheet, TRUE);
		gnumeric_error_read (COMMAND_CONTEXT (context),
			_("Parse error while trying to parse data into sheet"));
	}

	stf_parse_options_free (po);
	g_free (utf8data);
}

/***********************************************************************************/

static gboolean
stf_write_func (const char *string, GsfOutput *output)
{
	return (gsf_output_puts (output, string) >= 0);
}

/**
 * stf_write_workbook
 * @fs       : file saver
 * @context  : command context
 * @book     : workbook
 * @filename : file to read from+convert
 *
 * Main routine, handles exporting a file including all dialog mumbo-jumbo
 **/
static void
stf_write_workbook (GnmFileSaver const *fs, IOContext *context,
		    WorkbookView const *wbv, GsfOutput *output)
{
	StfE_Result_t *result = NULL;

	g_return_if_fail (context != NULL);
	g_return_if_fail (wbv != NULL);
	g_return_if_fail (output != NULL);

	if (IS_WORKBOOK_CONTROL_GUI (context->impl))
		result = stf_export_dialog (WORKBOOK_CONTROL_GUI (context->impl),
		         wb_view_workbook (wbv));

	if (result != NULL) {
		stf_export_options_set_write_callback (result->export_options,
						       (StfEWriteFunc) stf_write_func, (gpointer) output);
		if (stf_export (result->export_options) == FALSE) {
			gnumeric_error_read (COMMAND_CONTEXT (context),
				_("Error while trying to write csv file"));
			stf_export_dialog_result_free (result);
			return;
		}

		stf_export_dialog_result_free (result);
	} else
		gnumeric_io_error_unknown (context);
}

static gboolean
csv_tsv_probe (GnmFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	if (pl == FILE_PROBE_FILE_NAME) {
		char const *name = gsf_input_name (input);
		if (name == NULL)
			return FALSE;
		name = gsf_extension_pointer (name);
		return (name != NULL &&
		        (g_ascii_strcasecmp (name, "csv") == 0 ||
			 g_ascii_strcasecmp (name, "tsv") == 0));
	} else if (pl == FILE_PROBE_CONTENT)
		return TRUE;
	return FALSE;
}

void
stf_init (void)
{
	gnm_file_opener_register (gnm_file_opener_new_with_enc (
		"Gnumeric_stf:stf_csvtab",
		_("Comma or tab separated files (CSV/TSV)"),
		csv_tsv_probe, stf_read_workbook_auto_csvtab), 0);
	gnm_file_opener_register (gnm_file_opener_new_with_enc (
		"Gnumeric_stf:stf_druid",
		_("Text import (configurable)"),
		NULL, stf_read_workbook), 0);
	gnm_file_saver_register (gnm_file_saver_new (
		"Gnumeric_stf:stf", "csv",
		_("Text export (configurable)"),
		FILE_FL_WRITE_ONLY, stf_write_workbook));
}
