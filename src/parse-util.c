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

#include "application.h"
#include "workbook-priv.h"
#include "sheet.h"
#include "value.h"
#include "ranges.h"
#include "cell.h"
#include "expr.h"
#include "number-match.h"
#include "format.h"
#include "expr-name.h"
#include "str.h"
/* For def_expr_name_handler: */
#include "expr-impl.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>

static void
col_name_internal (GString *target, int col)
{
	static const int steps[] = {
		26,
		26 * 26,
		26 * 26 * 26,
		26 * 26 * 26 * 26,
		26 * 26 * 26 * 26 * 26,
		26 * 26 * 26 * 26 * 26 * 26,
		INT_MAX
	};
	int i;
	char *dst;

	for (i = 0; col >= steps[i]; i++)
		col -= steps[i];

	g_string_set_size (target, target->len + (i + 1));
	dst = target->str + target->len;
	while (i-- >= 0) {
		*--dst = 'A' + col % 26;
		col /= 26;
	}
}

char const *
col_name (int col)
{
	static GString *buffer = NULL;
	if (!buffer)
		buffer = g_string_new ("");
	g_string_truncate (buffer, 0);

	col_name_internal (buffer, col);

	return buffer->str;
}

char const *
cols_name (int start_col, int end_col)
{
	static GString *buffer = NULL;
	if (!buffer)
		buffer = g_string_new ("");
	g_string_truncate (buffer, 0);

	col_name_internal (buffer, start_col);
	if (start_col != end_col) {
		g_string_append_c (buffer, ':');
		col_name_internal (buffer, end_col);
	}

	return buffer->str;
}

char const *
col_parse (char const *str, int *res, unsigned char *relative)
{
	char const *ptr, *start = str;
	int col = -1;

	if (!(*relative = (*start != '$')))
		start++;

	for (ptr = start; col < SHEET_MAX_COLS ; ptr++)
		if (('a' <= *ptr && *ptr <= 'z'))
			col = 26 * (col + 1) + (*ptr - 'a');
		else if (('A' <= *ptr && *ptr <= 'Z'))
			col = 26 * (col + 1) + (*ptr - 'A');
		else if (ptr != start) {
			*res = col;
			return ptr;
		} else
			return NULL;
	return NULL;
}

/***************************************************************************/

static void
row_name_internal (GString *target, int row)
{
	g_string_append_printf (target, "%d", row + 1);
}

char const *
row_name (int row)
{
	static GString *buffer = NULL;
	if (!buffer)
		buffer = g_string_new ("");
	g_string_truncate (buffer, 0);

	row_name_internal (buffer, row);

	return buffer->str;
}

char const *
rows_name (int start_row, int end_row)
{
	static GString *buffer = NULL;
	if (!buffer)
		buffer = g_string_new ("");
	g_string_truncate (buffer, 0);

	row_name_internal (buffer, start_row);
	if (start_row != end_row) {
		g_string_append_c (buffer, ':');
		row_name_internal (buffer, end_row);
	}

	return buffer->str;
}

char const *
row_parse (char const *str, int *res, unsigned char *relative)
{
	char const *end, *ptr = str;
	long int row;

	if (!(*relative = (*ptr != '$')))
		ptr++;

	/* Initial '0' is not allowed.  */
	if (*ptr <= '0' || *ptr > '9')
		return NULL;

	row = strtol (ptr, (char **)&end, 10);
	if (ptr != end && 0 < row && row <= SHEET_MAX_ROWS) {
		*res = row - 1;
		return end;
	} else
		return NULL;
}

/***************************************************************************/

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
 * cellref_as_string :
 * @ref :
 * @pp  :
 * @no_sheetname :
 *
 * Returns a string that the caller needs to free containing the A1 format
 * representation of @ref as evaluated at @pp.  @no_sheetname can be used to
 * suppress the addition of the sheetname for non-local references.
 **/
void
cellref_as_string (GString *target, const GnmExprConventions *conv,
		   CellRef const *cell_ref,
		   ParsePos const *pp, gboolean no_sheetname)
{
	int col, row;
	Sheet *sheet = cell_ref->sheet;

	/* If it is a non-local reference, add the path to the external sheet */
	if (sheet != NULL && !no_sheetname) {
		if (pp->wb == NULL && pp->sheet == NULL)
			/* For the expression leak printer.  */
			g_string_append (target, "'?'");
		else if (pp->wb == NULL || sheet->workbook == pp->wb)
			g_string_append (target, sheet->name_quoted);
		else {
			g_string_append_c (target, '[');
			g_string_append (target, sheet->workbook->filename);
			g_string_append_c (target, ']');
			g_string_append (target, sheet->name_quoted);
		}
		g_string_append (target, conv->output_sheet_name_sep);
	}

	if (cell_ref->col_relative)
		col = pp->eval.col + cell_ref->col;
	else {
		g_string_append_c (target, '$');
		col = cell_ref->col;
	}

	/* ICK!  XL compatibility kludge */
	col %= SHEET_MAX_COLS;
	if (col < 0)
		col += SHEET_MAX_COLS;
	col_name_internal (target, col);

	if (cell_ref->row_relative)
		row = pp->eval.row + cell_ref->row;
	else {
		g_string_append_c (target, '$');
		row = cell_ref->row;
	}

	/* ICK!  XL compatibility kludge */
	row %= SHEET_MAX_ROWS;
	if (row < 0)
		row += SHEET_MAX_ROWS;
	row_name_internal (target, row);
}

/**
 * rangeref_as_string :
 * @ref :
 * @pp :
 *
 **/
void
rangeref_as_string (GString *target, const GnmExprConventions *conv,
		    RangeRef const *ref, ParsePos const *pp)
{
	Range r;

	r.start.col = cellref_abs_col (&ref->a, pp);
	r.end.col   = cellref_abs_col (&ref->b, pp);
	r.start.row = cellref_abs_row (&ref->a, pp);
	r.end.row   = cellref_abs_row (&ref->b, pp);

	if (ref->a.sheet) {
		if (pp->wb != NULL && ref->a.sheet->workbook != pp->wb) {
			g_string_append_c (target, '[');
			g_string_append (target, ref->a.sheet->workbook->filename);
			g_string_append_c (target, ']');
		}
		if (pp->wb == NULL && pp->sheet == NULL)
			/* For the expression leak printer. */
			g_string_append (target, "'?'");
		else if (ref->b.sheet == NULL || ref->a.sheet == ref->b.sheet)
			g_string_append (target, ref->a.sheet->name_quoted);
		else {
			g_string_append (target, ref->a.sheet->name_quoted);
			g_string_append_c (target, ':');
			g_string_append (target, ref->b.sheet->name_quoted);
		}
		g_string_append (target, conv->output_sheet_name_sep);
	}

	/* be sure to use else if so that a1:iv65535 does not vanish */
	if (r.start.col == 0 && r.end.col == SHEET_MAX_COLS-1) {
		if (!ref->a.row_relative)
			g_string_append_c (target, '$');
		row_name_internal (target, r.start.row);
		g_string_append_c (target, ':');
		if (!ref->b.row_relative)
			g_string_append_c (target, '$');
		row_name_internal (target, r.end.row);
	} else if (r.start.row == 0 && r.end.row == SHEET_MAX_ROWS-1) {
		if (!ref->a.col_relative)
			g_string_append_c (target, '$');
		col_name_internal (target, r.start.col);
		g_string_append_c (target, ':');
		if (!ref->b.col_relative)
			g_string_append_c (target, '$');
		col_name_internal (target, r.end.col);
	} else {
		if (!ref->a.col_relative)
			g_string_append_c (target, '$');
		col_name_internal (target, r.start.col);
		if (!ref->a.row_relative)
			g_string_append_c (target, '$');
		row_name_internal (target, r.start.row);

		if (r.start.col != r.end.col || r.start.row != r.end.row) {
			g_string_append_c (target, ':');
			if (!ref->b.col_relative)
				g_string_append_c (target, '$');
			col_name_internal (target, r.end.col);
			if (!ref->b.row_relative)
				g_string_append_c (target, '$');
			row_name_internal (target, r.end.row);
		}
	}
}

static char const *
cellref_a1_get (CellRef *out, char const *in, CellPos const *pos)
{
	int col;
	int row;

	g_return_val_if_fail (in != NULL, NULL);
	g_return_val_if_fail (out != NULL, NULL);

	in = col_parse (in, &col, &out->col_relative);
	if (!in)
		return NULL;

	in = row_parse (in, &row, &out->row_relative);
	if (!in)
		return NULL;

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

	while (**in >= '0' && **in <= '9') {
		*num = *num * 10 + (**in - '0');
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
 * cellref_parse:
 * @out: destination CellRef
 * @in: reference description text, no leading
 *      whitespace allowed.
 * @pos:
 *
 * Converts the char * representation of a Cell reference into
 * an internal representation.
 *
 * Return value: a pointer to the character following the cellref.
 **/
char const *
cellref_parse (CellRef *out, char const *in, CellPos const *pos)
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

char const *
cell_coord_name (int col, int row)
{
	static GString *buffer = NULL;
	if (!buffer)
		buffer = g_string_new ("");
	g_string_truncate (buffer, 0);

	col_name_internal (buffer, col);
	row_name_internal (buffer, row);

	return buffer->str;
}

char const *
cellpos_as_string (CellPos const *pos)
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
 * cellpos_parse
 * @cell_name:   a string representation of a cell name.
 * @pos:         result
 * @strict:      if this is TRUE, then parsing stops at possible errors,
 *               otherwise an attempt is made to return cell names with
 *               trailing garbage.
 *
 * Return value: pointer to following char on success, NULL on failure.
 * (In the strict case, that would be a pointer to the \0 or NULL.)
 */
char const *
cellpos_parse (char const *cell_str, CellPos *res, gboolean strict)
{
	unsigned char dummy_relative;

	cell_str = col_parse (cell_str, &res->col, &dummy_relative);
	if (!cell_str)
		return NULL;

	cell_str = row_parse (cell_str, &res->row, &dummy_relative);
	if (!cell_str)
		return NULL;

	if (*cell_str != 0 && strict)
		return NULL;

	return cell_str;
}

/**
 * gnm_expr_char_start_p :
 *
 * Can the supplied string be an expression ?  It does not guarantee that it is,
 * however, it is possible.  If it is possible it strips off any header
 * characters that are not relevant.
 *
 * NOTE : things like -1,234 will match
 */
char const *
gnm_expr_char_start_p (char const * c)
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

/**
 * parse_text_value_or_expr : Utility routine to parse a string and convert it
 *     into an expression or value.
 *
 * @pos : If the string looks like an expression parse it at this location.
 * @text: The text to be parsed.
 * @val : Returns a Value * if the text was a value, otherwise NULL.
 * @expr: Returns an GnmExpr * if the text was an expression, otherwise NULL.
 * @cur_fmt : Optional, current number format.
 * @date_conv : Optional, date parse conventions
 *
 * If there is a parse failure for an expression an error Value with the syntax
 * error is returned.
 */
void
parse_text_value_or_expr (ParsePos const *pos, char const *text,
			  Value **val, GnmExpr const **expr,
			  StyleFormat *cur_fmt,
			  GnmDateConventions const *date_conv)
{
	char const *expr_start;

	*expr = NULL;

	/* Does it match any formats?  */
	*val = format_match (text, cur_fmt, date_conv);
	if (*val != NULL)
		return;

	/* If it does not match known formats, see if it is an expression */
	expr_start = gnm_expr_char_start_p (text);
	if (NULL != expr_start && *expr_start) {
		*expr = gnm_expr_parse_str (expr_start, pos,
					    GNM_EXPR_PARSE_DEFAULT,
					    gnm_expr_conventions_default,
					    NULL);
		if (*expr != NULL)
			return;
	}

	/* Fall back on string */
	*val = value_new_string (text);
}

ParseError *
parse_error_init (ParseError *pe)
{
	pe->err		= NULL;
	pe->begin_char	= 0;
	pe->end_char	= 0;

	return pe;
}

void
parse_error_free (ParseError *pe)
{
	if (pe->err != NULL) {
		g_error_free (pe->err);
		pe->err = NULL;
	}
}

/***************************************************************************/

static char const *
check_quoted (char const *start, int *num_escapes)
{
	char const *str = start;
	if (*str == '\'' || *str == '\"') {
		char const quote = *str++;
		*num_escapes = 0;
		for (; *str && *str != quote; str = g_utf8_next_char (str))
			if (*str == '\\' && str[1]) {
				str++;
				(*num_escapes)++;
			}
		if (*str)
			return str+1;
	} else
		*num_escapes = -1;
	return start;
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

/**
 * wbref_parse :
 * @start :
 * @wb :
 *
 * Returns : NULL if there is a valid workbook name but it is unknown.
 *           If the string is a valid workbook known name it returns a pointer
 *           the end of the name.
 *           Otherwise returns @start and does not modify @wb.
 * **/
static char const *
wbref_parse (char const *start, Workbook **wb)
{
	/* Is this an external reference ? */
	if (*start == '[') {
		Workbook *tmp_wb;

		int num_escapes;
		char const *end = check_quoted (start+1, &num_escapes);
		char *name;

		if (end == start+1) {
			end = strchr (start, ']');
			if (end == NULL)
				return start;
		}
		if (*end != ']')
			return start;

		/* might be too big if quoted (remember leading [' */
		name = g_alloca (1 + end - start - 2);
		if (num_escapes < 0) {
			strncpy (name, start+1, end-start-1);
			name [end-start-1] = '\0';
		} else
			unquote (name, start+2, end-start-2);

		tmp_wb = application_workbook_get_by_name (name);
		if (tmp_wb == NULL)
			return NULL;
		*wb = tmp_wb;
		return end + 1;
	}

	return start;
}

/**
 * sheetref_parse :
 * @start :
 * @sheet :
 * @wb    :
 * @allow_3d :
 *
 * Returns : NULL if there is a valid sheet name but it is unknown.
 *           If the string is a valid sheet known name it returns a pointer
 *           the end of the name.
 *           Otherwise returns @start and does not modify @sheet.
 **/
char const *
sheetref_parse (char const *start, Sheet **sheet, Workbook const *wb,
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
	ptr = sheetref_parse (start_sheet, &res->a.sheet, wb, TRUE);
	if (ptr == NULL)
		return start; /* TODO error unknown sheet */
	if (ptr != start_sheet) {
		if (*ptr == ':') { /* 3d ref */
			ptr = sheetref_parse (ptr+1, &res->b.sheet, wb, FALSE);
			if (ptr == NULL)
				return start; /* TODO error unknown sheet */
		} else
			res->b.sheet = NULL;

		if (*ptr++ != '!')
			return start; /* TODO syntax error */
	} else
		res->b.sheet = NULL;

	tmp1 = col_parse (ptr, &res->a.col, &res->a.col_relative);
	if (tmp1 == NULL) { /* check for row only ref 2:3 */ 
		tmp1 = row_parse (ptr, &res->a.row, &res->a.row_relative);
		if (!tmp1 || *tmp1++ != ':') /* row only requires : even for singleton */
			return start;
		tmp2 = row_parse (tmp1, &res->b.row, &res->b.row_relative);
		if (!tmp2)
			return start;
		res->a.col_relative = res->b.col_relative = FALSE;
		res->a.col = 0; res->b.col = SHEET_MAX_COLS-1;
		if (res->a.row_relative)
			res->a.row -= pp->eval.row;
		if (res->b.row_relative)
			res->b.row -= pp->eval.row;
		return tmp2;
	}

	tmp2 = row_parse (tmp1, &res->a.row, &res->a.row_relative);
	if (!tmp2) { /* check for col only ref B:C */ 
		if (*tmp1++ != ':') /* col only requires : even for singleton */
			return start;
		tmp2 = col_parse (tmp1, &res->b.col, &res->b.col_relative);
		if (!tmp2)
			return start;
		res->a.row_relative = res->b.row_relative = FALSE;
		res->a.row = 0; res->b.row = SHEET_MAX_ROWS-1;
		if (res->a.col_relative)
			res->a.col -= pp->eval.col;
		if (res->b.col_relative)
			res->b.col -= pp->eval.col;
		return tmp2;
	}

	if (res->a.col_relative)
		res->a.col -= pp->eval.col;
	if (res->a.row_relative)
		res->a.row -= pp->eval.row;

	/* prepare as if its a singleton, in case we want to fall back */
	res->b.col = res->a.col;
	res->b.row = res->a.row;
	res->b.col_relative = res->a.col_relative;
	res->b.row_relative = res->a.row_relative;
	if (*tmp2 != ':')
		return tmp2;

	ptr = tmp2;
	tmp1 = col_parse (ptr+1, &res->b.col, &res->b.col_relative);
	if (!tmp1)
		return ptr;	/* strange, but valid singleton */
	tmp2 = row_parse (tmp1, &res->b.row, &res->b.row_relative);
	if (!tmp2)
		return ptr;	/* strange, but valid singleton */

	if (res->b.col_relative)
		res->b.col -= pp->eval.col;
	if (res->b.row_relative)
		res->b.row -= pp->eval.row;
	return tmp2;
}


char const *
gnm_1_0_rangeref_parse (RangeRef *res, char const *start, ParsePos const *pp)
{
	char const *ptr = start, *tmp1, *tmp2;
	Workbook *wb;

	g_return_val_if_fail (start != NULL, start);
	g_return_val_if_fail (pp != NULL, start);

	wb = pp->wb;
	ptr = wbref_parse (start, &wb);
	if (ptr == NULL)
		return start; /* TODO error unknown workbook */

	ptr = sheetref_parse (ptr, &res->a.sheet, wb, TRUE);
	if (ptr == NULL)
		return start; /* TODO error unknown sheet */
	if (*ptr == '!') ptr++;
	tmp1 = col_parse (ptr, &res->a.col, &res->a.col_relative);
	if (!tmp1)
		return start;
	tmp2 = row_parse (tmp1, &res->a.row, &res->a.row_relative);
	if (!tmp2)
		return start;
	if (res->a.col_relative)
		res->a.col -= pp->eval.col;
	if (res->a.row_relative)
		res->a.row -= pp->eval.row;

	/* prepare as if its a singleton, in case we want to fall back */
	res->b = res->a;
	if (*tmp2 != ':')
		return tmp2;

	start = tmp2;
	ptr = sheetref_parse (start+1, &res->b.sheet, wb, TRUE);
	if (ptr == NULL)
		return start; /* TODO error unknown sheet */
	if (*ptr == '!') ptr++;
	tmp1 = col_parse (ptr, &res->b.col, &res->b.col_relative);
	if (!tmp1)
		return start;
	tmp2 = row_parse (tmp1, &res->b.row, &res->b.row_relative);
	if (!tmp2)
		return start;
	if (res->b.col_relative)
		res->b.col -= pp->eval.col;
	if (res->b.row_relative)
		res->b.row -= pp->eval.row;
	return tmp2;
}

/* ------------------------------------------------------------------------- */

static void
def_expr_name_handler (GString *target,
		       const ParsePos *pp,
		       const GnmExprName *name,
		       const GnmExprConventions *conv)
{
	const GnmNamedExpr *thename = name->name;

	if (!thename->active) {
		g_string_append (target,
				 value_error_name (GNM_ERROR_REF, conv->output_translated));
		return;
	}

	if (name->optional_scope != NULL) {
		if (name->optional_scope->workbook != pp->wb) {
			g_string_append_c (target, '[');
			g_string_append (target, name->optional_wb_scope->filename);
			g_string_append_c (target, ']');
		} else {
			g_string_append (target, name->optional_scope->name_quoted);
			g_string_append (target, conv->output_sheet_name_sep);
		}
	} else if (pp->sheet != NULL &&
		   thename->pos.sheet != NULL &&
		   thename->pos.sheet != pp->sheet) {
		g_string_append (target, thename->pos.sheet->name_quoted);
		g_string_append (target, conv->output_sheet_name_sep);
	}

	g_string_append (target, thename->name->str);
}

/* ------------------------------------------------------------------------- */

GnmExprConventions *
gnm_expr_conventions_new (void)
{
	GnmExprConventions *res = g_new0 (GnmExprConventions, 1);

	res->expr_name_handler = def_expr_name_handler;
	res->cell_ref_handler = cellref_as_string;
	res->range_ref_handler = rangeref_as_string;
	res->output_sheet_name_sep = "!";
	res->output_translated = TRUE;
	return res;
}

void
gnm_expr_conventions_free (GnmExprConventions *c)
{
	if (c->function_rewriter_hash)
		g_hash_table_destroy (c->function_rewriter_hash);

	g_free (c);
}

/* ------------------------------------------------------------------------- */

#ifdef TEST
static void
test_col_stuff (void)
{
	int col;
	const char *end, *str;
	unsigned char col_relative;

	g_assert (strcmp ("A", col_name (0)) == 0);
	g_assert (strcmp ("AA", col_name (26)) == 0);
	g_assert (strcmp ("IV", col_name (255)) == 0);

	g_assert (strcmp ("A", cols_name (0, 0)) == 0);
	g_assert (strcmp ("A:IV", cols_name (0, 255)) == 0);

	end = col_parse ((str = "A"), &col, &col_relative);
	g_assert (end == str + strlen (str) && col == 0 && col_relative);
	end = col_parse ((str = "$A"), &col, &col_relative);
	g_assert (end == str + strlen (str) && col == 0 && !col_relative);
	end = col_parse ((str = "AA"), &col, &col_relative);
	g_assert (end == str + strlen (str) && col == 26 && col_relative);
	end = col_parse ((str = "$AA"), &col, &col_relative);
	g_assert (end == str + strlen (str) && col == 26 && !col_relative);
	end = col_parse ((str = "IV"), &col, &col_relative);
	g_assert (end == str + strlen (str) && col == 255 && col_relative);
	end = col_parse ((str = "$IV"), &col, &col_relative);
	g_assert (end == str + strlen (str) && col == 255 && !col_relative);
	end = col_parse ((str = "IW"), &col, &col_relative);
	g_assert (!end);
	end = col_parse ((str = ":IW"), &col, &col_relative);
	g_assert (!end);
	end = col_parse ((str = "$IW"), &col, &col_relative);
	g_assert (!end);
}

static void
test_row_stuff (void)
{
	int row;
	const char *end, *str;
	unsigned char row_relative;

	g_assert (strcmp ("1", row_name (0)) == 0);
	g_assert (strcmp ("42", row_name (41)) == 0);
	g_assert (strcmp ("65536", row_name (65535)) == 0);

	g_assert (strcmp ("1", rows_name (0, 0)) == 0);
	g_assert (strcmp ("1:65536", rows_name (0, 65535)) == 0);

	end = row_parse ((str = "1"), &row, &row_relative);
	g_assert (end == str + strlen (str) && row == 0 && row_relative);
	end = row_parse ((str = "$1"), &row, &row_relative);
	g_assert (end == str + strlen (str) && row == 0 && !row_relative);
	end = row_parse ((str = "42"), &row, &row_relative);
	g_assert (end == str + strlen (str) && row == 41 && row_relative);
	end = row_parse ((str = "$42"), &row, &row_relative);
	g_assert (end == str + strlen (str) && row == 41 && !row_relative);
	end = row_parse ((str = "65536"), &row, &row_relative);
	g_assert (end == str + strlen (str) && row == 65535 && row_relative);
	end = row_parse ((str = "$65536"), &row, &row_relative);
	g_assert (end == str + strlen (str) && row == 65535 && !row_relative);
	end = row_parse ((str = "0"), &row, &row_relative);
	g_assert (!end);
	end = row_parse ((str = "01"), &row, &row_relative);
	g_assert (!end);
	end = row_parse ((str = "$01"), &row, &row_relative);
	g_assert (!end);
	end = row_parse ((str = "$+1"), &row, &row_relative);
	g_assert (!end);
	end = row_parse ((str = "-1"), &row, &row_relative);
	g_assert (!end);
	end = row_parse ((str = "65537"), &row, &row_relative);
	g_assert (!end);
	end = row_parse ((str = "$65537"), &row, &row_relative);
	g_assert (!end);
}


static void
test_cellpos_stuff (void)
{
	CellPos cp;
	const char *end, *str;

	end = cellpos_parse ((str = "A1"), &cp, TRUE);
	g_assert (end == str + strlen (str) && cp.col == 0 && cp.row == 0);
	end = cellpos_parse ((str = "AA42"), &cp, TRUE);
	g_assert (end == str + strlen (str) && cp.col == 26 && cp.row == 41);

	end = cellpos_parse ((str = "A1"), &cp, FALSE);
	g_assert (end == str + strlen (str) && cp.col == 0 && cp.row == 0);
	end = cellpos_parse ((str = "AA42"), &cp, FALSE);
	g_assert (end == str + strlen (str) && cp.col == 26 && cp.row == 41);

	end = cellpos_parse ((str = "A1:"), &cp, TRUE);
	g_assert (end == NULL);
	end = cellpos_parse ((str = "AA42:"), &cp, TRUE);
	g_assert (end == NULL);

	end = cellpos_parse ((str = "A1:"), &cp, FALSE);
	g_assert (end == str + strlen (str) -1 && cp.col == 0 && cp.row == 0);
	end = cellpos_parse ((str = "AA42:"), &cp, FALSE);
	g_assert (end == str + strlen (str) - 1 && cp.col == 26 && cp.row == 41);
}

#endif

GnmExprConventions *gnm_expr_conventions_default;

void
parse_util_init (void)
{
#ifdef TEST
	test_row_stuff ();
	test_col_stuff ();
	test_cellpos_stuff ();
#endif

	gnm_expr_conventions_default = gnm_expr_conventions_new ();
	gnm_expr_conventions_default->ref_parser = rangeref_parse;
	gnm_expr_conventions_default->range_sep_colon = TRUE;
	gnm_expr_conventions_default->sheet_sep_exclamation = TRUE;
	gnm_expr_conventions_default->dots_in_names = TRUE;
}

void
parse_util_shutdown (void)
{
	gnm_expr_conventions_free (gnm_expr_conventions_default);
	gnm_expr_conventions_default = NULL;
}

GnmExpr const *
gnm_expr_parse_str_simple (char const *expr, ParsePos const *pp)
{
	return gnm_expr_parse_str (expr, pp, GNM_EXPR_PARSE_DEFAULT,
				   gnm_expr_conventions_default, NULL);
}

/* ------------------------------------------------------------------------- */
