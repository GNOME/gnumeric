/**
 * lotus.c: Lotus 123 support for Gnumeric
 *
 * Authors:
 *    See: README
 *    Michael Meeks (mmeeks@gnu.org)
 *    Stephen Wood  (saw@genhomepage.com)
 **/
#include <config.h>
#include "gnumeric.h"

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <ctype.h>
#include <gnome.h>
#include "workbook.h"
#include "gnumeric-util.h"
#include "main.h"
#include "sheet.h"
#include "file.h"
#include "utils.h"
#include "value.h"
#include "cell.h"
#include "command-context.h"

#include "lotus.h"
#include "lotus-types.h"
#include "lotus-formula.h"

#define LOTUS_DEBUG 0

#if G_BYTE_ORDER != G_LITTLE_ENDIAN

/* These really should be in glib */

double
lotus_getdouble (const guint8 *p)
{
	double d;
	int i;
	guint8 *t = (guint8 *)&d;
	int sd = sizeof (d);

	for (i = 0; i < sd; i++)
		t[i] = p[sd - 1 - i];

	return d;
}

void
lotus_setdouble (guint8 *p, double d)
{
	int i;
	guint8 *t = (guint8 *)&d;
	int sd = sizeof (d);

	for (i = 0; i < sd; i++)
		p[sd - 1 - i] = t[i];
}
#endif

char *lotus_special_formats [16] = {
	"",
	"",
	"d-mmm-yy",
	"d-mmm",
	"mmm yy",
	"",
	"",
	"h:mm:ss AM/PM",			/* Need am/pm */
	"h:mm",
	"m/d/yy",
	"d/m/yy",
	"",
	"",
	"",
	"",
	""
};

static void
append_zeros (char *s, int n) {

	if (n > 0) {
		strcat (s, ".");
		while (n--)
			strcat (s, "0");
	}
}
  
static void
cell_set_format_from_lotus_format (Cell *cell, int fmt)
{
	int fmt_type  = (fmt >> 4) & 0x7;
	int precision = fmt&0xf;
	char fmt_string [100];
	
	switch (fmt_type) {

	case 0:			/* Float */
		strcpy (fmt_string, "0");
		append_zeros (fmt_string, precision);
		break;
	case 1:			/* Scientific */
		strcpy (fmt_string, "0");
		append_zeros (fmt_string, precision);
		strcat (fmt_string, "E+00");
		break;
	case 2:			/* Currency */
		strcpy (fmt_string, "$#,##0"); /* Should not force $ */
		append_zeros (fmt_string, precision);
		strcat (fmt_string, "_);[Red]($#,##0");
		append_zeros (fmt_string, precision);
		strcat (fmt_string, ")");
		break;
	case 3:			/* Float */
		strcpy (fmt_string, "0");
		append_zeros (fmt_string, precision);
		strcat (fmt_string, "%");
		break;
	case 4:			/* Comma */
		strcpy (fmt_string, "#,##0"); /* Should not force $ */
		append_zeros (fmt_string, precision);
		break;

	case 7:			/* Lotus special format */
		strcpy (fmt_string,  lotus_special_formats [precision]);
		break;

	default:
		strcpy ("fmt_string", "");
		break;

	}
	if (fmt_string [0])
		cell_set_format (cell, fmt_string);
#if LOTUS_DEBUG > 0
	printf ("Format: %s\n", fmt_string);
#endif
}
  
typedef struct {
	FILE    *f;
	guint16  type;
	guint16  len;
	guint8  *data;
} record_t;

static record_t *
record_new (FILE *f)
{
	record_t *r = g_new (record_t, 1);
	r->f        = f;
	/* Speed & determinism */
	r->data     = g_new (guint8, 65540);
	r->type     = 0;
	r->len      = 0;
	return r;
}

static gboolean
record_next (record_t *r)
{
	guint8 hdata[8];
	g_return_val_if_fail (r != NULL, FALSE);

	if (fread (hdata, 1, 4, r->f) != 4)
		return FALSE;

	r->type = GUINT16_FROM_LE (*(guint16 *)hdata);
	r->len  = GUINT16_FROM_LE (*(guint16 *)(hdata + 2));

#if LOTUS_DEBUG > 0
	printf ("Record 0x%x length 0x%x\n", r->type, r->len);
#endif

	if (fread (r->data, 1, r->len, r->f) != r->len)
		return FALSE;

	return TRUE;
}

static void
record_destroy (record_t *r)
{
	if (r) {
		g_free (r->data);
		r->data = NULL;
		g_free (r);
	}
}

static Cell *
insert_value (Sheet *sheet, guint32 col, guint32 row, Value *val)
{
	Cell *cell;

	g_return_val_if_fail (val != NULL, NULL);
	g_return_val_if_fail (sheet != NULL, NULL);

	cell = sheet_cell_fetch (sheet, col, row);

	g_return_val_if_fail (cell != NULL, NULL);
	cell_set_value (cell, val);

#if LOTUS_DEBUG > 0
	printf ("Inserting value at %s:\n",
		cell_name (cell->col->pos, cell->row->pos));
	value_dump (val);
#endif
	return cell;
}

static Sheet *
attach_sheet (Workbook *wb, int idx)
{
	Sheet *sheet;
	char  *sheet_name;

	sheet_name = g_strdup_printf (_("Sheet%d"), idx); 
	sheet = sheet_new (wb, sheet_name);
	g_free (sheet_name);
	workbook_attach_sheet (wb, sheet);

	return sheet;
}

/* buf was old siag wb / sheet */
static int
read_workbook (CommandContext *context, Workbook *wb, FILE *f)
{
	int       sheetidx = 0;
	Sheet    *sheet = NULL;
	record_t *r;

	sheet = attach_sheet (wb, sheetidx++);

	r = record_new (f);

	while (record_next (r)) {
		Cell    *cell;
		Value   *v;
		guint32  i, j;
		guint16  fmt;	/* Format code of Lotus Cell */

		switch (r->type) {
		case LOTUS_BOF:
			if (sheetidx > 1)
				sheet = attach_sheet (wb, sheetidx++);
			break;

		case LOTUS_EOF:
			sheet = NULL;
			break;

		case LOTUS_INTEGER:
		{
			gint16 i = GINT16_FROM_LE (*(gint16 *)(r->data + 5));
			v = value_new_int (i);
			i = GUINT16_FROM_LE  (*(guint16 *)(r->data + 1));
		        j = GUINT16_FROM_LE  (*(guint16 *)(r->data + 3));
			fmt = *(guint8 *)(r->data);

			cell = insert_value (sheet, i, j, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_NUMBER:
		{
			float_t num = LOTUS_GETDOUBLE (r->data + 5);
			v = value_new_float (num);
			i = GUINT16_FROM_LE (*(guint16 *)(r->data + 1));
		        j = GUINT16_FROM_LE (*(guint16 *)(r->data + 3));
			fmt = *(guint8 *)(r->data);

			cell = insert_value (sheet, i, j, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_LABEL:
		{
			/* one of '\', '''', '"', '^' */
/*			gchar format_prefix = *(r->data + 5);*/
			v = value_new_string (r->data + 6); /* FIXME unsafe */
			i = GUINT16_FROM_LE (*(guint16 *)(r->data + 1));
		        j = GUINT16_FROM_LE (*(guint16 *)(r->data + 3));
			fmt = *(guint8 *)(r->data);
			cell = insert_value (sheet, i, j, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_FORMULA:
		{
			ExprTree *f;
		/* 5-12 = value */
		/* 13-14 = formula r->length */
			i = GUINT16_FROM_LE (*(guint16 *)(r->data + 1));
                        j = GUINT16_FROM_LE (*(guint16 *)(r->data + 3));
			fmt = *(guint8 *)(r->data);
			f = lotus_parse_formula (sheet, i, j, r->data + 15, /* FIXME: unsafe */
						 GINT16_FROM_LE (*(gint16 *)(r->data + 13)));
			v = value_new_float (LOTUS_GETDOUBLE (r->data + 5));
			cell = insert_value (sheet, i, j, v);
			if (cell) {
				cell_set_formula_tree (cell, f);
				cell_set_format_from_lotus_format (cell, fmt);
			}
			break;
		}
		default:
			break;
		}
	}
	record_destroy (r);

	return 0;
}

int
lotus_read (CommandContext *context, Workbook *wb, const char *filename)
{
	FILE *f;
	int res;

	if (!(f = fopen (filename, "rb"))) {
		gnumeric_error_read (context, g_strerror (errno));
		return -1;
	}
		
	res = read_workbook (context, wb, f);
	fclose (f);

	return res;
}

