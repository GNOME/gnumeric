/*
 * sylk.c - file import of Multiplan/Excel SYLK files
 *
 * Jody Goldberg   <jody@gnome.org>
 * Copyright (C) 2003-2007 Jody Goldberg (jody@gnome.org)
 *
 * Miguel de Icaza <miguel@gnu.org>
 * Based on work by
 *	Jeff Garzik <jgarzik@mandrakesoft.com>
 * With some code from:
 *	csv-io.c: read sheets using a CSV encoding.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <goffice/goffice.h>
#include <workbook-view.h>
#include <workbook.h>
#include <cell.h>
#include <sheet.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <gnm-format.h>
#include <mstyle.h>
#include <style-border.h>
#include <style-color.h>
#include <sheet-style.h>
#include <number-match.h>
#include <parse-util.h>
#include <gutils.h>
#include <gnm-plugin.h>

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-textline.h>
#include <gsf/gsf-utils.h>

GNM_PLUGIN_MODULE_HEADER;

gboolean sylk_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl);
void     sylk_file_open (GOFileOpener const *fo, GOIOContext *io_context,
                         WorkbookView *wb_view, GsfInput *input);

typedef struct {
	GOIOContext	 *io_context;

	GsfInputTextline *input;
	GIConv            converter;
	unsigned	  line_no;
	gboolean	  finished;

	GnmConventions const *convs;
	GnmParsePos	  pp;

	GPtrArray	*formats;
	GPtrArray	*fonts;
} SylkReader;

static void
sylk_read_warning (SylkReader *state, char const *fmt, ...)
{
	char *msg;
	va_list args;

	va_start (args, fmt);
	msg = g_strdup_vprintf (fmt, args);
	va_end (args);

	g_warning ("%d:%s", state->line_no, msg);
	go_io_warning (state->io_context, "%d:%s", state->line_no, msg);
	g_free (msg);
}

static char *
sylk_next_token (char *src)
{
	static gint8 const accents[] = { /* 0x40 - 0x4F */
		  -1, /* @ = ??? */
		   0, /* A = x300 Grave */
		   1, /* B = x301 Acute */
		   2, /* C = x302 Circumflex */
		   3, /* D = x303 tilde */
		  -1, /* E = ??? */
		  -1, /* F = ??? */
		  -1, /* G = ??? */
		   8, /* H = x308 diaeresis */
		  -1, /* I = ??? */
		 0xA, /* J = x30A ring */
		0x27, /* K = x327 cedilla */
		  -1, /* L = ??? */
		  -1, /* M = ??? */
		  -1, /* N = ??? */
		  -1, /* O = ??? */
	};

	static gunichar const unaccented_1[] = { /* 0x21 - 0x3F */
		/* ! */	0x00A1,	/* " */	0x00A2,	/* # */	0x00A3,	/* $ */	'$',
		/* % */	0x00A5,	/* & */	'#',	/* ' */	0x00A7,	/* ( */	0x00A4, /* yay for consistency :-p */
		/* ) */	'\'',	/* * */	'\"',	/* + */	0x00AB,	/* , */	',',
		/* - */	'-',	/* . */	'.',	/* / */	'/',	/* 0 */	0x00B0,
		/* 1 */	0x00B1,	/* 2 */	0x00B2,	/* 3 */	0x00B3,	/* 4 */	'4',
		/* 5 */	0x00B5,	/* 6 */	0x00B6,	/* 7 */	0x00B7,	/* 8 */	'8',
		/* 9 */	'\'',	/* : */	'\"',	/* ; */	0x00BB,	/* < */	0x00BC,
		/* = */	0x00BD,	/* > */	0x00BE,	/* ? */	0x00BF
	};
	static gunichar const unaccented_2[] = { /* 0x50 - 0x7E */
		/* P */	0x00AF,	/* Q */	0x00AC,	/* R */	0x00AE,	/* S */	0x00A9,
		/* T */	'T',	/* U */	'U',	/* V */	'V',	/* W */	'W',
		/* X */	'X',	/* Y */	'Y',	/* Z */	'Z',	/* [ */	'[',
		/* \ */	'\\',	/* ] */	']',	/* ^ */	'^',	/* _ */	'_',
		/* ` */	'`',	/* a */	0x00C6,	/* b */	0x00D0,	/* c */	0x00AA,
		/* d */	'd',	/* e */	'e',	/* f */	'f',	/* g */	'g',
		/* h */	'h',	/* i */	0x00D8,	/* j */	0x0152,	/* k */	0x00B0,
		/* l */	0x00DE,	/* m */	'm',	/* n */	'n',	/* o */	'o',
		/* p */	'p',	/* q */	0x00E6,	/* r */	'r',	/* s */	0x00F0,
		/* t */	't',	/* u */	'u',	/* v */	'v',	/* w */	'w',
		/* x */	'x',	/* y */	0x00F8,	/* z */	0x0153,	/* { */	0x00DF,
		/* | */	0x00DE,	/* } */	'}',	/* ~ */	'~'
	};

	char *dst = src;

	while (*src)
		if (src[0] == ';') {
			if (src[1] == ';') {
				*dst++ = ';';
				src += 2;
			} else {
				*dst = '\0';
				return src+1;
			}
		} else if (src[0] == 0x1B) { /* escape */
			gunichar u;
			if (src[1] != 'N') { /* must be <ESC>N? */
				src++;
				continue;
			} else if (src[2] <= 0x20 || 0x7f <= src[2]) { /* out of range */
				src += 2;
				continue;
			} else if (src[2] <= 0x3f) { /* unaccented chars 0x21 - 0x3f */
				u = unaccented_1[src[2] - 0x21];
			} else if (src[2] >= 0x50) { /* unaccented chars 0x50 - 0x7E */
				u = unaccented_2[src[2] - 0x50];
			} else { /* accents 0x40 - 0x4F */
				char *merged = NULL;
				int accent = accents[src[2] - 0x40];
				if (accent >= 0) {
					char buf[6];
					int len = g_unichar_to_utf8 (0x300 + accent, buf+1);
					buf[0] = src[3];
					if (NULL != (merged = g_utf8_normalize (buf, len+1, G_NORMALIZE_DEFAULT_COMPOSE ))) {
						/* all of the potential chars are < 4 bytes */
						strcpy (dst, merged);
						dst += strlen (merged);
						g_free (merged);
					}
				}
					/* fallback to the unaccented char */
				if (NULL == merged)
					*dst++ = src[3];
				src += 4;
				continue;
			}
			/* we have space for at least 3 bytes */
			dst += g_unichar_to_utf8 (u, dst);
			src += 3; /* <ESC>N<designator> */
		} else
			*dst++ = *src++;

	*dst = '\0';
	return src;
}

static gboolean
sylk_parse_int (char const *str, int *res)
{
	char *end;
	long l;

	errno = 0; /* strtol sets errno, but does not clear it.  */
	l = strtol (str, &end, 10);
	if (str != end && errno != ERANGE) {
		*res = l;
		return TRUE;
	}
	return FALSE;
}

static GnmValue *
sylk_parse_value (SylkReader *state, const char *str)
{
	GnmValue *val;
	if ('\"' == *str) { /* quoted string */
		size_t len;

		str++;
		len = strlen (str);
		if (len > 0 && str[len - 1] == '\"')
			len--;
		return value_new_string_nocopy (g_strndup (str, len));
	}

	val = format_match_simple (str);
	if (val)
		return val;

	return value_new_string (str);
}

static GnmExprTop const *
sylk_parse_expr (SylkReader *state, char const *str)
{
	return gnm_expr_parse_str (str, &state->pp, GNM_EXPR_PARSE_DEFAULT,
		state->convs, NULL);
}

static void
sylk_parse_comment (SylkReader *state, char const *str)
{
}

static gboolean
sylk_rtd_c_parse (SylkReader *state, char *str)
{
	GnmValue *val = NULL;
	GnmExprTop const *texpr = NULL;
	gboolean is_array = FALSE;
	int r = -1, c = -1, tmp;
	char *next;

	for (; *str != '\0' ; str = next) {
		next = sylk_next_token (str);
		switch (*str) {
		case 'X': if (sylk_parse_int (str+1, &tmp)) state->pp.eval.col = tmp - 1; break;
		case 'Y': if (sylk_parse_int (str+1, &tmp)) state->pp.eval.row = tmp - 1; break;

		case 'K': /* ;K value: Value of the cell. */
			if (val != NULL) {
				sylk_read_warning (state, _("Multiple values in the same cell"));
				value_release (val);
				val = NULL;
			}
			val = sylk_parse_value (state, str+1);
			break;

		case 'E':
			if (texpr != NULL) {
				sylk_read_warning (state, _("Multiple expressions in the same cell"));
				gnm_expr_top_unref (texpr);
			}
			texpr = sylk_parse_expr (state, str+1);
			break;
		case 'M' : /* ;M exp: Expression stored with UL corner of matrix (;R ;C defines
		  the lower right corner).  If the ;M field is supported, the
		  ;K record is ignored.  Note that no ;E field is written. */
			if (texpr != NULL) {
				sylk_read_warning (state, _("Multiple expressions in the same cell"));
				gnm_expr_top_unref (texpr);
			}
			texpr = sylk_parse_expr (state, str+1);
			is_array = TRUE;
			break;

		case 'I' : /* ;I: Inside a matrix or table (at row ;R, col ;C) C record for UL
		      corner must precede this record.  Note that any ;K field is
		      ignored if the ;I field is supported.  No ;E field is written
		      out. */
			is_array = TRUE;
			break;
		case 'C' : sylk_parse_int (str+1, &c); break;
		case 'R' : sylk_parse_int (str+1, &r); break;

		case 'A' : /* ;Aauthor:^[ :text 1) till end of line 2) excel extension */
			sylk_parse_comment (state, str+1);
			break;

		case 'G' : /* ;G: Defines shared value (may not have an ;E for this record). */
		case 'D' : /* ;D: Defines shared expression. */
		case 'S' : /* ;S: Shared expression/value given at row ;R, col ;C.  C record
		  for ;R, ;C must precede this one.  Note that no ;E or ;K
		  fields are written here (not allowed on Excel macro sheets). */

		case 'N' : /* ;N: Cell NOT protected/locked (if ;N present in ;ID record). */
		case 'P' : /* ;P: Cell protected/locked (if ;N not present in ;ID record).
		  Note if this occurs for any cell, we protect the entire
		  sheet. */

		case 'H' : /* ;H: Cell hidden. */

		case 'T' : /* ;Tref,ref: UL corner of table (;R ;C defines the lower left
		  corner).  Note that the defined rectangle is the INSIDE of
		  the table only.  Formulas and input cells are above and to
		  the left of this rectangle.  The row and column input cells
		  are given in by the two refs (possibly only one).  Note that
		  Excel's input refs are single cells only. */

		default:
			break;
		}
	}

	if (val != NULL || texpr != NULL) {
		GnmCell *cell = sheet_cell_fetch (state->pp.sheet,
			state->pp.eval.col, state->pp.eval.row);

		if (is_array) {
			if (texpr) {
				GnmRange rg;
				rg.start = state->pp.eval;
				rg.end.col = c - 1;
				rg.end.row = r - 1;

				gnm_cell_set_array (state->pp.sheet,
						    &rg,
						    texpr);
				gnm_expr_top_unref (texpr);
			}
			if (NULL != val)
				gnm_cell_assign_value (cell, val);
		} else if (NULL != texpr) {
			if (NULL != val)
				gnm_cell_set_expr_and_value (cell, texpr, val, TRUE);
			else
				gnm_cell_set_expr (cell, texpr);
			gnm_expr_top_unref (texpr);
		} else if (NULL != val)
			gnm_cell_set_value (cell, val);
	}

	return TRUE;
}


static gboolean
sylk_rtd_p_parse (SylkReader *state, char *str)
{
	char *next;
	int   font_size;
	GnmStyle *font = NULL;

	for (; *str != '\0' ; str = next) {
		next = sylk_next_token (str);
		switch (*str) {
		case 'P' : /* format */
			g_ptr_array_add (state->formats,
				go_format_new_from_XL (str+1));
			break;

		case 'F' : /* some sort of global font name  */
			break;

		case 'E' : /* font name */
			if (str[1] != '\0') {
				if (NULL == font) font = gnm_style_new ();
				gnm_style_set_font_name	(font, str+1);
			}
			break;

		case 'L' : /* font color ? */
			break;

		case 'M' : /* font size * 20 */
			if (sylk_parse_int (str+1, &font_size) &&
			    font_size > 0) {
				if (NULL == font) font = gnm_style_new ();
				gnm_style_set_font_size	(font, font_size / 20.0);
			}
			break;

		case 'S' :
			for (str++ ; *str && *str != ';' ; str++)
				switch (*str) {
				case 'I':
					if (NULL == font) font = gnm_style_new ();
					gnm_style_set_font_italic (font, TRUE);
					break;

				case 'B':
					if (NULL == font) font = gnm_style_new ();
					gnm_style_set_font_bold (font, TRUE);
					break;
				}
			break;
		default :
			sylk_read_warning (state, "unknown P option '%c'", *str);
		}
	}

	if (NULL != font)
		g_ptr_array_add (state->fonts, font);
	return TRUE;
}

static gboolean
sylk_rtd_o_parse (SylkReader *state, char *str)
{
	char *next;
	GnmConventions const *convs = gnm_conventions_xls_r1c1; /* default */

	for (; *str != '\0' ; str = next) {
		next = sylk_next_token (str);
		switch (*str) {
		case 'C' : /* ;C: Completion test at current cell. */
			break;

		case 'D' :
			break;

		case 'E' : /* ;E: Macro (executable) sheet.  Note that this should appear
		    before the first occurance of ;G or ;F field in an NN record
		    (otherwise not enabled in Excel).  Also before first C record
		    which uses a macro-only function. */
			break;

		case 'A' :   /* ;A<count> <tolerance>	iteration enabled */
		case 'G' : { /* ;G<count> <tolerance>	iteration disabled */
			int   count;
			double tolerance;
			if (2 == sscanf (str+1, "%d %lf", &count, &tolerance)) {
				workbook_iteration_tolerance (state->pp.wb, tolerance);
				workbook_iteration_max_number (state->pp.wb, count);
				workbook_iteration_enabled (state->pp.wb, *str == 'A');
			}
			break;
		}

		case 'K' :
			break;

		case 'L' : /* ;L: Use A1 mode references */
			convs = gnm_conventions_default;
			break;

		case 'M' : /* ;M: Manual recalc. */
			workbook_set_recalcmode (state->pp.wb, FALSE);
			break;

		case 'P' : /* ;P: Sheet is protected */
			state->pp.sheet->is_protected = TRUE;
			break;

		case 'R' : /* ;R: Precision as displayed (TODO) */
			break;

		case 'V' : /* Seems to be nul-date 0 == 1900, 4 == 1904 */
			if (str[1] == '4')
				workbook_set_1904 (state->pp.wb, TRUE);
			break;

		case 'Z' : /* Seems to be 'hide zeros' */
			state->pp.sheet->hide_zero = TRUE;
			break;

		default :
			sylk_read_warning (state, "unknown option '%c'", *str);
		}
	}
	g_object_set (state->pp.sheet, "conventions", convs, NULL);
	return TRUE;
}

static GnmStyle *
sylk_set_border (GnmStyle *style, GnmStyleElement border)
{
	GnmStyleBorderLocation const loc =
		GNM_STYLE_BORDER_TOP + (int)(border - MSTYLE_BORDER_TOP);
	if (style == NULL) style = gnm_style_new ();
	gnm_style_set_border (style, border,
		gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
			style_color_black (),
			gnm_style_border_get_orientation (loc)));
	return style;
}

static gboolean
sylk_rtd_f_parse (SylkReader *state, char *str)
{
	GnmStyle *style = NULL;
	char *next;
	int   tmp, size = -1;
	gboolean is_default_style = FALSE;
	int full_col = -1, full_row = -1;

	for (; *str != '\0' ; str = next) {
		next = sylk_next_token (str);
		switch (*str) {
		case 'D': /* Default sheet style
			     ;D<fmt-id><digits><alignment><num_cols??> default Format. */
			is_default_style = TRUE;

		case 'F': { /* Cell Format
			     ;F<fmt-id><digits><alignment>: Single cell format. */

			 /* Format:
				D - default
				C - currency (extended)
				E - exponent
				F - fixed
				G - general,
				$ - dollar
				* - graph
				% - percent

			   Alignment: DCGLR-X
				*/
			char ch1, alignment;
			if (3 == sscanf (str+1, "%c%d%c", &ch1, &tmp, &alignment)) {
				int a = -1;
				switch (alignment) {
				case 'S' : /* standard ? how does this differ from default */
				case 'D' : a = GNM_HALIGN_GENERAL; break;
				case 'L' : a = GNM_HALIGN_LEFT; break;
				case 'R' : a = GNM_HALIGN_RIGHT; break;
				case 'C' : a = GNM_HALIGN_CENTER; break;
				case 'X' : a = GNM_HALIGN_FILL; break;
				default :
					   break;
				}
				if (a >= 0) {
					if (style == NULL) style = gnm_style_new ();
					gnm_style_set_align_h (style, a);
				}
			}
			break;
		}

		/**************************************************/
		/* Globals */
		case 'E':
			state->pp.sheet->display_formulas = TRUE;
			break;
		case 'G':
			state->pp.sheet->hide_grid = TRUE;
			break;
		case 'H':
			state->pp.sheet->hide_col_header = TRUE;
			state->pp.sheet->hide_row_header = TRUE;
			break;
		case 'K': /* show commas ?? */
			break;
		case 'Z': /* hide zeros */
			state->pp.sheet->hide_zero = TRUE;
			break;
		/**************************************************/

		case 'M' : /* row or col size * 20 or num cols for global */
			sylk_parse_int (str+1, &size);
			break;

		case 'N' : { /* global font */
			int size;
			if (2 == sscanf (str+1, "%d %d", &tmp, &size) &&
			    1 <= tmp && tmp <= (int)state->fonts->len) {
				GnmStyle const *font =
					g_ptr_array_index (state->fonts, tmp-1);
				if (style == NULL) style = gnm_style_new ();
				is_default_style = TRUE;
				gnm_style_merge_element (style, font,
					MSTYLE_FONT_NAME);
				gnm_style_merge_element (style, font,
					MSTYLE_FONT_SIZE);
				/* It looks like the size from the id dominates
				 * this size */
			}
			break;
		}

		case 'P':
			if (sylk_parse_int (str+1, &tmp) &&
			    0 <= tmp && tmp < (int)state->formats->len) {
				if (style == NULL) style = gnm_style_new ();
				gnm_style_set_format (style,
					g_ptr_array_index (state->formats, tmp));
			}
			break;

		case 'S':
			for (str++ ; *str && *str != ';' ; str++) {
				switch (*str) {
				case 'I':
					if (style == NULL) style = gnm_style_new ();
					gnm_style_set_font_italic (style, TRUE);
					break;

				case 'D':
					if (style == NULL) style = gnm_style_new ();
					gnm_style_set_font_bold (style, TRUE);
					break;

				case 'M':
					if (sylk_parse_int (str+1, &tmp) &&
					    1 <= tmp && tmp <= (int)state->fonts->len) {
						GnmStyle const *font =
							g_ptr_array_index (state->fonts, tmp-1);
						if (style == NULL) style = gnm_style_new ();
						gnm_style_merge_element (style, font,
									 MSTYLE_FONT_NAME);
						gnm_style_merge_element (style, font,
									 MSTYLE_FONT_SIZE);
					}
					str = (char *)" ";
					break;

				case 'S': /* seems to stipple things */
					if (style == NULL) style = gnm_style_new ();
					gnm_style_set_pattern (style, 5);
					break;

				case 'T': style = sylk_set_border (style, MSTYLE_BORDER_TOP); break;
				case 'B': style = sylk_set_border (style, MSTYLE_BORDER_BOTTOM); break;
				case 'L': style = sylk_set_border (style, MSTYLE_BORDER_LEFT); break;
				case 'R': style = sylk_set_border (style, MSTYLE_BORDER_RIGHT); break;

				default:
					sylk_read_warning (state, "unhandled style S%c.", *str);
				}
			}
			break;

		case 'W': {
			int first, last, width;
			if (3 == sscanf (str+1, "%d %d %d", &first, &last, &width)) {
				/* width seems to be in characters */
				if (first <= last &&
				    first < gnm_sheet_get_max_cols (state->pp.sheet) &&
				    last < gnm_sheet_get_max_cols (state->pp.sheet))
					while (first <= last)
						sheet_col_set_size_pts (state->pp.sheet,
							first++ - 1, width*7.45, TRUE);
			}
			break;
		}

		case 'C': if (sylk_parse_int (str+1, &tmp)) full_col = tmp - 1; break;
		case 'R': if (sylk_parse_int (str+1, &tmp)) full_row = tmp - 1; break;
		case 'X': if (sylk_parse_int (str+1, &tmp)) state->pp.eval.col = tmp - 1; break;
		case 'Y': if (sylk_parse_int (str+1, &tmp)) state->pp.eval.row = tmp - 1; break;
		default:
			sylk_read_warning (state, "unhandled F option %c.", *str);
		}
	}

	if (full_col >= 0) {
		if (NULL != style)
			sheet_style_apply_col (state->pp.sheet, full_col, style);
		if (size > 0)
			sheet_col_set_size_pts (state->pp.sheet,
						full_col, size / 20.0, FALSE);
	} else if (full_row >= 0) {
		if (NULL != style)
			sheet_style_apply_row (state->pp.sheet, full_row, style);
		if (size > 0)
			sheet_row_set_size_pts (state->pp.sheet,
						full_row, size / 20.0, FALSE);
	} else if (NULL != style) {
		if (is_default_style) {
			GnmRange r;
			range_init_full_sheet (&r, state->pp.sheet);
			sheet_style_apply_range (state->pp.sheet, &r, style);
		} else
			sheet_style_apply_pos (state->pp.sheet,
				state->pp.eval.col, state->pp.eval.row, style);
	}

	return TRUE;
}

static gboolean
sylk_rtd_w_parse (SylkReader *state, char *str)
{
	char *next;

	for (; *str != '\0' ; str = next) {
		next = sylk_next_token (str);
		switch (*str) {
		case 'R' :
		/* ;R n1 n2 ... n14: (Mac Plan only)
			n1..n8: Title freeze info.
			n9..n12: Scroll bar info.
			n13..n14: Split bar info. */
			break;


		case 'A' :
		/* ;A<row> <col>: UL window corner (character based - only). */
			break;

		case 'B' :
		/* ;B: Window is bordered (character based - only). */
			break;

		case 'N' :
		/* ;N n: Window index (character based - only). */
			break;

		case 'S' :
		/* ;S ...: Window split (character based - only). */
			break;

		case 'C' :
		/* ;C n1 n2 n3: Foreground, background and border colors (IBM). */
			break;

		default:
			sylk_read_warning (state, "unhandled W option %c.", *str);
		}
	}

	return TRUE;
}
static gboolean
sylk_rtd_nn_parse (SylkReader *state, char *str)
{
	char *next;

	for (; *str != '\0' ; str = next) {
		next = sylk_next_token (str);
		switch (*str) {
		case 'E' : /* ;E exp: Expression desribing the value of the name */
			break;

		case 'F' : /* ;F: Usable as a function. */ break;

		case 'G' : /* ;G ch1 ch2: Runable name (macro) with command key
			      alias (Mac uses only first character). */
			break;

		case 'K' :
			/* ;K ch1 ch2: Ordinary name with unused command aliases (Plan 2.0). */
			break;

		case 'N' :
			/* ;N name: The name string. */
			break;

		default:
			sylk_read_warning (state, "unhandled NN option %c.", *str);
		}
	}

	return TRUE;
}
static gboolean
sylk_rtd_e_parse (SylkReader *state)
{
	state->finished = TRUE;
	return TRUE;
}

static gboolean
sylk_parse_line (SylkReader *state, char *buf, gsize len)
{
	if (buf != NULL) {
		if (len >= 2 && buf[1] == ';') {
			switch (*buf) {
			case 'B' : return TRUE; /* ignored */
			case 'C' : return sylk_rtd_c_parse (state, buf + 2);
			case 'E' : return sylk_rtd_e_parse (state);
			case 'F' : return sylk_rtd_f_parse (state, buf + 2);
			case 'P' : return sylk_rtd_p_parse (state, buf + 2);
			case 'O' : return sylk_rtd_o_parse (state, buf + 2);
			case 'W' : return sylk_rtd_w_parse (state, buf + 2);
			}
		} else if (0 == strncmp ("ID", buf, 2))
			return TRUE; /* who cares */
		else if (0 == strncmp ("NN;", buf, 2))
			return sylk_rtd_nn_parse (state, buf + 3);
		else if (buf[0] == 'E')
			return sylk_rtd_e_parse (state);
	}
	sylk_read_warning (state, "Unknown directive '%s'", buf);

	return TRUE;
}

static void
sylk_parse_sheet (SylkReader *state)
{
	char *buf, *utf8buf;
	gsize utf8_len;

	while (!state->finished &&
	       (buf = gsf_input_textline_ascii_gets (state->input)) != NULL) {
		g_strchomp (buf);

		utf8buf = g_convert_with_iconv (buf, -1, state->converter, NULL,
						&utf8_len, NULL);

		state->line_no++;
		sylk_parse_line (state, utf8buf, utf8_len);
		g_free (utf8buf);
	}
	if (!state->finished)
		sylk_read_warning (state, _("Missing closing 'E'"));
}

void
sylk_file_open (GOFileOpener const *fo,
		GOIOContext	*io_context,
                WorkbookView	*wb_view,
		GsfInput	*input)
{
	SylkReader state;
	char *name = NULL;
	int i;
	GnmLocale *locale;

	memset (&state, 0, sizeof (state));
	state.io_context = io_context;
	state.input	 = (GsfInputTextline *) gsf_input_textline_new (input);
	state.converter  = g_iconv_open ("UTF-8", "ISO-8859-1");
	state.finished	 = FALSE;
	state.line_no	 = 0;

	state.pp.wb = wb_view_get_workbook (wb_view);

	name = workbook_sheet_get_free_name (state.pp.wb, _("Sheet"), TRUE, FALSE);
	state.pp.sheet = sheet_new (state.pp.wb, name, 256, 65536);
	workbook_sheet_attach (state.pp.wb, state.pp.sheet);
	g_free (name);

	state.pp.eval.col = state.pp.eval.row = 1;
	state.convs = gnm_conventions_xls_r1c1;

	state.formats	= g_ptr_array_new ();
	state.fonts	= g_ptr_array_new ();

	locale = gnm_push_C_locale ();
	sylk_parse_sheet (&state);
	gnm_pop_C_locale (locale);
	workbook_set_saveinfo (state.pp.wb, GO_FILE_FL_AUTO,
		go_file_saver_for_id ("Gnumeric_sylk:sylk"));

	for (i = state.fonts->len ; i-- > 0 ; )
		gnm_style_unref (g_ptr_array_index (state.fonts, i));
	g_ptr_array_free (state.fonts, TRUE);

	for (i = state.formats->len ; i-- > 0 ; )
		go_format_unref (g_ptr_array_index (state.formats, i));
	g_ptr_array_free (state.formats, TRUE);

	gsf_iconv_close (state.converter);
	g_object_unref (state.input);
}

gboolean
sylk_file_probe (GOFileOpener const *fo, GsfInput *input, GOFileProbeLevel pl)
{
	char const *header = NULL;
	if (!gsf_input_seek (input, 0, G_SEEK_SET))
		header = gsf_input_read (input, 3, NULL);
	return (header != NULL && strncmp (header, "ID;", 3) == 0);
}
