/*
 * dao.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2001, 2002 by Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <tools/dao.h>

#include <expr.h>
#include <value.h>
#include <cell.h>
#include <sheet.h>
#include <ranges.h>
#include <style.h>
#include <sheet-style.h>
#include <workbook.h>
#include <workbook-view.h>
#include <workbook-control.h>
#include <command-context.h>
#include <gnm-format.h>
#include <sheet-merge.h>
#include <sheet-object-cell-comment.h>
#include <style-color.h>
#include <style-border.h>
#include <graph.h>
#include <goffice/goffice.h>

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include <time.h>
#include <parse-util.h>

/**
 * dao_init: (skip)
 * @type: #data_analysis_output_type_t
 *
 * Initialize dao to given type.
 *
 * Returns: (transfer full): the initialized #data_analysis_output_t.
 **/
data_analysis_output_t *
dao_init (data_analysis_output_type_t type)
{
	data_analysis_output_t *dao = g_new (data_analysis_output_t, 1);

	dao->type              = type;
	dao->ref_sheet         = NULL;

	dao->start_col         = 0;
	dao->start_row         = 0;
	dao->offset_col        = 0;
	dao->offset_row        = 0;
	dao->cols              = 1;  /* Fixed in dao_prepare_output */
	dao->rows              = 1;
	dao->dst_sheet         = NULL;
	dao->autofit_flag      = TRUE;
	dao->autofit_noshrink  = TRUE;
	dao->clear_outputrange = TRUE;
	dao->retain_format     = FALSE;
	dao->retain_comments   = FALSE;
	dao->put_formulas      = FALSE;
	dao->sos               = NULL;
        dao->omit_so           = FALSE;

	return dao;
}

/**
 * dao_init_new_sheet: (skip)
 * @ref_sheet: (transfer none): reference sheet
 *
 * Creates a dao targeting a new sheet.  The given @ref_sheet determines
 * what workbook it will be placed in as well as the size of the sheet.
 *
 * Returns: (transfer full): the initialized #data_analysis_output_t.
 **/
data_analysis_output_t *
dao_init_new_sheet (Sheet *ref_sheet)
{
	data_analysis_output_t *res = dao_init (GNM_DAO_OUTPUT_NEWSHEET);
	res->ref_sheet = ref_sheet;
	return res;
}

/**
 * dao_free:
 * @dao: #data_analysis_output_t
 *
 * Frees the #data_analysis_output_t structure.
 **/
void dao_free (data_analysis_output_t *dao)
{
	g_slist_free_full (dao->sos, g_object_unref);
	dao->sos = NULL;

	g_free (dao);
}

/**
 * dao_load_from_value: (skip)
 * @dao:
 * @output_range:
 *
 **/
void
dao_load_from_value (data_analysis_output_t *dao,
		     GnmValue const *output_range)
{
	g_return_if_fail (output_range != NULL);
	g_return_if_fail (VALUE_IS_CELLRANGE (output_range));

	dao->start_col = output_range->v_range.cell.a.col;
	dao->start_row = output_range->v_range.cell.a.row;
	dao->cols = output_range->v_range.cell.b.col
		- output_range->v_range.cell.a.col + 1;
	dao->rows = output_range->v_range.cell.b.row
		- output_range->v_range.cell.a.row + 1;
	dao->dst_sheet = output_range->v_range.cell.a.sheet;
}

/**
 * dao_range_name:
 * @dao:
 *
 * Provides the name of the output range
 *
 * Returns: (transfer full): the name of the output range
 **/

static char *
dao_range_name (data_analysis_output_t *dao)
{
	GnmRange range;
	range_init (&range, dao->start_col, dao->start_row,
		    dao->start_col + dao->cols - 1,
		    dao->start_row + dao->rows - 1);

	return undo_range_name (dao->dst_sheet, &range);
}

/**
 * dao_command_descriptor:
 * @dao: #data_analysis_output_t
 * @format: printf-style format string
 *
 * Uses format to provide a string to be used as command descriptor for
 * undo/redo
 *
 * Returns: (transfer full): the command descriptor string
 **/
char *
dao_command_descriptor (data_analysis_output_t *dao, char const *format)
{
	char *rangename = NULL;
	char *result;

	switch (dao->type) {
	case GNM_DAO_OUTPUT_NEWSHEET:
		result = g_strdup_printf (format, _("New Sheet"));
		break;
	case GNM_DAO_OUTPUT_NEWWORKBOOK:
		result = g_strdup_printf (format, _("New Workbook"));
		break;
	case GNM_DAO_OUTPUT_RANGE:
	default:
		rangename = dao_range_name (dao);
		result = g_strdup_printf (format, rangename);
		g_free (rangename);
		break;
	}

	return result;
}

/**
 * dao_adjust:
 * @dao:
 * @cols:
 * @rows:
 *
 * shrinks the dao to the given cols/rows
 * (or enlarges it if dao was a singleton)
 *
 **/
void
dao_adjust (data_analysis_output_t *dao, gint cols, gint rows)
{
	int max_rows, max_cols;

	if (dao->cols == 1 && dao->rows == 1) {
		if (cols != -1)
			dao->cols = cols;
		if (rows != -1)
			dao->rows = rows;
	} else {
		if (cols != -1)
			dao->cols = MIN (cols, dao->cols);
		if (rows != -1)
			dao->rows = MIN (rows, dao->rows);
	}

	/* In case of GNM_DAO_OUTPUT_NEWSHEET and GNM_DAO_OUTPUT_NEWWORKBOOK */
	/* this is called before we actually create the    */
	/* new sheet and/or workbook                       */
	GnmSheetSize const *ss = dao_get_sheet_size (dao);
	max_rows = ss->max_rows - dao->start_row;
	max_cols = ss->max_cols - dao->start_col;

	if (dao->cols > max_cols)
		dao->cols = max_cols;
	if (dao->rows > max_rows)
		dao->rows = max_rows;
}

/**
 * dao_prepare_output:
 * @wbc: #WorkbookControl
 * @dao: #data_analysis_output_t
 * @name: name
 *
 * Prepares the output by creating a new sheet or workbook as appropriate
 **/
void
dao_prepare_output (WorkbookControl *wbc, data_analysis_output_t *dao,
		    const char *name)
{
	char *unique_name;

	if (dao->type == GNM_DAO_OUTPUT_NEWSHEET) {
		GnmSheetSize const *ss = gnm_sheet_get_size (dao->ref_sheet);
		Workbook *wb = dao->ref_sheet->workbook;
		char *name_with_counter = g_strdup_printf ("%s (1)", name);
		unique_name = workbook_sheet_get_free_name
			(wb, name_with_counter, FALSE, TRUE);
		g_free (name_with_counter);
		dao->rows = ss->max_rows;
		dao->cols = ss->max_cols;
	        dao->dst_sheet = sheet_new (wb, unique_name, ss->max_cols, ss->max_rows);
		g_free (unique_name);
		dao->start_col = dao->start_row = 0;
		workbook_sheet_attach (wb, dao->dst_sheet);
	} else if (dao->type == GNM_DAO_OUTPUT_NEWWORKBOOK) {
		GnmSheetSize const *ss = dao_get_sheet_size (dao);
		Workbook *wb = workbook_new ();
		dao->rows = ss->max_rows;
		dao->cols = ss->max_cols;
		dao->dst_sheet = sheet_new (wb, name, ss->max_cols, ss->max_rows);
		dao->start_col = dao->start_row = 0;
		workbook_sheet_attach (wb, dao->dst_sheet);
		wbc = workbook_control_new_wrapper (wbc, NULL, wb, NULL);
	}

	if (wbc)
		wb_view_sheet_focus (wb_control_view (wbc), dao->dst_sheet);

	if (dao->rows == 0 || (dao->rows == 1 && dao->cols == 1))
		dao->rows = gnm_sheet_get_max_rows (dao->dst_sheet) - dao->start_row;
	if (dao->cols == 0 || (dao->rows == 1 && dao->cols == 1))
		dao->cols = gnm_sheet_get_max_cols (dao->dst_sheet) - dao->start_col;
	dao->offset_col = 0;
	dao->offset_row = 0;
}

/**
 * dao_format_output:
 * @wbc: control
 * @dao:
 * @cmd:
 *
 * Formats the output range according to the settings
 *
 * Returns: %TRUE in case of error.
 **/
gboolean
dao_format_output (WorkbookControl *wbc, data_analysis_output_t *dao, char const *cmd)
{
	int clear_flags = 0;
	GnmRange range;

	range_init (&range, dao->start_col, dao->start_row,
		    dao->start_col + dao->cols - 1,
		    dao->start_row + dao->rows - 1);

	if (dao->type == GNM_DAO_OUTPUT_RANGE
	    && sheet_range_splits_region (dao->dst_sheet, &range, NULL,
					  GO_CMD_CONTEXT (wbc), cmd))
		return TRUE;

	if (dao->clear_outputrange)
		clear_flags = CLEAR_VALUES | CLEAR_RECALC_DEPS;
	if (!dao->retain_format)
		clear_flags |= CLEAR_FORMATS;
	if (!dao->retain_comments)
		clear_flags |= CLEAR_COMMENTS;

	sheet_clear_region (dao->dst_sheet,
			    range.start.col, range.start.row,
			    range.end.col, range.end.row,
			    clear_flags | CLEAR_NOCHECKARRAY | CLEAR_MERGES,
			    GO_CMD_CONTEXT (wbc));
	return FALSE;
}


static gboolean
adjust_range (data_analysis_output_t *dao, GnmRange *r)
{
	range_normalize (r);

	r->start.col += dao->offset_col + dao->start_col;
	r->end.col   += dao->offset_col + dao->start_col;
	r->start.row += dao->offset_row + dao->start_row;
	r->end.row   += dao->offset_row + dao->start_row;

	if (dao->type == GNM_DAO_OUTPUT_RANGE && (dao->cols > 1 || dao->rows > 1)) {
		if (r->end.col >= dao->start_col + dao->cols)
			r->end.col = dao->start_col + dao->cols - 1;
		if (r->end.row >= dao->start_row + dao->rows)
			r->end.row = dao->start_row + dao->rows - 1;
	}

	range_ensure_sanity (r, dao->dst_sheet);

	return ((r->start.col <= r->end.col) && (r->start.row <= r->end.row));

}

/**
 * dao_cell_is_visible:
 * @dao: #data_analysis_output_t
 * @col: column
 * @row: row
 *
 * Returns: %TRUE if the cell at (@col, @row) is within the output range.
 **/
gboolean
dao_cell_is_visible (data_analysis_output_t *dao, int col, int row)
{
	col += dao->offset_col;
	row += dao->offset_row;

	if (dao->type == GNM_DAO_OUTPUT_RANGE &&
	    (dao->cols > 1 || dao->rows > 1) &&
	    (col >= dao->cols || row >= dao->rows))
	        return FALSE;

	col += dao->start_col;
	row += dao->start_row;

	return !(col >= gnm_sheet_get_max_cols (dao->dst_sheet) || row >= gnm_sheet_get_max_rows (dao->dst_sheet));
}


/**
 * dao_set_array_expr: (skip)
 * @dao:
 * @col: starting column
 * @row: starting row
 * @cols: number of columns
 * @rows: number of rows
 * @expr: (transfer full): expression to set
 *
 */
void
dao_set_array_expr (data_analysis_output_t *dao,
		    int col, int row, int cols, int rows,
		    GnmExpr const *expr)
{
	GnmExprTop const *texpr;
	GnmRange r;

	range_init (&r, col, row, col + cols - 1, row + rows -1);

	if (!adjust_range (dao, &r)) {
		gnm_expr_free (expr);
		return;
	}

	texpr = gnm_expr_top_new (expr);
	gnm_cell_set_array_formula (dao->dst_sheet,
				    r.start.col, r.start.row,
				    r.end.col, r.end.row,
				    texpr);
}

/**
 * dao_set_cell_array_expr: (skip)
 * @dao:
 * @col: column
 * @row: row
 * @expr: (transfer full): expression to set
 *
 * Sets a singleton array expression.
 */
void
dao_set_cell_array_expr (data_analysis_output_t *dao, int col, int row,
			 GnmExpr const *expr)
{
	dao_set_array_expr (dao, col, row, 1, 1, expr);
}

/**
 * dao_set_cell_expr: (skip)
 * @dao:
 * @col: column
 * @row: row
 * @expr: (transfer full): expression to set
 *
 * Sets a singleton array expression.
 */
void
dao_set_cell_expr (data_analysis_output_t *dao, int col, int row,
		   GnmExpr const *expr)
{
        GnmCell *cell;
	GnmExprTop const *texpr;
	GnmRange r;

	range_init (&r, col, row, col, row);

	if (!adjust_range (dao, &r)) {
		gnm_expr_free (expr);
	        return;
	}

	cell = sheet_cell_fetch (dao->dst_sheet, r.start.col, r.start.row);
	texpr = gnm_expr_top_new (expr);
	gnm_cell_set_expr (cell, texpr);
	gnm_expr_top_unref (texpr);
}


/**
 * dao_set_cell_value:
 * @dao:
 * @col: column
 * @row: row
 * @v: (transfer full):
 *
 * Set cell to a value
 *
 * Note: the rows/cols specification for all dao_set_cell_...
 *       commands are relative to the location of the output
 **/
void
dao_set_cell_value (data_analysis_output_t *dao, int col, int row, GnmValue *v)
{
        GnmCell *cell;
	GnmRange r;

	range_init (&r, col, row, col, row);

	if (!adjust_range (dao, &r)) {
		value_release (v);
	        return;
	}

	cell = sheet_cell_fetch (dao->dst_sheet, r.start.col, r.start.row);

	sheet_cell_set_value (cell, v);
}

/**
 * dao_set_cell:
 * @dao:
 * @col: column
 * @row: row
 * @text: (nullable):
 *
 * Set cell to a string
 **/
void
dao_set_cell (data_analysis_output_t *dao, int col, int row, const char *text)
{
	if (text == NULL) {
		/* FIXME: should we erase instead?  */
		dao_set_cell_value (dao, col, row, value_new_empty ());
	} else {
		dao_set_cell_value (dao, col, row, value_new_string (text));
	}
}


/**
 * dao_set_cell_printf:
 * @dao: #data_analysis_output_t
 * @col: column
 * @row: row
 * @fmt: printf-style format
 * @...: arguments for format
 *
 * Format string and set cell.
 **/
void
dao_set_cell_printf (data_analysis_output_t *dao, int col, int row,
		     const char *fmt, ...)
{
	char *buffer;
	va_list args;

	va_start (args, fmt);
	buffer = g_strdup_vprintf (fmt, args);
	va_end (args);

	dao_set_cell_value (dao, col, row, value_new_string (buffer));
	g_free (buffer);
}


/**
 * set_cell_float:
 * @dao:
 * @col: column
 * @row: row
 * @v:
 *
 * set cell to a gnm_float
 **/
void
dao_set_cell_float (data_analysis_output_t *dao, int col, int row, gnm_float v)
{
	dao_set_cell_value (dao, col, row, value_new_float (v));
}


/**
 * set_cell_int:
 * @dao:
 * @col: column
 * @row: row
 * @v:
 *
 * set cell to an int
 **/
void
dao_set_cell_int (data_analysis_output_t *dao, int col, int row, int v)
{
	dao_set_cell_value (dao, col, row, value_new_int (v));
}


/**
 * dao_set_cell_na:
 * @dao:
 * @col: column
 * @row: row
 *
 * set cell to NA
 **/
void
dao_set_cell_na (data_analysis_output_t *dao, int col, int row)
{
	dao_set_cell_value (dao, col, row, value_new_error_NA (NULL));
}

/**
 * dao_set_cell_float_na:
 * @dao:
 * @col: column
 * @row: row
 * @v:
 * @is_valid:
 *
 * set cell to a gnm_float or NA as appropriate
 **/
void
dao_set_cell_float_na (data_analysis_output_t *dao, int col, int row,
		       gnm_float v, gboolean is_valid)
{
	if (is_valid) {
		dao_set_cell_float (dao, col, row, v);
	} else {
		dao_set_cell_na (dao, col, row);
	}
}

/**
 * dao_set_cell_comment:
 * @dao:
 * @col: column
 * @row: row
 * @comment: comment text
 *
 * Set a cell comment
 **/
void
dao_set_cell_comment (data_analysis_output_t *dao, int col, int row,
		      const char *comment)
{
	char const *author = NULL;
	GnmRange r;

	range_init (&r, col, row, col, row);

	if (adjust_range (dao, &r))
		cell_set_comment (dao->dst_sheet, &r.start, author, comment, NULL);
}


/**
 * dao_autofit_these_columns:
 * @dao:
 * @from_col:
 * @to_col:
 *
 * Fits the specified columns to their content
 **/
void
dao_autofit_these_columns (data_analysis_output_t *dao, int from_col, int to_col)
{
	GnmRange r;

	if (!dao->autofit_flag)
		return;

	range_init_cols (&r, dao->dst_sheet,
			 from_col + dao->start_col,
			 to_col + dao->start_col);

	colrow_autofit (dao->dst_sheet, &r, TRUE,
			FALSE, dao->autofit_noshrink, FALSE,
			NULL, NULL,
			TRUE);
}

/**
 * dao_autofit_columns:
 * @dao:
 *
 * fits all columns to their content
 **/
void
dao_autofit_columns (data_analysis_output_t *dao)
{
	dao_autofit_these_columns (dao, 0, dao->cols - 1);
}

/**
 * dao_autofit_these_rows:
 * @dao: #data_analysis_output_t
 * @from_row: start row
 * @to_row: end row
 *
 * Autofits the specified rows.
 **/
void
dao_autofit_these_rows (data_analysis_output_t *dao, int from_row, int to_row)
{
	GnmRange r;

	if (!dao->autofit_flag)
		return;

	range_init_rows (&r, dao->dst_sheet,
			 from_row + dao->start_row,
			 to_row + dao->start_row);

	colrow_autofit (dao->dst_sheet, &r, FALSE,
			FALSE, dao->autofit_noshrink, FALSE,
			NULL, NULL,
			TRUE);
}

/**
 * dao_autofit_rows:
 * @dao: #data_analysis_output_t
 *
 * Autofits all rows in the output range.
 **/
void
dao_autofit_rows (data_analysis_output_t *dao)
{
	dao_autofit_these_rows (dao, 0, dao->rows - 1);
}


/**
 * dao_set_style:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 * @style: (transfer full):
 *
 * Applies a partial style to the given region.
 **/
static void
dao_set_style (data_analysis_output_t *dao, int col1, int row1,
	      int col2, int row2, GnmStyle *mstyle)
{
	GnmRange r;

	range_init (&r, col1, row1, col2, row2);

	if (!adjust_range (dao, &r)) {
		gnm_style_unref (mstyle);
	        return;
	}

	sheet_style_apply_range (dao->dst_sheet, &r, mstyle);
}

/**
 * dao_set_bold:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 *
 * Sets the given cell range to bold
 **/
void
dao_set_bold (data_analysis_output_t *dao, int col1, int row1,
	      int col2, int row2)
{
	GnmStyle *mstyle = gnm_style_new ();

	gnm_style_set_font_bold (mstyle, TRUE);

	dao_set_style (dao, col1, row1, col2, row2, mstyle);
}

/**
 * dao_set_italic:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 *
 * Sets the given cell range to italic
 **/
void
dao_set_italic (data_analysis_output_t *dao, int col1, int row1,
		int col2, int row2)
{
	GnmStyle *mstyle = gnm_style_new ();

	gnm_style_set_font_italic (mstyle, TRUE);
	dao_set_style (dao, col1, row1, col2, row2, mstyle);
}

/**
 * dao_set_format_percent:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 *
 * Set the given cell range to percent format
 **/
void
dao_set_format_percent (data_analysis_output_t *dao, int col1, int row1,
		 int col2, int row2)
{
	GnmStyle *mstyle = gnm_style_new ();
	gnm_style_set_format (mstyle, go_format_default_percentage ());
	dao_set_style (dao, col1, row1, col2, row2, mstyle);
}

/**
 * dao_set_format_date:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 *
 * Set the given cell range to date format
 **/
void
dao_set_format_date (data_analysis_output_t *dao, int col1, int row1,
		 int col2, int row2)
{
	GnmStyle *mstyle = gnm_style_new ();
	gnm_style_set_format (mstyle, go_format_default_date ());
	dao_set_style (dao, col1, row1, col2, row2, mstyle);
}

/**
 * dao_set_format:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 * @format:
 *
 * Set the given cell range to given format
 **/
void
dao_set_format (data_analysis_output_t *dao, int col1, int row1,
		int col2, int row2,
		char const *format)
{
	GOFormat *fmt;

	fmt = go_format_new_from_XL (format);
	if (go_format_is_invalid (fmt)) {
		g_warning ("Ignoring invalid format [%s]", format);
	} else {
		GnmStyle *mstyle = gnm_style_new ();
		gnm_style_set_format (mstyle, fmt);
		dao_set_style (dao, col1, row1, col2, row2, mstyle);
	}
	go_format_unref (fmt);
}


/**
 * dao_set_colors:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 * @fore: (nullable) (transfer full):
 * @back: (nullable) (transfer full):
 *
 * Set the given cell range to given background and text colors
 **/
void
dao_set_colors (data_analysis_output_t *dao, int col1, int row1,
		int col2, int row2,
		GnmColor *fore, GnmColor *back)
{
	GnmStyle *mstyle;

	mstyle = gnm_style_new ();
	if (fore)
		gnm_style_set_font_color (mstyle, fore);
	if (back) {
		gnm_style_set_back_color (mstyle, back);
		gnm_style_set_pattern (mstyle, 1);
	}
	dao_set_style (dao, col1, row1, col2, row2, mstyle);
}

/**
 * dao_set_align:
 * @dao: #data_analysis_output_t
 * @col1: start column
 * @row1: start row
 * @col2: end column
 * @row2: end row
 * @align_h: #GnmHAlign
 * @align_v: #GnmVAlign
 *
 * Set the given horizontal and vertical alignment to a cell range
 **/
void
dao_set_align (data_analysis_output_t *dao, int col1, int row1,
	       int col2, int row2,
	       GnmHAlign align_h, GnmVAlign align_v)
{
	GnmStyle *mstyle;

	mstyle = gnm_style_new ();
	gnm_style_set_align_h (mstyle, align_h);
	gnm_style_set_align_v (mstyle, align_v);
	dao_set_style (dao, col1, row1, col2, row2, mstyle);
}

/**
 * dao_set_border:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 * @elem:
 * @border:
 * @color: (transfer full):
 * @orientation:
 **/
void
dao_set_border (data_analysis_output_t *dao, int col1, int row1,
		int col2, int row2,
		GnmStyleElement elem, GnmStyleBorderType border,
		GnmColor *color,
		GnmStyleBorderOrientation orientation)
{
	GnmStyle *mstyle;

	mstyle = gnm_style_new ();
	gnm_style_set_border (mstyle, elem,
			      gnm_style_border_fetch (border,
						      color,
						      orientation));
	dao_set_style (dao, col1, row1, col2, row2, mstyle);
}



/**
 * dao_get_colrow_state_list:
 * @dao:
 * @is_cols: %TRUE for columns, %FALSE for rows.
 *
 * Returns: (transfer full):
 **/
ColRowStateList *
dao_get_colrow_state_list (data_analysis_output_t *dao, gboolean is_cols)
{
	switch (dao->type) {
	case GNM_DAO_OUTPUT_NEWSHEET:
	case GNM_DAO_OUTPUT_NEWWORKBOOK:
		return NULL;
	case GNM_DAO_OUTPUT_RANGE:
		if (is_cols)
			return colrow_get_states
				(dao->dst_sheet, is_cols, dao->start_col,
				 dao->start_col + dao->cols - 1);
		else
			return colrow_get_states
				(dao->dst_sheet, is_cols, dao->start_row,
				 dao->start_row + dao->rows - 1);
	default:
		return NULL;
	}
}

/**
 * dao_set_colrow_state_list:
 * @dao:
 * @is_cols: %TRUE for columns, %FALSE for rows.
 * @list:
 *
 **/
void
dao_set_colrow_state_list (data_analysis_output_t *dao, gboolean is_cols,
			   ColRowStateList *list)
{
	g_return_if_fail (list);

	if (dao->type == GNM_DAO_OUTPUT_RANGE)
		colrow_set_states (dao->dst_sheet, is_cols,
				   is_cols ? dao->start_col : dao->start_row,
				   list);
}

/**
 * dao_append_date:
 * @buf: #GString
 *
 * Appends the current date and time to @buf.
 **/
void
dao_append_date (GString *buf)
{
	GDate     date;
	struct tm tm_s;
	gchar     *tmp;
	time_t    now;

	now = time (NULL);
	g_date_set_time_t (&date, now);
	g_date_to_struct_tm (&date, &tm_s);
	tm_s.tm_sec  = now % 60;
	tm_s.tm_min  = (now / 60) % 60;
	tm_s.tm_hour = (now / 3600) % 24;
	tmp = asctime (&tm_s);
	g_string_append (buf, tmp);
}

/**
 * dao_write_header:
 * @dao:
 * @toolname: name of the tool, like Solver or Risk simulation
 * @title:
 * @sheet:
 *
 * Writes the titles of a report.
 **/
void
dao_write_header (data_analysis_output_t *dao, const gchar *toolname,
		  const gchar *title, Sheet *sheet)
{
	GString *buf;
	const char *uri;

	buf = g_string_new (NULL);
	g_string_append_printf (buf, "%s %s %s %s",
		_("Gnumeric "), toolname, VERSION, title);
	dao_set_cell (dao, 0, 0, buf->str);
	g_string_free (buf, TRUE);

	buf = g_string_new (NULL);
	uri = go_doc_get_uri (GO_DOC (sheet->workbook));
	g_string_append_printf (buf, "%s [%s]%s", _("Worksheet:"),
				uri,
				sheet->name_quoted);
	dao_set_cell (dao, 0, 1, buf->str);
	g_string_free (buf, TRUE);

	buf = g_string_new (NULL);
	g_string_append (buf, _("Report Created: "));
	dao_append_date (buf);
	dao_set_cell (dao, 0, 2, buf->str);
	g_string_free (buf, TRUE);

	dao_set_bold (dao, 0, 0, 0, 2);
}


/**
 * dao_find_name:
 * @sheet: #Sheet
 * @col: column
 * @row: row
 *
 * Returns: (transfer full): a string describing the cell at (@col, @row).
 **/
char *
dao_find_name (Sheet *sheet, int col, int row)
{
        static char *str     = NULL;
	const char  *col_str = "";
	const char  *row_str = "";
        int         col_n, row_n;

	for (col_n = col - 1; col_n >= 0; col_n--) {
	        GnmCell *cell = sheet_cell_get (sheet, col_n, row);
		if (cell && !VALUE_IS_NUMBER (cell->value)) {
			col_str = value_peek_string (cell->value);
		        break;
		}
	}

	for (row_n = row - 1; row_n >= 0; row_n--) {
	        GnmCell *cell = sheet_cell_get (sheet, col, row_n);
		if (cell && !VALUE_IS_NUMBER (cell->value)) {
			row_str = value_peek_string (cell->value);
		        break;
		}
	}

	if (*col_str || *row_str) {
		str = g_new (char, strlen (col_str) + strlen (row_str) + 2);

		if (*col_str)
			sprintf (str, "%s %s", col_str, row_str);
		else
			sprintf (str, "%s", row_str);
	} else {
		const char *tmp = cell_coord_name (col, row);

		str = g_new (char, strlen (tmp) + 1);
		strcpy (str, tmp);
	}

	return str;
}

/**
 * dao_put_formulas:
 * @dao: #data_analysis_output_t
 *
 * Returns: %TRUE if the tool should output formulas.
 **/
gboolean
dao_put_formulas (data_analysis_output_t *dao)
{
	g_return_val_if_fail (dao != NULL, FALSE);
	return dao->put_formulas;
}

static GnmValue *
cb_convert_to_value (GnmCellIter const *iter, G_GNUC_UNUSED gpointer user)
{
	GnmCell *cell = iter->cell;
	if (!cell || !gnm_cell_has_expr (cell))
		return NULL;

	gnm_cell_eval (cell);

	if (gnm_expr_top_is_array_elem (cell->base.texpr, NULL, NULL))
		return NULL;

	gnm_cell_convert_expr_to_value (cell);
	return NULL;
}


static void
dao_convert_to_values (data_analysis_output_t *dao)
{
	if (dao->put_formulas)
		return;

	sheet_foreach_cell_in_region (dao->dst_sheet, CELL_ITER_IGNORE_BLANK,
				      dao->start_col, dao->start_row,
				      dao->start_col + dao->cols - 1,
				      dao->start_row + dao->rows - 1,
				      cb_convert_to_value,
				      NULL);
}

/**
 * dao_redraw_respan:
 * @dao: #data_analysis_output_t
 *
 * Forces a redraw and respan of the output range.
 **/
void
dao_redraw_respan (data_analysis_output_t *dao)
{
	GnmRange r;

	range_init (&r, dao->start_col, dao->start_row,
		    dao->start_col + dao->cols - 1,
		    dao->start_row + dao->rows - 1);
	sheet_range_calc_spans (dao->dst_sheet, &r,
				GNM_SPANCALC_RESIZE | GNM_SPANCALC_RE_RENDER);
	sheet_region_queue_recalc (dao->dst_sheet, &r);
	dao_convert_to_values (dao);
	sheet_redraw_range (dao->dst_sheet, &r);
}


static GnmExpr const  *
dao_get_cellref_full (data_analysis_output_t *dao, int x, int y, Sheet *sheet)
{
	GnmCellRef r;
	r.sheet = sheet;
	r.col = x + dao->start_col + dao->offset_col;
	r.col_relative = FALSE;
	r.row = y + dao->start_row + dao->offset_row;
	r.row_relative = FALSE;
	return gnm_expr_new_cellref (&r);
}

/**
 * dao_get_cellref:
 * @dao: #data_analysis_output_t
 * @x: col
 * @y: row
 *
 * Returns: (transfer full): a cell reference to the output cell at (@x, @y).
 **/
GnmExpr const  *
dao_get_cellref (data_analysis_output_t *dao, int x, int y)
{
	return dao_get_cellref_full (dao, x, y, NULL);
}

static GnmExpr const  *
dao_get_rangeref_full (data_analysis_output_t *dao, int ax, int ay,  int bx, int by, Sheet *sheet)
{
	GnmValue *v;
	GnmCellRef ar;
	GnmCellRef br;

	ar.sheet = sheet;
	ar.col = ax + dao->start_col + dao->offset_col;
	ar.col_relative = FALSE;
	ar.row = ay + dao->start_row + dao->offset_row;
	ar.row_relative = FALSE;

	br.sheet = sheet;
	br.col = bx + dao->start_col + dao->offset_col;
	br.col_relative = FALSE;
	br.row = by + dao->start_row + dao->offset_row;
	br.row_relative = FALSE;

	v = value_new_cellrange (&ar, &br, 0, 0);
	return gnm_expr_new_constant (v);
}

/**
 * dao_get_rangeref:
 * @dao: #data_analysis_output_t
 * @ax: start col
 * @ay: start row
 * @bx: end col
 * @by: end row
 *
 * Returns: (transfer full): a range reference relative to the output.
 **/
GnmExpr const  *
dao_get_rangeref (data_analysis_output_t *dao, int ax, int ay,  int bx, int by)
{
	return dao_get_rangeref_full (dao, ax, ay, bx, by, NULL);
}

/**
 * dao_set_sheet_object:
 * @dao: #data_analysis_output_t
 * @col: col
 * @row: row
 * @so: #SheetObject
 *
 * Places @so in the output sheet at (@col, @row).
 **/
void
dao_set_sheet_object (data_analysis_output_t *dao, int col, int row, SheetObject* so)
{
	SheetObjectAnchor anchor;
	GnmRange	  anchor_r;

	g_return_if_fail (so != NULL);

	if (dao->omit_so) {
		g_object_unref (so);
		return;
	}

	range_init (&anchor_r, dao->start_col + col, dao->start_row + row,
		    dao->start_col + ((dao->cols < 5) ? dao->cols : 5),
		    dao->start_row + ((dao->rows < 20) ? dao->rows : 20));

	sheet_object_anchor_init (&anchor, &anchor_r, NULL, GOD_ANCHOR_DIR_UNKNOWN,
	                          GNM_SO_ANCHOR_TWO_CELLS);
	sheet_object_set_anchor (so, &anchor);
	sheet_object_set_sheet (so, dao->dst_sheet);

	dao->sos = g_slist_prepend (dao->sos, so);
}

/**
 * dao_go_data_vector:
 * @dao:
 * @ax:
 * @ay:
 * @bx:
 * @by:
 *
 * Returns: (transfer full):
 **/
GOData	*
dao_go_data_vector (data_analysis_output_t *dao, int ax, int ay,  int bx, int by)
{
	return gnm_go_data_vector_new_expr
		(dao->dst_sheet,
		 gnm_expr_top_new (dao_get_rangeref_full (dao, ax, ay, bx, by, dao->dst_sheet)));
}

/**
 * dao_surrender_so:
 * @dao:
 *
 * Returns: (element-type GObject) (transfer full):
 **/
GSList *
dao_surrender_so (data_analysis_output_t *dao)
{
	GSList *l = dao->sos;
	dao->sos = NULL;

	return l;
}

/**
 * dao_set_omit_so:
 * @dao: #data_analysis_output_t
 * @omit: boolean
 *
 * Sets the omit sheet objects flag for @dao.
 **/
void
dao_set_omit_so (data_analysis_output_t *dao, gboolean omit)
{
	dao->omit_so = omit;
}



/**
 * dao_set_merge:
 * @dao: #data_analysis_output_t
 * @col1: start col
 * @row1: start row
 * @col2: end col
 * @row2: end row
 *
 * Merges the cells in the specified relative range.
 **/
void
dao_set_merge (data_analysis_output_t *dao, int col1, int row1,
	       int col2, int row2)
{
	GnmRange r;

	range_init (&r, col1, row1, col2, row2);
	if (adjust_range (dao, &r))
		gnm_sheet_merge_add (dao->dst_sheet, &r, TRUE, NULL);
}


/**
 * dao_get_date_conv:
 * @dao: #data_analysis_output_t
 *
 * Returns: (transfer none): date conventions appropriate for querying
 * date conventions.
 */
GODateConventions const *
dao_get_date_conv (data_analysis_output_t *dao)
{
	if (dao->dst_sheet)
		return sheet_date_conv (dao->dst_sheet);

	if (dao->ref_sheet)
		return sheet_date_conv (dao->ref_sheet);

	g_warning ("Missing dao sheet info");

	return go_date_conv_from_str ("Lotus:1900");
}

/**
 * dao_get_sheet_size:
 * @dao: #data_analysis_output_t
 *
 * Returns: (transfer none): the relevant #GnmSheetSize object.
 */
GnmSheetSize const *
dao_get_sheet_size (data_analysis_output_t *dao)
{
	if (dao->dst_sheet)
		return gnm_sheet_get_size (dao->dst_sheet);

	if (dao->ref_sheet)
		return gnm_sheet_get_size (dao->ref_sheet);

	g_warning ("Missing dao sheet info");

	static const GnmSheetSize default_size = {
		GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS
	};
	return &default_size;
}
