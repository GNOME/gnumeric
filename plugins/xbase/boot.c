/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
#include <value.h>
#include <sheet.h>
#include <ranges.h>
#include <mstyle.h>
#include <sheet-style.h>
#include <goffice/app/io-context.h>
#include <goffice/app/go-plugin.h>
#include <goffice/app/error-info.h>
#include <goffice/utils/datetime.h>
#include <gsf/gsf-utils.h>
#include <gnm-plugin.h>
#include <glib/gi18n-lib.h>
#include <stdlib.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

void xbase_file_open (GOFileOpener const *fo, IOContext *io_context,
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
			val = value_new_int (datetime_g_to_serial (date, NULL));
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

void
xbase_file_open (GOFileOpener const *fo, IOContext *io_context,
                 WorkbookView *wb_view, GsfInput *input)
{
	Workbook  *wb;
	XBfile	  *file;
	XBrecord  *record;
	char	  *name;
	Sheet	  *sheet = NULL;
	GnmCell	  *cell;
	GnmValue	  *val;
	XBfield	  *field;
	ErrorInfo *open_error;
	guint row, i;

	if ((file = xbase_open (input, &open_error)) == NULL) {
		gnumeric_io_error_info_set (io_context, error_info_new_str_with_details (
		                            _("Error while opening xbase file."),
		                            open_error));
		return;
	}

	wb = wb_view_get_workbook (wb_view);
	name = workbook_sheet_get_free_name (wb, _("Sheet"), FALSE, TRUE);
	sheet = sheet_new (wb, name);
	g_free (name);
	workbook_sheet_attach (wb, sheet);

	i = 0;
	for (i = 0 ; i < file->fields ; i++) {
		cell = sheet_cell_fetch (sheet, i, 0);
		gnm_cell_set_text (cell, file->format [i]->name);
	}
	{
		GnmRange r;
		GnmStyle *bold = gnm_style_new ();
		gnm_style_set_font_bold (bold, TRUE);
		sheet_style_apply_range	(sheet,
			range_init (&r, 0, 0, file->fields-1, 0), bold);
	}

	record = record_new (file);
	row = 1;
	do {
		gboolean deleted = record_deleted (record);
		if (deleted)
			continue;
		if (row >= gnm_sheet_get_max_rows (sheet)) {
			/* FIXME: either we need to add new rows, if posible
			or create a larger sheet*/
			break;
		}
		for (i = 0; i < file->fields ; i++) {
			field = record->file->format[i];
			val = xbase_field_as_value (
				record_get_field (record, i), field, file);
			if (!val)
				continue;
			cell = sheet_cell_fetch (sheet, i, row);
			value_set_fmt (val, field->fmt);
			gnm_cell_set_value (cell, val);
		}
		row++;
	} while (record_seek (record, SEEK_CUR, 1));

	record_free (record);
	xbase_close (file);

	sheet_flag_recompute_spans (sheet);
}
