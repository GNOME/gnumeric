/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * sylk.c - file import of SYLK files
 *
 * Jody Goldberg   <jody@gnome.org>
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
 *
 * Miguel de Icaza <miguel@gnu.org>
 * Based on work by
 * 	Jeff Garzik <jgarzik@mandrakesoft.com>
 * With some code from:
 * 	csv-io.c: read sheets using a CSV encoding.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <goffice/app/file.h>
#include <goffice/app/io-context.h>
#include "workbook-view.h"
#include "workbook.h"
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include "expr.h"
#include "gnm-format.h"
#include "mstyle.h"
#include "style-border.h"
#include "style-color.h"
#include "sheet-style.h"
#include "number-match.h"
#include "gutils.h"
#include <goffice/app/error-info.h>
#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-textline.h>
#include <gsf/gsf-utils.h>

GNM_PLUGIN_MODULE_HEADER;

gboolean sylk_file_probe (GOFileOpener const *fo, GsfInput *input, FileProbeLevel pl);
void     sylk_file_open (GOFileOpener const *fo, IOContext *io_context,
                         WorkbookView *wb_view, GsfInput *input);

typedef struct {
	IOContext	 *io_context;
	GsfInputTextline *input;
	Sheet	 	 *sheet;
	gboolean	  finished;

	int col, row;	/* SYLK current X/Y pointers (begin at 1) */

	GIConv          converter;
	GPtrArray	*formats;
} SylkReadState;

static char *
sylk_next_token (char *str)
{
	while (*str)
		if (str[0] != ';')
			str++;
		else if (str[1] == ';')
			str += 2;
		else {
			*str = 0;
			return str+1;
		}

	return str;
}

static char *
sylk_parse_string (char const *str)
{
	GString *accum = g_string_new (NULL);
	gboolean ignore_trailing_quote = (*str == '\"');

	if (ignore_trailing_quote)
		str++;

	while (*str) {
		/* This is UTF-8 safe as long as ';' is ASCII.  */
		if (ignore_trailing_quote && str[0] == '"' && str[1] == '\0')
			break;
		else if (str[0] != ';')
			g_string_append_c (accum, *str++);
		else if (str[1] == ';') {
			g_string_append_c (accum, ';');
			str += 2;
		} else
			break;
	}

	return g_string_free (accum, FALSE);
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
sylk_parse_value (SylkReadState *state, char const *str)
{
	GnmValue *val = format_match_simple (str);
	if (val != NULL)
		return val;
	return value_new_string_nocopy (sylk_parse_string (str));
}

static GnmExprTop *
sylk_parse_expr (SylkReadState *state, char const *str)
{
	g_print ("%s\n", str);
	return NULL;
}

static void
sylk_parse_comment (SylkReadState *state, char const *str)
{
}

static gboolean
sylk_rtd_c_parse (SylkReadState *state, char *str)
{
	GnmValue *val = NULL;
	GnmExprTop const *texpr = NULL;
	gboolean is_array = FALSE;
	int r = -1, c = -1;
	char *next;

	for (; *str != '\0' ; str = next) {
		next = sylk_next_token (str);
		switch (*str) {
		case 'X': sylk_parse_int (str+1, &state->col); break;
		case 'Y': sylk_parse_int (str+1, &state->row); break;

		case 'K': /* ;K value: Value of the cell. */
			if (val != NULL) {
				g_warning ("Multiple values");
				value_release (val);
				val = NULL;
			}
			val = sylk_parse_value (state, str+1);
			break;

		case 'E':
			if (texpr != NULL) {
				g_warning ("Multiple expressions");
				gnm_expr_top_unref (texpr);
			}
			texpr = sylk_parse_expr (state, str+1);
			break;
		case 'M' : /* ;M exp: Expression stored with UL corner of matrix (;R ;C defines
		  the lower right corner).  If the ;M field is supported, the
		  ;K record is ignored.  Note that no ;E field is written. */
			if (texpr != NULL) {
				g_warning ("Multiple expressions");
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
		GnmCell *cell = sheet_cell_fetch (state->sheet,
			state->col - 1, state->row - 1);

		if (val != NULL) {
			GnmStyle const *style = sheet_style_get (state->sheet,
				state->col - 1, state->row - 1);
			value_set_fmt (val, gnm_style_get_format (style));
		}

		if (texpr != NULL) {
			if (val != NULL)
				gnm_cell_set_expr_and_value
					(cell, texpr, val, TRUE);
			else
				gnm_cell_set_expr (cell, texpr);
			gnm_expr_top_unref (texpr);
		} else if (is_array)
			gnm_cell_assign_value (cell, val);
		else
			gnm_cell_set_value (cell, val);
	}

	return TRUE;
}


static gboolean
sylk_rtd_p_parse (SylkReadState *state, char *str)
{
	char *next, *tmp;

	for (; *str != '\0' ; str = next) {
		next = sylk_next_token (str);
		switch (*str) {
		case 'P' : /* format */
			tmp = sylk_parse_string (str+1);
			g_ptr_array_add (state->formats,
				go_format_new_from_XL (tmp));
			g_free (tmp);
			break;

		case 'E' : /* font name */
		case 'F' : /* font name */
			tmp = sylk_parse_string (str+1);
			g_print ("FONT = %s\n", tmp);
			g_free (tmp);
			break;
		case 'M' : /* font size */
			break;

		default :
			break;
		}
	}
	return TRUE;
}

static gboolean
sylk_rtd_o_parse (SylkReadState *state, char *str)
{
	char *next;

	for (; *str != '\0' ; str = next) {
		next = sylk_next_token (str);
		switch (*str) {
		case 'A' : /* ;A cIter numDelta: Iteration on.  The parameters are not used by
		    plan but are for Excel. */

		case 'C' : /* ;C: Completion test at current cell. */
		case 'E' : /* ;E: Macro (executable) sheet.  Note that this should appear
		    before the first occurance of ;G or ;F field in an NN record
		    (otherwise not enabled in Excel).  Also before first C record
		    which uses a macro-only function. */
		case 'L' : /* ;L: Use A1 mode references (R1C1 always used in SYLK file expressions). */
		case 'M' : /* ;M: Manual recalc. */
		case 'P' : /* ;P: Sheet is protected (but no password). */
		case 'R' : /* ;R: Precision as formated (!fPrec). */
			break;

		default :
			break;
		}
	}
	return TRUE;
}

static gboolean
sylk_rtd_f_parse (SylkReadState *state, char *str)
{
	GnmStyle *style = NULL;
	GnmStyleElement border = MSTYLE_ELEMENT_MAX;
	char *next;
	int tmp;

	for (; *str != '\0' ; str = next) {
		next = sylk_next_token (str);
		switch (*str) {
		case 'D':
		case 'F':
			break;

		/* Globals */
		case 'E':
			state->sheet->display_formulas = TRUE;
			break;
		case 'G':
			state->sheet->hide_grid = TRUE;
			break;
		case 'H':
			state->sheet->hide_col_header = TRUE;
			state->sheet->hide_row_header = TRUE;
			break;
		case 'K': /* show commas ?? */
			break;

		case 'P':
			if (sylk_parse_int (str+1, &tmp) &&
			    0 <= tmp && tmp < (int)state->formats->len) {
				if (style == NULL)
					style = gnm_style_new_default ();
				gnm_style_set_format (style,
					g_ptr_array_index (state->formats, tmp));
			}
			break;

		case 'S':
			switch (str[1]) {
			case 'I':
				if (style == NULL)
					style = gnm_style_new_default ();
				gnm_style_set_font_italic (style, TRUE);
				break;

			case 'D':
				if (style == NULL)
					style = gnm_style_new_default ();
				gnm_style_set_font_bold (style, TRUE);
				break;

			case 'T': border = MSTYLE_BORDER_TOP; break;
			case 'L': border = MSTYLE_BORDER_LEFT; break;
			case 'B': border = MSTYLE_BORDER_BOTTOM; break;
			case 'R': border = MSTYLE_BORDER_RIGHT; break;

			default:
				g_warning ("unhandled style S%c.", str[1]);
				break;
			}
			break;

		case 'W': {
			int first, last, width;
			if (3 == sscanf (str+1, "%d %d %d", &first, &last, &width)) {
				/* width seems to be in characters */
				if (first <= last && first < SHEET_MAX_COLS && last < SHEET_MAX_COLS)
					while (first <= last)
						sheet_col_set_size_pixels (state->sheet,
							first++ - 1, width*7.45, TRUE);
			}
			break;
		}
		case 'X': sylk_parse_int (str+1, &state->col); break;
		case 'Y': sylk_parse_int (str+1, &state->row); break;
		default:
			g_warning ("unhandled F option %c.", *str);
			break;
		}
		if (border != MSTYLE_ELEMENT_MAX) {
			GnmStyleBorderLocation const loc =
				GNM_STYLE_BORDER_TOP + (int)(border - MSTYLE_BORDER_TOP);
			if (style == NULL)
				style = gnm_style_new_default ();
			gnm_style_set_border (style, border,
				gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
					style_color_black (),
					gnm_style_border_get_orientation (loc)));
		}
	}

	if (style != NULL)  {
#if 0
		g_warning ("formatting %s", cell_coord_name (state->col-1, state->row-1));
#endif
		sheet_style_set_pos (state->sheet, state->col-1, state->row-1, style);
	}

	return TRUE;
}


static gboolean
sylk_rtd_ignore (SylkReadState *state, char *str)
{
	return TRUE;
}

static gboolean
sylk_rtd_e_parse (SylkReadState *state, char *str)
{
	state->finished = TRUE;
	return TRUE;
}

static gboolean
sylk_parse_line (SylkReadState *state, char *buf)
{
	static struct {
		char const *name;
		unsigned name_len;
		gboolean (*handler) (SylkReadState *state, char *str);
	} const sylk_rtd_list[] = {
		{ "B;",  2, sylk_rtd_ignore }, /* we do not need it */
		{ "C;",  2, sylk_rtd_c_parse },
		{ "E",   1, sylk_rtd_e_parse },
		{ "F;",  2, sylk_rtd_f_parse },
		{ "P;",  2, sylk_rtd_p_parse },
		{ "O;",  2, sylk_rtd_o_parse },
		{ "ID;", 3, sylk_rtd_ignore },	 /* who cares */
	};
	unsigned i;

	for (i = 0; i < G_N_ELEMENTS (sylk_rtd_list); i++)
		if (strncmp (sylk_rtd_list [i].name, buf, sylk_rtd_list [i].name_len) == 0) {
			(sylk_rtd_list [i].handler) (state, buf + sylk_rtd_list [i].name_len);
			return TRUE;
		}

	fprintf (stderr, "unhandled directive: '%s'\n", buf);

	return TRUE;
}

static void
sylk_parse_sheet (SylkReadState *state, ErrorInfo **ret_error)
{
	char *buf;

	*ret_error = NULL;

	if ((buf = gsf_input_textline_ascii_gets (state->input)) == NULL ||
	    strncmp ("ID;", buf, 3)) {
		*ret_error = error_info_new_str (_("Not SYLK file"));
		return;
	}

	while (!state->finished &&
	       (buf = gsf_input_textline_ascii_gets (state->input)) != NULL) {
		char *utf8buf;
		g_strchomp (buf);

		utf8buf = g_convert_with_iconv (buf, -1, state->converter, NULL, NULL, NULL);

		if (utf8buf[0] && !sylk_parse_line (state, utf8buf)) {
			g_free (utf8buf);
			*ret_error = error_info_new_str (_("error parsing line\n"));
			return;
		}

		g_free (utf8buf);
	}
}

void
sylk_file_open (GOFileOpener const *fo,
		IOContext	*io_context,
                WorkbookView	*wb_view,
		GsfInput	*input)
{
	SylkReadState state;
	char const *input_name;
	char *base;
	int i;
	GnmLocale *locale;
	Workbook  *book = wb_view_get_workbook (wb_view);
	ErrorInfo *sheet_error;

	input_name = gsf_input_name (input);
	if (input_name == NULL)
		input_name = "";
	base = g_path_get_basename (input_name);

	memset (&state, 0, sizeof (state));
	state.io_context = io_context;
	state.input = (GsfInputTextline *) gsf_input_textline_new (input);
	state.sheet = sheet_new (book, base);
	state.col = state.row = 1;
	state.converter = g_iconv_open ("UTF-8", "ISO-8859-1");
	state.formats	= g_ptr_array_new ();
	state.finished = FALSE;

	workbook_sheet_attach (book, state.sheet);
	g_free (base);

	locale = gnm_push_C_locale ();

	sylk_parse_sheet (&state, &sheet_error);

	gnm_pop_C_locale (locale);

	if (sheet_error != NULL)
		gnumeric_io_error_info_set (io_context,
		                            error_info_new_str_with_details (
		                            _("Error while reading sheet."),
		                            sheet_error));
	g_object_unref (G_OBJECT (state.input));
	gsf_iconv_close (state.converter);
	for (i = state.formats->len ; i-- > 0 ; )
		go_format_unref (g_ptr_array_index (state.formats, i));

	g_ptr_array_free (state.formats, TRUE);
}

gboolean
sylk_file_probe (GOFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	char const *header = NULL;
	if (!gsf_input_seek (input, 0, G_SEEK_SET))
		header = gsf_input_read (input, 3, NULL);
	return (header != NULL && strncmp (header, "ID;", 3) == 0);
}
