/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * lotus.c: Lotus 123 support for Gnumeric
 *
 * Authors:
 *    See: README
 *    Michael Meeks (mmeeks@gnu.org)
 *    Stephen Wood (saw@genhomepage.com)
 *    Morten Welinder (terra@diku.dk)
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "lotus.h"
#include "lotus-types.h"
#include "lotus-formula.h"

#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <gutils.h>
#include <plugin-util.h>
#include <parse-util.h>

#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <string.h>

#define LOTUS_DEBUG 0

static const char *
lotus_special_formats[16] = {
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
		s = s + strlen (s);
		*s++ = '.';
		while (n--)
			*s++ = '0';
		*s = 0;
	}
}

static void
cell_set_format_from_lotus_format (Cell *cell, int fmt)
{
	int fmt_type  = (fmt >> 4) & 0x7;
	int precision = fmt&0xf;
	char fmt_string[100];

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
		strcpy (fmt_string,  lotus_special_formats[precision]);
		break;

	default:
		strcpy (fmt_string, "");
		break;

	}
	if (fmt_string[0])
		cell_set_format (cell, fmt_string);
#if LOTUS_DEBUG > 0
	printf ("Format: %s\n", fmt_string);
#endif
}

typedef struct {
	GsfInput *input;
	guint16   type;
	guint16   len;
	guint8 const *data;
} record_t;

static record_t *
record_new (GsfInput *input)
{
	record_t *r = g_new (record_t, 1);
	r->input    = input;
	r->data     = NULL;
	r->type     = 0;
	r->len      = 0;
	return r;
}

static guint16
record_peek_next (record_t *r)
{
	guint8 const *header;
	guint16 type;

	g_return_val_if_fail (r != NULL, FALSE);

	header = gsf_input_read (r->input, 2, NULL);
	if (header == NULL)
		return 0xffff;
	type = GSF_LE_GET_GUINT16 (header);
	gsf_input_seek (r->input, -2, G_SEEK_CUR);
	return type;
}

static gboolean
record_next (record_t *r)
{
	guint8 const *header;

	g_return_val_if_fail (r != NULL, FALSE);

	header = gsf_input_read (r->input, 4, NULL);
	if (header == NULL)
		return FALSE;

	r->type = GSF_LE_GET_GUINT16 (header);
	r->len  = GSF_LE_GET_GUINT16 (header + 2);

#if LOTUS_DEBUG > 0
	printf ("Record 0x%x length 0x%x\n", r->type, r->len);
#endif

	r->data = gsf_input_read (r->input, r->len, NULL);
	return (r->data != NULL);
}

static void
record_destroy (record_t *r)
{
	if (r) {
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

	cell_set_value (cell, val);

#if LOTUS_DEBUG > 0
	printf ("Inserting value at %s:\n",
		cell_name (cell));
	value_dump (val);
#endif
	return cell;
}

static Sheet *
attach_sheet (Workbook *wb, int idx)
{
	/* Yes I do mean col_name.  Use that as an easy proxy for naming the sheets 
	 * similarly to lotus.
	 */
	Sheet *sheet = sheet_new (wb, col_name (idx));

	/* in case nothing forces a spanning, do it here so that any new
	 * content will get spanned.
	 */
	sheet_flag_recompute_spans (sheet);

	workbook_sheet_attach (wb, sheet, NULL);

	return sheet;
}

/* buf was old siag wb / sheet */
gboolean
lotus_wk1_read (LotusWk1Read *state)
{
	gboolean result = TRUE;
	int sheetidx = 0;
	Cell    *cell;
	Value	*v;
	guint16  fmt;	/* Format code of Lotus Cell */
	record_t *r;

	state->sheet = attach_sheet (state->wb, sheetidx++);

	r = record_new (state->input);

	while (record_next (r)) {
		if (sheetidx == 0 && r->type != 0) {
			result = FALSE;
			break;
		}

		switch (r->type) {
		case LOTUS_BOF :
			if (sheetidx > 1)
				state->sheet = attach_sheet (state->wb, sheetidx++);
			break;

		case LOTUS_EOF :
			state->sheet = NULL;
			break;

		case LOTUS_INTEGER : {
			Value *v = value_new_int (GSF_LE_GET_GINT16 (r->data + 5));
			int i = GSF_LE_GET_GUINT16 (r->data + 1);
			int j = GSF_LE_GET_GUINT16 (r->data + 3);
			fmt = *(guint8 *)(r->data);

			cell = insert_value (state->sheet, i, j, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_NUMBER : {
			Value *v = value_new_float (gsf_le_get_double (r->data + 5));
			int i = GSF_LE_GET_GUINT16 (r->data + 1);
			int j = GSF_LE_GET_GUINT16 (r->data + 3);
			fmt = *(guint8 *)(r->data);

			cell = insert_value (state->sheet, i, j, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_LABEL : {
			/* one of '\', '''', '"', '^' */
/*			gchar format_prefix = *(r->data + 5);*/
			Value *v = lotus_new_string (state, r->data + 6);
			int i = GSF_LE_GET_GUINT16 (r->data + 1);
			int j = GSF_LE_GET_GUINT16 (r->data + 3);
			fmt = *(guint8 *)(r->data);
			cell = insert_value (state->sheet, i, j, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_FORMULA : {
			/* 5-12 = value */
			/* 13-14 = formula r->length */
			if (r->len >= 15) {
				int col = GSF_LE_GET_GUINT16 (r->data + 1);
				int row = GSF_LE_GET_GUINT16 (r->data + 3);
				int len = GSF_LE_GET_GINT16 (r->data + 13);
				GnmExpr const *expr;

				fmt = r->data[0];

#if DEBUG
				puts (cell_coord_name (col, row));
				gsf_mem_dump (r->data+5,8);
#endif
				if (r->len < (15+len))
					break;

				expr = lotus_parse_formula (state, col, row,
					r->data + 15, len);

				v = NULL;
				if (0x7ff0 == (GSF_LE_GET_GUINT16 (r->data + 11) & 0x7ff8)) {
					/* I can not find normative definition
					 * for when this is an error, an when
					 * a string, so we cheat, and peek
					 * at the next record.
					 */
					if (LOTUS_STRING == record_peek_next (r)) {
						record_next (r);
						v = lotus_new_string (state, r->data + 5);
					} else
						v = value_new_error (NULL,  gnumeric_err_VALUE);
				} else
					v = value_new_float (gsf_le_get_double (r->data + 5));
				cell = sheet_cell_fetch (state->sheet, col, row),
				cell_set_expr_and_value (cell, expr, v, TRUE);

				gnm_expr_unref (expr);
				cell_set_format_from_lotus_format (cell, fmt);
			}
			break;
		}

		default:
			break;
		}
	}
	record_destroy (r);

	return result;
}

Value *
lotus_new_string (LotusWk1Read *state, gchar const *data)
{
	return value_new_string_nocopy (
		g_convert_with_iconv (data, -1, state->converter,
				      NULL, NULL, NULL));
}
