/*
 * pln.c: read sheets using a Plan Perfect encoding.
 *
 * Kevin Handy <kth@srv.net>
 *	Based on ff-csv code.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <goffice/goffice.h>
#include <gnm-plugin.h>
#include <sheet.h>
#include <ranges.h>
#include <value.h>
#include <expr.h>
#include <cell.h>
#include <workbook.h>
#include <workbook-view.h>
#include <parse-util.h>
#include <sheet-style.h>
#include <style.h>
#include <mstyle.h>

#include <gsf/gsf-utils.h>
#include <gsf/gsf-input.h>
#include <string.h>
#include <math.h>

GNM_PLUGIN_MODULE_HEADER;

gboolean pln_file_probe (GOFileOpener const *fo, GsfInput *input,
			 GOFileProbeLevel pl);
void     pln_file_open (GOFileOpener const *fo, GOIOContext *io_context,
			WorkbookView *wb_view, GsfInput *input);

static char const *formula1[] = {
	NULL,			/* 0 */
	"-(",
	"ABS(",
	"INT(",
	"SIGN(",
	"NOT(",
	"TRUE(",
	"FALSE(",
	"AND(",
	"OR(",
	"AVERAGE(",			/* 10 */
	"COUNT(",
	"MIN(",
	"MAX(",
	"NA(",
	"ISNA(",
	"NOW(",
	"TODAY(",
	"FACT(",
	"ROW(",
	"COLUMN("			/* 20 */
};

static char const * formula2[] =
{
	"?bad1?(",		/* 0 */
	"POWER(",
	"LN(",
	"LOG(",
	"SQRT(",
	"PI(",
	"EXP(",
	"SIN(",
	"COS(",
	"TAN(",
	"MOD(",			/* 10 */
	"ASIN(",
	"ACOS(",
	"ATAN(",
	"TERM(",
	"PV(",
	"PMT(",
	"FV(",
	"NPV(",
	"LOOKUP(",
	"INDEX(",		/* 20 */
	"ROUND(",
	"STDEV(",
	"CONCAT(",
	"MID(",
	"LENGTH(",
	"VALUE(",
	"TEXT(",
	"MDY(",
	"MONTH(",
	"DAY(",			/* 30 */
	"YEAR(",
	"DATETEXT(",
	"DATEVALUE(",
	"VAR(",
	"RANDOM(",
	"CURRENCY(",
	"ITERATION(",
	"ISVALUE(",
	"ISTEXT(",
	"REPLACE(",		/* 40 */
	"RADIANS(",
	"CELL(",
	"SUBTRACT(",
	"IRR(",
	"FIND(",
	"LEFT(",
	"RIGHT(",
	"UPPER(",
	"LOWER(",
	"PROPER(",
	"CHAR(",		/* 50 */
	"CODE(",
	"TRIM(",
	"REPEAT(",
	"BLOCK(",
	"CURSOR(",
	"DDB(",
	"SLN(",
	"SYD(",
	"RATE(",		/* 60 */
	"STATUS(",
	"FOREACH(",
	"DEGREES(",
	"HOUR(",
	"MINUTE(",
	"SECOND(",
	"HMS(",
	"TIMETEXT(",
	"TIMEVALUE(",
	"PRODUCT(",		/* 70 */
	"QUOTIENT(",
	"VARP(",
	"STDEVP(",
	"ATAN2(",
	"MATCH(",
	"MATCH2(",
	"LOOKUP2(",
	"LINK(",
	"ISERR(",
	"ISERR2(",		/* 80 */
	"CHOOSE("
};

typedef struct {
	Sheet *sheet;
	GHashTable *styles;
} PlanPerfectImport;

static guint8 const signature[] =
    { 0xff, 'W','P','C', 0x10, 0, 0, 0, 0x9, 0xa };

/* in charset.c. */
guint8 *pln_get_str (guint8 const *ch, unsigned len);

static char const *
pln_get_func_table1 (unsigned i)
{
	g_return_val_if_fail (0 < i && i < G_N_ELEMENTS (formula1), "ERROR");
	return formula1 [i];
}
static char const *
pln_get_func_table2 (unsigned i)
{
	g_return_val_if_fail (0 < i && i < G_N_ELEMENTS (formula2), "ERROR");
	return formula2 [i];
}

gboolean
pln_file_probe (GOFileOpener const *fo, GsfInput *input,
		GOFileProbeLevel pl)
{
	/*
	 * a plan-perfect header
	 *	0	= -1
	 *	1-3	= "WPC"
	 *	4-7	= 16 (double word)
	 *	8	= 9 (plan perfect file)
	 *	9	= 10 (worksheet file)
	 *	10	= major version number
	 *	11	= minor version number
	 *	12-13	= encryption key
	 *	14-15	= unused
	 */
	char const *header = NULL;
	if (!gsf_input_seek (input, 0, G_SEEK_SET))
		header = gsf_input_read (input, sizeof (signature), NULL);
	return header != NULL &&
		memcmp (header, signature, sizeof (signature)) == 0;
}

static GnmStyle *
pln_get_style (PlanPerfectImport *state, guint8 const* data, gboolean is_cell)
{
	guint16 attr, fmt, font;
	guint32 key;
	GnmStyle *res;

	attr = GSF_LE_GET_GUINT16 (data);
	fmt  = GSF_LE_GET_GUINT16 (data+2);
	font = GSF_LE_GET_GUINT16 (data+4);

	/* Check for use of sheet defaults */
	if (is_cell) {
		GnmStyle *def = sheet_style_default (state->sheet);
		if ((attr & 0x0700) == 0x0400) {
			attr &= 0xf8ff;
			switch (gnm_style_get_align_h (def)) {
			default :
			case GNM_HALIGN_GENERAL:break;
			case GNM_HALIGN_LEFT:	attr |= 0x0100; break;
			case GNM_HALIGN_RIGHT:	attr |= 0x0200; break;
			case GNM_HALIGN_DISTRIBUTED:
			case GNM_HALIGN_CENTER_ACROSS_SELECTION :
			case GNM_HALIGN_CENTER:	attr |= 0x0300; break;
			}
		}
		if ((attr & 0x8000)) {
			gboolean is_locked = gnm_style_get_contents_locked (def);
			attr = (attr & 0x3fff) | (is_locked ? 0x4000 : 0);
		}
		gnm_style_unref (def);
	}

	/* bit bash a key containing all relevant info */
	key = fmt << 16;
	key |= font & 0xf800;
	key |= (attr >> 4) & 0x07ff; /* drop type, hide 0, and top lock bit */

	res = g_hash_table_lookup (state->styles, GINT_TO_POINTER (key));
	if (res == NULL) {
		static GnmHAlign const haligns[] = {
			GNM_HALIGN_GENERAL, GNM_HALIGN_LEFT, GNM_HALIGN_RIGHT, GNM_HALIGN_CENTER
		};
		res = gnm_style_new_default ();
		gnm_style_set_font_italic (res, (attr & 0x0010) ? TRUE : FALSE);
		gnm_style_set_contents_hidden (res, (attr & 0x0020) ? TRUE : FALSE);
		gnm_style_set_font_uline (res,
			(attr & 0x1000) ?  UNDERLINE_DOUBLE :
			((attr & 0x0040) ?  UNDERLINE_SINGLE : UNDERLINE_NONE));
		gnm_style_set_font_bold (res, (attr & 0x0080) ? TRUE : FALSE);
		gnm_style_set_align_h (res, haligns [(attr & 0x300) >> 8]);

		g_hash_table_insert (state->styles, GINT_TO_POINTER (key), res);
#warning generate formats
	}

	gnm_style_ref (res);
	return res;
}

static gnm_float
pln_get_number (guint8 const * ch)
{
	int exp;
	gnm_float dvalue, scale = 256.0;
	int i;

	dvalue = 0.0;
	exp = *ch;
	for (i = 1; i <= 7; i++) {
		dvalue += ch[i] / scale;
		scale *= 256;
	}
	if (exp & 128)
		dvalue = -dvalue;
	dvalue = gnm_ldexp (dvalue, ((exp & 127) - 64) * 4);

	return dvalue;
}

static char *
pln_get_addr (GnmParsePos const *pp, guint8 const *ch)
{
	guint16 r = GSF_LE_GET_GUINT16 (ch);
	guint16 c = GSF_LE_GET_GUINT16 (ch+2);
	GnmCellRef ref;
	GnmConventionsOut out;

	ref.sheet = NULL;
	ref.col_relative = ref.row_relative = FALSE;
	ref.col = (c & 0x3fff);
	ref.row = (r & 0x3fff);

	switch (c & 0xc000) {
	case 0xc000:	ref.col = *((gint16 *)&c);
	case 0x0000:	ref.col_relative = TRUE;
		break;
	default :
		break;
	}
	switch (r & 0xc000) {
	case 0xc000:	ref.row = *((gint16 *)&r);
	case 0x0000:	ref.row_relative = TRUE;
		break;
	default :
		break;
	}

	out.accum = g_string_new (NULL);
	out.pp    = pp;
	out.convs = gnm_conventions_default;
	cellref_as_string (&out, &ref, TRUE);

	return g_string_free (out.accum, FALSE);
}

static char *
pln_convert_expr (GnmParsePos const *pp, guint8 const *ch, size_t datalen)
{
	GString *expr = g_string_new (NULL);
	guint8 *str;
	guint8 const *end;
	int len, code;
	unsigned ui;

	g_return_val_if_fail (datalen >= 2, g_string_free (expr, FALSE));

	/* Expressions are stored INFIX so it is easier to just generate text */
	ui = GSF_LE_GET_GUINT16 (ch);
	g_return_val_if_fail (ui <= datalen - 2, g_string_free (expr, FALSE));

	ch += 2;
#if DEBUG_EXPR
	puts (cellpos_as_string (&pp->eval));
	gsf_mem_dump (ch, ui);
#endif
	for (end = ch + ui ; ch < end ; ) {
		code = *ch++;
		switch (code) {
		case  1: g_string_append_c (expr, '+');	break;
		case  5:	 /* Unary minus */
		case  2: g_string_append_c (expr, '-');	break;
		case  3: g_string_append_c (expr, '*');	break;
		case  4: g_string_append_c (expr, '/');	break;
		case  6: g_string_append_c (expr, '%');	break;
		case  7: g_string_append   (expr, "SUM("); break;
		case 11: g_string_append_c (expr, '^'); break;

		case 9:	/* Text constant */
			len = *ch;
			str = pln_get_str (ch + 1, len);
			g_string_append_c (expr, '\"');
			go_strescape (expr, str);
			g_string_append_c (expr, '\"');
			ch += len + 1;
			g_free (str);
			break;

		case 10: /* Named block */
			len = *ch;
			g_string_append_len (expr, ch + 1, len);
			ch += len + 1;
			break;

		case 12: g_string_append (expr, pln_get_func_table1 (*ch++));
			break;
		case 13: g_string_append (expr, pln_get_func_table2 (*ch++));
			break;
		case 14: /* Special '+' which sums contiguous cells */
			g_string_append   (expr, "?+?");
			break;
		case 15: g_string_append   (expr, "_MOD_"); break;
		case 16: g_string_append   (expr, "_NOT_"); break;
		case 17: g_string_append   (expr, "_AND_"); break;
		case 18: g_string_append   (expr, "_OR_"); break;
		case 19: g_string_append   (expr, "_XOR_"); break;
		case 20: g_string_append   (expr, "IF("); break;

		case 21:	/* Compare function */
			switch (*ch) {
			case 1: g_string_append   (expr, "="); break;
			case 2: g_string_append   (expr, "<>"); break;
			case 3: g_string_append   (expr, ">"); break;
			case 4: g_string_append   (expr, ">="); break;
			case 5: g_string_append   (expr, "<"); break;
			case 6: g_string_append   (expr, "<="); break;
			default:
				g_warning ("unknown comparative operator %u", *ch);
			}
			ch++;
			break;

		case 22: g_string_append_c (expr, ',');	break;
		case 23: g_string_append_c (expr, '(');	break;
		case 24: g_string_append_c (expr, ')');	break;

		case 25: {
			unsigned sp = *ch++;
			go_string_append_c_n (expr, ' ', sp);
			break;
		}

		case 26:	/* Special formula error code */
			g_string_append (expr, "??ERROR??");
			break;

		case 27:	/* Cell reference */
			str = pln_get_addr  (pp, ch);
			g_string_append (expr, str);
			g_free (str);
			ch += 4;
			break;

		case 28:	/* Block reference */
			str = pln_get_addr  (pp, ch);
			g_string_append   (expr, str);
			g_free (str);
			g_string_append_c (expr, ':');
			str = pln_get_addr  (pp, ch+4);
			g_string_append   (expr, str);
			g_free (str);
			ch += 8;
			break;

		case 29: g_string_append (expr, "<>1"); /* ?? is this right ?? */
			break;

		case 30:	/* Floating point constant */
			len = ch [8];  /* they store the ascii ?? will we be screwed by locale ? */
			g_string_append_len (expr, ch+9, len);
			ch += 9 + len;
			break;

		case 31:	/* Reference to passed argument in user defined function */
			g_string_append (expr, "_unknown31_");
			ch++; /* ignore arg number */
			break;

		case 32:	/* User function */
			g_string_append (expr, "_unknown32_");
			len = *ch;
			ch += len + 1;
			break;

		case 33:	/* Temporary variable (#:=) */
			len = ch [1];
			g_string_append (expr, "_unknown33_");
			g_string_append_len (expr, ch+2, len);
			ch += 2 + len;
			break;

		case 34:	/* Temporary variable (#) */
			len = ch [1];
			g_string_append (expr, "_unknown34_");
			g_string_append_len (expr, ch+2, len);
			ch += 2 + len;
			break;

		case 35: g_string_append (expr, "0.");
			break;

		case 36: g_string_append_c (expr, '{'); break;
		case 37: g_string_append_c (expr, ')'); break;
		case 38: g_string_append (expr, "FACTORIAL");  break;
		case 39: g_string_append (expr, "LOOKUP<");  break;
		case 40: g_string_append (expr, "LOOKUP>");  break;

		case 41:		/* Attribute on */
		case 42:		/* Attribute off */
			 ch++; /* ignore */
			break;

		case 43:	/* Total attributes for formula */
			ch += 2;
			break;

		case 44:	/* Conditional attribute */ break;
		case 45:	/* Assumed multiply - nop display */ break;

		case 46:	/* Date format */
			ch++;
			break;

		default:
			g_warning("PLN: Undefined formula code %d", code);
		}
	}

	return g_string_free (expr, FALSE);
}

/* Font width should really be calculated, but it's too hard right now */
#define FONT_WIDTH 8
static double
pln_calc_font_width (guint16 cwidth, gboolean permit_default)
{
	return (cwidth & 0xff) * FONT_WIDTH;
}

static GOErrorInfo *
pln_parse_sheet (GsfInput *input, PlanPerfectImport *state)
{
	int max_col = gnm_sheet_get_max_cols (state->sheet);
	int max_row = gnm_sheet_get_max_rows (state->sheet);
	int i, rcode, rlength;
	guint8 const *data;
	GnmValue   *v;
	GnmStyle  *style;
	GnmParsePos pp;
	GnmRange r;

	range_init (&r, 0,0,0, gnm_sheet_get_max_rows (state->sheet));
	parse_pos_init_sheet (&pp, state->sheet);

	data = gsf_input_read (input, 16, NULL);
	if (data == NULL || GSF_LE_GET_GUINT16 (data + 12) != 0)
		return go_error_info_new_str (_("PLN : Spreadsheet is password encrypted"));

	/* Process the record based sections
	 * Each record consists of a two-byte record type code,
	 * followed by a two byte length
	 */
	do {
		data = gsf_input_read (input, 4, NULL);
		if (data == NULL)
			break;

		rcode   = GSF_LE_GET_GUINT16 (data);
		rlength = GSF_LE_GET_GUINT16 (data + 2);

		data = gsf_input_read (input, rlength, NULL);
		if (data == NULL)
			break;

		switch (rcode) {
		case 0x01:
			/* guint16 last row with data;
			 */
			 max_col = GSF_LE_GET_GUINT16 (data + 2);
			break;

		case 0x02:
			/* char ascii char of decimal point		0
			 * char ascii char of thousands separator	1
			 * WPCHAR 1-6 bytes for currency symbol		2
			 * guint16 default worksheet attributs		8
			 * guint16 default worksheet format		10
			 * guint32 default font				12
			 * guint16  default col width			16
			 */
			break;

		case 0x03:	/* Column format information */
			for (i = 0; i < rlength / 6; i++, data += 6)
				if (i <= max_col) {
					double const width = pln_calc_font_width (
						GSF_LE_GET_GUINT16 (data + 4), TRUE);
					sheet_col_set_size_pts (state->sheet, i, width, FALSE);
					r.start.col = r.end.col = i;
					sheet_style_apply_range (state->sheet, &r,
						pln_get_style (state, data, FALSE));
				}
			break;

		default:
			;
			/* g_warning("PLN : Record handling code for code %d not yet written", rcode); */
		}
	} while (rcode != 25);

	/* process the CELL information */
	while (NULL != (data = gsf_input_read (input, 20, NULL))) {
		GnmExprTop const *texpr = NULL;
		GnmCell *cell = NULL;
		unsigned type = GSF_LE_GET_GUINT16 (data + 12);
		unsigned length = GSF_LE_GET_GUINT16 (data + 18);

		pp.eval.row = GSF_LE_GET_GUINT16 (data + 0);
		pp.eval.col = GSF_LE_GET_GUINT16 (data + 2);
		/* Special value indicating end of sheet */
		if (pp.eval.row == 0xFFFF)
			return NULL;

		if (pp.eval.row > max_row)
			return go_error_info_new_printf (
				_("Ignoring data that claims to be in row %u which is > max row %u"),
				pp.eval.row, max_row);
		if (pp.eval.col > max_col)
			return go_error_info_new_printf (
				_("Ignoring data that claims to be in column %u which is > max column %u"),
				pp.eval.col, max_col);

		v = NULL;
		if ((type & 0x7) != 0) {
			style = pln_get_style (state, data, TRUE);
			if (style != NULL)
				sheet_style_set_pos (state->sheet, pp.eval.col, pp.eval.row, style);
			if ((type & 0x7) != 6)
				cell = sheet_cell_fetch (state->sheet, pp.eval.col, pp.eval.row);
		} else {
			style = NULL;
		}

		switch (type & 0x7) {
		/* Empty Cell */
		case 0:
			if (length != 0) {
				g_warning ("an empty unformated cell has an expression ?");
			} else
				continue;

		/* Floating Point */
		case 1: v = value_new_float (pln_get_number (data + 4));
			break;
		/* Short Text */
		case 2:
			v = value_new_string_nocopy (
				pln_get_str (data + 5, data[4]));
			break;
		/* Long Text */
		case 3: data = gsf_input_read (input, GSF_LE_GET_GUINT16 (data+4), NULL);
			if (data != NULL)
				v = value_new_string_nocopy (
					pln_get_str (data + 2, GSF_LE_GET_GUINT16 (data)));
			break;
		/* Error Cell */
		case 4: v = value_new_error_VALUE (NULL);
			break;
		/* na Cell */
		case 5: v = value_new_error_NA (NULL);
			break;
		/* format only, no data in cell */
		case 6: break;
		}

		if (length != 0) {
			data = gsf_input_read (input, length, NULL);
			if (cell != NULL && data != NULL) {
				char *expr_txt = pln_convert_expr (&pp, data, length);

				if (expr_txt != NULL) {
					texpr = gnm_expr_parse_str (expr_txt, &pp,
								   GNM_EXPR_PARSE_DEFAULT,
								   gnm_conventions_default,
								   NULL);
					if (texpr == NULL) {
						value_release (v);
						v = value_new_string_nocopy (expr_txt);
					} else
						g_free (expr_txt);
				}
			}
		}

		if (texpr != NULL) {
			if (v != NULL)
				gnm_cell_set_expr_and_value (cell, texpr, v, TRUE);
			else
				gnm_cell_set_expr (cell, texpr);
			gnm_expr_top_unref (texpr);
		} else if (v != NULL)
			gnm_cell_set_value (cell, v);
	}

	return NULL;
}

void
pln_file_open (GOFileOpener const *fo, GOIOContext *io_context,
               WorkbookView *wb_view, GsfInput *input)
{
	Workbook *wb;
	char  *name;
	Sheet *sheet;
	GOErrorInfo *error;
	PlanPerfectImport state;

	wb    = wb_view_get_workbook (wb_view);
	name  = workbook_sheet_get_free_name (wb, "PlanPerfect", FALSE, TRUE);
	sheet = sheet_new (wb, name, 256, 65536);
	g_free (name);
	workbook_sheet_attach (wb, sheet);
	sheet_flag_recompute_spans (sheet);

	state.sheet  = sheet;
	state.styles = g_hash_table_new_full (
		g_direct_hash, g_direct_equal,
		NULL, (GDestroyNotify) gnm_style_unref);
	error = pln_parse_sheet (input, &state);
	g_hash_table_destroy (state.styles);
	if (error != NULL) {
		workbook_sheet_delete (sheet);
		go_io_error_info_set (io_context, error);
	}
}
