/**
 * boot.c: XBase support for Gnumeric
 *
 * Author:
 *    Sean Atkinson <sca20@cam.ac.uk>
 **/
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <gnome.h>
#include "gnumeric.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gnumeric-util.h"
#include "main.h"
#include "value.h"
#include "cell.h"
#include "file.h"
#include "xbase.h"
#include "plugin.h"
#include "plugin-util.h"

gchar gnumeric_plugin_version[] = GNUMERIC_VERSION;

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
xb_getdouble (const guint8 *p)
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

static FileOpenerId xbase_opener_id;

static gboolean
xbase_probe (const char *filename, gpointer user_data)
{
	return filename != NULL &&
	       g_strcasecmp ("dbf", g_extension_pointer (filename)) == 0;
}

static Value *
xbase_field_as_value (XBrecord *record, guint num)
{
	gint8 *s = g_strdup (record_get_field (record, num));
	Value *val;
	XBfield *field = record->file->format[num-1];

	s[field->len] = '\0';
	switch (field->type) {
	case 'C':
		val = value_new_string (g_strchomp (s));
		g_free (s);
		return val;
	case 'N':
		val = value_new_int (atoi (s));
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
	case 'D':
		val = value_new_string (s);
		g_free (s);
		return val;
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

static int
xbase_load (IOContext *context, WorkbookView *wb_view,
            const char *filename, gpointer user_data)
{
	Workbook *wb = wb_view_workbook (wb_view);
	XBfile *file;
	XBrecord *rec;
	guint row, field;
	char *name;
	Sheet *sheet = NULL;
	Cell *cell;
	Value *val;

	if (!(file = xbase_open (context, filename))) {
		gnumeric_io_error_system (context, g_strerror (errno));
		return -1;
	}

	name = g_strdup(filename);

	*((gchar *) g_extension_pointer (name)) = '\0'; /* remove "dbf" */

	rec = record_new (file);
	sheet = sheet_new (wb, g_basename (name));
	g_free (name);
	workbook_sheet_attach (wb, sheet, NULL);
	workbook_set_saveinfo (wb, filename, FILE_FL_MANUAL, FILE_SAVER_ID_INVAID);

	field = 0;
	while (field < file->fields) {
		cell = sheet_cell_fetch (sheet, field, 0);
		cell_set_text (cell, file->format[field++]->name);
		/* FIXME: apply StyleFont gnumeric-default_bold_font */
	}

	row = 1;
	do {
		field = 0;
		while (field < file->fields) {
			cell = sheet_cell_fetch (sheet, field, row);
			val = xbase_field_as_value (rec, ++field);
			cell_set_value (cell, val, NULL);
		}
		row++;
	} while (record_seek (rec, SEEK_CUR, 1));

	record_free (rec);
	xbase_close (file);

	return 0;
}

gboolean
can_deactivate_plugin (PluginInfo *pinfo)
{
	return TRUE;
}

gboolean
cleanup_plugin (PluginInfo *pinfo)
{
	file_format_unregister_open (xbase_opener_id);
	return TRUE;
}

gboolean
init_plugin (PluginInfo *pinfo, ErrorInfo **ret_error)
{
	/* We register XBase format with a precendence of 100 */
	xbase_opener_id = file_format_register_open (100,
	                                             _("Xbase (*.dbf) file format"),
	                                             &xbase_probe, &xbase_load, NULL);

	return TRUE;
}
