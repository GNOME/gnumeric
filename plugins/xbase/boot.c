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
#include "workbook.h"
#include "gnumeric-util.h"
#include "main.h"
#include "value.h"
#include "cell.h"
#include "file.h"
#include "command-context.h"

#include "xbase.h"
#include "plugin.h"

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#     define XB_GETDOUBLE(p)   (*((double*)(p)))
#     define XB_SETDOUBLE(p,q) (*((double*)(p))=(q))
#else
#     define XB_GETDOUBLE(p)   (xb_getdouble(p))
#     define XB_SETDOUBLE(p,q) (xb_setdouble(p,q))

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

static char *
filename_ext (const char *filename)
{
	char *p = strrchr (filename, '.');
	if (p == NULL)
		return NULL;
	return ++p;
}

static gboolean
xbase_probe (const char *filename)
{
	char *ext;

	if (!filename)
		return FALSE;
	ext = filename_ext (filename);
	if (!ext)
		return FALSE;
	if (g_strcasecmp ("dbf", ext))
	    return FALSE;
	return TRUE;
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
		return value_new_int (atoi (s));
	case 'L':
		switch (s[0]) {
		case 'Y': case 'y': case 'T': case 't':
			return value_new_bool (TRUE);
		case 'N': case 'n': case 'F': case 'f':
			return value_new_bool (FALSE);
		case '?': case ' ':
			return value_new_string ("Uninitialised boolean");
		default: {
				char str[20];
				snprintf (str, 20, "Invalid logical '%c'", s[0]);
				return value_new_string (str);
			}
		}
	case 'D':
		val = value_new_string (s);
		g_free (s);
		return val;
	case 'I':
		return value_new_int (GINT32_FROM_LE((gint32) *s));
	case 'F':
		g_assert (sizeof(float_t) == field->len);
		return value_new_float (XB_GETDOUBLE(s));
	case 'B': {
		gint64 tmp = GINT64_FROM_LE (*s);
		g_warning ("FIXME: \"BINARY\" field type doesn't work");
		g_assert (sizeof(tmp) == field->len);
		return value_new_int (tmp);
	}
	default: {
			char str[27];
			snprintf (str, 27, "Field type '%c' unsupported",
				  field->type);
			return value_new_string (str);
		}
	}
}

static int
xbase_load (CommandContext *context, Workbook *wb, const char *filename)
{
	XBfile *file;
	XBrecord *rec;
	guint row, field;
	char *name = g_strdup(filename), *p;
	Sheet    *sheet = NULL;
	Cell *cell;
	Value *val;

	if (!(file = xbase_open (context, filename))) {
		gnumeric_error_read (context, g_strerror (errno));
		return -1;
	}

	if ((p = filename_ext (name)) != NULL)
		*p = '\0'; /* remove "dbf" */

	rec = record_new (file);
	sheet = sheet_new (wb, g_basename (name));
	g_free (name);
	workbook_attach_sheet (wb, sheet);
	workbook_set_saveinfo (wb, filename, FILE_FL_MANUAL, NULL);

	cell_deep_freeze_redraws ();

	field = 0;
	while (field < file->fields) {
		cell = sheet_cell_fetch (sheet, field, 0);
		cell_set_text_simple (cell, file->format[field++]->name);
		/* FIXME: apply StyleFont gnumeric-default_bold_font */
	}

	row = 1;
	do {
		field = 0;
		while (field < file->fields) {
			cell = sheet_cell_fetch (sheet, field, row);
			cell_set_value_simple (cell, val =
					       xbase_field_as_value
					       (rec, ++field));
		}
		row++;
	} while (record_seek (rec, SEEK_CUR, 1));

	workbook_recalc (wb);
	cell_deep_thaw_redraws ();

	xbase_close (file);

	return 0;
}

static int
xbase_can_unload (PluginData *pd)
{
	return TRUE;
}

static void
xbase_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (xbase_probe, xbase_load);
}

#define XBASE_TITLE _("XBase file import plugin")
#define XBASE_DESCR _("This plugin enables XBase file import")

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{
	char *descr  = _("Xbase (*.dbf) file format");

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	/* We register XBase format with a precendence of 100 */
	file_format_register_open (100, descr, xbase_probe, xbase_load);

	if (plugin_data_init (pd, xbase_can_unload, xbase_cleanup_plugin,
			      XBASE_TITLE, XBASE_DESCR))
	        return PLUGIN_OK;
	else
	        return PLUGIN_ERROR;

}
