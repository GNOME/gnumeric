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
#include <string.h>

inline static char *
col_name_internal (char *buf, int col)
{
	g_return_val_if_fail (col < SHEET_MAX_COLS, buf);
	g_return_val_if_fail (col >= 0, buf);

	if (col <= 'Z'-'A') {
		*buf++ = col + 'A';
	} else {
		int a = col / ('Z'-'A'+1);
		int b = col % ('Z'-'A'+1);

		*buf++ = a + 'A' - 1;
		*buf++ = b + 'A';
	}
	return buf;
}

inline static char *
row_name_internal (char *buf, int row)
{
	int len = g_snprintf (buf, 6, "%d", row + 1); /* The 6 is hardcoded, see comments in row{s}_name */
	return buf + len;
}


inline static int
cellref_abs_col (CellRef const *ref, ParsePos const *pp)
{
	int col = (ref->col_relative) ? pp->eval.col + ref->col : ref->col;

	/* ICK!  XL compatibility kludge */
	col %= SHEET_MAX_COLS;
	if (col < 0)
		return col + SHEET_MAX_COLS;
	return col;
}

inline static int
cellref_abs_row (CellRef const *ref, ParsePos const *pp)
{
	int row = (ref->row_relative) ? pp->eval.row + ref->row : ref->row;

	/* ICK!  XL compatibility kludge */
	row %= SHEET_MAX_ROWS;
	if (row < 0)
		return row + SHEET_MAX_ROWS;
	return row;
}

/**
 * rangeref_as_string :
 * @ref :
 * @pp :
 *
 **/
char *
rangeref_as_string (RangeRef const *ref, ParsePos const *pp)
{
	char buf [2*(10  /* max digits in 32 bit row */
		     + 7 /* max letters in 32 bit col */
		     + 2 /* dollar signs for abs */
		    ) + 2 /* colon and eos */];
	char *p = buf;
	Range r;

	r.start.col = cellref_abs_col (&ref->a, pp);
	r.end.col   = cellref_abs_col (&ref->b, pp);
	r.start.row = cellref_abs_row (&ref->a, pp);
	r.end.row   = cellref_abs_row (&ref->b, pp);

	/* be sure to use else if so that a1:iv65535 does not vanish */
	if (r.start.col == 0 && r.end.col == SHEET_MAX_COLS-1) {
		if (!ref->a.row_relative)
			*p++ = '$';
		p = row_name_internal (p, r.start.row);
		*p++ = ':';
		if (!ref->b.row_relative)
			*p++ = '$';
		p = row_name_internal (p, r.end.row);
	} else if (r.start.row == 0 && r.end.row == SHEET_MAX_ROWS-1) {
		if (!ref->a.col_relative)
			*p++ = '$';
		p = col_name_internal (p, r.start.col);
		*p++ = ':';
		if (!ref->b.col_relative)
			*p++ = '$';
		p = col_name_internal (p, r.end.col);
	} else {
		if (!ref->a.col_relative)
			*p++ = '$';
		p = col_name_internal (p, r.start.col);
		if (!ref->a.row_relative)
			*p++ = '$';
		p = row_name_internal (p, r.start.row);

		if (r.start.col != r.end.col || r.start.row != r.end.row) {
			*p++ = ':';
			if (!ref->b.col_relative)
				*p++ = '$';
			p = col_name_internal (p, r.end.col);
			if (!ref->b.row_relative)
				*p++ = '$';
			p = row_name_internal (p, r.end.row);
		}
	}
	*p = '\0';

	if (ref->a.sheet == NULL)
		return g_strdup (buf);

	/* For the expression leak printer. */
	if (pp->wb == NULL && pp->sheet == NULL)
		return g_strconcat ("'?'!", buf, NULL);

	if (ref->b.sheet == NULL || ref->a.sheet == ref->b.sheet) {
		if (pp->wb == NULL || ref->a.sheet->workbook == pp->wb)
			return g_strconcat (ref->a.sheet->name_quoted, "!", buf, NULL);
		return g_strconcat ("[", ref->a.sheet->workbook->filename, "]",
				    ref->a.sheet->name_quoted, "!", buf, NULL);
	} else {
		if (pp->wb == NULL || ref->a.sheet->workbook == pp->wb)
			return g_strconcat (ref->a.sheet->name_quoted, ":", ref->b.sheet->name_quoted, "!", buf, NULL);
		return g_strconcat ("[", ref->a.sheet->workbook->filename, "]",
				    ref->a.sheet->name_quoted, ":", ref->b.sheet->name_quoted, "!", buf, NULL);
	}
}

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

	if (col <= 'Z'-'A') {
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
		if (pp->wb == NULL && pp->sheet == NULL) {
			/* For the expression leak printer.  */
			return g_strconcat ("'?'|", buffer, NULL);
		}

		if (pp->wb == NULL || sheet->workbook == pp->wb)
			return g_strconcat (sheet->name_quoted, "!", buffer, NULL);
		return g_strconcat ("[", sheet->workbook->filename, "]",
				    sheet->name_quoted, "!", buffer, NULL);
	} else
		return g_strdup (buffer);
}

static char const *
cellref_a1_get (CellRef *out, char const *in, CellPos const *pos)
{
	int col = 0;
	int row = 0;
	unsigned char uc;

	g_return_val_if_fail (in != NULL, NULL);
	g_return_val_if_fail (out != NULL, NULL);

	/* Try to parse a column */
	if (*in == '$') {
		out->col_relative = FALSE;
		in++;
	} else
		out->col_relative = TRUE;

	/*
	 * Careful here!  'A' and 'a' are not necessarily the only
	 * characters which toupper maps to 'A'.
	 */
	uc = (unsigned char)*in;
	if (!((uc >= 'A' && uc <= 'Z') ||
	      (uc >= 'a' && uc <= 'z')))
		return NULL;
	col = toupper (uc) - 'A';
	in++;

	uc = (unsigned char)*in;
	if ((uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z')) {
		col = (col + 1) * ('Z' - 'A' + 1) + toupper (uc) - 'A';
		in++;
	}
	if (col >= SHEET_MAX_COLS)
		return NULL;

	/* Try to parse a row */
	if (*in == '$') {
		out->row_relative = FALSE;
		in++;
	} else
		out->row_relative = TRUE;

	if (!(*in >= '1' && *in <= '9'))
		return NULL;

	while (isdigit ((unsigned char)*in)) {
		row = row * 10 + *in - '0';
		if (row > SHEET_MAX_ROWS)
			return NULL;
		in++;
	}
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

static char const *
cellref_r1c1_get (CellRef *out, char const *in, CellPos const *pos)
{
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
	char const *res;
	
	g_return_val_if_fail (in != NULL, NULL);
	g_return_val_if_fail (out != NULL, NULL);

	res = cellref_a1_get (out, in, pos);
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

	if (c0 == '=' || c0 == '@' || (c0 == '+' && c[1] == 0))
		return c + 1;

	if ((c0 == '-' || c0 == '+') && c0 != c[1]) {
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
 * @pos:         result
 * @strict:      if this is TRUE, then parsing stops at possible errors,
 *               otherwise an attempt is made to return cell names with trailing garbage.
 *
 * Return value: true if the cell_name could be successfully parsed
 */
gboolean
parse_cell_name (char const *cell_str, CellPos *res, gboolean strict, int *chars_read)
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

	res->col = c - 'A';
	c = toupper ((unsigned char)*cell_str);
	if (c >= 'A' && c <= 'Z') {
		res->col = ((res->col + 1) * ('Z' - 'A' + 1)) + (c - 'A');
		cell_str++;
	}
	if (res->col >= SHEET_MAX_COLS)
		return FALSE;

	if (*cell_str == '$')
		cell_str++;

	/* Parse row number: a sequence of digits.  */
	for (res->row = 0; *cell_str; cell_str++) {
		if (*cell_str < '0' || *cell_str > '9') {
			if (found_digits && strict == FALSE) {
				break;
			} else
				return FALSE;
		}
		found_digits = TRUE;
		res->row = res->row * 10 + (*cell_str - '0');
		if (res->row > SHEET_MAX_ROWS) /* Note: ">" is deliberate.  */
			return FALSE;
	}
	if (res->row == 0)
		return FALSE;

	/* Internal row numbers are one less than the displayed.  */
	(res->row)--;

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

/***************************************************************************/

static char const *
check_quoted (char const *str, int *num_escapes)
{
	if (*str == '\'' || *str == '\"') {
		char const quote = *str;
		*num_escapes = 0;
		for (; *str && *str != quote; str = g_utf8_next_char (str))
			if (*str == '\\' && str[1]) {
				str++;
				(*num_escapes)++;
			}
	} else
		*num_escapes = -1;
	return str;
}

static void
unquote (char *dst, char const *src, int n)
{
	while (n-- > 0)
		if (*src == '\\' && src[1]) {
			int n = g_utf8_skip [*(guchar *)(++src)];
			strncpy (dst, src, n);
			dst += n;
			src += n;
		} else
			*dst++ = *src++;
	*dst = 0;
}

static char const *
wbref_parse (char const *start, Workbook **wb)
{
	/* Is this an external reference ? */
	if (*start == '[') {
		int num_escapes;
		char const *end = check_quoted (start, &num_escapes);
		char *name;

		if (end == start) {
			end = strchr (start, ']');
			if (end == NULL)
				return start;
		}
		if (*end != ']')
			return NULL;

		/* might be too big if quoted (remember leading [' */
		name = g_alloca (1 + end - start - 2);
		if (num_escapes < 0) {
			strncpy (name, start+1, end-start-1);
			name [end-start-1] = '\0';
		} else
			unquote (name, start+2, end-start-2);
#warning TODO
		return end + 1;
	}

	return start;
}

static char const *
sheet_parse (char const *start, Sheet **sheet, Workbook const *wb,
	     gboolean allow_3d)
{
	int num_escapes;
	char const *end = check_quoted (start, &num_escapes);
	char *name;

	*sheet = NULL;

	/* Quoted definitely indicates a sheet */
	if (end == start)
		while (*end && g_unichar_isalnum (g_utf8_get_char (end)))
			end = g_utf8_next_char (end);

	if (*end != '!' && (!allow_3d || *end != ':'))
		return start;

	/* might be too big if quoted */
	name = g_alloca (1 + end - start);
	if (num_escapes < 0) {
		strncpy (name, start, end-start);
		name [end-start] = '\0';
	} else
		unquote (name, start+1, end-start-2);

	*sheet = workbook_sheet_by_name (wb, name);
	return *sheet != NULL ? end : start;
}

static char const *
col_parse (char const *str, int *res, unsigned char *relative)
{
	char const *ptr = str;
	int col = -1;

	if (!(*relative = (*ptr != '$')))
		ptr++;

	for (; TRUE ; ptr++)
		if (('a' <= *ptr && *ptr <= 'z'))
			col = 26 * (col + 1) + (*ptr - 'a');
		else if (('A' <= *ptr && *ptr <= 'Z'))
			col = 26 * (col + 1) + (*ptr - 'A');
		else if (ptr != str && col < SHEET_MAX_COLS) {
			*res = col;
			return ptr;
		} else
			return str;
}

static char const *
row_parse (char const *str, int *res, unsigned char *relative)
{
	char const *ptr = str;
	int row;

	if (!(*relative = (*ptr != '$')))
		ptr++;

	row = strtol (str, (char **)&ptr, 10);
	if (ptr != str && 0 < row && row <= SHEET_MAX_ROWS) {
		*res = row - 1;
		return ptr;
	} else
		return str;
}

/** rangeref_parse :
 * @res : where to store the result
 * @start : the start of the string to parse
 * @pos : the location to parse relative to
 *
 * Returns the a pointer to the first invalid character.
 * If the result != @start then @res is valid.
 **/
char const *
rangeref_parse (RangeRef *res, char const *start, ParsePos const *pp)
{
	char const *ptr = start, *start_sheet, *tmp1, *tmp2;
	Workbook *wb;

	g_return_val_if_fail (start != NULL, start);
	g_return_val_if_fail (pp != NULL, start);

	wb = pp->wb;
	start_sheet = wbref_parse (start, &wb);
	if (start_sheet == NULL)
		return start; /* TODO error unknown workbook */
	ptr = sheet_parse (start_sheet, &res->a.sheet, wb, TRUE);
	if (ptr == NULL)
		return start; /* TODO erro unknown sheet */
	if (ptr != start_sheet) {
		if (*ptr == ':') { /* 3d ref */
			ptr = sheet_parse (ptr, &res->b.sheet, wb, FALSE);
			if (ptr == NULL)
				return start; /* TODO error unknown sheet */
		} else
			res->b.sheet = NULL;

		if (*ptr != '!')
			return start; /* TODO syntax error */
	}

	tmp1 = col_parse (ptr, &res->a.col, &res->a.col_relative);
	if (tmp1 == ptr) { /* check for row only ref 2:3 */ 
		tmp1 = row_parse (ptr, &res->a.row, &res->a.row_relative);
		if (*tmp1++ != ':') /* row only requires : even for singleton */
			return start;
		tmp2 = row_parse (tmp1, &res->b.row, &res->b.row_relative);
		if (tmp2 == tmp1)
			return start;
		res->a.col_relative = res->b.col_relative = FALSE;
		res->a.col = 0; res->b.col = SHEET_MAX_COLS-1;
		return tmp2;
	}

	tmp2 = row_parse (tmp1, &res->a.row, &res->a.row_relative);
	if (tmp2 == tmp1) { /* check for col only ref B:C */ 
		if (*tmp1++ != ':') /* col only requires : even for singleton */
			return start;
		tmp2 = col_parse (tmp1, &res->b.col, &res->b.col_relative);
		if (tmp2 == tmp1)
			return start;
		res->a.row_relative = res->b.row_relative = FALSE;
		res->a.row = 0; res->b.row = SHEET_MAX_ROWS-1;
		return tmp2;
	}

	/* prepare as if its a singleton, in case we want to fall back */
	if (res->a.col_relative)
		res->a.col -= pp->eval.col;
	if (res->a.row_relative)
		res->a.row -= pp->eval.row;
	res->b = res->a;
	if (*tmp2 != ':')
		return tmp2;
	ptr = tmp2;

	tmp1 = col_parse (ptr+1, &res->b.col, &res->b.col_relative);
	if (tmp1 == (ptr+1))
		return ptr;	/* strange, but valid singleton */
	tmp2 = row_parse (tmp1, &res->b.row, &res->b.row_relative);
	if (tmp2 == tmp1)
		return ptr;	/* strange, but valid singleton */

	if (res->b.col_relative)
		res->b.col -= pp->eval.col;
	if (res->b.row_relative)
		res->b.row -= pp->eval.row;
	return tmp2;
}
