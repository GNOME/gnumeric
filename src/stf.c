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
#include "sheet-style.h"
#include "style.h"
#include "mstyle.h"
#include "formats.h"
#include "io-context-priv.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "dialog-stf.h"
#include "dialog-stf-export.h"

#include <gsf/gsf-input.h>
#include <ctype.h>
#include <string.h>
#include <glade/glade.h>

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
stf_open_and_read (GsfInput *input)
{
	gpointer result;
	gulong    allocsize;
	size_t    readsize;  
	gsf_off_t size = gsf_input_size (input);

	readsize = (size_t) size;
	if ((gsf_off_t) readsize != size) /* Check for overflow */
		return NULL;
	size++;
	allocsize = (gulong) size;
	if ((gsf_off_t) allocsize != size) /* Check for overflow */
		return NULL;
	result = g_try_malloc (allocsize);
	if (result == NULL)
		return NULL;

	if (gsf_input_read (input, readsize, result) == NULL) {
		g_free (result);
		result = NULL;
	}

	return result;
}

static char *
stf_preparse (IOContext *context, GsfInput *input)
{
	char *data = stf_open_and_read (input);
	unsigned char const *c;
	int len;

	if (!data) {
		if (context)
			gnumeric_io_error_read (context,
						_("Error while trying to read file"));
		return NULL;
	}

	len = stf_parse_convert_to_unix (data);
	if (len < 0) {
		g_free (data);
		if (context)
			gnumeric_io_error_read (context,
						_("Error while trying to pre-convert file"));
		return NULL;
	}

	if ((c = stf_parse_is_valid_data (data, len)) != NULL) {
		if (context) {
			char *message = g_strdup_printf (_("This file does not seem to be a valid text file.\nThe character '%c' (ASCII decimal %d) was encountered.\nMost likely your locale settings are wrong."),
							 *c, (int) *c);
			gnumeric_io_error_read (context, message);
			g_free (message);
		}
		g_free (data);
		return NULL;
	}

	return data;
}

/**
 * stf_read_workbook
 * @fo       : file opener
 * @context  : command context
 * @book     : workbook
 * @input    : file to read from+convert
 *
 * Main routine, handles importing a file including all dialog mumbo-jumbo
 **/
static void
stf_read_workbook (GnumFileOpener const *fo, IOContext *context, WorkbookView *wbv, GsfInput *input)
{
	DialogStfResult_t *dialogresult = NULL;
	char *name;
	char *data;
	Sheet *sheet;
	Workbook *book;

	book = wb_view_workbook (wbv);

	data = stf_preparse (context, input);
	if (!data)
		return;

	/* Add Sheet */
	name = g_path_get_basename (gsf_input_name (input));
	sheet = sheet_new (book, name);

	workbook_sheet_attach (book, sheet, NULL);

	/* FIXME : how to do this cleanly ? */
	if (IS_WORKBOOK_CONTROL_GUI (context->impl))
		dialogresult = stf_dialog (WORKBOOK_CONTROL_GUI (context->impl), name, data);

	if (dialogresult != NULL) {
		GSList *iterator;
		int col, rowcount;

		stf_parse_options_set_lines_to_parse (dialogresult->parseoptions, dialogresult->lines);

		iterator = dialogresult->formats;
		col = 0;
		rowcount = stf_parse_get_rowcount (dialogresult->parseoptions, dialogresult->newstart);
		while (iterator) {
			Range range;
			MStyle *style = mstyle_new ();

			mstyle_set_format (style, iterator->data);

			range.start.col = col;
			range.start.row = 0;
			range.end.col   = col;
			range.end.row   = rowcount;

			sheet_style_apply_range (sheet, &range, style);

			iterator = g_slist_next (iterator);

			col++;
		}

		if (!stf_parse_sheet (dialogresult->parseoptions, dialogresult->newstart, sheet)) {

			workbook_sheet_detach (book, sheet);
			g_free (data);
			gnumeric_io_error_read (context, _("Parse error while trying to parse data into sheet"));
			return;
		}

		workbook_recalc (book);
		sheet_calc_spans (sheet, SPANCALC_RENDER);
		workbook_set_saveinfo (book, name, FILE_FL_MANUAL, NULL);
	} else
		workbook_sheet_detach (book, sheet);

	g_free (data);
	g_free (name);

	if (dialogresult != NULL)
		stf_dialog_result_free (dialogresult);
	else
		gnumeric_io_error_unknown (context);
}

#define STF_PROBE_SIZE 16384

/**
 * stf_read_workbook_auto_csvtab
 * @fo       : file opener
 * @context  : command context
 * @book     : workbook
 * @input    : file to read from+convert
 *
 * Attempt to auto-detect CSV or tab-delimited file
 **/
static void
stf_read_workbook_auto_csvtab (GnumFileOpener const *fo, IOContext *context,
			       WorkbookView *wbv, GsfInput *input)
{
	Sheet *sheet;
	Workbook *book;
	char *name;
	char *data;
	StfParseOptions_t *po;
	char *pos;
	unsigned int comma = 0, tab = 0, lines = 0;
	int i;

	book = wb_view_workbook (wbv);
	data = stf_preparse (context, input);
	if (!data)
		return;

        for (i = STF_PROBE_SIZE, pos = data ; *pos && i-- > 0; ++pos )
		if (*pos == ',')
			++comma;
		else if (*pos == '\t')
			++tab;
		else if (*pos == '\n')
			++lines;

	name = g_path_get_basename (gsf_input_name (input));
	sheet = sheet_new (book, name);

	workbook_sheet_attach (book, sheet, NULL);

	po = stf_parse_options_new ();

	stf_parse_options_set_type (po, PARSE_TYPE_CSV);
	stf_parse_options_set_trim_spaces (po, TRIM_TYPE_LEFT | TRIM_TYPE_RIGHT);
	stf_parse_options_set_lines_to_parse (po, -1);

	stf_parse_options_csv_set_stringindicator (po, '"');
	stf_parse_options_csv_set_indicator_2x_is_single (po, FALSE);
	stf_parse_options_csv_set_duplicates (po, FALSE);

	/* Guess */
	stf_parse_options_csv_set_separators (po, ",", NULL);
	if (tab >= lines && tab > comma)
		stf_parse_options_csv_set_separators (po, "\t", NULL);

	if (!stf_parse_sheet (po, data, sheet)) {

		workbook_sheet_detach (book, sheet);
		g_free (data);
		stf_parse_options_free (po);
		gnumeric_io_error_read (context, _("Parse error while trying to parse data into sheet"));
		return;
	}
	stf_parse_options_free (po);

	workbook_recalc (book);
	sheet_calc_spans (sheet, SPANCALC_RENDER);
	workbook_set_saveinfo (book, name, FILE_FL_MANUAL, NULL);

	g_free (name);
	g_free (data);
}

static gboolean
stf_read_default_probe (GnumFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	guint8 const *data;
	gsf_off_t remain;
	int len;

	if (gsf_input_seek (input, 0, G_SEEK_SET))
		return FALSE;
	remain = gsf_input_remaining (input);
	len = MIN (remain, STF_PROBE_SIZE);
	data = gsf_input_read (input, len, NULL);
	return (data != NULL) && (stf_parse_is_valid_data (data, len) == NULL);
}

#ifndef PAGE_SIZE
#define PAGE_SIZE (BUFSIZ*8)
#endif

/***********************************************************************************/

static FILE *
stf_open_for_write (IOContext *context, const char *filename)
{
	FILE *f = gnumeric_fopen (context, filename, "w");

	if (!f)
		return NULL;

	setvbuf (f, NULL, _IOFBF, PAGE_SIZE);

	return f;
}

static gboolean
stf_write_func (const char *string, FILE *f)
{
	return (fputs (string, f) >= 0);
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
stf_write_workbook (GnumFileSaver const *fs, IOContext *context, WorkbookView *wbv, const char *filename)
{
	StfE_Result_t *result = NULL;

	g_return_if_fail (context != NULL);
	g_return_if_fail (wbv != NULL);
	g_return_if_fail (filename != NULL);

	if (IS_WORKBOOK_CONTROL_GUI (context->impl))
		result = stf_export_dialog (WORKBOOK_CONTROL_GUI (context->impl),
		         wb_view_workbook (wbv));

	if (result != NULL) {
		FILE *f = stf_open_for_write (context, filename);

		if (f == NULL) {
			gnumeric_io_error_unknown (context);
			return;
		}

		stf_export_options_set_write_callback (result->export_options,
						       (StfEWriteFunc) stf_write_func, (gpointer) f);
		if (stf_export (result->export_options) == FALSE) {
			gnumeric_io_error_read (context,
			_("Error while trying to write csv file"));
			stf_export_dialog_result_free (result);
			return;
		}

		fclose (f);
		stf_export_dialog_result_free (result);
	} else
		gnumeric_io_error_unknown (context);
}

void
stf_init (void)
{
	register_file_opener (gnum_file_opener_new (
		"Gnumeric_stf:stf_csvtab",
		_("Text import (auto-detect CSV or tab-delimited)"),
		stf_read_default_probe, stf_read_workbook_auto_csvtab), 0);
	register_file_opener_as_importer_as_default (gnum_file_opener_new (
		"Gnumeric_stf:stf_druid",
		_("Text import (configurable)"),
		NULL, stf_read_workbook), 50);
	register_file_saver (gnum_file_saver_new (
		"Gnumeric_stf:stf", "csv",
		_("Text export (configurable)"),
		FILE_FL_MANUAL, stf_write_workbook));
}
