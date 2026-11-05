/**
 * boot.c: XBase support for Gnumeric
 *
 * Author:
 *    Sean Atkinson <sca20@cam.ac.uk>
 **/
#include <gnumeric-config.h>
#include "xbase.h"
#include <gnumeric.h>

#include <workbook-view.h>
#include <workbook.h>
#include <cell.h>
#include <gutils.h>
#include <value.h>
#include <sheet.h>
#include <ranges.h>
#include <mstyle.h>
#include <sheet-style.h>
#include <goffice/goffice.h>
#include <gsf/gsf-utils.h>
#include <gnm-plugin.h>
#include <glib/gi18n-lib.h>
#include <stdlib.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

void xbase_file_open (GOFileOpener const *fo, GOIOContext *io_context,
                      WorkbookView *wb_view, GsfInput *input);


#define CHECK_LENGTH(_l) do {						\
	if (field->len != (_l)) {					\
		g_warning ("Invalid field length.  File is probably corrupted."); \
		g_free (s);						\
		return NULL;						\
	}								\
} while (0)

static GnmValue *
xbase_field_as_value (gchar *content, XBfield *field, XBfile *file)
{
	gchar *s = g_strndup (content, field->len);
	GnmValue *val;

	switch (field->type) {
	case 'C': {
		char *sutf8 = g_convert_with_iconv (g_strchomp (s), -1,
						    file->char_map, NULL, NULL, NULL);
		if (!sutf8) {
			char *t;
			for (t = s; *t; t++)
				if ((guchar)*t >= 0x7f)
					*t = '?';
			sutf8 = s;
			s = NULL;
			g_warning ("Unrepresentable characters replaced by '?'");
		}
		if (sutf8)
			val = value_new_string_nocopy (sutf8);
		else
			val = value_new_string ("???");
		g_free (s);
		return val;
	}
	case 'N': {
		char *end;
		gnm_float d = gnm_strto (s, &end);
		// A blank (or overflowed, "****") field holds no number at all
		val = (end == s) ? NULL : value_new_float (d);
		g_free (s);
		return val;
	}
	case 'L':
		switch (s[0]) {
		case 'Y': case 'y': case 'T': case 't':
			g_free (s);
			return value_new_bool (TRUE);
		case 'N': case 'n': case 'F': case 'f':
			g_free (s);
			return value_new_bool (FALSE);
		case '?': case ' ':
			g_free (s);
			return NULL;
		default:
			g_warning ("Invalid logical value.  File is probably corrupted.");
			g_free (s);
			return NULL;
		}
	case 'D': {
		/* double check that the date is stored according to spec */
		int year, month, day;
		g_strstrip (s);
		// An all-blank date is as empty as an all-zero one
		if (*s == '\0' || strcmp (s, "00000000") == 0)
			val = NULL;
		else if (sscanf (s, "%4d%2d%2d", &year, &month, &day) == 3 &&
			 year >= 1 &&
			 g_date_valid_dmy (day, month, year)) {
			GDate *date = g_date_new_dmy (day, month, year);
			/* Use default date convention */
			val = value_new_int (go_date_g_to_serial (date, NULL));
			g_date_free (date);
		} else
			val = value_new_string (s);
		g_free (s);
		return val;
	}
	case 'I':
		CHECK_LENGTH (4);
		val = value_new_int (GSF_LE_GET_GINT32 (s));
		g_free (s);
		return val;
	case 'F':
		CHECK_LENGTH (sizeof (double));
		val = value_new_float (GSF_LE_GET_DOUBLE (s));
		g_free (s);
		return val;
	case 'B': {
		gint64 tmp;
		// Check the length before reading 8 bytes from the field
		CHECK_LENGTH (sizeof (tmp));
		tmp = GSF_LE_GET_GINT64 (s);
		g_warning ("FIXME: \"BINARY\" field type doesn't work");
		g_free (s);
		return value_new_float (tmp);
	}
	default: {
		char *msg = g_strdup_printf ("Field type '0x%02x' unsupported",
					     field->type);
		g_free (s);
		return value_new_string_nocopy (msg);
	}
	}
}

#undef CHECK_LENGTH

// Field names are stored in the file's code page, not in UTF-8
static char *
xbase_name_to_utf8 (XBfile *file, char const *name)
{
	char *res = g_convert_with_iconv (name, -1, file->char_map, NULL, NULL, NULL);
	if (res == NULL) {
		char *t;
		res = g_strdup (name);
		for (t = res; *t; t++)
			if ((guchar)*t >= 0x7f)
				*t = '?';
	}
	return res;
}

static void
create_header (Sheet *sheet, XBfile *file, unsigned ncols)
{
	unsigned ui;
	GnmRange r;
	GnmStyle *bold = gnm_style_new ();

	for (ui = 0 ; ui < ncols ; ui++) {
		GnmCell *cell = sheet_cell_fetch (sheet, ui, 0);
		char *name = xbase_name_to_utf8 (file, file->format[ui]->name);
		gnm_cell_set_text (cell, name);
		g_free (name);
	}

	gnm_style_set_font_bold (bold, TRUE);
	range_init (&r, 0, 0, (int) ncols - 1, 0);
	sheet_style_apply_range	(sheet, &r, bold);
}

void
xbase_file_open (GOFileOpener const *fo, GOIOContext *io_context,
                 WorkbookView *wb_view, GsfInput *input)
{
	Workbook  *wb;
	XBfile	  *file;
	XBrecord  *record;
	Sheet	  *sheet = NULL;
	GOErrorInfo *open_error;
	// Number of data rows; row 0 of the sheet holds the header
	int rows = GNM_MAX_ROWS - 1;
	int pass;
	unsigned ncols = 0;
	gboolean truncated = FALSE;

	if ((file = xbase_open (input, &open_error)) == NULL) {
		go_io_error_info_set (io_context, go_error_info_new_str_with_details (
		                            _("Error while opening xbase file."),
		                            open_error));
		return;
	}

	wb = wb_view_get_workbook (wb_view);

	for (pass = 1; pass <= 2; pass++) {
		int row = 0;

		if (pass == 2) {
			int cols = file->fields;
			// The data rows need one more row for the header
			int sheet_rows = rows + 1;
			gnm_sheet_suggest_size (&cols, &sheet_rows);
			sheet = workbook_sheet_add (wb, -1, cols, sheet_rows);
			rows = sheet_rows - 1;
			// The suggested size can be too small for wide tables
			ncols = MIN (file->fields,
				     (unsigned) gnm_sheet_get_max_cols (sheet));
			if (ncols < file->fields)
				go_io_warning (io_context, "%s",
					       _("The xbase file has more fields than fit in a sheet; the remaining fields were not imported."));
			create_header (sheet, file, ncols);
		}

		// NULL when there is no readable first record
		record = record_new (file);
		while (record) {
			unsigned ui;
			gboolean deleted = record_deleted (record);
			if (deleted)
				goto next_record;

			if (row >= rows) {
				truncated = TRUE;
				break;
			}

			row++;
			if (pass == 1)
				goto next_record;

			for (ui = 0; ui < ncols; ui++) {
				GnmCell *cell;
				XBfield *field = record->file->format[ui];
				GnmValue *val = xbase_field_as_value
					(record_get_field (record, ui),
					 field, file);
				if (!val)
					continue;

				cell = sheet_cell_fetch (sheet, ui, row);
				value_set_fmt (val, field->fmt);
				gnm_cell_set_value (cell, val);
			}

		next_record:
			if (!record_seek (record, SEEK_CUR, 1))
				break;
		}
		if (record)
			record_free (record);
		rows = row;
	}

	if (truncated)
		go_io_warning (io_context, "%s",
			       _("The xbase file has more records than fit in a sheet; the remaining records were not imported."));

	xbase_close (file);

	sheet_flag_recompute_spans (sheet);
}
