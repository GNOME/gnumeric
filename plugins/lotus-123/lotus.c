/**
 * lotus.c: Lotus 123 support for Gnumeric
 *
 * Author:
 *    See: README
 *    Michael Meeks <michael@imagiantor.com>
 **/
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <config.h>
#include <stdio.h>
#include <ctype.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "main.h"
#include "sheet.h"
#include "file.h"
#include "utils.h"

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

/* buf was old siag wb / sheet */
static gboolean
read_workbook (Workbook *wb, FILE *f)
{
	int       sheetidx = 0;
	Sheet    *sheet = NULL;
	gboolean  panic = FALSE;
	record_t *r;

	r = record_new (f);

	while (!panic && record_next (r)) {
		Cell    *cell;
		Value   *v;
		guint32  i, j;

		switch (r->type) {
		case LOTUS_BOF:
		{
			char *name = g_strdup_printf ("Sheet%d\n", sheetidx++);
			sheet = sheet_new (wb, name);
			g_free (name);
			workbook_attach_sheet (wb, sheet);
			break;
		}

		case LOTUS_EOF:
			sheet = NULL;
			break;

		case LOTUS_INTEGER:
		{
			gint16 i = GINT16_FROM_LE (*(gint16 *)(r->data + 5));
			v = value_new_int (i);
			i = GUINT16_FROM_LE  (*(guint16 *)(r->data + 1));
		        j = GUINT16_FROM_LE  (*(guint16 *)(r->data + 3));
/*			rf = readfmt(p);  FIXME
			f = sf | rf;
			if (rf == FMT_DATE) {
				value.number 
					= from_wk1date(value.number,FALSE);
				sprintf(b, "%d", (int)value.number);
			}
			ins_data(buf, siod_interpreter, b,
				value, EXPRESSION, s, i, j);
				ins_format(buf,	s, i, j, fmt_old2new(f)); */
			cell = insert_value (sheet, i, j, v);
			break;
		}
		case LOTUS_NUMBER:
		{
			float_t num = LOTUS_GETDOUBLE (r->data + 5);
			v = value_new_float (num);
			i = GUINT16_FROM_LE (*(guint16 *)(r->data + 1));
		        j = GUINT16_FROM_LE (*(guint16 *)(r->data + 3));
/*			rf = readfmt(p); 
			f = sf | rf;
			if (rf == FMT_DATE || rf == FMT_TIME) {
				value.number = from_wk1date(value.number, 
							    rf == FMT_TIME);
				sprintf(b, "%d", (int) value.number);
			}
			ins_data(buf, siod_interpreter, b,
				value, EXPRESSION, s, i, j);
				ins_format(buf,	s, i, j, fmt_old2new(f)); */
			cell = insert_value (sheet, i, j, v);
			break;
		}
		case LOTUS_LABEL:
		{
			/* one of '\', '''', '"', '^' */
			gchar format_prefix = *(r->data + 5);
			v = value_new_string (r->data + 6); /* FIXME unsafe */
			i = GUINT16_FROM_LE (*(guint16 *)(r->data + 1));
		        j = GUINT16_FROM_LE (*(guint16 *)(r->data + 3));
/*			f = sf | readfmt(p); 
			ins_data(buf, siod_interpreter, (char *)r->data + 6,
				value, LABEL, s, i, j);
				ins_format(buf,	s, i, j, fmt_old2new(f)); */
			cell = insert_value (sheet, i, j, v);
			break;
		}
		case LOTUS_FORMULA:
		{
			ExprTree *f;
		/* 5-12 = value */
		/* 13-14 = formula r->length */
			i = GUINT16_FROM_LE (*(guint16 *)(r->data + 1));
                        j = GUINT16_FROM_LE (*(guint16 *)(r->data + 3));
/*                Ignore for now.
			f = sf | readfmt(p); 
			formula(GINT16_FROM_LE (*(gint16 *)(r->data + 13), r->data + 15, i, j));
			p1 = pop();
			value.number = LOTUS_GETDOUBLE (
			ins_data(buf, siod_interpreter, p1,
				value, EXPRESSION, s, i+1, j+1);
			cfree(p1);
			ins_format(buf,	s, i+1, j+1, fmt_old2new(f));*/
			f = lotus_parse_formula (sheet, i, j, r->data + 15, /* FIXME: unsafe */
						 GINT16_FROM_LE (*(gint16 *)(r->data + 13)));
			v = value_new_float (LOTUS_GETDOUBLE (r->data + 5));
			cell = insert_value (sheet, i, j, v);
			if (cell)
				cell_set_formula_tree (cell, f);
			break;
		}
		default:
			break;
		}
	}
	record_destroy (r);

	return !panic;
}

gboolean
lotus_read (Workbook *wb, const char *filename)
{
	FILE *f;
	
	if (!(f = fopen (filename, "rb")))
		return FALSE;

	cell_deep_freeze_redraws ();
	
	if (!read_workbook (wb, f)) {
		printf ("FIXME: Nasty workbook error, leaked\n");
		return FALSE;
	}

	workbook_recalc (wb);
	cell_deep_thaw_redraws ();

	fclose (f);
	return TRUE;
}

