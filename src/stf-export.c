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

#include <stdio.h>
#include <string.h>

/*******************************************************************************************************
 * STF_EXPORT_OPTIONS Creation/Destruction/Manipulation
 *******************************************************************************************************/

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

	export_options->terminator_type   = TERMINATOR_TYPE_UNKNOWN;
	export_options->cell_separator    = 0;
	export_options->quoting_char      = 0;
	export_options->sheet_list        = NULL;
	export_options->quoting_mode      = QUOTING_MODE_UNKNOWN;
	export_options->charset	  	  = NULL;

	export_options->write_func        = NULL;
	export_options->write_data        = NULL;

	return export_options;
}

/**
 * stf_export_options_free:
 * @export_options: an export options struct
 *
 * Frees the export options struct
 **/
void
stf_export_options_free (StfExportOptions_t *export_options)
{
	if (export_options->sheet_list)
		g_slist_free (export_options->sheet_list);

	g_free (export_options);
}

/**
 * stf_export_options_set_terminator_type:
 * @export_options: an export options struct
 * @terminator_type: The new terminator type
 *
 * Sets the terminator type
 **/
void
stf_export_options_set_terminator_type (StfExportOptions_t *export_options, StfTerminatorType_t terminator_type)
{
	g_return_if_fail (export_options != NULL);
	g_return_if_fail (terminator_type >= 0 && terminator_type < TERMINATOR_TYPE_UNKNOWN);

	export_options->terminator_type = terminator_type;
}


/**
 * stf_export_options_set_cell_separator:
 * @export_options: an export options struct
 * @cell_separator: The new cell separator
 *
 * Sets the cell separator (a.k.a. field separator)
 **/
void
stf_export_options_set_cell_separator (StfExportOptions_t *export_options, gunichar cell_separator)
{
	g_return_if_fail (export_options != NULL);
	g_return_if_fail (cell_separator != 0);

	export_options->cell_separator = cell_separator;
}

/**
 * stf_export_options_set_quoting_mode:
 * @export_options: an export options struct
 * @quoting_mode: the quoting mode
 *
 * Sets the quoting mode (auto/always/never)
 **/
void
stf_export_options_set_quoting_mode (StfExportOptions_t *export_options, StfQuotingMode_t quoting_mode)
{
	g_return_if_fail (export_options != NULL);
	g_return_if_fail (quoting_mode >= 0 && quoting_mode < QUOTING_MODE_UNKNOWN);

	export_options->quoting_mode = quoting_mode;
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
 * stf_export_options_set_quoting_char:
 * @export_options: an export options struct
 * @quoting_char: the quoting char
 *
 * Sets the quoting char (== character which is used to 'quote')
 * The quoting char can't be \0 !
 **/
void
stf_export_options_set_quoting_char (StfExportOptions_t *export_options, gunichar quoting_char)
{
	g_return_if_fail (export_options != NULL);
	g_return_if_fail (quoting_char != 0);

	export_options->quoting_char = quoting_char;
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

	export_options->charset = charset;
}
void
stf_export_options_set_write_callback (StfExportOptions_t *export_options,
				       StfEWriteFunc write_func, gpointer data)
{
	g_return_if_fail (export_options != NULL);
	g_return_if_fail (write_func != NULL);

	export_options->write_func = write_func;
	export_options->write_data = data;
}

/**
 * stf_export_options_sheet_list_clear:
 * @export_options: an export options struct
 *
 * Clears the sheet list.
 * NOTE : This does not free the sheets contained in the sheet list
 *        the caller is responsible for freeing them!
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
 * NOTE : The caller is responsible for freeing the sheets!
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
 * Passes the contents of @cell to the callback function
 * (also does some csv related formatting)
 *
 * Return value: return TRUE on success, FALSE otherwise.
 **/
static gboolean
stf_export_cell (StfExportOptions_t *export_options, Cell *cell)
{
	g_return_val_if_fail (export_options != NULL, FALSE);

	if (cell) {
		gboolean quoting = FALSE;
		char *text = cell->value
			? value_get_as_string (cell->value)
			: g_strdup ("");
		const char *s = text;
		GString *res = g_string_new (NULL);

		gsize bytes_read;
		gsize bytes_written;
		GError * error = NULL;
		char * encoded_text = NULL;

		if (export_options->charset != NULL)
		{
			if (!g_str_equal (export_options->charset, "UTF-8"))
			{
				char *use_charset;

				if (export_options->transliterate_mode == TRANSLITERATE_MODE_ESCAPE)
					use_charset = g_strdup
						(export_options->charset);
				else
					use_charset = g_strconcat
						(export_options->charset, "//TRANSLIT", NULL);

				encoded_text = g_convert_with_fallback
					(text,
					 strlen (text),
					 use_charset,
					 "UTF-8",
					 NULL,
					 &bytes_read,
					 &bytes_written,
					 &error);
				if (error != NULL)
				{
					g_warning ("stf-export.c in %s charset : %s",
						   use_charset,
						   error->message);
					g_warning
						("the following cell will be exported as UTF-8 :\n%s", s);
					g_error_free (error);
				}
				else
				{
					s = encoded_text;
				}
				g_free (use_charset);
			}
		}

		if (export_options->quoting_mode == QUOTING_MODE_AUTO) {
			if (g_utf8_strchr (s, -1, export_options->cell_separator) ||
			    g_utf8_strchr (s, -1, export_options->quoting_char) ||
			    strchr (s, ' ') || strchr (s, '\t')) {
				quoting = TRUE;
			}
		} else {
			quoting = (export_options->quoting_mode == QUOTING_MODE_ALWAYS);
		}

		if (quoting)
			g_string_append_unichar (res, export_options->quoting_char);

		while (*s) {
			gunichar c = g_utf8_get_char (s);
			if (quoting && c == export_options->quoting_char) {
				g_string_append_unichar (res, export_options->quoting_char);
				g_string_append_unichar (res, export_options->quoting_char);
			} else
				g_string_append_unichar (res, c);

			s = g_utf8_next_char (s);
		}

		if (quoting)
			g_string_append_unichar (res, export_options->quoting_char);

		if (!export_options->write_func (res->str, export_options->write_data))
			return FALSE;

		g_free (encoded_text);
		g_string_free (res, TRUE);
		g_free (text);
	}

	return TRUE;
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
	GString *separator;
	gboolean error = FALSE;
	int col, row;
	Range r;

	g_return_val_if_fail (export_options != NULL, FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (export_options->terminator_type != TERMINATOR_TYPE_UNKNOWN, FALSE);
	g_return_val_if_fail (export_options->cell_separator != 0, FALSE);
	g_return_val_if_fail (export_options->quoting_mode != QUOTING_MODE_UNKNOWN, FALSE);
	g_return_val_if_fail (export_options->quoting_char != 0, FALSE);
	g_return_val_if_fail (export_options->write_func != NULL, FALSE);

	separator = g_string_new (NULL);
	g_string_append_unichar (separator, export_options->cell_separator);

	r = sheet_get_extent (sheet, FALSE);

	for (row = r.start.row; row <= r.end.row; row++) {

		for (col = r.start.col; col <= r.end.col; col++) {
			Cell *cell = sheet_cell_get (sheet, col, row);

			if (!stf_export_cell (export_options, cell)) {
				g_string_free (separator, TRUE);
				return FALSE;
			}

			if (col != r.end.col)
				if (!export_options->write_func (separator->str, export_options->write_data)) {
					g_string_free (separator, TRUE);
					return FALSE;
				}
		}

		error = FALSE;
		switch (export_options->terminator_type) {
		case TERMINATOR_TYPE_LINEFEED :
			if (!export_options->write_func ("\n", export_options->write_data))
				error = TRUE;
			break;
		case TERMINATOR_TYPE_RETURN :
			if (!export_options->write_func ("\r", export_options->write_data))
				error = TRUE;
			break;
		case TERMINATOR_TYPE_RETURN_LINEFEED :
			if (!export_options->write_func ("\r\n", export_options->write_data))
				error = TRUE;
			break;
		default :
			g_warning ("STF-E : Unknown terminator type");
			break;
		}

		if (error) {
			g_string_free (separator, TRUE);
			return FALSE;
		}
	}

	return TRUE;
}


/**
 * stf_export:
 * @export_options: an export options struct
 *
 * Exports the sheets given in @export_options
 * (passes strings to write to the write callback function)
 *
 * Return value: TRUE on success, FALSE otherwise
 **/
gboolean
stf_export (StfExportOptions_t *export_options)
{
	GSList *iterator;

	g_return_val_if_fail (export_options != NULL, FALSE);
	g_return_val_if_fail (export_options->terminator_type != TERMINATOR_TYPE_UNKNOWN, FALSE);
	g_return_val_if_fail (export_options->cell_separator != 0, FALSE);
	g_return_val_if_fail (export_options->sheet_list != NULL, FALSE);
	g_return_val_if_fail (export_options->quoting_mode != QUOTING_MODE_UNKNOWN, FALSE);

	iterator = export_options->sheet_list;

	while (iterator) {

		if (!stf_export_sheet (export_options, iterator->data))
			break;

		iterator = g_slist_next (iterator);
	}

	if (iterator) /* hmm, something went wrong */
		return FALSE;
	else
		return TRUE;
}


/**
 * stf_export:
 *
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
