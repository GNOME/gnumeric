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
 * @dao:
 * @type:
 *
 * Initialize dao to given type.
 *
 **/

data_analysis_output_t *
dao_init (data_analysis_output_t *dao,
	  data_analysis_output_type_t type)
{
	if (dao == NULL) {
		dao = g_new (data_analysis_output_t, 1);
		dao->use_gfree = TRUE;
	} else
		dao->use_gfree = FALSE;

	dao->type              = type;
	dao->start_col         = 0;
	dao->start_row         = 0;
	dao->offset_col        = 0;
	dao->offset_row        = 0;
	dao->cols              = 1;  /* Fixed in dao_prepare_output */
	dao->rows              = 1;
	dao->sheet             = NULL;
	dao->autofit_flag      = TRUE;
	dao->autofit_noshrink  = TRUE;
	dao->clear_outputrange = TRUE;
	dao->retain_format     = FALSE;
	dao->retain_comments   = FALSE;
	dao->put_formulas      = FALSE;
	dao->wbc               = NULL;
	dao->sos               = NULL;
        dao->omit_so           = FALSE;

	return dao;
}

/**
 * dao_init_new_sheet: (skip)
 * @dao:
 *
 **/
data_analysis_output_t *
dao_init_new_sheet (data_analysis_output_t *dao)
{
	return dao_init (dao, NewSheetOutput);
}

void dao_free (data_analysis_output_t *dao)
{
	g_slist_free_full (dao->sos, g_object_unref);
	dao->sos = NULL;

	if (dao->use_gfree)
		g_free (dao);
}

/**
 * dao_load_from_value: (skip)
 * @dao:
 * @output_range:
 *
 **/
data_analysis_output_t *
dao_load_from_value (data_analysis_output_t *dao,
		     GnmValue *output_range)
{
	g_return_val_if_fail (output_range != NULL, dao);
	g_return_val_if_fail (VALUE_IS_CELLRANGE (output_range), dao);

	dao->start_col = output_range->v_range.cell.a.col;
	dao->start_row = output_range->v_range.cell.a.row;
	dao->cols = output_range->v_range.cell.b.col
		- output_range->v_range.cell.a.col + 1;
	dao->rows = output_range->v_range.cell.b.row
		- output_range->v_range.cell.a.row + 1;
	dao->sheet = output_range->v_range.cell.a.sheet;

	return dao;
}

/**
 * dao_range_name:
 * @dao:
 *
 * Provides the name of the output range
 * The caller has to dispose of the name
 *
 **/

static char *
dao_range_name (data_analysis_output_t *dao)
{
	GnmRange range;
	range_init (&range, dao->start_col, dao->start_row,
		    dao->start_col + dao->cols - 1,
		    dao->start_row + dao->rows - 1);

	return undo_range_name (dao->sheet, &range);
}

/**
 * dao_command_descriptor:
 * @dao:
 * @format:
 * @result:
 *
 * Uses format to provide a string to be used as command descriptor for
 * undo/redo
 *
 **/

char *
dao_command_descriptor (data_analysis_output_t *dao, char const *format,
			gpointer result)
{
	char *rangename = NULL;
	char **text = result;

	g_return_val_if_fail (result != NULL, NULL);

	g_free (*text);
	switch (dao->type) {
	case NewSheetOutput:
		*text = g_strdup_printf (format, _("New Sheet"));
		break;
	case NewWorkbookOutput:
		*text = g_strdup_printf (format, _("New Workbook"));
		break;
	case RangeOutput:
	default:
		rangename = dao_range_name (dao);
		*text = g_strdup_printf (format, rangename);
		g_free (rangename);
		break;
	}
	return *text;
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

	if (dao->sheet) {
		max_rows = gnm_sheet_get_max_rows (dao->sheet) - dao->start_row;
		max_cols = gnm_sheet_get_max_cols (dao->sheet) - dao->start_col;
	} else {
		/* In case of NewSheetOutput and NewWorkbookOutput */
		/* this is called before we actually create the    */
		/* new sheet and/or workbook                       */
		Sheet *old_sheet = wb_control_cur_sheet (dao->wbc);
		max_rows = gnm_sheet_get_max_rows (old_sheet) - dao->start_row;
		max_cols = gnm_sheet_get_max_cols (old_sheet) - dao->start_col;
	}

	if (dao->cols > max_cols)
		dao->cols = max_cols;
	if (dao->rows > max_rows)
		dao->rows = max_rows;
}

/**
 * dao_prepare_output:
 * @dao:
 * @name:
 *
 * prepares the output by creating a new sheet or workbook as appropriate
 *
 **/

void
dao_prepare_output (WorkbookControl *wbc, data_analysis_output_t *dao,
		    const char *name)
{
	char *unique_name;

	if (wbc)
		dao->wbc = wbc;

	if (dao->type == NewSheetOutput) {
		Sheet *old_sheet = dao->wbc
			? wb_control_cur_sheet (dao->wbc)
			: dao->sheet;
		Workbook *wb = old_sheet->workbook;
		char *name_with_counter = g_strdup_printf ("%s (1)", name);
		unique_name = workbook_sheet_get_free_name
			(wb, name_with_counter, FALSE, TRUE);
		g_free (name_with_counter);
		dao->rows = gnm_sheet_get_max_rows (old_sheet);
		dao->cols = gnm_sheet_get_max_cols (old_sheet);
	        dao->sheet = sheet_new (wb, unique_name, dao->cols, dao->rows);
		g_free (unique_name);
		dao->start_col = dao->start_row = 0;
		workbook_sheet_attach (wb, dao->sheet);
	} else if (dao->type == NewWorkbookOutput) {
		Sheet *old_sheet = wb_control_cur_sheet (dao->wbc);
		Workbook *wb = workbook_new ();
		dao->rows = gnm_sheet_get_max_rows (old_sheet);
		dao->cols = gnm_sheet_get_max_cols (old_sheet);
		dao->sheet = sheet_new (wb, name, dao->cols, dao->rows);
		dao->start_col = dao->start_row = 0;
		workbook_sheet_attach (wb, dao->sheet);
		dao->wbc = workbook_control_new_wrapper (dao->wbc, NULL, wb, NULL);
	}

	if (dao->wbc)
		wb_view_sheet_focus (wb_control_view (dao->wbc), dao->sheet);

	if (dao->rows == 0 || (dao->rows == 1 && dao->cols == 1))
		dao->rows = gnm_sheet_get_max_rows (dao->sheet) - dao->start_row;
	if (dao->cols == 0 || (dao->rows == 1 && dao->cols == 1))
		dao->cols = gnm_sheet_get_max_cols (dao->sheet) - dao->start_col;
	dao->offset_col = 0;
	dao->offset_row = 0;
}

/**
 * dao_format_output:
 * @dao:
 * @cmd:
 *
 * Formats the output range according to the settings
 *
 *
 **/
gboolean
dao_format_output (data_analysis_output_t *dao, char const *cmd)
{
	int clear_flags = 0;
	GnmRange range;

	range_init (&range, dao->start_col, dao->start_row,
		    dao->start_col + dao->cols - 1,
		    dao->start_row + dao->rows - 1);

	if (dao->type == RangeOutput
	    && sheet_range_splits_region (dao->sheet, &range, NULL,
					  GO_CMD_CONTEXT (dao->wbc), cmd))
		return TRUE;

	if (dao->clear_outputrange)
		clear_flags = CLEAR_VALUES | CLEAR_RECALC_DEPS;
	if (!dao->retain_format)
		clear_flags |= CLEAR_FORMATS;
	if (!dao->retain_comments)
		clear_flags |= CLEAR_COMMENTS;

	sheet_clear_region (dao->sheet,
			    range.start.col, range.start.row,
			    range.end.col, range.end.row,
			    clear_flags | CLEAR_NOCHECKARRAY | CLEAR_MERGES,
			    GO_CMD_CONTEXT (dao->wbc));
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

	if (dao->type == RangeOutput && (dao->cols > 1 || dao->rows > 1)) {
		if (r->end.col >= dao->start_col + dao->cols)
			r->end.col = dao->start_col + dao->cols - 1;
		if (r->end.row >= dao->start_row + dao->rows)
			r->end.row = dao->start_row + dao->rows - 1;
	}

	range_ensure_sanity (r, dao->sheet);

	return ((r->start.col <= r->end.col) && (r->start.row <= r->end.row));

}

gboolean
dao_cell_is_visible (data_analysis_output_t *dao, int col, int row)
{
	col += dao->offset_col;
	row += dao->offset_row;

	if (dao->type == RangeOutput &&
	    (dao->cols > 1 || dao->rows > 1) &&
	    (col >= dao->cols || row >= dao->rows))
	        return FALSE;

	col += dao->start_col;
	row += dao->start_row;

	return (!(col >= gnm_sheet_get_max_cols (dao->sheet) || row >= gnm_sheet_get_max_rows (dao->sheet)));
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
	gnm_cell_set_array_formula (dao->sheet,
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

	cell = sheet_cell_fetch (dao->sheet, r.start.col, r.start.row);
	texpr = gnm_expr_top_new (expr);
	gnm_cell_set_expr (cell, texpr);
	gnm_expr_top_unref (texpr);
}


/**
 * dao_set_cell_value:
 * @dao:
 * @col:
 * @row:
 * @v: (transfer full):
 *
 * set cell to a value
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

	cell = sheet_cell_fetch (dao->sheet, r.start.col, r.start.row);

	sheet_cell_set_value (cell, v);
}

/**
 * dao_set_cell:
 * @dao:
 * @col:
 * @row:
 * @text:
 *
 * set cell to a string
 *
 *
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
 * @dao:
 * @col:
 * @row:
 * @fmt:
 * @...:
 *
 * create format string and set cell.
 *
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
 * @col:
 * @row:
 * @v:
 *
 * set cell to a gnm_float
 *
 *
 **/
void
dao_set_cell_float (data_analysis_output_t *dao, int col, int row, gnm_float v)
{
	dao_set_cell_value (dao, col, row, value_new_float (v));
}


/**
 * set_cell_int:
 * @dao:
 * @col:
 * @row:
 * @v:
 *
 * set cell to an int
 *
 *
 **/
void
dao_set_cell_int (data_analysis_output_t *dao, int col, int row, int v)
{
	dao_set_cell_value (dao, col, row, value_new_int (v));
}


/**
 * dao_set_cell_na:
 * @dao:
 * @col:
 * @row:
 *
 * set cell to NA
 *
 *
 **/
void
dao_set_cell_na (data_analysis_output_t *dao, int col, int row)
{
	dao_set_cell_value (dao, col, row, value_new_error_NA (NULL));
}

/**
 * dao_set_cell_float_na:
 * @dao:
 * @col:
 * @row:
 * @v:
 * @is_valid:
 *
 * set cell to a gnm_float or NA as appropriate
 *
 *
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
 * @col:
 * @row:
 * @comment:
 *
 * set a cell comment
 *
 **/
void
dao_set_cell_comment (data_analysis_output_t *dao, int col, int row,
		      const char *comment)
{
	char const *author = NULL;
	GnmRange r;

	range_init (&r, col, row, col, row);

	if (adjust_range (dao, &r))
		cell_set_comment (dao->sheet, &r.start, author, comment, NULL);
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

	range_init_cols (&r, dao->sheet,
			 from_col + dao->start_col,
			 to_col + dao->start_col);

	colrow_autofit (dao->sheet, &r, TRUE,
			FALSE, dao->autofit_noshrink, FALSE,
			NULL, NULL);
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

void
dao_autofit_these_rows (data_analysis_output_t *dao, int from_row, int to_row)
{
	GnmRange r;

	if (!dao->autofit_flag)
		return;

	range_init_rows (&r, dao->sheet,
			 from_row + dao->start_row,
			 to_row + dao->start_row);

	colrow_autofit (dao->sheet, &r, FALSE,
			FALSE, dao->autofit_noshrink, FALSE,
			NULL, NULL);
}

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
 *
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

	sheet_style_apply_range (dao->sheet, &r, mstyle);
}

/**
 * dao_set_bold:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 *
 * sets the given cell range to bold
 *
 *
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
 * sets the given cell range to italic
 *
 *
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
 * dao_set_percent:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 *
 * set the given cell range to percent format
 *
 *
 **/
void
dao_set_percent (data_analysis_output_t *dao, int col1, int row1,
		 int col2, int row2)
{
	GnmStyle *mstyle = gnm_style_new ();
	gnm_style_set_format (mstyle, go_format_default_percentage ());
	dao_set_style (dao, col1, row1, col2, row2, mstyle);
}

/**
 * dao_set_date:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 *
 * set the given cell range to date format
 *
 *
 **/
void
dao_set_date (data_analysis_output_t *dao, int col1, int row1,
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
 * set the given cell range to given format
 *
 *
 **/
void
dao_set_format (data_analysis_output_t *dao, int col1, int row1,
		int col2, int row2,
		char const * format)
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
 *
 * set the given cell range to given background and text colors
 *
 *
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
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 *
 * set the given horizontal and vertical alignment to a cell range
 *
 *
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
 *
 *
 *
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
 *
 * Returns: (transfer full):
 **/
ColRowStateList *
dao_get_colrow_state_list (data_analysis_output_t *dao, gboolean is_cols)
{
	switch (dao->type) {
	case NewSheetOutput:
	case NewWorkbookOutput:
		return NULL;
	case RangeOutput:
		if (is_cols)
			return colrow_get_states
				(dao->sheet, is_cols, dao->start_col,
				 dao->start_col + dao->cols - 1);
		else
			return colrow_get_states
				(dao->sheet, is_cols, dao->start_row,
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

	if (dao->type == RangeOutput)
		colrow_set_states (dao->sheet, is_cols,
				   is_cols ? dao->start_col : dao->start_row,
				   list);
}

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
 *
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

	sheet_foreach_cell_in_region (dao->sheet, CELL_ITER_IGNORE_BLANK,
				      dao->start_col, dao->start_row,
				      dao->start_col + dao->cols - 1,
				      dao->start_row + dao->rows - 1,
				      cb_convert_to_value,
				      NULL);
}

void
dao_redraw_respan (data_analysis_output_t *dao)
{
	GnmRange r;

	range_init (&r, dao->start_col, dao->start_row,
		    dao->start_col + dao->cols - 1,
		    dao->start_row + dao->rows - 1);
	sheet_range_calc_spans (dao->sheet, &r,
				GNM_SPANCALC_RESIZE | GNM_SPANCALC_RE_RENDER);
	sheet_region_queue_recalc (dao->sheet, &r);
	dao_convert_to_values (dao);
	sheet_redraw_range (dao->sheet, &r);
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

GnmExpr const  *
dao_get_rangeref (data_analysis_output_t *dao, int ax, int ay,  int bx, int by)
{
	return dao_get_rangeref_full (dao, ax, ay, bx, by, NULL);
}

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
	sheet_object_set_sheet (so, dao->sheet);

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
	return gnm_go_data_vector_new_expr (dao->sheet, gnm_expr_top_new (dao_get_rangeref_full (dao, ax, ay, bx, by, dao->sheet)));
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

void
dao_set_omit_so (data_analysis_output_t *dao, gboolean omit)
{
	dao->omit_so = omit;
}



void
dao_set_merge (data_analysis_output_t *dao, int col1, int row1,
	       int col2, int row2)
{
	GnmRange r;

	range_init (&r, col1, row1, col2, row2);
	if (adjust_range (dao, &r))
		gnm_sheet_merge_add (dao->sheet, &r, TRUE, NULL);
}
