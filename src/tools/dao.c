/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <string.h>
#include "dao.h"

#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "ranges.h"
#include "style.h"
#include "sheet-style.h"
#include "workbook.h"
#include "workbook-control.h"
#include "command-context.h"
#include "format.h"
#include "sheet-object-cell-comment.h"
#include "commands.h"

#include <gtk/gtk.h>
#include <time.h>
#include <parse-util.h>

/**
 * dao_INIT:
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
	if (dao == NULL)
		dao = g_new (data_analysis_output_t, 1);

	dao->type              = type;
	dao->start_col         = 0;
	dao->start_row         = 0;
	dao->offset_col        = 0;
	dao->offset_row        = 0;
	dao->cols              = SHEET_MAX_COLS;
	dao->rows              = SHEET_MAX_ROWS;
	dao->sheet             = NULL;
	dao->autofit_flag      = TRUE;
	dao->clear_outputrange = TRUE;
	dao->retain_format     = FALSE;
	dao->retain_comments   = FALSE;

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

char *
dao_range_name (data_analysis_output_t *dao)
{
	Range range;
	range_init (&range, dao->start_col, dao->start_row,
		    dao->start_col + dao->cols - 1, 
		    dao->start_row + dao->rows - 1);

	return cmd_range_to_str_utility (dao->sheet, &range);
}

/**
 * dao_command_descriptor:
 * @dao:
 * @format:
 * @result:
 *
 * Uses fmt to provide a string to be used as command descriptor for
 * undo/redo
 *
 **/

char *
dao_command_descriptor (data_analysis_output_t *dao, char const *format,
			gpointer result)
{
	char *rangename = NULL;
	char **text;

	g_return_val_if_fail (result != NULL, NULL);

	text = ((char **)result);
	if (*text != NULL)
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
		break;;
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
	if (dao->cols == 1 && dao->rows == 1) {
		dao->cols = cols;
		dao->rows = rows;
	} else {
		dao->cols = MIN (cols, dao->cols);
		dao->rows = MIN (rows, dao->rows);
	}
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
		Workbook *wb = wb_control_workbook (dao->wbc);
		unique_name = workbook_sheet_get_free_name (wb, name, FALSE,
							    FALSE);
	        dao->sheet = sheet_new (wb, unique_name);
		g_free (unique_name);
		dao->start_col = dao->start_row = 0;
		dao->rows = SHEET_MAX_ROWS;
		dao->cols = SHEET_MAX_COLS;
		workbook_sheet_attach (wb, dao->sheet, NULL);
	} else if (dao->type == NewWorkbookOutput) {
		Workbook *wb = workbook_new ();
		dao->sheet = sheet_new (wb, name);
		dao->start_col = dao->start_row = 0;
		dao->rows = SHEET_MAX_ROWS;
		dao->cols = SHEET_MAX_COLS;
		workbook_sheet_attach (wb, dao->sheet, NULL);
		dao->wbc = wb_control_wrapper_new (dao->wbc, NULL, wb);
	}
	if (dao->rows == 0 || (dao->rows == 1 && dao->cols == 1))
		dao->rows = SHEET_MAX_ROWS - dao->start_row;
	if (dao->cols == 0 || (dao->rows == 1 && dao->cols == 1))
		dao->cols = SHEET_MAX_COLS - dao->start_col;
	dao->offset_col = 0;
	dao->offset_row = 0;
}

/**
 * dao_format_output:
 * @dao:
 * @cmd:
 *
 * format's the output range according to the settings
 * 
 *
 **/
gboolean
dao_format_output (data_analysis_output_t *dao, char const *cmd)
{
	int clear_flags = 0;
	Range range;

	range_init (&range, dao->start_col, dao->start_row,
		    dao->start_col + dao->cols - 1, 
		    dao->start_row + dao->rows - 1);
	
	if (dao->type == RangeOutput
	    && sheet_range_splits_region (dao->sheet, &range, NULL,
					  COMMAND_CONTEXT (dao->wbc), cmd))
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
			    COMMAND_CONTEXT (dao->wbc));
	return FALSE;
}

/**
 * dao_set_cell_value:
 * @dao:
 * @col:
 * @row:
 * @v:
 *
 * set cell to a value
 *
 * Note: the rows/cols specification for all dao_set_cell_...
 *       commands are relative to the location of the output
 * 
 *
 **/
void
dao_set_cell_value (data_analysis_output_t *dao, int col, int row, Value *v)
{
        Cell *cell;

	col += dao->offset_col;
	row += dao->offset_row;

	/* Check that the output is in the given range, but allow singletons
	 * to expand
	 */
	if (dao->type == RangeOutput &&
	    (dao->cols > 1 || dao->rows > 1) &&
	    (col >= dao->cols || row >= dao->rows)) {
		value_release (v);
	        return;
	}

	col += dao->start_col;
	row += dao->start_row;
	if (col >= SHEET_MAX_COLS || row >= SHEET_MAX_ROWS) {
		value_release (v);
		return;
	}

	cell = sheet_cell_fetch (dao->sheet, col, row);

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
 * set cell to a float
 * 
 *
 **/
void
dao_set_cell_float (data_analysis_output_t *dao, int col, int row, gnum_float v)
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
	dao_set_cell_value (dao, col, row, value_new_error (NULL,
							    gnumeric_err_NA));
}

/**
 * dao_set_cell_float_na:
 * @dao:
 * @col:
 * @row:
 * @v:
 * @is_valid:
 *
 * set cell to a float or NA as appropraite
 * 
 *
 **/
void
dao_set_cell_float_na (data_analysis_output_t *dao, int col, int row,
		       gnum_float v, gboolean is_valid)
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
	CellPos pos;
	const char *author = NULL;

	/* Check that the output is in the given range, but allow singletons
	 * to expand
	 */
	if (dao->type == RangeOutput &&
	    (dao->cols > 1 || dao->rows > 1) &&
	    (col >= dao->cols || row >= dao->rows))
	        return;

	col += dao->start_col;
	row += dao->start_row;
	if (col >= SHEET_MAX_COLS || row >= SHEET_MAX_ROWS)
		return;

	pos.col = col;
	pos.row = row;
	cell_set_comment (dao->sheet, &pos, author, comment);
}


/**
 * autofit_column:
 * @dao:
 * @col:
 *
 * fits a column to the content
 * 
 *
 **/
static void
dao_autofit_column (data_analysis_output_t *dao, int col)
{
        int ideal_size, actual_col;

	actual_col = dao->start_col + col;

	ideal_size = sheet_col_size_fit_pixels (dao->sheet, actual_col);
	if (ideal_size == 0)
	        return;

	sheet_col_set_size_pixels (dao->sheet, actual_col, ideal_size, TRUE);
	sheet_recompute_spans_for_col (dao->sheet, col);
}

/**
 * dao_autofit_these_columns:
 * @dao:
 * @from:
 * @to:
 *
 * fits all columns to their content
 * 
 *
 **/
void
dao_autofit_these_columns (data_analysis_output_t *dao, int from, int to)
{
	int i;

	if (!dao->autofit_flag)
		return;
	for (i = from; i <= to; i++)
		dao_autofit_column (dao,i);
}

/**
 * autofit_columns:
 * @dao:
 * @from:
 * @to:
 *
 * fits all columns to their content
 * 
 *
 **/
void
dao_autofit_columns (data_analysis_output_t *dao)
{
	dao_autofit_these_columns (dao, 0, dao->cols - 1);
}

/**
 * dao_set_style:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 * @style:
 *
 * sets the given cell range to bold
 * 
 *
 **/
static void
dao_set_style (data_analysis_output_t *dao, int col1, int row1,
	      int col2, int row2, MStyle *mstyle)
{
	Range  range;

	range.start.col = col1 + dao->start_col + dao->offset_col;
	range.start.row = row1 + dao->start_row + dao->offset_row;
	range.end.col   = col2 + dao->start_col + dao->offset_col;
	range.end.row   = row2 + dao->start_row + dao->offset_row;

	if (range.end.col > dao->start_col + dao->cols)
		range.end.col = dao->start_col + dao->cols;
	if (range.end.row > dao->start_row + dao->rows)
		range.end.row = dao->start_row + dao->rows;

	if (range.end.col < range.start.col)
		return;
	if (range.end.row < range.start.row)
		return;

	sheet_style_apply_range (dao->sheet, &range, mstyle);
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
	MStyle *mstyle = mstyle_new ();
	Range  range;

	range.start.col = col1 + dao->start_col;
	range.start.row = row1 + dao->start_row;
	range.end.col   = col2 + dao->start_col;
	range.end.row   = row2 + dao->start_row;

	mstyle_set_font_bold (mstyle, TRUE);
	sheet_style_apply_range (dao->sheet, &range, mstyle);
}

/**
 * dao_set_underlined:
 * @dao:
 * @col1:
 * @row1:
 * @col2:
 * @row2:
 *
 * sets the given cell range to underlined
 * 
 *
 **/
void
dao_set_underlined (data_analysis_output_t *dao, int col1, int row1,
		    int col2, int row2)
{
	MStyle *mstyle = mstyle_new ();
	Range  range;

	range.start.col = col1 + dao->start_col;
	range.start.row = row1 + dao->start_row;
	range.end.col   = col2 + dao->start_col;
	range.end.row   = row2 + dao->start_row;

	mstyle_set_font_uline (mstyle, TRUE);
	sheet_style_apply_range (dao->sheet, &range, mstyle);
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
	MStyle *mstyle = mstyle_new ();

	mstyle_set_font_italic (mstyle, TRUE);
	dao_set_style (dao, col1 + dao->start_col, row1 + dao->start_row,
		       col2 + dao->start_col, row2 + dao->start_row, mstyle);
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
	MStyle *mstyle = mstyle_new ();

	mstyle_set_format_text (mstyle, "0.00%");
	dao_set_style (dao, col1 + dao->start_col, row1 + dao->start_row,
		       col2 + dao->start_col, row2 + dao->start_row, mstyle);
}


/**
 * dao_get_colrow_state_list:
 * @dao:
 * @is_cols:
 *
 * 
 *
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
 * @is_cols:
 * @list:
 *
 * 
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
	GTimeVal  t;
	struct tm tm_s;
	gchar     *tmp;

	g_get_current_time (&t);
	g_date_set_time (&date, t.tv_sec);
	g_date_to_struct_tm (&date, &tm_s);
	tm_s.tm_sec  = t.tv_sec % 60;
	tm_s.tm_min  = (t.tv_sec / 60) % 60;
	tm_s.tm_hour = (t.tv_sec / 3600) % 24;
	tmp = asctime (&tm_s);
	g_string_append (buf, tmp);
}

/**
 * dao_write_header: Writes the titles of a report.
 * @dao:
 * @toolname: name of the tool, like Solver or Risk simulation
 * @title:
 * @sheet:
 *
 * 
 *
 **/
void
dao_write_header (data_analysis_output_t *dao, const gchar *toolname,
		  const gchar *title, Sheet *sheet)
{
	GString   *buf;

	buf = g_string_new ("");
	g_string_append_printf (buf, "%s %s %s %s", 
		_("Gnumeric "), toolname, VERSION, title);
	dao_set_cell (dao, 0, 0, buf->str);
	g_string_free (buf, FALSE);

	buf = g_string_new ("");
	g_string_append_printf (buf, "%s [%s]%s", _("Worksheet:"),
		workbook_get_filename (sheet->workbook),
		sheet->name_quoted);
	dao_set_cell (dao, 0, 1, buf->str);
	g_string_free (buf, FALSE);

	buf = g_string_new ("");
	g_string_append (buf, _("Report Created: "));
	dao_append_date (buf);
	dao_set_cell (dao, 0, 2, buf->str);
	g_string_free (buf, FALSE);

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
	        Cell *cell = sheet_cell_get (sheet, col_n, row);
		if (cell && !VALUE_IS_NUMBER (cell->value)) {
			col_str = value_peek_string (cell->value);
		        break;
		}
	}

	for (row_n = row - 1; row_n >= 0; row_n--) {
	        Cell *cell = sheet_cell_get (sheet, col, row_n);
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
