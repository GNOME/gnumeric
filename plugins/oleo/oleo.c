/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
/*
 * GNU Oleo input filter for Gnumeric
 *
 * Author:
 *    Robert Brady <rwb197@ecs.soton.ac.uk>
 *
 * partially based on the Lotus-123 code,
 * partially based on actual Oleo code.
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "oleo.h"

#include <workbook.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>
#include <parse-util.h>
#include <expr.h>
#include <plugin-util.h>
#include <sheet-style.h>
#include <mstyle.h>
#include <ranges.h>
#include <number-match.h>
#include <func.h>

#include <gsf/gsf-input-textline.h>

#include <string.h>

#define OLEO_DEBUG 0


typedef struct {
	GsfInputTextline   *textline;
	Sheet              *sheet;
	GIConv             converter;
	GnmExprConventions *exprconv;
} OleoParseState;

#define OLEO_TO_GNUMERIC(a) ((a) - 1)
#define GNUMERIC_TO_OLEO(a) ((a) + 1)

static void
append_zeros (GString *s, int n)
{
	if (n > 0) {
		size_t oldlen = s->len;
		g_string_set_size (s, oldlen + n);
		memset (s->str+oldlen, '0', n);
	}
}


static void
oleo_set_style (Sheet *sheet, int col, int row, MStyle *mstyle)
{
	Range range;

	if (!sheet)
		return;

	range_init_full_sheet (&range);
	if (col >= 0)
		range.start.col = range.end.col = OLEO_TO_GNUMERIC (col);
	if (row >= 0)
		range.start.row = range.end.row = OLEO_TO_GNUMERIC (row);

	/* sheet_style_set_range absorbs our reference */
	mstyle_ref (mstyle);
	sheet_style_set_range (sheet, &range, mstyle);
}

/* adapted from Oleo */
static long
astol (char **ptr)
{
	char *end;
	long res;

	res = strtol (*ptr, &end, 10);
	*ptr = end;

	return res;
}

static void
oleo_get_ref_value (int *start, unsigned char *start_relative,
		    int *end, unsigned char *end_relative,
		    char const **spec)
{
	char *s = (char *)*spec;

	if (*s == '[') {		/* Relative row or col */
		*start_relative = TRUE;
		s++;
		*start = astol (&s);
		s++;			/* Skip ']' */
	} else if (g_ascii_isdigit (*s) || *s == '-') {
		*start_relative = FALSE;
		*start = OLEO_TO_GNUMERIC (astol (&s));
	} else {
		*start_relative = TRUE;
		*start = 0;
	}

	if (*s == ':') {
		s++;
		if (*s == '[') {
			*end_relative = TRUE;
			s++;
			*end = astol (&s);
			s++;			/* Skip ']' */
		} else {
			*end_relative = FALSE;
			*end = OLEO_TO_GNUMERIC (astol (&s));
		}
	} else {
		*end = *start;
		*end_relative = *start_relative;
	}
	*spec = s;
}


static char *
oleo_get_gnumeric_expr (char const *o_expr,
			ParsePos const *cur_pos)
{
	char const *from = o_expr;
	GString *gres = g_string_sized_new (1024);

	while (*from) {
		if (*from == 'r') {
			CellRef start, end;

			from++;
			oleo_get_ref_value (&start.row, &start.row_relative,
					    &end.row, &end.row_relative, &from);

			if (*from == 'c') {
				from++;
				oleo_get_ref_value (&start.col, &start.col_relative,
						    &end.col, &end.col_relative, &from);
			} else {
				start.col = 0;
				start.col_relative = TRUE;
				end.col = start.col;
				end.col_relative = start.col_relative;
			}

			cellref_as_string (gres, gnm_expr_conventions_default,
					   &start, cur_pos, TRUE);

			if (!cellref_equal (&start, &end)) {
				g_string_append_c (gres, ':');
				cellref_as_string (gres, gnm_expr_conventions_default,
						   &end, cur_pos, TRUE);
			}
		} else {
			g_string_append_c (gres, *from);
			from++;
		}
	}

#if OLEO_DEBUG > 0
	g_warning ("\"%s\"->\"%s\".", o_expr, gres->str);
#endif /* OLEO_DEBUG */

	return g_string_free (gres, FALSE);
}


static GnmExpr const *
oleo_parse_formula (OleoParseState *state, char const *text, int col, int row)
{
	ParsePos pos;
	ParseError error;
	GnmExpr const *expr;
	char *gnumeric_text;

	Cell const *cell = sheet_cell_fetch (state->sheet,
		OLEO_TO_GNUMERIC (col), OLEO_TO_GNUMERIC (row));

	parse_pos_init_cell (&pos, cell);

	gnumeric_text = oleo_get_gnumeric_expr (text, &pos);
	expr = gnm_expr_parse_str (gnumeric_text,
				   &pos, GNM_EXPR_PARSE_DEFAULT,
				   state->exprconv, parse_error_init (&error));

	if (error.err != NULL) {
		g_warning ("%s \"%s\" at %s!%s.",  error.err->message, gnumeric_text,
			   state->sheet->name_unquoted,
			   cell_coord_name (OLEO_TO_GNUMERIC (col), OLEO_TO_GNUMERIC (row)));
	}
	g_free (gnumeric_text);
	parse_error_free (&error);

	return expr;
}

static void
oleo_deal_with_cell (OleoParseState *state, guint8 *str, int *ccol, int *crow, MStyle *style)
{
	Cell *cell;
	GnmExpr const *expr = NULL;
	char *ptr = str + 1, *cval = NULL, *formula = NULL;

	while (*ptr) {
		int quotes = 0;
		if (*ptr != ';') {
#if OLEO_DEBUG > 0
			g_warning ("ptr: %s.", ptr);
#endif
			break;
		}
		*ptr++ = '\0';
		switch (*ptr++) {
		case 'c' : *ccol = astol (&ptr); break;
		case 'r' : *crow = astol (&ptr); break;
		case 'K' :
			cval = ptr;
			quotes = 0;
			while (*ptr && (*ptr != ';' || quotes > 0))
				if (*ptr++ == '"')
					quotes = !quotes;
			break;

		case 'E' :
			formula = ptr;
			while (*ptr && *ptr != ';')
				ptr++;
			break;

		default:
#if OLEO_DEBUG > 0
			g_warning ("oleo: Don't know how to deal with C; '%c'.",
				   *ptr);
#endif
			ptr = (char *)""; /* I wish C had multilevel break */
			break;
		}

		if (!*ptr)
			break;
	}

	cell = sheet_cell_fetch (state->sheet,
		OLEO_TO_GNUMERIC (*ccol), OLEO_TO_GNUMERIC (*crow));

	if (formula != NULL)
		expr = oleo_parse_formula (state, formula, *ccol, *crow);

	if (cval != NULL) {
		Value *val = format_match_simple (cval);

		if (val == NULL) {
			char *last = cval + strlen (cval) - 1;
			if (*cval == '"' && *last == '"') {
				*last = 0;
				val = value_new_string (cval + 1);
			} else
				val = value_new_string (cval);
		}

		if (expr != NULL)
			cell_set_expr_and_value (cell, expr, val, TRUE);
		else
			cell_set_value (cell, val);

		if (style != NULL)
			oleo_set_style (state->sheet, *ccol, *crow, style);

	} else {
#if OLEO_DEBUG > 0
		g_warning ("oleo: cval is NULL.");
#endif
		/* We can still store the expression, even if the value is missing */
		if (expr != NULL)
			cell_set_expr (cell, expr);
	}
	if (expr)
		gnm_expr_unref (expr);
}


/* NOTE : We don't care to much about formatting as such, but we need to
 * parse the command as it may update current row/column
 */
static void
oleo_deal_with_format (OleoParseState *state, guint8 *str, int *ccol, int *crow,
		       MStyle **style)
{
	char *ptr = str + 1;
	MStyle *mstyle = mstyle_new_default ();
	GString *fmt_string = g_string_new (NULL);

	while (*ptr) {
		char c = *ptr++;

		switch (c) {
		case 'c' : *ccol = astol (&ptr); break;
		case 'r' : *crow = astol (&ptr); break;
		case 'F': case 'G':
			c = *ptr++;

			g_string_truncate (fmt_string, 0);
			g_string_append_c (fmt_string, '0');
			if (g_ascii_isdigit (*ptr))
				append_zeros (fmt_string, astol (&ptr));
			switch (c) {
			case 'F':
				break;
			case '%':
				g_string_append_c (fmt_string, '%');
				break;
			default: /* Unknown format type... */
				g_string_truncate (fmt_string, 0);
			}
			break;
		case 'L':
			mstyle_set_align_h (mstyle, HALIGN_LEFT);
			break;
		case 'R':
			mstyle_set_align_h (mstyle, HALIGN_RIGHT);
	}
	}
	if (fmt_string->len)
		mstyle_set_format_text (mstyle, fmt_string->str);
	g_string_free (fmt_string, TRUE);

	if (*style)
		mstyle_unref (*style);
	*style = mstyle;
}

static Sheet *
oleo_new_sheet (Workbook *wb, int idx)
{
	char  *sheet_name = g_strdup_printf (_("Sheet%d"), idx);
	Sheet *sheet = sheet_new (wb, sheet_name);
	g_free (sheet_name);
	workbook_sheet_attach (wb, sheet, NULL);

	/* Ensure that things get rendered and spanned */
	sheet_flag_recompute_spans (sheet);
	return sheet;
}

static GnmExprConventions *
oleo_conventions (void)
{
	GnmExprConventions *res = gnm_expr_conventions_new ();

	res->decimal_sep_dot = TRUE;
	res->ref_parser = gnm_1_0_rangeref_parse;
	res->range_sep_colon = TRUE;
	res->sheet_sep_exclamation = TRUE;
	res->dots_in_names = TRUE;
	res->ignore_whitespace = TRUE;
	res->unknown_function_handler = gnm_func_placeholder_factory;

	return res;
}

void
oleo_read (IOContext *io_context, Workbook *wb, GsfInput *input)
{
	int sheetidx = 0;
	int ccol = 0, crow = 0;
	MStyle *style = NULL;
	guint8 *line;
	OleoParseState state;

	state.textline = gsf_input_textline_new (input);
	/* This should probably come from import dialog.  */
	state.converter = g_iconv_open ("UTF-8", "ISO-8859-1");
	state.sheet = oleo_new_sheet (wb, ++sheetidx);
	state.exprconv = oleo_conventions ();

	while (NULL != (line = gsf_input_textline_ascii_gets (state.textline))) {
		char *utf8line =
			g_convert_with_iconv (line, -1, state.converter, NULL, NULL, NULL);

		switch (utf8line[0]) {
		case '#': /* Comment */
			break;

		case 'C': oleo_deal_with_cell (&state, utf8line, &ccol, &crow, style);
			break;

		case 'F': oleo_deal_with_format (&state, utf8line, &ccol, &crow, &style);
			break;

		default: /* unknown */
#if OLEO_DEBUG > 0
			g_warning ("oleo: Don't know how to deal with %c.",
				   line[0]);
#endif
			break;
		}

		g_free (utf8line);
	}

	g_iconv_close (state.converter);
	gnm_expr_conventions_free (state.exprconv);
	g_object_unref (G_OBJECT (state.textline));
}
