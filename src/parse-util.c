/*
 * parse-util.c: Various utility routines to parse or produce
 *     string representations of common reference types.
 *
 * Copyright (C) 2000-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2008-2009 Morten Welinder (terra@gnome.org)
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
#include <gnumeric.h>
#include <parse-util.h>

#include <application.h>
#include <workbook.h>
#include <sheet.h>
#include <value.h>
#include <ranges.h>
#include <cell.h>
#include <expr.h>
#include <number-match.h>
#include <gnm-format.h>
#include <expr-name.h>
#include <func.h>
#include <mstyle.h>
#include <sheet-style.h>
/* For std_expr_name_handler: */
#include <expr-impl.h>
#include <gutils.h>
#include <goffice/goffice.h>

#include <errno.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>

static GnmLexerItem *
gnm_lexer_item_copy (GnmLexerItem *li)
{
	return go_memdup (li, sizeof (*li));
}

GType
gnm_lexer_item_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmLexerItem",
			 (GBoxedCopyFunc)gnm_lexer_item_copy,
			 (GBoxedFreeFunc)g_free);
	}
	return t;
}

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

/**
 * col_name: (skip)
 * @col: column number
 *
 * Returns: (transfer none): A string representation of @col
 */
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

/**
 * cols_name: (skip)
 * @start_col: column number
 * @end_col: column number
 *
 * Returns: (transfer none): A string representation of the columns from
 * @start_col to @end_col.
 */
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

/**
 * col_parse: (skip)
 *
 * Returns: (transfer none):
 */
char const *
col_parse (char const *str, GnmSheetSize const *ss,
	   int *res, unsigned char *relative)
{
	char const *ptr, *start = str;
	int col = -1;
	int max = ss->max_cols;

	if (!(*relative = (*start != '$')))
		start++;

	for (ptr = start; col < max ; ptr++)
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

/**
 * row_name: (skip)
 * @row: row number
 *
 * Returns: (transfer none): A string representation of @row
 */
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

/**
 * rows_name: (skip)
 * @start_row: row number
 * @end_row: row number
 *
 * Returns: (transfer none): A string representation of the rows from
 * @start_row to @end_row.
 */
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

/**
 * row_parse: (skip)
 *
 * Returns: (transfer none):
 */
char const *
row_parse (char const *str, GnmSheetSize const *ss,
	   int *res, unsigned char *relative)
{
	char const *end, *ptr = str;
	long int row;
	int max = ss->max_rows;

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
	    0 < row && row <= max) {
		*res = row - 1;
		return end;
	} else
		return NULL;
}

/***************************************************************************/

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
 * cellref_as_string: (skip)
 * @out: #GnmConventionsOut
 * @cell_ref:
 * @no_sheetname: If %TRUE, suppress sheet name
 *
 * Emits a string containing representation of @ref as evaluated at @pp.
 * @no_sheetname can be used to suppress the addition of the sheetname
 * for non-local references.
 **/
void
cellref_as_string (GnmConventionsOut *out,
		   GnmCellRef const *cell_ref,
		   gboolean no_sheetname)
{
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
		GnmCellPos pos;
		Sheet const *size_sheet = eval_sheet (sheet, out->pp->sheet);
		GnmSheetSize const *ss =
			gnm_sheet_get_size2 (size_sheet, out->pp->wb);

		gnm_cellpos_init_cellref_ss (&pos, cell_ref, &out->pp->eval, ss);

		if (!cell_ref->col_relative)
			g_string_append_c (target, '$');
		col_name_internal (target, pos.col);

		if (!cell_ref->row_relative)
			g_string_append_c (target, '$');
		row_name_internal (target, pos.row);
	}
}

/**
 * rangeref_as_string: (skip)
 * @out: #GnmConventionsOut
 * @ref: #GnmRangeRef
 *
 **/
void
rangeref_as_string (GnmConventionsOut *out, GnmRangeRef const *ref)
{
	GnmRange r;
	GString *target = out->accum;
	Sheet *start_sheet, *end_sheet;
	GnmSheetSize const *end_ss;

	gnm_rangeref_normalize_pp (ref, out->pp, &start_sheet, &end_sheet, &r);

	end_ss = gnm_sheet_get_size2 (end_sheet, out->pp->wb);

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
		if (r.start.col == 0 && r.end.col == end_ss->max_cols - 1) {
			r1c1_add_index (target, 'R', ref->a.row, ref->a.row_relative);
			if (ref->a.row != ref->b.row ||
			    ref->a.row_relative != ref->b.row_relative) {
				g_string_append_c (target, ':');
				r1c1_add_index (target, 'R', ref->b.row, ref->b.row_relative);
			}
		} else if (r.start.row == 0 && r.end.row == end_ss->max_rows - 1) {
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
		if (r.start.col == 0 && r.end.col == end_ss->max_cols - 1) {
			if (!ref->a.row_relative)
				g_string_append_c (target, '$');
			row_name_internal (target, r.start.row);
			g_string_append_c (target, ':');
			if (!ref->b.row_relative)
				g_string_append_c (target, '$');
			row_name_internal (target, r.end.row);
		} else if (r.start.row == 0 && r.end.row == end_ss->max_rows - 1) {
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
 * gnm_1_0_rangeref_as_string: (skip)
 * @out: #GnmConventionsOut
 * @ref: #GnmRangeRef
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
	Sheet *start_sheet, *end_sheet;

	gnm_rangeref_normalize_pp (ref, out->pp, &start_sheet, &end_sheet, &r);

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
cellref_a1_get (GnmCellRef *out, GnmSheetSize const *ss,
		char const *in, GnmCellPos const *pos)
{
	int col;
	int row;

	g_return_val_if_fail (in != NULL, NULL);
	g_return_val_if_fail (out != NULL, NULL);

	in = col_parse (in, ss, &col, &out->col_relative);
	if (!in)
		return NULL;

	in = row_parse (in, ss, &row, &out->row_relative);
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
r1c1_get_index (char const *str, GnmSheetSize const *ss,
		int *num, unsigned char *relative, gboolean is_col)
{
	char *end;
	long l;
	int max = is_col ? ss->max_cols : ss->max_rows;

	if (str[0] == '\0')
		return NULL;

	str++;
	*relative = (*str == '[');
	if (*relative)
		str++;
	else if (*str == '-' || *str == '+') { /* handle RC-10 as RC followed by -10 */
		*relative = TRUE;
		*num = 0;
		return str;
	}

	errno = 0;
	*num = l = strtol (str, &end, 10);
	if (errno == ERANGE || l <= G_MININT || l > G_MAXINT) {
		/* Note: this includes G_MININT to avoid negation overflow.  */
		return NULL;
	}
	if (str == end) {
		if (*relative)
			return NULL;
		*relative = TRUE;
		*num = 0;
	} else if (*relative) {
		if (*end != ']')
			return NULL;
		*num = (*num > 0
			? *num % max
			: -(-*num % max));
		return end + 1;
	} else {
		if (*num <= 0 || *num > max)
			return NULL;
		(*num)--;
	}
	return end;
}

static char const *
cellref_r1c1_get (GnmCellRef *out, GnmSheetSize const *ss,
		  char const *in, GnmCellPos const *pos)
{
	out->sheet = NULL;
	if (*in != 'R' && *in != 'r')
		return NULL;
	if (NULL == (in = r1c1_get_index (in, ss,
					  &out->row, &out->row_relative,
					  FALSE)))
		return NULL;
	if (*in != 'C' && *in != 'c')
		return NULL;
	if (NULL == (in = r1c1_get_index (in, ss,
					  &out->col, &out->col_relative,
					  TRUE)))
		return NULL;
	if (g_ascii_isalpha (*in))
		return NULL;
	return in;
}

/**
 * cellref_parse: (skip)
 * @out: (out): destination GnmCellRef
 * @ss: size of the sheet where parsing is being done
 * @in: reference description text, no leading whitespace allowed.
 * @pos: position parsing is being done at
 *
 * Converts the string representation of a #GnmCellRef into
 * an internal representation.
 *
 * Returns: (transfer none): a pointer to the character following the
 * cellref.
 **/
char const *
cellref_parse (GnmCellRef *out, GnmSheetSize const *ss,
	       char const *in, GnmCellPos const *pos)
{
	char const *res;

	g_return_val_if_fail (in != NULL, NULL);
	g_return_val_if_fail (out != NULL, NULL);

	res = cellref_a1_get (out, ss, in, pos);
	if (res != NULL)
		return res;
	return cellref_r1c1_get (out, ss, in, pos);
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

/**
 * cell_coord_name: (skip)
 * @col: column number
 * @row: row number
 *
 * Returns: (transfer none): a string representation of the cell at (@col,@row)
 */
char const *
cell_coord_name (int col, int row)
{
	return cell_coord_name2 (col, row, FALSE);
}

/**
 * cellpos_as_string: (skip)
 * @pos: A #GnmCellPos
 *
 * Returns: (transfer none): a string representation of the cell at @pos
 */
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

/**
 * cell_name:
 * @cell: #GnmCell
 *
 * Returns: (transfer none): the name of @cell, like "B11"
 */
char const *
cell_name (GnmCell const *cell)
{
	g_return_val_if_fail (cell != NULL, "ERROR");

	return cell_coord_name2 (cell->pos.col,
				 cell->pos.row,
				 cell->base.sheet->convs->r1c1_addresses);
}

/**
 * cellpos_parse: (skip)
 * @cell_str:   a string representation of a cell name.
 * @ss:          #GnmSheetSize
 * @res:         result
 * @strict:      if this is %TRUE, then parsing stops at possible errors,
 *               otherwise an attempt is made to return cell names with
 *               trailing garbage.
 *
 * Returns: (transfer none): pointer to following char on success, %NULL on
 * failure.  (In the strict case, that would be a pointer to the \0 or %NULL.)
 */
char const *
cellpos_parse (char const *cell_str, GnmSheetSize const *ss,
	       GnmCellPos *res, gboolean strict)
{
	unsigned char dummy_relative;

	cell_str = col_parse (cell_str, ss, &res->col, &dummy_relative);
	if (!cell_str)
		return NULL;

	cell_str = row_parse (cell_str, ss, &res->row, &dummy_relative);
	if (!cell_str)
		return NULL;

	if (*cell_str != 0 && strict)
		return NULL;

	return cell_str;
}

/**
 * gnm_expr_char_start_p: (skip)
 * @c: string
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
	int N = 1;

	if (NULL == c)
		return NULL;

	c0 = *c;

	if (c0 == '=' || c0 == '@' || c0 == '+' || c0 == '-')
		while (c[N] == ' ')
			N++;

	if (c0 == '=' || c0 == '@' || (c0 == '+' && c[1] == 0))
		return c + N;

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
			return (c0 == '+') ? c + N : c;
		/* Otherwise, it's a number.  */
	}
	return NULL;
}

/**
 * parse_text_value_or_expr:
 * @pos: If the string looks like an expression parse it at this location.
 * @text: The text to be parsed.
 * @val: (out) (nullable): Returns a #GnmValue if the text was a value, otherwise %NULL.
 * @texpr: (out) (nullable): Returns a #GnmExprTop if the text was an expression, otherwise %NULL.
 *
 * Utility routine to parse a string and convert it into an expression or value.
 *
 * If there is a parse failure for an expression an error GnmValue with
 * the syntax error is returned in @val.
 */
void
parse_text_value_or_expr (GnmParsePos const *pos, char const *text,
			  GnmValue **val, GnmExprTop const **texpr)
{
	char const *expr_start;
	GODateConventions const *date_conv;
	GOFormat const *cur_fmt;
	GOFormat const *cell_fmt;
	GnmStyle const *cell_style;

	*texpr = NULL;
	*val = NULL;

	/* Determine context information.  */
	date_conv =
		pos->sheet
		? sheet_date_conv (pos->sheet)
		: (pos->wb
		   ? workbook_date_conv (pos->wb)
		   : NULL);
	cell_style = pos->sheet
		? sheet_style_get (pos->sheet, pos->eval.col, pos->eval.row)
		: NULL;
	cur_fmt = cell_fmt = cell_style ? gnm_style_get_format (cell_style) : NULL;
	if (cell_fmt && go_format_is_general (cell_fmt)) {
		GnmCell const *cell = pos->sheet
			? sheet_cell_get (pos->sheet, pos->eval.col, pos->eval.row)
			: NULL;
		if (cell && cell->value && VALUE_FMT (cell->value))
			cur_fmt = VALUE_FMT (cell->value);
	}

	/* Does it match any formats?  */
	*val = format_match (text, cur_fmt, date_conv);
	if (*val != NULL) {
		GOFormat const *val_fmt = VALUE_FMT (*val);
		/* Avoid value formats we don't need.  */
		if (val_fmt && go_format_eq (cell_fmt, val_fmt))
			value_set_fmt (*val, NULL);
		return;
	}

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

static GnmParseError *
gnm_parse_error_copy (GnmParseError *pe)
{
	GnmParseError *res = g_new (GnmParseError, 1);
	res->begin_char = pe->begin_char;
	res->end_char = pe->end_char;
	res->err = (pe->err)? g_error_copy (pe->err): NULL;
	return res;
}

GType
gnm_parse_error_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmParseError",
			 (GBoxedCopyFunc)gnm_parse_error_copy,
			 (GBoxedFreeFunc)parse_error_free);
	}
	return t;
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
			int l = g_utf8_skip [*(guchar *)(++src)];
			strncpy (dst, src, l);
			dst += l;
			src += l;
			n -= l;
		} else
			*dst++ = *src++;
	*dst = 0;
}

/**
 * wbref_parse:
 * @convs: #GnmConventions const
 * @start:
 * @wb:
 *
 * Returns: %NULL if there is a valid workbook name but it is unknown.
 *           If the string is a valid workbook known name it returns a pointer
 *           the end of the name.
 *           Otherwise returns @start and does not modify @wb.
 **/
static char const *
wbref_parse (GnmConventions const *convs,
	     char const *start, Workbook **wb, Workbook *ref_wb)
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

		if (num_escapes < 0)
			name = g_strndup (start + 1, end - start - 1);
		else {
			name = g_malloc (1 + end - start - 2);
			unquote (name, start+2, end-start-2);
		}

		tmp_wb = (*convs->input.external_wb) (convs, ref_wb, name);
		g_free (name);
		if (tmp_wb == NULL)
			return NULL;

		*wb = tmp_wb;
		return end + 1;
	}

	return start;
}

/**
 * sheetref_parse: (skip)
 * @convs: #GnmConventions
 * @start:
 * @sheet: (out)
 * @wb: A #Workbook
 * @allow_3d:
 *
 * Returns: (transfer none): %NULL if there is a valid sheet name but it
 * is unknown.  If the string is a valid sheet name it returns a pointer
 * the end of the name.  Otherwise returns @start and does not
 * modify @sheet.
 **/
static char const *
sheetref_parse (GnmConventions const *convs,
		char const *start, Sheet **sheet, Workbook const *wb,
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
	GnmSheetSize const *a_ss, *b_ss;
	Sheet const *a_sheet, *b_sheet;

	a_sheet = eval_sheet (res->a.sheet, pp->sheet);
	b_sheet = eval_sheet (res->b.sheet, a_sheet);

	a_ss = gnm_sheet_get_size2 (a_sheet, pp->wb);
	b_ss = gnm_sheet_get_size2 (b_sheet, pp->wb);

	if (*ptr == 'R' || *ptr == 'r') {
		ptr = r1c1_get_index (ptr, a_ss,
				      &res->a.row, &res->a.row_relative,
				      FALSE);
		if (!ptr)
			return NULL;
		if (*ptr != 'C' && *ptr != 'c') {
			if (g_ascii_isalpha (*ptr))
				return NULL;
			/* full row R# */
			res->a.col_relative = FALSE;
			res->a.col = 0;
			res->b = res->a;
			res->b.col = a_ss->max_cols - 1;
			if (ptr[0] != ':' || (ptr[1] != 'R' && ptr[1] != 'r'))
				return ptr;
			tmp = r1c1_get_index (ptr+1, a_ss,
					      &res->b.row, &res->b.row_relative,
					      FALSE);
			if (!tmp)
				return ptr; /* fallback to just the initial R */
			return tmp;
		} else {
			ptr = r1c1_get_index (ptr, a_ss,
					      &res->a.col, &res->a.col_relative,
					      TRUE);
			if (!ptr)
				return NULL;
		}

		res->b = res->a;
		if (ptr[0] != ':' || (ptr[1] != 'R' && ptr[1] != 'r') ||
		    NULL == (tmp = r1c1_get_index (ptr+1, b_ss,
						   &res->b.row, &res->b.row_relative, FALSE)) ||
		    (*tmp != 'C' && *tmp != 'c') ||
		    NULL == (tmp = r1c1_get_index (tmp, b_ss,
						   &res->b.col, &res->b.col_relative, FALSE)))
			return ptr;
		return tmp;
	} else if (*ptr == 'C' || *ptr == 'c') {
		if (NULL == (ptr = r1c1_get_index (ptr, a_ss,
						   &res->a.col, &res->a.col_relative, TRUE)))
			return NULL;
		if (g_ascii_isalpha (*ptr))
			return NULL;
		 /* full col C[#] */
		res->a.row_relative = FALSE;
		res->a.row = 0;
		res->b = res->a;
		res->b.row = b_ss->max_rows - 1;
		if (ptr[0] != ':' || (ptr[1] != 'C' && ptr[1] != 'c'))
			return ptr;
		tmp = r1c1_get_index (ptr, b_ss,
				      &res->b.col, &res->b.col_relative,
				      TRUE);
		if (!tmp)
			return ptr; /* fallback to just the initial C */
		return tmp;
	}

	return NULL;
}

/**
 * rangeref_parse: (skip)
 * @res: (out): #GnmRangeRef
 * @start: the start of the string to parse
 * @pp: the location to parse relative to
 * @convs: #GnmConventions
 *
 * Returns: (transfer none): a pointer to the first invalid character.
 * If the result != @start then @res is valid.
 **/
char const *
rangeref_parse (GnmRangeRef *res, char const *start, GnmParsePos const *pp,
		GnmConventions const *convs)
{
	char const *ptr = start, *start_sheet, *start_wb, *tmp1, *tmp2;
	Workbook *wb;
	Workbook *ref_wb;
	Sheet *a_sheet, *b_sheet;
	GnmSheetSize const *a_ss, *b_ss;

	g_return_val_if_fail (start != NULL, start);
	g_return_val_if_fail (pp != NULL, start);

	wb = pp->wb;
	ref_wb = wb ? wb : pp->sheet->workbook;
	start_wb = start;
	start_sheet = wbref_parse (convs, start, &wb, ref_wb);
	if (start_sheet == NULL)
		return start; /* TODO error unknown workbook */
	ptr = sheetref_parse (convs, start_sheet, &res->a.sheet, wb, TRUE);
	if (ptr == NULL)
		return start; /* TODO error unknown sheet */
	if (ptr != start_sheet) {
		const char *ref;

		if (*ptr == ':') { /* 3d ref */
			ptr = sheetref_parse (convs, ptr+1, &res->b.sheet, wb, FALSE);
			if (ptr == NULL)
				return start; /* TODO error unknown sheet */
		} else
			res->b.sheet = NULL;

		if (*ptr++ != '!')
			return start; /* TODO syntax error */

		ref = value_error_name (GNM_ERROR_REF, FALSE);
		if (g_str_has_prefix (ptr, ref)) {
			res->a.sheet = invalid_sheet;
			res->a.col = res->a.row = 0;
			res->a.col_relative = res->a.row_relative = FALSE;
			res->b.sheet = res->a.sheet;
			ptr += strlen (ref);
			return ptr;
		}
	} else {
		if (start_sheet != start_wb)
			return start; /* Workbook, but no sheet.  */
		res->b.sheet = NULL;
	}

	if (convs->r1c1_addresses) { /* R1C1 handler */
		const char *tmp1 = r1c1_rangeref_parse (res, ptr, pp);
		return (tmp1 != NULL) ? tmp1 : start;
	}

	a_sheet = eval_sheet (res->a.sheet, pp->sheet);
	b_sheet = eval_sheet (res->b.sheet, a_sheet);

	a_ss = gnm_sheet_get_size2 (a_sheet, pp->wb);
	b_ss = gnm_sheet_get_size2 (b_sheet, pp->wb);

	tmp1 = col_parse (ptr, a_ss, &res->a.col, &res->a.col_relative);
	if (tmp1 == NULL) { /* check for row only ref 2:3 */
		tmp1 = row_parse (ptr, a_ss,
				  &res->a.row, &res->a.row_relative);
		if (!tmp1 || *tmp1++ != ':') /* row only requires : even for singleton */
			return start;
		tmp2 = row_parse (tmp1, b_ss,
				  &res->b.row, &res->b.row_relative);
		if (!tmp2)
			return start;
		res->a.col_relative = res->b.col_relative = FALSE;
		res->a.col = 0;
		res->b.col = b_ss->max_cols - 1;
		if (res->a.row_relative)
			res->a.row -= pp->eval.row;
		if (res->b.row_relative)
			res->b.row -= pp->eval.row;
		return tmp2;
	}

	tmp2 = row_parse (tmp1, a_ss, &res->a.row, &res->a.row_relative);
	if (tmp2 == NULL) { /* check for col only ref B:C or R1C1 style */
		if (*tmp1++ != ':') /* col only requires : even for singleton */
			return start;
		tmp2 = col_parse (tmp1, a_ss,
				  &res->b.col, &res->b.col_relative);
		if (!tmp2)
			return start;
		res->a.row_relative = res->b.row_relative = FALSE;
		res->a.row = 0;
		res->b.row = b_ss->max_rows - 1;
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

	ptr = tmp2;
	if (*ptr != ':')
		goto singleton;

	tmp1 = col_parse (ptr+1, b_ss, &res->b.col, &res->b.col_relative);
	if (!tmp1)
		goto singleton;	/* strange, but valid singleton */
	tmp2 = row_parse (tmp1, b_ss, &res->b.row, &res->b.row_relative);
	if (!tmp2)
		goto singleton;	/* strange, but valid singleton */

	if (res->b.col_relative)
		res->b.col -= pp->eval.col;
	if (res->b.row_relative)
		res->b.row -= pp->eval.row;
	return tmp2;

 singleton:
	res->b.col = res->a.col;
	res->b.row = res->a.row;
	res->b.col_relative = res->a.col_relative;
	res->b.row_relative = res->a.row_relative;
	return ptr;
}

/* ------------------------------------------------------------------------- */

static void
std_expr_func_handler (GnmConventionsOut *out, GnmExprFunction const *func)
{
	char const *name = gnm_func_get_name (func->func,
					      out->convs->localized_function_names);
	GString *target = out->accum;

	g_string_append (target, name);
	/* FIXME: possibly a space here.  */
	gnm_expr_list_as_string (func->argc, func->argv, out);
}

static void
std_expr_name_handler (GnmConventionsOut *out, GnmExprName const *name)
{
	GnmNamedExpr const *thename = name->name;
	GString *target = out->accum;

	if (!expr_name_is_active (thename)) {
		g_string_append (target,
				 value_error_name (GNM_ERROR_REF,
						   out->convs->output.translated));
		return;
	}

	if (name->optional_scope != NULL) {
		Workbook *out_wb = out->pp->wb
			? out->pp->wb
			: out->pp->sheet->workbook;
		if (name->optional_scope->workbook != out_wb) {
			char *rel_uri = wb_rel_uri (name->optional_scope->workbook, out_wb);
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
	} else if (out->pp->sheet &&
		   thename->pos.sheet == NULL &&
		   expr_name_lookup (out->pp, expr_name_name (thename)) != thename) {
		/* Special syntax for global names shadowed by sheet names.  */
		g_string_append (target, "[]");
	}

	g_string_append (target, expr_name_name (thename));
}

static void
std_output_string (GnmConventionsOut *out, GOString const *str)
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
		static const GnmSheetSize max_size = {
			GNM_MAX_COLS, GNM_MAX_ROWS
		};
		/*
		 * Excel also quotes things that look like cell references.
		 * Precisely, check for a match against
		 *    ([A-Za-z]+)0*([1-9][0-9]*)
		 * where $1 is a valid column name and $2 is a valid row
		 * number.  (The 0* is an Excel bug.)
		 */

		int col, row;
		unsigned char col_relative, row_relative;
		if (!col_parse (str, &max_size, &col, &col_relative))
			goto unquoted;

		p = str + nletters;
		while (*p == '0')
			p++, ndigits--;
		if (!row_parse (p, &max_size, &row, &row_relative))
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
	GnmFunc *f = convs->localized_function_names
		? gnm_func_lookup_localized (name, scope)
		: gnm_func_lookup (name, scope);

	if (!f) {
		f = convs->localized_function_names
			? gnm_func_add_placeholder_localized (NULL, name)
			: gnm_func_add_placeholder_localized (name, NULL);
	}

	return gnm_expr_new_funcall (f, args);
}

static Workbook *
std_external_wb (G_GNUC_UNUSED GnmConventions const *convs,
		 Workbook *ref_wb,
		 const char *wb_name)
{
	const char *ref_uri = ref_wb ? go_doc_get_uri ((GODoc *)ref_wb) : NULL;
	return gnm_app_workbook_get_by_name (wb_name, ref_uri);
}

static char const *
std_string_parser (char const *in, GString *target,
		   G_GNUC_UNUSED GnmConventions const *convs)
{
	return go_strunescape (target, in);
}

/**
 * gnm_conventions_new_full:
 * @size:
 *
 * Construct a GnmConventions of @size.
 *
 * Returns: (transfer full): A #GnmConventions with default values.
 **/
GnmConventions *
gnm_conventions_new_full (unsigned size)
{
	GnmConventions *convs;

	g_return_val_if_fail (size >= sizeof (GnmConventions), NULL);

	convs = g_malloc0 (size);
	convs->ref_count = 1;

	convs->r1c1_addresses           = FALSE;
	convs->localized_function_names = FALSE;

	convs->sheet_name_sep		= '!';
	convs->intersection_char	= ' ';
	convs->exp_is_left_associative  = FALSE;
	convs->input.range_ref		= rangeref_parse;
	convs->input.string		= std_string_parser;
	convs->input.name		= std_name_parser;
	convs->input.name_validate     	= expr_name_validate;
	convs->input.func		= std_func_map;
	convs->input.external_wb	= std_external_wb;

	convs->output.decimal_digits	= -1;
	convs->output.uppercase_E       = TRUE;
	convs->output.translated	= TRUE;
	convs->output.string		= std_output_string;
	convs->output.name		= std_expr_name_handler;
	convs->output.func              = std_expr_func_handler;
	convs->output.cell_ref		= cellref_as_string;
	convs->output.range_ref		= rangeref_as_string;
	convs->output.boolean		= NULL;
	convs->output.quote_sheet_name	= std_sheet_name_quote;

	return convs;
}

/**
 * gnm_conventions_new:
 *
 * A convenience wrapper around gnm_conventions_new_full
 * that constructs a GnmConventions of std size.
 *
 * Returns: (transfer full): A #GnmConventions with default values.
 **/
GnmConventions *
gnm_conventions_new (void)
{
	return gnm_conventions_new_full (sizeof (GnmConventions));
}

/**
 * gnm_conventions_unref: (skip)
 * @c: (transfer full): #GnmConventions
 *
 * Release a reference to a #GnmConvention
 **/
void
gnm_conventions_unref (GnmConventions *c)
{
	if (c == NULL)
		return;

	g_return_if_fail (c->ref_count > 0);

	c->ref_count--;
	if (c->ref_count > 0)
		return;

	g_free (c);
}

/**
 * gnm_conventions_ref: (skip)
 * @c: (transfer none) (nullable): #GnmConventions
 *
 * Returns: (transfer full) (nullable): a new reference to @c
 **/
GnmConventions *
gnm_conventions_ref (GnmConventions const *c)
{
	GnmConventions *uc = (GnmConventions *)c;
	if (uc)
		uc->ref_count++;
	return uc;
}

GType
gnm_conventions_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmConventions",
			 (GBoxedCopyFunc)gnm_conventions_ref,
			 (GBoxedFreeFunc)gnm_conventions_unref);
	}
	return t;
}

/* ------------------------------------------------------------------------- */

GnmConventions const *gnm_conventions_default;
GnmConventions const *gnm_conventions_xls_r1c1;

void
parse_util_init (void)
{
	GnmConventions *convs;

	convs = gnm_conventions_new ();
	convs->range_sep_colon		 = TRUE;
	convs->r1c1_addresses		 = FALSE;
	/* Not ready for general use yet.  */
	convs->localized_function_names = g_getenv ("GNM_LOCAL_FUNCS") != NULL;
	gnm_conventions_default	 = convs;

	convs = gnm_conventions_new ();
	convs->range_sep_colon		 = TRUE;
	convs->r1c1_addresses		 = TRUE;
	convs->localized_function_names = gnm_conventions_default->localized_function_names;
	gnm_conventions_xls_r1c1	 = convs;
}

void
parse_util_shutdown (void)
{
	gnm_conventions_unref ((GnmConventions *)gnm_conventions_default);
	gnm_conventions_default = NULL;
	gnm_conventions_unref ((GnmConventions *)gnm_conventions_xls_r1c1);
	gnm_conventions_xls_r1c1 = NULL;
}

/* ------------------------------------------------------------------------- */
/**
 * gnm_expr_conv_quote:
 * @convs: #GnmConventions
 * @str: string to quote
 *
 * Returns: (transfer full): A quoted string according to @convs.  If no
 * quoting is necessary, a literal copy of @str will be returned.
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
