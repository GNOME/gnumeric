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

#include <gsf/gsf-input-textline.h>

#include <ctype.h>
#include <string.h>

#define OLEO_DEBUG 0

#define OLEO_TO_GNUMERIC(a) ((a) - 1)
#define GNUMERIC_TO_OLEO(a) ((a) + 1)

/* Copied from Lotus-123 plugin */
static void
append_zeros (char *s, int n)
{
	if (n > 0) {
		s = s + strlen (s);
		*s++ = '.';
		while (n--)
			*s++ = '0';
		*s = 0;
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
	long i = 0;
	int sign = 1;
	unsigned char *s, c;

	s = (unsigned char *)*ptr;

	/* Skip whitespace */
	while (isspace (*s))
		if (*s++ == '\0') {
			*ptr = (char *)s;
			return (0);
		}
	/* Check for - or + */
	if (*s == '-') {
		s++;
		sign = -1;
	} else if (*s == '+')
		s++;

	/* FIXME -- this is silly and assumed 32 bit ints.  */
	/* Read in the digits */
	for (; (c = *s); s++) {
		if (!isdigit (c) || i > 214748364 ||
		    (i == 214748364 && c > (sign > 0 ? '7' : '8')))
			break;
		i = i * 10 + c - '0';
	}
	*ptr = (char *)s;
	return i * sign;
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
	} else if (isdigit ((unsigned char)*s) || *s == '-') {
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
			char *name;

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

			name = cellref_as_string (&start, cur_pos, TRUE);
			g_string_append (gres, name);
			g_free (name);

			if (!cellref_equal (&start, &end)) {
				g_string_append_c (gres, ':');
				name = cellref_as_string (&end, cur_pos, TRUE);
				g_string_append (gres, name);
				g_free (name);
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
oleo_parse_formula (char const *text, Sheet *sheet, int col, int row)
{
	ParsePos pos;
	ParseError error;
	GnmExpr const *expr;
	char *gnumeric_text;

	Cell const *cell = sheet_cell_fetch (sheet,
		OLEO_TO_GNUMERIC (col), OLEO_TO_GNUMERIC (row));

	parse_pos_init_cell (&pos, cell);

	gnumeric_text = oleo_get_gnumeric_expr (text, &pos);
	expr = gnm_expr_parse_str (gnumeric_text,
				   &pos, GNM_EXPR_PARSE_DEFAULT,
				   &rangeref_parse, parse_error_init (&error));

	if (error.err != NULL) {
		g_warning ("%s \"%s\" at %s!%s.",  error.err->message, gnumeric_text,
			   sheet->name_unquoted,
			   cell_coord_name (OLEO_TO_GNUMERIC (col), OLEO_TO_GNUMERIC (row)));
	}
	g_free (gnumeric_text);
	parse_error_free (&error);

	return expr;
}

static void
oleo_deal_with_cell (guint8 *str, Sheet *sheet, int *ccol, int *crow, MStyle *style)
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

	cell = sheet_cell_fetch (sheet,
		OLEO_TO_GNUMERIC (*ccol), OLEO_TO_GNUMERIC (*crow));

	if (formula != NULL)
		expr = oleo_parse_formula (formula, sheet, *ccol, *crow);

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
			oleo_set_style (sheet, *ccol, *crow, style);

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
oleo_deal_with_format (guint8 *str, Sheet *sheet, int *ccol, int *crow,
		       MStyle **style)
{
	char *ptr = str + 1, fmt_string[100];
	MStyle *mstyle = mstyle_new_default ();

	fmt_string[0] = '\0';

	while (*ptr) {
		char c=*ptr++;

		switch (c) {
		case 'c' : *ccol = astol (&ptr); break;
		case 'r' : *crow = astol (&ptr); break;
		case 'F': case 'G':
			c=*ptr++;

			strcpy (fmt_string, "0");
			if (isdigit ((unsigned char)*ptr))
				append_zeros (fmt_string, astol (&ptr));
			switch (c) {
			case 'F':
				break;
			case '%':
				strcat (fmt_string, "%");
				break;
			default: /* Unknown format type... */
				fmt_string[0] = '\0'; /* - ignore completely */
		}
			break;
		case 'L':
			mstyle_set_align_h (mstyle, HALIGN_LEFT);
			break;
		case 'R':
			mstyle_set_align_h (mstyle, HALIGN_RIGHT);
	}
	}
	if (fmt_string[0])
		mstyle_set_format_text (mstyle, fmt_string);

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

void
oleo_read (IOContext *io_context, Workbook *wb, GsfInput *input)
{
	int sheetidx = 0;
	int ccol = 0, crow = 0;
	Sheet *sheet = NULL;
	MStyle *style = NULL;
	guint8 *line;
	GsfInputTextline *textline = gsf_input_textline_new (input);
	/* This should probably come from import dialog.  */
	GIConv ic = g_iconv_open ("UTF-8", "ISO-8859-1");

	sheet = oleo_new_sheet (wb, ++sheetidx);

	while (NULL != (line = gsf_input_textline_ascii_gets (textline))) {
		char *utf8line =
			g_convert_with_iconv (line, -1, ic, NULL, NULL, NULL);

		switch (utf8line[0]) {
		case '#': /* Comment */
			break;

		case 'C': oleo_deal_with_cell (utf8line, sheet, &ccol, &crow, style);
			break;

		case 'F': oleo_deal_with_format (utf8line, sheet, &ccol, &crow, &style);
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

	g_iconv_close (ic);
	g_object_unref (G_OBJECT (textline));
}
