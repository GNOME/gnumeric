/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * boot.c: XBase support for Gnumeric
 *
 * Author:
 *    Sean Atkinson <sca20@cam.ac.uk>
 **/
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>
#include "xbase.h"

#include <workbook-view.h>
#include <workbook.h>
#include <cell.h>
#include <value.h>
#include <plugin.h>
#include <plugin-util.h>
#include <module-plugin-defs.h>
#include <sheet.h>
#include <datetime.h>
#include <ranges.h>
#include <mstyle.h>
#include <sheet-style.h>
#include <io-context.h>

#include <stdlib.h>

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

void xbase_file_open (GnmFileOpener const *fo, IOContext *io_context,
                      WorkbookView *wb_view, GsfInput *input);

#if G_BYTE_ORDER == G_LITTLE_ENDIAN

#     define XB_GETDOUBLE(p)   (*((double*)(p)))
#if 0
#     define XB_SETDOUBLE(p,q) (*((double*)(p))=(q))
#endif

#else

#     define XB_GETDOUBLE(p)   (xb_getdouble(p))
#if 0
#     define XB_SETDOUBLE(p,q) (xb_setdouble(p,q))
#endif

static double
xb_getdouble (guint8 const *p)
{
    double d;
    int i;
    guint8 *t = (guint8 *)&d;
    int sd = sizeof (d);

    for (i = 0; i < sd; i++)
      t[i] = p[sd - 1 - i];

    return d;
}

#if 0
static void
xb_setdouble (guint8 *p, double d)
{
    int i;
    guint8 *t = (guint8 *)&d;
    int sd = sizeof (d);

    for (i = 0; i < sd; i++)
	    p[sd - 1 - i] = t[i];
}
#endif

#endif

static GnmValue *
xbase_field_as_value (gchar *content, XBfield *field, XBfile *file)
{
	gchar *s = g_strndup (content, field->len);
	GnmValue *val;

	switch (field->type) {
	case 'C': {
		if (file->char_map != (GIConv)-1)
			val = value_new_string_nocopy (
				g_convert_with_iconv (g_strchomp (s), -1,
				      file->char_map, NULL, NULL, NULL));
		else
			val = value_new_string_nocopy (g_strchomp (s));
		return val;
	}
	case 'N':
		val = value_new_float (strtognum (s, NULL));
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
			return value_new_string ("Uninitialised boolean");
		default: {
				char str[20];
				snprintf (str, 20, "Invalid logical '%c'", s[0]);
				g_free (s);
				return value_new_string (str);
			}
		}
	case 'D': {
		/* double check that the date is stored according to spec */
		int year, month, day;
		if (sscanf (s, "%4d%2d%2d", &year, &month, &day) == 3) {
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
		val = value_new_int (GINT32_FROM_LE (*(gint32 *)s));
		g_free (s);
		return val;
	case 'F':
		g_assert (sizeof (double) == field->len);
		val = value_new_float (XB_GETDOUBLE (s));
		g_free (s);
		return val;
	case 'B': {
		gint64 tmp = GINT32_FROM_LE (*(gint64 *)s);
		g_free (s);
		g_warning ("FIXME: \"BINARY\" field type doesn't work");
		g_assert (sizeof(tmp) == field->len);
		return value_new_int (tmp);
	}
	default: {
			char str[27];
			snprintf (str, 27, "Field type '%c' unsupported",
				  field->type);
			g_free (s);
			return value_new_string (str);
		}
	}
}

void
xbase_file_open (GnmFileOpener const *fo, IOContext *io_context,
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

	wb = wb_view_workbook (wb_view);
	name = workbook_sheet_get_free_name (wb, _("Sheet"), FALSE, TRUE);
	sheet = sheet_new (wb, name);
	g_free (name);
	workbook_sheet_attach (wb, sheet, NULL);

	i = 0;
	for (i = 0 ; i < file->fields ; i++) {
		cell = sheet_cell_fetch (sheet, i, 0);
		cell_set_text (cell, file->format [i]->name);
	}
	{
		GnmRange r;
		GnmStyle *bold = mstyle_new ();
		mstyle_set_font_bold (bold, TRUE);
		sheet_style_apply_range	(sheet,
			range_init (&r, 0, 0, file->fields-1, 0), bold);
	}

	record = record_new (file);
	row = 1;
	do {
		if (row >= SHEET_MAX_ROWS) {
			g_warning (_("This build of Gnumeric can only hold %d "
				   "rows.  Ignoring the rest of this file.  You "
				   "will need a custom build with SHEET_MAX_ROWS "
				   "increased to read this file."),
				   SHEET_MAX_ROWS);
			break;
		}
		for (i = 0; i < file->fields ; i++) {
			field = record->file->format [i];
			val = xbase_field_as_value (
				record_get_field (record, i), field, file);
			cell = sheet_cell_fetch (sheet, i, row);
			value_set_fmt (val, field->fmt);
			cell_set_value (cell, val);
		}
		row++;
	} while (record_seek (record, SEEK_CUR, 1));

	record_free (record);
	xbase_close (file);

	sheet_flag_recompute_spans (sheet);
}
