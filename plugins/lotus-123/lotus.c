/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * lotus.c: Lotus 123 support for Gnumeric
 *
 * Authors:
 *    See: README
 *    Michael Meeks (mmeeks@gnu.org)
 *    Stephen Wood (saw@genhomepage.com)
 *    Morten Welinder (terra@gnome.org)
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "lotus.h"
#include "lotus-types.h"
#include "lotus-formula.h"

#include <workbook.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <gutils.h>
#include <parse-util.h>
#include <sheet-object-cell-comment.h>

#include <gsf/gsf-input.h>
#include <gsf/gsf-utils.h>
#include <string.h>

#define LOTUS_DEBUG 0

const gunichar lmbcs_group_1[256] = {
	0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
	0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
	0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
	0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC,
	0x00A8, 0x007E, 0x02DA, 0x005E, 0x0060, 0x00B4, 0x201C, 0x0027,
	0x2026, 0x2013, 0x2014, 0x2018, 0x2019, 0x0000, 0x2039, 0x203A,
	0x00A8, 0x007E, 0x02DA, 0x005E, 0x0060, 0x00B4, 0x201E, 0x201A,
	0x201D, 0x2017, 0x0000, 0x00A0, 0x0000, 0xFFFD, 0x0000, 0x0000,
	0x0152, 0x0153, 0x0178, 0x02D9, 0x02DA, 0x0000, 0x255E, 0x255F,
	0x258C, 0x2590, 0x25CA, 0x2318, 0xF8FF, 0xF8FE, 0x2126, 0x0000,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
	0x256A, 0x2561, 0x2562, 0x2556, 0x2555, 0x255C, 0x255B, 0x2567,
	0x0133, 0x0132, 0xF8FD, 0xF8FC, 0x0149, 0x0140, 0x013F, 0x00AF,
	0x02D8, 0x02DD, 0x02DB, 0x02C7, 0x007E, 0x005E, 0x0000, 0x0000,
	0x2020, 0x2021, 0x0126, 0x0127, 0x0166, 0x0167, 0x2122, 0x2113,
	0x014A, 0x014B, 0x0138, 0x0000, 0xF8FB, 0x2310, 0x20A4, 0x20A7,
	0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
	0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
	0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
	0x00FF, 0x00D6, 0x00DC, 0x00F8, 0x00A3, 0x00D8, 0x00D7, 0x0192,
	0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
	0x00BF, 0x00AE, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x00C1, 0x00C2, 0x00C0,
	0x00A9, 0x2563, 0x2551, 0x2557, 0x255D, 0x00A2, 0x00A5, 0x2510,
	0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x00E3, 0x00C3,
	0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x00A4,
	0x00F0, 0x00D0, 0x00CA, 0x00CB, 0x00C8, 0x0131, 0x00CD, 0x00CE,
	0x00CF, 0x2518, 0x250C, 0x2588, 0x2584, 0x00A6, 0x00CC, 0x2580,
	0x00D3, 0x00DF, 0x00D4, 0x00D2, 0x00F5, 0x00D5, 0x00B5, 0x00FE,
	0x00DE, 0x00DA, 0x00DB, 0x00D9, 0x00FD, 0x00DD, 0x00AF, 0x00B4,
	0x00AD, 0x00B1, 0x2017, 0x00BE, 0x00B6, 0x00A7, 0x00F7, 0x00B8,
	0x00B0, 0x00A8, 0x00B7, 0x00B9, 0x00B3, 0x00B2, 0x25A0, 0x00A0
};

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
cell_set_format_from_lotus_format (GnmCell *cell, int fmt)
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

	g_return_val_if_fail (r != NULL, LOTUS_EOF);

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

	r->data = (r->len == 0
		   ? (void *)""
		   : gsf_input_read (r->input, r->len, NULL));

#if LOTUS_DEBUG > 0
	g_print ("Record 0x%x length 0x%x\n", r->type, r->len);
	if (r->data)
		gsf_mem_dump (r->data, r->len);
#endif

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

static GnmCell *
insert_value (Sheet *sheet, guint32 col, guint32 row, GnmValue *val)
{
	GnmCell *cell;

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

	workbook_sheet_attach (wb, sheet);

	return sheet;
}

static gboolean
lotus_read_old (LotusWk1Read *state, record_t *r)
{
	gboolean result = TRUE;
	int sheetidx = 0;
	GnmCell    *cell;
	GnmValue	*v;
	guint16  fmt;	/* Format code of Lotus Cell */

	do {
		switch (r->type) {
		case LOTUS_BOF:
			state->sheet = attach_sheet (state->wb, sheetidx++);
			break;

		case LOTUS_EOF:
			state->sheet = NULL;
			break;

		case LOTUS_INTEGER: {
			GnmValue *v = value_new_int (GSF_LE_GET_GINT16 (r->data + 5));
			int i = GSF_LE_GET_GUINT16 (r->data + 1);
			int j = GSF_LE_GET_GUINT16 (r->data + 3);
			fmt = *(guint8 *)(r->data);

			cell = insert_value (state->sheet, i, j, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_NUMBER: {
			GnmValue *v = value_new_float (gsf_le_get_double (r->data + 5));
			int i = GSF_LE_GET_GUINT16 (r->data + 1);
			int j = GSF_LE_GET_GUINT16 (r->data + 3);
			fmt = *(guint8 *)(r->data);

			cell = insert_value (state->sheet, i, j, v);
			if (cell)
				cell_set_format_from_lotus_format (cell, fmt);
			break;
		}
		case LOTUS_LABEL: {
			/* one of '\', '''', '"', '^' */
/*			gchar format_prefix = *(r->data + 1 + 4);*/
			GnmValue *v = lotus_new_string (r->data + 6);
			int i = GSF_LE_GET_GUINT16 (r->data + 1);
			int j = GSF_LE_GET_GUINT16 (r->data + 3);
			cell = insert_value (state->sheet, i, j, v);
			if (cell) {
				fmt = *(guint8 *)(r->data);
				cell_set_format_from_lotus_format (cell, fmt);
			}
			break;
		}
		case LOTUS_FORMULA: {
			/* 5-12 = value */
			/* 13-14 = formula r->length */
			if (r->len >= 15) {
				int col = GSF_LE_GET_GUINT16 (r->data + 1);
				int row = GSF_LE_GET_GUINT16 (r->data + 3);
				int len = GSF_LE_GET_GINT16 (r->data + 13);
				GnmExpr const *expr;
				GnmParsePos pp;

				fmt = r->data[0];

#if DEBUG
				puts (cell_coord_name (col, row));
				gsf_mem_dump (r->data+5,8);
#endif
				if (r->len < (15+len))
					break;

				pp.eval.col = col;
				pp.eval.row = row;
				pp.sheet = state->sheet;
				pp.wb = pp.sheet->workbook;
				expr = lotus_parse_formula (state, &pp,
							    r->data + 15, len);

				v = NULL;
				if (0x7ff0 == (GSF_LE_GET_GUINT16 (r->data + 11) & 0x7ff8)) {
					/* I cannot find normative definition
					 * for when this is an error, an when
					 * a string, so we cheat, and peek
					 * at the next record.
					 */
					if (LOTUS_STRING == record_peek_next (r)) {
						record_next (r);
						v = lotus_new_string (r->data + 5);
					} else
						v = value_new_error_VALUE (NULL);
				} else
					v = value_new_float (gsf_le_get_double (r->data + 5));
				cell = sheet_cell_fetch (state->sheet, col, row);
				cell_set_expr_and_value (cell, expr, v, TRUE);

				gnm_expr_unref (expr);
				cell_set_format_from_lotus_format (cell, fmt);
			}
			break;
		}

		default:
			break;
		}
	} while (record_next (r));

	return result;
}

Sheet *
lotus_get_sheet (Workbook *wb, int i)
{
	g_return_val_if_fail (i >= 0 && i <= 255, NULL);

	while (i >= workbook_sheet_count (wb))
		workbook_sheet_add (wb, -1, FALSE);

	return workbook_sheet_by_index (wb, i);
}

char *
lotus_get_lmbcs (const char *data, int maxlen)
{
	GString *res = g_string_sized_new (maxlen + 2);
	const guint8 *p;
	const guint8 *theend;

	p = (const guint8 *)data;
	if (maxlen == -1)
		maxlen = strlen (data);
	theend = p + maxlen;

	while (p < theend) {
		switch (p[0]) {
		case 0:
			theend = p;
			break;

		case 0x01: {
			gunichar uc = lmbcs_group_1[p[1]];
			if (uc)
				g_string_append_unichar (res, uc);
			p += 2;
			break;
		}

		case 0x02: case 0x03: case 0x04:
		case 0x05: case 0x06: case 0x07: case 0x08:
		case 0x0b: case 0x0c: case 0x0e: case 0x0f: {
			unsigned code = (p[0] << 8) | p[1];
			g_warning ("Unhandled character 0x%04x", code);
			p += 2;
			/* See http://www.batutis.com/i18n/papers/lmbcs/ */
			break;
		}

		case 0x10: case 0x11: case 0x12: case 0x13:
		case 0x15: case 0x16: case 0x17: {
			unsigned code = (p[0] << 16) | (p[1] << 8) | p[2];
			g_warning ("Unhandled character 0x%06x", code);
			p += 3;
			/* See http://www.batutis.com/i18n/papers/lmbcs/ */
			break;
		}

		case 0x18: case 0x19: case 0x1a: case 0x1b:
		case 0x1c: case 0x1d: case 0x1e: case 0x1f:
			/* Ignore two bytes.  */
			p += 2;
			break;

		case 0x14: {
			/* Big-endian two-byte unicode with private-
			   use-area filled in by something.  */
			gunichar uc = (p[1] << 8) | p[2];
			if (uc >= 0xe000 && uc <= 0xf8ff) {
				g_warning ("Unhandled character 0x14%04x", uc);
			} else
				g_string_append_unichar (res, uc);
			p += 3;
		}

		default:
			if (p[0] <= 0x7f) {
				g_string_append_c (res, *p++);
			} else {
				/* Assume default group is 1.  */
				gunichar uc = lmbcs_group_1[*p++];
				if (uc)
					g_string_append_unichar (res, uc);
			}
			break;
		}
	}

	return g_string_free (res, FALSE);
}


static char *
lotus_get_cstr (const record_t *r, int ofs)
{
	if (ofs >= r->len)
		return NULL;
	else
		return lotus_get_lmbcs (r->data + ofs, r->len - ofs);
}

GnmValue *
lotus_new_string (gchar const *data)
{
	return value_new_string_nocopy
		(lotus_get_lmbcs (data, strlen (data)));
}

double
lotus_unpack_number (guint32 u)
{
	double v = (u >> 6);

	if (u & 0x20) v = 0 - v;
	if (u & 0x10)
		v = v / gnm_pow10 (u & 15);
	else
		v = v * gnm_pow10 (u & 15);

	return v;
}

static GnmValue *
get_lnumber (const record_t *r, int ofs)
{
	const guint8 *p;
	g_return_val_if_fail (ofs + 8 <= r->len, NULL);

	p = r->data + ofs;

	/* FIXME: Some special values indicate ERR, NA, BLANK, and string.  */

	if (1) {
		double v = gsf_le_get_double (p);
		return value_new_float (v);
	}
}


static gboolean
lotus_read_new (LotusWk1Read *state, record_t *r)
{
	gboolean result = TRUE;
	int sheetnameno = 0;

	do {
		switch (r->type) {
		case LOTUS_BOF:
			break;

		case LOTUS_EOF:
			goto done;

		case LOTUS_ERRCELL: {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *v = value_new_error_VALUE (NULL);
			(void)insert_value (sheet, col, row, v);
			break;
		}

		case LOTUS_NACELL: {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *v = value_new_error_NA (NULL);
			(void)insert_value (sheet, col, row, v);
			break;
		}

		case LOTUS_LABEL2: {
			/* one of '\', '''', '"', '^' */
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
/*			gchar format_prefix = *(r->data + ofs + 4);*/
			GnmValue *v = lotus_new_string (r->data + 5);
			(void)insert_value (sheet, col, row, v);
			break;
		}

		case LOTUS_NUMBER2: {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *v = get_lnumber (r, 4);
			(void)insert_value (sheet, col, row, v);
			break;
		}

		case LOTUS_STYLE: {
			guint16 subtype = GSF_LE_GET_GUINT16 (r->data);
			switch (subtype) {
			case 0xfa1: {
				/* Text style.  */
				guint16 styleid = GSF_LE_GET_GUINT16 (r->data + 2);
				guint8 fontid = GSF_LE_GET_GUINT8 (r->data + 9);
				guint16 fontsize = GSF_LE_GET_GUINT16 (r->data + 10);
				/* (FontSize * 100 / 83 + 16 ) / 32. */
				guint16 fg = GSF_LE_GET_GUINT16 (r->data + 12);
				guint16 bg = GSF_LE_GET_GUINT16 (r->data + 14);
				guint16 facebits = GSF_LE_GET_GUINT16 (r->data + 16);
				guint16 facemask = GSF_LE_GET_GUINT16 (r->data + 18);
				guint8 halign = GSF_LE_GET_GUINT8 (r->data + 20);
				guint8 valign = GSF_LE_GET_GUINT8 (r->data + 21);
				guint16 angle = GSF_LE_GET_GUINT16 (r->data + 22);
				break;
			}

			case 0xfd2: {
				/* Cell style.  */
				guint16 styleid = GSF_LE_GET_GUINT16 (r->data + 2);
				guint8 fontid = GSF_LE_GET_GUINT8 (r->data + 9);
				guint16 fontsize = GSF_LE_GET_GUINT16 (r->data + 10);
				/* (FontSize * 100 / 83 + 16 ) / 32. */
				guint16 textfg = GSF_LE_GET_GUINT16 (r->data + 12);
				guint16 textbg = GSF_LE_GET_GUINT16 (r->data + 14);
				guint16 facebits = GSF_LE_GET_GUINT16 (r->data + 16);
				guint16 facemask = GSF_LE_GET_GUINT16 (r->data + 18);
				guint8 halign = GSF_LE_GET_GUINT8 (r->data + 20);
				guint8 valign = GSF_LE_GET_GUINT8 (r->data + 21);
				guint16 angle = GSF_LE_GET_GUINT16 (r->data + 22);
				guint16 intfg = GSF_LE_GET_GUINT16 (r->data + 24);
				guint16 intbg = GSF_LE_GET_GUINT16 (r->data + 26);
				guint8 intpat = GSF_LE_GET_GUINT8 (r->data + 28);
				break;
			}

			default:
				g_print ("Unknown style record 0x%x/%04x of length %d.\n",
					 r->type, subtype,
					 r->len);

			case 0xfab: /* Edge style */
			case 0xfb4: /* Interior style */
			case 0xfc9: /* Frame style */
			case 0xfdc: /* Fontname style */
			case 0xfe6: /* Named style */
			case 0xffa: /* Style pool */
				break;
			}
			break;
		}

		case LOTUS_PACKED_NUMBER: {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			double v = lotus_unpack_number (GSF_LE_GET_GUINT32 (r->data + 4));
			GnmValue *val;

			if (v == gnm_floor (v) &&
			    v >= G_MININT &&
			    v <= G_MAXINT)
				val = value_new_int ((int)v);
			else
				val = value_new_float (v);

			(void)insert_value (sheet, col, row, val);
			break;
		}

		case LOTUS_SHEET_NAME: {
			Sheet *sheet = lotus_get_sheet (state->wb, sheetnameno++);
			char *name = lotus_get_cstr (r, 10);
			g_return_val_if_fail (name != NULL, FALSE);
			/* Name is followed by something indicating tab colour.  */
			g_object_set (sheet, "name", name, NULL);
			g_free (name);
			break;
		}

		case LOTUS_NAMED_SHEET:
			/*
			 * Compare LOTUS_SHEET_NAME.  It is unclear why there
			 * are two such records.
			 */
			break;

		case LOTUS_FORMULA2: {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			GnmValue *curval = get_lnumber (r, 4);
			GnmParsePos pp;

			pp.eval.col = col;
			pp.eval.row = row;
			pp.sheet = sheet;
			pp.wb = sheet->workbook;
			const GnmExpr *expr =
				lotus_parse_formula (state, &pp,
						     r->data + 12, r->len - 12);
			GnmCell *cell = sheet_cell_fetch (sheet, col, row);
			cell_set_expr_and_value (cell, expr, curval, TRUE);

			gnm_expr_unref (expr);
			break;
		}

		case LOTUS_CA_DB:
		case LOTUS_DEFAULTS_DB:
		case LOTUS_NAMED_STYLE_DB:

		case LOTUS_RLDB_DEFAULTS:
		case LOTUS_RLDB_NAMEDSTYLES:
		case LOTUS_RLDB_STYLES:
		case LOTUS_RLDB_FORMATS:
		case LOTUS_RLDB_BORDERS:
		case LOTUS_RLDB_COLWIDTHS:
		case LOTUS_RLDB_ROWHEIGHTS:
		case LOTUS_RL2DB:
		case LOTUS_RL3DB:
		case LOTUS_RLDB_NODE:
		case LOTUS_RLDB_DATANODE:
		case LOTUS_RLDB_REGISTERID:
		case LOTUS_RLDB_USEREGISTEREDID:
		case LOTUS_RLDB_PACKINFO:
			/* Style database related.  */
			break;

		case LOTUS_DOCUMENT_1:
		case LOTUS_DOCUMENT_2:
			break;

		case LOTUS_PRINT_SETTINGS:
		case LOTUS_PRINT_STRINGS:
			break;

		case LOTUS_LARGE_DATA:
			g_warning ("Unhandled \"large data\" record seen.");
			break;

		case LOTUS_CELL_COMMENT: {
			int row = GSF_LE_GET_GUINT16 (r->data);
			Sheet *sheet = lotus_get_sheet (state->wb, r->data[2]);
			int col = r->data[3];
			char *text = lotus_get_cstr (r, 5);
			GnmCellPos pos;

			pos.col = col;
			pos.row = row;
			cell_set_comment (sheet, &pos, NULL, text);
			g_free (text);			
			break;
		}

		default:
			g_print ("Unknown record 0x%x of length %d.\n", r->type, r->len);

		case LOTUS_CALCORDER:
		case LOTUS_USER_RANGE:
		case LOTUS_ZEROFORCE:
		case LOTUS_SORTKEY_DIR:
		case LOTUS_DTLABELMISC:
		case LOTUS_CPA:
		case LOTUS_PERSISTENT_ID:
		case LOTUS_WINDOW:
		case LOTUS_BEGIN_OBJECT:
		case LOTUS_END_OBJECT:
		case LOTUS_BEGIN_GROUP:
		case LOTUS_END_GROUP:
		case LOTUS_DOCUMENT_WINDOW:
		case LOTUS_OBJECT_SELECT:
		case LOTUS_OBJECT_NAME_INDEX:
		case LOTUS_STYLE_MANAGER_BEGIN:
		case LOTUS_STYLE_MANAGER_END:
		case LOTUS_WORKBOOK_VIEW:
		case LOTUS_SPLIT_MANAGEMENT:
		case LOTUS_SHEET_OBJECT_ID:
		case LOTUS_SHEET:
		case LOTUS_SHEET_VIEW:
		case LOTUS_FIRST_WORKSHEET:
		case LOTUS_SHEET_PROPS:
		case LOTUS_RESERVED_288:
		case LOTUS_SCRIPT_STREAM:
		case LOTUS_RANGE_REGION:
		case LOTUS_RANGE_MISC:
		case LOTUS_RANGE_ALIAS:
		case LOTUS_DATA_FILL:
		case LOTUS_BACKSOLVER:
		case LOTUS_SORT_HEADER:
		case LOTUS_CELL_EOF:
		case LOTUS_FILE_PREFERENCE:
		case LOTUS_END_DATA:
			break;
		}
	} while (record_next (r));

 done:
	/*
	 * Newer formats have something that looks like document
	 * properties after the EOF record.
	 */

	if (workbook_sheet_count (state->wb) < 1)
		result = FALSE;

	return result;
}

gboolean
lotus_read (LotusWk1Read *state)
{
	record_t *r = record_new (state->input);
	gboolean result;

	if (record_next (r) && r->type == LOTUS_BOF) {
		state->version = GSF_LE_GET_GUINT16 (r->data);
		switch (state->version) {
		case LOTUS_VERSION_ORIG_123:
		case LOTUS_VERSION_SYMPHONY:
			result = lotus_read_old (state, r);
			break;
		default:
			g_warning ("Unexpected version %x", state->version);
			/* Fall through.  */
		case LOTUS_VERSION_123V6:
		case LOTUS_VERSION_123SS98:
			result = lotus_read_new (state, r);
		}
	} else
		result = FALSE;

	record_destroy (r);

	return result;
}
