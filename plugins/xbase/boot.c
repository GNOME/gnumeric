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
	case 'N':
		val = value_new_float (gnm_strto (s, NULL));
		g_free (s);
		return val;
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
		if (strcmp (s, "00000000") == 0)
			val = NULL;
		else if (sscanf (s, "%4d%2d%2d", &year, &month, &day) == 3 &&
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
		val = value_new_int (GSF_LE_GET_GINT32 (s));
		g_free (s);
		return val;
	case 'F':
		CHECK_LENGTH (sizeof (double));
		val = value_new_float (GSF_LE_GET_DOUBLE (s));
		g_free (s);
		return val;
	case 'B': {
		gint64 tmp = GSF_LE_GET_GINT64 (s);
		g_warning ("FIXME: \"BINARY\" field type doesn't work");
		CHECK_LENGTH (sizeof (tmp));
		g_free (s);
		return value_new_float (tmp);
	}
	default: {
		char *s = g_strdup_printf ("Field type '0x%02x' unsupported",
					   field->type);
		return value_new_string_nocopy (s);
	}
	}
}

#undef CHECK_LENGTH

static void
create_header (Sheet *sheet, XBfile *file)
{
	unsigned ui;
	GnmRange r;
	GnmStyle *bold = gnm_style_new ();

	for (ui = 0 ; ui < file->fields ; ui++) {
		GnmCell *cell = sheet_cell_fetch (sheet, ui, 0);
		gnm_cell_set_text (cell, file->format[ui]->name);
	}

	gnm_style_set_font_bold (bold, TRUE);
	range_init (&r, 0, 0, file->fields - 1, 0);
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
	int rows = GNM_MAX_ROWS;
	int pass;

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
			gnm_sheet_suggest_size (&cols, &rows);
			sheet = workbook_sheet_add (wb, -1, cols, rows);
			create_header (sheet, file);
		}

		record = record_new (file);
		do {
			unsigned ui;
			gboolean deleted = record_deleted (record);
			if (deleted)
				continue;

			if (row >= rows)
				break;

			row++;
			if (pass == 1)
				continue;

			for (ui = 0; ui < file->fields; ui++) {
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
		} while (record_seek (record, SEEK_CUR, 1));
		record_free (record);
		rows = row;
	}

	xbase_close (file);

	sheet_flag_recompute_spans (sheet);
}
