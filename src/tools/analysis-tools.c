/*
 * analysis-tools.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "analysis-tools.h"

#include "mathfunc.h"
#include "complex.h"
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
#include "format.h"
#include "sheet-object-cell-comment.h"
#include "workbook-control.h"
#include "command-context.h"

#include <string.h>
#include <math.h>

#undef DEBUG_ANALYSIS_TOOLS
#ifdef DEBUG_ANALYSIS_TOOLS
#include <stdio.h>
#endif


/*************************************************************************/
/*
 *  data_set_t: a data set format (optionally) keeping track of missing
 *  observations.
 *
 */

typedef struct {
        GArray  *data;
	char *label;
	GSList *missing;
	gboolean complete;
	gboolean read_label;
} data_set_t;

typedef struct {
	char *format;
	GPtrArray *data_lists;
	gboolean read_label;
	gboolean ignore_non_num;
	guint length;
	Sheet *sheet;
} data_list_specs_t;

/*
 *  cb_store_data:
 *  @cell:
 *  @data: pointer to a data_set_t
 */
static Value *
cb_store_data (__attribute__((unused)) Sheet *sheet,
	       __attribute__((unused)) int col,
	       __attribute__((unused)) int row, Cell *cell, void *user_data)
{
	data_set_t *data_set = (data_set_t *)user_data;
	gnum_float the_data;

	if (data_set->read_label) {
		if (cell != NULL) {
			data_set->label = cell_get_rendered_text (cell);
			if (strlen (data_set->label) == 0) {
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
 *  @range: Value *       the data location, usually a single column or row
 *  @ignore_non_num: gboolean   whether simply to ignore non-numerical values
 *  @read_label: gboolean       whether the first entry contains a label
 *  @format: char*              format string for default label
 *  @i: guint                    index for default label
 */
static data_set_t *
new_data_set (Value *range, gboolean ignore_non_num, gboolean read_label,
	      char *format, gint i, Sheet* sheet)
{
	Value *result;
	EvalPos  *pos = g_new (EvalPos, 1);
	data_set_t * the_set = g_new (data_set_t, 1);
	CellPos cellpos = {0, 0};

	pos = eval_pos_init (pos, sheet, &cellpos);
	the_set->data = g_array_new (FALSE, FALSE, sizeof (gnum_float)),
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
destroy_data_set ( data_set_t *data_set)
{

	if (data_set->data != NULL)
		g_array_free (data_set->data, TRUE);
	if (data_set->missing != NULL)
		g_slist_free (data_set->missing);
	if (data_set->label != NULL)
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
	Value * the_range = (Value *) data;
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
static GPtrArray *
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
static void
destroy_data_set_list (GPtrArray * the_list)
{
	guint i;
	gpointer data;

	for (i = 0; i < the_list->len; i++) {
		data = g_ptr_array_index (the_list, i);
		destroy_data_set ((data_set_t *) data);
	}
	g_ptr_array_free (the_list, TRUE);
}

/*
 *  cb_insert_diff_elements :
 *  @data:
 *  @user_data: really a GSList **
 *
 */
static void
cb_insert_diff_elements (gpointer data, gpointer user_data)
{
	GSList **the_list = (GSList **) (user_data);

	if (g_slist_find (*the_list, data) == NULL) {
		*the_list = g_slist_prepend (*the_list, data);
	}
	return;
}

/*
 *  cb_int_descending:
 *  @a:
 *  @b:
 *
 */
static gint
cb_int_descending (gconstpointer a, gconstpointer b)
{
	guint a_int = GPOINTER_TO_UINT (a);
	guint b_int = GPOINTER_TO_UINT (b);

	if (b_int > a_int) return 1;
	if (b_int < a_int) return -1;
	return 0;
}

/*
 *  union_of_int_sets:
 *  @list_1:
 *  @list_2:
 *
 */
static GSList*
union_of_int_sets (GSList * list_1, GSList * list_2)
{
	GSList *list_res = NULL;

	if ((list_1 == NULL) || (g_slist_length (list_1) == 0))
		return ((list_2 == NULL) ? NULL :
			g_slist_copy (list_2));
	if ((list_2 == NULL) || (g_slist_length (list_2) == 0))
		return g_slist_copy (list_1);

	list_res = g_slist_copy (list_1);
	g_slist_foreach (list_2, cb_insert_diff_elements, &list_res);

	return list_res;
}

/*
 *  cb_remove_missing_el :
 *  @data:
 *  @user_data: really a GArray **
 *
 */
static void
cb_remove_missing_el (gpointer data, gpointer user_data)
{
	GArray **the_data = (GArray **) (user_data);
	guint the_item = GPOINTER_TO_UINT (data);

	*the_data = g_array_remove_index (*the_data, the_item);
	return;
}



/*
 *  strip_missing:
 *  @data:
 *  @missing:
 *
 */
static GArray*
strip_missing (GArray * data, GSList * missing)
{
	GArray *new_data;
	GSList * sorted_missing;

	if ((missing == NULL) || (g_slist_length (missing) == 0))
		return data;

	sorted_missing = g_slist_sort (g_slist_copy (missing), cb_int_descending);;
	new_data = g_array_new (FALSE, FALSE, sizeof (gnum_float));
	g_array_set_size (new_data, data->len);
	g_memmove (new_data->data, data->data, sizeof (gnum_float) * data->len);
	g_slist_foreach (sorted_missing, cb_remove_missing_el, &new_data);
	g_slist_free (sorted_missing);

	return new_data;
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
	Value *range = (Value *)data;
	Value *col_value;
	GSList **list_of_units = (GSList **) user_data;
	gint col;

	if (range == NULL) {
		return;
	}
	if ((range->type != VALUE_CELLRANGE) ||
	    (range->v_range.cell.a.sheet != range->v_range.cell.b.sheet)) {
		value_release (range);
		return;
	}
	if (range->v_range.cell.a.col == range->v_range.cell.b.col) {
		*list_of_units = g_slist_prepend (*list_of_units, range);
		return;
	}

	for (col = range->v_range.cell.a.col; col <= range->v_range.cell.b.col; col++) {
		col_value = value_duplicate (range);
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
	Value *range = (Value *)data;
	Value *row_value;
	GSList **list_of_units = (GSList **) user_data;
	gint row;

	if (range == NULL) {
		return;
	}
	if ((range->type != VALUE_CELLRANGE) ||
	    (range->v_range.cell.a.sheet != range->v_range.cell.b.sheet)) {
		value_release (range);
		return;
	}
	if (range->v_range.cell.a.row == range->v_range.cell.b.row) {
		*list_of_units = g_slist_prepend (*list_of_units, range);
		return;
	}

	for (row = range->v_range.cell.a.row; row <= range->v_range.cell.b.row; row++) {
		row_value = value_duplicate (range);
		row_value->v_range.cell.a.row = row;
		row_value->v_range.cell.b.row = row;
		*list_of_units = g_slist_prepend (*list_of_units, row_value);
	}
	value_release (range);
	return;
}


/*
 *  prepare_input_range:
 *  @input_range:
 *  @group_by:
 *
 */
static void
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
	Value *range = (Value *)data;
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
 *  check_input_range_list_homogeneity:
 *  @input_range:
 *
 *  Check that all columns have the same size
 *
 */
static gboolean
check_input_range_list_homogeneity (GSList *input_range)
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
		if (((data_set_t *)g_ptr_array_index (data, i))->missing != NULL)
			return TRUE;
	}

	return FALSE;
}

/***** Some general routines ***********************************************/

static gint
float_compare (const gnum_float *a, const gnum_float *b)
{
        if (*a < *b)
                return -1;
        else if (*a == *b)
                return 0;
        else
                return 1;
}

static gnum_float *
range_sort (const gnum_float *xs, int n)
{
	if (n <= 0)
		return NULL;
	else {
		gnum_float *ys = g_new (gnum_float, n);
		memcpy (ys, xs, n * sizeof (gnum_float));
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

/* Callbacks for write */
static Value *
WriteData_ForeachCellCB (Sheet *sheet, int col, int row,
			      Cell *cell, GArray* data)
{
	gnum_float  x;
	if (data->len == 0)
		return VALUE_TERMINATE;
	if (cell == NULL)
		cell = sheet_cell_new (sheet, col, row);
	x = g_array_index (data, gnum_float, 0);
	g_array_remove_index (data, 0);
	sheet_cell_set_value (cell, value_new_float (x));
	return NULL;
}

static void
write_data (__attribute__((unused)) WorkbookControl *wbc,
	    data_analysis_output_t *dao, GArray *data)
{
	gint st_row = dao->start_row + dao->offset_row;
	gint end_row = dao->start_row + dao->rows - 1;
	gint st_col = dao->start_col + dao->offset_col;
	gint end_col = dao->start_col + dao->offset_col;

	if (dao->cols <= dao->offset_col)
		return;

	sheet_foreach_cell_in_range (dao->sheet, CELL_ITER_ALL,
		st_col, st_row, end_col, end_row,
		(CellIterFunc)&WriteData_ForeachCellCB, data);
}

static gboolean
analysis_tool_generic_clean (__attribute__((unused)) data_analysis_output_t *dao,
			     gpointer specs)
{
	analysis_tools_data_generic_t *info = specs;

	range_list_destroy (info->input);
	info->input = NULL;
	return FALSE;
}

static gboolean
analysis_tool_ftest_clean (__attribute__((unused)) data_analysis_output_t *dao,
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
		Value    *current = dataset->data;
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
analysis_tool_correlation_engine_run (data_analysis_output_t *dao, 
				      analysis_tools_data_generic_t *info)
{
	GPtrArray *data = NULL;
	guint col, row;
	int error;
	gnum_float x;
	data_set_t *col_data, *row_data;
	GArray *clean_col_data, *clean_row_data;
	GSList *missing;

	data = new_data_set_list (info->input, info->group_by,
				  FALSE, info->labels, dao->sheet);

	dao_set_cell_printf (dao, 0, 0,  _("Correlations"));
	dao_set_italic (dao, 0, 0, 0, 0);

	for (row = 0; row < data->len; row++) {
		row_data = g_ptr_array_index (data, row);
		dao_set_cell_printf (dao, 0, row+1, row_data->label);
		dao_set_italic (dao, 0, row+1, 0,  row+1);
		dao_set_cell_printf (dao, row+1, 0, row_data->label);
		dao_set_italic (dao, row+1, 0,  row+1, 0);
		for (col = 0; col < data->len; col++) {
		        if (row == col) {
			        dao_set_cell_int (dao, col + 1, row + 1, 1);
				break;
			} else {
				if (row < col) {
					dao_set_cell (dao, col + 1, row + 1, NULL);
				} else {
					col_data = g_ptr_array_index (data, col);
					missing = union_of_int_sets (col_data->missing,
								     row_data->missing);
					clean_col_data = strip_missing (col_data->data,
									missing);
					clean_row_data = strip_missing (row_data->data,
									missing);
					g_slist_free (missing);
					error =  range_correl_pop
						((gnum_float *)(clean_col_data->data),
						 (gnum_float *)(clean_row_data->data),
						 clean_col_data->len, &x);
					if (clean_col_data != col_data->data)
						g_array_free (clean_col_data, TRUE);
					if (clean_row_data != row_data->data)
						g_array_free (clean_row_data, TRUE);
					if (error)
						dao_set_cell_na (dao, col + 1, row + 1);
					else
						dao_set_cell_float (dao, col + 1, row + 1, x);
				}

			}
		}
	}

	destroy_data_set_list (data);

	return 0;
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
		if (!check_input_range_list_homogeneity (info->input)) {
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
	GPtrArray *data = NULL;
	guint col, row;
	int error;
	gnum_float x;
	data_set_t *col_data, *row_data;
	GArray *clean_col_data, *clean_row_data;
	GSList *missing;

	data = new_data_set_list (info->input, info->group_by,
				  FALSE, info->labels, dao->sheet);

	dao_set_cell_printf (dao, 0, 0,  _("Covariances"));
	dao_set_italic (dao, 0, 0, 0, 0);

	for (row = 0; row < data->len; row++) {
		row_data = g_ptr_array_index (data, row);
		dao_set_cell_printf (dao, 0, row+1, row_data->label);
		dao_set_italic (dao, 0, row+1, 0,  row+1);
		dao_set_cell_printf (dao, row+1, 0, row_data->label);
		dao_set_italic (dao, row+1, 0,  row+1, 0);
		for (col = 0; col < data->len; col++) {
		        if (row == col) {
			        dao_set_cell_int (dao, col + 1, row + 1, 1);
				break;
			} else {
				if (row < col) {
					dao_set_cell (dao, col + 1, row + 1, NULL);
				} else {
					col_data = g_ptr_array_index (data, col);
					missing = union_of_int_sets (col_data->missing,
								     row_data->missing);
					clean_col_data = strip_missing (col_data->data,
									missing);
					clean_row_data = strip_missing (row_data->data,
									missing);
					g_slist_free (missing);
					error =  range_covar
						((gnum_float *)(clean_col_data->data),
						 (gnum_float *)(clean_row_data->data),
						 clean_col_data->len, &x);
					if (clean_col_data != col_data->data)
						g_array_free (clean_col_data, TRUE);
					if (clean_row_data != row_data->data)
						g_array_free (clean_row_data, TRUE);
					if (error)
						dao_set_cell_na (dao, col + 1, row + 1);
					else
						dao_set_cell_float (dao, col + 1, row + 1, x);
				}

			}
		}
	}

	destroy_data_set_list (data);

	return 0;
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
		if (!check_input_range_list_homogeneity (info->input)) {
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
	gnum_float mean;
	gint       error_mean;
	gnum_float var;
	gint       error_var;
	gint      len;
} desc_stats_t;

static void
summary_statistics (GPtrArray *data,
		    data_analysis_output_t *dao, GArray *basic_stats)
{
	guint     col;

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

	for (col = 0; col < data->len; col++) {
		data_set_t *the_col = (g_ptr_array_index (data, col));
		const gnum_float *the_data = (gnum_float *)the_col->data->data;
		int the_col_len = the_col->data->len;
		gnum_float x, xmin, xmax;
		int error, error2;
	        desc_stats_t info = g_array_index (basic_stats, desc_stats_t, col);

		dao_set_cell_printf (dao, col + 1, 0, the_col->label);
		dao_set_italic (dao, col+1, 0, col+1, 0);

	        /* Mean */
		dao_set_cell_float_na (dao, col + 1, 1, info.mean, info.error_mean == 0);

		/* Standard Error */
		dao_set_cell_float_na (dao, col + 1, 2, sqrtgnum (info.var / info.len),
				   info.error_var == 0);

		/* Standard Deviation */
		dao_set_cell_float_na (dao, col + 1, 5, sqrtgnum (info.var), info.error_var == 0);

		/* Sample Variance */
		dao_set_cell_float_na (dao, col + 1, 6, info.var, info.error_var == 0);

		/* Median */
		error = range_median_inter (the_data, the_col_len, &x);
		dao_set_cell_float_na (dao, col + 1, 3, x, error == 0);

		/* Mode */
		error = range_mode (the_data, the_col_len, &x);
		dao_set_cell_float_na (dao, col + 1, 4, x, error == 0);

		/* Kurtosis */
		error = range_kurtosis_m3_est (the_data, the_col_len, &x);
		dao_set_cell_float_na (dao, col + 1, 7, x, error == 0);

		/* Skewness */
		error = range_skew_est (the_data, the_col_len, &x);
		dao_set_cell_float_na (dao, col + 1, 8, x, error == 0);

		/* Minimum */
		error = range_min (the_data, the_col_len, &xmin);
		dao_set_cell_float_na (dao, col + 1, 10, xmin, error == 0);

		/* Maximum */
		error2 = range_max (the_data, the_col_len, &xmax);
		dao_set_cell_float_na (dao, col + 1, 11, xmax, error2 == 0);

		/* Range */
		dao_set_cell_float_na (dao, col + 1, 9, xmax - xmin,
				   error == 0 && error2 == 0);

		/* Sum */
		error = range_sum (the_data, the_col_len, &x);
		dao_set_cell_float_na (dao, col + 1, 12, x, error == 0);

		/* Count */
		dao_set_cell_int (dao, col + 1, 13, the_col_len);
	}
	dao_autofit_columns (dao);
}

static void
confidence_level (GPtrArray *data, gnum_float c_level,
		  data_analysis_output_t *dao, GArray *basic_stats)
{
        gnum_float x;
        guint col;
	char *buffer;
	char *format;
	desc_stats_t info;
	data_set_t *the_col;

	format = g_strdup_printf (_("/%%%s%%%% CI for the Mean from"
				    "/to"), GNUM_FORMAT_g);
	buffer = g_strdup_printf (format, c_level * 100);
	g_free (format);
	set_cell_text_col (dao, 0, 1, buffer);
        g_free (buffer);

        dao_set_cell (dao, 0, 0, NULL);

	for (col = 0; col < data->len; col++) {
		the_col = g_ptr_array_index (data, col);
		dao_set_cell_printf (dao, col + 1, 0, the_col->label);
		dao_set_italic (dao, col+1, 0, col+1, 0);

		if ((c_level < 1) && (c_level >= 0)) {
			info = g_array_index (basic_stats, desc_stats_t, col);
			if (info.error_mean == 0 && info.error_var == 0) {
				x = -qt ((1 - c_level) / 2, info.len - 1, TRUE, FALSE) *
					sqrtgnum (info.var / info.len);
				dao_set_cell_float (dao, col + 1, 1, info.mean - x);
				dao_set_cell_float (dao, col + 1, 2, info.mean + x);
				continue;
			}
		}
		dao_set_cell_na (dao, col + 1, 1);
	}
}


static void
kth_largest (GPtrArray *data, int k,
	     data_analysis_output_t *dao)
{
        gnum_float x;
        guint col;
	gint error;
	data_set_t *the_col;

        dao_set_cell_printf (dao, 0, 1, _("Largest (%d)"), k);

        dao_set_cell (dao, 0, 0, NULL);

	for (col = 0; col < data->len; col++) {
		the_col = g_ptr_array_index (data, col);
		dao_set_cell_printf (dao, col + 1, 0, the_col->label);
		dao_set_italic (dao, col+1, 0, col+1, 0);
		error = range_min_k_nonconst ((gnum_float *)(the_col->data->data),
					      the_col->data->len, &x, the_col->data->len - k);
		dao_set_cell_float_na (dao, col + 1, 1, x, error == 0);
	}
}

static void
kth_smallest (GPtrArray  *data, int k,
	      data_analysis_output_t *dao)
{
        gnum_float x;
        guint col;
	gint error;
	data_set_t *the_col;

        dao_set_cell_printf (dao, 0, 1, _("Smallest (%d)"), k);

        dao_set_cell (dao, 0, 0, NULL);

	for (col = 0; col < data->len; col++) {
		the_col = g_ptr_array_index (data, col);
		dao_set_cell_printf (dao,  col + 1, 0, the_col->label);
		dao_set_italic (dao, col+1, 0, col+1, 0);
		error = range_min_k_nonconst ((gnum_float *)(the_col->data->data),
					      the_col->data->len, &x, k - 1);
		dao_set_cell_float_na (dao, col + 1, 1, x, error == 0);
	}
}

/* Descriptive Statistics
 */
static gboolean
analysis_tool_descriptive_engine_run (data_analysis_output_t *dao, 
				      analysis_tools_data_descriptive_t *info)
{
	GPtrArray *data = NULL;

	GArray        *basic_stats = NULL;
	data_set_t    *the_col;
        desc_stats_t  local_info;
	guint         col;
	const gnum_float *the_data;

	data = new_data_set_list (info->base.input, info->base.group_by,
				  TRUE, info->base.labels, dao->sheet);

	if (info->summary_statistics || info->confidence_level) {
		basic_stats = g_array_new (FALSE, FALSE, sizeof (desc_stats_t));
		for (col = 0; col < data->len; col++) {
			the_col = g_ptr_array_index (data, col);
			the_data = (gnum_float *)the_col->data->data;
                        local_info.len = the_col->data->len;
			local_info.error_mean = range_average 
				(the_data, local_info.len, &(local_info.mean));
			local_info.error_var = range_var_est 
				(the_data, local_info.len, &(local_info.var));
			g_array_append_val (basic_stats, local_info);
		}
	}

        if (info->summary_statistics) {
                summary_statistics (data, dao, basic_stats);
		dao->offset_row += 16;
		if (dao->rows <= dao->offset_row)
			goto finish_descriptive_tool;
	}
        if (info->confidence_level) {
                confidence_level (data, info->c_level,  dao, basic_stats);
		dao->offset_row += 4;
		if (dao->rows <= dao->offset_row)
			goto finish_descriptive_tool;
	}
        if (info->kth_largest) {
                kth_largest (data, info->k_largest, dao);
		dao->offset_row += 4;
		if (dao->rows <= dao->offset_row)
			goto finish_descriptive_tool;
	}
        if (info->kth_smallest)
                kth_smallest (data, info->k_smallest, dao);

	
 finish_descriptive_tool:

	destroy_data_set_list (data);
	if (basic_stats != NULL)
		g_array_free (basic_stats, TRUE);

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
	gnum_float x;

	data = new_data_set_list (info->base.input, info->base.group_by,
				  TRUE, info->base.labels, dao->sheet);

	for (n_data = 0; n_data < data->len; n_data++) {
		for (n_sample = 0; n_sample < info->number; n_sample++) {
			GArray * sample = g_array_new (FALSE, FALSE,
						       sizeof (gnum_float));
			GArray * this_data = g_array_new (FALSE, FALSE,
							  sizeof (gnum_float));
			data_set_t * this_data_set;

			this_data_set = g_ptr_array_index (data, n_data);
			data_len = this_data_set->data->len;

			dao_set_cell_printf (dao, 0, 0, this_data_set->label);
			dao_set_italic (dao, 0, 0, 0, 0);
			dao->offset_row = 1;

			g_array_set_size (this_data, data_len);
			g_memmove (this_data->data, this_data_set->data->data,
				   sizeof (gnum_float) * data_len);

			if (info->periodic) {
				if ((info->size < 0) || (info->size > data_len)) {
					destroy_data_set_list (data);
					gnumeric_error_calc (COMMAND_CONTEXT (info->base.wbc),
						_("The requested sample size is to large for a periodic sample."));
					return TRUE;
				}
				for (i = info->size - 1; i < data_len; i += info->size) {
					x = g_array_index (this_data, gnum_float, i);
					g_array_append_val (sample, x);
				}
				write_data (info->base.wbc, dao, sample);
			} else {
				for (i = 0; i < info->size; i++) {
					guint random_index;

					if (0 == data_len)
						break;
					random_index = random_01 () * data_len;
					if (random_index == data_len)
						random_index--;
					x = g_array_index (this_data, gnum_float, random_index);
					g_array_remove_index_fast (this_data, random_index);
					g_array_append_val (sample, x);
					data_len--;
				}
				write_data (info->base.wbc, dao, sample);
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
	data_set_t *variable_1;
	data_set_t *variable_2;
	gboolean no_error;
	gnum_float mean_1 = 0, mean_2 = 0, z = 0, p = 0;
	gint mean_error_1 = 0, mean_error_2 = 0;

	variable_1 = new_data_set (info->base.range_1, TRUE, info->base.labels,
				   _("Variable %i"), 1, dao->sheet);
	variable_2 = new_data_set (info->base.range_2, TRUE, info->base.labels,
				   _("Variable %i"), 2, dao->sheet);

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

	mean_error_1 = range_average ((const gnum_float *) variable_1->data->data,
				      variable_1->data->len, &mean_1);
	mean_error_2 = range_average ((const gnum_float *) variable_2->data->data,
				      variable_2->data->len, &mean_2);
	no_error = (mean_error_1 == 0) && (mean_error_2 == 0);

	if (no_error) {
		z = (mean_1 - mean_2 - info->mean_diff) /
			sqrtgnum (info->var1 / variable_1->data->len  + info->var2 /
				  variable_2->data->len);
		p = pnorm (gnumabs (z), 0, 1, FALSE, FALSE);
	}

	/* Labels */
	dao_set_cell_printf (dao, 1, 0, variable_1->label);
	dao_set_cell_printf (dao, 2, 0, variable_2->label);

	/* Mean */
	dao_set_cell_float_na (dao, 1, 1, mean_1, mean_error_1 == 0);
	dao_set_cell_float_na (dao, 2, 1, mean_2, mean_error_2 == 0);

	/* Known Variance */
	dao_set_cell_float (dao, 1, 2, info->var1);
	dao_set_cell_float (dao, 2, 2, info->var2);

	/* Observations */
	dao_set_cell_int (dao, 1, 3, variable_1->data->len);
	dao_set_cell_int (dao, 2, 3, variable_2->data->len);

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 4, info->mean_diff);

	/* Observed Mean Difference */
	dao_set_cell_float_na (dao, 1, 5, mean_1 - mean_2, no_error);

	/* z */
	dao_set_cell_float_na (dao, 1, 6, z, no_error);

	/* P (Z<=z) one-tail */
	dao_set_cell_float_na (dao, 1, 7, p, no_error);

	/* z Critical one-tail */
	dao_set_cell_float (dao, 1, 8, qnorm (info->base.alpha, 0, 1, FALSE, FALSE));

	/* P (Z<=z) two-tail */
	dao_set_cell_float_na (dao, 1, 9, 2 * p, no_error);

	/* z Critical two-tail */
	dao_set_cell_float (dao, 1, 10, qnorm (info->base.alpha / 2, 0, 1, FALSE, FALSE));

	dao_set_italic (dao, 0, 0, 0, 10);
	dao_set_italic (dao, 0, 0, 2, 0);

	destroy_data_set (variable_1);
	destroy_data_set (variable_2);

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
	data_set_t *variable_1;
	data_set_t *variable_2;
	GArray * cleaned_variable_1;
	GArray * cleaned_variable_2;
	GSList *missing;
	GArray * difference;
	gnum_float     *current_1, *current_2;
	guint i;
	gint mean_error_1 = 0, mean_error_2 = 0, var_error_1 = 0, var_error_2 = 0;
	gint error = 0, mean_diff_error = 0, var_diff_error = 0;
	gnum_float    mean_1 = 0, mean_2 = 0;
	gnum_float    pearson, var_1, var_2, t = 0, p = 0, df, var_diff = 0, mean_diff = 0;

	variable_1 = new_data_set (info->base.range_1, TRUE, info->base.labels,
				   _("Variable %i"), 1, dao->sheet);
	variable_2 = new_data_set (info->base.range_2, TRUE, info->base.labels,
				   _("Variable %i"), 2, dao->sheet);

	if (variable_1->data->len != variable_2->data->len) {
		destroy_data_set (variable_1);
		destroy_data_set (variable_2);
		gnumeric_error_calc (COMMAND_CONTEXT (info->base.wbc),
			_("The 2 input ranges must have the same size."));
	        return TRUE;
	}

	missing = union_of_int_sets (variable_1->missing, variable_2->missing);
	cleaned_variable_1 = strip_missing (variable_1->data, missing);
	cleaned_variable_2 = strip_missing (variable_2->data, missing);

        dao_set_cell (dao, 0, 0, "");
        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/Pearson Correlation"
					"/Hypothesized Mean Difference"
					"/df"
					"/t Stat"
					"/P (T<=t) one-tail"
					"/t Critical one-tail"
					"/P (T<=t) two-tail"
					"/t Critical two-tail"));

	current_1 = (gnum_float *)cleaned_variable_1->data;
	current_2 = (gnum_float *)cleaned_variable_2->data;
	difference = g_array_new (FALSE, FALSE, sizeof (gnum_float));
	for (i = 0; i < cleaned_variable_1->len; i++) {
		gnum_float diff;
		diff = *current_1  - *current_2;
		current_1++;
		current_2++;
		g_array_append_val (difference, diff);
	}

	mean_error_1 = range_average ((const gnum_float *) cleaned_variable_1->data,
				      cleaned_variable_1->len, &mean_1);
	mean_error_2 = range_average ((const gnum_float *) cleaned_variable_2->data,
				      cleaned_variable_2->len, &mean_2);
	mean_diff_error = range_average ((const gnum_float *) difference->data,
					 difference->len, &info->mean_diff);

	if (mean_error_1 == 0)
		var_error_1 = range_var_est (
			(const gnum_float *)cleaned_variable_1->data,
			cleaned_variable_1->len , &var_1);
	if (mean_error_2 == 0)
		var_error_2 = range_var_est (
			(const gnum_float *) cleaned_variable_2->data,
			cleaned_variable_2->len , &var_2);
	if (mean_diff_error == 0)
		var_diff_error = range_var_est (
			(const gnum_float *) difference->data,
			difference->len , &var_diff);
	else
		var_diff_error = 99;

	df = cleaned_variable_1->len - 1;

	if (var_diff_error == 0) {
		t = (mean_diff - info->mean_diff) / sqrtgnum (var_diff / difference->len);
		p = pt (gnumabs (t), df, FALSE, FALSE);
	}



	/* Labels */
	dao_set_cell_printf (dao, 1, 0, variable_1->label);
	dao_set_cell_printf (dao, 2, 0, variable_2->label);

	/* Mean */
	dao_set_cell_float_na (dao, 1, 1, mean_1, mean_error_1 == 0);
	dao_set_cell_float_na (dao, 2, 1, mean_2, mean_error_2 == 0);

	/* Variance */
	dao_set_cell_float_na (dao, 1, 2, var_1, (mean_error_1 == 0) && (var_error_1 == 0));
	dao_set_cell_float_na (dao, 2, 2, var_2, (mean_error_2 == 0) && (var_error_2 == 0));

	/* Observations */
	dao_set_cell_int (dao, 1, 3, cleaned_variable_1->len);
	dao_set_cell_int (dao, 2, 3, cleaned_variable_2->len);

	/* Pearson Correlation */
	error =  range_correl_pop
		((gnum_float *)(cleaned_variable_1->data),
		 (gnum_float *)(cleaned_variable_2->data),
		 cleaned_variable_1->len, &pearson);
	dao_set_cell_float_na (dao, 1, 4, pearson, error == 0);

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 5, info->mean_diff);

	/* df */
	dao_set_cell_float (dao, 1, 6, df);

	/* t */
  	dao_set_cell_float_na (dao, 1, 7, t, var_diff_error == 0);

	/* P (T<=t) one-tail */
	dao_set_cell_float_na (dao, 1, 8, p, var_diff_error == 0);

	/* t Critical one-tail */
	dao_set_cell_float (dao, 1, 9, qt (info->base.alpha, df, FALSE, FALSE));

	/* P (T<=t) two-tail */
	dao_set_cell_float_na (dao, 1, 10, 2 * p, var_diff_error == 0);

	/* t Critical two-tail */
	dao_set_cell_float (dao, 1, 11, qt (info->base.alpha / 2, df, FALSE, FALSE));

	dao_set_italic (dao, 0, 0, 0, 11);
	dao_set_italic (dao, 0, 0, 2, 0);

	if (cleaned_variable_1 != variable_1->data)
		g_array_free (cleaned_variable_1, TRUE);
	if (cleaned_variable_2 != variable_2->data)
		g_array_free (cleaned_variable_2, TRUE);

	g_array_free (difference, TRUE);

	destroy_data_set (variable_1);
	destroy_data_set (variable_2);

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
	data_set_t *variable_1;
	data_set_t *variable_2;
	gboolean no_error;
	gnum_float mean_1 = 0, mean_2 = 0, var_1 = 0, var_2 = 0;
	gnum_float t = 0, p = 0, var = 0;
	gint df;
	gint mean_error_1 = 0, mean_error_2 = 0, var_error_1 = 0, var_error_2 = 0;

	variable_1 = new_data_set (info->base.range_1, TRUE, info->base.labels,
				   _("Variable %i"), 1, dao->sheet);
	variable_2 = new_data_set (info->base.range_2, TRUE, info->base.labels,
				   _("Variable %i"), 2, dao->sheet);

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

	mean_error_1 = range_average ((const gnum_float *) variable_1->data->data,
				      variable_1->data->len, &mean_1);
	mean_error_2 = range_average ((const gnum_float *) variable_2->data->data,
				      variable_2->data->len, &mean_2);

	if (mean_error_1 == 0)
		var_error_1 = range_var_est (
			(const gnum_float *)variable_1->data->data,
			variable_1->data->len , &var_1);
	if (mean_error_2 == 0)
		var_error_2 = range_var_est (
			(const gnum_float *) variable_2->data->data,
			variable_2->data->len , &var_2);

	df = variable_1->data->len + variable_2->data->len - 2;

	no_error = ((mean_error_1 == 0) && (mean_error_2 == 0) &&
		       (var_error_1 == 0) && (var_error_2 == 0) && (df > 0));

	if (no_error) {
		var = (var_1 * (variable_1->data->len - 1) +
		       var_2 * (variable_2->data->len - 1)) / df;
		if (var != 0) {
			t = (mean_1 - mean_2 - info->mean_diff) /
				sqrtgnum (var / variable_1->data->len + var / variable_2->data->len);
			p = pt (gnumabs (t), df, FALSE, FALSE);
		}
	}

	/* Labels */
	dao_set_cell_printf (dao, 1, 0, variable_1->label);
	dao_set_cell_printf (dao, 2, 0, variable_2->label);

	/* Mean */
	dao_set_cell_float_na (dao, 1, 1, mean_1, mean_error_1 == 0);
	dao_set_cell_float_na (dao, 2, 1, mean_2, mean_error_2 == 0);

	/* Variance */
	dao_set_cell_float_na (dao, 1, 2, var_1, (mean_error_1 == 0) && (var_error_1 == 0));
	dao_set_cell_float_na (dao, 2, 2, var_2, (mean_error_2 == 0) && (var_error_2 == 0));

	/* Observations */
	dao_set_cell_int (dao, 1, 3, variable_1->data->len);
	dao_set_cell_int (dao, 2, 3, variable_2->data->len);

	/* Pooled Variance */
	dao_set_cell_float_na (dao, 1, 4, var, no_error);

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 5, info->mean_diff);

	/* Observed Mean Difference */
	dao_set_cell_float_na (dao, 1, 6, mean_1 - mean_2,
			   (mean_error_1 == 0) && (mean_error_2 == 0));

	/* df */
	dao_set_cell_float (dao, 1, 7, df);

	/* t */
	dao_set_cell_float_na (dao, 1, 8, t, no_error && (var != 0));

	/* P (T<=t) one-tail */
	dao_set_cell_float_na (dao, 1, 9, p, no_error && (var != 0));

	/* t Critical one-tail */
	dao_set_cell_float (dao, 1, 10, qt (info->base.alpha, df, FALSE, FALSE));

	/* P (T<=t) two-tail */
	dao_set_cell_float_na (dao, 1, 11, 2 * p, no_error && (var != 0));

	/* t Critical two-tail */
	dao_set_cell_float (dao, 1, 12, qt (info->base.alpha / 2, df, FALSE, FALSE));

	dao_set_italic (dao, 0, 0, 0, 12);
	dao_set_italic (dao, 0, 0, 2, 0);

	destroy_data_set (variable_1);
	destroy_data_set (variable_2);

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
	data_set_t *variable_1;
	data_set_t *variable_2;
	gboolean no_error;
	gnum_float mean_1 = 0, mean_2 = 0, var_1 = 0, var_2 = 0;
	gnum_float t = 0, p = 0, c = 0;
	gnum_float df = 0;
	gint mean_error_1 = 0, mean_error_2 = 0, var_error_1 = 0, var_error_2 = 0;

	variable_1 = new_data_set (info->base.range_1, TRUE, info->base.labels,
				   _("Variable %i"), 1, dao->sheet);
	variable_2 = new_data_set (info->base.range_2, TRUE, info->base.labels,
				   _("Variable %i"), 2, dao->sheet);

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

	mean_error_1 = range_average ((const gnum_float *) variable_1->data->data,
				      variable_1->data->len, &mean_1);
	mean_error_2 = range_average ((const gnum_float *) variable_2->data->data,
				      variable_2->data->len, &mean_2);

	if (mean_error_1 == 0)
		var_error_1 = range_var_est (
			(const gnum_float *)variable_1->data->data,
			variable_1->data->len , &var_1);
	if (mean_error_2 == 0)
		var_error_2 = range_var_est (
			(const gnum_float *) variable_2->data->data,
			variable_2->data->len , &var_2);

	no_error = ((mean_error_1 == 0) && (mean_error_2 == 0) &&
		    (var_error_1 == 0) && (var_error_2 == 0) &&
		    (variable_1->data->len > 0) && (variable_2->data->len > 0));

	if (no_error) {
		c = (var_1 / variable_1->data->len) /
			(var_1 / variable_1->data->len + var_2 / variable_2->data->len);
		df = 1.0 / ((c * c) / (variable_1->data->len - 1.0) +
			    ((1 - c)* (1 - c)) / (variable_2->data->len - 1.0));

		t =  (mean_1 - mean_2 - info->mean_diff) /
			sqrtgnum (var_1 / variable_1->data->len + var_2 / variable_2->data->len);
		p = pt (gnumabs (t), df, FALSE, FALSE);
	}

	/* Labels */
	dao_set_cell_printf (dao, 1, 0, variable_1->label);
	dao_set_cell_printf (dao, 2, 0, variable_2->label);

	/* Mean */
	dao_set_cell_float_na (dao, 1, 1, mean_1, mean_error_1 == 0);
	dao_set_cell_float_na (dao, 2, 1, mean_2, mean_error_2 == 0);

	/* Variance */
	dao_set_cell_float_na (dao, 1, 2, var_1, (mean_error_1 == 0) && (var_error_1 == 0));
	dao_set_cell_float_na (dao, 2, 2, var_2, (mean_error_2 == 0) && (var_error_2 == 0));

	/* Observations */
	dao_set_cell_int (dao, 1, 3, variable_1->data->len);
	dao_set_cell_int (dao, 2, 3, variable_2->data->len);

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 4, info->mean_diff);

	/* Observed Mean Difference */
	dao_set_cell_float_na (dao, 1, 5, mean_1 - mean_2,
			   (mean_error_1 == 0) && (mean_error_2 == 0));

	/* df */
	dao_set_cell_float_na (dao, 1, 6, df, no_error);

	/* t */
	dao_set_cell_float_na (dao, 1, 7, t, no_error);

	/* P (T<=t) one-tail */
	dao_set_cell_float_na (dao, 1, 8, p, no_error);

	/* t Critical one-tail */
	dao_set_cell_float (dao, 1, 9, qt (info->base.alpha, df, FALSE, FALSE));

	/* P (T<=t) two-tail */
	dao_set_cell_float_na (dao, 1, 10, 2 * p, no_error);

	/* t Critical two-tail */
	dao_set_cell_float (dao, 1, 11, qt (info->base.alpha / 2, df, FALSE, FALSE));

	dao_set_italic (dao, 0, 0, 0, 11);
	dao_set_italic (dao, 0, 0, 2, 0);

	destroy_data_set (variable_1);
	destroy_data_set (variable_2);

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
				       analysis_tools_data_ttests_t *info)
{
	data_set_t *variable_1;
	data_set_t *variable_2;
	int result = 0;
	gboolean calc_error = FALSE;

	gnum_float    mean_1 = 0, mean_2 = 0, var_1 = 0, var_2 = 0, f = 0;
	gnum_float  p_right_tail = 0, q_right_tail = 0;
	gnum_float  p_left_tail = 0, q_left_tail = 0;
	gnum_float  p_2_tail = 0, q_2_tail_right = 0, q_2_tail_left = 0;
	gint  df_1= 0, df_2 = 0;
	gint mean_error_1, mean_error_2, var_error_1 = 0, var_error_2 = 0;

	variable_1 = new_data_set (info->base.range_1, TRUE, info->base.labels,
				   _("Variable %i"), 1, dao->sheet);
	variable_2 = new_data_set (info->base.range_2, TRUE, info->base.labels,
				   _("Variable %i"), 2, dao->sheet);

	if ((variable_1->data->len == 0) ||  (variable_2->data->len == 0))
	{
		destroy_data_set (variable_1);
		destroy_data_set (variable_2);
		gnumeric_error_calc (COMMAND_CONTEXT (info->base.wbc),
			_("A data set is empty"));
		return TRUE;
	} 

	dao_set_cell (dao, 0, 0, "");
	dao_set_cell (dao, 1, 0, variable_1->label);
	dao_set_cell (dao, 2, 0, variable_2->label);
	
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
	
	mean_error_1 = range_average ((const gnum_float *) variable_1->data->data,
				      variable_1->data->len, &mean_1);
	mean_error_2 = range_average ((const gnum_float *) variable_2->data->data,
				      variable_2->data->len, &mean_2);
	
	if (mean_error_1 == 0)
		var_error_1 = range_var_est (
			(const gnum_float *)variable_1->data->data,
			variable_1->data->len , &var_1);
	if (mean_error_2 == 0)
		var_error_2 = range_var_est (
			(const gnum_float *) variable_2->data->data,
			variable_2->data->len , &var_2);
	
	df_1 = variable_1->data->len - 1;
	df_2 = variable_2->data->len - 1;
	
	
	
	calc_error = !((mean_error_1 == 0) && (mean_error_2 == 0) &&
		       (var_error_1 == 0) && (var_error_2 == 0) && (var_2 != 0));
	
	if (!calc_error) {
		f = var_1/var_2;
		p_right_tail = pf (f, df_1, df_2, FALSE, FALSE);
		q_right_tail = qf (info->base.alpha, df_1, df_2, FALSE, FALSE);
		p_left_tail =  pf (f, df_1, df_2, TRUE, FALSE);
		q_left_tail =  qf (info->base.alpha, df_1, df_2, TRUE, FALSE);
		if (p_right_tail < 0.5)
			p_2_tail =  2 * p_right_tail;
		else
			p_2_tail =  2 * p_left_tail;
		
		q_2_tail_left =  qf (info->base.alpha / 2.0, df_1, df_2, TRUE, FALSE);
		q_2_tail_right  =  qf (info->base.alpha / 2.0, df_1, df_2, FALSE, FALSE);
	}
	
	/* Mean */
	dao_set_cell_float_na (dao, 1, 1, mean_1, mean_error_1 == 0);
	dao_set_cell_float_na (dao, 2, 1, mean_2, mean_error_2 == 0);
	
	/* Variance */
	dao_set_cell_float_na (dao, 1, 2, var_1, (mean_error_1 == 0) && (var_error_1 == 0));
	dao_set_cell_float_na (dao, 2, 2, var_2, (mean_error_2 == 0) && (var_error_2 == 0));
	
	/* Observations */
	dao_set_cell_int (dao, 1, 3, variable_1->data->len);
	dao_set_cell_int (dao, 2, 3, variable_2->data->len);
	
	/* df */
	dao_set_cell_int (dao, 1, 4, df_1);
	dao_set_cell_int (dao, 2, 4, df_2);
	
	/* F */
	dao_set_cell_float_na (dao, 1, 5, f, !calc_error);
	
	/* P (F<=f) right-tail */
	dao_set_cell_float_na (dao, 1, 6, p_right_tail, !calc_error);
	
	/* F Critical right-tail */
	dao_set_cell_float_na (dao, 1, 7, q_right_tail, !calc_error);
	
	/* P (F<=f) left-tail */
	dao_set_cell_float_na (dao, 1, 8, p_left_tail, !calc_error);
	
	/* F Critical left-tail */
	dao_set_cell_float_na (dao, 1, 9, q_left_tail, !calc_error);
	
	/* P (F<=f) two-tail */
	dao_set_cell_float_na (dao, 1, 10, p_2_tail, !calc_error);
	
	/* F Critical two-tail */
	dao_set_cell_float_na (dao, 1, 11, q_2_tail_left, !calc_error);
	dao_set_cell_float_na (dao, 2, 11, q_2_tail_right, !calc_error);
	
	dao_set_italic (dao, 0, 0, 0, 11);
	dao_set_italic (dao, 0, 0, 2, 0);
	
	destroy_data_set (variable_1);
	destroy_data_set (variable_2);

	return result;
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
 * does to calculate the the F-stat itself. Inference on regressions
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
analysis_tool_regression_engine_last_check (__attribute__((unused)) data_analysis_output_t *dao,
					    __attribute__((unused)) analysis_tools_data_regression_t *info)
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
	GArray       *cleaned       = NULL;
	char         *text          = NULL;
	char         *format;
	regression_stat_t   *regression_stat = NULL;
	gnum_float   r;
	gnum_float   *res,  **xss;
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
		gnumeric_error_calc (COMMAND_CONTEXT (info->base.wbc),
			_("There must be an equal number of entries for each variable in the regression."));
		return TRUE;
	}

	/* create a list of all missing or incomplete observations */
	missing = y_data->missing;
	for (i = 0; i < xdim; i++) {
		GSList *this_missing;
		GSList *the_union;

		this_missing = ((data_set_t *)g_ptr_array_index (x_data, i))->missing;
		the_union = union_of_int_sets (missing, this_missing);
		g_slist_free (missing);
		missing = the_union;
	}

	if (missing != NULL) {
		cleaned = strip_missing (y_data->data, missing);
		g_array_free (y_data->data, TRUE);
		y_data->data = cleaned;
		for (i = 0; i < xdim; i++) {
			cleaned = strip_missing (((data_set_t *)
						  g_ptr_array_index (x_data, i))->data,
						 missing);
			g_array_free (((data_set_t *)
				       g_ptr_array_index (x_data, i))->data, TRUE);
			((data_set_t *) g_ptr_array_index (x_data, i))->data = cleaned;
		}
	}

	/* data is now clean and ready */
	xss = g_new (gnum_float *, xdim);
	res = g_new (gnum_float, xdim + 1);

	for (i = 0; i < xdim; i++) {
		xss[i] = (gnum_float *)(((data_set_t *)g_ptr_array_index
					 (x_data, i))->data->data);
	}

	regression_stat = regression_stat_new ();
	regerr = linear_regression (xss, xdim,
				    (gnum_float *)(y_data->data->data),
				    y_data->data->len,
				    info->intercept, res, regression_stat);

	if (regerr != REG_ok && regerr != REG_near_singular_good) {
		regression_stat_destroy (regression_stat);
		destroy_data_set (y_data);
		destroy_data_set_list (x_data);
		g_free (xss);
		g_free (res);
		switch (regerr) {
		case REG_not_enough_data:
			gnumeric_error_calc (COMMAND_CONTEXT (info->base.wbc),
					 _("There are too few data points to conduct this "
					   "regression.\nThere must be at least as many "
					   "data points as free variables."));
			break;
			
		case REG_near_singular_bad:
			gnumeric_error_calc (COMMAND_CONTEXT (info->base.wbc),
					 _("Two or more of the independent variables "
					   "are nearly linear\ndependent.  All numerical "
					   "precision was lost in the computation."));
                break;
		
		case REG_singular:
			gnumeric_error_calc (COMMAND_CONTEXT (info->base.wbc),
					 _("Two or more of the independent variables "
					   "are linearly\ndependent, and the regression "
					   "cannot be calculated.\n\nRemove one of these\n"
					   "variables and try the regression again."));
			break;
			
		case REG_invalid_data:
		case REG_invalid_dimensions:
			gnumeric_error_calc (COMMAND_CONTEXT (info->base.wbc),
					 _("There must be an equal number of entries "
					   "for each variable in the regression."));
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
	for (i = 0; i < xdim; i++)
		dao_set_cell (dao, 0, 17 + i, ((data_set_t *)g_ptr_array_index (x_data, i))->label);
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
				  GNUM_FORMAT_f,
				  GNUM_FORMAT_f);
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
		cor_err =  range_correl_pop (xss[0], (gnum_float *)(y_data->data->data),
					     y_data->data->len, &r);
	else r = sqrtgnum (regression_stat->sqr_r);

	/* Multiple R */
	dao_set_cell_float_na (dao, 1, 3, r, cor_err == 0);

	/* R Square */
	dao_set_cell_float (dao, 1, 4, regression_stat->sqr_r);

	/* Adjusted R Square */
	dao_set_cell_float (dao, 1, 5, regression_stat->adj_sqr_r);

	/* Standard Error */
	dao_set_cell_float (dao, 1, 6, sqrtgnum (regression_stat->var));

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
		gnum_float this_res = res[i + 1];
		/*
		 * With no intercept se[0] is for the first slope variable;
		 * with intercept, se[1] is the first slope se
		 */
		gnum_float this_se = regression_stat->se[info->intercept + i];
		gnum_float this_tval = regression_stat->t[info->intercept + i];
		gnum_float t, P;

		/* Coefficient */
		dao_set_cell_float (dao, 1, 17 + i, this_res);

		/* Standard Error */
		dao_set_cell_float (dao, 2, 17 + i, this_se);

		/* t Stat */
		dao_set_cell_float (dao, 3, 17 + i, this_tval);

		/* P values */
		P = finitegnum (this_tval)
			? 2 * pt (gnumabs (this_tval), regression_stat->df_resid, FALSE, FALSE)
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

	regression_stat_destroy (regression_stat);
	destroy_data_set (y_data);
	destroy_data_set_list (x_data);
	g_free (xss);
	g_free (res);
	
	if (regerr == REG_near_singular_good) 
		gnumeric_error_calc (COMMAND_CONTEXT (info->base.wbc),
			_("Two or more of the independent variables "
			  "are nearly linear\ndependent.  Treat the "
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
		if (!check_input_range_list_homogeneity (info->base.input)) {
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
	gnum_float    *prev, *prev_av;

	data = new_data_set_list (info->base.input, info->base.group_by,
				  TRUE, info->base.labels, dao->sheet);

	prev = g_new (gnum_float, info->interval);
	prev_av = g_new (gnum_float, info->interval);

	for (dataset = 0; dataset < data->len; dataset++) {
		data_set_t    *current;
		gnum_float    sum;
		gnum_float    std_err;
		gint         row;
		int           add_cursor, del_cursor;

		current = g_ptr_array_index (data, dataset);
		dao_set_cell_printf (dao, col, 0, current->label);
		if (info->std_error_flag)
			dao_set_cell_printf (dao, col + 1, 0, _("Standard Error"));

		add_cursor = del_cursor = 0;
		sum = 0;
		std_err = 0;

		for (row = 0; row < info->interval - 1; row++) {
			prev[add_cursor] = g_array_index
				(current->data, gnum_float, row);
			sum += prev[add_cursor];
			++add_cursor;
			dao_set_cell_na (dao, col, row + 1);
			if (info->std_error_flag)
				dao_set_cell_na (dao, col + 1, row + 1);
		}
		for (row = info->interval - 1; row < (gint)current->data->len; row++) {
			prev[add_cursor] = g_array_index
				(current->data, gnum_float, row);
			sum += prev[add_cursor];
			prev_av[add_cursor] = sum / info->interval;
			dao_set_cell_float (dao, col, row + 1, prev_av[add_cursor]);
			sum -= prev[del_cursor];
			if (info->std_error_flag) {
				std_err += (prev[add_cursor] - prev_av[add_cursor]) *
					(prev[add_cursor] - prev_av[add_cursor]);
				if (row >= 2 * info->interval - 2) {
					dao_set_cell_float (dao, col + 1, row + 1,
							sqrtgnum (std_err / info->interval));
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
		gnum_float    a, f, F[2] = { 0, 0 }, A[2] = { 0, 0 };
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
				        gnum_float m1 = a - f;
					gnum_float m2 = A[0] - F[0];
					gnum_float m3 = A[1] - F[1];

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
			a = g_array_index (current->data, gnum_float, row);
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
        gnum_float x;
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
		        gnum_float x = g_array_index (this_data_set->data, gnum_float, i);

			rank[i].point = i + 1;
			rank[i].x = x;
			rank[i].rank = 1;
			rank[i].same_rank_count = -1;

			for (j = 0; j < this_data_set->data->len; j++) {
			        gnum_float y = g_array_index (this_data_set->data, gnum_float, j);
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

			/* Value */
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
	GPtrArray *data = NULL;
	guint index, i;
	gint error;
	gint N = 0;
	gnum_float ss_b, ss_w, ss_t;
	gnum_float ms_b, ms_w, f = 0.0, p = 0.0, f_c = 0.0;
	gnum_float overall_mean;
	gnum_float *treatment_mean = NULL;
	gnum_float *ss_terms = NULL;
	int        df_b, df_w, df_t;

	prepare_input_range (&info->base.input, info->base.group_by);
	data = new_data_set_list (info->base.input, info->base.group_by,
				  TRUE, info->base.labels, dao->sheet);


	dao_set_cell (dao, 0, 0, _("Anova: Single Factor"));
	dao_set_cell (dao, 0, 2, _("SUMMARY"));
	set_cell_text_row (dao, 0, 3, _("/Groups"
					"/Count"
					"/Sum"
					"/Average"
					"/Variance"));
	dao_set_italic (dao, 0, 0, 0, data->len + 11);
	dao_set_italic (dao, 0, 3, 4, 3);

	dao->offset_row += 4;
	if (dao->rows <= dao->offset_row)
		goto finish_anova_single_factor_tool;

	/* SUMMARY & ANOVA calculation*/
	treatment_mean = g_new (gnum_float, data->len);
	for (index = 0; index < data->len; index++) {
		gnum_float x;
		data_set_t *current_data;
		gnum_float *the_data;

		current_data = g_ptr_array_index (data, index);
		the_data = (gnum_float *)current_data->data->data;

		/* Label */
		dao_set_cell_printf (dao, 0, index, current_data->label);

		/* Count */
		dao_set_cell_int (dao, 1, index, current_data->data->len);
		N += current_data->data->len;

		/* Sum */
		error = range_sum (the_data,
				   current_data->data->len, &x);
		dao_set_cell_float_na (dao, 2, index, x, error == 0);

		/* Average */
		error = range_average (the_data,
				       current_data->data->len, &x);
		dao_set_cell_float_na (dao, 3, index, x, error == 0);
		treatment_mean[index] = x;

		/* Variance */
		error = range_var_est (the_data,
				       current_data->data->len, &x);
		dao_set_cell_float_na (dao, 4, index, x, error == 0);

		/* Further Calculations */;
		error = range_sumsq (the_data, current_data->data->len, &x);
	}

	dao->offset_row += data->len + 2;
	if (dao->rows <= dao->offset_row)
		goto finish_anova_single_factor_tool;


	set_cell_text_col (dao, 0, 0, _("/ANOVA"
					"/Source of Variation"
					"/Between Groups"
					"/Within Groups"
					"/Total"));
	set_cell_text_row (dao, 1, 1, _("/SS"
					"/df"
					"/MS"
					"/F"
					"/P-value"
					"/F critical"));
	dao_set_italic (dao, 1, 1, 6, 1);

	/* ANOVA */
	df_b = data->len - 1;
	df_w = N - data->len;
	df_t = N - 1;

	error = range_average (treatment_mean, data->len, &overall_mean);
	ss_terms = g_new (gnum_float, data->len);
	for (index = 0; index < data->len; index++) {
		data_set_t *current_data = g_ptr_array_index (data, index);
		gnum_float diff = treatment_mean[index] - overall_mean;

		ss_terms[index] = current_data->data->len * (diff * diff);
	}
	error = range_sum (ss_terms, data->len, &ss_b);

	for (index = 0; index < data->len; index++) {
		data_set_t *current_data = g_ptr_array_index (data, index);
		gnum_float *the_data = (gnum_float *)current_data->data->data;

		ss_terms[index] = 0;
		for (i = 0; i < current_data->data->len; i++) {
			gnum_float diff = the_data[i] - treatment_mean[index];
			ss_terms[index] += diff * diff;
		}
	}
	error = range_sum (ss_terms, data->len, &ss_w);
	g_free (ss_terms);

	ss_t = ss_b + ss_w;
	ms_b = ss_b / df_b;
	ms_w = ss_w / df_w;
	f    = ms_b / ms_w;
	p    = pf (f, df_b, df_w, FALSE, FALSE);
	f_c  = qf (info->alpha, df_b, df_w, FALSE, FALSE);

	dao_set_cell_float (dao, 1, 2, ss_b);
	dao_set_cell_float (dao, 1, 3, ss_w);
	dao_set_cell_float (dao, 1, 4, ss_t);
	dao_set_cell_int (dao, 2, 2, df_b);
	dao_set_cell_int (dao, 2, 3, df_w);
	dao_set_cell_int (dao, 2, 4, df_t);
	dao_set_cell_float (dao, 3, 2, ms_b);
	dao_set_cell_float (dao, 3, 3, ms_w);
	dao_set_cell_float (dao, 4, 2, f);
	dao_set_cell_float (dao, 5, 2, p);
	dao_set_cell_float (dao, 6, 2, f_c);

finish_anova_single_factor_tool:

	if (treatment_mean != NULL)
		g_free (treatment_mean);

	dao->offset_row = 0;
	dao->offset_col = 0;

	destroy_data_set_list (data);

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
		info->row_input_range = g_slist_prepend (NULL, value_duplicate (info->input));
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
	gnum_float    cm,  sum = 0;
	gnum_float    ss_r = 0, ss_c = 0, ss_e = 0, ss_t = 0;
	gnum_float    ms_r, ms_c, ms_e, f1, f2, p1, p2, f1_c, f2_c;
	int        df_r, df_c, df_e, df_t;


	row_data = new_data_set_list (info->row_input_range, GROUPED_BY_ROW,
				  FALSE, info->labels, dao->sheet);
	col_data = new_data_set_list (info->col_input_range, GROUPED_BY_COL,
				  FALSE, info->labels, dao->sheet);
	if (check_data_for_missing (row_data) ||
	    check_data_for_missing (col_data)) {
		destroy_data_set_list (row_data);
		destroy_data_set_list (col_data);
		gnumeric_error_calc (COMMAND_CONTEXT (info->wbc),
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
	        gnum_float v;
		data_set_t *data_set;
		gnum_float *the_data;

		data_set = (data_set_t *)g_ptr_array_index (row_data, i);
		the_data = (gnum_float *)data_set->data->data;

		dao_set_cell (dao, 0, i + 3, data_set->label);
		dao_set_cell_int (dao, 1, i + 3, data_set->data->len);
		error = range_sum (the_data, data_set->data->len, &v);
		sum +=  v;
		ss_r += v * v/ data_set->data->len;
		dao_set_cell_float_na (dao, 2, i + 3, v, error == 0);
		dao_set_cell_float_na (dao, 3, i + 3,  v / data_set->data->len,
				   error == 0 && data_set->data->len != 0);
		error = range_var_est (the_data, data_set->data->len , &v);
		dao_set_cell_float_na (dao, 4, i + 3, v, error == 0);

		error = range_sumsq (the_data, data_set->data->len, &v);
		ss_t += v;
	}

	for (i = 0; i < cols; i++) {
	        gnum_float v;
		data_set_t *data_set;
		gnum_float *the_data;

		data_set = (data_set_t *)g_ptr_array_index (col_data, i);
		the_data = (gnum_float *)data_set->data->data;

		dao_set_cell (dao, 0, i + 4 + rows, data_set->label);
		dao_set_cell_int (dao, 1, i + 4 + rows, data_set->data->len);
		error = range_sum (the_data, data_set->data->len, &v);
		ss_c += v * v/ data_set->data->len;
		dao_set_cell_float_na (dao, 2, i + 4 + rows, v, error == 0);
		dao_set_cell_float_na (dao, 3, i + 4 + rows, v / data_set->data->len,
				   error == 0 && data_set->data->len != 0);
		error = range_var_est (the_data, data_set->data->len , &v);
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
		Cell *cell;
		cell = sheet_cell_get (sheet, col, row);
		if (cell)
			rendered_text = cell_get_rendered_text (cell);
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
	Value *input_cp;
	gint df_r, df_c, df_rc, df_e, df_total;
	gnum_float ss_r = 0.0, ss_c = 0.0, ss_rc = 0.0, ss_e = 0.0, ss_total = 0.0;
	gnum_float ms_r, ms_c, ms_rc, ms_e;
	gnum_float f_r, f_c, f_rc;
	gnum_float p_r, p_c, p_rc;
	gnum_float cm = 0.0;
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

	input_cp = value_duplicate (info->input);
	input_cp->v_range.cell.b.row = input_cp->v_range.cell.a.row +
		info->replication - 1;
	row_data = g_ptr_array_new ();
	while (input_cp->v_range.cell.a.row < info->input->v_range.cell.b.row) {
		GPtrArray *col_data = NULL;
		GSList *col_input_range = NULL;

		col_input_range = g_slist_prepend (NULL, value_duplicate (input_cp));
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
			data_set_t *cell_data;
			guint num;

			cell_data = g_ptr_array_index (data, i_c);
			num = cell_data->data->len;
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
		gnumeric_error_calc (COMMAND_CONTEXT (info->wbc),
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
		gnum_float row_sum = 0.0;
		gnum_float row_sum_sq = 0.0;
		guint row_cnt = 0;

		data = g_ptr_array_index (row_data, i_r);
		for (i_c = 0; i_c < info->n_c; i_c++) {
			data_set_t *cell_data;
			gnum_float v;
			int error;
			int num;
			gnum_float *the_data;

			cell_data = g_ptr_array_index (data, i_c);
			the_data = (gnum_float *)cell_data->data->data;
			num = cell_data->data->len;
			row_cnt += num;

			dao_set_cell_int (dao, 1 + i_c, 4 + i_r * 6, num);
			error = range_sum (the_data, num, &v);
			row_sum += v;
			ss_rc += v * v / num;
			dao_set_cell_float_na (dao, 1 + i_c, 5 + i_r * 6, v, error == 0);
			dao_set_cell_float_na (dao, 1 + i_c, 6 + i_r * 6, v / num,
					   error == 0 && num > 0);
			error = range_var_est (the_data, num , &v);
			dao_set_cell_float_na (dao, 1 + i_c, 7 + i_r * 6, v, error == 0);

			error = range_sumsq (the_data, num, &v);
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
		gnum_float col_sum = 0.0;
		gnum_float col_sum_sq = 0.0;
		guint col_cnt = 0;

		for (i_r = 0; i_r < info->n_r; i_r++) {
			data_set_t *cell_data;
			gnum_float v;
			int error;
			gnum_float *the_data;
			GPtrArray *data = NULL;

			data = g_ptr_array_index (row_data, i_r);
			cell_data = g_ptr_array_index (data, i_c);
			the_data = (gnum_float *)cell_data->data->data;

			col_cnt += cell_data->data->len;
			error = range_sum (the_data, cell_data->data->len, &v);
			col_sum += v;

			error = range_sumsq (the_data, cell_data->data->len, &v);
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
					gnum_float *the_data, v;
					guint i;

					the_data = (gnum_float *)cell_data->data->data;
					error = range_average (the_data, num, &v);
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
			gnum_float row_sum = 0.0;
			gnum_float row_sum_sq = 0.0;
			guint row_cnt = 0;

			data = g_ptr_array_index (row_data, i_r);
			for (i_c = 0; i_c < info->n_c; i_c++) {
				data_set_t *cell_data;
				gnum_float v;
				int error;
				int num;
				gnum_float *the_data;

				cell_data = g_ptr_array_index (data, i_c);
				the_data = (gnum_float *)cell_data->data->data;
				num = cell_data->data->len;
				row_cnt += num;

				error = range_sum (the_data, num, &v);
				row_sum += v;
				ss_rc += v * v / num;

				error = range_sumsq (the_data, num, &v);
				row_sum_sq += v;
			}
			cm += row_sum;
			n += row_cnt;
			ss_r += row_sum * row_sum / row_cnt;
			ss_total += row_sum_sq;
		}

		for (i_c = 0; i_c < info->n_c; i_c++) {
			gnum_float col_sum = 0.0;
			guint col_cnt = 0;

			for (i_r = 0; i_r < info->n_r; i_r++) {
				data_set_t *cell_data;
				gnum_float v;
				int error;
				gnum_float *the_data;
				GPtrArray *data = NULL;

				data = g_ptr_array_index (row_data, i_r);
				cell_data = g_ptr_array_index (data, i_c);
				the_data = (gnum_float *)cell_data->data->data;

				col_cnt += cell_data->data->len;
				error = range_sum (the_data, cell_data->data->len, &v);
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
analysis_tool_anova_two_factor_engine_clean (__attribute__((unused)) data_analysis_output_t *dao,
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



/************* Histogram Tool *********************************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

typedef struct {
	gnum_float limit;
	GArray     *counts;
	char       *label;
	gboolean   strict;
	gboolean   first;
	gboolean   last;
	gboolean   destroy_label;
} bin_t;

static gint
bin_compare (const bin_t *set_a, const bin_t *set_b)
{
	gnum_float a, b;

	a = set_a->limit;
	b = set_b->limit;

        if (a < b)
                return -1;
        else if (a == b)
                return 0;
        else
                return 1;
}

static gint
bin_pareto_at_i (const bin_t *set_a, const bin_t *set_b, guint index)
{
	gnum_float a, b;

	if (set_a->counts->len <= index)
		return 0;

	a = g_array_index (set_a->counts, gnum_float, index);
	b = g_array_index (set_b->counts, gnum_float, index);

        if (a > b)
                return -1;
        else if (a == b)
                return bin_pareto_at_i (set_a, set_b, index + 1);
        else
                return 1;
}

static gint
bin_pareto (const bin_t *set_a, const bin_t *set_b)
{
	return bin_pareto_at_i (set_a, set_b, 0);
}

static void
destroy_items (gpointer data, __attribute__((unused)) gpointer user_data) {
	if (((bin_t*)data)->label != NULL && ((bin_t*)data)->destroy_label)
		g_free (((bin_t*)data)->label);
	g_free (data);
}

static gboolean
analysis_tool_histogram_engine_run (data_analysis_output_t *dao,
				  analysis_tools_data_histogram_t *info, GPtrArray **bin_data)
{

	GPtrArray *data = NULL;
	GSList *bin_list = NULL;
	bin_t  *a_bin;
	guint  i, j, row, col;
	GSList * this;
	gnum_float *this_value;


	data = new_data_set_list (info->input, info->group_by,
				  TRUE, info->labels, dao->sheet);

/* set up counter structure */
	if (info->bin != NULL) {
		for (i=0; i < (*bin_data)->len; i++) {
			a_bin = g_new (bin_t, 1);
			a_bin->limit = g_array_index (
				((data_set_t *)g_ptr_array_index ((*bin_data), i))->data,
				gnum_float, 0);
			a_bin->counts = g_array_new (FALSE, TRUE, sizeof (gnum_float));
			a_bin->counts = g_array_set_size (a_bin->counts, data->len);
			a_bin->label = ((data_set_t *)g_ptr_array_index ((*bin_data), i))->label;
			a_bin->destroy_label = FALSE;
			a_bin->last = FALSE;
			a_bin->first = FALSE;
			a_bin->strict = FALSE;
			bin_list = g_slist_prepend (bin_list, a_bin);
		}
		bin_list = g_slist_sort (bin_list,
					 (GCompareFunc) bin_compare);
	} else {
		gnum_float skip;
		gboolean value_set;
		char        *text;
		Value       *val;

		if (!info->max_given) {
			value_set = FALSE;
			for (i = 0; i < data->len; i++) {
				GArray * the_data;
				gnum_float a_max;
				the_data = ((data_set_t *)(g_ptr_array_index (data, i)))->data;
				if (0 == range_max ((gnum_float *)the_data->data, the_data->len,
						    &a_max)) {
					if (value_set) {
						if (a_max > info->max)
							info->max = a_max;
					} else {
						info->max = a_max;
						value_set = TRUE;
					}
				}
			}
			if (!value_set)
				info->max = 0.0;
		}
		if (!info->min_given) {
			value_set = FALSE;
			for (i = 0; i < data->len; i++) {
				GArray * the_data;
				gnum_float a_min;
				the_data = ((data_set_t *)(g_ptr_array_index (data, i)))->data;
				if (0 == range_min ((gnum_float *)the_data->data, the_data->len,
						    &a_min)) {
					if (value_set) {
						if (a_min < info->min)
							info->min = a_min;
					} else {
						info->min = a_min;
						value_set = TRUE;
					}
				}
			}
			if (!value_set)
				info->min = 0.0;
		}

		skip = (info->max - info->min) / info->n;
		for (i = 0; (int)i < info->n;  i++) {
			a_bin = g_new (bin_t, 1);
			a_bin->limit = info->max - i * skip;
			a_bin->counts = g_array_new (FALSE, TRUE, sizeof (gnum_float));
			a_bin->counts = g_array_set_size (a_bin->counts, data->len);
			a_bin->label = NULL;
			a_bin->destroy_label = FALSE;
			a_bin->last = FALSE;
			a_bin->first = FALSE;
			a_bin->strict = FALSE;
			bin_list = g_slist_prepend (bin_list, a_bin);
		}
		a_bin = g_new (bin_t, 1);
		a_bin->limit = info->min;
		a_bin->counts = g_array_new (FALSE, TRUE, sizeof (gnum_float));
		a_bin->counts = g_array_set_size (a_bin->counts, data->len);
		val = value_new_float(info->min);
		text = format_value (NULL, val, NULL, 10);
		if (text) {
			a_bin->label = g_strdup_printf (_("<%s"), text);
			a_bin->destroy_label = TRUE;
			g_free (text);
		} else {
			a_bin->label = _("Too Small");
			a_bin->destroy_label = FALSE;
		}
		if (val)
			value_release (val);
		a_bin->last = FALSE;
		a_bin->first = TRUE;
		a_bin->strict = TRUE;
		bin_list = g_slist_prepend (bin_list, a_bin);
		info->bin_labels = FALSE;
	}
	a_bin = g_new (bin_t, 1);
	a_bin->limit = 0.0;
	a_bin->counts = g_array_new (FALSE, TRUE, sizeof (gnum_float));
	a_bin->counts = g_array_set_size (a_bin->counts, data->len);
	a_bin->destroy_label = FALSE;
	if (info->bin != NULL) {
		a_bin->label = _("More");
	} else {
		char        *text;
		Value       *val;

		val = value_new_float(info->max);
		text = format_value (NULL, val, NULL, 10);
		if (text) {
			a_bin->label = g_strdup_printf (_(">%s"), text);
			a_bin->destroy_label = TRUE;
			g_free (text);
		} else
			a_bin->label = _("Too Large");
		if (val)
			value_release (val);
	}
	a_bin->last = TRUE;
	a_bin->first = FALSE;
	a_bin->strict = FALSE;
	bin_list = g_slist_append (bin_list, a_bin);

/* count data */
	for (i = 0; i < data->len; i++) {
		GArray * the_data;
		gnum_float *the_sorted_data;

		the_data = ((data_set_t *)(g_ptr_array_index (data, i)))->data;
		the_sorted_data =  range_sort ((gnum_float *)(the_data->data), the_data->len);

		this = bin_list;
		this_value = the_sorted_data;

		for (j = 0; j < the_data->len;) {
			if ((*this_value < ((bin_t *)this->data)->limit) ||
			    (*this_value == ((bin_t *)this->data)->limit &&
			     !((bin_t *)this->data)->strict) ||
			    (this->next == NULL)){
				(g_array_index (((bin_t *)this->data)->counts,
						gnum_float, i))++;
				j++;
				this_value++;
			} else {
				this = this->next;
			}
		}
		g_free (the_sorted_data);
	}

/* sort if pareto */
	if (info->pareto && (data->len > 0))
		bin_list = g_slist_sort (bin_list,
					 (GCompareFunc) bin_pareto);

/* print labels */
	row = info->labels ? 1 : 0;
	if (!info->bin_labels)
		dao_set_cell (dao, 0, row, _("Bin"));

	this = bin_list;
	while (this != NULL) {
		row++;
		if (info->bin_labels || ((bin_t *)this->data)->last || ((bin_t *)this->data)->first) {
			dao_set_cell (dao, 0, row, ((bin_t *)this->data)->label);
		} else {
			dao_set_cell_float (dao, 0, row, ((bin_t *)this->data)->limit);
		}
		this = this->next;
	}
	dao_set_italic (dao, 0, 0, 0, row);

	col = 1;
	for (i = 0; i < data->len; i++) {
		guint l_col = col;
		gnum_float y = 0.0;
		row = 0;

		if (info->labels) {
			dao_set_cell (dao, col, row,
				  ((data_set_t *)g_ptr_array_index (data, i))->label);
			row++;
		}
		dao_set_cell (dao, col, row, _("Frequency"));
		if (info->percentage)
			dao_set_cell (dao, ++l_col, row, _("%"));
		if (info->cumulative)
			/* xgettext:no-c-format */
			dao_set_cell (dao, ++l_col, row, _("Cumulative %"));
/* print data */
		this = bin_list;
		while (this != NULL) {
			gnum_float x;

			l_col = col;
			x = g_array_index (((bin_t *)this->data)->counts, gnum_float, i);
			row ++;
			dao_set_cell_float (dao, col, row,  x);
			x /= ((data_set_t *)(g_ptr_array_index (data, i)))->data->len;
			if (info->percentage) {
				l_col++;
				dao_set_percent (dao, l_col, row, l_col, row);
				dao_set_cell_float (dao, l_col, row, x);
			}
			if (info->cumulative) {
				y += x;
				l_col++;
				dao_set_percent (dao, l_col, row, l_col, row);
				dao_set_cell_float (dao, l_col, row, y);
			}
			this = this->next;
		}
		col++;
		if (info->percentage)
			col++;
		if (info->cumulative)
			col++;
	}
	dao_set_italic (dao, 0, 0,  col - 1, info->labels ? 1 : 0);

/* finish up */
	destroy_data_set_list (data);
	g_slist_foreach (bin_list, destroy_items, NULL);
	g_slist_free (bin_list);

	if (*bin_data) {
		destroy_data_set_list (*bin_data);
		*bin_data = NULL;
	}

	if (info->chart)
		g_warning ("TODO : tie this into the graph generator");
	return FALSE;
}

static gboolean
analysis_tool_histogram_engine_check_bins (data_analysis_output_t *dao, 
					   analysis_tools_data_histogram_t *info, 
					   GPtrArray **bin_data_cont)
{
	GPtrArray *bin_data = NULL;
	guint  i;

	if (info->bin == NULL) 
		return FALSE;

	bin_data = new_data_set_list (info->bin, GROUPED_BY_BIN,
				      TRUE, info->bin_labels, dao->sheet);
	for (i = 0; i < bin_data->len; i++) {
		if (((data_set_t *)g_ptr_array_index (bin_data, i))->data->len != 1) {
			destroy_data_set_list (bin_data);
			return TRUE;
		}
	}
	*bin_data_cont = bin_data;

	return FALSE;
}

gboolean 
analysis_tool_histogram_engine (data_analysis_output_t *dao, gpointer specs, 
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_histogram_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Histogram (%s)"), result) 
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO: 
		prepare_input_range (&info->input, info->group_by);
		if (info->bin)
			prepare_input_range (&info->bin, GROUPED_BY_ROW);
		dao_adjust (dao, 
			    1 + (1 + (info->cumulative ? 1 : 0) +
				 (info->percentage ? 1 : 0)) * g_slist_length (info->input), 
			    2 + (info->bin ? g_slist_length (info->bin) : info->n) +
			    (info->labels ? 1 : 0));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		range_list_destroy (info->input);
		range_list_destroy (info->bin);
		return FALSE;
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return analysis_tool_histogram_engine_check_bins (dao, specs, result);
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Histogram"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Histogram"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_histogram_engine_run (dao, specs, result);
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

static void
fourier_fft (const complex_t *in, int n, int skip, complex_t **fourier, gboolean inverse)
{
	complex_t  *fourier_1, *fourier_2;
	int        i;
	int        nhalf = n / 2;
	gnum_float argstep;

	*fourier = g_new (complex_t, n);

	if (n == 1) {
		(*fourier)[0] = in[0];
		return;
	}

	fourier_fft (in, nhalf, skip * 2, &fourier_1, inverse);
	fourier_fft (in + skip, nhalf, skip * 2, &fourier_2, inverse);

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
		gnum_float    zero_val = 0.0;

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
				      ((const gnum_float *)current->data->data)[i]);

		fourier_fft (in, desired_length, 1, &fourier, info->inverse);
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
analysis_tool_fourier_calc_length (data_analysis_output_t *dao, 
				   analysis_tools_data_fourier_t *info)
{
	GPtrArray     *data = new_data_set_list (info->base.input, info->base.group_by,
						 TRUE, info->base.labels, dao->sheet);
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
			    2 + analysis_tool_fourier_calc_length (dao, specs));
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
