/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * stf-export.c : Structured Text Format Exporter (STF-E)
 *                Engine to construct CSV files
 *
 * Copyright (C) Almer. S. Tigelaar.
 * EMail: almer1@dds.nl or almer-t@bigfoot.com
 *
 * Based on the csv-io.c plugin by :
 *   Miguel de Icaza <miguel@gnu.org>
 *   Jody Goldberg   <jody@gnome.org>
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
#include "gnumeric.h"
#include "stf-export.h"

#include "sheet.h"
#include "cell.h"
#include "value.h"
#include <gsf/gsf-output-iconv.h>

#include <stdio.h>
#include <string.h>

/**
 * stf_export_options_new:
 *
 * Creates a new export options struct
 *
 * Return value: a new export options struct
 **/
StfExportOptions_t *
stf_export_options_new (void)
{
	StfExportOptions_t *export_options = g_new (StfExportOptions_t, 1);

	export_options->csv = g_object_new (GSF_OUTPUT_CSV_TYPE, NULL);
	export_options->sheet_list = NULL;
	export_options->preserve_format = FALSE;
	export_options->charset	= NULL;
	export_options->transliterate_mode = TRANSLITERATE_MODE_UNKNOWN;

	return export_options;
}

void
stf_export_options_free (StfExportOptions_t *export_options)
{
	stf_export_options_sheet_list_clear (export_options);
	g_free (export_options->charset);
	g_object_unref (export_options->csv);
	g_free (export_options);
}


/**
 * stf_export_options_set_transliterate_mode:
 * @export_options: an export options struct
 * @transliterate_mode: the quoting mode
 *
 * Sets the transliterate mode (trans/escape)
 **/
void
stf_export_options_set_transliterate_mode (StfExportOptions_t *export_options, StfTransliterateMode_t transliterate_mode)
{
	g_return_if_fail (export_options != NULL);
	g_return_if_fail (transliterate_mode >= 0 && transliterate_mode < TRANSLITERATE_MODE_UNKNOWN);

	export_options->transliterate_mode = transliterate_mode;
}

/**
 * stf_export_options_set_format_mode:
 * @export_options: an export options struct
 * @preserve_format: whether to preserve formats
 *
 * Sets the transliterate mode (trans/escape)
 **/
void
stf_export_options_set_format_mode (StfExportOptions_t *export_options, gboolean preserve_format)
{
	g_return_if_fail (export_options != NULL);

	export_options->preserve_format = preserve_format;
}

/**
 * stf_export_options_set_charset:
 * @export_options: an export options struct
 * @charset: charset selection string
 *
 * Select the export charset
 **/

void
stf_export_options_set_charset (StfExportOptions_t *export_options, char const * charset)
{
	g_return_if_fail (export_options != NULL);
 	g_return_if_fail (charset != NULL);

	export_options->charset = g_strdup (charset);
}

/**
 * stf_export_options_sheet_list_clear:
 * @export_options: an export options struct
 *
 * Clears the sheet list.
 **/
void
stf_export_options_sheet_list_clear (StfExportOptions_t *export_options)
{
	g_return_if_fail (export_options != NULL);

	g_slist_free (export_options->sheet_list);
	export_options->sheet_list = NULL;
}


/**
 * stf_export_options_sheet_list_add:
 * @export_options: an export options struct
 * @sheet: a gnumeric sheet
 *
 * Appends a @sheet to the list of sheets to be exported
 **/
void
stf_export_options_sheet_list_add (StfExportOptions_t *export_options, Sheet *sheet)
{
	g_return_if_fail (export_options != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	export_options->sheet_list = g_slist_append (export_options->sheet_list, sheet);
}

/*******************************************************************************************************
 * STF EXPORT : The routines that convert sheets > text
 *******************************************************************************************************/


/**
 * stf_export_cell:
 * @export_options: an export options struct
 * @cell: the cell to write to the file
 *
 * Return value: return TRUE on success, FALSE otherwise.
 **/
static gboolean
stf_export_cell (StfExportOptions_t *export_options, GnmCell *cell)
{
	char *text = NULL;
	gboolean ok;
	g_return_val_if_fail (export_options != NULL, FALSE);

	if (cell) {
		if (export_options->preserve_format) 
			text = cell_get_rendered_text (cell);
		else if (cell->value)
			text = value_get_as_string (cell->value);
	}
		
	ok = gsf_output_csv_write_field (export_options->csv,
					 text ? text : "",
					 -1);
	g_free (text);

	return ok;
}

/**
 * stf_export_sheet:
 * @export_options: an export options struct
 * @sheet: the sheet to export
 *
 * Writes the @sheet to the callback function
 *
 * Return value: returns TRUE on success, FALSE otherwise
 **/
static gboolean
stf_export_sheet (StfExportOptions_t *export_options, Sheet *sheet)
{
	int col, row;
	GnmRange r;

	g_return_val_if_fail (export_options != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	r = sheet_get_extent (sheet, FALSE);
	for (row = r.start.row; row <= r.end.row; row++) {
		for (col = r.start.col; col <= r.end.col; col++) {
			GnmCell *cell = sheet_cell_get (sheet, col, row);
			if (!stf_export_cell (export_options, cell))
				return FALSE;
		}
		if (!gsf_output_csv_write_eol (export_options->csv))
			return FALSE;
	}

	return TRUE;
}

/**
 * stf_export:
 * @export_options: an export options struct
 *
 * Exports the sheets given in @export_options
 *
 * Return value: TRUE on success, FALSE otherwise
 **/
gboolean
stf_export (StfExportOptions_t *export_options, GsfOutput *sink)
{
	GSList *ptr;

	g_return_val_if_fail (export_options != NULL, FALSE);
	g_return_val_if_fail (export_options->sheet_list != NULL, FALSE);
	g_return_val_if_fail (GSF_IS_OUTPUT (sink), FALSE);

	g_object_ref (sink);

	if (export_options->charset &&
	    strcmp (export_options->charset, "UTF-8") != 0) {
		char *charset;			
		GsfOutput *converter;

		switch (export_options->transliterate_mode) {
		default:
		case TRANSLITERATE_MODE_ESCAPE:
			charset = g_strdup (export_options->charset);
			break;
		case TRANSLITERATE_MODE_TRANS:
			charset = g_strconcat (export_options->charset,
					       "//TRANSLIT",
					       NULL);
			break;
		}
		converter = gsf_output_iconv_new (sink, charset, "UTF-8");
		g_free (charset);

		if (converter) {
			g_object_unref (sink);
			sink = converter;
		} else {
			g_warning ("Failed to create converter.");
			g_object_unref (sink);
			return FALSE;
		}
	}

	g_object_set (G_OBJECT (export_options->csv),
		      "sink", sink,
		      NULL);
	g_object_unref (sink);

	for (ptr = export_options->sheet_list; ptr != NULL ; ptr = ptr->next)
		if (!stf_export_sheet (export_options, ptr->data))
			return FALSE;
	return TRUE;
}


/**
 * stf_export_can_transliterate:
 *
 * Return value: TRUE iff //TRANSLIT is supported
 **/

gboolean
stf_export_can_transliterate (void)
{
	char const *text = "G\xc3\xbclzow";
	char *encoded_text;
	GError *error = NULL;

	encoded_text = g_convert (text, -1,
				  "ASCII//TRANSLIT", "UTF-8",
				  NULL, NULL, &error);
	g_free (encoded_text);

	if (error == NULL)
		return TRUE;

	g_error_free (error);
	return FALSE;
}
