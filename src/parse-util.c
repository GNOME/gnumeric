/* vim: set sw=8: */

/*
 * parse-util.c: Various utility routines to parse or produce
 *     string representations of common reference types.
 *
 * Copyright (C) 2000 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "parse-util.h"

#include "workbook.h"
#include "sheet.h"
#include "value.h"
#include "ranges.h"
#include "cell.h"
#include "expr.h"
#include "number-match.h"
#include "format.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <glib.h>

/* Can remove sheet since local references have NULL sheet */
char *
cellref_name (CellRef const *cell_ref, ParsePos const *pp, gboolean no_sheetname)
{
	static char buffer [sizeof (long) * 4 + 4];
	char *p = buffer;
	int col, row;
	Sheet *sheet = cell_ref->sheet;

	if (cell_ref->col_relative)
		col = pp->eval.col + cell_ref->col;
	else {
		*p++ = '$';
		col = cell_ref->col;
	}

	/* ICK!  XL compatibility kludge */
	col %= SHEET_MAX_COLS;
	if (col < 0)
		col += SHEET_MAX_COLS;

	if (col <= 'Z'-'A'){
		*p++ = col + 'A';
	} else {
		int a = col / ('Z'-'A'+1);
		int b = col % ('Z'-'A'+1);

		*p++ = a + 'A' - 1;
		*p++ = b + 'A';
	}
	if (cell_ref->row_relative)
		row = pp->eval.row + cell_ref->row;
	else {
		*p++ = '$';
		row = cell_ref->row;
	}

	/* ICK!  XL compatibility kludge */
	row %= SHEET_MAX_ROWS;
	if (row < 0)
		row += SHEET_MAX_ROWS;

	sprintf (p, "%d", row+1);

	/* If it is a non-local reference, add the path to the external sheet */
	if (sheet != NULL && !no_sheetname) {
		/* pp->wb==NULL happens for the leak printer.  */
		if (pp->wb == NULL || sheet->workbook == pp->wb)
			return g_strconcat (sheet->name_quoted, "!", buffer, NULL);
		return g_strconcat ("[", sheet->workbook->filename, "]",
				    sheet->name_quoted, "!", buffer, NULL);
	} else
		return g_strdup (buffer);
}

char const *
cellref_a1_get (CellRef *out, char const *in, CellPos const *pos)
{
	int col = 0;
	int row = 0;

	g_return_val_if_fail (in != NULL, NULL);
	g_return_val_if_fail (out != NULL, NULL);

	/* Try to parse a column */
	if (*in == '$'){
		out->col_relative = FALSE;
		in++;
	} else
		out->col_relative = TRUE;

	if (!(toupper (*in) >= 'A' && toupper (*in) <= 'Z'))
		return NULL;

	col = toupper (*in++) - 'A';

	if (toupper (*in) >= 'A' && toupper (*in) <= 'Z')
		col = (col+1) * ('Z'-'A'+1) + toupper (*in++) - 'A';

	/* Try to parse a row */
	if (*in == '$'){
		out->row_relative = FALSE;
		in++;
	} else
		out->row_relative = TRUE;

	if (!(*in >= '1' && *in <= '9'))
		return NULL;

	while (isdigit ((unsigned char)*in)){
		row = row * 10 + *in - '0';
		in++;
	}
	if (row > SHEET_MAX_ROWS)
		return NULL;
	row--;

	/* Setup the cell reference information */
	if (out->row_relative)
		out->row = row - pos->row;
	else
		out->row = row;

	if (out->col_relative)
		out->col = col - pos->col;
	else
		out->col = col;

	out->sheet = NULL;

	return in;
}

static gboolean
r1c1_get_item (int *num, unsigned char *rel, char const * *in)
{
	gboolean neg = FALSE;

	if (**in == '\0')
		return FALSE;

	if (**in == '[') {
		(*in)++;
		*rel = TRUE;
		if (!**in)
			return FALSE;

		if (**in == '+')
			(*in)++;
		else if (**in == '-') {
			neg = TRUE;
			(*in)++;
		}
	}
	*num = 0;

	while (**in && isdigit ((unsigned char)**in)) {
		*num = *num * 10 + **in - '0';
		(*in)++;
	}

	if (neg)
		*num = -*num;

	if (**in == ']')
		(*in)++;

	return TRUE;
}

char const *
cellref_r1c1_get (CellRef *out, char const *in, CellPos const *pos)
{
	g_return_val_if_fail (in != NULL, NULL);
	g_return_val_if_fail (out != NULL, NULL);

	out->row_relative = FALSE;
	out->col_relative = FALSE;
	out->col = pos->col;
	out->row = pos->row;
	out->sheet = NULL;

	if (*in == 'R') {
		in++;
		if (!r1c1_get_item (&out->row, &out->row_relative, &in))
			return NULL;
	} else
		return NULL;

	if (*in == 'C') {
		in++;
		if (!r1c1_get_item (&out->col, &out->col_relative, &in))
			return NULL;
	} else
		return NULL;

	out->col--;
	out->row--;
	return in;
}

/**
 * cellref_get:
 * @out: destination CellRef
 * @in: reference description text, no leading
 *      whitespace allowed.
 *
 * Converts the char * representation of a Cell reference into
 * an internal representation.
 *
 * Return value: TRUE if no format errors found.
 **/
char const *
cellref_get (CellRef *out, char const *in, CellPos const *pos)
{
	char const *res = cellref_a1_get (out, in, pos);
	if (res != NULL)
		return res;
	return cellref_r1c1_get (out, in, pos);
}

/****************************************************************************/

/**
 * gnumeric_char_start_expr_p :
 *
 * Can the supplied string be an expression ?  It does not guarantee that it is,
 * however, it is possible.  If it is possible it strips off any header
 * characters that are not relevant.
 *
 * NOTE : things like -1,234 will match
 */
char const *
gnumeric_char_start_expr_p (char const * c)
{
	char c0;

	if (NULL == c)
		return NULL;

	c0 = *c;

	if (c0 == '=' || c0 == '@')
		return c + 1;

	if ((c0 == '-' || c0 == '+') && c[1] != 0 && c0 != c[1]) {
		char *end;

		/*
		 * Ok, we have a string that
		 * 1. starts with a sign
		 * 2. does not start with the sign repeated (think --------)
		 * 3. is more than one character
		 *
		 * Now we check whether we have a number.  We don't want
		 * numbers to be treated as formulae.  FIXME: this really
		 * just checks for C-syntax numbers.
		 */
		errno = 0;
		(void) strtognum (c, &end);
		if (errno || *end != 0 || end == c)
			return (c0 == '+') ? c + 1 : c;
		/* Otherwise, it's a number.  */
	}
	return NULL;
}

static char *
col_name_internal (char *buf, int col)
{
	g_return_val_if_fail (col < SHEET_MAX_COLS, buf);
	g_return_val_if_fail (col >= 0, buf);

	if (col <= 'Z'-'A'){
		*buf++ = col + 'A';
	} else {
		int a = col / ('Z'-'A'+1);
		int b = col % ('Z'-'A'+1);

		*buf++ = a + 'A' - 1;
		*buf++ = b + 'A';
	}
	return buf;
}

char const *
col_name (int col)
{
	static char buffer [3]; /* What if SHEET_MAX_COLS is changed ? */
	char *res = col_name_internal (buffer, col);
	*res = '\0';
	return buffer;
}

char const *
cols_name (int start_col, int end_col)
{
	static char buffer [16]; /* Why is this 16 ? */
	char *res = col_name_internal (buffer, start_col);

	if (start_col != end_col) {
		*res = ':';
		res = col_name_internal (res + 1, end_col);
	}
	*res = '\0';
	return buffer;
}

static char *
row_name_internal (char *buf, int row)
{
	int len = g_snprintf (buf, 6, "%d", row + 1); /* The 6 is hardcoded, see comments in row{s}_name */
	return buf + len;
}

char const *
row_name (int row)
{
	static char buffer [6]; /* What if SHEET_MAX_ROWS changes? */
	char *res = row_name_internal (buffer, row);
	*res = '\0';
	return buffer;
}

char const *
rows_name (int start_row, int end_row)
{
	static char buffer [13]; /* What if SHEET_MAX_ROWS changes? */
	char *res = row_name_internal (buffer, start_row);

	if (start_row != end_row) {
		*res = ':';
		res = row_name_internal (res + 1, end_row);
	}
	*res = '\0';
	return buffer;
}

char const *
cell_coord_name (int col, int row)
{
	static char buffer [2 + 4 * sizeof (long)];
	char *res = col_name_internal (buffer, col);
	sprintf (res, "%d", row + 1);
	return buffer;
}

char const *
cell_pos_name (CellPos const *pos)
{
	g_return_val_if_fail (pos != NULL, "ERROR");

	return cell_coord_name (pos->col, pos->row);
}

char const *
cell_name (Cell const *cell)
{
	g_return_val_if_fail (cell != NULL, "ERROR");

	return cell_coord_name (cell->pos.col, cell->pos.row);
}

/**
 * Converts a column name into an integer
 **/
int
parse_col_name (char const *cell_str, char const **endptr)
{
	char c;
	int col = 0;

	if (endptr)
		*endptr = cell_str;

	c = toupper ((unsigned char)*cell_str++);
	if (c < 'A' || c > 'Z')
		return 0;

	col = c - 'A';
	c = toupper ((unsigned char)*cell_str);
	if (c >= 'A' && c <= 'Z') {
		col = ((col + 1) * ('Z' - 'A' + 1)) + (c - 'A');
		cell_str++;
	}

	if (col >= SHEET_MAX_COLS)
		return 0;

	if (endptr)
		*endptr = cell_str;

	return col;
}

/**
 * parse_cell_name
 * @cell_name:   a string representation of a cell name.
 * @col:         result col
 * @row:         result row
 * @strict:      if this is TRUE, then parsing stops at possible errors,
 *               otherwise an attempt is made to return cell names with trailing garbage.
 *
 * Return value: true if the cell_name could be successfully parsed
 */
gboolean
parse_cell_name (char const *cell_str, int *col, int *row, gboolean strict, int *chars_read)
{
	char const * const original = cell_str;
	unsigned char c;
	gboolean found_digits = FALSE;

	if (*cell_str == '$')
		cell_str++;

	/* Parse column name: one or two letters.  */
	c = toupper ((unsigned char) *cell_str);
	cell_str++;
	if (c < 'A' || c > 'Z')
		return FALSE;

	*col = c - 'A';
	c = toupper ((unsigned char)*cell_str);
	if (c >= 'A' && c <= 'Z') {
		*col = ((*col + 1) * ('Z' - 'A' + 1)) + (c - 'A');
		cell_str++;
	}
	if (*col >= SHEET_MAX_COLS)
		return FALSE;

	if (*cell_str == '$')
		cell_str++;

	/* Parse row number: a sequence of digits.  */
	for (*row = 0; *cell_str; cell_str++) {
		if (*cell_str < '0' || *cell_str > '9'){
			if (found_digits && strict == FALSE){
				break;
			} else
				return FALSE;
		}
		found_digits = TRUE;
		*row = *row * 10 + (*cell_str - '0');
		if (*row > SHEET_MAX_ROWS) /* Note: ">" is deliberate.  */
			return FALSE;
	}
	if (*row == 0)
		return FALSE;

	/* Internal row numbers are one less than the displayed.  */
	(*row)--;

	if (chars_read)
		*chars_read = cell_str - original;
	return TRUE;
}

/**
 * parse_text_value_or_expr : Utility routine to parse a string and convert it
 *     into an expression or value.
 *
 * @pos : If the string looks like an expression parse it at this location.
 * @text: The text to be parsed.
 * @val : Returns a Value * if the text was a value, otherwise NULL.
 * @expr: Returns an GnmExpr * if the text was an expression, otherwise NULL.
 * @current_format : Optional, current number format.
 *
 * If there is a parse failure for an expression an error Value with the syntax
 * error is returned.
 */
void
parse_text_value_or_expr (ParsePos const *pos, char const *text,
			  Value **val, GnmExpr const **expr,
			  StyleFormat *current_format /* can be NULL */)
{
	char const *expr_start;

	*expr = NULL;

	/* Does it match any formats?  */
	*val = format_match (text, current_format);
	if (*val != NULL)
		return;

	/* If it does not match known formats, see if it is an expression */
	expr_start = gnumeric_char_start_expr_p (text);
	if (NULL != expr_start && *expr_start) {
		*expr = gnm_expr_parse_str (expr_start, pos,
			GNM_EXPR_PARSE_DEFAULT, NULL);
		if (*expr != NULL)
			return;
	}

	/* Fall back on string */
	*val = value_new_string (text);
}

ParseError *
parse_error_init (ParseError *pe)
{
	pe->id         = PERR_NONE;
	pe->message    = NULL;
	pe->begin_char = -1;
	pe->end_char   = -1;

	return pe;
}

void
parse_error_free (ParseError *pe)
{
	if (pe->message != NULL) {
		g_free (pe->message);
		pe->message = NULL;
	}
}


#undef DEBUG_PARSE_SURROUNDING_RANGES

gboolean   
parse_surrounding_ranges  (char const *text, gint cursor, Sheet *sheet, 
			   gboolean single_range_only, gint *from, gint *to,
			   RangeRef **range)
{
	int start, end, last;
	gchar *test;
	gboolean last_was_alnum = FALSE;
	
	if (text == NULL)
		return FALSE;

#ifdef DEBUG_PARSE_SURROUNDING_RANGES
			g_warning ("Starting  to parse [%s]", text);
#endif
	
	last = strlen (text);
	for (start = 0;
	     start <= cursor;
	     start = g_utf8_next_char (text + start) - text) {
		int next_end = -1;
		gboolean next_was_alnum = FALSE;
		gunichar c = g_utf8_get_char (text + start);
		gboolean is_alnum = g_unichar_isalnum (c);

		/* A range does not start in the middle of a word.  */
		if (last_was_alnum && is_alnum)
			continue;
		last_was_alnum = is_alnum;
		/* A range starts with a letter, a quote, or a dollar sign.  */
		if (is_alnum ? g_unichar_isdigit (c) : (c != '\'' && c != '$'))
			continue;

		for (end = last; end >= MAX (cursor, start + 1); end = next_end) {
			GSList *ranges;
			gunichar c_end;
			gboolean is_alnum;

			next_end = g_utf8_prev_char (text + end) - text;
			c_end = g_utf8_get_char (text + next_end);
			is_alnum = g_unichar_isalnum (c_end);

			/* A range does not end in the middle of a word.  */
			if (is_alnum && next_was_alnum)
				continue;
			next_was_alnum = is_alnum;
			/* A range ends in a letter, digit, or quote.  */
			if (!is_alnum && c_end != '\'')
				continue;

			test = g_strndup (text + start, end - start);

#ifdef DEBUG_PARSE_SURROUNDING_RANGES
			g_warning ("Parsing [%s]", test);
#endif

			ranges = global_range_list_parse (sheet, test);
			g_free (test);

			if (ranges != NULL) {
				if ((ranges->next != NULL) && single_range_only) { 
					range_list_destroy (ranges);
					continue;
				}
				*from = start;
				*to = end;
				if (range) {
					*range = value_to_rangeref 
						((Value *) ((g_slist_last 
							     (ranges))->data), FALSE);
				}
				range_list_destroy (ranges);
				return TRUE;
			}
		}
	}
	return FALSE;
}
