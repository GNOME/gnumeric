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

#include <libgnome/gnome-i18n.h>

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

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


static char const *
oleo_get_gnumeric_expr (char *g_expr, char const *o_expr,
			ParsePos const *cur_pos)
{
	char const *from = o_expr;
	char *to = g_expr;

	while (*from) {
		*to = '\0';
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

			name = cellref_name (&start, cur_pos, TRUE);

			strcat (to, name);
			g_free (name);
			if (!cellref_equal (&start, &end)) {
				strcat (to, ":");
				name = cellref_name (&end, cur_pos, TRUE);
				strcat (to, name);
				g_free (name);
			}
			to += strlen (to);
		} else
			*to++=*from++;
	}
	*to = '\0';

#if OLEO_DEBUG > 0
	g_warning ("\"%s\"->\"%s\"\n", o_expr, g_expr);
#endif /* OLEO_DEBUG */

	return g_expr;
}


static ExprTree *
oleo_parse_formula (char const *text, Sheet *sheet, int col, int row)
{
	ParsePos pos;
	ParseError error;
	ExprTree *expr;
	char gnumeric_text[2048];

	Cell const *cell = sheet_cell_fetch (sheet,
		OLEO_TO_GNUMERIC (col), OLEO_TO_GNUMERIC (row));

	parse_pos_init_cell (&pos, cell);

	expr = expr_parse_str (oleo_get_gnumeric_expr (gnumeric_text,
						       text, &pos),
			       &pos, GNM_PARSER_DEFAULT,
			       parse_error_init (&error));

	if (error.id!=PERR_NONE) {
		g_warning ("%s \"%s\" at %s!%s\n",  error.message, gnumeric_text,
			   sheet->name_unquoted,
			   cell_coord_name (OLEO_TO_GNUMERIC (col), OLEO_TO_GNUMERIC (row)));
	}
	parse_error_free (&error);

	return expr;
}

static void
oleo_deal_with_cell (char *str, Sheet *sheet, MStyle *style, int *ccol, int *crow)
{
	Cell *cell;
	ExprTree *expr = NULL;
	char *ptr = str + 1, *cval = NULL, *formula = NULL;

	while (*ptr) {
		int quotes = 0;
		if (*ptr != ';') {
#if OLEO_DEBUG > 0
			g_warning ("ptr : %s\n", ptr);
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
			g_warning ("oleo: Don't know how to deal with C; '%c'\n",
				   *ptr);
#endif
			ptr = ""; /* I wish C had multilevel break */
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
		g_warning ("oleo: cval is NULL.\n");
#endif
		/* We can still store the expression, even if the value is missing */
		if (expr != NULL)
			cell_set_expr (cell, expr);
	}
	if (expr)
		expr_tree_unref (expr);
}


/* NOTE : We don't care to much about formatting as such, but we need to
 * parse the command as it may update current row/column
 */
static void
oleo_deal_with_format (MStyle **style,
		       char *str, Sheet *sheet, int *ccol, int *crow)
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
oleo_read (IOContext *io_context, Workbook *wb, gchar const *filename)
{
	FILE *f;
	int sheetidx  = 0;
	int ccol = 0, crow = 0;
	Sheet *sheet = NULL;
	MStyle *style = NULL;
	char str[2048];
	ErrorInfo *error;

	f = gnumeric_fopen_error_info (filename, "rb", &error);
	if (f == NULL) {
		gnumeric_io_error_info_set (io_context, error);
		return;
	}

	sheet = oleo_new_sheet (wb, sheetidx++);

	while (1) {
		char *n;
		fgets (str, 2000, f);
		str[2000] = 0;
		if (feof (f))
			break;

		n = strchr (str, '\n');
		if (n)
			*n = 0;
		else
			break;

		switch (str[0]) {

		case '#': /* Comment */
			break;

		case 'C': oleo_deal_with_cell (str, sheet, style, &ccol, &crow);
			break;

		case 'F': oleo_deal_with_format (&style, str, sheet, &ccol, &crow);
			break;
		default: /* unknown */
#if OLEO_DEBUG > 0
			g_warning ("oleo: Don't know how to deal with %c.\n",
				   str[0]);
#endif
			break;
		}
	}

	fclose (f);
}
