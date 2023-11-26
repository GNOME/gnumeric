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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <tools/analysis-tools.h>

#include <mathfunc.h>
#include <func.h>
#include <expr.h>
#include <position.h>
#include <tools/tools.h>
#include <value.h>
#include <cell.h>
#include <sheet.h>
#include <ranges.h>
#include <parse-util.h>
#include <style.h>
#include <regression.h>
#include <sheet-style.h>
#include <workbook.h>
#include <collect.h>
#include <gnm-format.h>
#include <sheet-object-cell-comment.h>
#include <workbook-control.h>
#include <command-context.h>
#include <sheet-object-graph.h>
#include <graph.h>
#include <goffice/goffice.h>

#include <string.h>
#include <stdlib.h>
#include <math.h>


const GnmExpr *
make_cellref (int dx, int dy)
{
	GnmCellRef r;
	r.sheet = NULL;
	r.col = dx;
	r.col_relative = TRUE;
	r.row = dy;
	r.row_relative = TRUE;
	return gnm_expr_new_cellref (&r);
}

const GnmExpr *
make_rangeref (int dx0, int dy0, int dx1, int dy1)
{
	GnmCellRef a, b;
	GnmValue *val;

	a.sheet = NULL;
	a.col = dx0;
	a.col_relative = TRUE;
	a.row = dy0;
	a.row_relative = TRUE;
	b.sheet = NULL;
	b.col = dx1;
	b.col_relative = TRUE;
	b.row = dy1;
	b.row_relative = TRUE;

	val = value_new_cellrange_unsafe (&a, &b);
	return gnm_expr_new_constant (val);
}


typedef struct {
	char *format;
	GPtrArray *data_lists;
	gboolean read_label;
	gboolean ignore_non_num;
	guint length;
	Sheet *sheet;
} data_list_specs_t;

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

	if (range == NULL || !VALUE_IS_CELLRANGE (range)) {
		return;
	}

	range->v_range.cell.a.col_relative = 0;
	range->v_range.cell.a.row_relative = 0;
	range->v_range.cell.b.col_relative = 0;
	range->v_range.cell.b.row_relative = 0;
}

/*
 *  analysis_tools_remove_label:
 *
 */
static void
analysis_tools_remove_label (GnmValue *val,
			     gboolean labels, group_by_t group_by)
{
	if (labels) {
		switch (group_by) {
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

void
analysis_tools_write_label (GnmValue *val, data_analysis_output_t *dao,
			    analysis_tools_data_generic_t *info,
			    int x, int y, int i)
{
	char const *format = NULL;

	if (info->labels) {
		GnmValue *label = value_dup (val);

		label->v_range.cell.b = label->v_range.cell.a;
		dao_set_cell_expr (dao, x, y, gnm_expr_new_constant (label));
		analysis_tools_remove_label (val, info->labels, info->group_by);
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
 *  @labels: analysis_tools_data_generic_t infowhether the
 *           @val contains label info
 *  @group_by: grouping info
 *  @x: output col number
 *  @y: output row number
 *  @i: default col/row number
 *
 */

static void
analysis_tools_write_a_label (GnmValue *val, data_analysis_output_t *dao,
			      gboolean   labels, group_by_t group_by,
			      int x, int y)
{
	if (labels) {
		GnmValue *label = value_dup (val);

		label->v_range.cell.b = label->v_range.cell.a;
		dao_set_cell_expr (dao, x, y, gnm_expr_new_constant (label));
		analysis_tools_remove_label (val, labels, group_by);
	} else {
		char const *str = ((group_by == GROUPED_BY_ROW) ? "row" : "col");
		char const *label = ((group_by == GROUPED_BY_ROW) ? _("Row") : _("Column"));

		GnmFunc *fd_concatenate;
		GnmFunc *fd_cell;

		fd_concatenate = gnm_func_lookup_or_add_placeholder ("CONCATENATE");
		gnm_func_inc_usage (fd_concatenate);
		fd_cell = gnm_func_lookup_or_add_placeholder ("CELL");
		gnm_func_inc_usage (fd_cell);

		dao_set_cell_expr (dao, x, y, gnm_expr_new_funcall3
				   (fd_concatenate, gnm_expr_new_constant (value_new_string (label)),
				    gnm_expr_new_constant (value_new_string (" ")),
				    gnm_expr_new_funcall2 (fd_cell,
							   gnm_expr_new_constant (value_new_string (str)),
							   gnm_expr_new_constant (value_dup (val)))));

		gnm_func_dec_usage (fd_concatenate);
		gnm_func_dec_usage (fd_cell);
	}
}

/*
 *  analysis_tools_write_label_ftest:
 *  @val: range to extract label from
 *  @dao: data_analysis_output_t, where to write to
 *  @info: analysis_tools_data_generic_t info
 *  @x: output col number
 *  @y: output row number
 *  @i: default col/row number
 *
 */

void
analysis_tools_write_label_ftest (GnmValue *val, data_analysis_output_t *dao,
				  int x, int y, gboolean labels, int i)
{
	cb_adjust_areas (val, NULL);

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
	if (!VALUE_IS_CELLRANGE (range) ||
	    (range->v_range.cell.b.sheet != NULL &&
	     range->v_range.cell.b.sheet != range->v_range.cell.a.sheet)) {
		value_release (range);
		return;
	}

	cb_adjust_areas (data, NULL);

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
	if (!VALUE_IS_CELLRANGE (range) ||
	    (range->v_range.cell.b.sheet != NULL &&
	     range->v_range.cell.b.sheet != range->v_range.cell.a.sheet)) {
		value_release (range);
		return;
	}

	cb_adjust_areas (data, NULL);

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


/**
 *  prepare_input_range:
 *  @input_range: (inout) (element-type GnmRange) (transfer full):
 *  @group_by:
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

	if (!VALUE_IS_CELLRANGE (range)) {
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
static gboolean
gnm_check_input_range_list_homogeneity (GSList *input_range)
{
	homogeneity_check_t state = { FALSE, 0, TRUE };

	g_slist_foreach (input_range, cb_check_hom, &state);

	return state.hom;
}


/***** Some general routines ***********************************************/

/*
 * Set a column of text from a string like "/first/second/third" or "|foo|bar|baz".
 */
void
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
void
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

gboolean
analysis_tool_generic_clean (gpointer specs)
{
	analysis_tools_data_generic_t *info = specs;

	range_list_destroy (info->input);
	info->input = NULL;
	return FALSE;
}

gboolean
analysis_tool_generic_b_clean (gpointer specs)
{
	analysis_tools_data_generic_b_t *info = specs;

	value_release (info->range_1);
	info->range_1 = NULL;
	value_release (info->range_2);
	info->range_2 = NULL;
	return FALSE;
}



int analysis_tool_calc_length (analysis_tools_data_generic_t *info)
{
	int           result = 1;
	GSList        *dataset;

	for (dataset = info->input; dataset; dataset = dataset->next) {
		GnmValue    *current = dataset->data;
		int      given_length;

		if (info->group_by == GROUPED_BY_AREA) {
			given_length = (current->v_range.cell.b.row - current->v_range.cell.a.row + 1) *
				(current->v_range.cell.b.col - current->v_range.cell.a.col + 1);
		} else
			given_length = (info->group_by == GROUPED_BY_COL) ?
				(current->v_range.cell.b.row - current->v_range.cell.a.row + 1) :
				(current->v_range.cell.b.col - current->v_range.cell.a.col + 1);
		if (given_length > result)
			result = given_length;
	}
	if (info->labels)
		result--;
	return result;
}

/**
 * analysis_tool_get_function:
 * @name: name of function
 * @dao:
 *
 * Returns: (transfer full): the function named @name or a placeholder.
 * The usage count of the function is incremented.
 */
GnmFunc *
analysis_tool_get_function (char const *name,
			    data_analysis_output_t *dao)
{
	GnmFunc *fd;

	fd = gnm_func_lookup_or_add_placeholder (name);
	gnm_func_inc_usage (fd);
	return fd;
}



/************* Correlation Tool *******************************************
 *
 * The correlation tool calculates the correlation coefficient of two
 * data sets.  The two data sets can be grouped by rows or by columns.
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

gboolean
analysis_tool_table (data_analysis_output_t *dao,
		     analysis_tools_data_generic_t *info,
		     gchar const *title, gchar const *functionname,
		     gboolean full_table)
{
	GSList *inputdata, *inputexpr = NULL;
	GnmFunc *fd = NULL;

	guint col, row;

	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell_printf (dao, 0, 0, "%s", title);

	fd = gnm_func_lookup_or_add_placeholder (functionname);
	gnm_func_inc_usage (fd);

	for (col = 1, inputdata = info->input; inputdata != NULL;
	     inputdata = inputdata->next, col++) {
		GnmValue *val = NULL;

		val = value_dup (inputdata->data);

		/* Label */
		dao_set_italic (dao, col, 0, col, 0);
		analysis_tools_write_label (val, dao, info,
					    col, 0, col);

		inputexpr = g_slist_prepend (inputexpr,
					     (gpointer) gnm_expr_new_constant (val));
	}
	inputexpr = g_slist_reverse (inputexpr);

	for (row = 1, inputdata = info->input; inputdata != NULL;
	     inputdata = inputdata->next, row++) {
		GnmValue *val = value_dup (inputdata->data);
		GSList *colexprlist;

		/* Label */
		dao_set_italic (dao, 0, row, 0, row);
		analysis_tools_write_label (val, dao, info,
					    0, row, row);

		for (col = 1, colexprlist = inputexpr; colexprlist != NULL;
		     colexprlist = colexprlist->next, col++) {
			GnmExpr const *colexpr = colexprlist->data;

			if ((!full_table) && (col < row))
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

	g_slist_free_full (inputexpr, (GDestroyNotify)gnm_expr_free);
	if (fd) gnm_func_dec_usage (fd);

	dao_redraw_respan (dao);
	return FALSE;
}

static gboolean
analysis_tool_correlation_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_generic_t *info)
{
	return analysis_tool_table (dao, info, _("Correlations"),
				    "CORREL", FALSE);
}

gboolean
analysis_tool_correlation_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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
		return analysis_tool_generic_clean (specs);
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
	return analysis_tool_table (dao, info, _("Covariances"),
				    "COVAR", FALSE);
}

gboolean
analysis_tool_covariance_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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
		return analysis_tool_generic_clean (specs);
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

	fd_mean = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_mean);
	fd_median = gnm_func_lookup_or_add_placeholder (info->use_ssmedian ? "SSMEDIAN" : "MEDIAN");
	gnm_func_inc_usage (fd_median);
	fd_mode = gnm_func_lookup_or_add_placeholder ("MODE");
	gnm_func_inc_usage (fd_mode);
	fd_stdev = gnm_func_lookup_or_add_placeholder ("STDEV");
	gnm_func_inc_usage (fd_stdev);
	fd_var = gnm_func_lookup_or_add_placeholder ("VAR");
	gnm_func_inc_usage (fd_var);
	fd_kurt = gnm_func_lookup_or_add_placeholder ("KURT");
	gnm_func_inc_usage (fd_kurt);
	fd_skew = gnm_func_lookup_or_add_placeholder ("SKEW");
	gnm_func_inc_usage (fd_skew);
	fd_min = gnm_func_lookup_or_add_placeholder ("MIN");
	gnm_func_inc_usage (fd_min);
	fd_max = gnm_func_lookup_or_add_placeholder ("MAX");
	gnm_func_inc_usage (fd_max);
	fd_sum = gnm_func_lookup_or_add_placeholder ("SUM");
	gnm_func_inc_usage (fd_sum);
	fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
	gnm_func_inc_usage (fd_count);
	fd_sqrt = gnm_func_lookup_or_add_placeholder ("SQRT");
	gnm_func_inc_usage (fd_sqrt);

        dao_set_cell (dao, 0, 0, NULL);

	dao_set_italic (dao, 0, 1, 0, 13);
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

		dao_set_italic (dao, col + 1, 0, col+1, 0);
		/* Note that analysis_tools_write_label may modify val_org */
		analysis_tools_write_label (val_org, dao, &info->base,
					    col + 1, 0, col + 1);

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

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_median);
	gnm_func_dec_usage (fd_mode);
	gnm_func_dec_usage (fd_stdev);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_kurt);
	gnm_func_dec_usage (fd_skew);
	gnm_func_dec_usage (fd_min);
	gnm_func_dec_usage (fd_max);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_sqrt);
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
	dao_set_italic (dao, 0, 1, 0, 2);
	set_cell_text_col (dao, 0, 1, buffer);
        g_free (buffer);

        dao_set_cell (dao, 0, 0, NULL);

	fd_mean = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_mean);
	fd_var = gnm_func_lookup_or_add_placeholder ("VAR");
	gnm_func_inc_usage (fd_var);
	fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
	gnm_func_inc_usage (fd_count);
	fd_tinv = gnm_func_lookup_or_add_placeholder ("TINV");
	gnm_func_inc_usage (fd_tinv);
	fd_sqrt = gnm_func_lookup_or_add_placeholder ("SQRT");
	gnm_func_inc_usage (fd_sqrt);


	for (col = 0; data != NULL; data = data->next, col++) {
		GnmExpr const *expr;
		GnmExpr const *expr_mean;
		GnmExpr const *expr_var;
		GnmExpr const *expr_count;
		GnmValue *val_org = value_dup (data->data);

		dao_set_italic (dao, col+1, 0, col+1, 0);
		/* Note that analysis_tools_write_label may modify val_org */
		analysis_tools_write_label (val_org, dao, &info->base, col + 1, 0, col + 1);

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

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_tinv);
	gnm_func_dec_usage (fd_sqrt);
}

static void
kth_smallest_largest (data_analysis_output_t *dao,
		      analysis_tools_data_descriptive_t *info,
		      char const* func, char const* label, int k)
{
        guint col;
	GSList *data = info->base.input;
	GnmFunc *fd = gnm_func_lookup_or_add_placeholder (func);
	gnm_func_inc_usage (fd);

	dao_set_italic (dao, 0, 1, 0, 1);
        dao_set_cell_printf (dao, 0, 1, label, k);

        dao_set_cell (dao, 0, 0, NULL);

	for (col = 0; data != NULL; data = data->next, col++) {
		GnmExpr const *expr = NULL;
		GnmValue *val = value_dup (data->data);

		dao_set_italic (dao, col + 1, 0, col + 1, 0);
		analysis_tools_write_label (val, dao, &info->base,
					    col + 1, 0, col + 1);

		expr = gnm_expr_new_funcall2
			(fd,
			 gnm_expr_new_constant (val),
			 gnm_expr_new_constant (value_new_int (k)));

		dao_set_cell_expr (dao, col + 1, 1, expr);
	}

	gnm_func_dec_usage (fd);
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
analysis_tool_descriptive_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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
		return analysis_tool_generic_clean (specs);
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
	GSList *l;
	gint col = 0;
	guint ct;
	GnmFunc *fd_index = NULL;
	GnmFunc *fd_randdiscrete = NULL;
	gint source;

	if (info->base.labels || info->periodic) {
		fd_index = gnm_func_lookup_or_add_placeholder ("INDEX");
		gnm_func_inc_usage (fd_index);
	}
	if (!info->periodic) {
		fd_randdiscrete = gnm_func_lookup_or_add_placeholder ("RANDDISCRETE");
		gnm_func_inc_usage (fd_randdiscrete);
	}

	for (l = info->base.input, source = 1; l; l = l->next, source++) {
		GnmValue *val = value_dup ((GnmValue *)l->data);
		GnmValue *val_c = NULL;
		GnmExpr const *expr_title = NULL;
		GnmExpr const *expr_input = NULL;
		char const *format = NULL;
		guint offset = info->periodic ? ((info->offset == 0) ? info->period : info->offset): 0;
		GnmEvalPos ep;

		eval_pos_init_sheet (&ep, val->v_range.cell.a.sheet);

		dao_set_italic (dao, col, 0, col + info->number - 1, 0);

		if (info->base.labels) {
			val_c = value_dup (val);
			switch (info->base.group_by) {
			case GROUPED_BY_ROW:
				val->v_range.cell.a.col++;
				break;
			case GROUPED_BY_COL:
				val->v_range.cell.a.row++;
				break;
			default:
				offset++;
				break;
			}
			expr_title = gnm_expr_new_funcall1 (fd_index,
							    gnm_expr_new_constant (val_c));
			for (ct = 0; ct < info->number; ct++)
				dao_set_cell_expr (dao, col+ct, 0, gnm_expr_copy (expr_title));
			gnm_expr_free (expr_title);
		} else {
			switch (info->base.group_by) {
			case GROUPED_BY_ROW:
				format = _("Row %d");
				break;
			case GROUPED_BY_COL:
				format = _("Column %d");
				break;
			default:
				format = _("Area %d");
				break;
			}
			for (ct = 0; ct < info->number; ct++)
				dao_set_cell_printf (dao, col+ct, 0, format, source);
		}

		expr_input = gnm_expr_new_constant (value_dup (val));


		if (info->periodic) {
			guint i;
			gint height = value_area_get_height (val, &ep);
			gint width = value_area_get_width (val, &ep);
			GnmExpr const *expr_period;

			for (i=0; i < info->size; i++, offset += info->period) {
				gint x_offset;
				gint y_offset;

				if (info->row_major) {
					y_offset = (offset - 1)/width + 1;
					x_offset = offset - (y_offset - 1) * width;
				} else {
					x_offset = (offset - 1)/height + 1;
					y_offset = offset - (x_offset - 1) * height;
				}

				expr_period = gnm_expr_new_funcall3
					(fd_index, gnm_expr_copy (expr_input),
					 gnm_expr_new_constant (value_new_int (y_offset)),
					 gnm_expr_new_constant (value_new_int (x_offset)));

				for (ct = 0; ct < info->number; ct += 2)
					dao_set_cell_expr (dao, col + ct, i + 1,
							   gnm_expr_copy (expr_period));
				gnm_expr_free (expr_period);

				if (info->number > 1) {
					if (!info->row_major) {
						y_offset = (offset - 1)/width + 1;
						x_offset = offset - (y_offset - 1) * width;
					} else {
						x_offset = (offset - 1)/height + 1;
						y_offset = offset - (x_offset - 1) * height;
					}

					expr_period = gnm_expr_new_funcall3
						(fd_index, gnm_expr_copy (expr_input),
						 gnm_expr_new_constant (value_new_int (y_offset)),
						 gnm_expr_new_constant (value_new_int (x_offset)));

					for (ct = 1; ct < info->number; ct += 2)
						dao_set_cell_expr (dao, col + ct, i + 1,
								   gnm_expr_copy (expr_period));
					gnm_expr_free (expr_period);

				}
			}
			col += info->number;
		} else {
			GnmExpr const *expr_random;
			guint i;

			expr_random = gnm_expr_new_funcall1 (fd_randdiscrete,
							     gnm_expr_copy (expr_input));

			for (ct = 0; ct < info->number; ct++, col++)
				for (i=0; i < info->size; i++)
					dao_set_cell_expr (dao, col, i + 1,
							   gnm_expr_copy (expr_random));
			gnm_expr_free (expr_random);
		}

		value_release (val);
		gnm_expr_free (expr_input);

	}

	if (fd_index != NULL)
		gnm_func_dec_usage (fd_index);
	if (fd_randdiscrete != NULL)
		gnm_func_dec_usage (fd_randdiscrete);

	dao_redraw_respan (dao);

	return FALSE;
}

gboolean
analysis_tool_sampling_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			       analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_sampling_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Sampling (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
	{
		GSList *l;

		prepare_input_range (&info->base.input, info->base.group_by);

		if (info->periodic) {
			info->size = 1;
			for (l = info->base.input; l; l = l->next) {
				GnmEvalPos ep;
				GnmValue *val = ((GnmValue *)l->data);
				gint size;
				guint usize;
				eval_pos_init_sheet (&ep, val->v_range.cell.a.sheet);
				size = (value_area_get_width (val, &ep) *
					     value_area_get_height (val, &ep));
				usize = (size > 0) ? size : 1;

				if (info->offset == 0)
					usize = usize/info->period;
				else
					usize = (usize - info->offset)/info->period + 1;
				if (usize > info->size)
					info->size = usize;
			}
		}

		dao_adjust (dao, info->number * g_slist_length (info->base.input),
			    1 + info->size);
		return FALSE;
	}
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (specs);
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

	dao_set_italic (dao, 0, 0, 0, 11);
	dao_set_italic (dao, 0, 0, 2, 0);

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

	fd_mean = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_mean);
	fd_normsdist = gnm_func_lookup_or_add_placeholder ("NORMSDIST");
	gnm_func_inc_usage (fd_normsdist);
	fd_abs = gnm_func_lookup_or_add_placeholder ("ABS");
	gnm_func_inc_usage (fd_abs);
	fd_sqrt = gnm_func_lookup_or_add_placeholder ("SQRT");
	gnm_func_inc_usage (fd_sqrt);
	fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
	gnm_func_inc_usage (fd_count);
	fd_normsinv = gnm_func_lookup_or_add_placeholder ("NORMSINV");
	gnm_func_inc_usage (fd_normsinv);

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
		gnm_expr_free (expr_mean_2);
		expr_mean_2 = make_cellref (1, -4);
	}

	{
		dao_set_cell_expr (dao, 1, 5,
				   gnm_expr_new_binary
				   (make_cellref (0, -4),
				    GNM_EXPR_OP_SUB,
				    expr_mean_2));
	}

	/* z */
	{
		GnmExpr const *expr_var_1 = make_cellref (0, -4);
		GnmExpr const *expr_var_2 = NULL;
		GnmExpr const *expr_count_1 = make_cellref (0, -3);
		GnmExpr const *expr_a = NULL;
		GnmExpr const *expr_b = NULL;
		GnmExpr const *expr_count_2_adj = NULL;

		if (dao_cell_is_visible (dao, 2, 2)) {
			expr_var_2 = make_cellref (1, -4);
		} else {
			expr_var_2 = gnm_expr_new_constant
			(value_new_float (info->var2));
		}

		if (dao_cell_is_visible (dao, 2, 3)) {
			gnm_expr_free (expr_count_2);
			expr_count_2_adj = make_cellref (1, -3);
		} else
			expr_count_2_adj = expr_count_2;

		expr_a = gnm_expr_new_binary (expr_var_1, GNM_EXPR_OP_DIV,
					      expr_count_1);
		expr_b = gnm_expr_new_binary (expr_var_2, GNM_EXPR_OP_DIV,
					      expr_count_2_adj);

		dao_set_cell_expr (dao, 1, 6,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (make_cellref (0, -1),
				     GNM_EXPR_OP_SUB,
				     make_cellref (0, -2)),
				    GNM_EXPR_OP_DIV,
				    gnm_expr_new_funcall1
				    (fd_sqrt,
				     gnm_expr_new_binary
				     (expr_a,
				      GNM_EXPR_OP_ADD,
				      expr_b))));
	}

	/* P (Z<=z) one-tail */
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
		    make_cellref (0, -1)))));


	/* Critical Z, one right tail */
	dao_set_cell_expr
		(dao, 1, 8,
		 gnm_expr_new_unary
		 (GNM_EXPR_OP_UNARY_NEG,
		  gnm_expr_new_funcall1
		  (fd_normsinv,
		   gnm_expr_new_constant
		   (value_new_float (info->base.alpha)))));

	/* P (T<=t) two-tail */
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
		     make_cellref (0, -3))))));

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

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_normsdist);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_sqrt);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_normsinv);

	/* And finish up */

	value_release (val_1);
	value_release (val_2);

	dao_redraw_respan (dao);

        return FALSE;
}


gboolean
analysis_tool_ztest_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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
		return analysis_tool_generic_b_clean (specs);
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
	GnmFunc *fd_isodd;
	GnmFunc *fd_isnumber;
	GnmFunc *fd_if;
	GnmFunc *fd_sum;

	GnmExpr const *expr_1;
	GnmExpr const *expr_2;
	GnmExpr const *expr_diff;
	GnmExpr const *expr_ifisnumber;
	GnmExpr const *expr_ifisoddifisnumber;

	dao_set_italic (dao, 0, 0, 0, 13);
	dao_set_italic (dao, 0, 0, 2, 0);

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

	fd_mean = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_mean);
	fd_var = gnm_func_lookup_or_add_placeholder ("VAR");
	gnm_func_inc_usage (fd_var);
	fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
	gnm_func_inc_usage (fd_count);
	fd_correl = gnm_func_lookup_or_add_placeholder ("CORREL");
	gnm_func_inc_usage (fd_correl);
	fd_tinv = gnm_func_lookup_or_add_placeholder ("TINV");
	gnm_func_inc_usage (fd_tinv);
	fd_tdist = gnm_func_lookup_or_add_placeholder ("TDIST");
	gnm_func_inc_usage (fd_tdist);
	fd_abs = gnm_func_lookup_or_add_placeholder ("ABS");
	gnm_func_inc_usage (fd_abs);
	fd_isodd = gnm_func_lookup_or_add_placeholder ("ISODD");
	gnm_func_inc_usage (fd_isodd);
	fd_isnumber = gnm_func_lookup_or_add_placeholder ("ISNUMBER");
	gnm_func_inc_usage (fd_isnumber);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF");
	gnm_func_inc_usage (fd_if);
	fd_sum = gnm_func_lookup_or_add_placeholder ("SUM");
	gnm_func_inc_usage (fd_sum);

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

	/* Some useful expressions for the next field */

	expr_diff = gnm_expr_new_binary (expr_1, GNM_EXPR_OP_SUB, expr_2);

	/* IF (ISNUMBER (area1), 1, 0) * IF (ISNUMBER (area2), 1, 0)  */
	expr_ifisnumber = gnm_expr_new_binary (gnm_expr_new_funcall3 (
						       fd_if,
						       gnm_expr_new_funcall1 (
							       fd_isnumber,
							       gnm_expr_copy (expr_1)),
						       gnm_expr_new_constant (value_new_int (1)),
						       gnm_expr_new_constant (value_new_int (0))),
					       GNM_EXPR_OP_MULT,
					       gnm_expr_new_funcall3 (
						       fd_if,
						       gnm_expr_new_funcall1 (
							       fd_isnumber,
							       gnm_expr_copy (expr_2)),
						       gnm_expr_new_constant (value_new_int (1)),
						       gnm_expr_new_constant (value_new_int (0)))
		);
	/* IF (ISODD (expr_ifisnumber), area1-area2, "NA")*/
	expr_ifisoddifisnumber = gnm_expr_new_funcall3 (fd_if,
							gnm_expr_new_funcall1 (fd_isodd,
									       gnm_expr_copy (expr_ifisnumber)),
							expr_diff,
							gnm_expr_new_constant (value_new_string ("NA")));

	/* Observed Mean Difference */
	dao_set_cell_array_expr (dao, 1, 6,
				 gnm_expr_new_funcall1 (fd_mean,
							gnm_expr_copy (expr_ifisoddifisnumber)));

	/* Variance of the Differences */
	dao_set_cell_array_expr (dao, 1, 7,
				 gnm_expr_new_funcall1 (fd_var,
							expr_ifisoddifisnumber));

	/* df */
	dao_set_cell_array_expr (dao, 1, 8,
				 gnm_expr_new_binary
				 (gnm_expr_new_funcall1 (
					 fd_sum,
					 expr_ifisnumber),
				  GNM_EXPR_OP_SUB,
				  gnm_expr_new_constant (value_new_int (1))));

	/* t */
	/* E24 = (E21-E20)/(E22/(E23+1))^0.5 */
	{
		GnmExpr const *expr_num;
		GnmExpr const *expr_denom;

		expr_num = gnm_expr_new_binary (make_cellref (0, -3),
						GNM_EXPR_OP_SUB,
						make_cellref (0,-4));

		expr_denom = gnm_expr_new_binary
			(gnm_expr_new_binary
			 (make_cellref (0, -2),
			  GNM_EXPR_OP_DIV,
			  gnm_expr_new_binary
			  (make_cellref (0, -1),
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
	dao_set_cell_expr
		(dao, 1, 10,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1
		  (fd_abs,
		   make_cellref (0, -1)),
		  make_cellref (0, -2),
		  gnm_expr_new_constant (value_new_int (1))));

	/* t Critical one-tail */
	dao_set_cell_expr
		(dao, 1, 11,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_binary
		  (gnm_expr_new_constant (value_new_int (2)),
		   GNM_EXPR_OP_MULT,
		   gnm_expr_new_constant
		   (value_new_float (info->base.alpha))),
		  make_cellref (0, -3)));

	/* P (T<=t) two-tail */
	dao_set_cell_expr
		(dao, 1, 12,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1 (fd_abs, make_cellref (0, -3)),
		  make_cellref (0, -4),
		  gnm_expr_new_constant (value_new_int (2))));

	/* t Critical two-tail */
	dao_set_cell_expr
		(dao, 1, 13,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_constant
		  (value_new_float (info->base.alpha)),
		  make_cellref (0, -5)));

	/* And finish up */

	value_release (val_1);
	value_release (val_2);

	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_correl);
	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_tinv);
	gnm_func_dec_usage (fd_tdist);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_isodd);
	gnm_func_dec_usage (fd_isnumber);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_sum);

	dao_redraw_respan (dao);

	return FALSE;
}

gboolean
analysis_tool_ttest_paired_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
				   analysis_tool_engine_t selector,
				   gpointer result)
{
	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("t-Test, paired (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		dao_adjust (dao, 3, 14);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_b_clean (specs);
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

	dao_set_italic (dao, 0, 0, 0, 12);
	dao_set_italic (dao, 0, 0, 2, 0);

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

	fd_mean = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_mean);
	fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
	gnm_func_inc_usage (fd_count);
	fd_var = gnm_func_lookup_or_add_placeholder ("VAR");
	gnm_func_inc_usage (fd_var);
	fd_tdist = gnm_func_lookup_or_add_placeholder ("TDIST");
	gnm_func_inc_usage (fd_tdist);
	fd_abs = gnm_func_lookup_or_add_placeholder ("ABS");
	gnm_func_inc_usage (fd_abs);
	fd_tinv = gnm_func_lookup_or_add_placeholder ("TINV");
	gnm_func_inc_usage (fd_tinv);

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
		GnmExpr const *expr_var_1 = make_cellref (0, -2);
		GnmExpr const *expr_count_1 = make_cellref (0, -1);
		GnmExpr const *expr_one = gnm_expr_new_constant
			(value_new_int (1));
		GnmExpr const *expr_count_1_minus_1;
		GnmExpr const *expr_count_2_minus_1;

		if (dao_cell_is_visible (dao, 2, 2)) {
			gnm_expr_free (expr_var_2);
			expr_var_2_adj = make_cellref (1, -2);
		} else
			expr_var_2_adj = expr_var_2;

		if (dao_cell_is_visible (dao, 2, 3)) {
			expr_count_2_adj = make_cellref (1, -1);
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
		gnm_expr_free (expr_mean_2);
		expr_mean_2 = make_cellref (1, -5);
	}
	dao_set_cell_expr (dao, 1, 6,
			   gnm_expr_new_binary
			   (make_cellref (0, -5),
			    GNM_EXPR_OP_SUB,
			    expr_mean_2));

	/* df */
	{
		GnmExpr const *expr_count_1 = make_cellref (0, -4);
		GnmExpr const *expr_count_2_adj;
		GnmExpr const *expr_two = gnm_expr_new_constant
			(value_new_int (2));

		if (dao_cell_is_visible (dao, 2,3)) {
			expr_count_2_adj = make_cellref (1, -4);
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
		GnmExpr const *expr_var = make_cellref (0, -4);
		GnmExpr const *expr_count_1 = make_cellref (0, -5);
		GnmExpr const *expr_a;
		GnmExpr const *expr_b;
		GnmExpr const *expr_count_2_adj;

		if (dao_cell_is_visible (dao, 2,3)) {
			gnm_expr_free (expr_count_2);
			expr_count_2_adj = make_cellref (1, -5);
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
				    (make_cellref (0, -2),
				     GNM_EXPR_OP_SUB,
				     make_cellref (0, -3)),
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
	dao_set_cell_expr
		(dao, 1, 9,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1
		  (fd_abs,
		   make_cellref (0, -1)),
		  make_cellref (0, -2),
		  gnm_expr_new_constant (value_new_int (1))));

	/* t Critical one-tail */
	dao_set_cell_expr
		(dao, 1, 10,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_binary
		  (gnm_expr_new_constant (value_new_int (2)),
		   GNM_EXPR_OP_MULT,
		   gnm_expr_new_constant
		   (value_new_float (info->base.alpha))),
		  make_cellref (0, -3)));

	/* P (T<=t) two-tail */
	dao_set_cell_expr
		(dao, 1, 11,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1
		  (fd_abs,
		   make_cellref (0, -3)),
		  make_cellref (0, -4),
		  gnm_expr_new_constant (value_new_int (2))));

	/* t Critical two-tail */
	dao_set_cell_expr
		(dao, 1, 12,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_constant
		  (value_new_float (info->base.alpha)),
		  make_cellref (0, -5)));

	/* And finish up */

	value_release (val_1);
	value_release (val_2);

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_tdist);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_tinv);

	dao_redraw_respan (dao);

	return FALSE;
}

gboolean
analysis_tool_ttest_eqvar_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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
		return analysis_tool_generic_b_clean (specs);
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

	dao_set_italic (dao, 0, 0, 0, 11);
	dao_set_italic (dao, 0, 0, 2, 0);

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

	fd_mean = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_mean);
	fd_var = gnm_func_lookup_or_add_placeholder ("VAR");
	gnm_func_inc_usage (fd_var);
	fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
	gnm_func_inc_usage (fd_count);
	fd_tdist = gnm_func_lookup_or_add_placeholder ("TDIST");
	gnm_func_inc_usage (fd_tdist);
	fd_abs = gnm_func_lookup_or_add_placeholder ("ABS");
	gnm_func_inc_usage (fd_abs);
	fd_tinv = gnm_func_lookup_or_add_placeholder ("TINV");
	gnm_func_inc_usage (fd_tinv);

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
		gnm_expr_free (expr_mean_2);
		expr_mean_2 = make_cellref (1, -4);
	}
	dao_set_cell_expr (dao, 1, 5,
			   gnm_expr_new_binary
			   (make_cellref (0, -4),
			    GNM_EXPR_OP_SUB,
			    expr_mean_2));

	/* df */

	{
		GnmExpr const *expr_var_1 = make_cellref (0, -4);
		GnmExpr const *expr_count_1 = make_cellref (0, -3);
		GnmExpr const *expr_a;
		GnmExpr const *expr_b;
		GnmExpr const *expr_var_2_adj;
		GnmExpr const *expr_count_2_adj;
		GnmExpr const *expr_two = gnm_expr_new_constant
			(value_new_int (2));
		GnmExpr const *expr_one = gnm_expr_new_constant
			(value_new_int (1));

		if (dao_cell_is_visible (dao, 2,2)) {
			expr_var_2_adj = make_cellref (1, -4);
		} else
			expr_var_2_adj = gnm_expr_copy (expr_var_2);

		if (dao_cell_is_visible (dao, 2,3)) {
			expr_count_2_adj = make_cellref (1, -3);
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
		GnmExpr const *expr_var_1 = make_cellref (0, -5);
		GnmExpr const *expr_count_1 = make_cellref (0, -4);
		GnmExpr const *expr_a;
		GnmExpr const *expr_b;
		GnmExpr const *expr_var_2_adj;
		GnmExpr const *expr_count_2_adj;

		if (dao_cell_is_visible (dao, 2,2)) {
			gnm_expr_free (expr_var_2);
			expr_var_2_adj = make_cellref (1, -5);
		} else
			expr_var_2_adj = expr_var_2;
		if (dao_cell_is_visible (dao, 2,3)) {
			gnm_expr_free (expr_count_2);
			expr_count_2_adj = make_cellref (1, -4);
		} else
			expr_count_2_adj = expr_count_2;

		expr_a = gnm_expr_new_binary (expr_var_1, GNM_EXPR_OP_DIV,
					      expr_count_1);
		expr_b = gnm_expr_new_binary (expr_var_2_adj, GNM_EXPR_OP_DIV,
					      expr_count_2_adj);

		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (make_cellref (0, -2),
				     GNM_EXPR_OP_SUB,
				     make_cellref (0, -3)),
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
	dao_set_cell_expr
		(dao, 1, 8,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1 (fd_abs,
					 make_cellref (0, -1)),
		  make_cellref (0, -2),
		  gnm_expr_new_constant (value_new_int (1))));

	/* t Critical one-tail */
        /* H10 = tinv(2*alpha,Sheet1!H7) */
	dao_set_cell_expr
		(dao, 1, 9,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_binary
		  (gnm_expr_new_constant (value_new_int (2)),
		   GNM_EXPR_OP_MULT,
		   gnm_expr_new_constant
		   (value_new_float (info->base.alpha))),
		  make_cellref (0, -3)));

	/* P (T<=t) two-tail */
	/* I11: =tdist(abs(Sheet1!I8),Sheet1!I7,1) */
	dao_set_cell_expr
		(dao, 1, 10,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1 (fd_abs,
					 make_cellref (0, -3)),
		  make_cellref (0, -4),
		  gnm_expr_new_constant (value_new_int (2))));

	/* t Critical two-tail */
	dao_set_cell_expr
		(dao, 1, 11,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_constant
		  (value_new_float (info->base.alpha)),
		  make_cellref (0, -5)));

	/* And finish up */

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_tdist);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_tinv);

	value_release (val_1);
	value_release (val_2);

	dao_redraw_respan (dao);
	return FALSE;
}

gboolean
analysis_tool_ttest_neqvar_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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
		return analysis_tool_generic_b_clean (specs);
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
				analysis_tools_data_generic_b_t *info)
{
	GnmValue *val_1 = value_dup (info->range_1);
	GnmValue *val_2 = value_dup (info->range_2);
	GnmExpr const *expr;
	GnmExpr const *expr_var_denum;
	GnmExpr const *expr_count_denum;
	GnmExpr const *expr_df_denum = NULL;

	GnmFunc *fd_finv;

	fd_finv = gnm_func_lookup_or_add_placeholder ("FINV");
	gnm_func_inc_usage (fd_finv);

	dao_set_italic (dao, 0, 0, 0, 11);
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

	/* Label */
	dao_set_italic (dao, 0, 0, 2, 0);
	analysis_tools_write_label_ftest (val_1, dao, 1, 0, info->labels, 1);
	analysis_tools_write_label_ftest (val_2, dao, 2, 0, info->labels, 2);

	/* Mean */
	{
		GnmFunc *fd_mean = gnm_func_lookup_or_add_placeholder ("AVERAGE");
		gnm_func_inc_usage (fd_mean);

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

		gnm_func_dec_usage (fd_mean);
	}

	/* Variance */
	{
		GnmFunc *fd_var = gnm_func_lookup_or_add_placeholder ("VAR");
		gnm_func_inc_usage (fd_var);

		dao_set_cell_expr
			(dao, 1, 2,
			 gnm_expr_new_funcall1
			 (fd_var,
			  gnm_expr_new_constant (value_dup (val_1))));

		expr_var_denum = gnm_expr_new_funcall1
			(fd_var,
			 gnm_expr_new_constant (value_dup (val_2)));
		dao_set_cell_expr (dao, 2, 2, gnm_expr_copy (expr_var_denum));

		gnm_func_dec_usage (fd_var);
	}

        /* Count */
	{
		GnmFunc *fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
		gnm_func_inc_usage (fd_count);

		dao_set_cell_expr
			(dao, 1, 3,
			 gnm_expr_new_funcall1
			 (fd_count,
			  gnm_expr_new_constant (value_dup (val_1))));

		expr_count_denum = gnm_expr_new_funcall1
			(fd_count,
			 gnm_expr_new_constant (value_dup (val_2)));
		dao_set_cell_expr (dao, 2, 3, gnm_expr_copy (expr_count_denum));

		gnm_func_dec_usage (fd_count);
	}

	/* df */
	{
		expr = gnm_expr_new_binary
			(make_cellref (0, -1),
			 GNM_EXPR_OP_SUB,
			 gnm_expr_new_constant (value_new_int (1)));
		dao_set_cell_expr (dao, 1, 4, gnm_expr_copy (expr));
		dao_set_cell_expr (dao, 2, 4, expr);
	}

	/* F value */
	if (dao_cell_is_visible (dao, 2, 2)) {
		expr = gnm_expr_new_binary
			(make_cellref (0, -3),
			 GNM_EXPR_OP_DIV,
			 make_cellref (1, -3));
		gnm_expr_free (expr_var_denum);
	} else {
		expr = gnm_expr_new_binary
			(make_cellref (0, -3),
			 GNM_EXPR_OP_DIV,
			 expr_var_denum);
	}
	dao_set_cell_expr (dao, 1, 5, expr);

	/* P right-tail */
	{
		GnmFunc *fd_fdist = gnm_func_lookup_or_add_placeholder ("FDIST");
		const GnmExpr *arg3;

		gnm_func_inc_usage (fd_fdist);

		if (dao_cell_is_visible (dao, 2, 2)) {
			arg3 = make_cellref (1, -2);
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
			  make_cellref (0, -1),
			  make_cellref (0, -2),
			  arg3));

		gnm_func_dec_usage (fd_fdist);
	}

	/* F critical right-tail */
	{
		const GnmExpr *arg3;

		if (expr_df_denum == NULL) {
			arg3 = make_cellref (1, -3);
		} else {
			arg3 = gnm_expr_copy (expr_df_denum);
		}

		dao_set_cell_expr
			(dao, 1, 7,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant (value_new_float (info->alpha)),
			  make_cellref (0, -3),
			  arg3));
	}

	/* P left-tail */
	dao_set_cell_expr (dao, 1, 8,
			   gnm_expr_new_binary
			   (gnm_expr_new_constant (value_new_int (1)),
			    GNM_EXPR_OP_SUB,
			    make_cellref (0, -2)));

	/* F critical left-tail */
	{
		const GnmExpr *arg3;

		if (expr_df_denum == NULL) {
			arg3 = make_cellref (1, -5);
		} else {
			arg3 = gnm_expr_copy (expr_df_denum);
		}

		dao_set_cell_expr
			(dao, 1, 9,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant
			  (value_new_float (1 - info->alpha)),
			  make_cellref (0, -5),
			  arg3));
	}

	/* P two-tail */
	{
		GnmFunc *fd_min = gnm_func_lookup_or_add_placeholder ("MIN");

		gnm_func_inc_usage (fd_min);

		dao_set_cell_expr
			(dao, 1, 10,
			 gnm_expr_new_binary
			 (gnm_expr_new_constant (value_new_int (2)),
			  GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall2
			  (fd_min,
			   make_cellref (0, -4),
			   make_cellref (0, -2))));
		gnm_func_dec_usage (fd_min);
	}

	/* F critical two-tail (left) */
	{
		const GnmExpr *arg3;

		if (expr_df_denum == NULL) {
			arg3 = make_cellref (1, -7);
		} else {
			arg3 = expr_df_denum;
		}

		dao_set_cell_expr
			(dao, 1, 11,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant
			  (value_new_float (1 - info->alpha / 2)),
			  make_cellref (0, -7),
			  arg3));
	}

	/* F critical two-tail (right) */
	dao_set_cell_expr
		(dao, 2, 11,
		 gnm_expr_new_funcall3
		 (fd_finv,
		  gnm_expr_new_constant
		  (value_new_float (info->alpha / 2)),
		  make_cellref (-1, -7),
		  make_cellref (0, -7)));

	value_release (val_1);
	value_release (val_2);

	gnm_func_dec_usage (fd_finv);

	dao_redraw_respan (dao);
	return FALSE;
}

gboolean
analysis_tool_ftest_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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
		return analysis_tool_generic_b_clean (specs);
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

static gint
calculate_xdim (GnmValue *input, group_by_t  group_by)
{
		GnmRange r;

		g_return_val_if_fail (input != NULL, 0);

		if (NULL == range_init_value (&r, input))
			return 0;

		if (group_by == GROUPED_BY_ROW)
			return range_height (&r);

		return range_width (&r);
}

static gint
calculate_n_obs (GnmValue *input, group_by_t  group_by)
{
		GnmRange r;

		g_return_val_if_fail (input != NULL, 0);

		if (NULL == range_init_value (&r, input))
			return 0;

		if (group_by == GROUPED_BY_ROW)
			return range_width (&r);

		return range_height (&r);
}


static gboolean
analysis_tool_regression_engine_run (data_analysis_output_t *dao,
				     analysis_tools_data_regression_t *info)
{
	gint xdim = calculate_xdim (info->base.range_1, info->group_by);
	gint i;

	GnmValue *val_1 = value_dup (info->base.range_1);
	GnmValue *val_2 = value_dup (info->base.range_2);
	GnmValue *val_1_cp = NULL;
	GnmValue *val_2_cp = NULL;

	GnmExpr const *expr_x;
	GnmExpr const *expr_y;
	GnmExpr const *expr_linest;
	GnmExpr const *expr_intercept;
	GnmExpr const *expr_ms;
	GnmExpr const *expr_sum;
	GnmExpr const *expr_tstat;
	GnmExpr const *expr_pvalue;
	GnmExpr const *expr_n;
	GnmExpr const *expr_df;
	GnmExpr const *expr_lower;
	GnmExpr const *expr_upper;
	GnmExpr const *expr_confidence;

	GnmFunc *fd_linest    = analysis_tool_get_function ("LINEST", dao);
	GnmFunc *fd_index     = analysis_tool_get_function ("INDEX", dao);
	GnmFunc *fd_fdist     = analysis_tool_get_function ("FDIST", dao);
	GnmFunc *fd_sum       = analysis_tool_get_function ("SUM", dao);
	GnmFunc *fd_sqrt      = analysis_tool_get_function ("SQRT", dao);
	GnmFunc *fd_tdist     = analysis_tool_get_function ("TDIST", dao);
	GnmFunc *fd_abs       = analysis_tool_get_function ("ABS", dao);
	GnmFunc *fd_tinv      = analysis_tool_get_function ("TINV", dao);
	GnmFunc *fd_transpose = analysis_tool_get_function ("TRANSPOSE", dao);
	GnmFunc *fd_concatenate = NULL;
	GnmFunc *fd_cell = NULL;
	GnmFunc *fd_offset = NULL;
	GnmFunc *fd_sumproduct = NULL;
	GnmFunc *fd_leverage = NULL;

	char const *str = ((info->group_by == GROUPED_BY_ROW) ? "row" : "col");
	char const *label = ((info->group_by == GROUPED_BY_ROW) ? _("Row")
			     : _("Column"));

	if (!info->base.labels) {
		fd_concatenate = analysis_tool_get_function ("CONCATENATE",
							     dao);
		fd_cell        = analysis_tool_get_function ("CELL", dao);
		fd_offset      = analysis_tool_get_function ("OFFSET", dao);
	}
	if (info->residual) {
		fd_sumproduct  = analysis_tool_get_function ("SUMPRODUCT", dao);
		fd_leverage = analysis_tool_get_function ("LEVERAGE", dao);
	}

	cb_adjust_areas (val_1, NULL);
	cb_adjust_areas (val_2, NULL);

	dao_set_italic (dao, 0, 0, 0, 16 + xdim);
        set_cell_text_col (dao, 0, 0, _("/SUMMARY OUTPUT"
					"/"
					"/Regression Statistics"
					"/Multiple R"
					"/R^2"
					"/Standard Error"
					"/Adjusted R^2"
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
	dao_set_merge (dao, 0, 0, 1, 0);
	dao_set_italic (dao, 2, 0, 3, 0);
	dao_set_cell (dao, 2, 0, _("Response Variable"));
	dao_set_merge (dao, 0, 2, 1, 2);

	if (info->base.labels) {

		dao_set_cell_expr (dao, 3, 0,
				   gnm_expr_new_funcall1 (fd_index, gnm_expr_new_constant (value_dup (val_2))));

		val_1_cp =  value_dup (val_1);
		val_2_cp =  value_dup (val_2);
		if (info->group_by == GROUPED_BY_ROW) {
			val_1->v_range.cell.a.col++;
			val_2->v_range.cell.a.col++;
			val_1_cp->v_range.cell.b.col = val_1_cp->v_range.cell.a.col;
			dao_set_array_expr (dao, 0, 17, 1, xdim, gnm_expr_new_constant
					    (value_dup (val_1_cp)));
		} else {
			val_1->v_range.cell.a.row++;
			val_2->v_range.cell.a.row++;
			val_1_cp->v_range.cell.b.row = val_1_cp->v_range.cell.a.row;
			dao_set_array_expr (dao, 0, 17, 1, xdim, gnm_expr_new_funcall1
					    (fd_transpose,
					     gnm_expr_new_constant (value_dup (val_1_cp))));
		}
	} else {
		dao_set_cell_expr (dao, 3, 0, gnm_expr_new_funcall3
				   (fd_concatenate, gnm_expr_new_constant (value_new_string (label)),
				    gnm_expr_new_constant (value_new_string (" ")),
				    gnm_expr_new_funcall2 (fd_cell,
							   gnm_expr_new_constant (value_new_string (str)),
							   gnm_expr_new_constant (value_dup (val_2)))));
	}

	dao_set_italic (dao, 1, 10, 5, 10);
        set_cell_text_row (dao, 1, 10, _("/df"
					 "/SS"
					 "/MS"
					 "/F"
					 "/Significance of F"));

	dao_set_italic (dao, 1, 15, 6, 15);
	set_cell_text_row (dao, 1, 15, _("/Coefficients"
					 "/Standard Error"
					 "/t-Statistics"
					 "/p-Value"));

	/* xgettext: this is an Excel-style number format.  Use "..." quotes and do not translate the 0% */
	dao_set_format  (dao, 5, 15, 5, 15, _("\"Lower\" 0%"));
	/* xgettext: this is an Excel-style number format.  Use "..." quotes and do not translate the 0% */
	dao_set_format  (dao, 6, 15, 6, 15, _("\"Upper\" 0%"));
	dao_set_align (dao, 5, 15, 5, 15, GNM_HALIGN_LEFT, GNM_VALIGN_TOP);
	dao_set_align (dao, 6, 15, 6, 15, GNM_HALIGN_RIGHT, GNM_VALIGN_TOP);

	dao_set_cell_float (dao, 5, 15, 1 - info->base.alpha);
	dao_set_cell_expr (dao, 6, 15, make_cellref (-1, 0));
	expr_confidence = dao_get_cellref (dao, 5, 15);

	dao_set_cell_comment (dao, 4, 15,
			      _("Probability of observing a t-statistic\n"
				"whose absolute value is at least as large\n"
				"as the absolute value of the actually\n"
				"observed t-statistic, assuming the null\n"
				"hypothesis is in fact true."));
	if (!info->intercept)
		dao_set_cell_comment (dao, 0, 4,
			      _("This value is not the square of R\n"
				"but the uncentered version of the\n"
				"coefficient of determination; that\n"
				"is, the proportion of the sum of\n"
				"squares explained by the model."));

	expr_x = gnm_expr_new_constant (value_dup (val_1));
	expr_y = gnm_expr_new_constant (value_dup (val_2));

	expr_intercept = gnm_expr_new_constant (value_new_bool (info->intercept));

	expr_linest = gnm_expr_new_funcall4 (fd_linest,
					     expr_y,
					     expr_x,
					     expr_intercept,
					     gnm_expr_new_constant (value_new_bool (TRUE)));


	/* Multiple R */
	if (info->intercept) {
		if (dao_cell_is_visible (dao, 1, 4))
			dao_set_cell_expr (dao, 1, 3, gnm_expr_new_funcall1 (fd_sqrt, make_cellref (0, 1)));
		else
			dao_set_cell_expr (dao, 1, 3,
					   gnm_expr_new_funcall1 (fd_sqrt, gnm_expr_new_funcall3
								  (fd_index,
								   gnm_expr_copy (expr_linest),
								   gnm_expr_new_constant (value_new_int (3)),
								   gnm_expr_new_constant (value_new_int (1)))));
	} else
			dao_set_cell_expr (dao, 1, 3,
					   gnm_expr_new_funcall1 (fd_sqrt, gnm_expr_new_funcall3
								  (fd_index,
								   gnm_expr_new_funcall4
								   (fd_linest,
								    gnm_expr_new_constant (value_dup (val_2)),
								    gnm_expr_new_constant (value_dup (val_1)),
								    gnm_expr_new_constant (value_new_bool (TRUE)),
								    gnm_expr_new_constant (value_new_bool (TRUE))),
								   gnm_expr_new_constant (value_new_int (3)),
								   gnm_expr_new_constant (value_new_int (1)))));


	/* R Square */
	dao_set_cell_array_expr (dao, 1, 4,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (3)),
							gnm_expr_new_constant (value_new_int (1))));

	/* Standard Error */
	dao_set_cell_array_expr (dao, 1, 5,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (3)),
							gnm_expr_new_constant (value_new_int (2))));

	/* Adjusted R Square */
	if (dao_cell_is_visible (dao, 1, 7))
		expr_n = make_cellref (0, 1);
	else
		expr_n = gnm_expr_new_funcall3 (fd_sum,
						gnm_expr_new_constant (value_new_int (xdim)),
						gnm_expr_new_funcall3 (fd_index,
								       gnm_expr_copy (expr_linest),
								       gnm_expr_new_constant (value_new_int (4)),
								       gnm_expr_new_constant (value_new_int (2))),
						gnm_expr_new_constant (value_new_int (1)));

	dao_set_cell_expr (dao, 1, 6, gnm_expr_new_binary
			   (gnm_expr_new_constant (value_new_int (1)),
			    GNM_EXPR_OP_SUB,
			    gnm_expr_new_binary
			    (gnm_expr_new_binary
			     (gnm_expr_new_binary
			      (gnm_expr_copy (expr_n),
			       GNM_EXPR_OP_SUB,
			       gnm_expr_new_constant (value_new_int (1))),
			      GNM_EXPR_OP_DIV,
			      gnm_expr_new_binary
			      (expr_n,
			       GNM_EXPR_OP_SUB,
			       gnm_expr_new_constant (value_new_int (xdim + (info->intercept?1:0))))),
			     GNM_EXPR_OP_MULT,
			     gnm_expr_new_binary
			     (gnm_expr_new_constant (value_new_int (1)),
			      GNM_EXPR_OP_SUB,
			      make_cellref (0, -2)))));

	/* Observations */

	if (dao_cell_is_visible (dao, 1, 13))
		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_funcall2 (fd_sum,
							  make_cellref (0, 6),
							  gnm_expr_new_constant (value_new_int (info->intercept?1:0))));
	else if (dao_cell_is_visible (dao, 1, 12))
		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_funcall3 (fd_sum,
							  make_cellref (0, 4),
							  make_cellref (0, 5),
							  gnm_expr_new_constant (value_new_int (info->intercept?1:0))));
	else
		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_funcall3 (fd_sum,
							  gnm_expr_new_constant (value_new_int (xdim)),
							  gnm_expr_new_funcall3 (fd_index,
										 gnm_expr_copy (expr_linest),
										 gnm_expr_new_constant (value_new_int (4)),
										 gnm_expr_new_constant (value_new_int (2))),
							  gnm_expr_new_constant (value_new_int (info->intercept?1:0))));



	/* Regression / df */

	dao_set_cell_int (dao, 1, 11, xdim);

	/* Residual / df */
	dao_set_cell_array_expr (dao, 1, 12,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (4)),
							gnm_expr_new_constant (value_new_int (2))));


	/* Total / df */
	expr_sum = gnm_expr_new_binary (make_cellref (0, -2),
				       GNM_EXPR_OP_ADD,
				       make_cellref (0, -1));
	dao_set_cell_expr (dao, 1, 13, gnm_expr_copy (expr_sum));

	/* Regression / SS */
	dao_set_cell_array_expr (dao, 2, 11,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (5)),
							gnm_expr_new_constant (value_new_int (1))));

	/* Residual / SS */
	dao_set_cell_array_expr (dao, 2, 12,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (5)),
							gnm_expr_new_constant (value_new_int (2))));


	/* Total / SS */
	dao_set_cell_expr (dao, 2, 13, expr_sum);


	/* Regression / MS */
	expr_ms = gnm_expr_new_binary (make_cellref (-1, 0),
				       GNM_EXPR_OP_DIV,
				       make_cellref (-2, 0));
	dao_set_cell_expr (dao, 3, 11, gnm_expr_copy (expr_ms));

	/* Residual / MS */
	dao_set_cell_expr (dao, 3, 12, expr_ms);


	/* F */
	dao_set_cell_array_expr (dao, 4, 11,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (4)),
							gnm_expr_new_constant (value_new_int (1))));

	/* Significance of F */

	if (dao_cell_is_visible (dao, 1, 12))
		dao_set_cell_expr (dao, 5, 11, gnm_expr_new_funcall3 (fd_fdist,
								      make_cellref (-1, 0),
								      make_cellref (-4, 0),
								      make_cellref (-4, 1)));
	else
		dao_set_cell_expr (dao, 5, 11, gnm_expr_new_funcall3 (fd_fdist,
								      make_cellref (-1, 0),
								      make_cellref (-4, 0),
								      gnm_expr_new_funcall3
								      (fd_index,
								       gnm_expr_copy (expr_linest),
								       gnm_expr_new_constant (value_new_int (4)),
								       gnm_expr_new_constant (value_new_int (2)))));


	/* Intercept */


	expr_tstat = gnm_expr_new_binary (make_cellref (-2, 0),
				       GNM_EXPR_OP_DIV,
				       make_cellref (-1, 0));
	expr_df = dao_get_cellref (dao, 1, 12);
	expr_pvalue = gnm_expr_new_funcall3 (fd_tdist, gnm_expr_new_funcall1 (fd_abs, make_cellref (-1, 0)),
					     gnm_expr_copy (expr_df),
					     gnm_expr_new_constant (value_new_int (2)));
	expr_lower = gnm_expr_new_binary (make_cellref (-4, 0),
				      GNM_EXPR_OP_SUB,
				      gnm_expr_new_binary (make_cellref (-3, 0),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_funcall2
							   (fd_tinv,
							    gnm_expr_new_binary
							    (gnm_expr_new_constant (value_new_float (1.0)),
							     GNM_EXPR_OP_SUB,
							     gnm_expr_copy (expr_confidence)),
							    gnm_expr_copy (expr_df))));
	expr_upper = gnm_expr_new_binary (make_cellref (-5, 0),
				      GNM_EXPR_OP_ADD,
				      gnm_expr_new_binary (make_cellref (-4, 0),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_funcall2
							   (fd_tinv,
							    gnm_expr_new_binary
							    (gnm_expr_new_constant (value_new_float (1.0)),
							     GNM_EXPR_OP_SUB,
							     expr_confidence),
							    expr_df)));


	/* Intercept */

	if (!info->intercept) {
		dao_set_cell_int (dao, 1, 16, 0);
		for (i = 2; i <= 6; i++)
			dao_set_cell_na (dao, i, 16);
	} else {
		dao_set_cell_array_expr (dao, 1, 16,
					 gnm_expr_new_funcall3
					 (fd_index,
					  gnm_expr_copy (expr_linest),
					  gnm_expr_new_constant (value_new_int (1)),
					  gnm_expr_new_constant (value_new_int (xdim+1))));
		dao_set_cell_array_expr (dao, 2, 16,
					 gnm_expr_new_funcall3
					 (fd_index,
					  gnm_expr_copy (expr_linest),
					  gnm_expr_new_constant (value_new_int (2)),
					  gnm_expr_new_constant (value_new_int (xdim+1))));
		dao_set_cell_expr (dao, 3, 16, gnm_expr_copy (expr_tstat));
		dao_set_cell_expr (dao, 4, 16, gnm_expr_copy (expr_pvalue));
		dao_set_cell_expr (dao, 5, 16, gnm_expr_copy (expr_lower));
		dao_set_cell_expr (dao, 6, 16, gnm_expr_copy (expr_upper));
	}

	/* Coefficients */

	dao->offset_row += 17;

	for (i = 0; i < xdim; i++) {
		if (!info->base.labels) {
			GnmExpr const *expr_offset;

			if (info->group_by == GROUPED_BY_ROW)
				expr_offset = gnm_expr_new_funcall3
					(fd_offset, gnm_expr_new_constant (value_dup (val_1)),
					 gnm_expr_new_constant (value_new_int (i)),
					 gnm_expr_new_constant (value_new_int (0)));
			else
				expr_offset = gnm_expr_new_funcall3
					(fd_offset, gnm_expr_new_constant (value_dup (val_1)),
					 gnm_expr_new_constant (value_new_int (0)),
					 gnm_expr_new_constant (value_new_int (i)));

			dao_set_cell_expr (dao, 0, i, gnm_expr_new_funcall3
					   (fd_concatenate, gnm_expr_new_constant (value_new_string (label)),
					    gnm_expr_new_constant (value_new_string (" ")),
					    gnm_expr_new_funcall2
					    (fd_cell,
					     gnm_expr_new_constant (value_new_string (str)),
					     expr_offset)));
		}

		dao_set_cell_array_expr (dao, 1, i,
					 gnm_expr_new_funcall3
					 (fd_index,
					  gnm_expr_copy (expr_linest),
					  gnm_expr_new_constant (value_new_int (1)),
					  gnm_expr_new_constant (value_new_int (xdim - i))));
		dao_set_cell_array_expr (dao, 2, i,
					 gnm_expr_new_funcall3
					 (fd_index,
					  gnm_expr_copy (expr_linest),
					  gnm_expr_new_constant (value_new_int (2)),
					  gnm_expr_new_constant (value_new_int (xdim - i))));
		dao_set_cell_expr (dao, 3, i, gnm_expr_copy (expr_tstat));
		dao_set_cell_expr (dao, 4, i, gnm_expr_copy (expr_pvalue));
		dao_set_cell_expr (dao, 5, i, gnm_expr_copy (expr_lower));
		dao_set_cell_expr (dao, 6, i, gnm_expr_copy (expr_upper));
	}


	gnm_expr_free (expr_linest);
	gnm_expr_free (expr_tstat);
	gnm_expr_free (expr_pvalue);
	gnm_expr_free (expr_lower);
	gnm_expr_free (expr_upper);

	value_release (val_1_cp);
	value_release (val_2_cp);

	if (info->residual) {
		gint n_obs = calculate_n_obs (val_1, info->group_by);
		GnmExpr const *expr_diff;
		GnmExpr const *expr_prediction;

		dao->offset_row += xdim + 1;
		dao_set_italic (dao, 0, 0, xdim + 7, 0);
		dao_set_cell (dao, 0, 0, _("Constant"));
		dao_set_array_expr (dao, 1, 0, xdim, 1,
				    gnm_expr_new_funcall1
				    (fd_transpose,
				     make_rangeref (-1, - xdim - 1, -1, -2)));
		set_cell_text_row (dao, xdim + 1, 0, _("/Prediction"
						       "/"
						       "/Residual"
						       "/Leverages"
						       "/Internally studentized"
						       "/Externally studentized"
						       "/p-Value"));
		dao_set_cell_expr (dao, xdim + 2, 0, make_cellref (1 - xdim, - 18 - xdim));
		if (info->group_by == GROUPED_BY_ROW) {
			dao_set_array_expr (dao, 1, 1, xdim, n_obs,
					    gnm_expr_new_funcall1
					    (fd_transpose,
					     gnm_expr_new_constant (val_1)));
			dao_set_array_expr (dao, xdim + 2, 1, 1, n_obs,
					    gnm_expr_new_funcall1
					    (fd_transpose,
					     gnm_expr_new_constant (val_2)));
		} else {
			dao_set_array_expr (dao, 1, 1, xdim, n_obs,
					    gnm_expr_new_constant (val_1));
			dao_set_array_expr (dao, xdim + 2, 1, 1, n_obs,
					    gnm_expr_new_constant (val_2));
		}

		expr_prediction =  gnm_expr_new_funcall2 (fd_sumproduct,
							  dao_get_rangeref (dao, 1, - 2 - xdim, 1, - 2),
							  gnm_expr_new_funcall1
							  (fd_transpose, make_rangeref
							   (-1 - xdim, 0, -1, 0)));
		expr_diff = gnm_expr_new_binary (make_cellref (-1, 0), GNM_EXPR_OP_SUB, make_cellref (-2, 0));

		for (i = 0; i < n_obs; i++) {
			dao_set_cell_expr (dao, xdim + 1, i + 1, gnm_expr_copy (expr_prediction));
			dao_set_cell_expr (dao, xdim + 3, i + 1, gnm_expr_copy (expr_diff));
			dao_set_cell_expr (dao, 0, i + 1, gnm_expr_new_constant (value_new_int (1)));
		}
		gnm_expr_free (expr_diff);
		gnm_expr_free (expr_prediction);

		if (dao_cell_is_visible (dao, xdim + 4, n_obs)) {
			GnmExpr const *expr_X = dao_get_rangeref (dao, info->intercept ? 0 : 1, 1, xdim, n_obs);
			GnmExpr const *expr_diagonal =
				gnm_expr_new_funcall1
				(fd_leverage, expr_X);
			GnmExpr const *expr_var =
				dao_get_cellref (dao, 3, - 6 - xdim);
			GnmExpr const *expr_int_stud =
				gnm_expr_new_binary
				(make_cellref (-2, 0),
				 GNM_EXPR_OP_DIV,
				 gnm_expr_new_funcall1
				 (fd_sqrt,
				  gnm_expr_new_binary
				  (expr_var,
				   GNM_EXPR_OP_MULT,
				   gnm_expr_new_binary
				   (gnm_expr_new_constant (value_new_int (1)),
				    GNM_EXPR_OP_SUB,
				    make_cellref (-1, 0)))));
			GnmExpr const *expr_ext_stud;
			GnmExpr const *expr_p_val_res;

			expr_var = gnm_expr_new_binary
				(gnm_expr_new_binary
				 (dao_get_cellref (dao, 2, - 6 - xdim),
				  GNM_EXPR_OP_SUB,
				  gnm_expr_new_binary
				  (make_cellref (-3, 0),
				   GNM_EXPR_OP_EXP,
				   gnm_expr_new_constant (value_new_int (2)))),
				 GNM_EXPR_OP_DIV,
				 gnm_expr_new_binary
				 (dao_get_cellref (dao, 1, - 6 - xdim),
				  GNM_EXPR_OP_SUB,
				  gnm_expr_new_constant (value_new_int (1))));
			expr_ext_stud = gnm_expr_new_binary
				(make_cellref (-3, 0),
				 GNM_EXPR_OP_DIV,
				 gnm_expr_new_funcall1
				 (fd_sqrt,
				  gnm_expr_new_binary
				  (expr_var,
				   GNM_EXPR_OP_MULT,
				   gnm_expr_new_binary
				   (gnm_expr_new_constant (value_new_int (1)),
				    GNM_EXPR_OP_SUB,
				    make_cellref (-2, 0)))));
			expr_p_val_res = gnm_expr_new_funcall3
				(fd_tdist,
				 gnm_expr_new_funcall1
				 (fd_abs,
				  make_cellref (-1, 0)),
				 gnm_expr_new_binary
				 (dao_get_cellref (dao, 1, - 6 - xdim),
				  GNM_EXPR_OP_SUB,
				  gnm_expr_new_constant (value_new_int (1))),
				 gnm_expr_new_constant (value_new_int (2)));

			dao_set_array_expr (dao, xdim + 4, 1, 1, n_obs, expr_diagonal);
			dao_set_format (dao, xdim + 5, 1, xdim + 6, n_obs, "0.0000");
			dao_set_percent (dao, xdim + 7, 1, xdim + 7, n_obs);
			for (i = 0; i < n_obs; i++){
				dao_set_cell_expr (dao, xdim + 5, i + 1, gnm_expr_copy (expr_int_stud));
				dao_set_cell_expr (dao, xdim + 6, i + 1, gnm_expr_copy (expr_ext_stud));
				dao_set_cell_expr (dao, xdim + 7, i + 1, gnm_expr_copy (expr_p_val_res));
			}
			gnm_expr_free (expr_int_stud);
			gnm_expr_free (expr_ext_stud);
			gnm_expr_free (expr_p_val_res);
		}
	} else {
		value_release (val_1);
		value_release (val_2);
	}

	gnm_func_dec_usage (fd_linest);
	gnm_func_dec_usage (fd_index);
	gnm_func_dec_usage (fd_fdist);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_sqrt);
	gnm_func_dec_usage (fd_tdist);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_tinv);
	gnm_func_dec_usage (fd_transpose);
	if (fd_concatenate != NULL)
		gnm_func_dec_usage (fd_concatenate);
	if (fd_cell != NULL)
		gnm_func_dec_usage (fd_cell);
	if (fd_offset != NULL)
		gnm_func_dec_usage (fd_offset);
	if (fd_sumproduct != NULL)
		gnm_func_dec_usage (fd_sumproduct);
	if (fd_leverage != NULL)
		gnm_func_dec_usage (fd_leverage);

	dao_redraw_respan (dao);

	return FALSE;
}

static gboolean
analysis_tool_regression_simple_engine_run (data_analysis_output_t *dao,
				     analysis_tools_data_regression_t *info)
{
	GnmFunc *fd_linest  = analysis_tool_get_function ("LINEST", dao);
	GnmFunc *fd_index   = analysis_tool_get_function ("INDEX", dao);
	GnmFunc *fd_fdist   = analysis_tool_get_function ("FDIST", dao);
	GnmFunc *fd_rows    = analysis_tool_get_function ("ROWS", dao);
	GnmFunc *fd_columns = analysis_tool_get_function ("COLUMNS", dao);

	GSList *inputdata;
	guint row;

	GnmValue *val_dep = value_dup (info->base.range_2);
	GnmExpr const *expr_intercept
		= gnm_expr_new_constant (value_new_bool (info->intercept));
	GnmExpr const *expr_observ;
	GnmExpr const *expr_val_dep;

	dao_set_italic (dao, 0, 0, 4, 0);
	dao_set_italic (dao, 0, 2, 5, 2);
        set_cell_text_row (dao, 0, 0, info->multiple_y ?
			   _("/SUMMARY OUTPUT"
			     "/"
			     "/Independent Variable"
			     "/"
			     "/Observations") :
			   _("/SUMMARY OUTPUT"
			     "/"
			     "/Response Variable"
			     "/"
			     "/Observations"));
        set_cell_text_row (dao, 0, 2, info->multiple_y ?
			   _("/Response Variable"
			     "/R^2"
			     "/Slope"
			     "/Intercept"
			     "/F"
			     "/Significance of F") :
			   _("/Independent Variable"
			     "/R^2"
			     "/Slope"
			     "/Intercept"
			     "/F"
			     "/Significance of F"));
	analysis_tools_write_a_label (val_dep, dao,
				      info->base.labels, info->group_by,
				      3, 0);

	expr_val_dep = gnm_expr_new_constant (val_dep);
	dao_set_cell_expr (dao, 5, 0, gnm_expr_new_binary (gnm_expr_new_funcall1 (fd_rows, gnm_expr_copy (expr_val_dep)),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_funcall1 (fd_columns, gnm_expr_copy (expr_val_dep))));
	expr_observ = dao_get_cellref (dao, 5, 0);

	for (row = 3, inputdata = info->indep_vars; inputdata != NULL;
	     inputdata = inputdata->next, row++) {
		GnmValue *val_indep = value_dup (inputdata->data);
		GnmExpr const *expr_linest;

		dao_set_italic (dao, 0, row, 0, row);
		analysis_tools_write_a_label (val_indep, dao,
					      info->base.labels, info->group_by,
					      0, row);
		expr_linest = info->multiple_y ?
			gnm_expr_new_funcall4 (fd_linest,
					       gnm_expr_new_constant (val_indep),
					       gnm_expr_copy (expr_val_dep),
					       gnm_expr_copy (expr_intercept),
					       gnm_expr_new_constant (value_new_bool (TRUE))) :
			gnm_expr_new_funcall4 (fd_linest,
					       gnm_expr_copy (expr_val_dep),
					       gnm_expr_new_constant (val_indep),
					       gnm_expr_copy (expr_intercept),
					       gnm_expr_new_constant (value_new_bool (TRUE)));
		dao_set_cell_array_expr (dao, 1, row,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (3)),
							gnm_expr_new_constant (value_new_int (1))));
		dao_set_cell_array_expr (dao, 4, row,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (4)),
							gnm_expr_new_constant (value_new_int (1))));
		dao_set_array_expr (dao, 2, row, 2, 1, expr_linest);

		dao_set_cell_expr (dao, 5, row, gnm_expr_new_funcall3
				   (fd_fdist,
				    make_cellref (-1, 0),
				    gnm_expr_new_constant (value_new_int (1)),
				    gnm_expr_new_binary (gnm_expr_copy (expr_observ),
							 GNM_EXPR_OP_SUB,
							 gnm_expr_new_constant (value_new_int (2)))));

	}

	gnm_expr_free (expr_intercept);
	gnm_expr_free (expr_observ);
	gnm_expr_free (expr_val_dep);

	gnm_func_dec_usage (fd_fdist);
	gnm_func_dec_usage (fd_linest);
	gnm_func_dec_usage (fd_index);
	gnm_func_dec_usage (fd_rows);
	gnm_func_dec_usage (fd_columns);

	dao_redraw_respan (dao);

	return FALSE;
}

gboolean
analysis_tool_regression_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			    analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_regression_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Regression (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
	{
		gint xdim = calculate_xdim (info->base.range_1, info->group_by);
		gint cols, rows;

		if (info->multiple_regression) {
			cols = 7;
			rows = 17 + xdim;
			info->indep_vars = NULL;
			if (info->residual) {
				gint residual_cols = xdim + 4;
				GnmValue *val = info->base.range_1;

				rows += 2 + calculate_n_obs (val, info->group_by);
				residual_cols += 4;
				if (cols < residual_cols)
					cols = residual_cols;
			}
		} else {
			info->indep_vars = g_slist_prepend (NULL, info->base.range_1);
			info->base.range_1 = NULL;
			prepare_input_range (&info->indep_vars, info->group_by);
			cols = 6;
			rows = 3 + xdim;
		}
		dao_adjust (dao, cols, rows);
		return FALSE;
	}
	case TOOL_ENGINE_CLEAN_UP:
		range_list_destroy (info->indep_vars);
		info->indep_vars = NULL;
		return analysis_tool_generic_b_clean (specs);

	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Regression"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Regression"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		if (info->multiple_regression)
			return analysis_tool_regression_engine_run (dao, specs);
		else
			return analysis_tool_regression_simple_engine_run (dao, specs);
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

static GnmExpr const *
analysis_tool_moving_average_funcall5 (GnmFunc *fd, GnmExpr const *ex, int y, int x, int dy, int dx)
{
	GnmExprList *list;
	list = gnm_expr_list_prepend (NULL, gnm_expr_new_constant (value_new_int (dx)));
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (dy)));
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (x)));
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (y)));
	list = gnm_expr_list_prepend (list, gnm_expr_copy (ex));

	return gnm_expr_new_funcall (fd, list);
}

static GnmExpr const *
analysis_tool_moving_average_weighted_av (GnmFunc *fd_sum, GnmFunc *fd_in, GnmExpr const *ex,
					  int y, int x, int dy, int dx, int *w)
{
	GnmExprList *list = NULL;

	while (*w != 0) {
		list = gnm_expr_list_prepend
			(list, gnm_expr_new_binary
			 (gnm_expr_new_constant (value_new_int (*w)),
			  GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall3 (fd_in, gnm_expr_copy (ex),
						 gnm_expr_new_constant (value_new_int (y)),
						 gnm_expr_new_constant (value_new_int (x)))));
		w++;
		x += dx;
		y += dy;
	}

	return gnm_expr_new_funcall (fd_sum, list);
}

static gboolean
analysis_tool_moving_average_engine_run (data_analysis_output_t *dao,
					 analysis_tools_data_moving_average_t *info)
{
	GnmFunc *fd_index = NULL;
	GnmFunc *fd_average;
	GnmFunc *fd_offset;
	GnmFunc *fd_sqrt = NULL;
	GnmFunc *fd_sumxmy2 = NULL;
	GnmFunc *fd_sum = NULL;
	GSList *l;
	gint col = 0;
	gint source;
	SheetObject *so = NULL;
	GogPlot	     *plot = NULL;

	if (info->base.labels || info->ma_type == moving_average_type_wma
	    || info->ma_type== moving_average_type_spencer_ma) {
		fd_index = gnm_func_lookup_or_add_placeholder ("INDEX");
		gnm_func_inc_usage (fd_index);
	}
	if (info->std_error_flag) {
		fd_sqrt = gnm_func_lookup_or_add_placeholder ("SQRT");
		gnm_func_inc_usage (fd_sqrt);
		fd_sumxmy2 = gnm_func_lookup_or_add_placeholder ("SUMXMY2");
		gnm_func_inc_usage (fd_sumxmy2);
	}
	if (moving_average_type_wma == info->ma_type || moving_average_type_spencer_ma == info->ma_type) {
		fd_sum = gnm_func_lookup_or_add_placeholder ("SUM");
		gnm_func_inc_usage (fd_sum);
	}
	fd_average = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_average);
	fd_offset = gnm_func_lookup_or_add_placeholder ("OFFSET");
	gnm_func_inc_usage (fd_offset);

	if (info->show_graph) {
		GogGraph     *graph;
		GogChart     *chart;

		graph = g_object_new (GOG_TYPE_GRAPH, NULL);
		chart = GOG_CHART (gog_object_add_by_name (GOG_OBJECT (graph), "Chart", NULL));
		plot = gog_plot_new_by_name ("GogLinePlot");
		gog_object_add_by_name (GOG_OBJECT (chart), "Plot", GOG_OBJECT (plot));
		so = sheet_object_graph_new (graph);
		g_object_unref (graph);
	}

	for (l = info->base.input, source = 1; l; l = l->next, col++, source++) {
		GnmValue *val = value_dup ((GnmValue *)l->data);
		GnmValue *val_c = NULL;
		GnmExpr const *expr_title = NULL;
		GnmExpr const *expr_input = NULL;
		char const *format = NULL;
		gint height;
		gint  x = 0;
		gint  y = 0;
		gint  *mover;
		guint *delta_mover;
		guint delta_x = 1;
		guint delta_y = 1;
		gint row, base;
		Sheet *sheet;
		GnmEvalPos ep;

		eval_pos_init_sheet (&ep, val->v_range.cell.a.sheet);

		if (info->base.labels) {
			val_c = value_dup (val);
			switch (info->base.group_by) {
			case GROUPED_BY_ROW:
				val->v_range.cell.a.col++;
				break;
			default:
				val->v_range.cell.a.row++;
				break;
			}
			expr_title = gnm_expr_new_funcall1 (fd_index,
							    gnm_expr_new_constant (val_c));

			dao_set_italic (dao, col, 0, col, 0);
			dao_set_cell_expr (dao, col, 0, expr_title);
		} else {
			switch (info->base.group_by) {
			case GROUPED_BY_ROW:
				format = _("Row %d");
				break;
			default:
				format = _("Column %d");
				break;
			}
			dao_set_cell_printf (dao, col, 0, format, source);
		}

		switch (info->base.group_by) {
		case GROUPED_BY_ROW:
			height = value_area_get_width (val, &ep);
			mover = &x;
			delta_mover = &delta_x;
			break;
		default:
			height = value_area_get_height (val, &ep);
			mover = &y;
			delta_mover = &delta_y;
			break;
		}

		sheet = val->v_range.cell.a.sheet;
		expr_input = gnm_expr_new_constant (val);

		if  (plot != NULL) {
			GogSeries    *series;

			series = gog_plot_new_series (plot);
			gog_series_set_dim (series, 1,
					    gnm_go_data_vector_new_expr (sheet,
									 gnm_expr_top_new (gnm_expr_copy (expr_input))),
					    NULL);

			series = gog_plot_new_series (plot);
			gog_series_set_dim (series, 1,
					    dao_go_data_vector (dao, col, 1, col, height),
					    NULL);
		}

		switch (info->ma_type) {
		case moving_average_type_central_sma:
		{
			GnmExpr const *expr_offset_last = NULL;
			GnmExpr const *expr_offset = NULL;
			*delta_mover = info->interval;
			(*mover) = 1 - info->interval + info->offset;
			for (row = 1; row <= height; row++, (*mover)++) {
				expr_offset_last = expr_offset;
				expr_offset = NULL;
				if ((*mover >= 0) && (*mover < height - info->interval + 1)) {
					expr_offset = gnm_expr_new_funcall1
						(fd_average, analysis_tool_moving_average_funcall5
						 (fd_offset,expr_input, y, x, delta_y, delta_x));

					if (expr_offset_last == NULL)
						dao_set_cell_na (dao, col, row);
					else
						dao_set_cell_expr (dao, col, row,
								   gnm_expr_new_funcall2 (fd_average, expr_offset_last,
											  gnm_expr_copy (expr_offset)));
				} else {
					if (expr_offset_last != NULL) {
						gnm_expr_free (expr_offset_last);
						expr_offset_last = NULL;
					}
					dao_set_cell_na (dao, col, row);
				}
			}
			base = info->interval - info->offset;
		}
		break;
		case moving_average_type_cma:
			for (row = 1; row <= height; row++) {
				GnmExpr const *expr_offset;

				*delta_mover = row;

				expr_offset = analysis_tool_moving_average_funcall5
					 (fd_offset, expr_input, y, x, delta_y, delta_x);

				dao_set_cell_expr (dao, col, row,
						   gnm_expr_new_funcall1 (fd_average, expr_offset));
			}
			base = 0;
			break;
		case moving_average_type_wma:
		{
			GnmExpr const *expr_divisor = gnm_expr_new_constant
				(value_new_int((info->interval * (info->interval + 1))/2));
			int *w = g_new (int, (info->interval + 1));
			int i;

			for (i = 0; i < info->interval; i++)
				w[i] = i+1;
			w[info->interval] = 0;

			delta_x = 0;
			delta_y= 0;
			(*delta_mover) = 1;
			(*mover) = 1 - info->interval;
			for (row = 1; row <= height; row++, (*mover)++) {
				if ((*mover >= 0) && (*mover < height - info->interval + 1)) {
					GnmExpr const *expr_sum;

					expr_sum = analysis_tool_moving_average_weighted_av
						(fd_sum, fd_index, expr_input, y+1, x+1, delta_y, delta_x, w);

					dao_set_cell_expr (dao, col, row,
							   gnm_expr_new_binary
							   (expr_sum,
							    GNM_EXPR_OP_DIV,
							    gnm_expr_copy (expr_divisor)));
				} else
					dao_set_cell_na (dao, col, row);
			}
			g_free (w);
			gnm_expr_free (expr_divisor);
			base =  info->interval - 1;
			delta_x = 1;
			delta_y= 1;
		}
		break;
		case moving_average_type_spencer_ma:
		{
			GnmExpr const *expr_divisor = gnm_expr_new_constant
				(value_new_int(-3-6-5+3+21+45+67+74+67+46+21+3-5-6-3));
			int w[] = {-3, -6, -5, 3, 21, 45, 67, 74, 67, 46, 21, 3, -5, -6, -3, 0};

			delta_x = 0;
			delta_y= 0;
			(*delta_mover) = 1;
			(*mover) = 1 - info->interval + info->offset;
			for (row = 1; row <= height; row++, (*mover)++) {
				if ((*mover >= 0) && (*mover < height - info->interval + 1)) {
					GnmExpr const *expr_sum;

					expr_sum = analysis_tool_moving_average_weighted_av
						(fd_sum, fd_index, expr_input, y+1, x+1, delta_y, delta_x, w);

					dao_set_cell_expr (dao, col, row,
							   gnm_expr_new_binary
							   (expr_sum,
							    GNM_EXPR_OP_DIV,
							    gnm_expr_copy (expr_divisor)));
				} else
					dao_set_cell_na (dao, col, row);
			}
			gnm_expr_free (expr_divisor);
			base =  info->interval - info->offset - 1;
			delta_x = 1;
			delta_y= 1;
		}
		break;
		default:
			(*delta_mover) = info->interval;
			(*mover) = 1 - info->interval + info->offset;
			for (row = 1; row <= height; row++, (*mover)++) {
				if ((*mover >= 0) && (*mover < height - info->interval + 1)) {
					GnmExpr const *expr_offset;

					expr_offset = analysis_tool_moving_average_funcall5
						(fd_offset, expr_input, y, x, delta_y, delta_x);
					dao_set_cell_expr (dao, col, row,
							   gnm_expr_new_funcall1 (fd_average, expr_offset));
				} else
					dao_set_cell_na (dao, col, row);
			}
			base =  info->interval - info->offset - 1;
			break;
		}

		if (info->std_error_flag) {
			col++;
			dao_set_italic (dao, col, 0, col, 0);
			dao_set_cell (dao, col, 0, _("Standard Error"));

			(*mover) = base;
			for (row = 1; row <= height; row++) {
				if (row > base && row <= height - info->offset && (row - base - info->df) > 0) {
					GnmExpr const *expr_offset;

					if (info->base.group_by == GROUPED_BY_ROW)
						delta_x = row - base;
					else
						delta_y = row - base;

					expr_offset = analysis_tool_moving_average_funcall5
						(fd_offset, expr_input, y, x, delta_y, delta_x);
					dao_set_cell_expr (dao, col, row,
							   gnm_expr_new_funcall1
							   (fd_sqrt,
							    gnm_expr_new_binary
							    (gnm_expr_new_funcall2
							     (fd_sumxmy2,
							      expr_offset,
							      make_rangeref (-1, - row + base + 1, -1, 0)),
							     GNM_EXPR_OP_DIV,
							     gnm_expr_new_constant (value_new_int
										    (row - base - info->df)))));
				} else
					dao_set_cell_na (dao, col, row);
			}
		}

		gnm_expr_free (expr_input);
	}

	if (so != NULL)
		dao_set_sheet_object (dao, 0, 1, so);

	if (fd_index != NULL)
		gnm_func_dec_usage (fd_index);
	if (fd_sqrt != NULL)
		gnm_func_dec_usage (fd_sqrt);
	if (fd_sumxmy2 != NULL)
		gnm_func_dec_usage (fd_sumxmy2);
	if (fd_sum != NULL)
		gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_average);
	gnm_func_dec_usage (fd_offset);

	dao_redraw_respan (dao);

	return FALSE;
}


gboolean
analysis_tool_moving_average_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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
		return analysis_tool_generic_clean (specs);
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


/************* Rank and Percentile Tool ************************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

static gboolean
analysis_tool_ranking_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_ranking_t *info)
{
	GSList *data = info->base.input;
	int col = 0;

	GnmFunc *fd_large;
	GnmFunc *fd_row;
	GnmFunc *fd_rank;
	GnmFunc *fd_match;
	GnmFunc *fd_percentrank;

	fd_large = gnm_func_lookup_or_add_placeholder ("LARGE");
	gnm_func_inc_usage (fd_large);
	fd_row = gnm_func_lookup_or_add_placeholder ("ROW");
	gnm_func_inc_usage (fd_row);
	fd_rank = gnm_func_lookup_or_add_placeholder ("RANK");
	gnm_func_inc_usage (fd_rank);
	fd_match = gnm_func_lookup_or_add_placeholder ("MATCH");
	gnm_func_inc_usage (fd_match);
	fd_percentrank = gnm_func_lookup_or_add_placeholder ("PERCENTRANK");
	gnm_func_inc_usage (fd_percentrank);

	dao_set_merge (dao, 0, 0, 1, 0);
	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Ranks & Percentiles"));

	for (; data; data = data->next, col++) {
		GnmValue *val_org = value_dup (data->data);
		GnmExpr const *expr_large;
		GnmExpr const *expr_rank;
		GnmExpr const *expr_position;
		GnmExpr const *expr_percentile;
		int rows, i;

		dao_set_italic (dao, 0, 1, 3, 1);
		dao_set_cell (dao, 0, 1, _("Point"));
		dao_set_cell (dao, 2, 1, _("Rank"));
		dao_set_cell (dao, 3, 1, _("Percentile Rank"));
		analysis_tools_write_label (val_org, dao, &info->base, 1, 1, col + 1);

		rows = (val_org->v_range.cell.b.row - val_org->v_range.cell.a.row + 1) *
			(val_org->v_range.cell.b.col - val_org->v_range.cell.a.col + 1);

		expr_large = gnm_expr_new_funcall2
			(fd_large, gnm_expr_new_constant (value_dup (val_org)),
			 gnm_expr_new_binary (gnm_expr_new_binary
					      (gnm_expr_new_funcall (fd_row, NULL),
					       GNM_EXPR_OP_SUB,
					       gnm_expr_new_funcall1
					       (fd_row, dao_get_cellref (dao, 1, 2))),
					      GNM_EXPR_OP_ADD,
					      gnm_expr_new_constant (value_new_int (1))));
		dao_set_array_expr (dao, 1, 2, 1, rows, gnm_expr_copy (expr_large));

		/* If there are ties the following will only give us the first occurrence... */
		expr_position = gnm_expr_new_funcall3 (fd_match, expr_large,
						       gnm_expr_new_constant (value_dup (val_org)),
						       gnm_expr_new_constant (value_new_int (0)));

		dao_set_array_expr (dao, 0, 2, 1, rows, expr_position);

		expr_rank = gnm_expr_new_funcall2 (fd_rank,
						   make_cellref (-1,0),
						   gnm_expr_new_constant (value_dup (val_org)));
		if (info->av_ties) {
			GnmExpr const *expr_rank_lower;
			GnmExpr const *expr_rows_p_one;
			GnmExpr const *expr_rows;
			GnmFunc *fd_count;
			fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
			gnm_func_inc_usage (fd_count);

			expr_rows = gnm_expr_new_funcall1
				(fd_count, gnm_expr_new_constant (value_dup (val_org)));
			expr_rows_p_one = gnm_expr_new_binary
				(expr_rows,
				 GNM_EXPR_OP_ADD,
				 gnm_expr_new_constant (value_new_int (1)));
			expr_rank_lower = gnm_expr_new_funcall3
				(fd_rank,
				 make_cellref (-1,0),
				 gnm_expr_new_constant (value_dup (val_org)),
				 gnm_expr_new_constant (value_new_int (1)));
			expr_rank = gnm_expr_new_binary
				(gnm_expr_new_binary
				 (gnm_expr_new_binary (expr_rank, GNM_EXPR_OP_SUB, expr_rank_lower),
				  GNM_EXPR_OP_ADD, expr_rows_p_one),
				 GNM_EXPR_OP_DIV,
				 gnm_expr_new_constant (value_new_int (2)));

			gnm_func_dec_usage (fd_count);
		}
		expr_percentile = gnm_expr_new_funcall3 (fd_percentrank,
							 gnm_expr_new_constant (value_dup (val_org)),
							 make_cellref (-2,0),
							 gnm_expr_new_constant (value_new_int (10)));

		dao_set_percent (dao, 3, 2, 3, 1 + rows);
		for (i = 2; i < rows + 2; i++) {
			dao_set_cell_expr ( dao, 2, i, gnm_expr_copy (expr_rank));
			dao_set_cell_expr ( dao, 3, i, gnm_expr_copy (expr_percentile));
		}


		dao->offset_col += 4;
		value_release (val_org);
		gnm_expr_free (expr_rank);
		gnm_expr_free (expr_percentile);
	}

	gnm_func_dec_usage (fd_large);
	gnm_func_dec_usage (fd_row);
	gnm_func_dec_usage (fd_rank);
	gnm_func_dec_usage (fd_match);
	gnm_func_dec_usage (fd_percentrank);

	dao_redraw_respan (dao);

	return FALSE;
}

gboolean
analysis_tool_ranking_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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
			    2 + analysis_tool_calc_length (specs));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (specs);
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

	dao_set_italic (dao, 0, 0, 0, 2);
	dao_set_cell (dao, 0, 0, _("Anova: Single Factor"));
	dao_set_cell (dao, 0, 2, _("SUMMARY"));

	dao_set_italic (dao, 0, 3, 4, 3);
	set_cell_text_row (dao, 0, 3, _("/Groups"
					"/Count"
					"/Sum"
					"/Average"
					"/Variance"));

	fd_mean = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_mean);
	fd_var = gnm_func_lookup_or_add_placeholder ("VAR");
	gnm_func_inc_usage (fd_var);
	fd_sum = gnm_func_lookup_or_add_placeholder ("SUM");
	gnm_func_inc_usage (fd_sum);
	fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
	gnm_func_inc_usage (fd_count);
	fd_devsq = gnm_func_lookup_or_add_placeholder ("DEVSQ");
	gnm_func_inc_usage (fd_devsq);

	dao->offset_row += 4;
	if (dao->rows <= dao->offset_row)
		goto finish_anova_single_factor_tool;

	/* SUMMARY */

	for (index = 0; inputdata != NULL;
	     inputdata = inputdata->next, index++) {
		GnmValue *val_org = value_dup (inputdata->data);

		/* Label */
		dao_set_italic (dao, 0, index, 0, index);
		analysis_tools_write_label (val_org, dao, &info->base,
					    0, index, index + 1);

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


	dao_set_italic (dao, 0, 0, 0, 4);
	set_cell_text_col (dao, 0, 0, _("/ANOVA"
					"/Source of Variation"
					"/Between Groups"
					"/Within Groups"
					"/Total"));
	dao_set_italic (dao, 1, 1, 6, 1);
	set_cell_text_row (dao, 1, 1, _("/SS"
					"/df"
					"/MS"
					"/F"
					"/P-value"
					"/F critical"));

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

			analysis_tools_remove_label (val_org,
						     info->base.labels,
						     info->base.group_by);
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
				expr_ss_between = gnm_expr_new_binary
					(make_cellref (0, 2),
					 GNM_EXPR_OP_SUB,
					 make_cellref (0, 1));

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
			GnmExpr const *expr_ms =
				gnm_expr_new_binary
				(make_cellref (-2, 0),
				 GNM_EXPR_OP_DIV,
				 make_cellref (-1, 0));
			dao_set_cell_expr (dao, 3, 2, gnm_expr_copy (expr_ms));
			dao_set_cell_expr (dao, 3, 3, expr_ms);
		}
		{
			/* Observed F */
			GnmExpr const *expr_denom;
			GnmExpr const *expr_f;

			if (dao_cell_is_visible (dao, 3, 3)) {
				expr_denom = make_cellref (-1, 1);
				gnm_expr_free (expr_ss_within);
			} else {
				expr_denom = gnm_expr_new_binary
					(expr_ss_within,
					 GNM_EXPR_OP_DIV,
					 gnm_expr_copy (expr_wdof));
			}

			expr_f = gnm_expr_new_binary
				(make_cellref (-1, 0),
				 GNM_EXPR_OP_DIV,
				 expr_denom);
			dao_set_cell_expr(dao, 4, 2, expr_f);
		}
		{
			/* P value */
			GnmFunc *fd_fdist;
			const GnmExpr *arg1;
			const GnmExpr *arg2;
			const GnmExpr *arg3;

			arg1 = make_cellref (-1, 0);
			arg2 = make_cellref (-3, 0);

			if (dao_cell_is_visible (dao, 2, 3)) {
				arg3 = make_cellref (-3, 1);
			} else {
				arg3 = gnm_expr_copy (expr_wdof);
			}

			fd_fdist = gnm_func_lookup_or_add_placeholder ("FDIST");
			gnm_func_inc_usage (fd_fdist);

			dao_set_cell_expr
				(dao, 5, 2,
				 gnm_expr_new_funcall3
				 (fd_fdist,
				  arg1, arg2, arg3));
			if (fd_fdist)
				gnm_func_dec_usage (fd_fdist);
		}
		{
			/* Critical F*/
			GnmFunc *fd_finv;
			const GnmExpr *arg3;

			if (dao_cell_is_visible (dao, 2, 3)) {
				arg3 = make_cellref (-4, 1);
				gnm_expr_free (expr_wdof);
			} else
				arg3 = expr_wdof;

			fd_finv = gnm_func_lookup_or_add_placeholder ("FINV");
			gnm_func_inc_usage (fd_finv);

			dao_set_cell_expr
				(dao, 6, 2,
				 gnm_expr_new_funcall3
				 (fd_finv,
				  gnm_expr_new_constant
				  (value_new_float (info->alpha)),
				  make_cellref (-4, 0),
				  arg3));
			gnm_func_dec_usage (fd_finv);
		}
	}

finish_anova_single_factor_tool:

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_devsq);

	dao->offset_row = 0;
	dao->offset_col = 0;

	dao_redraw_respan (dao);
        return FALSE;
}



gboolean
analysis_tool_anova_single_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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
		return analysis_tool_generic_clean (specs);
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


/************* Fourier Analysis Tool **************************************
 *
 * This tool performes a fast fourier transform calculating the fourier
 * transform as defined in Weaver: Theory of dis and cont Fouriere Analysis
 *
 *
 **/


static gboolean
analysis_tool_fourier_engine_run (data_analysis_output_t *dao,
				  analysis_tools_data_fourier_t *info)
{
	GSList *data = info->base.input;
	int col = 0;

	GnmFunc *fd_fourier;

	fd_fourier = gnm_func_lookup_or_add_placeholder ("FOURIER");
	gnm_func_inc_usage (fd_fourier);

	dao_set_merge (dao, 0, 0, 1, 0);
	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, info->inverse ? _("Inverse Fourier Transform")
		      : _("Fourier Transform"));

	for (; data; data = data->next, col++) {
		GnmValue *val_org = value_dup (data->data);
		GnmExpr const *expr_fourier;
		int rows, n;

		dao_set_italic (dao, 0, 1, 1, 2);
		set_cell_text_row (dao, 0, 2, _("/Real"
						"/Imaginary"));
		dao_set_merge (dao, 0, 1, 1, 1);
		analysis_tools_write_label (val_org, dao, &info->base, 0, 1, col + 1);

		n = (val_org->v_range.cell.b.row - val_org->v_range.cell.a.row + 1) *
			(val_org->v_range.cell.b.col - val_org->v_range.cell.a.col + 1);
		rows = 1;
		while (rows < n)
			rows *= 2;

		expr_fourier = gnm_expr_new_funcall3
			(fd_fourier,
			 gnm_expr_new_constant (val_org),
			 gnm_expr_new_constant (value_new_bool (info->inverse)),
			 gnm_expr_new_constant (value_new_bool (TRUE)));

		dao_set_array_expr (dao, 0, 3, 2, rows, expr_fourier);

		dao->offset_col += 2;
	}

	gnm_func_dec_usage (fd_fourier);

	dao_redraw_respan (dao);

	return FALSE;
}

static int
analysis_tool_fourier_calc_length (analysis_tools_data_fourier_t *info)
{
	int m = 1, n = analysis_tool_calc_length (&info->base);

	while (m < n)
		m *= 2;
	return m;
}


gboolean
analysis_tool_fourier_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
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
			    3 + analysis_tool_fourier_calc_length (specs));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (specs);
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
