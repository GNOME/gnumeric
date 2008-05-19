/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * parse-util.c: Various utility routines to parse or produce
 *     string representations of common reference types.
 *
 * Copyright (C) 2000-2007 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "parse-util.h"

#include "application.h"
#include "workbook.h"
#include "sheet.h"
#include "value.h"
#include "ranges.h"
#include "cell.h"
#include "expr.h"
#include "number-match.h"
#include "gnm-format.h"
#include "expr-name.h"
#include "str.h"
/* For std_expr_name_handler: */
#include "expr-impl.h"
#include "gutils.h"
#include <goffice/app/go-doc.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-file.h>

#include <errno.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>

static void
col_name_internal (GString *target, int col)
{
	static int const steps[] = {
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

	if (col < 0) {
		/* Invalid column.  */
		g_string_append_printf (target, "[C%d]", col);
		return;
	}

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
		buffer = g_string_new (NULL);
	g_string_truncate (buffer, 0);

	col_name_internal (buffer, col);

	return buffer->str;
}

char const *
cols_name (int start_col, int end_col)
{
	static GString *buffer = NULL;
	if (!buffer)
		buffer = g_string_new (NULL);
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

	for (ptr = start; col < gnm_sheet_get_max_cols (NULL) ; ptr++)
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
		buffer = g_string_new (NULL);
	g_string_truncate (buffer, 0);

	row_name_internal (buffer, row);

	return buffer->str;
}

char const *
rows_name (int start_row, int end_row)
{
	static GString *buffer = NULL;
	if (!buffer)
		buffer = g_string_new (NULL);
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

	/*
	 * Do not allow letters after the row number.  If we did, then
	 * the name "K3P" would lex as the reference K3 followed by the
	 * name "P".
	 */
	row = strtol (ptr, (char **)&end, 10);
	if (ptr != end &&
	    !g_unichar_isalnum (g_utf8_get_char (end)) && *end != '_' &&
	    0 < row && row <= gnm_sheet_get_max_rows (NULL)) {
		*res = row - 1;
		return end;
	} else
		return NULL;
}

/***************************************************************************/

inline static int
cellref_abs_col (GnmCellRef const *ref, GnmParsePos const *pp)
{
	int col = (ref->col_relative) ? pp->eval.col + ref->col : ref->col;

	/* ICK!  XL compatibility kludge */
	col %= gnm_sheet_get_max_cols (ref->sheet);
	if (col < 0)
		return col + gnm_sheet_get_max_cols (ref->sheet);
	return col;
}

inline static int
cellref_abs_row (GnmCellRef const *ref, GnmParsePos const *pp)
{
	int row = (ref->row_relative) ? pp->eval.row + ref->row : ref->row;

	/* ICK!  XL compatibility kludge */
	row %= gnm_sheet_get_max_rows (ref->sheet);
	if (row < 0)
		return row + gnm_sheet_get_max_rows (ref->sheet);
	return row;
}

static void
r1c1_add_index (GString *target, char type, int num, unsigned char relative)
{
	if (relative) {
		if (num != 0)
			g_string_append_printf (target, "%c[%d]", type, num);
		else
			g_string_append_c (target, type);
	} else
		g_string_append_printf (target, "%c%d", type, num + 1);
}

static char *
wb_rel_uri (Workbook *wb, Workbook *ref_wb)
{
	char const *uri = go_doc_get_uri ((GODoc *)wb);
	char const *ref_uri = go_doc_get_uri ((GODoc *)ref_wb);
	char *rel_uri = go_url_make_relative (uri, ref_uri);

	if (rel_uri == NULL || rel_uri[0] == '/') {
		g_free (rel_uri);
		return g_strdup (uri);
	}

	return rel_uri;
}

/**
 * cellref_as_string :
 * @out : #GnmConventionsOut
 * @ref :
 * @no_sheetname :
 *
 * Returns a string that the caller needs to free containing the A1 format
 * representation of @ref as evaluated at @pp.  @no_sheetname can be used to
 * suppress the addition of the sheetname for non-local references.
 **/
void
cellref_as_string (GnmConventionsOut *out,
		   GnmCellRef const *cell_ref,
		   gboolean no_sheetname)
{
	int col, row;
	GString *target = out->accum;
	Sheet const *sheet = cell_ref->sheet;

	/* If it is a non-local reference, add the path to the external sheet */
	if (sheet != NULL && !no_sheetname) {
		if (out->pp->wb == NULL && out->pp->sheet == NULL)
			/* For the expression leak printer.  */
			g_string_append (target, "'?'");
		else if (NULL == out->pp->wb || sheet->workbook == out->pp->wb)
			g_string_append (target, sheet->name_quoted);
		else {
			char *rel_uri = wb_rel_uri (sheet->workbook, out->pp->wb);
			g_string_append_c (target, '[');
			g_string_append (target, rel_uri);
			g_string_append_c (target, ']');
			g_string_append (target, sheet->name_quoted);
			g_free (rel_uri);
		}
		g_string_append_unichar (target, out->convs->sheet_name_sep);
	}

	if (out->convs->r1c1_addresses) { /* R1C1 handler */
		r1c1_add_index (target, 'R', cell_ref->row, cell_ref->row_relative);
		r1c1_add_index (target, 'C', cell_ref->col, cell_ref->col_relative);
	} else {
		if (cell_ref->col_relative)
			col = out->pp->eval.col + cell_ref->col;
		else {
			g_string_append_c (target, '$');
			col = cell_ref->col;
		}

		/* ICK!  XL compatibility kludge */
		col %= gnm_sheet_get_max_cols (sheet);
		if (col < 0)
			col += gnm_sheet_get_max_cols (sheet);
		col_name_internal (target, col);

		if (cell_ref->row_relative)
			row = out->pp->eval.row + cell_ref->row;
		else {
			g_string_append_c (target, '$');
			row = cell_ref->row;
		}

		/* ICK!  XL compatibility kludge */
		row %= gnm_sheet_get_max_rows (sheet);
		if (row < 0)
			row += gnm_sheet_get_max_rows (sheet);
		row_name_internal (target, row);
	}
}

/**
 * rangeref_as_string :
 * @out : #GnmConventionsOut
 * @ref : #GnmRangeRef
 *
 **/
void
rangeref_as_string (GnmConventionsOut *out, GnmRangeRef const *ref)
{
	GnmRange r;
	GString *target = out->accum;

	r.start.col = cellref_abs_col (&ref->a, out->pp);
	r.end.col   = cellref_abs_col (&ref->b, out->pp);
	r.start.row = cellref_abs_row (&ref->a, out->pp);
	r.end.row   = cellref_abs_row (&ref->b, out->pp);

	if (ref->a.sheet) {
		if (NULL != out->pp->wb && ref->a.sheet->workbook != out->pp->wb) {
			char *rel_uri = wb_rel_uri (ref->a.sheet->workbook, out->pp->wb);
			g_string_append_c (target, '[');
			g_string_append (target, rel_uri);
			g_string_append_c (target, ']');
			g_free (rel_uri);
		}
		if (out->pp->wb == NULL && out->pp->sheet == NULL)
			/* For the expression leak printer.  */
			g_string_append (target, "'?'");
		else if (ref->b.sheet == NULL || ref->a.sheet == ref->b.sheet)
			g_string_append (target, ref->a.sheet->name_quoted);
		else {
			g_string_append (target, ref->a.sheet->name_quoted);
			g_string_append_c (target, ':');
			g_string_append (target, ref->b.sheet->name_quoted);
		}
		g_string_append_unichar (target, out->convs->sheet_name_sep);
	}

	if (out->convs->r1c1_addresses) { /* R1C1 handler */
		/* be sure to use else if so that a1:iv65535 does not vanish */
		if (r.start.col == 0 && r.end.col == gnm_sheet_get_max_cols (ref->a.sheet)-1) {
			r1c1_add_index (target, 'R', ref->a.row, ref->a.row_relative);
			if (ref->a.row != ref->b.row ||
			    ref->a.row_relative != ref->b.row_relative) {
				g_string_append_c (target, ':');
				r1c1_add_index (target, 'R', ref->b.row, ref->b.row_relative);
			}
		} else if (r.start.row == 0 && r.end.row == gnm_sheet_get_max_rows (ref->a.sheet)-1) {
			r1c1_add_index (target, 'C', ref->a.col, ref->a.col_relative);
			if (ref->a.col != ref->b.col ||
			    ref->a.col_relative != ref->b.col_relative) {
				g_string_append_c (target, ':');
				r1c1_add_index (target, 'C', ref->b.col, ref->b.col_relative);
			}
		} else {
			r1c1_add_index (target, 'R', ref->a.row, ref->a.row_relative);
			r1c1_add_index (target, 'C', ref->a.col, ref->a.col_relative);
			if (r.start.col != r.end.col ||
			    ref->a.col_relative != ref->b.col_relative ||
			    r.start.row != r.end.row ||
			    ref->a.row_relative != ref->b.row_relative) {
				g_string_append_c (target, ':');
				r1c1_add_index (target, 'R', ref->b.row, ref->b.row_relative);
				r1c1_add_index (target, 'C', ref->b.col, ref->b.col_relative);
			}
		}
	} else {
		/* be sure to use else if so that a1:iv65535 does not vanish */
		if (r.start.col == 0 && r.end.col == gnm_sheet_get_max_cols (ref->a.sheet)-1) {
			if (!ref->a.row_relative)
				g_string_append_c (target, '$');
			row_name_internal (target, r.start.row);
			g_string_append_c (target, ':');
			if (!ref->b.row_relative)
				g_string_append_c (target, '$');
			row_name_internal (target, r.end.row);
		} else if (r.start.row == 0 && r.end.row == gnm_sheet_get_max_rows (ref->a.sheet)-1) {
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

			if (r.start.col != r.end.col ||
			    ref->a.col_relative != ref->b.col_relative ||
			    r.start.row != r.end.row ||
			    ref->a.row_relative != ref->b.row_relative) {
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
}

/**
 * gnm_1_0_rangeref_as_string :
 * @out : #GnmConventionsOut
 * @ref : #GnmRangeRef
 *
 * Simplified variant of rangeref_as_string that old versions of gnumeric can
 * read.  It drops support for full col/row references.  We can remap them on
 * import.
 *
 * This function also ignores R1C1 settings.
 **/
void
gnm_1_0_rangeref_as_string (GnmConventionsOut *out, GnmRangeRef const *ref)
{
	GnmRange r;
	GString *target = out->accum;

	r.start.col = cellref_abs_col (&ref->a, out->pp);
	r.end.col   = cellref_abs_col (&ref->b, out->pp);
	r.start.row = cellref_abs_row (&ref->a, out->pp);
	r.end.row   = cellref_abs_row (&ref->b, out->pp);

	if (ref->a.sheet) {
		if (NULL != out->pp->wb && ref->a.sheet->workbook != out->pp->wb) {
			char *rel_uri = wb_rel_uri (ref->a.sheet->workbook, out->pp->wb);
			g_string_append_c (target, '[');
			g_string_append (target, rel_uri);
			g_string_append_c (target, ']');
			g_free (rel_uri);
		}
		if (out->pp->wb == NULL && out->pp->sheet == NULL)
			/* For the expression leak printer. */
			g_string_append (target, "'?'");
		else if (ref->b.sheet == NULL || ref->a.sheet == ref->b.sheet)
			g_string_append (target, ref->a.sheet->name_quoted);
		else {
			g_string_append (target, ref->a.sheet->name_quoted);
			g_string_append_c (target, ':');
			g_string_append (target, ref->b.sheet->name_quoted);
		}
		g_string_append_unichar (target, out->convs->sheet_name_sep);
	}

	if (!ref->a.col_relative)
		g_string_append_c (target, '$');
	col_name_internal (target, r.start.col);
	if (!ref->a.row_relative)
		g_string_append_c (target, '$');
	row_name_internal (target, r.start.row);

	if (r.start.col != r.end.col ||
	    ref->a.col_relative != ref->b.col_relative ||
	    r.start.row != r.end.row ||
	    ref->a.row_relative != ref->b.row_relative) {
		g_string_append_c (target, ':');
		if (!ref->b.col_relative)
			g_string_append_c (target, '$');
		col_name_internal (target, r.end.col);
		if (!ref->b.row_relative)
			g_string_append_c (target, '$');
		row_name_internal (target, r.end.row);
	}
}

static char const *
cellref_a1_get (GnmCellRef *out, char const *in, GnmCellPos const *pos)
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

/* skip first character (which was R or C) */
static char const *
r1c1_get_index (char const *str, int *num, unsigned char *relative, gboolean is_col)
{
	char *end;
	if (str[0] == '\0')
		return NULL;

	str++;
	if ((*relative = (*str == '[')))
		str++;
	else if (*str == '-' || *str == '+') { /* handle RC-10 as RC followed by -10 */
		*relative = TRUE;
		*num = 0;
		return str;
	}

	errno = 0;
	*num = strtol (str, &end, 10);
	if (errno == ERANGE)
		return NULL;
	if (str == end) {
		if (*relative)
			return NULL;
		*relative = TRUE;
		*num = 0;
	} else if (*relative) {
		if (*end != ']')
			return NULL;
		return end + 1;
	} else {
#warning "We cannot have NULL here."
		if (*num <= 0 || *num > colrow_max (is_col, NULL))
			return NULL;
		(*num)--;
	}
	return end;
}

static char const *
cellref_r1c1_get (GnmCellRef *out, char const *in, GnmCellPos const *pos)
{
	out->sheet = NULL;
	if (*in != 'R' && *in != 'r')
		return NULL;
	if (NULL == (in = r1c1_get_index (in, &out->row, &out->row_relative, FALSE)))
		return NULL;
	if (*in != 'C' && *in != 'c')
		return NULL;
	if (NULL == (in = r1c1_get_index (in, &out->col, &out->col_relative, TRUE)))
		return NULL;
	if (g_ascii_isalpha (*in))
		return NULL;
	return in;
}

/**
 * cellref_parse:
 * @out: destination GnmCellRef
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
cellref_parse (GnmCellRef *out, char const *in, GnmCellPos const *pos)
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

static char const *
cell_coord_name2 (int col, int row, gboolean r1c1)
{
	static GString *buffer = NULL;
	if (buffer)
		g_string_truncate (buffer, 0);
	else
		buffer = g_string_new (NULL);

	if (r1c1) {
		r1c1_add_index (buffer, 'R', row, FALSE);
		r1c1_add_index (buffer, 'C', col, FALSE);
	} else {
		col_name_internal (buffer, col);
		row_name_internal (buffer, row);
	}

	return buffer->str;
}

char const *
cell_coord_name (int col, int row)
{
	return cell_coord_name2 (col, row, FALSE);
}

char const *
cellpos_as_string (GnmCellPos const *pos)
{
	g_return_val_if_fail (pos != NULL, "ERROR");

	return cell_coord_name (pos->col, pos->row);
}

char const *
parsepos_as_string (GnmParsePos const *pp)
{
	g_return_val_if_fail (pp != NULL, "ERROR");

	return cell_coord_name2 (pp->eval.col,
				 pp->eval.row,
				 pp->sheet && pp->sheet->convs->r1c1_addresses);
}

char const *
cell_name (GnmCell const *cell)
{
	g_return_val_if_fail (cell != NULL, "ERROR");

	return cell_coord_name2 (cell->pos.col,
				 cell->pos.row,
				 cell->base.sheet->convs->r1c1_addresses);
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
cellpos_parse (char const *cell_str, GnmCellPos *res, gboolean strict)
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
		(void) gnm_strto (c, &end);
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
 * @val : Returns a GnmValue* if the text was a value, otherwise NULL.
 * @texpr: Returns a GnmExprTop* if the text was an expression, otherwise NULL.
 * @cur_fmt : Optional, current number format.
 * @date_conv : Optional, date parse conventions
 *
 * If there is a parse failure for an expression an error GnmValue with the syntax
 * error is returned.
 */
void
parse_text_value_or_expr (GnmParsePos const *pos, char const *text,
			  GnmValue **val, GnmExprTop const **texpr,
			  GOFormat *cur_fmt,
			  GODateConventions const *date_conv)
{
	char const *expr_start;

	*texpr = NULL;

	/* Does it match any formats?  */
	*val = format_match (text, cur_fmt, date_conv);
	if (*val != NULL)
		return;

	/* If it does not match known formats, see if it is an expression */
	expr_start = gnm_expr_char_start_p (text);
	if (NULL != expr_start && *expr_start) {
		*texpr = gnm_expr_parse_str (expr_start, pos,
			GNM_EXPR_PARSE_DEFAULT, NULL, NULL);
		if (*texpr != NULL)
			return;
	}

	/* Fall back on string */
	*val = value_new_string (text);
}

GnmParseError *
parse_error_init (GnmParseError *pe)
{
	pe->err		= NULL;
	pe->begin_char	= 0;
	pe->end_char	= 0;

	return pe;
}

void
parse_error_free (GnmParseError *pe)
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
wbref_parse (char const *start, Workbook **wb, Workbook *ref_wb)
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

		tmp_wb = gnm_app_workbook_get_by_name
			(name,
			 ref_wb ? go_doc_get_uri ((GODoc *)ref_wb) : NULL);
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
	GString *sheet_name;
	char const *end;

	*sheet = NULL;
	if (*start == '\'' || *start == '"') {
		sheet_name = g_string_new (NULL);
		end = go_strunescape (sheet_name, start);
		if (end == NULL) {
			g_string_free (sheet_name, TRUE);
			return start;
		}
	} else {
		gboolean only_digits = TRUE;
		end = start;

		/*
		 * Valid: Normal!a1
		 * Valid: x.y!a1
		 * Invalid: .y!a1
		 *
		 * Some names starting with digits are actually valid, but
		 * unparse quoted. Things are quite tricky: most sheet names
		 * starting with a digit are ok, but not those starting with
		 * "[0-9]*\." or "[0-9]+[eE]".
		 *
		 * Valid: 42!a1
		 * Valid: 4x!a1
		 * Invalid: 1.!a1
		 * Invalid: 1e!a1
		 */

		while (1) {
			gunichar uc = g_utf8_get_char (end);
			if (g_unichar_isalpha (uc) || uc == '_') {
				if (only_digits && end != start &&
				    (uc == 'e' || uc == 'E')) {
					end = start;
					break;
				}
				only_digits = FALSE;
				end = g_utf8_next_char (end);
			} else if (g_unichar_isdigit (uc)) {
				end = g_utf8_next_char (end);
			} else if (uc == '.') {
				/* Valid, except after only digits.  */
				if (only_digits) {
					end = start;
					break;
				}
				end++;
			} else
				break;
		}

		if (*end != '!' && (!allow_3d || *end != ':'))
			return start;

		sheet_name = g_string_new_len (start, end - start);
	}

	*sheet = workbook_sheet_by_name (wb, sheet_name->str);
	if (*sheet == NULL)
		end = start;

	g_string_free (sheet_name, TRUE);
	return end;
}

static char const *
r1c1_rangeref_parse (GnmRangeRef *res, char const *ptr, GnmParsePos const *pp)
{
	char const *tmp;

	if (*ptr == 'R' || *ptr == 'r') {
		if (NULL == (ptr = r1c1_get_index (ptr, &res->a.row, &res->a.row_relative, FALSE)))
			return NULL;
		if (*ptr != 'C' && *ptr != 'c') {
			if (g_ascii_isalpha (*ptr))
				return NULL;
			/* full row R# */
			res->a.col_relative = FALSE;
			res->a.col = 0;
			res->b = res->a;
			res->b.col = gnm_sheet_get_max_cols (res->b.sheet) - 1;
			if (ptr[0] != ':' || (ptr[1] != 'R' && ptr[1] != 'r'))
				return ptr;
			if (NULL == (tmp = r1c1_get_index (ptr+1, &res->b.row, &res->b.row_relative, FALSE)))
				return ptr; /* fallback to just the initial R */
			return tmp;
		} else if (NULL == (ptr = r1c1_get_index (ptr, &res->a.col, &res->a.col_relative, TRUE)))
			return NULL;

		res->b = res->a;
		if (ptr[0] != ':' || (ptr[1] != 'R' && ptr[1] != 'r') ||
		    NULL == (tmp = r1c1_get_index (ptr+1, &res->b.row, &res->b.row_relative, FALSE)) ||
		    (*tmp != 'C' && *tmp != 'c') ||
		    NULL == (tmp = r1c1_get_index (tmp, &res->b.col, &res->b.col_relative, FALSE)))
			return ptr;
		return tmp;
	} else if (*ptr == 'C' || *ptr == 'c') {
		if (NULL == (ptr = r1c1_get_index (ptr, &res->a.col, &res->a.col_relative, TRUE)))
			return NULL;
		if (g_ascii_isalpha (*ptr))
			return NULL;
		 /* full col C[#] */
		res->a.row_relative = FALSE;
		res->a.row = 0;
		res->b = res->a;
		res->b.row = gnm_sheet_get_max_rows (res->b.sheet) - 1;
		if (ptr[0] != ':' || (ptr[1] != 'C' && ptr[1] != 'c'))
			return ptr;
		if (NULL == (tmp = r1c1_get_index (ptr, &res->b.col, &res->b.col_relative, TRUE)))
			return ptr; /* fallback to just the initial C */
		return tmp;
	}

	return NULL;
}

/**
 * rangeref_parse :
 * @res : where to store the result
 * @start : the start of the string to parse
 * @pos : the location to parse relative to
 *
 * Returns the a pointer to the first invalid character.
 * If the result != @start then @res is valid.
 **/
char const *
rangeref_parse (GnmRangeRef *res, char const *start, GnmParsePos const *pp,
		GnmConventions const *convs)
{
	char const *ptr = start, *start_sheet, *start_wb, *tmp1, *tmp2;
	Workbook *wb;
	Workbook *ref_wb;

	g_return_val_if_fail (start != NULL, start);
	g_return_val_if_fail (pp != NULL, start);

	wb = pp->wb;
	ref_wb = wb ? wb : pp->sheet->workbook;
	start_wb = start;
	start_sheet = wbref_parse (start, &wb, ref_wb);
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
	} else {
		if (start_sheet != start_wb)
			return start; /* Workbook, but no sheet.  */
		res->b.sheet = NULL;
	}

	if (convs->r1c1_addresses) { /* R1C1 handler */
		tmp1 = r1c1_rangeref_parse (res, ptr, pp);
		return (tmp1 != NULL) ? tmp1 : start;
	}

	tmp1 = col_parse (ptr, &res->a.col, &res->a.col_relative);
	if (tmp1 == NULL) { /* check for row only ref 2:3 */
		tmp1 = row_parse (ptr, &res->a.row, &res->a.row_relative);
		if (!tmp1 || *tmp1++ != ':') /* row only requires : even for singleton */
			return start;
		tmp2 = row_parse (tmp1, &res->b.row, &res->b.row_relative);
		if (!tmp2)
			return start;
		res->a.col_relative = res->b.col_relative = FALSE;
		res->a.col = 0; res->b.col = gnm_sheet_get_max_cols (res->b.sheet)-1;
		if (res->a.row_relative)
			res->a.row -= pp->eval.row;
		if (res->b.row_relative)
			res->b.row -= pp->eval.row;
		return tmp2;
	}

	tmp2 = row_parse (tmp1, &res->a.row, &res->a.row_relative);
	if (tmp2 == NULL) { /* check for col only ref B:C or R1C1 style */
		if (*tmp1++ != ':') /* col only requires : even for singleton */
			return start;
		tmp2 = col_parse (tmp1, &res->b.col, &res->b.col_relative);
		if (!tmp2)
			return start;
		res->a.row_relative = res->b.row_relative = FALSE;
		res->a.row = 0; res->b.row = gnm_sheet_get_max_rows (res->b.sheet)-1;
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

	/* prepare as if it's a singleton, in case we want to fall back */
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

#if 0
/* Do we even need this anymore ? */
char const *
gnm_1_0_rangeref_parse (GnmRangeRef *res, char const *start, GnmParsePos const *pp,
			G_GNUC_UNUSED GnmConventions const *convs)
{
	char const *ptr = start, *tmp1, *tmp2;
	Workbook *wb;

	g_return_val_if_fail (start != NULL, start);
	g_return_val_if_fail (pp != NULL, start);

	wb = pp->wb;
	ptr = wbref_parse (start, &wb, NULL);
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

	/* prepare as if it's a singleton, in case we want to fall back */
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
#endif

/* ------------------------------------------------------------------------- */

static void
std_expr_name_handler (GnmConventionsOut *out, GnmExprName const *name)
{
	GnmNamedExpr const *thename = name->name;
	GString *target = out->accum;

	if (!thename->active) {
		g_string_append (target,
			value_error_name (GNM_ERROR_REF, out->convs->output.translated));
		return;
	}

	if (name->optional_scope != NULL) {
		if (name->optional_scope->workbook != out->pp->wb) {
			char *rel_uri = wb_rel_uri (name->optional_wb_scope, out->pp->wb);
			g_string_append_c (target, '[');
			g_string_append (target, rel_uri);
			g_string_append_c (target, ']');
			g_free (rel_uri);
		} else {
			g_string_append (target, name->optional_scope->name_quoted);
			g_string_append_unichar (target, out->convs->sheet_name_sep);
		}
	} else if (out->pp->sheet != NULL &&
		   thename->pos.sheet != NULL &&
		   thename->pos.sheet != out->pp->sheet) {
		g_string_append (target, thename->pos.sheet->name_quoted);
		g_string_append_unichar (target, out->convs->sheet_name_sep);
	}

	g_string_append (target, thename->name->str);
}

static void
std_output_string (GnmConventionsOut *out, GnmString const *str)
{
	go_strescape (out->accum, str->str);
}

/* ------------------------------------------------------------------------- */

static GString *
std_sheet_name_quote (GnmConventions const *convs,
		      char const *str)
{
	gunichar uc = g_utf8_get_char (str);
	GString *res = g_string_sized_new (20);
	char const *p;
	int nletters;
	int ndigits;

	if (g_ascii_isalpha (uc)) {
		nletters = 1;
		ndigits = 0;
		p = str + 1;
	} else if (g_unichar_isalpha (uc) || uc == '_') {
		nletters = -1;
		ndigits = -1;
		p = g_utf8_next_char (str);
	} else
		goto quoted;

	/* FIXME: What about '?' and '\\'.  I cannot enter those.  */

	for (; *p; p = g_utf8_next_char (p)) {
		uc = g_utf8_get_char (p);

		if (g_ascii_isalpha (uc)) {
			if (ndigits == 0)
				nletters++;
		} else if (g_ascii_isdigit (uc)) {
			if (ndigits >= 0)
				ndigits++;
		} else if (uc == '.' || uc == '_' || g_unichar_isalpha (uc))
			nletters = ndigits = -1;
		else
			goto quoted;
	}

	if (ndigits > 0) {
		/*
		 * Excel also quotes things that look like cell references.
		 * Precisely, check for a match against
		 *    ([A-Za-z]+)0*([1-9][0-9]*)
		 * where $1 is a valid column name and $2 is a valid row
		 * number.  (The 0* is an Excel bug.)
		 */

		int col, row;
		unsigned char col_relative, row_relative;
		if (!col_parse (str, &col, &col_relative))
			goto unquoted;

		p = str + nletters;
		while (*p == '0')
			p++, ndigits--;
		if (!row_parse (p, &row, &row_relative))
			goto unquoted;

		goto quoted;
	}

 unquoted:
	g_string_append (res, str);
	return res;

 quoted:
	g_string_append_c (res, '\'');
	/* This is UTF-8 safe.  */
	for (; *str; str++) {
		gchar c = *str;
		if (c == '\'' || c == '\\')
			g_string_append_c (res, '\\');
		g_string_append_c (res, c);
	}
	g_string_append_c (res, '\'');

	return res;
}

static char const *
std_name_parser (char const *str,
		 G_GNUC_UNUSED GnmConventions const *convs)
{
	gunichar uc = g_utf8_get_char (str);

	if (!g_unichar_isalpha (uc) && uc != '_' && uc != '\\')
		return NULL;

	do {
		str = g_utf8_next_char (str);
		uc = g_utf8_get_char (str);
	} while (g_unichar_isalnum (uc) ||
		 uc == '_' ||
		 uc == '?' ||
		 uc == '\\' ||
		 uc == '.');

	return str;
}

static GnmExpr const *
std_func_map (GnmConventions const *convs, Workbook *scope,
	      char const *name, GnmExprList *args)
{
	GnmFunc *f;

	if (NULL == (f = gnm_func_lookup (name, scope)))
		f = gnm_func_add_placeholder (scope, name, "", TRUE);
	return gnm_expr_new_funcall (f, args);
}

/**
 * gnm_conventions_new_full :
 * @size :
 *
 * Construct a GnmConventions of @size.
 *
 * Returns a GnmConventions with default values.  Caller is responsible for
 * freeing the result.
 **/
GnmConventions *
gnm_conventions_new_full (unsigned size)
{
	GnmConventions *convs;

	g_return_val_if_fail (size >= sizeof (GnmConventions), NULL);

	convs = g_malloc0 (size);

	convs->sheet_name_sep		= '!';
	convs->intersection_char	= ' ';
	convs->input.range_ref		= rangeref_parse;
	convs->input.name		= std_name_parser;
	convs->input.func		= std_func_map;

	convs->output.translated	= TRUE;
	convs->output.string		= std_output_string;
	convs->output.name		= std_expr_name_handler;
	convs->output.cell_ref		= cellref_as_string;
	convs->output.range_ref		= rangeref_as_string;
	convs->output.quote_sheet_name	= std_sheet_name_quote;

	return convs;
}

/**
 * gnm_conventions_new :
 *
 * A convenience wrapper around gnm_conventions_new_full
 * that constructs a GnmConventions of std size.
 *
 * Returns a GnmConventions with default values.  Caller is responsible for
 * freeing the result.
 **/
GnmConventions *
gnm_conventions_new (void)
{
	return gnm_conventions_new_full (sizeof (GnmConventions));
}

/**
 * gnm_conventions_free :
 * @c : #GnmConventions
 *
 * Release a convention
 **/
void
gnm_conventions_free (GnmConventions *c)
{
	g_free (c);
}

/* ------------------------------------------------------------------------- */

#ifdef TEST
static void
test_col_stuff (void)
{
	int col;
	char const *end, *str;
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
	char const *end, *str;
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
	GnmCellPos cp;
	char const *end, *str;

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

GnmConventions const *gnm_conventions_default;
GnmConventions const *gnm_conventions_xls_r1c1;

void
parse_util_init (void)
{
	GnmConventions *convs;
#ifdef TEST
	test_row_stuff ();
	test_col_stuff ();
	test_cellpos_stuff ();
#endif

	convs = gnm_conventions_new ();
	convs->range_sep_colon		 = TRUE;
	convs->r1c1_addresses		 = FALSE;
	gnm_conventions_default	 = convs;

	convs = gnm_conventions_new ();
	convs->range_sep_colon		 = TRUE;
	convs->r1c1_addresses		 = TRUE;
	gnm_conventions_xls_r1c1	 = convs;
}

void
parse_util_shutdown (void)
{
	gnm_conventions_free ((GnmConventions *)gnm_conventions_default);
	gnm_conventions_default = NULL;
	gnm_conventions_free ((GnmConventions *)gnm_conventions_xls_r1c1);
	gnm_conventions_xls_r1c1 = NULL;
}

GnmExprTop const *
gnm_expr_parse_str_simple (char const *str, GnmParsePos const *pp)
{
	return gnm_expr_parse_str (str, pp, GNM_EXPR_PARSE_DEFAULT, NULL, NULL);
}

/* ------------------------------------------------------------------------- */
/**
 * gnm_expr_conv_quote:
 * @convs : #GnmConventions
 * @str   : string to quote
 *
 * Quotes @str according to the convention @convs if necessary.
 * or returns a literal copy of @str if no quoting was needed.
 *
 * Return value: caller is responsible for the resulting GString
 **/
GString *
gnm_expr_conv_quote (GnmConventions const *convs,
		     char const *str)
{
	g_return_val_if_fail (convs != NULL, NULL);
	g_return_val_if_fail (convs->output.quote_sheet_name != NULL, NULL);
	g_return_val_if_fail (str != NULL, NULL);
	g_return_val_if_fail (str[0] != 0, NULL);

	return convs->output.quote_sheet_name (convs, str);
}
