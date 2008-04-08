/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * analysis-tools.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2002, 2004 by Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * Modified 2001 to use range_* functions of mathfunc.h
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
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "analysis-tools.h"

#include "mathfunc.h"
#include "func.h"
#include "expr.h"
#include "position.h"
#include "rangefunc.h"
#include "tools.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "ranges.h"
#include "style.h"
#include "regression.h"
#include "sheet-style.h"
#include "workbook.h"
#include "collect.h"
#include "gnm-format.h"
#include "sheet-object-cell-comment.h"
#include "workbook-control.h"
#include "command-context.h"
#include <goffice/utils/go-glib-extras.h>

#include <string.h>
#include <stdlib.h>
#include <math.h>

typedef struct {
	char *format;
	GPtrArray *data_lists;
	gboolean read_label;
	gboolean ignore_non_num;
	guint length;
	Sheet *sheet;
} data_list_specs_t;

static GnmValue *
cb_store_data (GnmCellIter const *iter, gpointer user)
{
	data_set_t *data_set = (data_set_t *)user;
	GnmCell *cell = iter->cell;
	gnm_float the_data;

	if (data_set->read_label) {
		if (cell != NULL) {
			data_set->label = cell->value
				? value_get_as_string (cell->value)
				: NULL;
			if (data_set->label == NULL || strlen (data_set->label) == 0) {
				g_free (data_set->label);
				data_set->label = NULL;
			}
		}
		data_set->read_label = FALSE;
		return NULL;
	}
	if (cell == NULL || !VALUE_IS_NUMBER (cell->value)) {
		if (data_set->complete) {
			data_set->missing = g_slist_prepend (data_set->missing,
						 GUINT_TO_POINTER (data_set->data->len));
			the_data = 0;
			g_array_append_val (data_set->data, the_data);
		}
	} else {
		the_data =  value_get_as_float (cell->value);
		g_array_append_val (data_set->data, the_data);
	}
	return NULL;
}

/*
 *  new_data_set:
 *  @range: GnmValue *       the data location, usually a single column or row
 *  @ignore_non_num: gboolean   whether simply to ignore non-numerical values
 *  @read_label: gboolean       whether the first entry contains a label
 *  @format: char*              format string for default label
 *  @i: guint                    index for default label
 */
static data_set_t *
new_data_set (GnmValue *range, gboolean ignore_non_num, gboolean read_label,
	      char *format, gint i, Sheet* sheet)
{
	GnmValue *result;
	GnmEvalPos  *pos = g_new (GnmEvalPos, 1);
	data_set_t * the_set = g_new (data_set_t, 1);

	pos = eval_pos_init_sheet (pos, sheet);
	the_set->data = g_array_new (FALSE, FALSE, sizeof (gnm_float)),
	the_set->missing = NULL;
        the_set->label = NULL;
	the_set->complete = !ignore_non_num;
	the_set->read_label = read_label;

	result = workbook_foreach_cell_in_range (pos, range, CELL_ITER_ALL,
						 cb_store_data, the_set);
	g_free (pos);

	if (result != NULL) value_release (result);
	the_set->missing = g_slist_reverse (the_set->missing);
	if (the_set->label == NULL)
		the_set->label = g_strdup_printf (format, i);
	return the_set;
}

/*
 *  destroy_data_set:
 *  @data_set:
 */
static void
destroy_data_set (data_set_t *data_set)
{

	if (data_set->data != NULL)
		g_array_free (data_set->data, TRUE);
	g_slist_free (data_set->missing);
	g_free (data_set->label);
	g_free (data_set);
}

/*
 *  cb_get_data_set_list:
 *  @data:
 *  @user_data:
 */
static void
cb_get_data_set_list (gpointer data, gpointer user_data)
{
	GnmValue * the_range = (GnmValue *) data;
	data_list_specs_t *specs = (data_list_specs_t *)user_data;

	specs->length++;
	g_ptr_array_add (specs->data_lists,
			 new_data_set (the_range, specs->ignore_non_num,
				       specs->read_label, specs->format,
				       specs->length, specs->sheet));
}


/*
 *  new_data_set_list:
 *  @ranges: GSList *           the data location
 *  @group_by: group_by_t       how to group the data
 *  @ignore_non_num: gboolean   whether simply to ignore non-numerical values
 *  @read_label: gboolean       whether the first entry contains a label
 */
GPtrArray *
new_data_set_list (GSList *ranges, group_by_t group_by,
		   gboolean ignore_non_num, gboolean read_labels, Sheet *sheet)
{
	data_list_specs_t specs = {NULL, NULL, FALSE, FALSE, 0};

	if (ranges == NULL)
		return NULL;

	specs.read_label = read_labels;
	specs.data_lists = g_ptr_array_new ();
	specs.ignore_non_num = ignore_non_num;
	specs.sheet = sheet;

	switch (group_by) {
	case GROUPED_BY_ROW:
		specs.format = _("Row %i");
		break;
	case GROUPED_BY_COL:
		specs.format = _("Column %i");
		break;
	case GROUPED_BY_BIN:
		specs.format = _("Bin %i");
		break;
	case GROUPED_BY_AREA:
	default:
		specs.format = _("Area %i");
		break;
	}

	g_slist_foreach (ranges, cb_get_data_set_list, &specs);

	return specs.data_lists;
}

/*
 *  destroy_data_set_list:
 *  @the_list:
 */
void
destroy_data_set_list (GPtrArray * the_list)
{
	guint i;

	for (i = 0; i < the_list->len; i++) {
		data_set_t *data = g_ptr_array_index (the_list, i);
		destroy_data_set (data);
	}
	g_ptr_array_free (the_list, TRUE);
}

/*
 *  analysis_tools_remove_label:
 *  @val: range to extract label from
 *  @info: analysis_tools_data_generic_t info
 *
 */

static void
analysis_tools_remove_label (GnmValue *val,
			    analysis_tools_data_generic_t *info)
{
	if (info->labels) {
		switch (info->group_by) {
		case GROUPED_BY_ROW:
			val->v_range.cell.a.col++;
			break;
		case GROUPED_BY_COL:
		case GROUPED_BY_BIN:
		case GROUPED_BY_AREA:
		default:
			val->v_range.cell.a.row++;
			break;
		}
	}
}



/*
 *  analysis_tools_write_label:
 *  @val: range to extract label from
 *  @dao: data_analysis_output_t, where to write to
 *  @info: analysis_tools_data_generic_t info
 *  @x: output col number
 *  @y: output row number
 *  @i: default col/row number
 *
 */

static void
analysis_tools_write_label (GnmValue *val, data_analysis_output_t *dao,
			    analysis_tools_data_generic_t *info,
			    int x, int y, int i)
{
	char const *format = NULL;

	if (info->labels) {
		GnmValue *label = value_dup (val);

		label->v_range.cell.b = label->v_range.cell.a;
		dao_set_cell_expr (dao, x, y, gnm_expr_new_constant (label));
		analysis_tools_remove_label (val, info);
	} else {
		switch (info->group_by) {
		case GROUPED_BY_ROW:
			format = _("Row %i");
			break;
		case GROUPED_BY_COL:
			format = _("Column %i");
			break;
		case GROUPED_BY_BIN:
			format = _("Bin %i");
			break;
		case GROUPED_BY_AREA:
		default:
			format = _("Area %i");
			break;
		}

		dao_set_cell_printf (dao, x, y, format, i);
	}
}

/*
 *  analysis_tools_write_label:
 *  @val: range to extract label from
 *  @dao: data_analysis_output_t, where to write to
 *  @info: analysis_tools_data_generic_t info
 *  @x: output col number
 *  @y: output row number
 *  @i: default col/row number
 *
 */

static void
analysis_tools_write_label_ftest (GnmValue *val, data_analysis_output_t *dao,
				  int x, int y, gboolean labels, int i)
{
	val->v_range.cell.a.col_relative = 0;
	val->v_range.cell.a.row_relative = 0;
	val->v_range.cell.b.col_relative = 0;
	val->v_range.cell.b.row_relative = 0;

	if (labels) {
		GnmValue *label = value_dup (val);

		label->v_range.cell.b = label->v_range.cell.a;
		dao_set_cell_expr (dao, x, y, gnm_expr_new_constant (label));

		if ((val->v_range.cell.b.col - val->v_range.cell.a.col) <
		    (val->v_range.cell.b.row - val->v_range.cell.a.row))
			val->v_range.cell.a.row++;
		else
			val->v_range.cell.a.col++;
	} else {
		dao_set_cell_printf (dao, x, y,  _("Variable %i"), i);
	}
}

/*
 *  cb_cut_into_cols:
 *  @data:
 *  @user_data:
 *
 */
static void
cb_cut_into_cols (gpointer data, gpointer user_data)
{
	GnmValue *range = (GnmValue *)data;
	GnmValue *col_value;
	GSList **list_of_units = (GSList **) user_data;
	gint col;

	if (range == NULL) {
		return;
	}
	if ((range->type != VALUE_CELLRANGE) ||
	    (range->v_range.cell.b.sheet != NULL &&
	     range->v_range.cell.b.sheet != range->v_range.cell.a.sheet)) {
		value_release (range);
		return;
	}

	range->v_range.cell.a.col_relative = 0;
	range->v_range.cell.a.row_relative = 0;
	range->v_range.cell.b.col_relative = 0;
	range->v_range.cell.b.row_relative = 0;

	if (range->v_range.cell.a.col == range->v_range.cell.b.col) {
		*list_of_units = g_slist_prepend (*list_of_units, range);
		return;
	}

	for (col = range->v_range.cell.a.col; col <= range->v_range.cell.b.col; col++) {
		col_value = value_dup (range);
		col_value->v_range.cell.a.col = col;
		col_value->v_range.cell.b.col = col;
		*list_of_units = g_slist_prepend (*list_of_units, col_value);
	}
	value_release (range);
	return;
}

/*
 *  cb_cut_into_rows:
 *  @data:
 *  @user_data:
 *
 */
static void
cb_cut_into_rows (gpointer data, gpointer user_data)
{
	GnmValue *range = (GnmValue *)data;
	GnmValue *row_value;
	GSList **list_of_units = (GSList **) user_data;
	gint row;

	if (range == NULL) {
		return;
	}
	if ((range->type != VALUE_CELLRANGE) ||
	    (range->v_range.cell.b.sheet != NULL &&
	     range->v_range.cell.b.sheet != range->v_range.cell.a.sheet)) {
		value_release (range);
		return;
	}

	range->v_range.cell.a.col_relative = 0;
	range->v_range.cell.a.row_relative = 0;
	range->v_range.cell.b.col_relative = 0;
	range->v_range.cell.b.row_relative = 0;

	if (range->v_range.cell.a.row == range->v_range.cell.b.row) {
		*list_of_units = g_slist_prepend (*list_of_units, range);
		return;
	}

	for (row = range->v_range.cell.a.row; row <= range->v_range.cell.b.row; row++) {
		row_value = value_dup (range);
		row_value->v_range.cell.a.row = row;
		row_value->v_range.cell.b.row = row;
		*list_of_units = g_slist_prepend (*list_of_units, row_value);
	}
	value_release (range);
	return;
}

/*
 *  cb_adjust_areas:
 *  @data:
 *  @user_data:
 *
 */
static void
cb_adjust_areas (gpointer data, G_GNUC_UNUSED gpointer user_data)
{
	GnmValue *range = (GnmValue *)data;

	if (range == NULL || (range->type != VALUE_CELLRANGE)) {
		return;
	}

	range->v_range.cell.a.col_relative = 0;
	range->v_range.cell.a.row_relative = 0;
	range->v_range.cell.b.col_relative = 0;
	range->v_range.cell.b.row_relative = 0;
}

/*
 *  prepare_input_range:
 *  @input_range:
 *  @group_by:
 *
 */
void
prepare_input_range (GSList **input_range, group_by_t group_by)
{
	GSList *input_by_units = NULL;

	switch (group_by) {
	case GROUPED_BY_ROW:
		g_slist_foreach (*input_range, cb_cut_into_rows, &input_by_units);
		g_slist_free (*input_range);
		*input_range = g_slist_reverse (input_by_units);
		return;
	case GROUPED_BY_COL:
		g_slist_foreach (*input_range, cb_cut_into_cols, &input_by_units);
		g_slist_free (*input_range);
		*input_range = g_slist_reverse (input_by_units);
		return;
	case GROUPED_BY_AREA:
	default:
		g_slist_foreach (*input_range, cb_adjust_areas, NULL);
		return;
	}
}

typedef struct {
	gboolean init;
	gint size;
	gboolean hom;
} homogeneity_check_t;


/*
 *  cb_check_hom:
 *  @data:
 *  @user_data:
 *
 */
static void
cb_check_hom (gpointer data, gpointer user_data)
{
	GnmValue *range = (GnmValue *)data;
	homogeneity_check_t *state = (homogeneity_check_t *) user_data;
	gint this_size;

	if (range->type != VALUE_CELLRANGE) {
		state->hom = FALSE;
		return;
	}

	this_size = (range->v_range.cell.b.col - range->v_range.cell.a.col + 1) *
		(range->v_range.cell.b.row - range->v_range.cell.a.row + 1);

	if (state->init) {
		if (state->size != this_size)
			state->hom = FALSE;
	} else {
		state->init = TRUE;
		state->size = this_size;
	}
	return;
}

/*
 *  gnm_check_input_range_list_homogeneity:
 *  @input_range:
 *
 *  Check that all columns have the same size
 *
 */
gboolean
gnm_check_input_range_list_homogeneity (GSList *input_range)
{
	homogeneity_check_t state = { FALSE, 0, TRUE };

	g_slist_foreach (input_range, cb_check_hom, &state);

	return state.hom;
}

/*
 *  check_data_for_missing:
 *  @data:
 *
 *  Check whether any data is missing
 *
 */
static gboolean
check_data_for_missing (GPtrArray *data)
{
	guint i;

	for (i = 0; i < data->len; i++) {
		data_set_t *this_data = g_ptr_array_index (data, i);
		if (this_data->missing != NULL)
			return TRUE;
	}

	return FALSE;
}

/***** Some general routines ***********************************************/

static gint
float_compare (gnm_float const *a, gnm_float const *b)
{
        if (*a < *b)
                return -1;
        else if (*a == *b)
                return 0;
        else
                return 1;
}

gnm_float *
range_sort (gnm_float const *xs, int n)
{
	if (n <= 0)
		return NULL;
	else {
		gnm_float *ys = g_new (gnm_float, n);
		memcpy (ys, xs, n * sizeof (gnm_float));
		qsort (ys, n, sizeof (ys[0]),
		       (int (*) (const void *, const void *))&float_compare);
		return ys;
	}
}


/*
 * Set a column of text from a string like "/first/second/third" or "|foo|bar|baz".
 */
static void
set_cell_text_col (data_analysis_output_t *dao, int col, int row, const char *text)
{
	gboolean leave = FALSE;
	char *copy, *orig_copy;
	char sep = *text;
	if (sep == 0) return;

	copy = orig_copy = g_strdup (text + 1);
	while (!leave) {
		char *p = copy;
		while (*copy && *copy != sep)
			copy++;
		if (*copy)
			*copy++ = 0;
		else
			leave = TRUE;
		dao_set_cell_value (dao, col, row++, value_new_string (p));
	}
	g_free (orig_copy);
}


/*
 * Set a row of text from a string like "/first/second/third" or "|foo|bar|baz".
 */
static void
set_cell_text_row (data_analysis_output_t *dao, int col, int row, const char *text)
{
	gboolean leave = 0;
	char *copy, *orig_copy;
	char sep = *text;
	if (sep == 0) return;

	copy = orig_copy = g_strdup (text + 1);
	while (!leave) {
		char *p = copy;
		while (*copy && *copy != sep)
			copy++;
		if (*copy)
			*copy++ = 0;
		else
			leave = TRUE;
		dao_set_cell_value (dao, col++, row, value_new_string (p));
	}
	g_free (orig_copy);
}

#warning 2006/May/31  Review this.  Why are we pulling elements from the front of an array ??
static GnmValue *
cb_write_data (GnmCellIter const *iter, GArray* data)
{
	gnm_float  x;
	GnmCell *cell;
	if (data->len == 0)
		return VALUE_TERMINATE;
	if (NULL == (cell = iter->cell))
		cell = sheet_cell_create (iter->pp.sheet,
			iter->pp.eval.col, iter->pp.eval.row);
	x = g_array_index (data, gnm_float, 0);
	g_array_remove_index (data, 0);
	sheet_cell_set_value (cell, value_new_float (x));
	return NULL;
}

static void
write_data (data_analysis_output_t *dao, GArray *data)
{
	gint st_row = dao->start_row + dao->offset_row;
	gint end_row = dao->start_row + dao->rows - 1;
	gint st_col = dao->start_col + dao->offset_col;
	gint end_col = dao->start_col + dao->offset_col;

	if (dao->cols <= dao->offset_col)
		return;

	sheet_foreach_cell_in_range (dao->sheet, CELL_ITER_ALL,
		st_col, st_row, end_col, end_row,
		(CellIterFunc)&cb_write_data, data);
}

static gboolean
analysis_tool_generic_clean (G_GNUC_UNUSED data_analysis_output_t *dao,
			     gpointer specs)
{
	analysis_tools_data_generic_t *info = specs;

	range_list_destroy (info->input);
	info->input = NULL;
	return FALSE;
}

static gboolean
analysis_tool_ftest_clean (G_GNUC_UNUSED data_analysis_output_t *dao,
			   gpointer specs)
{
	analysis_tools_data_ftest_t *info = specs;

	value_release (info->range_1);
	info->range_1 = NULL;
	value_release (info->range_2);
	info->range_2 = NULL;
	return FALSE;
}



static int
analysis_tool_calc_length (analysis_tools_data_generic_t *info)
{
	int           result = 1;
	GSList        *dataset;

	for (dataset = info->input; dataset; dataset = dataset->next) {
		GnmValue    *current = dataset->data;
		int      given_length;

		given_length = current->v_range.cell.b.row - current->v_range.cell.a.row + 1;
		if (given_length > result)
			result = given_length;
	}
	return result;
}



/************* Correlation Tool *******************************************
 *
 * The correlation tool calculates the correlation coefficient of two
 * data sets.  The two data sets can be grouped by rows or by columns.
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

static gboolean
analysis_tool_table (data_analysis_output_t *dao,
		     analysis_tools_data_generic_t *info,
		     gchar const *title, gchar const *functionname)
{
	GSList *inputdata, *inputexpr = NULL;
	GnmFunc *fd = NULL;

	guint col, row;

	dao_set_cell_printf (dao, 0, 0, title);
	dao_set_italic (dao, 0, 0, 0, 0);

	fd = gnm_func_lookup (functionname, NULL);
	gnm_func_ref (fd);

	for (col = 1, inputdata = info->input; inputdata != NULL;
	     inputdata = inputdata->next, col++) {
		GnmValue *val = NULL;

		val = value_dup (inputdata->data);

		/* Label */
		analysis_tools_write_label (val, dao, info,
					    col, 0, col);

		inputexpr = g_slist_prepend (inputexpr,
					     (gpointer) gnm_expr_new_constant (val));
	}
	inputexpr = g_slist_reverse (inputexpr);
	dao_set_italic (dao, 0, 0, col, 0);

	for (row = 1, inputdata = info->input; inputdata != NULL;
	     inputdata = inputdata->next, row++) {
		GnmValue *val = value_dup (inputdata->data);
		GSList *colexprlist;

		/* Label */
		analysis_tools_write_label (val, dao, info,
					    0, row, row);

		for (col = 1, colexprlist = inputexpr; colexprlist != NULL;
		     colexprlist = colexprlist->next, col++) {
			GnmExpr const *colexpr = colexprlist->data;

			if (col < row)
				continue;

			dao_set_cell_expr
				(dao, row, col,
				 gnm_expr_new_funcall2
				 (fd,
				  gnm_expr_new_constant (value_dup (val)),
				  gnm_expr_copy (colexpr)));
		}

		value_release (val);
	}
	dao_set_italic (dao, 0, 0, 0, row);

	go_slist_free_custom (inputexpr, (GFreeFunc)gnm_expr_free);
	if (fd) gnm_func_unref (fd);

	dao_redraw_respan (dao);
	return FALSE;
}

static gboolean
analysis_tool_correlation_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_generic_t *info)
{
	return analysis_tool_table (dao, info, _("Correlations"), "CORREL");
}

gboolean
analysis_tool_correlation_engine (data_analysis_output_t *dao, gpointer specs,
				   analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_generic_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Correlation (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->input, info->group_by);
		if (!gnm_check_input_range_list_homogeneity (info->input)) {
			info->err = info->group_by + 1;
			return TRUE;
		}
		dao_adjust (dao, 1 + g_slist_length (info->input),
			    1 + g_slist_length (info->input));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Correlation"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Correlation"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_correlation_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}




/************* Covariance Tool ********************************************
 *
 * The covariance tool calculates the covariance of two data sets.
 * The two data sets can be grouped by rows or by columns.  The
 * results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

static gboolean
analysis_tool_covariance_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_generic_t *info)
{
	return analysis_tool_table (dao, info, _("Covariances"), "COVAR");
}

gboolean
analysis_tool_covariance_engine (data_analysis_output_t *dao, gpointer specs,
				   analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_generic_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Covariance (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->input, info->group_by);
		if (!gnm_check_input_range_list_homogeneity (info->input)) {
			info->err = info->group_by + 1;
			return TRUE;
		}
		dao_adjust (dao, 1 + g_slist_length (info->input),
			    1 + g_slist_length (info->input));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Covariance"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Covariance"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_covariance_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}




/************* Descriptive Statistics Tool *******************************
 *
 * Descriptive Statistics Tool calculates some useful statistical
 * information such as the mean, standard deviation, sample variance,
 * skewness, kurtosis, and standard error about the given variables.
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

typedef struct {
	gnm_float mean;
	gint       error_mean;
	gnm_float var;
	gint       error_var;
	gint      len;
} desc_stats_t;

static void
summary_statistics (data_analysis_output_t *dao,
		    analysis_tools_data_descriptive_t *info)
{
	guint     col;
	GSList *data = info->base.input;
	GnmFunc *fd_mean;
	GnmFunc *fd_median;
	GnmFunc *fd_mode;
	GnmFunc *fd_stdev;
	GnmFunc *fd_var;
	GnmFunc *fd_kurt;
	GnmFunc *fd_skew;
	GnmFunc *fd_min;
	GnmFunc *fd_max;
	GnmFunc *fd_sum;
	GnmFunc *fd_count;
	GnmFunc *fd_sqrt;

	fd_mean = gnm_func_lookup ("AVERAGE", NULL);
	gnm_func_ref (fd_mean);
	fd_median = gnm_func_lookup (info->use_ssmedian ? "SSMEDIAN" : "MEDIAN", NULL);
	gnm_func_ref (fd_median);
	fd_mode = gnm_func_lookup ("MODE", NULL);
	gnm_func_ref (fd_mode);
	fd_stdev = gnm_func_lookup ("STDEV", NULL);
	gnm_func_ref (fd_stdev);
	fd_var = gnm_func_lookup ("VAR", NULL);
	gnm_func_ref (fd_var);
	fd_kurt = gnm_func_lookup ("KURT", NULL);
	gnm_func_ref (fd_kurt);
	fd_skew = gnm_func_lookup ("SKEW", NULL);
	gnm_func_ref (fd_skew);
	fd_min = gnm_func_lookup ("MIN", NULL);
	gnm_func_ref (fd_min);
	fd_max = gnm_func_lookup ("MAX", NULL);
	gnm_func_ref (fd_max);
	fd_sum = gnm_func_lookup ("SUM", NULL);
	gnm_func_ref (fd_sum);
	fd_count = gnm_func_lookup ("COUNT", NULL);
	gnm_func_ref (fd_count);
	fd_sqrt = gnm_func_lookup ("SQRT", NULL);
	gnm_func_ref (fd_sqrt);

        dao_set_cell (dao, 0, 0, NULL);

	/*
	 * Note to translators: in the following string and others like it,
	 * the "/" is a separator character that can be changed to anything
	 * if the translation needs the slash; just use, say, "|" instead.
	 *
	 * The items are bundled like this to increase translation context.
	 */
        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Standard Error"
					"/Median"
					"/Mode"
					"/Standard Deviation"
					"/Sample Variance"
					"/Kurtosis"
					"/Skewness"
					"/Range"
					"/Minimum"
					"/Maximum"
					"/Sum"
					"/Count"));

	for (col = 0; data != NULL; data = data->next, col++) {
		GnmExpr const *expr;
		GnmExpr const *expr_min;
		GnmExpr const *expr_max;
		GnmExpr const *expr_var;
		GnmExpr const *expr_count;
		GnmValue *val_org = value_dup (data->data);

		/* Note that analysis_tools_write_label may modify val_org */
		analysis_tools_write_label (val_org, dao, &info->base, col + 1, 0, col + 1);
		dao_set_italic (dao, col + 1, 0, col+1, 0);

	        /* Mean */
		expr = gnm_expr_new_funcall1
			(fd_mean,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 1, expr);

		/* Standard Deviation */
		expr = gnm_expr_new_funcall1
			(fd_stdev,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 5, expr);

		/* Sample Variance */
		expr_var = gnm_expr_new_funcall1
			(fd_var,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 6, gnm_expr_copy (expr_var));

		/* Median */
		expr = gnm_expr_new_funcall1
			(fd_median,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 3, expr);

		/* Mode */
		expr = gnm_expr_new_funcall1
			(fd_mode,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 4, expr);

		/* Kurtosis */
		expr = gnm_expr_new_funcall1
			(fd_kurt,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 7, expr);

		/* Skewness */
		expr = gnm_expr_new_funcall1
			(fd_skew,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 8, expr);

		/* Minimum */
		expr_min = gnm_expr_new_funcall1
			(fd_min,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 10, gnm_expr_copy (expr_min));

		/* Maximum */
		expr_max = gnm_expr_new_funcall1
			(fd_max,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 11, gnm_expr_copy (expr_max));

		/* Range */
		expr = gnm_expr_new_binary (expr_max, GNM_EXPR_OP_SUB, expr_min);
		dao_set_cell_expr (dao, col + 1, 9, expr);

		/* Sum */
		expr = gnm_expr_new_funcall1
			(fd_sum,
			 gnm_expr_new_constant (value_dup (val_org)));
		dao_set_cell_expr (dao, col + 1, 12, expr);

		/* Count */
		expr_count = gnm_expr_new_funcall1
			(fd_count,
			 gnm_expr_new_constant (val_org));
		dao_set_cell_expr (dao, col + 1, 13, gnm_expr_copy (expr_count));

		/* Standard Error */
		expr = gnm_expr_new_funcall1
			(fd_sqrt,
			 gnm_expr_new_binary (expr_var,
					      GNM_EXPR_OP_DIV,
					      expr_count));
		dao_set_cell_expr (dao, col + 1, 2, expr);
	}

	gnm_func_unref (fd_mean);
	gnm_func_unref (fd_median);
	gnm_func_unref (fd_mode);
	gnm_func_unref (fd_stdev);
	gnm_func_unref (fd_var);
	gnm_func_unref (fd_kurt);
	gnm_func_unref (fd_skew);
	gnm_func_unref (fd_min);
	gnm_func_unref (fd_max);
	gnm_func_unref (fd_sum);
	gnm_func_unref (fd_count);
	gnm_func_unref (fd_sqrt);

	dao_autofit_columns (dao);
}

static void
confidence_level (data_analysis_output_t *dao,
		  analysis_tools_data_descriptive_t *info)
{
        guint col;
	char *buffer;
	char *format;
	GSList *data = info->base.input;
	GnmFunc *fd_mean;
	GnmFunc *fd_var;
	GnmFunc *fd_count;
	GnmFunc *fd_tinv;
	GnmFunc *fd_sqrt;

	format = g_strdup_printf (_("/%%%s%%%% CI for the Mean from"
				    "/to"), GNM_FORMAT_g);
	buffer = g_strdup_printf (format, info->c_level * 100);
	g_free (format);
	set_cell_text_col (dao, 0, 1, buffer);
        g_free (buffer);

        dao_set_cell (dao, 0, 0, NULL);

	fd_mean = gnm_func_lookup ("AVERAGE", NULL);
	gnm_func_ref (fd_mean);
	fd_var = gnm_func_lookup ("VAR", NULL);
	gnm_func_ref (fd_var);
	fd_count = gnm_func_lookup ("COUNT", NULL);
	gnm_func_ref (fd_count);
	fd_tinv = gnm_func_lookup ("TINV", NULL);
	gnm_func_ref (fd_tinv);
	fd_sqrt = gnm_func_lookup ("SQRT", NULL);
	gnm_func_ref (fd_sqrt);


	for (col = 0; data != NULL; data = data->next, col++) {
		GnmExpr const *expr;
		GnmExpr const *expr_mean;
		GnmExpr const *expr_var;
		GnmExpr const *expr_count;
		GnmValue *val_org = value_dup (data->data);

		/* Note that analysis_tools_write_label may modify val_org */
		analysis_tools_write_label (val_org, dao, &info->base, col + 1, 0, col + 1);
		dao_set_italic (dao, col+1, 0, col+1, 0);

		expr_mean = gnm_expr_new_funcall1
			(fd_mean,
			 gnm_expr_new_constant (value_dup (val_org)));

		expr_var = gnm_expr_new_funcall1
			(fd_var,
			 gnm_expr_new_constant (value_dup (val_org)));

		expr_count = gnm_expr_new_funcall1
			(fd_count,
			 gnm_expr_new_constant (val_org));

		expr = gnm_expr_new_binary
			(gnm_expr_new_funcall2
			 (fd_tinv,
			  gnm_expr_new_constant (value_new_float (1 - info->c_level)),
			  gnm_expr_new_binary
			  (gnm_expr_copy (expr_count),
			   GNM_EXPR_OP_SUB,
			   gnm_expr_new_constant (value_new_int (1)))),
			 GNM_EXPR_OP_MULT,
			 gnm_expr_new_funcall1
			 (fd_sqrt,
			  gnm_expr_new_binary (expr_var,
					       GNM_EXPR_OP_DIV,
					       expr_count)));

		dao_set_cell_expr (dao, col + 1, 1,
				   gnm_expr_new_binary
				   (gnm_expr_copy (expr_mean),
				    GNM_EXPR_OP_SUB,
				    gnm_expr_copy (expr)));
		dao_set_cell_expr (dao, col + 1, 2,
				   gnm_expr_new_binary (expr_mean,
							GNM_EXPR_OP_ADD,
							expr));
	}

	gnm_func_unref (fd_mean);
	gnm_func_unref (fd_var);
	gnm_func_unref (fd_count);
	gnm_func_unref (fd_tinv);
	gnm_func_unref (fd_sqrt);
}

static void
kth_smallest_largest (data_analysis_output_t *dao,
		      analysis_tools_data_descriptive_t *info,
		      char const* func, char const* label, int k)
{
        guint col;
	GSList *data = info->base.input;
	GnmFunc *fd = gnm_func_lookup (func, NULL);
	gnm_func_ref (fd);

        dao_set_cell_printf (dao, 0, 1, label, k);

        dao_set_cell (dao, 0, 0, NULL);

	for (col = 0; data != NULL; data = data->next, col++) {
		GnmExpr const *expr = NULL;
		GnmValue *val = value_dup (data->data);

		analysis_tools_write_label (val, dao, &info->base, col + 1, 0, col + 1);
		dao_set_italic (dao, col + 1, 0, col + 1, 0);

		expr = gnm_expr_new_funcall2
			(fd,
			 gnm_expr_new_constant (val),
			 gnm_expr_new_constant (value_new_int (k)));

		dao_set_cell_expr (dao, col + 1, 1, expr);
	}

	gnm_func_unref (fd);
}

/* Descriptive Statistics
 */
static gboolean
analysis_tool_descriptive_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_descriptive_t *info)
{
        if (info->summary_statistics) {
                summary_statistics (dao, info);
		dao->offset_row += 16;
		if (dao->rows <= dao->offset_row)
			goto finish_descriptive_tool;
	}
        if (info->confidence_level) {
                confidence_level (dao, info);
		dao->offset_row += 4;
		if (dao->rows <= dao->offset_row)
			goto finish_descriptive_tool;
	}
        if (info->kth_largest) {
		kth_smallest_largest (dao, info, "LARGE", _("Largest (%d)"),
				      info->k_largest);
		dao->offset_row += 4;
		if (dao->rows <= dao->offset_row)
			goto finish_descriptive_tool;
	}
        if (info->kth_smallest)
                kth_smallest_largest (dao, info, "SMALL", _("Smallest (%d)"),
				      info->k_smallest);

 finish_descriptive_tool:

	dao_redraw_respan (dao);
	return 0;
}

gboolean
analysis_tool_descriptive_engine (data_analysis_output_t *dao, gpointer specs,
				   analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_descriptive_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Descriptive Statistics (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		dao_adjust (dao, 1 + g_slist_length (info->base.input),
			    (info->summary_statistics ? 16 : 0) +
			    (info->confidence_level ? 4 : 0) +
			    (info->kth_largest ? 4 : 0) +
			    (info->kth_smallest ? 4 : 0 ) - 1);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Descriptive Statistics"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Descriptive Statistics"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_descriptive_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}



/************* Sampling Tool *********************************************
 *
 * Sampling tool takes a sample from a given data set.  Sample can be
 * a random sample where a given number of data points are selected
 * randomly from the data set.  The sample can also be a periodic
 * sample where, for example, every fourth data element is selected to
 * the sample.  The results are given in a table which can be printed
 * out in a new sheet, in a new workbook, or simply into an existing
 * sheet.
 *
 **/


static gboolean
analysis_tool_sampling_engine_run (data_analysis_output_t *dao,
					 analysis_tools_data_sampling_t *info)
{
	GPtrArray *data = NULL;

	guint i, j, data_len;
	guint n_sample;
	guint n_data;
	gnm_float x;

	data = new_data_set_list (info->base.input, info->base.group_by,
				  TRUE, info->base.labels, dao->sheet);

	for (n_data = 0; n_data < data->len; n_data++) {
		for (n_sample = 0; n_sample < info->number; n_sample++) {
			GArray * sample = g_array_new (FALSE, FALSE,
						       sizeof (gnm_float));
			GArray * this_data = g_array_new (FALSE, FALSE,
							  sizeof (gnm_float));
			data_set_t * this_data_set;

			this_data_set = g_ptr_array_index (data, n_data);
			data_len = this_data_set->data->len;

			dao_set_cell_printf (dao, 0, 0, this_data_set->label);
			dao_set_italic (dao, 0, 0, 0, 0);
			dao->offset_row = 1;

			g_array_set_size (this_data, data_len);
			g_memmove (this_data->data, this_data_set->data->data,
				   sizeof (gnm_float) * data_len);

			if (info->periodic) {
				if ((info->size < 0) || (info->size > data_len)) {
					destroy_data_set_list (data);
					gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->base.wbc),
						_("The requested sample size is too large for a periodic sample."));
					return TRUE;
				}
				for (i = info->size - 1; i < data_len; i += info->size) {
					x = g_array_index (this_data, gnm_float, i);
					g_array_append_val (sample, x);
				}
				write_data (dao, sample);
			} else {
				for (i = 0; i < info->size; i++) {
					guint random_index;

					if (0 == data_len)
						break;
					random_index = random_01 () * data_len;
					if (random_index == data_len)
						random_index--;
					x = g_array_index (this_data, gnm_float, random_index);
					g_array_remove_index_fast (this_data, random_index);
					g_array_append_val (sample, x);
					data_len--;
				}
				write_data (dao, sample);
				for (j = i; j < info->size; j++)
					dao_set_cell_na (dao, 0, j);
			}

			g_array_free (this_data, TRUE);
			g_array_free (sample, TRUE);
      			dao->offset_col++;
			dao->offset_row = 0;
		}
	}

	destroy_data_set_list (data);

	return FALSE;
}

gboolean
analysis_tool_sampling_engine (data_analysis_output_t *dao, gpointer specs,
			       analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_sampling_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Sampling (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		dao_adjust (dao, info->number * g_slist_length (info->base.input),
			    1 + info->size);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Sample"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Sample"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_sampling_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}



/************* z-Test: Two Sample for Means ******************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/


static gboolean
analysis_tool_ztest_engine_run (data_analysis_output_t *dao,
				analysis_tools_data_ttests_t *info)
{
	GnmValue *val_1;
	GnmValue *val_2;
	GnmFunc *fd_count;
	GnmFunc *fd_mean;
	GnmFunc *fd_normsdist;
	GnmFunc *fd_normsinv;
	GnmFunc *fd_abs;
	GnmFunc *fd_sqrt;
	GnmExpr const *expr_1;
	GnmExpr const *expr_2;
	GnmExpr const *expr_mean_1;
	GnmExpr const *expr_mean_2;
	GnmExpr const *expr_count_1;
	GnmExpr const *expr_count_2;

        dao_set_cell (dao, 0, 0, "");
        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Known Variance"
					"/Observations"
					"/Hypothesized Mean Difference"
					"/Observed Mean Difference"
					"/z"
					"/P (Z<=z) one-tail"
					"/z Critical one-tail"
					"/P (Z<=z) two-tail"
					"/z Critical two-tail"));

	fd_mean = gnm_func_lookup ("AVERAGE", NULL);
	gnm_func_ref (fd_mean);
	fd_normsdist = gnm_func_lookup ("NORMSDIST", NULL);
	gnm_func_ref (fd_normsdist);
	fd_abs = gnm_func_lookup ("ABS", NULL);
	gnm_func_ref (fd_abs);
	fd_sqrt = gnm_func_lookup ("SQRT", NULL);
	gnm_func_ref (fd_sqrt);
	fd_count = gnm_func_lookup ("COUNT", NULL);
	gnm_func_ref (fd_count);
	fd_normsinv = gnm_func_lookup ("NORMSINV", NULL);
	gnm_func_ref (fd_normsinv);

	val_1 = value_dup (info->base.range_1);
	expr_1 = gnm_expr_new_constant (value_dup (val_1));

	val_2 = value_dup (info->base.range_2);
	expr_2 = gnm_expr_new_constant (value_dup (val_2));

	/* Labels */
	analysis_tools_write_label_ftest (val_1, dao, 1, 0,
					  info->base.labels, 1);
	analysis_tools_write_label_ftest (val_2, dao, 2, 0,
					  info->base.labels, 2);


	/* Mean */
	expr_mean_1 = gnm_expr_new_funcall1 (fd_mean, gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 1, expr_mean_1);
	expr_mean_2 = gnm_expr_new_funcall1 (fd_mean, gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 1, gnm_expr_copy (expr_mean_2));

	/* Known Variance */
	dao_set_cell_float (dao, 1, 2, info->var1);
	dao_set_cell_float (dao, 2, 2, info->var2);

	/* Observations */
	expr_count_1 = gnm_expr_new_funcall1 (fd_count, expr_1);
	dao_set_cell_expr (dao, 1, 3, expr_count_1);
	expr_count_2 = gnm_expr_new_funcall1 (fd_count, expr_2);
	dao_set_cell_expr (dao, 2, 3, gnm_expr_copy (expr_count_2));

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 4, info->mean_diff);

	/* Observed Mean Difference */
	if (dao_cell_is_visible (dao, 2, 1)) {
		static const GnmCellRef mean_2 = {NULL, 1, -4, TRUE, TRUE};
		gnm_expr_free (expr_mean_2);
		expr_mean_2 = gnm_expr_new_cellref (&mean_2);
	}

	{
		static const GnmCellRef mean_1 = {NULL, 0, -4, TRUE, TRUE};
		dao_set_cell_expr (dao, 1, 5,
				   gnm_expr_new_binary
				   (gnm_expr_new_cellref (&mean_1),
				    GNM_EXPR_OP_SUB,
				    expr_mean_2));
	}

	/* z */
	{
		static const GnmCellRef mean_diff_hypo =
			{NULL, 0, -2, TRUE, TRUE};
		static const GnmCellRef mean_diff_observed =
			{NULL, 0, -1, TRUE, TRUE};

		GnmCellRef var_1 = {NULL, 0, -4, TRUE, TRUE};
		GnmCellRef count_1 = {NULL, 0, -3, TRUE, TRUE};
		GnmExpr const *expr_var_1 = gnm_expr_new_cellref (&var_1);
		GnmExpr const *expr_var_2 = NULL;
		GnmExpr const *expr_count_1 = gnm_expr_new_cellref (&count_1);
		GnmExpr const *expr_a = NULL;
		GnmExpr const *expr_b = NULL;
		GnmExpr const *expr_count_2_adj = NULL;

		if (dao_cell_is_visible (dao, 2,2)) {
			static const GnmCellRef var_2 =
				{NULL, 1, -4, TRUE, TRUE};
			expr_var_2 = gnm_expr_new_cellref (&var_2);
		} else {
			expr_var_2 = gnm_expr_new_constant
			(value_new_float (info->var2));
		}

		if (dao_cell_is_visible (dao, 2, 3)) {
			static const GnmCellRef count_2 =
				{NULL, 1, -3, TRUE, TRUE};
			gnm_expr_free (expr_count_2);
			expr_count_2_adj = gnm_expr_new_cellref (&count_2);
		} else
			expr_count_2_adj = expr_count_2;

		expr_a = gnm_expr_new_binary (expr_var_1, GNM_EXPR_OP_DIV,
					      expr_count_1);
		expr_b = gnm_expr_new_binary (expr_var_2, GNM_EXPR_OP_DIV,
					      expr_count_2_adj);

		dao_set_cell_expr (dao, 1, 6,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (gnm_expr_new_cellref
				     (&mean_diff_observed),
				     GNM_EXPR_OP_SUB,
				     gnm_expr_new_cellref
				     (&mean_diff_hypo)),
				    GNM_EXPR_OP_DIV,
				    gnm_expr_new_funcall1
				    (fd_sqrt,
				     gnm_expr_new_binary
				     (expr_a,
				      GNM_EXPR_OP_ADD,
				      expr_b))));
	}

	/* P (Z<=z) one-tail */
	{
		static const GnmCellRef cr = {NULL, 0, -1, TRUE, TRUE};

		/* FIXME: 1- looks like a bad idea.  */
		dao_set_cell_expr
			(dao, 1, 7,
			 gnm_expr_new_binary
			 (gnm_expr_new_constant (value_new_int (1)),
			  GNM_EXPR_OP_SUB,
			  gnm_expr_new_funcall1
			  (fd_normsdist,
			   gnm_expr_new_funcall1
			   (fd_abs,
			    gnm_expr_new_cellref (&cr)))));
	}


	/* Critical Z, one right tail */
	dao_set_cell_expr
		(dao, 1, 8,
		 gnm_expr_new_unary
		 (GNM_EXPR_OP_UNARY_NEG,
		  gnm_expr_new_funcall1
		  (fd_normsinv,
		   gnm_expr_new_constant
		   (value_new_float (info->base.alpha))
			  )));

	/* P (T<=t) two-tail */

	{
		static const GnmCellRef cr = {NULL, 0, -3, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 9,
			 gnm_expr_new_binary
			 (gnm_expr_new_constant (value_new_int (2)),
			  GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall1
			  (fd_normsdist,
			   gnm_expr_new_unary
			   (GNM_EXPR_OP_UNARY_NEG,
			    gnm_expr_new_funcall1
			    (fd_abs,
			     gnm_expr_new_cellref (&cr))))));
	}

	/* Critical Z, two tails */
	dao_set_cell_expr
		(dao, 1, 10,
		 gnm_expr_new_unary
		 (GNM_EXPR_OP_UNARY_NEG,
		  gnm_expr_new_funcall1
		  (fd_normsinv,
		   gnm_expr_new_binary
		   (gnm_expr_new_constant
		    (value_new_float (info->base.alpha)),
		    GNM_EXPR_OP_DIV,
		    gnm_expr_new_constant (value_new_int (2))))));

	gnm_func_unref (fd_mean);
	gnm_func_unref (fd_normsdist);
	gnm_func_unref (fd_abs);
	gnm_func_unref (fd_sqrt);
	gnm_func_unref (fd_count);
	gnm_func_unref (fd_normsinv);

	/* And finish up */

	dao_set_italic (dao, 0, 0, 0, 11);
	dao_set_italic (dao, 0, 0, 2, 0);

	value_release (val_1);
	value_release (val_2);

	dao_redraw_respan (dao);

        return FALSE;
}


gboolean
analysis_tool_ztest_engine (data_analysis_output_t *dao, gpointer specs,
			       analysis_tool_engine_t selector, gpointer result)
{
	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("z-Test (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		dao_adjust (dao, 3, 11);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_ftest_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("z-Test"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("z-Test"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_ztest_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}


/************* t-Test Tools ********************************************
 *
 * The t-Test tool set consists of three kinds of tests to test the
 * mean of two variables.  The tests are: Student's t-test for paired
 * sample, Student's t-test for two samples assuming equal variance
 * and the same test assuming unequal variance.  The results are given
 * in a table which can be printed out in a new sheet, in a new
 * workbook, or simply into an existing sheet.
 *
 **/

/* t-Test: Paired Two Sample for Means.
 */
static gboolean
analysis_tool_ttest_paired_engine_run (data_analysis_output_t *dao,
				       analysis_tools_data_ttests_t *info)
{
	GnmValue *val_1;
	GnmValue *val_2;
	GnmFunc *fd_count;
	GnmFunc *fd_mean;
	GnmFunc *fd_var;
	GnmFunc *fd_tdist;
	GnmFunc *fd_abs;
	GnmFunc *fd_tinv;
	GnmFunc *fd_correl;
	GnmExpr const *expr_1;
	GnmExpr const *expr_2;
	GnmExpr const *expr_diff;

        dao_set_cell (dao, 0, 0, "");
        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/Pearson Correlation"
					"/Hypothesized Mean Difference"
					"/Observed Mean Difference"
					"/Variance of the Differences"
					"/df"
					"/t Stat"
					"/P (T<=t) one-tail"
					"/t Critical one-tail"
					"/P (T<=t) two-tail"
					"/t Critical two-tail"));

	fd_mean = gnm_func_lookup ("AVERAGE", NULL);
	gnm_func_ref (fd_mean);
	fd_var = gnm_func_lookup ("VAR", NULL);
	gnm_func_ref (fd_var);
	fd_count = gnm_func_lookup ("COUNT", NULL);
	gnm_func_ref (fd_count);
	fd_correl = gnm_func_lookup ("CORREL", NULL);
	gnm_func_ref (fd_correl);
	fd_tinv = gnm_func_lookup ("TINV", NULL);
	gnm_func_ref (fd_tinv);
	fd_tdist = gnm_func_lookup ("TDIST", NULL);
	gnm_func_ref (fd_tdist);
	fd_abs = gnm_func_lookup ("ABS", NULL);
	gnm_func_ref (fd_abs);

	val_1 = value_dup (info->base.range_1);
	val_2 = value_dup (info->base.range_2);

	/* Labels */
	analysis_tools_write_label_ftest (val_1, dao, 1, 0,
					  info->base.labels, 1);
	analysis_tools_write_label_ftest (val_2, dao, 2, 0,
					  info->base.labels, 2);

	/* Mean */

	expr_1 = gnm_expr_new_constant (value_dup (val_1));
	dao_set_cell_expr (dao, 1, 1,
			   gnm_expr_new_funcall1 (fd_mean,
						  gnm_expr_copy (expr_1)));

	expr_2 = gnm_expr_new_constant (value_dup (val_2));
	dao_set_cell_expr (dao, 2, 1,
			   gnm_expr_new_funcall1 (fd_mean,
						  gnm_expr_copy (expr_2)));

	/* Variance */
	dao_set_cell_expr (dao, 1, 2,
			   gnm_expr_new_funcall1 (fd_var,
						  gnm_expr_copy (expr_1)));
	dao_set_cell_expr (dao, 2, 2,
			   gnm_expr_new_funcall1 (fd_var,
						  gnm_expr_copy (expr_2)));

	/* Observations */
	dao_set_cell_expr (dao, 1, 3,
			   gnm_expr_new_funcall1 (fd_count,
						  gnm_expr_copy (expr_1)));
	dao_set_cell_expr (dao, 2, 3,
			   gnm_expr_new_funcall1 (fd_count,
						  gnm_expr_copy (expr_2)));

	/* Pearson Correlation */
	dao_set_cell_expr (dao, 1, 4,
			   gnm_expr_new_funcall2 (fd_correl,
						  gnm_expr_copy (expr_1),
						  gnm_expr_copy (expr_2)));

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 5, info->mean_diff);

	/* Observed Mean Difference */
	expr_diff = gnm_expr_new_binary (expr_1, GNM_EXPR_OP_SUB, expr_2);
	dao_set_cell_array_expr (dao, 1, 6,
				 gnm_expr_new_funcall1 (fd_mean,
							gnm_expr_copy (expr_diff)));

	/* Variance of the Differences */
	dao_set_cell_array_expr (dao, 1, 7,
				 gnm_expr_new_funcall1 (fd_var,
							gnm_expr_copy (expr_diff)));

	/* df */
	dao_set_cell_array_expr (dao, 1, 8,
				 gnm_expr_new_binary
				 (gnm_expr_new_funcall1 (fd_count, expr_diff),
				  GNM_EXPR_OP_SUB,
				  gnm_expr_new_constant (value_new_int (1))));
	
	/* t */
	/* E24 = (E21-E20)/(E22/(E23+1))^0.5 */
	{
		GnmCellRef cr_1 = {NULL, 0, -3, TRUE, TRUE};
		GnmCellRef cr_2 = {NULL, 0, -4, TRUE, TRUE};
		GnmExpr const *expr_num;
		GnmExpr const *expr_denom;

		expr_num = gnm_expr_new_binary (
			gnm_expr_new_cellref (&cr_1),
			GNM_EXPR_OP_SUB,
			gnm_expr_new_cellref (&cr_2));

		cr_1.row = -2;
		cr_2.row = -1;

		expr_denom = gnm_expr_new_binary
			(gnm_expr_new_binary
			 (gnm_expr_new_cellref (&cr_1),
			  GNM_EXPR_OP_DIV,
			  gnm_expr_new_binary
			  (gnm_expr_new_cellref (&cr_2),
			   GNM_EXPR_OP_ADD,
			   gnm_expr_new_constant
			   (value_new_int (1)))),
			 GNM_EXPR_OP_EXP,
			 gnm_expr_new_constant
			 (value_new_float (0.5)));

		dao_set_cell_expr (dao, 1, 9,
				   gnm_expr_new_binary
				   (expr_num, GNM_EXPR_OP_DIV, expr_denom));
	}

	/* P (T<=t) one-tail */
	{
		static const GnmCellRef cr1 = {NULL, 0, -1, TRUE, TRUE};
		static const GnmCellRef cr2 = {NULL, 0, -2, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 10,
			 gnm_expr_new_funcall3
			 (fd_tdist,
			  gnm_expr_new_funcall1
			  (fd_abs,
			   gnm_expr_new_cellref (&cr1)),
			  gnm_expr_new_cellref (&cr2),
			  gnm_expr_new_constant (value_new_int (1))));
	}

	/* t Critical one-tail */
	{
		static const GnmCellRef cr = {NULL, 0, -3, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 11,
			 gnm_expr_new_funcall2
			 (fd_tinv,
			  gnm_expr_new_binary
			  (gnm_expr_new_constant (value_new_int (2)),
			   GNM_EXPR_OP_MULT,
			   gnm_expr_new_constant
			   (value_new_float (info->base.alpha))),
			  gnm_expr_new_cellref (&cr)));
	}

	/* P (T<=t) two-tail */

	{
		static const GnmCellRef cr1 = {NULL, 0, -3, TRUE, TRUE};
		static const GnmCellRef cr2 = {NULL, 0, -4, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 12,
			 gnm_expr_new_funcall3
			 (fd_tdist,
			  gnm_expr_new_funcall1 (fd_abs,
						 gnm_expr_new_cellref (&cr1)),
			  gnm_expr_new_cellref (&cr2),
			  gnm_expr_new_constant (value_new_int (2))));
	}

	/* t Critical two-tail */
	{
		static const GnmCellRef cr = {NULL, 0, -5, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 13,
			 gnm_expr_new_funcall2
			 (fd_tinv,
			  gnm_expr_new_constant
			  (value_new_float (info->base.alpha)),
			  gnm_expr_new_cellref (&cr)));
	}

	/* And finish up */

	dao_set_italic (dao, 0, 0, 0, 13);
	dao_set_italic (dao, 0, 0, 2, 0);

	value_release (val_1);
	value_release (val_2);

	gnm_func_unref (fd_count);
	gnm_func_unref (fd_correl);
	gnm_func_unref (fd_mean);
	gnm_func_unref (fd_var);
	gnm_func_unref (fd_tinv);
	gnm_func_unref (fd_tdist);
	gnm_func_unref (fd_abs);

	dao_redraw_respan (dao);

	return FALSE;
}

gboolean
analysis_tool_ttest_paired_engine (data_analysis_output_t *dao, gpointer specs,
				  analysis_tool_engine_t selector, gpointer result)
{
	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("t-Test, paired (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		dao_adjust (dao, 3, 14);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_ftest_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("t-Test"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("t-Test"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_ttest_paired_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}




/* t-Test: Two-Sample Assuming Equal Variances.
 */
static gboolean
analysis_tool_ttest_eqvar_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_ttests_t *info)
{
	GnmValue *val_1;
	GnmValue *val_2;
	GnmFunc *fd_count;
	GnmFunc *fd_mean;
	GnmFunc *fd_var;
	GnmFunc *fd_tdist;
	GnmFunc *fd_abs;
	GnmFunc *fd_tinv;
	GnmExpr const *expr_1;
	GnmExpr const *expr_2;
	GnmExpr const *expr_mean_1;
	GnmExpr const *expr_mean_2;
	GnmExpr const *expr_var_1;
	GnmExpr const *expr_var_2;
	GnmExpr const *expr_count_1;
	GnmExpr const *expr_count_2;

        dao_set_cell (dao, 0, 0, "");
	set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/Pooled Variance"
					"/Hypothesized Mean Difference"
					"/Observed Mean Difference"
					"/df"
					"/t Stat"
					"/P (T<=t) one-tail"
					"/t Critical one-tail"
					"/P (T<=t) two-tail"
					"/t Critical two-tail"));


	val_1 = value_dup (info->base.range_1);
	val_2 = value_dup (info->base.range_2);

	fd_mean = gnm_func_lookup ("AVERAGE", NULL);
	gnm_func_ref (fd_mean);
	fd_count = gnm_func_lookup ("COUNT", NULL);
	gnm_func_ref (fd_count);
	fd_var = gnm_func_lookup ("VAR", NULL);
	gnm_func_ref (fd_var);
	fd_tdist = gnm_func_lookup ("TDIST", NULL);
	gnm_func_ref (fd_tdist);
	fd_abs = gnm_func_lookup ("ABS", NULL);
	gnm_func_ref (fd_abs);
	fd_tinv = gnm_func_lookup ("TINV", NULL);
	gnm_func_ref (fd_tinv);

	/* Labels */
	analysis_tools_write_label_ftest (val_1, dao, 1, 0,
					  info->base.labels, 1);
	analysis_tools_write_label_ftest (val_2, dao, 2, 0,
					  info->base.labels, 2);


	/* Mean */
	expr_1 = gnm_expr_new_constant (value_dup (val_1));
	expr_mean_1 = gnm_expr_new_funcall1 (fd_mean,
					     gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 1, expr_mean_1);
	expr_2 = gnm_expr_new_constant (value_dup (val_2));
	expr_mean_2 = gnm_expr_new_funcall1 (fd_mean,
					     gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 1, gnm_expr_copy (expr_mean_2));

	/* Variance */
	expr_var_1 = gnm_expr_new_funcall1 (fd_var, gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 2, expr_var_1);
	expr_var_2 = gnm_expr_new_funcall1 (fd_var, gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 2, gnm_expr_copy (expr_var_2));

	/* Observations */
	expr_count_1 = gnm_expr_new_funcall1 (fd_count, expr_1);
	dao_set_cell_expr (dao, 1, 3, expr_count_1);
	expr_count_2 = gnm_expr_new_funcall1 (fd_count, expr_2);
       	dao_set_cell_expr (dao, 2, 3, gnm_expr_copy (expr_count_2));

        /* Pooled Variance */
	{
		GnmExpr const *expr_var_2_adj = NULL;
		GnmExpr const *expr_count_2_adj = NULL;
		GnmCellRef var_1 = {NULL, 0, -2, TRUE, TRUE};
		GnmCellRef count_1 = {NULL, 0, -1, TRUE, TRUE};
		GnmExpr const *expr_var_1 = gnm_expr_new_cellref (&var_1);
		GnmExpr const *expr_count_1 = gnm_expr_new_cellref (&count_1);
		GnmExpr const *expr_one = gnm_expr_new_constant
			(value_new_int (1));
		GnmExpr const *expr_count_1_minus_1;
		GnmExpr const *expr_count_2_minus_1;

		if (dao_cell_is_visible (dao, 2,2)) {
			static const GnmCellRef var_2 =
				{NULL, 1, -2, TRUE, TRUE};
			gnm_expr_free (expr_var_2);
			expr_var_2_adj = gnm_expr_new_cellref (&var_2);
		} else
			expr_var_2_adj = expr_var_2;
		if (dao_cell_is_visible (dao, 2,3)) {
			static const GnmCellRef count_2 =
				{NULL, 1, -1, TRUE, TRUE};
			expr_count_2_adj = gnm_expr_new_cellref (&count_2);
		} else
			expr_count_2_adj = gnm_expr_copy (expr_count_2);

		expr_count_1_minus_1 = gnm_expr_new_binary
			(expr_count_1,
			 GNM_EXPR_OP_SUB,
			 gnm_expr_copy (expr_one));
		expr_count_2_minus_1 = gnm_expr_new_binary
			(expr_count_2_adj, GNM_EXPR_OP_SUB, expr_one);

		dao_set_cell_expr (dao, 1, 4,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (gnm_expr_new_binary
				     (gnm_expr_copy (expr_count_1_minus_1),
				      GNM_EXPR_OP_MULT,
				      expr_var_1),
				     GNM_EXPR_OP_ADD,
				     gnm_expr_new_binary
				     (gnm_expr_copy (expr_count_2_minus_1),
				      GNM_EXPR_OP_MULT,
				      expr_var_2_adj)),
				    GNM_EXPR_OP_DIV,
				    gnm_expr_new_binary
				    (expr_count_1_minus_1,
				     GNM_EXPR_OP_ADD,
				     expr_count_2_minus_1)));

	}

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 5, info->mean_diff);

	/* Observed Mean Difference */
	if (dao_cell_is_visible (dao, 2,1)) {
		static const GnmCellRef mean_2 = {NULL, 1, -5, TRUE, TRUE};
		gnm_expr_free (expr_mean_2);
		expr_mean_2 = gnm_expr_new_cellref (&mean_2);
	}
	{
		static const GnmCellRef mean_1 = {NULL, 0, -5, TRUE, TRUE};

		dao_set_cell_expr (dao, 1, 6,
				   gnm_expr_new_binary
				   (gnm_expr_new_cellref (&mean_1),
				    GNM_EXPR_OP_SUB,
				    expr_mean_2));
	}

	/* df */
	{
		static const GnmCellRef count_1 = {NULL, 0, -4, TRUE, TRUE};
		GnmExpr const *expr_count_1 = gnm_expr_new_cellref (&count_1);
		GnmExpr const *expr_count_2_adj;
		GnmExpr const *expr_two = gnm_expr_new_constant
			(value_new_int (2));

		if (dao_cell_is_visible (dao, 2,3)) {
			GnmCellRef count_2 = {NULL, 1, -4, TRUE, TRUE};
			expr_count_2_adj = gnm_expr_new_cellref (&count_2);
		} else
			expr_count_2_adj = gnm_expr_copy (expr_count_2);

		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (expr_count_1,
				     GNM_EXPR_OP_ADD,
				     expr_count_2_adj),
				    GNM_EXPR_OP_SUB,
				    expr_two));
	}

	/* t */
	{
		GnmCellRef mean_diff_hypo = {NULL, 0, -3, TRUE, TRUE};
		GnmCellRef mean_diff_observed = {NULL, 0, -2, TRUE, TRUE};

		GnmCellRef var = {NULL, 0, -4, TRUE, TRUE};
		GnmCellRef count_1 = {NULL, 0, -5, TRUE, TRUE};
		GnmExpr const *expr_var = gnm_expr_new_cellref (&var);
		GnmExpr const *expr_count_1 = gnm_expr_new_cellref (&count_1);
		GnmExpr const *expr_a;
		GnmExpr const *expr_b;
		GnmExpr const *expr_count_2_adj;

		if (dao_cell_is_visible (dao, 2,3)) {
			GnmCellRef count_2 = {NULL, 1, -5, TRUE, TRUE};
			gnm_expr_free (expr_count_2);
			expr_count_2_adj = gnm_expr_new_cellref (&count_2);
		} else
			expr_count_2_adj = expr_count_2;

		expr_a = gnm_expr_new_binary (gnm_expr_copy (expr_var),
					      GNM_EXPR_OP_DIV,
					      expr_count_1);
		expr_b = gnm_expr_new_binary (expr_var,
					      GNM_EXPR_OP_DIV,
					      expr_count_2_adj);

		dao_set_cell_expr (dao, 1, 8,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (gnm_expr_new_cellref
				     (&mean_diff_observed),
				     GNM_EXPR_OP_SUB,
				     gnm_expr_new_cellref
				     (&mean_diff_hypo)),
				    GNM_EXPR_OP_DIV,
				    gnm_expr_new_binary
					     (gnm_expr_new_binary
					      (expr_a,
					       GNM_EXPR_OP_ADD,
					       expr_b),
					      GNM_EXPR_OP_EXP,
					      gnm_expr_new_constant
					      (value_new_float (0.5)))));

	}

	/* P (T<=t) one-tail */

	{
		static const GnmCellRef cr1 = {NULL, 0, -1, TRUE, TRUE};
		static const GnmCellRef cr2 = {NULL, 0, -2, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 9,
			 gnm_expr_new_funcall3
			 (fd_tdist,
			  gnm_expr_new_funcall1
			  (fd_abs,
			   gnm_expr_new_cellref (&cr1)),
			  gnm_expr_new_cellref (&cr2),
			  gnm_expr_new_constant (value_new_int (1))));
	}

	/* t Critical one-tail */
	{
		static const GnmCellRef cr = {NULL, 0, -3, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 10,
			 gnm_expr_new_funcall2
			 (fd_tinv,
			  gnm_expr_new_binary
			  (gnm_expr_new_constant (value_new_int (2)),
			   GNM_EXPR_OP_MULT,
			   gnm_expr_new_constant
			   (value_new_float (info->base.alpha))),
			  gnm_expr_new_cellref (&cr)));
	}

	/* P (T<=t) two-tail */

	{
		static const GnmCellRef cr1 = {NULL, 0, -3, TRUE, TRUE};
		static const GnmCellRef cr2 = {NULL, 0, -4, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 11,
			 gnm_expr_new_funcall3
			 (fd_tdist,
			  gnm_expr_new_funcall1
			  (fd_abs,
			   gnm_expr_new_cellref (&cr1)),
			  gnm_expr_new_cellref (&cr2),
			  gnm_expr_new_constant (value_new_int (2))));
	}

	/* t Critical two-tail */

	{
		static const GnmCellRef cr = {NULL, 0, -5, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 12,
			 gnm_expr_new_funcall2
			 (fd_tinv,
			  gnm_expr_new_constant
			  (value_new_float (info->base.alpha)),
			  gnm_expr_new_cellref (&cr)));
	}

	/* And finish up */

	dao_set_italic (dao, 0, 0, 0, 12);
	dao_set_italic (dao, 0, 0, 2, 0);

	value_release (val_1);
	value_release (val_2);

	gnm_func_unref (fd_mean);	
	gnm_func_unref (fd_var);
	gnm_func_unref (fd_count);
	gnm_func_unref (fd_tdist);
	gnm_func_unref (fd_abs);
	gnm_func_unref (fd_tinv);

	dao_redraw_respan (dao);

	return FALSE;
}

gboolean
analysis_tool_ttest_eqvar_engine (data_analysis_output_t *dao, gpointer specs,
				  analysis_tool_engine_t selector, gpointer result)
{
	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("t-Test (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		dao_adjust (dao, 3, 13);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_ftest_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("t-Test"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("t-Test"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_ttest_eqvar_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}

/* t-Test: Two-Sample Assuming Unequal Variances.
 */
static gboolean
analysis_tool_ttest_neqvar_engine_run (data_analysis_output_t *dao,
				analysis_tools_data_ttests_t *info)
{
	GnmValue *val_1;
	GnmValue *val_2;
	GnmFunc *fd_count;
	GnmFunc *fd_mean;
	GnmFunc *fd_var;
	GnmFunc *fd_tdist;
	GnmFunc *fd_abs;
	GnmFunc *fd_tinv;
	GnmExpr const *expr_1;
	GnmExpr const *expr_2;
	GnmExpr const *expr_mean_1;
	GnmExpr const *expr_mean_2;
	GnmExpr const *expr_var_1;
	GnmExpr const *expr_var_2;
	GnmExpr const *expr_count_1;
	GnmExpr const *expr_count_2;

        dao_set_cell (dao, 0, 0, "");
        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/Hypothesized Mean Difference"
					"/Observed Mean Difference"
					"/df"
					"/t Stat"
					"/P (T<=t) one-tail"
					"/t Critical one-tail"
					"/P (T<=t) two-tail"
					"/t Critical two-tail"));


	val_1 = value_dup (info->base.range_1);
	val_2 = value_dup (info->base.range_2);

	fd_mean = gnm_func_lookup ("AVERAGE", NULL);
	gnm_func_ref (fd_mean);
	fd_var = gnm_func_lookup ("VAR", NULL);
	gnm_func_ref (fd_var);
	fd_count = gnm_func_lookup ("COUNT", NULL);
	gnm_func_ref (fd_count);
	fd_tdist = gnm_func_lookup ("TDIST", NULL);
	gnm_func_ref (fd_tdist);
	fd_abs = gnm_func_lookup ("ABS", NULL);
	gnm_func_ref (fd_abs);
	fd_tinv = gnm_func_lookup ("TINV", NULL);
	gnm_func_ref (fd_tinv);

	/* Labels */
	analysis_tools_write_label_ftest (val_1, dao, 1, 0,
					  info->base.labels, 1);
	analysis_tools_write_label_ftest (val_2, dao, 2, 0,
					  info->base.labels, 2);


	/* Mean */
	expr_1 = gnm_expr_new_constant (value_dup (val_1));
	expr_mean_1 = gnm_expr_new_funcall1 (fd_mean, gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 1, expr_mean_1);
	expr_2 = gnm_expr_new_constant (value_dup (val_2));
	expr_mean_2 = gnm_expr_new_funcall1 (fd_mean, gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 1, gnm_expr_copy (expr_mean_2));

	/* Variance */
	expr_var_1 = gnm_expr_new_funcall1 (fd_var, gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 2, expr_var_1);
	expr_var_2 = gnm_expr_new_funcall1 (fd_var, gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 2, gnm_expr_copy (expr_var_2));

	/* Observations */
	expr_count_1 = gnm_expr_new_funcall1 (fd_count, expr_1);
	dao_set_cell_expr (dao, 1, 3, expr_count_1);
	expr_count_2 = gnm_expr_new_funcall1 (fd_count, expr_2);
	dao_set_cell_expr (dao, 2, 3, gnm_expr_copy (expr_count_2));

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 4, info->mean_diff);

	/* Observed Mean Difference */
	if (dao_cell_is_visible (dao, 2,1)) {
		static const GnmCellRef mean_2 = {NULL, 1, -4, TRUE, TRUE};
		gnm_expr_free (expr_mean_2);
		expr_mean_2 = gnm_expr_new_cellref (&mean_2);
	}
	{
		static const GnmCellRef mean_1 = {NULL, 0, -4, TRUE, TRUE};

		dao_set_cell_expr (dao, 1, 5,
				   gnm_expr_new_binary
				   (gnm_expr_new_cellref (&mean_1),
				    GNM_EXPR_OP_SUB,
				    expr_mean_2));
	}

	/* df */

	{
		static const GnmCellRef var_1 = {NULL, 0, -4, TRUE, TRUE};
		static const GnmCellRef count_1 = {NULL, 0, -3, TRUE, TRUE};
		GnmExpr const *expr_var_1 = gnm_expr_new_cellref (&var_1);
		GnmExpr const *expr_count_1 = gnm_expr_new_cellref (&count_1);
		GnmExpr const *expr_a;
		GnmExpr const *expr_b;
		GnmExpr const *expr_var_2_adj;
		GnmExpr const *expr_count_2_adj;
		GnmExpr const *expr_two = gnm_expr_new_constant
			(value_new_int (2));
		GnmExpr const *expr_one = gnm_expr_new_constant
			(value_new_int (1));

		if (dao_cell_is_visible (dao, 2,2)) {
			static const GnmCellRef var_2 =
				{NULL, 1, -4, TRUE, TRUE};
			expr_var_2_adj = gnm_expr_new_cellref (&var_2);
		} else
			expr_var_2_adj = gnm_expr_copy (expr_var_2);

		if (dao_cell_is_visible (dao, 2,3)) {
			static const GnmCellRef count_2 =
				{NULL, 1, -3, TRUE, TRUE};
			expr_count_2_adj = gnm_expr_new_cellref (&count_2);
		} else
			expr_count_2_adj = gnm_expr_copy (expr_count_2);

		expr_a = gnm_expr_new_binary (expr_var_1,
					      GNM_EXPR_OP_DIV,
					      gnm_expr_copy (expr_count_1));
		expr_b = gnm_expr_new_binary (expr_var_2_adj,
					      GNM_EXPR_OP_DIV,
					      gnm_expr_copy (expr_count_2_adj));

		dao_set_cell_expr (dao, 1, 6,
				   gnm_expr_new_binary (
					   gnm_expr_new_binary
					   (gnm_expr_new_binary
					    (gnm_expr_copy (expr_a),
					     GNM_EXPR_OP_ADD,
					     gnm_expr_copy (expr_b)),
					    GNM_EXPR_OP_EXP,
					    gnm_expr_copy (expr_two)),
					   GNM_EXPR_OP_DIV,
					   gnm_expr_new_binary
					   (gnm_expr_new_binary
					    (gnm_expr_new_binary
					     (expr_a,
					      GNM_EXPR_OP_EXP,
					      gnm_expr_copy (expr_two)),
					     GNM_EXPR_OP_DIV,
					     gnm_expr_new_binary
					     (expr_count_1,
					      GNM_EXPR_OP_SUB,
					      gnm_expr_copy (expr_one))),
					    GNM_EXPR_OP_ADD,
					    gnm_expr_new_binary
					    (gnm_expr_new_binary
					     (expr_b,
					      GNM_EXPR_OP_EXP,
					      expr_two),
					     GNM_EXPR_OP_DIV,
					     gnm_expr_new_binary
					     (expr_count_2_adj,
					      GNM_EXPR_OP_SUB,
					      expr_one)))));
	}

	/* t */

	{
		static const GnmCellRef mean_diff_hypo =
			{NULL, 0, -3, TRUE, TRUE};
		static const GnmCellRef mean_diff_observed =
			{NULL, 0, -2, TRUE, TRUE};
		static const GnmCellRef var_1 = {NULL, 0, -5, TRUE, TRUE};
		static const GnmCellRef count_1 = {NULL, 0, -4, TRUE, TRUE};

		GnmExpr const *expr_var_1 = gnm_expr_new_cellref (&var_1);
		GnmExpr const *expr_count_1 = gnm_expr_new_cellref (&count_1);
		GnmExpr const *expr_a;
		GnmExpr const *expr_b;
		GnmExpr const *expr_var_2_adj;
		GnmExpr const *expr_count_2_adj;

		if (dao_cell_is_visible (dao, 2,2)) {
			static const GnmCellRef var_2 =
				{NULL, 1, -5, TRUE, TRUE};
			gnm_expr_free (expr_var_2);
			expr_var_2_adj = gnm_expr_new_cellref (&var_2);
		} else
			expr_var_2_adj = expr_var_2;
		if (dao_cell_is_visible (dao, 2,3)) {
			static const GnmCellRef count_2 =
				{NULL, 1, -4, TRUE, TRUE};
			gnm_expr_free (expr_count_2);
			expr_count_2_adj = gnm_expr_new_cellref (&count_2);
		} else
			expr_count_2_adj = expr_count_2;

		expr_a = gnm_expr_new_binary (expr_var_1, GNM_EXPR_OP_DIV,
					      expr_count_1);
		expr_b = gnm_expr_new_binary (expr_var_2_adj, GNM_EXPR_OP_DIV,
					      expr_count_2_adj);

		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (gnm_expr_new_cellref
				     (&mean_diff_observed),
				     GNM_EXPR_OP_SUB,
				     gnm_expr_new_cellref
				     (&mean_diff_hypo)),
				    GNM_EXPR_OP_DIV,
				    gnm_expr_new_binary
					     (gnm_expr_new_binary
					      (expr_a,
					       GNM_EXPR_OP_ADD,
					       expr_b),
					      GNM_EXPR_OP_EXP,
					      gnm_expr_new_constant
					      (value_new_float (0.5)))));

	}

	/* P (T<=t) one-tail */
	/* I9: =tdist(abs(Sheet1!I8),Sheet1!I7,1) */

	{
		static const GnmCellRef cr1 = {NULL, 0, -1, TRUE, TRUE};
		static const GnmCellRef cr2 = {NULL, 0, -2, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 8,
			 gnm_expr_new_funcall3
			 (fd_tdist,
			  gnm_expr_new_funcall1 (fd_abs,
						 gnm_expr_new_cellref (&cr1)),
			  gnm_expr_new_cellref (&cr2),
			  gnm_expr_new_constant (value_new_int (1))));
	}

	/* t Critical one-tail */
        /* H10 = tinv(2*alpha,Sheet1!H7) */
	{
		static const GnmCellRef cr = {NULL, 0, -3, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 9,
			 gnm_expr_new_funcall2
			 (fd_tinv,
			  gnm_expr_new_binary
			  (gnm_expr_new_constant (value_new_int (2)),
			   GNM_EXPR_OP_MULT,
			   gnm_expr_new_constant
			   (value_new_float (info->base.alpha))),
			  gnm_expr_new_cellref (&cr)));
	}

	/* P (T<=t) two-tail */
	/* I11: =tdist(abs(Sheet1!I8),Sheet1!I7,1) */
	{
		static const GnmCellRef cr1 = {NULL, 0, -3, TRUE, TRUE};
		static const GnmCellRef cr2 = {NULL, 0, -4, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 10,
			 gnm_expr_new_funcall3
			 (fd_tdist,
			  gnm_expr_new_funcall1 (fd_abs,
						 gnm_expr_new_cellref (&cr1)),
			  gnm_expr_new_cellref (&cr2),
			  gnm_expr_new_constant (value_new_int (2))));
	}

	/* t Critical two-tail */
	{
		static const GnmCellRef cr = {NULL, 0, -5, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 1, 11,
			 gnm_expr_new_funcall2
			 (fd_tinv,
			  gnm_expr_new_constant
			  (value_new_float (info->base.alpha)),
			  gnm_expr_new_cellref (&cr)));
	}

	/* And finish up */

	gnm_func_unref (fd_mean);
	gnm_func_unref (fd_var);
	gnm_func_unref (fd_count);
	gnm_func_unref (fd_tdist);
	gnm_func_unref (fd_abs);
	gnm_func_unref (fd_tinv);

	dao_set_italic (dao, 0, 0, 0, 11);
	dao_set_italic (dao, 0, 0, 2, 0);

	value_release (val_1);
	value_release (val_2);

	dao_redraw_respan (dao);
	return FALSE;
}

gboolean
analysis_tool_ttest_neqvar_engine (data_analysis_output_t *dao, gpointer specs,
				  analysis_tool_engine_t selector, gpointer result)
{
	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("t-Test (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		dao_adjust (dao, 3, 12);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_ftest_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("t-Test"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("t-Test"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_ttest_neqvar_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}


/************* F-Test Tool *********************************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/


/* F-Test: Two-Sample for Variances
 */
static gboolean
analysis_tool_ftest_engine_run (data_analysis_output_t *dao,
				analysis_tools_data_ftest_t *info)
{
	GnmValue *val_1 = value_dup (info->range_1);
	GnmValue *val_2 = value_dup (info->range_2);
	GnmExpr const *expr;
	GnmExpr const *expr_var_denum;
	GnmExpr const *expr_count_denum;
	GnmExpr const *expr_df_denum = NULL;

	GnmFunc *fd_finv;

	fd_finv = gnm_func_lookup ("FINV", NULL);
	gnm_func_ref (fd_finv);

	dao_set_cell (dao, 0, 0, _("F-Test"));

	set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/df"
					"/F"
					"/P (F<=f) right-tail"
					"/F Critical right-tail"
					"/P (f<=F) left-tail"
					"/F Critical left-tail"
					"/P two-tail"
					"/F Critical two-tail"));
	dao_set_italic (dao, 0, 0, 0, 11);

	/* Label */
	analysis_tools_write_label_ftest (val_1, dao, 1, 0, info->labels, 1);
	analysis_tools_write_label_ftest (val_2, dao, 2, 0, info->labels, 2);
	dao_set_italic (dao, 0, 0, 2, 0);

	/* Mean */
	{
		GnmFunc *fd_mean = gnm_func_lookup ("AVERAGE", NULL);
		gnm_func_ref (fd_mean);

		dao_set_cell_expr
			(dao, 1, 1,
			 gnm_expr_new_funcall1
			 (fd_mean,
			  gnm_expr_new_constant (value_dup (val_1))));

		dao_set_cell_expr
			(dao, 2, 1,
			 gnm_expr_new_funcall1
			 (fd_mean,
			  gnm_expr_new_constant (value_dup (val_2))));

		gnm_func_unref (fd_mean);
	}

	/* Variance */
	{
		GnmFunc *fd_var = gnm_func_lookup ("VAR", NULL);
		gnm_func_ref (fd_var);

		dao_set_cell_expr
			(dao, 1, 2,
			 gnm_expr_new_funcall1
			 (fd_var,
			  gnm_expr_new_constant (value_dup (val_1))));

		expr_var_denum = gnm_expr_new_funcall1
			(fd_var,
			 gnm_expr_new_constant (value_dup (val_2)));
		dao_set_cell_expr (dao, 2, 2, gnm_expr_copy (expr_var_denum));

		gnm_func_unref (fd_var);
	}

        /* Count */
	{
		GnmFunc *fd_count = gnm_func_lookup ("COUNT", NULL);
		gnm_func_ref (fd_count);

		dao_set_cell_expr
			(dao, 1, 3,
			 gnm_expr_new_funcall1
			 (fd_count,
			  gnm_expr_new_constant (value_dup (val_1))));

		expr_count_denum = gnm_expr_new_funcall1
			(fd_count,
			 gnm_expr_new_constant (value_dup (val_2)));
		dao_set_cell_expr (dao, 2, 3, gnm_expr_copy (expr_count_denum));

		gnm_func_unref (fd_count);
	}

	/* df */
	{
		static const GnmCellRef cr = {NULL, 0, -1, TRUE, TRUE};
		expr = gnm_expr_new_binary
			(gnm_expr_new_cellref (&cr),
			 GNM_EXPR_OP_SUB,
			 gnm_expr_new_constant (value_new_int (1)));
		dao_set_cell_expr (dao, 1, 4, gnm_expr_copy (expr));
		dao_set_cell_expr (dao, 2, 4, expr);
	}

	/* F value */
	{
		GnmCellRef cr_num = {NULL, 0, -3, TRUE, TRUE};
		GnmCellRef cr_denum = {NULL, 1, -3, TRUE, TRUE};
		if (dao_cell_is_visible (dao, 2, 2)) {
			expr = gnm_expr_new_binary (
				gnm_expr_new_cellref (&cr_num),
				GNM_EXPR_OP_DIV,
				gnm_expr_new_cellref (&cr_denum));
			gnm_expr_free (expr_var_denum);
		} else {
			expr = gnm_expr_new_binary (
				gnm_expr_new_cellref (&cr_num),
				GNM_EXPR_OP_DIV,
				expr_var_denum);
		}

		dao_set_cell_expr (dao, 1, 5, expr);
	}

	/* P right-tail */
	{
		static const GnmCellRef cr_df_num = {NULL, 0, -2, TRUE, TRUE};
		static const GnmCellRef cr_df_denum = {NULL, 1, -2, TRUE, TRUE};
		static const GnmCellRef cr_F = {NULL, 0, -1, TRUE, TRUE};
		GnmFunc *fd_fdist = gnm_func_lookup ("FDIST", NULL);
		const GnmExpr *arg3;

		gnm_func_ref (fd_fdist);

		if (dao_cell_is_visible (dao, 2, 2)) {
			arg3 = gnm_expr_new_cellref (&cr_df_denum);
			gnm_expr_free (expr_count_denum);
		} else {
			expr_df_denum = gnm_expr_new_binary
				(expr_count_denum,
				 GNM_EXPR_OP_SUB,
				 gnm_expr_new_constant (value_new_int (1)));
			arg3 = gnm_expr_copy (expr_df_denum);
		}

		dao_set_cell_expr
			(dao, 1, 6,
			 gnm_expr_new_funcall3
			 (fd_fdist,
			  gnm_expr_new_cellref (&cr_F),
			  gnm_expr_new_cellref (&cr_df_num),
			  arg3));

		gnm_func_unref (fd_fdist);
	}

	/* F critical right-tail */
	{
		static const GnmCellRef cr_df_num = {NULL, 0, -3, TRUE, TRUE};
		static const GnmCellRef cr_df_denum = {NULL, 1, -3, TRUE, TRUE};
		const GnmExpr *arg3;

		if (expr_df_denum == NULL) {
			arg3 = gnm_expr_new_cellref (&cr_df_denum);
		} else {
			arg3 = gnm_expr_copy (expr_df_denum);
		}

		dao_set_cell_expr
			(dao, 1, 7,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant (value_new_float (info->alpha)),
			  gnm_expr_new_cellref (&cr_df_num),
			  arg3));
	}

	/* P left-tail */
	{
		static const GnmCellRef cr = {NULL, 0, -2, TRUE, TRUE};

		expr = gnm_expr_new_binary (
			gnm_expr_new_constant (value_new_int (1)),
			GNM_EXPR_OP_SUB,
			gnm_expr_new_cellref (&cr));

		dao_set_cell_expr (dao, 1, 8, expr);
	}

	/* F critical left-tail */
	{
		static const GnmCellRef cr_df_num = {NULL, 0, -5, TRUE, TRUE};
		static const GnmCellRef cr_df_denum = {NULL, 1, -5, TRUE, TRUE};
		const GnmExpr *arg3;

		if (expr_df_denum == NULL) {
			arg3 = gnm_expr_new_cellref (&cr_df_denum);
		} else {
			arg3 = gnm_expr_copy (expr_df_denum);
		}

		dao_set_cell_expr
			(dao, 1, 9,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant
			  (value_new_float (1. - info->alpha)),
			  gnm_expr_new_cellref (&cr_df_num),
			  arg3));
	}

	/* P two-tail */
	{
		static const GnmCellRef cr_left = {NULL, 0, -2, TRUE, TRUE};
		static const GnmCellRef cr_right = {NULL, 0, -4, TRUE, TRUE};
		GnmFunc *fd_min = gnm_func_lookup ("MIN", NULL);;

		gnm_func_ref (fd_min);

		dao_set_cell_expr
			(dao, 1, 10,
			 gnm_expr_new_binary
			 (gnm_expr_new_constant (value_new_int (2)),
			  GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall2
			  (fd_min,
			   gnm_expr_new_cellref (&cr_right),
			   gnm_expr_new_cellref (&cr_left))));
		gnm_func_unref (fd_min);
	}

	/* F critical two-tail (left) */
	{
		static const GnmCellRef cr_df_num = {NULL, 0, -7, TRUE, TRUE};
		static const GnmCellRef cr_df_denum = {NULL, 1, -7, TRUE, TRUE};
		const GnmExpr *arg3;

		if (expr_df_denum == NULL) {
			arg3 = gnm_expr_new_cellref (&cr_df_denum);
		} else {
			arg3 = expr_df_denum;
		}

		dao_set_cell_expr
			(dao, 1, 11,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant
			  (value_new_float (1 - info->alpha / 2.)),
			  gnm_expr_new_cellref (&cr_df_num),
			  arg3));
	}

	/* F critical two-tail (right) */
	{
		static const GnmCellRef cr_df_num = {NULL, -1, -7, TRUE, TRUE};
		static const GnmCellRef cr_df_denum = {NULL, 0, -7, TRUE, TRUE};

		dao_set_cell_expr
			(dao, 2, 11,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant
			  (value_new_float (info->alpha / 2.)),
			  gnm_expr_new_cellref (&cr_df_num),
			  gnm_expr_new_cellref (&cr_df_denum)));
	}

	value_release (val_1);
	value_release (val_2);

	gnm_func_unref (fd_finv);

	dao_redraw_respan (dao);
	return FALSE;
}

gboolean
analysis_tool_ftest_engine (data_analysis_output_t *dao, gpointer specs,
			    analysis_tool_engine_t selector, gpointer result)
{
	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("F-Test (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		dao_adjust (dao, 3, 12);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_ftest_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("F-Test"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("F-Test"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_ftest_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}



/************* Regression Tool *********************************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 * Excel Bug 1: (Andrew) I believe that the following is a bug in Excel: When
 * calculating the  F-statistic in the no-intercept case, it will use xdim as
 * the numerator df and (n - xdim) as the denominator df, which is as it should
 * be. However, in the regression it will then calculate the significance of the
 * F-statistic using (n - #slope parameters - 1) as the denominator df, which
 * makes sense when you are calculating an intercept, but in this case you are not
 * and the df should be just (n - #slope parameters). Excel is inconsistent,
 * in that it does not use the same df to calculate the significance that it
 * does to calculate the F-stat itself. Inference on regressions
 * without intercepts don't really work anyway (because of the way the
 * statistics work, not the code), so this is not a terribly big deal, and
 * those who would actually use the significance of F  are not likely to be
 * using interceptless regressions anyway. So while it is easy to mimic Excel
 * in this respect, currently we do not and chose what at least for now seems
 * to be more correct.
 *
 * Excel Bug 2: (Andrew) Also in the no-intercept case: Excel has some weird way of
 * calculating the adjusted R^2 value that makes absolutely no sense to me, so
 * I couldn't mimic it if I tried. Again, what statistical opinion I have found
 * suggests that if you're running interceptless regressions, you won't know what
 * to do with an adjusted R^2 anyway.
 *
 **/

static gboolean
analysis_tool_regression_engine_last_check (G_GNUC_UNUSED data_analysis_output_t *dao,
					    G_GNUC_UNUSED analysis_tools_data_regression_t *info)
{
	/* FIXME: code from ...engine_run that may lead to error               */
	/* messages and no dao output should be moved here.                    */
	/* data can be transported from here to ..._run via the result pointer */
	return FALSE;
}


static gboolean
analysis_tool_regression_engine_run (data_analysis_output_t *dao,
				     analysis_tools_data_regression_t *info)
{
	GSList       *missing       = NULL;
	GPtrArray    *x_data        = NULL;
	data_set_t   *y_data        = NULL;
	char         *text          = NULL;
	char         *format;
	gnm_regression_stat_t   *regression_stat = NULL;
	gnm_float   r;
	gnm_float   *res,  **xss;
	int          i;
	int          xdim;
	RegressionResult regerr;
	int          cor_err        = 0;


	/* read the data */
	x_data = new_data_set_list (info->base.input, info->base.group_by,
				  FALSE, info->base.labels, dao->sheet);
	xdim = x_data->len;
	y_data = new_data_set (info->y_input, FALSE, info->base.labels,
			       _("Y Variable"), 0, dao->sheet);

	if (y_data->data->len != ((data_set_t *)g_ptr_array_index (x_data, 0))->data->len) {
		destroy_data_set (y_data);
		destroy_data_set_list (x_data);
		gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->base.wbc),
			_("There must be an equal number of entries for each variable in the regression."));
		info->base.err = analysis_tools_reported_err_input;
		return TRUE;
	}

	/* create a list of all missing or incomplete observations */
	missing = g_slist_copy (y_data->missing);
	for (i = 0; i < xdim; i++) {
		data_set_t *this_data = g_ptr_array_index (x_data, i);
		GSList *this_missing = this_data->missing;
		missing = gnm_slist_sort_merge (missing, g_slist_copy (this_missing));
	}

	if (missing != NULL) {
		gnm_strip_missing (y_data->data, missing);
		for (i = 0; i < xdim; i++) {
			data_set_t *this_data = g_ptr_array_index (x_data, i);
			gnm_strip_missing (this_data->data, missing);
		}
		g_slist_free (missing);
	}

	/* data is now clean and ready */
	xss = g_new (gnm_float *, xdim);
	res = g_new (gnm_float, xdim + 1);

	for (i = 0; i < xdim; i++) {
		data_set_t *this_data = g_ptr_array_index (x_data, i);
		xss[i] = (gnm_float *)(this_data->data->data);
	}

	regression_stat = gnm_regression_stat_new ();
	regerr = gnm_linear_regression (xss, xdim,
				    (gnm_float *)(y_data->data->data),
				    y_data->data->len,
				    info->intercept, res, regression_stat);

	if (regerr != REG_ok && regerr != REG_near_singular_good) {
		gnm_regression_stat_destroy (regression_stat);
		destroy_data_set (y_data);
		destroy_data_set_list (x_data);
		g_free (xss);
		g_free (res);
		switch (regerr) {
		case REG_not_enough_data:
			gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->base.wbc),
					 _("There are too few data points to conduct this "
					   "regression.\nThere must be at least as many "
					   "data points as free variables."));
			info->base.err = analysis_tools_reported_err_input;
			break;

		case REG_near_singular_bad:
			gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->base.wbc),
					 _("Two or more of the independent variables "
					   "are nearly linearly\ndependent.  All numerical "
					   "precision was lost in the computation."));
			info->base.err = analysis_tools_reported_err_input;
			break;

		case REG_singular:
			gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->base.wbc),
					 _("Two or more of the independent variables "
					   "are linearly\ndependent, and the regression "
					   "cannot be calculated.\n\nRemove one of these\n"
					   "variables and try the regression again."));
			info->base.err = analysis_tools_reported_err_input;
			break;

		case REG_invalid_data:
		case REG_invalid_dimensions:
			gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->base.wbc),
					 _("There must be an equal number of entries "
					   "for each variable in the regression."));
			info->base.err = analysis_tools_reported_err_input;
			break;
		default:
			break;
		}

		return TRUE;
	}


/* FIXME: virtually all the code above needs to be moved from here into  */
/*        analysis_tool_regression_engine_last_check                     */
/*        we have to figure out which data we have to trnasfer from      */
/*        to here */

        set_cell_text_col (dao, 0, 0, _("/SUMMARY OUTPUT"
					"/"
					"/Regression Statistics"
					"/Multiple R"
					"/R Square"
					"/Adjusted R Square"
					"/Standard Error"
					"/Observations"
					"/"
					"/ANOVA"
					"/"
					"/Regression"
					"/Residual"
					"/Total"
					"/"
					"/"
					"/Intercept"));
	for (i = 0; i < xdim; i++) {
		data_set_t *this_data = g_ptr_array_index (x_data, i);
		dao_set_cell (dao, 0, 17 + i, this_data->label);
	}
	dao_set_italic (dao, 0, 0, 0, 16 + xdim);

        set_cell_text_row (dao, 1, 10, _("/df"
					 "/SS"
					 "/MS"
					 "/F"
					 "/Significance of F"));
	dao_set_italic (dao, 1, 10, 5, 10);

	format = g_strdup_printf (_("/Coefficients"
				    "/Standard Error"
				    "/t Stat"
				    "/P-value"
				    "/Lower %%0.0%s%%%%"
				    "/Upper %%0.0%s%%%%"),
				  GNM_FORMAT_f,
				  GNM_FORMAT_f);
	text = g_strdup_printf (format,
				(1.0 - info->alpha) * 100,
				(1.0 - info->alpha) * 100);
	g_free (format);
        set_cell_text_row (dao, 1, 15, text);
	dao_set_italic (dao, 1, 15, 6, 15);
	g_free (text);

	dao_set_cell_comment (dao, 4, 15,
			      _("Probability of an observation's absolute value being larger than the t-value's"));

	if (xdim == 1)
		cor_err =  gnm_range_correl_pop (xss[0], (gnm_float *)(y_data->data->data),
					     y_data->data->len, &r);
	else r = gnm_sqrt (regression_stat->sqr_r);

	/* Multiple R */
	dao_set_cell_float_na (dao, 1, 3, r, cor_err == 0);

	/* R Square */
	dao_set_cell_float (dao, 1, 4, regression_stat->sqr_r);

	/* Adjusted R Square */
	dao_set_cell_float (dao, 1, 5, regression_stat->adj_sqr_r);

	/* Standard Error */
	dao_set_cell_float (dao, 1, 6, gnm_sqrt (regression_stat->var));

	/* Observations */
	dao_set_cell_float (dao, 1, 7, y_data->data->len);

	/* Regression / df */
	dao_set_cell_float (dao, 1, 11, regression_stat->df_reg);

	/* Residual / df */
	dao_set_cell_float (dao, 1, 12, regression_stat->df_resid);

	/* Total / df */
	dao_set_cell_float (dao, 1, 13, regression_stat->df_total);

	/* Residual / SS */
	dao_set_cell_float (dao, 2, 12, regression_stat->ss_resid);

	/* Total / SS */
	dao_set_cell_float (dao, 2, 13, regression_stat->ss_total);

	/* Regression / SS */
	dao_set_cell_float (dao, 2, 11, regression_stat->ss_reg);

	/* Regression / MS */
	dao_set_cell_float (dao, 3, 11, regression_stat->ms_reg);

	/* Residual / MS */
	dao_set_cell_float (dao, 3, 12, regression_stat->ms_resid);

	/* F */
	dao_set_cell_float (dao, 4, 11, regression_stat->F);

	/* Significance of F */
	dao_set_cell_float (dao, 5, 11, pf (regression_stat->F,
					regression_stat->df_reg,
					regression_stat->df_resid,
					FALSE, FALSE));

	/* Intercept / Coefficient */
	dao_set_cell_float (dao, 1, 16, res[0]);

	if (!info->intercept)
		for (i = 2; i <= 6; i++)
			dao_set_cell_na (dao, i, 16);

	/* i==-1 is for intercept, i==0... is for slopes.  */
	for (i = -info->intercept; i < xdim; i++) {
		gnm_float this_res = res[i + 1];
		/*
		 * With no intercept se[0] is for the first slope variable;
		 * with intercept, se[1] is the first slope se
		 */
		gnm_float this_se = regression_stat->se[info->intercept + i];
		gnm_float this_tval = regression_stat->t[info->intercept + i];
		gnm_float t, P;

		/* Coefficient */
		dao_set_cell_float (dao, 1, 17 + i, this_res);

		/* Standard Error */
		dao_set_cell_float (dao, 2, 17 + i, this_se);

		/* t Stat */
		dao_set_cell_float (dao, 3, 17 + i, this_tval);

		/* P values */
		P = gnm_finite (this_tval)
			? 2 * pt (gnm_abs (this_tval), regression_stat->df_resid, FALSE, FALSE)
			: 0;
		dao_set_cell_float (dao, 4, 17 + i, P);

		t = (this_se == 0)
			? 0
			: qt (info->alpha / 2, regression_stat->df_resid,
			      FALSE, FALSE);

		/* Lower 95% */
		dao_set_cell_float (dao, 5, 17 + i, this_res - t * this_se);

		/* Upper 95% */
		dao_set_cell_float (dao, 6, 17 + i, this_res + t * this_se);
	}

	gnm_regression_stat_destroy (regression_stat);
	destroy_data_set (y_data);
	destroy_data_set_list (x_data);
	g_free (xss);
	g_free (res);

	if (regerr == REG_near_singular_good)
		gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->base.wbc),
			_("Two or more of the independent variables "
			  "are nearly linearly\ndependent.  Treat the "
			  "regression result with great care!"));

	return FALSE;
}

gboolean
analysis_tool_regression_engine (data_analysis_output_t *dao, gpointer specs,
			    analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_regression_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Regression (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		if (!gnm_check_input_range_list_homogeneity (info->base.input)) {
			info->base.err = analysis_tools_REG_invalid_dimensions;
			return TRUE;
		}
		dao_adjust (dao, 7, 17 + g_slist_length (info->base.input));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		value_release (info->y_input);
		info->y_input = NULL;
		return analysis_tool_generic_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return analysis_tool_regression_engine_last_check (dao, specs);
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Regression"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Regression"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_regression_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}



/************* Moving Average Tool *****************************************
 *
 * The moving average tool calculates moving averages of given data
 * set.  The results are given in a table which can be printed out in
 * a new sheet, in a new workbook, or simply into an existing sheet.
 *
 **/


static gboolean
analysis_tool_moving_average_engine_run (data_analysis_output_t *dao,
					 analysis_tools_data_moving_average_t *info)
{
	GPtrArray     *data;
	guint         dataset;
	gint          col = 0;
	gnm_float    *prev, *prev_av;

	data = new_data_set_list (info->base.input, info->base.group_by,
				  TRUE, info->base.labels, dao->sheet);

	prev = g_new (gnm_float, info->interval);
	prev_av = g_new (gnm_float, info->interval);

	for (dataset = 0; dataset < data->len; dataset++) {
		data_set_t    *current;
		gnm_float    sum;
		gnm_float    std_err;
		gint         row;
		int           add_cursor, del_cursor;

		current = g_ptr_array_index (data, dataset);
		dao_set_cell_printf (dao, col, 0, current->label);
		if (info->std_error_flag)
			dao_set_cell_printf (dao, col + 1, 0, _("Standard Error"));

		add_cursor = del_cursor = 0;
		sum = 0;
		std_err = 0;

		for (row = 0; row < info->interval - 1 &&
			      row < (gint)current->data->len; row++) {
			prev[add_cursor] = g_array_index
				(current->data, gnm_float, row);
			sum += prev[add_cursor];
			++add_cursor;
			dao_set_cell_na (dao, col, row + 1);
			if (info->std_error_flag)
				dao_set_cell_na (dao, col + 1, row + 1);
		}
		for (row = info->interval - 1; row < (gint)current->data->len; row++) {
			prev[add_cursor] = g_array_index
				(current->data, gnm_float, row);
			sum += prev[add_cursor];
			prev_av[add_cursor] = sum / info->interval;
			dao_set_cell_float (dao, col, row + 1, prev_av[add_cursor]);
			sum -= prev[del_cursor];
			if (info->std_error_flag) {
				std_err += (prev[add_cursor] - prev_av[add_cursor]) *
					(prev[add_cursor] - prev_av[add_cursor]);
				if (row >= 2 * info->interval - 2) {
					dao_set_cell_float (dao, col + 1, row + 1,
							gnm_sqrt (std_err / info->interval));
					std_err -= (prev[del_cursor] - prev_av[del_cursor]) *
						(prev[del_cursor] - prev_av[del_cursor]);
				} else {
					dao_set_cell_na (dao, col + 1, row + 1);
				}
			}
			if (++add_cursor == info->interval)
				add_cursor = 0;
			if (++del_cursor == info->interval)
				del_cursor = 0;
		}
		col++;
		if (info->std_error_flag)
			col++;
	}
	dao_set_italic (dao, 0, 0, col - 1, 0);

	destroy_data_set_list (data);
	g_free (prev);
	g_free (prev_av);

	return FALSE;
}


gboolean
analysis_tool_moving_average_engine (data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_moving_average_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Moving Average (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		dao_adjust (dao, (info->std_error_flag ? 2 : 1) *
			    g_slist_length (info->base.input),
			    1 + analysis_tool_calc_length (specs));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Moving Average"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Moving Average"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_moving_average_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}


/************* Exponential Smoothing Tool **************************************
 *
 * This tool can be used to predict a value based on the prior period.
 * Exponential smoothing uses the following formula to adjust the error in
 * the prior forecast:
 *
 *    F(t+1) = F(t) + (1 - damp_fact) * ( A(t) - F(t) )
 *
 * The results are given in a table which can be printed out in
 * a new sheet, in a new workbook, or simply into an existing sheet.
 *
 * The stanard errors are calculated using the following formula:
 *
 *                ((A(t-3)-F(t-3))^2 + (A(t-2)-F(t-2))^2 + (A(t-1)-F(t-1))^2))
 *    e(t) = SQRT (----------------------------------------------------------)
 *                (                            3                             )
 *
 */

static gboolean
analysis_tool_exponential_smoothing_engine_run (data_analysis_output_t *dao,
						analysis_tools_data_exponential_smoothing_t *info)
{
	GPtrArray     *data;
	guint         dataset;

	/* TODO: Standard error output */

	data = new_data_set_list (info->base.input, info->base.group_by,
				  TRUE, info->base.labels, dao->sheet);

	for (dataset = 0; dataset < data->len; dataset++) {
		data_set_t    *current;
		gnm_float    a, f, F[2] = { 0, 0 }, A[2] = { 0, 0 };
		guint         row;

		current = g_ptr_array_index (data, dataset);
		dao_set_cell_printf (dao, dataset, 0, current->label);
		a = f = 0;
		for (row = 0; row < current->data->len; row++) {
		        if (row == 0) {
				/* Cannot forecast for the first data element.
				 */

				dao_set_cell_na (dao, dataset, row + 1);
				if (info->std_error_flag)
				        dao_set_cell_na (dao, dataset + 1,
							 row + 1);
			} else if (row == 1) {
				/* The second forecast is always the first
				 * data element. */
				dao_set_cell_float (dao, dataset, row + 1, a);
				f = a;
				if (info->std_error_flag)
				        dao_set_cell_na (dao, dataset + 1,
							 row + 1);
			} else {
			        if (info->std_error_flag) {
				        gnm_float m1 = a - f;
					gnm_float m2 = A[0] - F[0];
					gnm_float m3 = A[1] - F[1];

					if (row < 4)
					        dao_set_cell_na (dao,
								 dataset + 1,
								 row + 1);
					else
					        dao_set_cell_float
						        (dao, dataset + 1,
							 row + 1,
							 sqrt ((m1*m1 + m2*m2 +
								m3*m3) / 3));
				        A[1] = A[0];
					A[0] = a;
					F[1] = F[0];
					F[0] = f;
				}

				/*
				 * F(t+1) = F(t) + (1 - damp_fact) *
				 *          ( A(t) - F(t) ),
				 * where A(t) is the t'th data element.
				 */

				f = f + (1.0 - info->damp_fact) * (a - f);
				dao_set_cell_float (dao, dataset, row + 1, f);

			}
			a = g_array_index (current->data, gnm_float, row);
		}
	}
	dao_set_italic (dao, 0, 0, data->len - 1, 0);

	destroy_data_set_list (data);

	return FALSE;
}

gboolean
analysis_tool_exponential_smoothing_engine (data_analysis_output_t *dao,
					    gpointer specs,
					    analysis_tool_engine_t selector,
					    gpointer result)
{
	analysis_tools_data_exponential_smoothing_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Exponential Smoothing (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		dao_adjust (dao, (info->std_error_flag ? 2 : 1) *
			    g_slist_length (info->base.input),
			    1 + analysis_tool_calc_length (specs));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Exponential Smoothing"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Exponential Smoothing"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_exponential_smoothing_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}


/************* Rank and Percentile Tool ************************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

typedef struct {
        int     rank;
        int     same_rank_count;
        int     point;
        gnm_float x;
} rank_t;

static gint
rank_compare (const rank_t *a, const rank_t *b)
{
        if (a->x < b->x)
                return 1;
        else if (a->x == b->x)
                return 0;
        else
                return -1;
}


static gboolean
analysis_tool_ranking_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_ranking_t *info)
{
	GPtrArray *data = NULL;
	guint n_data;

	data = new_data_set_list (info->base.input, info->base.group_by,
				  TRUE, info->base.labels, dao->sheet);

	for (n_data = 0; n_data < data->len; n_data++) {
	        rank_t *rank;
		guint i, j;
		data_set_t * this_data_set;

	        this_data_set = g_ptr_array_index (data, n_data);

		dao_set_cell (dao, n_data * 4, 0, _("Point"));
		dao_set_cell (dao, n_data * 4+1, 0, this_data_set->label);
		dao_set_cell (dao, n_data * 4 + 2, 0, _("Rank"));
		dao_set_cell (dao, n_data * 4 + 3, 0, _("Percentile"));

		rank = g_new (rank_t, this_data_set->data->len);

		for (i = 0; i < this_data_set->data->len; i++) {
		        gnm_float x = g_array_index (this_data_set->data, gnm_float, i);

			rank[i].point = i + 1;
			rank[i].x = x;
			rank[i].rank = 1;
			rank[i].same_rank_count = -1;

			for (j = 0; j < this_data_set->data->len; j++) {
			        gnm_float y = g_array_index (this_data_set->data, gnm_float, j);
				if (y > x)
				        rank[i].rank++;
				else if (y == x)
				        rank[i].same_rank_count++;
			}
		}

		qsort (rank, this_data_set->data->len,
		       sizeof (rank_t), (void *) &rank_compare);

		dao_set_percent (dao, n_data * 4 + 3, 1,
			     n_data * 4 + 3, this_data_set->data->len);
		for (i = 0; i < this_data_set->data->len; i++) {
			/* Point number */
			dao_set_cell_int (dao, n_data * 4 + 0, i + 1, rank[i].point);

			/* GnmValue */
			dao_set_cell_float (dao, n_data * 4 + 1, i + 1, rank[i].x);

			/* Rank */
			dao_set_cell_float (dao, n_data * 4 + 2, i + 1,
					rank[i].rank +
					(info->av_ties ? rank[i].same_rank_count / 2. : 0));

			/* Percent */
			dao_set_cell_float_na (dao, n_data * 4 + 3, i + 1,
					   1. - (rank[i].rank - 1.) /
						    (this_data_set->data->len - 1.),
					   this_data_set->data->len != 0);
		}
		g_free (rank);
	}

	destroy_data_set_list (data);

	return 0;
}

gboolean
analysis_tool_ranking_engine (data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_ranking_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Ranks (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		dao_adjust (dao, 4 * g_slist_length (info->base.input),
			    1 + analysis_tool_calc_length (specs));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Ranks"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Ranks"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_ranking_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}




/************* Anova: Single Factor Tool **********************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

static gboolean
analysis_tool_anova_single_engine_run (data_analysis_output_t *dao, gpointer specs)
{
	analysis_tools_data_anova_single_t *info = specs;
	GSList *inputdata = info->base.input;
	GnmFunc *fd_sum;
	GnmFunc *fd_count;
	GnmFunc *fd_mean;
	GnmFunc *fd_var;
	GnmFunc *fd_devsq;

	guint index;

	dao_set_cell (dao, 0, 0, _("Anova: Single Factor"));
	dao_set_cell (dao, 0, 2, _("SUMMARY"));
	dao_set_italic (dao, 0, 0, 0, 2);

	set_cell_text_row (dao, 0, 3, _("/Groups"
					"/Count"
					"/Sum"
					"/Average"
					"/Variance"));
	dao_set_italic (dao, 0, 3, 4, 3);

	fd_mean = gnm_func_lookup ("AVERAGE", NULL);
	gnm_func_ref (fd_mean);
	fd_var = gnm_func_lookup ("VAR", NULL);
	gnm_func_ref (fd_var);
	fd_sum = gnm_func_lookup ("SUM", NULL);
	gnm_func_ref (fd_sum);
	fd_count = gnm_func_lookup ("COUNT", NULL);
	gnm_func_ref (fd_count);
	fd_devsq = gnm_func_lookup ("DEVSQ", NULL);
	gnm_func_ref (fd_devsq);

	dao->offset_row += 4;
	if (dao->rows <= dao->offset_row)
		goto finish_anova_single_factor_tool;

	/* SUMMARY */

	for (index = 0; inputdata != NULL;
	     inputdata = inputdata->next, index++) {
		GnmValue *val_org = value_dup (inputdata->data);

		/* Label */
		analysis_tools_write_label (val_org, dao, &info->base,
					    0, index, index + 1);
		dao_set_italic (dao, 0, index, 0, index);

		/* Count */
		dao_set_cell_expr
			(dao, 1, index,
			 gnm_expr_new_funcall1
			 (fd_count,
			  gnm_expr_new_constant (value_dup (val_org))));

		/* Sum */
		dao_set_cell_expr
			(dao, 2, index,
			 gnm_expr_new_funcall1
			 (fd_sum,
			  gnm_expr_new_constant (value_dup (val_org))));

		/* Average */
		dao_set_cell_expr
			(dao, 3, index,
			 gnm_expr_new_funcall1
			 (fd_mean,
			  gnm_expr_new_constant (value_dup (val_org))));

		/* Variance */
		dao_set_cell_expr
			(dao, 4, index,
			 gnm_expr_new_funcall1
			 (fd_var,
			  gnm_expr_new_constant (val_org)));

	}

	dao->offset_row += index + 2;
	if (dao->rows <= dao->offset_row)
		goto finish_anova_single_factor_tool;


	set_cell_text_col (dao, 0, 0, _("/ANOVA"
					"/Source of Variation"
					"/Between Groups"
					"/Within Groups"
					"/Total"));
	dao_set_italic (dao, 0, 0, 0, 4);
	set_cell_text_row (dao, 1, 1, _("/SS"
					"/df"
					"/MS"
					"/F"
					"/P-value"
					"/F critical"));
	dao_set_italic (dao, 1, 1, 6, 1);

	/* ANOVA */
	{
		GnmExprList *sum_wdof_args = NULL;
		GnmExprList *sum_tdof_args = NULL;
		GnmExprList *arg_ss_total = NULL;
		GnmExprList *arg_ss_within = NULL;

		GnmExpr const *expr_wdof = NULL;
		GnmExpr const *expr_ss_total = NULL;
		GnmExpr const *expr_ss_within = NULL;

		for (inputdata = info->base.input; inputdata != NULL;
		     inputdata = inputdata->next) {
			GnmValue *val_org = value_dup (inputdata->data);
			GnmExpr const *expr_one;
			GnmExpr const *expr_count_one;

			analysis_tools_remove_label (val_org, &info->base);
			expr_one = gnm_expr_new_constant (value_dup (val_org));

			arg_ss_total =  gnm_expr_list_append
				(arg_ss_total,
				 gnm_expr_new_constant (val_org));

			arg_ss_within = gnm_expr_list_append
				(arg_ss_within,
				 gnm_expr_new_funcall1
				 (fd_devsq, gnm_expr_copy (expr_one)));

			expr_count_one =
				gnm_expr_new_funcall1 (fd_count, expr_one);

			sum_wdof_args = gnm_expr_list_append
				(sum_wdof_args,
				 gnm_expr_new_binary(
					 gnm_expr_copy (expr_count_one),
					 GNM_EXPR_OP_SUB,
					 gnm_expr_new_constant
					 (value_new_int (1))));
			sum_tdof_args = gnm_expr_list_append
				(sum_tdof_args,
				 expr_count_one);
		}

		expr_ss_total = gnm_expr_new_funcall
			(fd_devsq, arg_ss_total);
		expr_ss_within = gnm_expr_new_funcall
			(fd_sum, arg_ss_within);

		{
			/* SS between groups */
			GnmExpr const *expr_ss_between;

			if (dao_cell_is_visible (dao, 1,4)) {
				GnmCellRef cr_within = {NULL, 0, 1, TRUE, TRUE};
				GnmCellRef cr_total = {NULL, 0, 2, TRUE, TRUE};
				expr_ss_between = gnm_expr_new_binary
					(gnm_expr_new_cellref (&cr_total),
					 GNM_EXPR_OP_SUB,
					 gnm_expr_new_cellref (&cr_within));

			} else {
				expr_ss_between = gnm_expr_new_binary
					(gnm_expr_copy (expr_ss_total),
					 GNM_EXPR_OP_SUB,
					 gnm_expr_copy (expr_ss_within));
			}
			dao_set_cell_expr (dao, 1, 2, expr_ss_between);
		}
		{
			/* SS within groups */
			dao_set_cell_expr (dao, 1, 3, gnm_expr_copy (expr_ss_within));
		}
		{
			/* SS total groups */
			dao_set_cell_expr (dao, 1, 4, expr_ss_total);
		}
		{
			/* Between groups degrees of freedom */
			dao_set_cell_int (dao, 2, 2,
					  g_slist_length (info->base.input) - 1);
		}
		{
			/* Within groups degrees of freedom */
			expr_wdof = gnm_expr_new_funcall (fd_sum, sum_wdof_args);
			dao_set_cell_expr (dao, 2, 3, gnm_expr_copy (expr_wdof));
		}
		{
			/* Total degrees of freedom */
			GnmExpr const *expr_tdof =
				gnm_expr_new_binary
				(gnm_expr_new_funcall (fd_sum, sum_tdof_args),
				 GNM_EXPR_OP_SUB,
				 gnm_expr_new_constant (value_new_int (1)));
			dao_set_cell_expr (dao, 2, 4, expr_tdof);
		}
		{
			/* MS values */
			GnmExpr const *expr_ms = NULL;
			GnmCellRef cr_num = {NULL, -2, 0, TRUE, TRUE};
			GnmCellRef cr_denom = {NULL, -1, 0, TRUE, TRUE};
			cr_num.sheet = dao->sheet;
			cr_denom.sheet = dao->sheet;

			expr_ms = gnm_expr_new_binary
				(gnm_expr_new_cellref (&cr_num),
				 GNM_EXPR_OP_DIV,
				 gnm_expr_new_cellref (&cr_denom));
			dao_set_cell_expr (dao, 3, 2, gnm_expr_copy (expr_ms));
			dao_set_cell_expr (dao, 3, 3, expr_ms);
		}
		{
			/* Observed F */
			GnmCellRef cr_num = {NULL, -1, 0, TRUE, TRUE};
			GnmCellRef cr_denom = {NULL, -1, 1, TRUE, TRUE};
			GnmExpr const *expr_denom = NULL;
			GnmExpr const *expr_f = NULL;
			cr_num.sheet = dao->sheet;
			cr_denom.sheet = dao->sheet;

			if (dao_cell_is_visible (dao, 3, 3)) {
				expr_denom = gnm_expr_new_cellref (&cr_denom);
				gnm_expr_free (expr_ss_within);
			} else {
				expr_denom = gnm_expr_new_binary
					(expr_ss_within,
					 GNM_EXPR_OP_DIV,
					 gnm_expr_copy (expr_wdof));
			}

			expr_f = gnm_expr_new_binary
				(gnm_expr_new_cellref (&cr_num),
				 GNM_EXPR_OP_DIV, expr_denom);
			dao_set_cell_expr(dao, 4, 2, expr_f);
		}
		{
			/* P value */
			GnmFunc *fd_fdist;
			GnmCellRef cr = {NULL, -1, 0, TRUE, TRUE};
			const GnmExpr *arg1;
			const GnmExpr *arg2;
			const GnmExpr *arg3;

			cr.sheet = dao->sheet;
			arg1 = gnm_expr_new_cellref (&cr);
			cr.col = -3;
			arg2 = gnm_expr_new_cellref (&cr);

			if (dao_cell_is_visible (dao, 2, 3)) {
				cr.row = 1;
				arg3 = gnm_expr_new_cellref (&cr);
			} else {
				arg3 = gnm_expr_copy (expr_wdof);
			}

			fd_fdist = gnm_func_lookup ("FDIST", NULL);
			gnm_func_ref (fd_fdist);

			dao_set_cell_expr
				(dao, 5, 2,
				 gnm_expr_new_funcall3
				 (fd_fdist,
				  arg1, arg2, arg3));
			if (fd_fdist)
				gnm_func_unref (fd_fdist);
		}
		{
			/* Critical F*/
			GnmFunc *fd_finv;
			GnmCellRef cr = {NULL, -4, 0, TRUE, TRUE};
			const GnmExpr *arg2, *arg3;
			cr.sheet = dao->sheet;

			arg2 = gnm_expr_new_cellref (&cr);

			if (dao_cell_is_visible (dao, 2, 3)) {
				cr.row = 1;
				arg3 = gnm_expr_new_cellref (&cr);
				gnm_expr_free (expr_wdof);
			} else
				arg3 = expr_wdof;

			fd_finv = gnm_func_lookup ("FINV", NULL);
			gnm_func_ref (fd_finv);

			dao_set_cell_expr
				(dao, 6, 2,
				 gnm_expr_new_funcall3
				 (fd_finv,
				  gnm_expr_new_constant
				  (value_new_float (info->alpha)),
				  arg2,
				  arg3));
			gnm_func_unref (fd_finv);
		}
	}

finish_anova_single_factor_tool:

	gnm_func_unref (fd_mean);
	gnm_func_unref (fd_var);
	gnm_func_unref (fd_sum);
	gnm_func_unref (fd_count);
	gnm_func_unref (fd_devsq);

	dao->offset_row = 0;
	dao->offset_col = 0;

	dao_redraw_respan (dao);
        return FALSE;
}



gboolean
analysis_tool_anova_single_engine (data_analysis_output_t *dao, gpointer specs,
				   analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_anova_single_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Single Factor ANOVA (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		dao_adjust (dao, 7, 11 + g_slist_length (info->base.input));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Anova"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Single Factor ANOVA"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_anova_single_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}



/************* Anova: Two-Factor Without Replication Tool ****************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

static gboolean
analysis_tool_anova_two_factor_prepare_input_range (
                       analysis_tools_data_anova_two_factor_t *info)
{
	info->row_input_range = NULL;
	info->col_input_range = NULL;

	info->rows = info->input->v_range.cell.b.row - info->input->v_range.cell.a.row +
		(info->labels ? 0 : 1);
	info->n_r = info->rows/info->replication;
	info->n_c = info->input->v_range.cell.b.col - info->input->v_range.cell.a.col +
		(info->labels ? 0 : 1);

	if (info->replication == 1) {
		info->row_input_range = g_slist_prepend (NULL, value_dup (info->input));
		prepare_input_range (&info->row_input_range, GROUPED_BY_ROW);
		if (info->labels) {
			GSList *list = info->row_input_range;
			info->row_input_range = g_slist_remove_link (info->row_input_range, list);
			range_list_destroy (list);
		}
		info->col_input_range = g_slist_prepend (NULL, info->input);
		prepare_input_range (&info->col_input_range, GROUPED_BY_COL);
		if (info->labels) {
			GSList *list = info->col_input_range;
			info->col_input_range = g_slist_remove_link (info->col_input_range, list);
			range_list_destroy (list);
		}
		info->input = NULL;
		if (info->col_input_range == NULL || info->row_input_range == NULL ||
		    info->col_input_range->next == NULL || info->row_input_range->next == NULL) {
			range_list_destroy (info->col_input_range);
			info->col_input_range = NULL;
			range_list_destroy (info->row_input_range);
			info->row_input_range = NULL;
			info->err = analysis_tools_missing_data;
			return TRUE;
		}
	} else {
		/* Check that correct number of rows per sample */
		if (info->rows % info->replication != 0) {
			info->err = analysis_tools_replication_invalid;
			return TRUE;
		}

		/* Check that at least two columns of data are given */
		if (info->n_c < 2) {
			info->err = analysis_tools_too_few_cols;
			return TRUE;
		}
		/* Check that at least two data rows of data are given */
		if (info->n_r < 2) {
			info->err = analysis_tools_too_few_rows;
			return TRUE;
		}

		if (info->labels) {
			info->input->v_range.cell.a.row += 1;
			info->input->v_range.cell.a.col += 1;
		}

	}
	return FALSE;
}

static gboolean
analysis_tool_anova_two_factor_no_rep_engine_run (data_analysis_output_t *dao,
						  analysis_tools_data_anova_two_factor_t *info)
{
	GPtrArray *row_data = NULL;
	GPtrArray *col_data = NULL;

	int        i, n, error;
	int        cols;
	int        rows;
	gnm_float    cm,  sum = 0;
	gnm_float    ss_r = 0, ss_c = 0, ss_e = 0, ss_t = 0;
	gnm_float    ms_r, ms_c, ms_e, f1, f2, p1, p2, f1_c, f2_c;
	int        df_r, df_c, df_e, df_t;


	row_data = new_data_set_list (info->row_input_range, GROUPED_BY_ROW,
				  FALSE, info->labels, dao->sheet);
	col_data = new_data_set_list (info->col_input_range, GROUPED_BY_COL,
				  FALSE, info->labels, dao->sheet);
	if (check_data_for_missing (row_data) ||
	    check_data_for_missing (col_data)) {
		destroy_data_set_list (row_data);
		destroy_data_set_list (col_data);
		gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->wbc),
			_("The input range contains non-numeric data."));
		return TRUE;
	}

	cols = col_data->len;
	rows = row_data->len;
	n = rows * cols;

	dao_set_cell (dao, 0, 0, _("Anova: Two-Factor Without Replication"));
	set_cell_text_row (dao, 0, 2, _("/SUMMARY"
					"/Count"
					"/Sum"
					"/Average"
					"/Variance"));

	for (i = 0; i < rows; i++) {
	        gnm_float v;
		data_set_t *data_set = g_ptr_array_index (row_data, i);
		gnm_float *the_data = (gnm_float *)data_set->data->data;

		dao_set_cell (dao, 0, i + 3, data_set->label);
		dao_set_cell_int (dao, 1, i + 3, data_set->data->len);
		error = gnm_range_sum (the_data, data_set->data->len, &v);
		sum +=  v;
		ss_r += v * v/ data_set->data->len;
		dao_set_cell_float_na (dao, 2, i + 3, v, error == 0);
		dao_set_cell_float_na (dao, 3, i + 3,  v / data_set->data->len,
				   error == 0 && data_set->data->len != 0);
		error = gnm_range_var_est (the_data, data_set->data->len , &v);
		dao_set_cell_float_na (dao, 4, i + 3, v, error == 0);

		error = gnm_range_sumsq (the_data, data_set->data->len, &v);
		ss_t += v;
	}

	for (i = 0; i < cols; i++) {
	        gnm_float v;
		data_set_t *data_set = g_ptr_array_index (col_data, i);
		gnm_float *the_data = (gnm_float *)data_set->data->data;

		dao_set_cell (dao, 0, i + 4 + rows, data_set->label);
		dao_set_cell_int (dao, 1, i + 4 + rows, data_set->data->len);
		error = gnm_range_sum (the_data, data_set->data->len, &v);
		ss_c += v * v/ data_set->data->len;
		dao_set_cell_float_na (dao, 2, i + 4 + rows, v, error == 0);
		dao_set_cell_float_na (dao, 3, i + 4 + rows, v / data_set->data->len,
				   error == 0 && data_set->data->len != 0);
		error = gnm_range_var_est (the_data, data_set->data->len , &v);
		dao_set_cell_float_na (dao, 4, i + 4 + rows, v, error == 0);
	}

	cm = sum * sum / n;
	ss_t -= cm;
	ss_r -= cm;
	ss_c -= cm;
	ss_e = ss_t - ss_r - ss_c;


	df_r = rows - 1;
	df_c = cols - 1;
	df_e = (rows - 1) * (cols - 1);
	df_t = n - 1;
	ms_r = ss_r / df_r;
	ms_c = ss_c / df_c;
	ms_e = ss_e / df_e;
	f1   = ms_r / ms_e;
	f2   = ms_c / ms_e;
	p1   = pf (f1, df_r, df_e, FALSE, FALSE);
	p2   = pf (f2, df_c, df_e, FALSE, FALSE);
	f1_c = qf (info->alpha, df_r, df_e, FALSE, FALSE);
	f2_c = qf (info->alpha, df_c, df_e, FALSE, FALSE);

	set_cell_text_col (dao, 0, 6 + rows + cols, _("/ANOVA"
						      "/Source of Variation"
						      "/Rows"
						      "/Columns"
						      "/Error"
						      "/Total"));
	set_cell_text_row (dao, 1, 7 + rows + cols, _("/SS"
						      "/df"
						      "/MS"
						      "/F"
						      "/P-value"
						      "/F critical"));

	dao_set_cell_float (dao, 1, 8 + rows + cols, ss_r);
	dao_set_cell_float (dao, 1, 9 + rows + cols, ss_c);
	dao_set_cell_float (dao, 1, 10 + rows + cols, ss_e);
	dao_set_cell_float (dao, 1, 11 + rows + cols, ss_t);
	dao_set_cell_int (dao, 2, 8 + rows + cols, df_r);
	dao_set_cell_int (dao, 2, 9 + rows + cols, df_c);
	dao_set_cell_int (dao, 2, 10 + rows + cols, df_e);
	dao_set_cell_int (dao, 2, 11 + rows + cols, df_t);
	dao_set_cell_float (dao, 3, 8 + rows + cols, ms_r);
	dao_set_cell_float (dao, 3, 9 + rows + cols, ms_c);
	dao_set_cell_float (dao, 3, 10 + rows + cols, ms_e);
	dao_set_cell_float_na (dao, 4, 8 + rows + cols, f1, ms_e != 0);
	dao_set_cell_float_na (dao, 4, 9 + rows + cols, f2, ms_e != 0);
	dao_set_cell_float_na (dao, 5, 8 + rows + cols, p1, ms_e != 0);
	dao_set_cell_float_na (dao, 5, 9 + rows + cols, p2, ms_e != 0);
	dao_set_cell_float (dao, 6, 8 + rows + cols, f1_c);
	dao_set_cell_float (dao, 6, 9 + rows + cols, f2_c);

	dao_set_italic (dao, 1, 2, 4, 2);
	dao_set_italic (dao, 1, 7 + rows + cols, 6, 7 + rows + cols);
	dao_set_italic (dao, 0, 0, 0, 11 + rows + cols);

	destroy_data_set_list (row_data);
	destroy_data_set_list (col_data);

	return FALSE;
}


/************* Anova: Two-Factor With Replication Tool *******************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

static char *
make_label (Sheet *sheet, int col, int row, char *default_format, int index,
	    gboolean read_cell)
{
	char *rendered_text = NULL;
	if (read_cell) {
		GnmCell *cell = sheet_cell_get (sheet, col, row);
		if (cell && cell->value)
			rendered_text = value_get_as_string (cell->value);
	}
	if (rendered_text != NULL) {
		if (strlen (rendered_text) == 0)
			g_free (rendered_text);
		else
			return rendered_text;
	}
	return g_strdup_printf (default_format, index);
}

static gboolean
analysis_tool_anova_two_factor_engine_run (data_analysis_output_t *dao,
					   analysis_tools_data_anova_two_factor_t *info)
{
	guint i_c, i_r;
	guint n = 0;
	GPtrArray *col_labels = NULL;
	GPtrArray *row_labels = NULL;
	GPtrArray *row_data = NULL;
	GnmValue *input_cp;
	gint df_r, df_c, df_rc, df_e, df_total;
	gnm_float ss_r = 0.0, ss_c = 0.0, ss_rc = 0.0, ss_e = 0.0, ss_total = 0.0;
	gnm_float ms_r, ms_c, ms_rc, ms_e;
	gnm_float f_r, f_c, f_rc;
	gnm_float p_r, p_c, p_rc;
	gnm_float cm = 0.0;
	guint max_sample_size = 0;
	guint missing_observations = 0;
	gboolean empty_sample = FALSE;
	gboolean return_value = FALSE;


	col_labels = g_ptr_array_new ();
	g_ptr_array_set_size (col_labels, info->n_c);
	for (i_c = 0; i_c < info->n_c; i_c++)
		g_ptr_array_index (col_labels, i_c) = make_label (
			dao->sheet, info->input->v_range.cell.a.col + i_c,
			info->input->v_range.cell.a.row - 1,
			_("Column %i"), i_c + 1, info->labels);
	row_labels = g_ptr_array_new ();
	g_ptr_array_set_size (row_labels, info->n_r);
	for (i_r = 0; i_r < info->n_r; i_r++)
		g_ptr_array_index (row_labels, i_r) = make_label (
			dao->sheet,
			info->input->v_range.cell.a.col - 1,
			info->input->v_range.cell.a.row + i_r * info->replication,
			_("Row %i"), i_r + 1, info->labels);

	input_cp = value_dup (info->input);
	input_cp->v_range.cell.b.row = input_cp->v_range.cell.a.row +
		info->replication - 1;
	row_data = g_ptr_array_new ();
	while (input_cp->v_range.cell.a.row < info->input->v_range.cell.b.row) {
		GPtrArray *col_data = NULL;
		GSList *col_input_range = NULL;

		col_input_range = g_slist_prepend (NULL, value_dup (input_cp));
		prepare_input_range (&col_input_range, GROUPED_BY_COL);
		col_data = new_data_set_list (col_input_range, GROUPED_BY_COL,
				  TRUE, FALSE, dao->sheet);
		g_ptr_array_add (row_data, col_data);
		range_list_destroy (col_input_range);
		input_cp->v_range.cell.a.row += info->replication;
		input_cp->v_range.cell.b.row += info->replication;
	}
	value_release (input_cp);

/* We have loaded the data, we should now check for missing observations */

	for (i_r = 0; i_r < info->n_r; i_r++) {
		GPtrArray *data = NULL;

		data = g_ptr_array_index (row_data, i_r);
		for (i_c = 0; i_c < info->n_c; i_c++) {
			data_set_t *cell_data = g_ptr_array_index (data, i_c);
			guint num = cell_data->data->len;
			if (num == 0)
				empty_sample = TRUE;
			else {
				if (num > max_sample_size)
					max_sample_size = num;
				missing_observations += (info->replication - num);
			}
		}
	}

	if (empty_sample) {
		gnm_cmd_context_error_calc (GO_CMD_CONTEXT (info->wbc),
				 _("One of the factor combinations does not contain "
				   "any observations!"));
		return_value =  TRUE;
		goto anova_two_factor_with_r_tool_end;
	}

	missing_observations -= ((info->replication - max_sample_size) * info->n_c * info->n_r);

/* We are ready to create the summary table */
	dao_set_cell (dao, 0, 0, _("Anova: Two-Factor With Replication"));
	dao_set_cell (dao, 0, 2, _("SUMMARY"));
	for (i_c = 0; i_c < info->n_c; i_c++)
		dao_set_cell (dao, 1 + i_c, 2,
			 (char *)g_ptr_array_index (col_labels, i_c));
	dao_set_cell (dao, 1 + info->n_c, 2, _("Total"));
	for (i_r = 0; i_r <= info->n_r; i_r++) {
		set_cell_text_col (dao, 0, 4 + i_r * 6, _("/Count"
							"/Sum"
							"/Average"
							"/Variance"));
		if (i_r < info->n_r)
			dao_set_cell (dao, 0, 3 + i_r * 6,
				  (char *)g_ptr_array_index (row_labels, i_r));
	}
	dao_set_cell (dao, 0, 3 + info->n_r * 6, _("Total"));

	set_cell_text_col (dao, 0, info->n_r * 6 + 10, _("/ANOVA"
						    "/Source of Variation"
						    "/Rows"
						    "/Columns"
						    "/Interaction"
						    "/Within"));
	dao_set_cell (dao, 0, info->n_r * 6 + 17, _("Total"));

	set_cell_text_row (dao, 1,  info->n_r * 6 + 11, _("/SS"
						    "/df"
						    "/MS"
						    "/F"
						    "/P-value"
						    "/F critical"));

	for (i_r = 0; i_r < info->n_r; i_r++) {
		GPtrArray *data = NULL;
		gnm_float row_sum = 0.0;
		gnm_float row_sum_sq = 0.0;
		guint row_cnt = 0;

		data = g_ptr_array_index (row_data, i_r);
		for (i_c = 0; i_c < info->n_c; i_c++) {
			data_set_t *cell_data;
			gnm_float v;
			int error;
			int num;
			gnm_float *the_data;

			cell_data = g_ptr_array_index (data, i_c);
			the_data = (gnm_float *)cell_data->data->data;
			num = cell_data->data->len;
			row_cnt += num;

			dao_set_cell_int (dao, 1 + i_c, 4 + i_r * 6, num);
			error = gnm_range_sum (the_data, num, &v);
			row_sum += v;
			ss_rc += v * v / num;
			dao_set_cell_float_na (dao, 1 + i_c, 5 + i_r * 6, v, error == 0);
			dao_set_cell_float_na (dao, 1 + i_c, 6 + i_r * 6, v / num,
					   error == 0 && num > 0);
			error = gnm_range_var_est (the_data, num , &v);
			dao_set_cell_float_na (dao, 1 + i_c, 7 + i_r * 6, v, error == 0);

			error = gnm_range_sumsq (the_data, num, &v);
			row_sum_sq += v;
		}
		cm += row_sum;
		n += row_cnt;
		ss_r += row_sum * row_sum / row_cnt;
		ss_total += row_sum_sq;
		dao_set_cell_int (dao, 1 + info->n_c, 4 + i_r * 6, row_cnt);
		dao_set_cell_float (dao, 1 + info->n_c, 5 + i_r * 6, row_sum);
		dao_set_cell_float (dao, 1 + info->n_c, 6 + i_r * 6, row_sum / row_cnt);
		dao_set_cell_float (dao, 1 + info->n_c, 7 + i_r * 6,
				(row_sum_sq - row_sum * row_sum / row_cnt) / (row_cnt - 1));
	}

	for (i_c = 0; i_c < info->n_c; i_c++) {
		gnm_float col_sum = 0.0;
		gnm_float col_sum_sq = 0.0;
		guint col_cnt = 0;

		for (i_r = 0; i_r < info->n_r; i_r++) {
			data_set_t *cell_data;
			gnm_float v;
			int error;
			gnm_float *the_data;
			GPtrArray *data = NULL;

			data = g_ptr_array_index (row_data, i_r);
			cell_data = g_ptr_array_index (data, i_c);
			the_data = (gnm_float *)cell_data->data->data;

			col_cnt += cell_data->data->len;
			error = gnm_range_sum (the_data, cell_data->data->len, &v);
			col_sum += v;

			error = gnm_range_sumsq (the_data, cell_data->data->len, &v);
			col_sum_sq += v;
		}
		ss_c += col_sum * col_sum / col_cnt;
		dao_set_cell_int (dao, 1 + i_c, 4 + info->n_r * 6, col_cnt);
		dao_set_cell_float (dao, 1 + i_c, 5 + info->n_r * 6, col_sum);
		dao_set_cell_float (dao, 1 + i_c, 6 + info->n_r * 6, col_sum / col_cnt);
		dao_set_cell_float (dao, 1 + i_c, 7 + info->n_r * 6,
				(col_sum_sq - col_sum * col_sum / col_cnt) / (col_cnt - 1));
	}

	dao_set_cell_int (dao, 1 + info->n_c, 4 + info->n_r * 6, n);
	dao_set_cell_float (dao, 1 + info->n_c, 5 + info->n_r * 6, cm);
	dao_set_cell_float (dao, 1 + info->n_c, 6 + info->n_r * 6, cm / n);
	cm = cm * cm/ n;
	dao_set_cell_float   (dao, 1 + info->n_c, 7 + info->n_r * 6, (ss_total - cm) / (n - 1));

	df_r = info->n_r - 1;
	df_c = info->n_c - 1;
	df_rc = df_r * df_c;
	df_total = n - 1;
	df_e = df_total - df_rc - df_c - df_r;

	if (missing_observations > 0) {
		/* Oh bother: there is some data missing which means we have to: */
		/* -- Estimate the missing values                                */
                /* -- Recalculate the values obtained towards the SS             */
		/* We couldn't do that above because the Summary table should    */
                /* contain the real values                                       */

		/* Estimate the missing values */

		for (i_r = 0; i_r < info->n_r; i_r++) {
			GPtrArray *data = NULL;

			data = g_ptr_array_index (row_data, i_r);
			for (i_c = 0; i_c < info->n_c; i_c++) {
				data_set_t *cell_data;
				guint num, error;

				cell_data = g_ptr_array_index (data, i_c);
				num = cell_data->data->len;
				if (num < max_sample_size) {
					gnm_float *the_data, v;
					guint i;

					the_data = (gnm_float *)cell_data->data->data;
					error = gnm_range_average (the_data, num, &v);
					for (i = num; i < max_sample_size; i++)
						g_array_append_val (cell_data->data, v);
				}
			}
		}

		/* Recalculate the values obtained towards the SS */

		ss_r = 0;
		ss_c = 0;
		ss_rc = 0;
		ss_total = 0;
		cm = 0;
		n = 0;

		for (i_r = 0; i_r < info->n_r; i_r++) {
			GPtrArray *data = NULL;
			gnm_float row_sum = 0.0;
			gnm_float row_sum_sq = 0.0;
			guint row_cnt = 0;

			data = g_ptr_array_index (row_data, i_r);
			for (i_c = 0; i_c < info->n_c; i_c++) {
				data_set_t *cell_data;
				gnm_float v;
				int error;
				int num;
				gnm_float *the_data;

				cell_data = g_ptr_array_index (data, i_c);
				the_data = (gnm_float *)cell_data->data->data;
				num = cell_data->data->len;
				row_cnt += num;

				error = gnm_range_sum (the_data, num, &v);
				row_sum += v;
				ss_rc += v * v / num;

				error = gnm_range_sumsq (the_data, num, &v);
				row_sum_sq += v;
			}
			cm += row_sum;
			n += row_cnt;
			ss_r += row_sum * row_sum / row_cnt;
			ss_total += row_sum_sq;
		}

		for (i_c = 0; i_c < info->n_c; i_c++) {
			gnm_float col_sum = 0.0;
			guint col_cnt = 0;

			for (i_r = 0; i_r < info->n_r; i_r++) {
				data_set_t *cell_data;
				gnm_float v;
				int error;
				gnm_float *the_data;
				GPtrArray *data = NULL;

				data = g_ptr_array_index (row_data, i_r);
				cell_data = g_ptr_array_index (data, i_c);
				the_data = (gnm_float *)cell_data->data->data;

				col_cnt += cell_data->data->len;
				error = gnm_range_sum (the_data, cell_data->data->len, &v);
				col_sum += v;
			}
			ss_c += col_sum * col_sum / col_cnt;
		}

		cm = cm * cm/ n;
	}

	ss_r = ss_r - cm;
	ss_c = ss_c - cm;
	ss_rc = ss_rc - ss_r - ss_c - cm;
	ss_total = ss_total - cm;
	ss_e = ss_total - ss_r - ss_c - ss_rc;

	dao_set_cell_float (dao, 1, info->n_r * 6 + 12, ss_r);
	dao_set_cell_float (dao, 1, info->n_r * 6 + 13, ss_c);
	dao_set_cell_float (dao, 1, info->n_r * 6 + 14, ss_rc);
	dao_set_cell_float (dao, 1, info->n_r * 6 + 15, ss_e);
	dao_set_cell_float (dao, 1, info->n_r * 6 + 17, ss_total);

	dao_set_cell_int   (dao, 2, info->n_r * 6 + 12, df_r);
	dao_set_cell_int   (dao, 2, info->n_r * 6 + 13, df_c);
	dao_set_cell_int   (dao, 2, info->n_r * 6 + 14, df_rc);
	dao_set_cell_int   (dao, 2, info->n_r * 6 + 15, df_e);
	dao_set_cell_int   (dao, 2, info->n_r * 6 + 17, df_total);

	ms_r = ss_r / df_r;
	ms_c = ss_c / df_c;
	ms_rc = ss_rc / df_rc;
	ms_e = ss_e / df_e;

	dao_set_cell_float_na (dao, 3, info->n_r * 6 + 12, ms_r, df_r > 0);
	dao_set_cell_float_na (dao, 3, info->n_r * 6 + 13, ms_c, df_c > 0);
	dao_set_cell_float_na (dao, 3, info->n_r * 6 + 14, ms_rc, df_rc > 0);
	dao_set_cell_float_na (dao, 3, info->n_r * 6 + 15, ms_e, df_e > 0);

	f_r = ms_r / ms_e;
	f_c = ms_c / ms_e;
	f_rc = ms_rc / ms_e;

	dao_set_cell_float_na (dao, 4, info->n_r * 6 + 12, f_r, ms_e != 0 && df_r > 0);
	dao_set_cell_float_na (dao, 4, info->n_r * 6 + 13, f_c, ms_e != 0 && df_c > 0);
	dao_set_cell_float_na (dao, 4, info->n_r * 6 + 14, f_rc, ms_e != 0 && df_rc > 0);

	p_r = pf (f_r, df_r, df_e, FALSE, FALSE);
	p_c = pf (f_c, df_c, df_e, FALSE, FALSE);
	p_rc = pf (f_rc, df_rc, df_e, FALSE, FALSE);

	dao_set_cell_float_na (dao, 5, info->n_r * 6 + 12, p_r, ms_e != 0 && df_r > 0 && df_e > 0);
	dao_set_cell_float_na (dao, 5, info->n_r * 6 + 13, p_c, ms_e != 0 && df_c > 0 && df_e > 0);
	dao_set_cell_float_na (dao, 5, info->n_r * 6 + 14, p_rc, ms_e != 0 && df_rc > 0 && df_e > 0);

	dao_set_cell_float_na (dao, 6, info->n_r * 6 + 12,
			   qf (info->alpha, df_r, df_e, FALSE, FALSE),
			   df_r > 0 && df_e > 0);
	dao_set_cell_float_na (dao, 6, info->n_r * 6 + 13,
			   qf (info->alpha, df_c, df_e, FALSE, FALSE),
			   df_c > 0 && df_e > 0);
	dao_set_cell_float_na (dao, 6, info->n_r * 6 + 14,
			   qf (info->alpha, df_rc, df_e, FALSE, FALSE),
			   df_rc > 0 && df_e > 0);

	dao_set_italic (dao, 0, info->n_r * 6 + 11, 6, info->n_r * 6 + 11);
	dao_set_italic (dao, 0, 2, 1 + info->n_c, 2);
	dao_set_italic (dao, 0, 0, 0, 17 + info->n_r * 6);

 anova_two_factor_with_r_tool_end:

	for (i_r = 0; i_r < row_data->len; i_r++)
		destroy_data_set_list (g_ptr_array_index (row_data, i_r));
	g_ptr_array_free (row_data, TRUE);

	for (i_c = 0; i_c < info->n_c; i_c++)
		g_free (g_ptr_array_index (col_labels, i_c));
	g_ptr_array_free (col_labels, TRUE);
	for (i_r = 0; i_r < info->n_r; i_r++)
		g_free (g_ptr_array_index (row_labels, i_r));
	g_ptr_array_free (row_labels, TRUE);

	return return_value;
}

static gboolean
analysis_tool_anova_two_factor_engine_clean (G_GNUC_UNUSED data_analysis_output_t *dao,
					     gpointer specs)
{
	analysis_tools_data_anova_two_factor_t *info = specs;

	range_list_destroy (info->col_input_range);
	info->col_input_range = NULL;
	range_list_destroy (info->row_input_range);
	info->row_input_range = NULL;
	if (info->input)
		value_release (info->input);
	info->input = NULL;

	return FALSE;
}

gboolean
analysis_tool_anova_two_factor_engine (data_analysis_output_t *dao, gpointer specs,
				   analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_anova_two_factor_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (
				dao, (info->replication == 1) ?
				_("Two Factor ANOVA (%s), no replication") :
				_("Two Factor ANOVA (%s),  with replication") , result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		if (analysis_tool_anova_two_factor_prepare_input_range (info))
			return TRUE;
		if (info->replication == 1)
			dao_adjust (dao, 7, info->n_c + info->n_r + 12);
		else
			dao_adjust (dao, MAX (2 + info->n_c, 7), info->n_r * 6 + 18);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_anova_two_factor_engine_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Anova"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Two Factor ANOVA"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		if (info->replication == 1)
			return analysis_tool_anova_two_factor_no_rep_engine_run (dao, info);
		else
			return analysis_tool_anova_two_factor_engine_run (dao, info);
	}
	return TRUE;  /* We shouldn't get here */
}



/************* Fourier Analysis Tool **************************************
 *
 * This tool performes a fast fourier transform calculating the fourier
 * transform as defined in Weaver: Theory of dis and cont Fouriere Analysis
 *
 * If the length of the given sequence is not a power of 2, an appropriate
 * number of 0s are added.
 *
 *
 *
 **/

void
gnm_fourier_fft (complex_t const *in, int n, int skip, complex_t **fourier, gboolean inverse)
{
	complex_t  *fourier_1, *fourier_2;
	int        i;
	int        nhalf = n / 2;
	gnm_float argstep;

	*fourier = g_new (complex_t, n);

	if (n == 1) {
		(*fourier)[0] = in[0];
		return;
	}

	gnm_fourier_fft (in, nhalf, skip * 2, &fourier_1, inverse);
	gnm_fourier_fft (in + skip, nhalf, skip * 2, &fourier_2, inverse);

	argstep = (inverse ? M_PIgnum : -M_PIgnum) / nhalf;
	for (i = 0; i < nhalf; i++) {
		complex_t dir, tmp;

		complex_from_polar (&dir, 1, argstep * i);
		complex_mul (&tmp, &fourier_2[i], &dir);

		complex_add (&((*fourier)[i]), &fourier_1[i], &tmp);
		complex_scale_real (&((*fourier)[i]), 0.5);

		complex_sub (&((*fourier)[i + nhalf]), &fourier_1[i], &tmp);
		complex_scale_real (&((*fourier)[i + nhalf]), 0.5);
	}

	g_free (fourier_1);
	g_free (fourier_2);
}

static gboolean
analysis_tool_fourier_engine_run (data_analysis_output_t *dao,
				  analysis_tools_data_fourier_t *info)
{
	GPtrArray     *data;
	guint         dataset;
	gint          col = 0;

	data = new_data_set_list (info->base.input, info->base.group_by,
				  TRUE, info->base.labels, dao->sheet);

	for (dataset = 0; dataset < data->len; dataset++) {
		data_set_t    *current;
		complex_t     *in, *fourier;
		int           row;
		int           given_length;
		int           desired_length = 1;
		int           i;
		gnm_float    zero_val = 0.0;

		current = g_ptr_array_index (data, dataset);
		given_length = current->data->len;
		while (given_length > desired_length)
			desired_length *= 2;
		for (i = given_length; i < desired_length; i++)
			current->data = g_array_append_val (current->data, zero_val);

		dao_set_cell_printf (dao, col, 0, current->label);
		dao_set_cell_printf (dao, col, 1, _("Real"));
		dao_set_cell_printf (dao, col + 1, 1, _("Imaginary"));

		in = g_new (complex_t, desired_length);
		for (i = 0; i < desired_length; i++)
			complex_real (&in[i],
				      ((gnm_float const *)current->data->data)[i]);

		gnm_fourier_fft (in, desired_length, 1, &fourier, info->inverse);
		g_free (in);

		if (fourier) {
			for (row = 0; row < given_length; row++) {
				dao_set_cell_float (dao, col, row + 2,
						fourier[row].re);
				dao_set_cell_float (dao, col + 1, row + 2,
						fourier[row].im);
			}
			g_free (fourier);
		}

		col += 2;
	}
	dao_set_italic (dao, 0, 0, col - 1, 1);

	destroy_data_set_list (data);

	return 0;
}

static int
analysis_tool_fourier_calc_length (analysis_tools_data_fourier_t *info)
{
	Sheet         *sheet = wb_control_cur_sheet(info->base.wbc);
	GPtrArray     *data = new_data_set_list (info->base.input, info->base.group_by,
						 TRUE, info->base.labels, sheet);
	int           result = 1;
	guint         dataset;

	for (dataset = 0; dataset < data->len; dataset++) {
		data_set_t    *current;
		int           given_length;

		current = g_ptr_array_index (data, dataset);
		given_length = current->data->len;
		if (given_length > result)
			result = given_length;
	}
	destroy_data_set_list (data);
	return result;
}


gboolean
analysis_tool_fourier_engine (data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_fourier_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Fourier Series (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		dao_adjust (dao, 2 * g_slist_length (info->base.input),
			    2 + analysis_tool_fourier_calc_length (specs));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (dao, specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Fourier Series"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Fourier Series"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_fourier_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}
