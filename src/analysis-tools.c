/*
 * analysis-tools.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * Modified 2001 to use range_* functions of mathfunc.h
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "analysis-tools.h"

#include "mathfunc.h"
#include "rangefunc.h"
#include "numbers.h"
#include "dialogs.h"
#include "parse-util.h"
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

#include <libgnome/gnome-i18n.h>
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
cb_store_data (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
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

	result = workbook_foreach_cell_in_range (pos, range, FALSE,
						 cb_store_data, the_set);

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
	g_ptr_array_free (the_list, FALSE);
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

void
set_cell_value (data_analysis_output_t *dao, int col, int row, Value *v)
{
        Cell *cell;

	/* Check that the output is in the given range, but allow singletons
	 * to expand
	 */
	if (dao->type == RangeOutput &&
	    (dao->cols > 1 || dao->rows > 1) &&
	    (col >= dao->cols || row >= dao->rows))
	        return;

	cell = sheet_cell_fetch (dao->sheet, dao->start_col + col,
				 dao->start_row + row);

	sheet_cell_set_value (cell, v, NULL);
}


void
set_cell (data_analysis_output_t *dao, int col, int row, const char *text)
{
	if (text == NULL) {
		/* FIXME: should we erase instead?  */
		set_cell_value (dao, col, row, value_new_empty ());
	} else {
		set_cell_value (dao, col, row, value_new_string (text));
	}
}


void
set_cell_printf (data_analysis_output_t *dao, int col, int row, const char *fmt, ...)
{
	char *buffer;
	va_list args;

	va_start (args, fmt);
	buffer = g_strdup_vprintf (fmt, args);
	va_end (args);

	set_cell_value (dao, col, row, value_new_string (buffer));
	g_free (buffer);
}


void
set_cell_float (data_analysis_output_t *dao, int col, int row, gnum_float v)
{
	set_cell_value (dao, col, row, value_new_float (v));
}


void
set_cell_int (data_analysis_output_t *dao, int col, int row, int v)
{
	set_cell_value (dao, col, row, value_new_int (v));
}


void
set_cell_na (data_analysis_output_t *dao, int col, int row)
{
	set_cell_value (dao, col, row, value_new_error (NULL, gnumeric_err_NA));
}

static void
set_cell_float_na (data_analysis_output_t *dao, int col, int row, gnum_float v, gboolean is_valid)
{
	if (is_valid) {
		set_cell_float (dao, col, row, v);
	} else {
		set_cell_na (dao, col, row);
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
		set_cell_value (dao, col, row++, value_new_string (p));
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
		set_cell_value (dao, col++, row, value_new_string (p));
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
		return (value_terminate ());
	if (cell == NULL)
		cell = sheet_cell_new (sheet, col, row);
	x = g_array_index (data, gnum_float, 0);
	g_array_remove_index (data, 0);
	sheet_cell_set_value (cell, value_new_float (x), NULL);
	return NULL;
}

static void
write_data (WorkbookControl *wbc, data_analysis_output_t *dao, GArray *data)
{
	gint st_row = dao->start_row;
	gint end_row = dao->start_row + data->len - 1;
	gint st_col = dao->start_col, end_col = dao->start_col;

	if (dao->type == RangeOutput) {
		end_row = st_row + dao->rows - 1;
		if (dao->cols == 0)
			return;
	}

	sheet_foreach_cell_in_range (dao->sheet, FALSE, st_col, st_row, end_col, end_row,
				     (ForeachCellCB)&WriteData_ForeachCellCB, data);
}

void
prepare_output (WorkbookControl *wbc, data_analysis_output_t *dao, const char *name)
{
	char *unique_name;

	if (dao->type == NewSheetOutput) {
		Workbook *wb = wb_control_workbook (wbc);
		unique_name = workbook_sheet_get_free_name (wb, name, FALSE, FALSE);
	        dao->sheet = sheet_new (wb, unique_name);
		g_free (unique_name);
		dao->start_col = dao->start_row = 0;
		workbook_sheet_attach (wb, dao->sheet, NULL);
	} else if (dao->type == NewWorkbookOutput) {
		Workbook *wb = workbook_new ();
		dao->sheet = sheet_new (wb, name);
		dao->start_col = dao->start_row = 0;
		workbook_sheet_attach (wb, dao->sheet, NULL);
		(void)wb_control_wrapper_new (wbc, NULL, wb);
	}
}

void
autofit_column (data_analysis_output_t *dao, int col)
{
        int ideal_size, actual_col;

	actual_col = dao->start_col + col;

	ideal_size = sheet_col_size_fit_pixels (dao->sheet, actual_col);
	if (ideal_size == 0)
	        return;

	sheet_col_set_size_pixels (dao->sheet, actual_col, ideal_size, TRUE);
	sheet_recompute_spans_for_col (dao->sheet, col);
}

static void
autofit_columns (data_analysis_output_t *dao, int from, int to)
{
	int i;

	if (!dao->autofit_flag)
		return;
	for (i = from; i <= to; i++)
		autofit_column (dao,i);
}

static void
set_italic (data_analysis_output_t *dao, int col1, int row1,
	    int col2, int row2)
{
	MStyle *mstyle = mstyle_new ();
	Range  range;

	range.start.col = col1 + dao->start_col;
	range.start.row = row1 + dao->start_row;
	range.end.col   = col2 + dao->start_col;
	range.end.row   = row2 + dao->start_row;

	mstyle_set_font_italic (mstyle, TRUE);
	sheet_style_apply_range (dao->sheet, &range, mstyle);
}

static void
set_percent (data_analysis_output_t *dao, int col1, int row1,
	    int col2, int row2)
{
	MStyle *mstyle = mstyle_new ();
	Range  range;

	range.start.col = col1 + dao->start_col;
	range.start.row = row1 + dao->start_row;
	range.end.col   = col2 + dao->start_col;
	range.end.row   = row2 + dao->start_row;

	mstyle_set_format_text (mstyle, "0.00%");
	sheet_style_apply_range (dao->sheet, &range, mstyle);
}


/************* Correlation Tool *******************************************
 *
 * The correlation tool calculates the correlation coefficient of two
 * data sets.  The two data sets can be grouped by rows or by columns.
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

int
correlation_tool (WorkbookControl *wbc, Sheet *sheet,
		  GSList *input, group_by_t group_by,
		  data_analysis_output_t *dao)
{
	GSList *input_range;
	GPtrArray *data = NULL;

	guint col, row;
	int error;
	gnum_float x;
	data_set_t *col_data, *row_data;
	GArray *clean_col_data, *clean_row_data;
	GSList *missing;

	input_range = input;
	prepare_input_range (&input_range, group_by);
	if (!check_input_range_list_homogeneity (input_range)) {
		range_list_destroy (input_range);
		return group_by + 1;
	}
	data = new_data_set_list (input_range, group_by,
				  FALSE, dao->labels_flag, sheet);
	prepare_output (wbc, dao, _("Correlations"));

	if (dao->type == RangeOutput) {
		set_cell_printf (dao, 0, 0,  _("Correlations"));
		set_italic (dao, 0, 0, 0, 0);
	}

	for (row = 0; row < data->len; row++) {
		row_data = g_ptr_array_index (data, row);
		set_cell_printf (dao, 0, row+1, row_data->label);
		set_italic (dao, 0, row+1, 0,  row+1);
		set_cell_printf (dao, row+1, 0, row_data->label);
		set_italic (dao, row+1, 0,  row+1, 0);
		for (col = 0; col < data->len; col++) {
		        if (row == col) {
			        set_cell_int (dao, col + 1, row + 1, 1);
				break;
			} else {
				if (row < col) {
					set_cell (dao, col + 1, row + 1, NULL);
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
						set_cell_na (dao, col + 1, row + 1);
					else
						set_cell_float (dao, col + 1, row + 1, x);
				}

			}
		}
	}

	autofit_columns (dao, 0, data->len);
	destroy_data_set_list (data);
	range_list_destroy (input_range);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (dao->sheet);

	return 0;
}


/************* Covariance Tool ********************************************
 *
 * The covariance tool calculates the covariance of two data sets.
 * The two data sets can be grouped by rows or by columns.  The
 * results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

/* If columns_flag is set, the data entries are grouped by columns
 * otherwise by rows.
 */
int
covariance_tool (WorkbookControl *wbc, Sheet *sheet,
		 GSList *input, group_by_t group_by,
		 data_analysis_output_t *dao)
{
	GSList *input_range = input;
	GPtrArray *data = NULL;

	guint col, row;
	int error;
	gnum_float x;
	data_set_t *col_data, *row_data;
	GArray *clean_col_data, *clean_row_data;
	GSList *missing;

	prepare_input_range (&input_range, group_by);
	if (!check_input_range_list_homogeneity (input_range)) {
		range_list_destroy (input_range);
		return group_by + 1;
	}
	data = new_data_set_list (input_range, group_by,
				  FALSE, dao->labels_flag, sheet);
	prepare_output (wbc, dao, _("Covariances"));

	if (dao->type == RangeOutput) {
		set_cell_printf (dao, 0, 0,  _("Covariances"));
		set_italic (dao, 0, 0, 0, 0);
	}

	for (row = 0; row < data->len; row++) {
		row_data = g_ptr_array_index (data, row);
		set_cell_printf (dao, 0, row+1, row_data->label);
		set_italic (dao, 0, row+1, 0,  row+1);
		set_cell_printf (dao, row+1, 0, row_data->label);
		set_italic (dao, row+1, 0,  row+1, 0);
		for (col = 0; col < data->len; col++) {
		        if (row == col) {
			        set_cell_int (dao, col + 1, row + 1, 1);
				break;
			} else {
				if (row < col) {
					set_cell (dao, col + 1, row + 1, NULL);
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
						set_cell_na (dao, col + 1, row + 1);
					else
						set_cell_float (dao, col + 1, row + 1, x);
				}

			}
		}
	}

	autofit_columns (dao, 0, data->len);
	destroy_data_set_list (data);
	range_list_destroy (input_range);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (dao->sheet);

	return 0;
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
summary_statistics (WorkbookControl *wbc, GPtrArray *data,
		    data_analysis_output_t *dao, GArray *basic_stats)
{
	guint     col;

	prepare_output (wbc, dao, _("Summary Statistics"));

        set_cell (dao, 0, 0, NULL);

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

		set_cell_printf (dao, col + 1, 0, the_col->label);
		set_italic (dao, col+1, 0, col+1, 0);

	        /* Mean */
		set_cell_float_na (dao, col + 1, 1, info.mean, info.error_mean == 0);

		/* Standard Error */
		set_cell_float_na (dao, col + 1, 2, sqrt (info.var / info.len), 
				   info.error_var == 0);

		/* Standard Deviation */
		set_cell_float_na (dao, col + 1, 5, sqrt (info.var), info.error_var == 0);

		/* Sample Variance */
		set_cell_float_na (dao, col + 1, 6, info.var, info.error_var == 0);

		/* Median */
		error = range_median_inter (the_data, the_col_len, &x);
		set_cell_float_na (dao, col + 1, 3, x, error == 0);

		/* Mode */
		error = range_mode (the_data, the_col_len, &x);
		set_cell_float_na (dao, col + 1, 4, x, error == 0);

		/* Kurtosis */
		error = range_kurtosis_m3_est (the_data, the_col_len, &x);
		set_cell_float_na (dao, col + 1, 7, x, error == 0);

		/* Skewness */
		error = range_skew_est (the_data, the_col_len, &x);
		set_cell_float_na (dao, col + 1, 8, x, error == 0);

		/* Minimum */
		error = range_min (the_data, the_col_len, &xmin);
		set_cell_float_na (dao, col + 1, 10, xmin, error == 0);

		/* Maximum */
		error2 = range_max (the_data, the_col_len, &xmax);
		set_cell_float_na (dao, col + 1, 11, xmax, error2 == 0);

		/* Range */
		set_cell_float_na (dao, col + 1, 9, xmax - xmin,
				   error == 0 && error2 == 0);

		/* Sum */
		error = range_sum (the_data, the_col_len, &x);
		set_cell_float_na (dao, col + 1, 12, x, error == 0);

		/* Count */
		set_cell_int (dao, col + 1, 13, the_col_len);
	}
	autofit_columns (dao, 0, data->len);
}

static void
confidence_level (WorkbookControl *wbc, GPtrArray *data, gnum_float c_level,
		  data_analysis_output_t *dao, GArray *basic_stats)
{
        gnum_float x;
        guint col;
	char *buffer;
	desc_stats_t info;
	data_set_t *the_col;

	prepare_output (wbc, dao, _("Confidence Interval for the Mean"));
	buffer = g_strdup_printf (_("/%g%% CI for the Mean from"
				    "/to"), c_level * 100);
	set_cell_text_col (dao, 0, 1, buffer);
        g_free (buffer);

        set_cell (dao, 0, 0, NULL);

	for (col = 0; col < data->len; col++) {
		the_col = g_ptr_array_index (data, col);
		set_cell_printf (dao, col + 1, 0, the_col->label);
		set_italic (dao, col+1, 0, col+1, 0);

		if ((c_level < 1) && (c_level >= 0)) {
			info = g_array_index (basic_stats, desc_stats_t, col);
			if (info.error_mean == 0 && info.error_var == 0) {
				x = -qt ((1 - c_level) / 2, info.len - 1) * 
					sqrt (info.var / info.len);
				set_cell_float (dao, col + 1, 1, info.mean - x);
				set_cell_float (dao, col + 1, 2, info.mean + x);
				continue;
			}
		}
		set_cell_na (dao, col + 1, 1);
	}
	autofit_columns (dao, 0, data->len);
}


static void
kth_largest (WorkbookControl *wbc, GPtrArray *data, int k,
	     data_analysis_output_t *dao)
{
        gnum_float x;
        guint col;
	gint error;
	data_set_t *the_col;

	prepare_output (wbc, dao, _("Kth Largest"));
        set_cell_printf (dao, 0, 1, _("Largest (%d)"), k);

        set_cell (dao, 0, 0, NULL);

	for (col = 0; col < data->len; col++) {
		the_col = g_ptr_array_index (data, col);
		set_cell_printf (dao, col + 1, 0, the_col->label);
		set_italic (dao, col+1, 0, col+1, 0);
		error = range_min_k_nonconst ((gnum_float *)(the_col->data->data),
					      the_col->data->len, &x, the_col->data->len - k);
		set_cell_float_na (dao, col + 1, 1, x, error == 0);
	}
	autofit_columns (dao, 0, data->len);
}

static void
kth_smallest (WorkbookControl *wbc, GPtrArray  *data, int k,
	      data_analysis_output_t *dao)
{
        gnum_float x;
        guint col;
	gint error;
	data_set_t *the_col;

	prepare_output (wbc, dao, _("Kth Smallest"));
        set_cell_printf (dao, 0, 1, _("Smallest (%d)"), k);

        set_cell (dao, 0, 0, NULL);

	for (col = 0; col < data->len; col++) {
		the_col = g_ptr_array_index (data, col);
		set_cell_printf (dao,  col + 1, 0, the_col->label);
		set_italic (dao, col+1, 0, col+1, 0);
		error = range_min_k_nonconst ((gnum_float *)(the_col->data->data),
					      the_col->data->len, &x, k - 1);
		set_cell_float_na (dao, col + 1, 1, x, error == 0);
	}
	autofit_columns (dao, 0, data->len);
}

/* Descriptive Statistics
 */
int
descriptive_stat_tool (WorkbookControl *wbc, Sheet *sheet,
		       GSList *input, group_by_t group_by,
		       descriptive_stat_tool_t *ds,
		       data_analysis_output_t *dao)
{
	GSList *input_range = input;
	GPtrArray *data = NULL;

	GArray        *basic_stats = NULL;
	data_set_t        *the_col;
        desc_stats_t  info;
	guint         col;
	const gnum_float *the_data;

	prepare_input_range (&input_range, group_by);
	data = new_data_set_list (input_range, group_by,
				  TRUE, dao->labels_flag, sheet);

	if (ds->summary_statistics || ds->confidence_level) {
		basic_stats = g_array_new (FALSE, FALSE, sizeof (desc_stats_t));
		for (col = 0; col < data->len; col++) {
			the_col = g_ptr_array_index (data, col);
			the_data = (gnum_float *)the_col->data->data;
                        info.len = the_col->data->len;
			info.error_mean = range_average (the_data, info.len, &(info.mean));
			info.error_var = range_var_est (the_data, info.len, &(info.var));
			g_array_append_val (basic_stats, info);
		}
	}

        if (ds->summary_statistics) {
                summary_statistics (wbc, data, dao, basic_stats);
		if (dao->type == RangeOutput) {
		        dao->start_row += 16;
			dao->rows -= 16;
			if (dao->rows < 1)
				return 0;
		}
	}
        if (ds->confidence_level) {
                confidence_level (wbc, data, ds->c_level,  dao, basic_stats);
		if (dao->type == RangeOutput) {
		        dao->start_row += 4;
			dao->rows -= 4;
			if (dao->rows < 1)
				return 0;
		}
	}
        if (ds->kth_largest) {
                kth_largest (wbc, data, ds->k_largest, dao);
		if (dao->type == RangeOutput) {
		        dao->start_row += 4;
			dao->rows -= 4;
			if (dao->rows < 1)
				return 0;
		}
	}
        if (ds->kth_smallest)
                kth_smallest (wbc, data, ds->k_smallest, dao);

	destroy_data_set_list (data);
	range_list_destroy (input_range);
	if (basic_stats != NULL)
		g_array_free (basic_stats, TRUE);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (dao->sheet);

	return 0;
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


int
sampling_tool (WorkbookControl *wbc, Sheet *sheet,
	       GSList *input, group_by_t group_by,
	       gboolean periodic_flag, guint size, guint number,
	       data_analysis_output_t *dao)
{
	GSList *input_range = input;
	GPtrArray *data = NULL;

	guint i, j, data_len;
	guint n_sample;
	guint n_data;
	gnum_float x;

	prepare_input_range (&input_range, group_by);
	data = new_data_set_list (input_range, group_by,
				  TRUE, dao->labels_flag, sheet);

	prepare_output (wbc, dao, _("Sample"));

	for (n_data = 0; n_data < data->len; n_data++) {
		for (n_sample = 0; n_sample < number; n_sample++) {
			GArray * sample = g_array_new (FALSE, FALSE,
						       sizeof (gnum_float));
			GArray * this_data = g_array_new (FALSE, FALSE,
							  sizeof (gnum_float));
			data_set_t * this_data_set;

			this_data_set = g_ptr_array_index (data, n_data);
			data_len = this_data_set->data->len;

			set_cell_printf (dao, 0, 0, this_data_set->label);
			set_italic (dao, 0, 0, 0, 0);
			dao->start_row++;

			g_array_set_size (this_data, data_len);
			g_memmove (this_data->data, this_data_set->data->data,
				   sizeof (gnum_float) * data_len);

			if (periodic_flag) {
				if ((size < 0) || (size > data_len))
					return 1;
				for (i = size - 1; i < data_len; i += size) {
					x = g_array_index (this_data, gnum_float, i);
					g_array_append_val (sample, x);
				}
				write_data (wbc, dao, sample);
			} else {
				for (i = 0; i < size; i++) {
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

				write_data (wbc, dao, sample);
				for (j = i; j < size; j++)
					set_cell_na (dao, 0, j);
			}

			autofit_columns (dao, 0, 0);
			g_array_free (this_data, TRUE);
			g_array_free (sample, TRUE);
      			dao->start_col++;
			dao->start_row--;
			if (dao->type == RangeOutput) {
				dao->start_col++;
				dao->cols--;
				if (dao->cols <= 0)
					break;
			}
		}
		if ((dao->type == RangeOutput) && (dao->cols <= 0))
			break;
	}

	destroy_data_set_list (data);
	range_list_destroy (input_range);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
}


/************* z-Test: Two Sample for Means ******************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/


int
ztest_tool (WorkbookControl *wbc, Sheet *sheet, 
	    Value *input_range_1, Value *input_range_2, 
	    gnum_float mean_diff, gnum_float known_var_1,
	    gnum_float known_var_2, gnum_float alpha,
	    data_analysis_output_t *dao)
{
	data_set_t *variable_1;
	data_set_t *variable_2;
	gboolean no_error;
	gnum_float mean_1 = 0, mean_2 = 0, z = 0, p = 0;
	gint mean_error_1 = 0, mean_error_2 = 0;

	variable_1 = new_data_set (input_range_1, TRUE, dao->labels_flag,
				   _("Variable %i"), 1, sheet);
	variable_2 = new_data_set (input_range_2, TRUE, dao->labels_flag,
				   _("Variable %i"), 2, sheet);

	prepare_output (wbc, dao, _("z-Test"));

        set_cell (dao, 0, 0, "");
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
		z = (mean_1 - mean_2 - mean_diff) /
			sqrt (known_var_1 / variable_1->data->len  + known_var_2 / 
			      variable_2->data->len);
		p = 1 - pnorm (fabs (z), 0, 1);
	}

	/* Labels */
	set_cell_printf (dao, 1, 0, variable_1->label);
	set_cell_printf (dao, 2, 0, variable_2->label);

	/* Mean */
	set_cell_float_na (dao, 1, 1, mean_1, mean_error_1 == 0);
	set_cell_float_na (dao, 2, 1, mean_2, mean_error_2 == 0);

	/* Known Variance */
	set_cell_float (dao, 1, 2, known_var_1);
	set_cell_float (dao, 2, 2, known_var_2);

	/* Observations */
	set_cell_int (dao, 1, 3, variable_1->data->len);
	set_cell_int (dao, 2, 3, variable_2->data->len);

	/* Hypothesized Mean Difference */
	set_cell_float (dao, 1, 4, mean_diff);

	/* Observed Mean Difference */
	set_cell_float_na (dao, 1, 5, mean_1 - mean_2, no_error);

	/* z */
	set_cell_float_na (dao, 1, 6, z, no_error);

	/* P (Z<=z) one-tail */
	set_cell_float_na (dao, 1, 7, p, no_error);

	/* z Critical one-tail */
	set_cell_float (dao, 1, 8, qnorm (1.0 - alpha, 0, 1));

	/* P (Z<=z) two-tail */
	set_cell_float_na (dao, 1, 9, 2 * p, no_error);

	/* z Critical two-tail */
	set_cell_float (dao, 1, 10, qnorm (1.0 - alpha / 2, 0, 1));

	set_italic (dao, 0, 0, 0, 10);
	set_italic (dao, 0, 0, 2, 0);

	autofit_columns (dao, 0, 2);

	value_release (input_range_1);
	value_release (input_range_2);

	destroy_data_set (variable_1);
	destroy_data_set (variable_2);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

        return 0;
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
int
ttest_paired_tool (WorkbookControl *wbc, Sheet *sheet, 
		   Value *input_range_1, Value *input_range_2, 
		   gnum_float mean_diff_hypo, gnum_float alpha,
		   data_analysis_output_t *dao)
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

	variable_1 = new_data_set (input_range_1, FALSE, dao->labels_flag,
				   _("Variable %i"), 1, sheet);
	variable_2 = new_data_set (input_range_2, FALSE, dao->labels_flag,
				   _("Variable %i"), 2, sheet);

	if (variable_1->data->len != variable_2->data->len) {
		value_release (input_range_1);
		value_release (input_range_2);
		destroy_data_set (variable_1);
		destroy_data_set (variable_2);
	        return 1;
	}

	missing = union_of_int_sets (variable_1->missing, variable_2->missing);
	cleaned_variable_1 = strip_missing (variable_1->data, missing);
	cleaned_variable_2 = strip_missing (variable_2->data, missing);
	
	prepare_output (wbc, dao, _("t-Test"));

        set_cell (dao, 0, 0, "");
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
					 difference->len, &mean_diff);

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
		t = (mean_diff - mean_diff_hypo)/sqrt (var_diff/difference->len);
		p = 1.0 - pt (fabs (t), df);
	}



	/* Labels */
	set_cell_printf (dao, 1, 0, variable_1->label);
	set_cell_printf (dao, 2, 0, variable_2->label);

	/* Mean */
	set_cell_float_na (dao, 1, 1, mean_1, mean_error_1 == 0);
	set_cell_float_na (dao, 2, 1, mean_2, mean_error_2 == 0);

	/* Variance */
	set_cell_float_na (dao, 1, 2, var_1, (mean_error_1 == 0) && (var_error_1 == 0));
	set_cell_float_na (dao, 2, 2, var_2, (mean_error_2 == 0) && (var_error_2 == 0));

	/* Observations */
	set_cell_int (dao, 1, 3, cleaned_variable_1->len);
	set_cell_int (dao, 2, 3, cleaned_variable_2->len);

	/* Pearson Correlation */
	error =  range_correl_pop
		((gnum_float *)(cleaned_variable_1->data),
		 (gnum_float *)(cleaned_variable_2->data),
		 cleaned_variable_1->len, &pearson);
	set_cell_float_na (dao, 1, 4, pearson, error == 0);

	/* Hypothesized Mean Difference */
	set_cell_float (dao, 1, 5, mean_diff_hypo);

	/* df */
	set_cell_float (dao, 1, 6, df);

	/* t */
  	set_cell_float_na (dao, 1, 7, t, var_diff_error == 0);

	/* P (T<=t) one-tail */
	set_cell_float_na (dao, 1, 8, p, var_diff_error == 0);

	/* t Critical one-tail */
	set_cell_float (dao, 1, 9, qt (1 - alpha, df));

	/* P (T<=t) two-tail */
	set_cell_float_na (dao, 1, 10, 2 * p, var_diff_error == 0);

	/* t Critical two-tail */
	set_cell_float (dao, 1, 11, qt (1.0 - alpha / 2, df));

	set_italic (dao, 0, 0, 0, 11);
	set_italic (dao, 0, 0, 2, 0);

	autofit_columns (dao, 0, 2);

	if (cleaned_variable_1 != variable_1->data)
		g_array_free (cleaned_variable_1, TRUE);
	if (cleaned_variable_2 != variable_2->data)
		g_array_free (cleaned_variable_2, TRUE);

	g_array_free (difference, TRUE);

	value_release (input_range_1);
	value_release (input_range_2);

	destroy_data_set (variable_1);
	destroy_data_set (variable_2);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
}


/* t-Test: Two-Sample Assuming Equal Variances.
 */
int
ttest_eq_var_tool (WorkbookControl *wbc, Sheet *sheet, 
		   Value *input_range_1, Value *input_range_2, 
		   gnum_float mean_diff, gnum_float alpha,
		   data_analysis_output_t *dao)
{
	data_set_t *variable_1;
	data_set_t *variable_2;
	gboolean no_error;
	gnum_float mean_1 = 0, mean_2 = 0, var_1 = 0, var_2 = 0;
	gnum_float t = 0, p = 0, var = 0;
	gint df;
	gint mean_error_1 = 0, mean_error_2 = 0, var_error_1 = 0, var_error_2 = 0;

	variable_1 = new_data_set (input_range_1, TRUE, dao->labels_flag,
				   _("Variable %i"), 1, sheet);
	variable_2 = new_data_set (input_range_2, TRUE, dao->labels_flag,
				   _("Variable %i"), 2, sheet);

	prepare_output (wbc, dao, _("t-Test"));

        set_cell (dao, 0, 0, "");
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
			t = (mean_1 - mean_2 - mean_diff) /
				sqrt (var / variable_1->data->len + var / variable_2->data->len);
			p = 1.0 - pt (fabs (t), df);
		}
	}

	/* Labels */
	set_cell_printf (dao, 1, 0, variable_1->label);
	set_cell_printf (dao, 2, 0, variable_2->label);

	/* Mean */
	set_cell_float_na (dao, 1, 1, mean_1, mean_error_1 == 0);
	set_cell_float_na (dao, 2, 1, mean_2, mean_error_2 == 0);

	/* Variance */
	set_cell_float_na (dao, 1, 2, var_1, (mean_error_1 == 0) && (var_error_1 == 0));
	set_cell_float_na (dao, 2, 2, var_2, (mean_error_2 == 0) && (var_error_2 == 0));

	/* Observations */
	set_cell_int (dao, 1, 3, variable_1->data->len);
	set_cell_int (dao, 2, 3, variable_2->data->len);

	/* Pooled Variance */
	set_cell_float_na (dao, 1, 4, var, no_error);

	/* Hypothesized Mean Difference */
	set_cell_float (dao, 1, 5, mean_diff);

	/* Observed Mean Difference */
	set_cell_float_na (dao, 1, 6, mean_1 - mean_2, 
			   (mean_error_1 == 0) && (mean_error_2 == 0));

	/* df */
	set_cell_float (dao, 1, 7, df);

	/* t */
	set_cell_float_na (dao, 1, 8, t, no_error && (var != 0));

	/* P (T<=t) one-tail */
	set_cell_float_na (dao, 1, 9, p, no_error && (var != 0));

	/* t Critical one-tail */
	set_cell_float (dao, 1, 10, qt (1.0 - alpha, df));

	/* P (T<=t) two-tail */
	set_cell_float_na (dao, 1, 11, 2 * p, no_error && (var != 0));

	/* t Critical two-tail */
	set_cell_float (dao, 1, 12, qt (1.0 - alpha / 2, df));

	set_italic (dao, 0, 0, 0, 12);
	set_italic (dao, 0, 0, 2, 0);

	autofit_columns (dao, 0, 2);

	value_release (input_range_1);
	value_release (input_range_2);

	destroy_data_set (variable_1);
	destroy_data_set (variable_2);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
}


/* t-Test: Two-Sample Assuming Unequal Variances.
 */
int
ttest_neq_var_tool (WorkbookControl *wbc, Sheet *sheet, 
		    Value *input_range_1, Value *input_range_2, 
		    gnum_float mean_diff, gnum_float alpha,
		    data_analysis_output_t *dao)
{
	data_set_t *variable_1;
	data_set_t *variable_2;
	gboolean no_error;
	gnum_float mean_1 = 0, mean_2 = 0, var_1 = 0, var_2 = 0;
	gnum_float t = 0, p = 0, c = 0;
	gnum_float df = 0;
	gint mean_error_1 = 0, mean_error_2 = 0, var_error_1 = 0, var_error_2 = 0;

	variable_1 = new_data_set (input_range_1, TRUE, dao->labels_flag,
				   _("Variable %i"), 1, sheet);
	variable_2 = new_data_set (input_range_2, TRUE, dao->labels_flag,
				   _("Variable %i"), 2, sheet);

	prepare_output (wbc, dao, _("t-Test"));

        set_cell (dao, 0, 0, "");
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
		
		t =  (mean_1 - mean_2 - mean_diff) /
			sqrt (var_1 / variable_1->data->len + var_2 / variable_2->data->len);
		p = 1.0 - pt (fabs (t), df);
	}

	/* Labels */
	set_cell_printf (dao, 1, 0, variable_1->label);
	set_cell_printf (dao, 2, 0, variable_2->label);

	/* Mean */
	set_cell_float_na (dao, 1, 1, mean_1, mean_error_1 == 0);
	set_cell_float_na (dao, 2, 1, mean_2, mean_error_2 == 0);

	/* Variance */
	set_cell_float_na (dao, 1, 2, var_1, (mean_error_1 == 0) && (var_error_1 == 0));
	set_cell_float_na (dao, 2, 2, var_2, (mean_error_2 == 0) && (var_error_2 == 0));

	/* Observations */
	set_cell_int (dao, 1, 3, variable_1->data->len);
	set_cell_int (dao, 2, 3, variable_2->data->len);

	/* Hypothesized Mean Difference */
	set_cell_float (dao, 1, 4, mean_diff);

	/* Observed Mean Difference */
	set_cell_float_na (dao, 1, 5, mean_1 - mean_2, 
			   (mean_error_1 == 0) && (mean_error_2 == 0));

	/* df */
	set_cell_float_na (dao, 1, 6, df, no_error);

	/* t */
	set_cell_float_na (dao, 1, 7, t, no_error);

	/* P (T<=t) one-tail */
	set_cell_float_na (dao, 1, 8, p, no_error);

	/* t Critical one-tail */
	set_cell_float (dao, 1, 9, qt (1.0 - alpha, df));

	/* P (T<=t) two-tail */
	set_cell_float_na (dao, 1, 10, 2 * p, no_error);

	/* t Critical two-tail */
	set_cell_float (dao, 1, 11, qt (1.0 - alpha / 2, df));

	set_italic (dao, 0, 0, 0, 11);
	set_italic (dao, 0, 0, 2, 0);

	autofit_columns (dao, 0, 2);

	value_release (input_range_1);
	value_release (input_range_2);

	destroy_data_set (variable_1);
	destroy_data_set (variable_2);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
}


/************* F-Test Tool *********************************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/


/* F-Test: Two-Sample for Variances
 */
int
ftest_tool (WorkbookControl *wbc, Sheet *sheet, 
	    Value *input_range_1, Value *input_range_2, 
	    gnum_float alpha,
	    data_analysis_output_t *dao)
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

	variable_1 = new_data_set (input_range_1, TRUE, dao->labels_flag,
			      _("Variable %i"), 1, sheet);
	variable_2 = new_data_set (input_range_2, TRUE, dao->labels_flag,
			      _("Variable %i"), 2, sheet);

	if ((variable_1->data->len == 0) ||  (variable_2->data->len == 0))
	{
		result = ((variable_1->data->len == 0) ?  1 : 2);
	} else {

		prepare_output (wbc, dao, _("F-Test"));
		
		set_cell (dao, 0, 0, "");
		set_cell (dao, 1, 0, variable_1->label);
		set_cell (dao, 2, 0, variable_2->label);
		
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
			p_right_tail = 1.0 - pf (f, df_1, df_2);
			q_right_tail = qf (1.0 - alpha, df_1, df_2);
			p_left_tail =  pf (f, df_1, df_2);
			q_left_tail =  qf ( alpha, df_1, df_2);
			if (p_right_tail < 0.5)
				p_2_tail =  2 * p_right_tail;
			else
				p_2_tail =  2 * p_left_tail;
		
			q_2_tail_left =  qf (alpha/2.0, df_1, df_2);
			q_2_tail_right  =  qf (1.0 - alpha/2.0, df_1, df_2);
		}

		/* Mean */
		set_cell_float_na (dao, 1, 1, mean_1, mean_error_1 == 0);
		set_cell_float_na (dao, 2, 1, mean_2, mean_error_2 == 0);
		
		/* Variance */
		set_cell_float_na (dao, 1, 2, var_1, (mean_error_1 == 0) && (var_error_1 == 0));
		set_cell_float_na (dao, 2, 2, var_2, (mean_error_2 == 0) && (var_error_2 == 0));
		
		/* Observations */
		set_cell_int (dao, 1, 3, variable_1->data->len);
		set_cell_int (dao, 2, 3, variable_2->data->len);
		
		/* df */
		set_cell_int (dao, 1, 4, df_1);
		set_cell_int (dao, 2, 4, df_2);
		
		/* F */
		set_cell_float_na (dao, 1, 5, f, !calc_error);
		
		/* P (F<=f) right-tail */
		set_cell_float_na (dao, 1, 6, p_right_tail, !calc_error);
		
		/* F Critical right-tail */
		set_cell_float_na (dao, 1, 7, q_right_tail, !calc_error);

		/* P (F<=f) left-tail */
		set_cell_float_na (dao, 1, 8, p_left_tail, !calc_error);
		
		/* F Critical left-tail */
		set_cell_float_na (dao, 1, 9, q_left_tail, !calc_error);

		/* P (F<=f) two-tail */
		set_cell_float_na (dao, 1, 10, p_2_tail, !calc_error);
		
		/* F Critical two-tail */
		set_cell_float_na (dao, 1, 11, q_2_tail_left, !calc_error);
		set_cell_float_na (dao, 2, 11, q_2_tail_right, !calc_error);
		
		set_italic (dao, 0, 0, 0, 11);
		set_italic (dao, 0, 0, 2, 0);
		
		autofit_columns (dao, 0, 2);
		
		sheet_set_dirty (dao->sheet, TRUE);
		sheet_update (sheet);
	}
	
	value_release (input_range_1);
	value_release (input_range_2);
	destroy_data_set (variable_1);
	destroy_data_set (variable_2);
	
	return result;
}


/************* Random Number Generation Tool ******************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

int
random_tool (WorkbookControl *wbc, Sheet *sheet, int vars, int count,
	     random_distribution_t distribution,
	     random_tool_t *param, data_analysis_output_t *dao)
{
	if (distribution != DiscreteDistribution)
		prepare_output (wbc, dao, _("Random"));

	switch (distribution) {
	case DiscreteDistribution: {
		Value *range = param->discrete.range;
	        int n = range->v_range.cell.b.row - range->v_range.cell.a.row + 1;
	        gnum_float *prob = g_new (gnum_float, n);
		gnum_float *cumul_p = g_new (gnum_float, n);
		Value **values = g_new0 (Value *, n);
                gnum_float cumprob = 0;
		int j = 0;
		int i;
		int err = 0;

	        for (i = range->v_range.cell.a.row;
		     i <= range->v_range.cell.b.row;
		     i++, j++) {
			Value *v;
			gnum_float thisprob;
		        Cell *cell = sheet_cell_get (range->v_range.cell.a.sheet,
						     range->v_range.cell.a.col + 1, i);

			if (cell == NULL || 
			    (v = cell->value) == NULL ||
			    !VALUE_IS_NUMBER (v)) {
				err = 1;
				goto random_tool_discrete_out;				
			}
			if ((thisprob = value_get_as_float (v)) < 0) {
				err = 3;
				goto random_tool_discrete_out;				
			}

			prob[j] = thisprob;
			cumprob += thisprob;
			cumul_p[j] = cumprob;

		        cell = sheet_cell_get (range->v_range.cell.a.sheet,
					       range->v_range.cell.a.col, i);

			if (cell == NULL || cell->value == NULL) {
				err = 4;
				goto random_tool_discrete_out;
			}

			values[j] = value_duplicate (cell->value);
		}

		if (cumprob == 0) {
			err = 2;
			goto random_tool_discrete_out;
		}
		/* Rescale... */
		for (i = 0; i < n; i++) {
			prob[i] /= cumprob;
			cumul_p[i] /= cumprob;
		}

		prepare_output (wbc, dao, _("Random"));
	        for (i = 0; i < vars; i++) {
			int k;
		        for (k = 0; k < count; k++) {
				int j;
			        gnum_float x = random_01 ();

				for (j = 0; cumul_p[j] < x; j++)
				        ;

				set_cell_value (dao, i, k, value_duplicate (values[j]));
			}
		}

	random_tool_discrete_out:
		for (i = 0; i < n; i++)
			if (values[i])
				value_release (values[i]);
		g_free (prob);
		g_free (cumul_p);
		g_free (values);
		value_release (range);

		if (err)
			return err;

	        break;
	}

	case NormalDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = param->normal.stdev * random_normal () + param->normal.mean;
				set_cell_float (dao, i, n, v);
			}
		}
	        break;
	}

	case BernoulliDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
			        gnum_float tmp = random_bernoulli (param->bernoulli.p);
				set_cell_int (dao, i, n, (int)tmp);
			}
		}
	        break;
	}

	case UniformDistribution: {
		int i, n;
		gnum_float range = param->uniform.upper_limit - param->uniform.lower_limit;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
				v = range * random_01 () + param->uniform.lower_limit;
				set_cell_float (dao, i, n, v);
			}
		}
		break;
	}

	case PoissonDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_poisson (param->poisson.lambda);
				set_cell_float (dao, i, n, v);
			}
		}
	        break;
	}

	case ExponentialDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_exponential (param->exponential.b);
				set_cell_float (dao, i, n, v);
			}
		}
	        break;
	}

	case BinomialDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_binomial (param->binomial.p,
						     param->binomial.trials);
				set_cell_float (dao, i, n, v);
			}
		}
	        break;
	}

	case NegativeBinomialDistribution: {
		int i, n;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_negbinom (param->negbinom.p,
						     param->negbinom.f);
				set_cell_float (dao, i, n, v);
			}
		}
	        break;
	}

	default:
	        printf (_("Not implemented yet.\n"));
		break;
	}

	autofit_columns (dao, 0, vars - 1);
	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (dao->sheet);

	return 0;
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

int
regression_tool (WorkbookControl *wbc, Sheet *sheet,
		 GSList *x_input, Value *y_input,
		 group_by_t group_by, gnum_float alpha,
		 data_analysis_output_t *dao, int intercept)
{
	GSList       *x_input_range = NULL;
	GSList       *missing       = NULL;
	GPtrArray    *x_data        = NULL;
	data_set_t   *y_data        = NULL;
	GArray       *cleaned       = NULL;
	char         *text          = NULL;
	regression_stat_t   extra_stat;
	gnum_float   mean_y;
	gnum_float   ss_yy;
	gnum_float   r;
	gnum_float   *res,  **xss;
	guint        i;
	guint        xdim           = 0;
	int          err            = 0; 
	int          cor_err        = 0;
	int          av_err         = 0;
	int          sumsq_err      = 0;

/* read the data and check for consistency */
	x_input_range = x_input;
	prepare_input_range (&x_input_range, group_by);
	if (!check_input_range_list_homogeneity (x_input_range)) {
		range_list_destroy (x_input_range);
		value_release (y_input);
		return 3;
	}
	x_data = new_data_set_list (x_input_range, group_by,
				  FALSE, dao->labels_flag, sheet);
	xdim = x_data->len;
	y_data = new_data_set (y_input, FALSE, dao->labels_flag,
			       _("Y Variable"), 0, sheet);

	if (y_data->data->len != ((data_set_t *)g_ptr_array_index (x_data, 0))->data->len) {
		destroy_data_set (y_data);
		destroy_data_set_list (x_data);
		range_list_destroy (x_input_range);
		value_release (y_input);
		return 3;		
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

	err = linear_regression (xss, xdim, (gnum_float *)(y_data->data->data), 
				 y_data->data->len, intercept, res, &extra_stat);

	if (err) {
		destroy_data_set (y_data);
		destroy_data_set_list (x_data);
		range_list_destroy (x_input_range);
		value_release (y_input);
		g_free (xss);
		g_free (res);
		return err;
	}

	prepare_output (wbc, dao, _("Regression"));

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
		set_cell (dao, 0, 17 + i, ((data_set_t *)g_ptr_array_index (x_data, i))->label);
	set_italic (dao, 0, 0, 0, 16 + xdim);

        set_cell_text_row (dao, 1, 10, _("/df"
					 "/SS"
					 "/MS"
					 "/F"
					 "/Significance of F"));
	set_italic (dao, 1, 10, 5, 10);
	
	text = g_strdup_printf (_("/Coefficients"
				  "/Standard Error"
				  "/t Stat"
				  "/P-value"
				  "/Lower %0.0f%%"
				  "/Upper %0.0f%%"),
				((1.0 - alpha) * 100),((1.0 - alpha) * 100));
        set_cell_text_row (dao, 1, 15, text);
	set_italic (dao, 1, 15, 6, 15);
	g_free (text);

	av_err = range_average ((gnum_float *)(y_data->data->data), y_data->data->len, &mean_y);
	sumsq_err = range_sumsq ((gnum_float *)(y_data->data->data), y_data->data->len, &ss_yy);
	ss_yy -=  y_data->data->len * mean_y * mean_y;

	if (xdim == 1)
		cor_err =  range_correl_pop (xss[0], (gnum_float *)(y_data->data->data),
					  y_data->data->len, &r);
	else r = sqrt (extra_stat.sqr_r);

	/* Multiple R */
	set_cell_float_na (dao, 1, 3, r, cor_err == 0);

	/* R Square */
	set_cell_float (dao, 1, 4, extra_stat.sqr_r);

	/* Adjusted R Square */
	set_cell_float (dao, 1, 5, extra_stat.adj_sqr_r);

	/* Standard Error */
	set_cell_float (dao, 1, 6, sqrt (extra_stat.var));

	/* Observations */
	set_cell_float (dao, 1, 7, y_data->data->len);

	/* Regression / df */
	set_cell_float (dao, 1, 11, xdim);

	/* Residual / df */
	set_cell_float (dao, 1, 12, y_data->data->len - intercept - xdim);

	/* Total / df */
	set_cell_float (dao, 1, 13, y_data->data->len - intercept);

	/* Residual / SS */
	set_cell_float (dao, 2, 12, extra_stat.ss_resid);

	/* Total / SS */
	set_cell_float_na (dao, 2, 13, ss_yy, (sumsq_err == 0) && (av_err == 0));

	/* Regression / SS */
	set_cell_float_na (dao, 2, 11, ss_yy - extra_stat.ss_resid, 
			   (sumsq_err == 0) && (av_err == 0));

	/* Regression / MS */
	set_cell_float_na (dao, 3, 11, (ss_yy - extra_stat.ss_resid) / xdim,
			   (sumsq_err == 0) && (av_err == 0));

	/* Residual / MS */
	set_cell_float (dao, 3, 12, extra_stat.ss_resid / (y_data->data->len - 1 - xdim));

	/* F */
	set_cell_float (dao, 4, 11, extra_stat.F);

	/* Significance of F */
	set_cell_float (dao, 5, 11, 1 - pf (extra_stat.F, xdim - intercept,
					    y_data->data->len - xdim));

	/* Intercept / Coefficient */
	set_cell_float (dao, 1, 16, res[0]);

	if (!intercept)
		for (i = 2; i <= 6; i++)
			set_cell_na (dao, i, 16);
	else {
		gnum_float t;

		t = qt (1 - alpha/2, y_data->data->len - xdim - 1);

		/* Intercept / Standard Error */
		set_cell_float (dao, 2, 16, extra_stat.se[0]);

		/* Intercept / t Stat */
		set_cell_float (dao, 3, 16, extra_stat.t[0]);

		/* Intercept / p values */
		set_cell_float (dao, 4, 16, 2.0 * (1.0 - pt (extra_stat.t[0],
							     y_data->data->len - xdim - 1)));

		/* Intercept / Lower 95% */
		set_cell_float (dao, 5, 16, res[0] - t * extra_stat.se[0]);

		/* Intercept / Upper 95% */
		set_cell_float (dao, 6, 16, res[0] + t * extra_stat.se[0]);
	}

	/* Slopes */
	for (i = 0; i < xdim; i++) {
		gnum_float t;

		/* Slopes / Coefficient */
		set_cell_float (dao, 1, 17 + i, res[i + 1]);

		/* Slopes / Standard Error */
		/*With no intercept se[0] is for the first slope variable; with
		  intercept, se[1] is the first slope se */
		set_cell_float (dao, 2, 17 + i, extra_stat.se[intercept + i]);

		/* Slopes / t Stat */
		set_cell_float (dao, 3, 17 + i, extra_stat.t[intercept + i]);

		/* Slopes / p values */
		set_cell_float (dao, 4, 17 + i,
				2.0 * (1.0 - pt (extra_stat.t[intercept + i],
						 y_data->data->len - xdim - intercept)));

		t = qt (1 - alpha/2, y_data->data->len - xdim - intercept);

		/* Slope / Lower 95% */
		set_cell_float (dao, 5, 17 + i,
				res[i + 1] - t * extra_stat.se[intercept + i]);

		/* Slope / Upper 95% */
		set_cell_float (dao, 6, 17 + i,
				res[i + 1] + t * extra_stat.se[intercept + i]);
	}

	autofit_columns (dao, 0, 6);
	destroy_data_set (y_data);
	destroy_data_set_list (x_data);
	range_list_destroy (x_input_range);
	value_release (y_input);
	g_free (xss);
	g_free (res);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (dao->sheet);

	return 0;
}


/************* Moving Average Tool *****************************************
 *
 * The moving average tool calculates moving averages of given data
 * set.  The results are given in a table which can be printed out in
 * a new sheet, in a new workbook, or simply into an existing sheet.
 *
 **/


int
average_tool (WorkbookControl *wbc, Sheet *sheet, 
	      GSList *input, group_by_t group_by,
	      int interval,
	      int std_error_flag, data_analysis_output_t *dao)
{
	GSList        *input_range;
	GPtrArray     *data;
	guint         dataset;
	gint          col = 0;
	gnum_float    *prev, *prev_av;

	input_range = input;
	prepare_input_range (&input_range, group_by);
	data = new_data_set_list (input_range, group_by,
				  TRUE, dao->labels_flag, sheet);

	prepare_output (wbc, dao, _("Moving Averages"));

	prev = g_new (gnum_float, interval);
	prev_av = g_new (gnum_float, interval);

	for (dataset = 0; dataset < data->len; dataset++) {
		data_set_t    *current;
		gnum_float    sum;
		gnum_float    std_err;
		gint         row;
		int           add_cursor, del_cursor;

		current = g_ptr_array_index (data, dataset);
		set_cell_printf (dao, col, 0, current->label);
		if (std_error_flag)
			set_cell_printf (dao, col + 1, 0, _("Standard Error"));

		add_cursor = del_cursor = 0;
		sum = 0;
		std_err = 0;
		
		for (row = 0; row < interval - 1; row++) {
			prev[add_cursor] = g_array_index 
				(current->data, gnum_float, row);
			sum += prev[add_cursor];
			++add_cursor;
			set_cell_na (dao, col, row + 1);
			if (std_error_flag)
				set_cell_na (dao, col + 1, row + 1);
		}
		for (row = interval - 1; row < (gint)current->data->len; row++) {
			prev[add_cursor] = g_array_index 
				(current->data, gnum_float, row);
			sum += prev[add_cursor];
			prev_av[add_cursor] = sum / interval;
			set_cell_float (dao, col, row + 1, prev_av[add_cursor]);
			sum -= prev[del_cursor];
			if (std_error_flag) {
				std_err += (prev[add_cursor] - prev_av[add_cursor]) *
					(prev[add_cursor] - prev_av[add_cursor]);
				if (row >= 2 * interval - 2) {
					set_cell_float (dao, col + 1, row + 1, 
							sqrt (std_err / interval));
					std_err -= (prev[del_cursor] - prev_av[del_cursor]) *
						(prev[del_cursor] - prev_av[del_cursor]);
				} else {
					set_cell_na (dao, col + 1, row + 1);
				}
			}
			if (++add_cursor == interval)
				add_cursor = 0;
			if (++del_cursor == interval)
				del_cursor = 0;
		}
		col++;
		if (std_error_flag)
			col++;
	}
	set_italic (dao, 0, 0, col - 1, 0);
	autofit_columns (dao, 0, col - 1);

	destroy_data_set_list (data);
	range_list_destroy (input_range);
	g_free (prev);
	g_free (prev_av);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
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
 **/

int
exp_smoothing_tool (WorkbookControl *wbc, Sheet *sheet,
		    GSList *input, group_by_t group_by,
		    gnum_float damp_fact, int std_error_flag,
		    data_analysis_output_t *dao)
{
	GSList        *input_range;
	GPtrArray     *data;
	guint           dataset;

	/* TODO: Standard error output */

	input_range = input;
	prepare_input_range (&input_range, group_by);
	data = new_data_set_list (input_range, group_by,
				  TRUE, dao->labels_flag, sheet);

	prepare_output (wbc, dao, _("Exponential Smoothing"));

	for (dataset = 0; dataset < data->len; dataset++) {
		data_set_t    *current;
		gnum_float    a, f;
		guint           row;

		current = g_ptr_array_index (data, dataset);
		set_cell_printf (dao, dataset, 0, current->label);
		a = f = 0;
		for (row = 0; row < current->data->len; row++) {
			if (row == 0)
				/* Cannot forecast for the first data element */
				
				set_cell_na (dao, dataset, row + 1);
			else if (row == 1) {
				/* The second forecast is always the first data element */
				set_cell_float (dao, dataset, row + 1, a);
				f = a;
			} else {
				/* F(t+1) = F(t) + (1 - damp_fact) * ( A(t) - F(t) ),
				 * where A(t) is the t'th data element.
				 */
				
				f = f + (1.0 - damp_fact) * (a - f);
				set_cell_float (dao, dataset, row + 1, f);
			}
			a = g_array_index (current->data, gnum_float, row);
		}
	}
	set_italic (dao, 0, 0, data->len - 1, 0);
	autofit_columns (dao, 0, data->len - 1);

	destroy_data_set_list (data);
	range_list_destroy (input_range);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
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

int
ranking_tool (WorkbookControl *wbc, Sheet *sheet, GSList *input,
	      group_by_t group_by, gboolean av_ties_flag, data_analysis_output_t *dao)
{
	GSList *input_range = input;
	GPtrArray *data = NULL;
	guint n_data;

	prepare_input_range (&input_range, group_by);
	data = new_data_set_list (input_range, group_by,
				  TRUE, dao->labels_flag, sheet);

	prepare_output (wbc, dao, _("Ranks"));

	for (n_data = 0; n_data < data->len; n_data++) {
	        rank_t *rank;
		guint i, j;
		data_set_t * this_data_set;

	        this_data_set = g_ptr_array_index (data, n_data);

		set_cell (dao, n_data * 4, 0, _("Point"));
		set_cell (dao, n_data * 4+1, 0, this_data_set->label);
		set_cell (dao, n_data * 4 + 2, 0, _("Rank"));
		set_cell (dao, n_data * 4 + 3, 0, _("Percent"));
		
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

		set_percent (dao, n_data * 4 + 3, 1,
			     n_data * 4 + 3, this_data_set->data->len);
		for (i = 0; i < this_data_set->data->len; i++) {
			/* Point number */
			set_cell_int (dao, n_data * 4 + 0, i + 1, rank[i].point);

			/* Value */
			set_cell_float (dao, n_data * 4 + 1, i + 1, rank[i].x);

			/* Rank */
			set_cell_float (dao, n_data * 4 + 2, i + 1, 
					rank[i].rank + 
					(av_ties_flag ? rank[i].same_rank_count/2. : 0));

			/* Percent */
			set_cell_float_na (dao, n_data * 4 + 3, i + 1,
					   1. - (rank[i].rank - 1.)/
						    (this_data_set->data->len - 1.), 
					   this_data_set->data->len != 0);
		}
		g_free (rank);
	}

	autofit_columns (dao, 0, data->len * 4 - 1);

	destroy_data_set_list (data);
	range_list_destroy (input_range);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
}


/************* Anova: Single Factor Tool **********************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

int
anova_single_factor_tool (WorkbookControl *wbc, Sheet *sheet, 
			  GSList *input, group_by_t group_by,
			  gnum_float alpha, data_analysis_output_t *dao)
{
	GSList *input_range;
	GPtrArray *data = NULL;
	guint index;
	gint error;
	gboolean A_is_valid = TRUE, T_is_valid = TRUE, CF_is_valid = TRUE;
	gint N = 0;
	gnum_float T = 0.0, A = 0.0, CF = 0.0;
	gnum_float ss_b, ss_w, ss_t;
	gnum_float ms_b, ms_w, f = 0.0, p = 0.0, f_c = 0.0;
	int        df_b, df_w, df_t;

	input_range = input;
	prepare_input_range (&input_range, group_by);
	data = new_data_set_list (input_range, group_by,
				  TRUE, dao->labels_flag, sheet);

	prepare_output (wbc, dao, _("Anova"));

	set_cell (dao, 0, 0, _("Anova: Single Factor"));
	set_cell (dao, 0, 2, _("SUMMARY"));
	set_cell_text_row (dao, 0, 3, _("/Groups"
					"/Count"
					"/Sum"
					"/Average"
					"/Variance"));
	set_italic (dao, 0, 0, 0, data->len + 11);
	set_italic (dao, 0, 3, 4, 3);

	dao->start_row += 4;
	dao->rows -= 4;
	if ((dao->type == RangeOutput) &&  (dao->rows <= 0)) 
		goto finish_anova_single_factor_tool;
	
	/* SUMMARY & ANOVA calculation*/
	for (index = 0; index < data->len; index++) {
		gnum_float x;
		data_set_t *current_data;
		gnum_float *the_data;
		
		current_data = g_ptr_array_index (data, index);
		the_data = (gnum_float *)current_data->data->data;
		
		/* Label */
		set_cell_printf (dao, 0, index, current_data->label);
		
		/* Count */
		set_cell_int (dao, 1, index, current_data->data->len);
		N += current_data->data->len;
		
		/* Sum */
		error = range_sum (the_data, 
				   current_data->data->len, &x);
		set_cell_float_na (dao, 2, index, x, error == 0);
		A += (x * x) / current_data->data->len;
		A_is_valid = A_is_valid && (error == 0) && (current_data->data->len > 0);
		CF += x;
		CF_is_valid = CF_is_valid && (error == 0);

		/* Average */
		error = range_average (the_data, 
				       current_data->data->len, &x);
		set_cell_float_na (dao, 3, index, x, error == 0);

		
		/* Variance */
		error = range_var_est (the_data, 
				       current_data->data->len, &x);
		set_cell_float_na (dao, 4, index, x, error == 0);

		/* Further Calculations */;
		error = range_sumsq (the_data, current_data->data->len, &x);
		T += x;
		T_is_valid = T_is_valid && (error == 0);
	}

	CF = (CF * CF)/N;

	dao->start_row += data->len + 2;
	dao->rows -= data->len + 2;
	if ((dao->type == RangeOutput) &&  (dao->rows <= 0)) 
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
	set_italic (dao, 1, 1, 6, 1);

	/* ANOVA */
	df_b = data->len - 1;
	df_w = N - data->len;
	df_t = N - 1;
	ss_b = A - CF;
	ss_w = T - A;
	ss_t = T - CF;
	ms_b = ss_b / df_b;
	ms_w = ss_w / df_w;
	if (A_is_valid && CF_is_valid && T_is_valid) {
		f    = ms_b / ms_w;
		p    = 1.0 - pf (f, df_b, df_w);
		f_c  = qf (1 - alpha, df_b, df_w);
	}

	set_cell_float_na (dao, 1, 2, ss_b, A_is_valid && CF_is_valid);
	set_cell_float_na (dao, 1, 3, ss_w, A_is_valid && T_is_valid);
	set_cell_float_na (dao, 1, 4, ss_t, T_is_valid && CF_is_valid);
	set_cell_int (dao, 2, 2, df_b);
	set_cell_int (dao, 2, 3, df_w);
	set_cell_int (dao, 2, 4, df_t);
	set_cell_float_na (dao, 3, 2, ms_b, A_is_valid && CF_is_valid);
	set_cell_float_na (dao, 3, 3, ms_w, A_is_valid && T_is_valid);
	set_cell_float_na (dao, 4, 2, f, A_is_valid && CF_is_valid && T_is_valid);
	set_cell_float_na (dao, 5, 2, p, A_is_valid && CF_is_valid && T_is_valid);
	set_cell_float_na (dao, 6, 2, f_c, A_is_valid && CF_is_valid && T_is_valid);

finish_anova_single_factor_tool:

	autofit_columns (dao, 0, 6);

	destroy_data_set_list (data);
	range_list_destroy (input_range);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

        return 0;
}


/************* Anova: Two-Factor Without Replication Tool ****************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

int
anova_two_factor_without_r_tool (WorkbookControl *wbc, Sheet *sheet, Value *input,
				 gnum_float alpha,
				 data_analysis_output_t *dao)
{
	GSList *row_input_range = NULL;
	GSList *col_input_range = NULL;
	GPtrArray *row_data = NULL;
	GPtrArray *col_data = NULL;

	int        i, n, error;
	int        cols;
	int        rows;
	gnum_float    cm,  sum = 0;
	gnum_float    ss_r = 0, ss_c = 0, ss_e = 0, ss_t = 0;
	gnum_float    ms_r, ms_c, ms_e, f1, f2, p1, p2, f1_c, f2_c;
	int        df_r, df_c, df_e, df_t;

	row_input_range = g_slist_prepend (NULL, value_duplicate (input));
	prepare_input_range (&row_input_range, GROUPED_BY_ROW);
	if (dao->labels_flag) {
		GSList *list = row_input_range;
		row_input_range = g_slist_remove_link (row_input_range, list);
		range_list_destroy (list);
	}
	col_input_range = g_slist_prepend (NULL, input);
	prepare_input_range (&col_input_range, GROUPED_BY_COL);
	if (dao->labels_flag) {
		GSList *list = col_input_range;
		col_input_range = g_slist_remove_link (col_input_range, list);
		range_list_destroy (list);
	}
	if (col_input_range == NULL || row_input_range == NULL || 
	    col_input_range->next == NULL || row_input_range->next == NULL) {
		range_list_destroy (col_input_range);
		range_list_destroy (row_input_range);
		return 3; 
	}

	row_data = new_data_set_list (row_input_range, GROUPED_BY_ROW,
				  FALSE, dao->labels_flag, sheet);
	col_data = new_data_set_list (col_input_range, GROUPED_BY_COL,
				  FALSE, dao->labels_flag, sheet);
	if (check_data_for_missing (row_data) || 
	    check_data_for_missing (col_data)) {
		range_list_destroy (col_input_range);
		range_list_destroy (row_input_range);
		destroy_data_set_list (row_data);
		destroy_data_set_list (col_data);
		return 2; 
	}

	prepare_output (wbc, dao, _("Anova"));

	cols = col_data->len;
	rows = row_data->len;
	n = rows * cols;

	set_cell (dao, 0, 0, _("Anova: Two-Factor Without Replication"));
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

		set_cell (dao, 0, i + 3, data_set->label);
		set_cell_int (dao, 1, i + 3, data_set->data->len);
		error = range_sum (the_data, data_set->data->len, &v);
		sum +=  v;
		ss_r += v * v/ data_set->data->len;
		set_cell_float_na (dao, 2, i + 3, v, error == 0);
		set_cell_float_na (dao, 3, i + 3,  v / data_set->data->len, 
				   error == 0 && data_set->data->len != 0);
		error = range_var_est (the_data, data_set->data->len , &v);
		set_cell_float_na (dao, 4, i + 3, v, error == 0);
		
		error = range_sumsq (the_data, data_set->data->len, &v);
		ss_t += v;
	}
	
	for (i = 0; i < cols; i++) {
	        gnum_float v;
		data_set_t *data_set;
		gnum_float *the_data;

		data_set = (data_set_t *)g_ptr_array_index (col_data, i);
		the_data = (gnum_float *)data_set->data->data;

		set_cell (dao, 0, i + 4 + rows, data_set->label);
		set_cell_int (dao, 1, i + 4 + rows, data_set->data->len);
		error = range_sum (the_data, data_set->data->len, &v);
		ss_c += v * v/ data_set->data->len;
		set_cell_float_na (dao, 2, i + 4 + rows, v, error == 0);
		set_cell_float_na (dao, 3, i + 4 + rows, v / data_set->data->len, 
				   error == 0 && data_set->data->len != 0);
		error = range_var_est (the_data, data_set->data->len , &v);
		set_cell_float_na (dao, 4, i + 4 + rows, v, error == 0);
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
	p1   = 1.0 - pf (f1, df_r, df_e);
	p2   = 1.0 - pf (f2, df_c, df_e);
	f1_c = qf (1 - alpha, df_r, df_e);
	f2_c = qf (1 - alpha, df_c, df_e);

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

	set_cell_float (dao, 1, 8 + rows + cols, ss_r);
	set_cell_float (dao, 1, 9 + rows + cols, ss_c);
	set_cell_float (dao, 1, 10 + rows + cols, ss_e);
	set_cell_float (dao, 1, 11 + rows + cols, ss_t);
	set_cell_int (dao, 2, 8 + rows + cols, df_r);
	set_cell_int (dao, 2, 9 + rows + cols, df_c);
	set_cell_int (dao, 2, 10 + rows + cols, df_e);
	set_cell_int (dao, 2, 11 + rows + cols, df_t);
	set_cell_float (dao, 3, 8 + rows + cols, ms_r);
	set_cell_float (dao, 3, 9 + rows + cols, ms_c);
	set_cell_float (dao, 3, 10 + rows + cols, ms_e);
	set_cell_float_na (dao, 4, 8 + rows + cols, f1, ms_e != 0);
	set_cell_float_na (dao, 4, 9 + rows + cols, f2, ms_e != 0);
	set_cell_float_na (dao, 5, 8 + rows + cols, p1, ms_e != 0);
	set_cell_float_na (dao, 5, 9 + rows + cols, p2, ms_e != 0);
	set_cell_float (dao, 6, 8 + rows + cols, f1_c);
	set_cell_float (dao, 6, 9 + rows + cols, f2_c);

	set_italic (dao, 1, 2, 4, 2);
	set_italic (dao, 1, 7 + rows + cols, 6, 7 + rows + cols);
	set_italic (dao, 0, 0, 0, 11 + rows + cols);

	autofit_columns (dao, 0, 6);

	range_list_destroy (col_input_range);
	range_list_destroy (row_input_range);
	destroy_data_set_list (row_data);
	destroy_data_set_list (col_data);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
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

int
anova_two_factor_with_r_tool (WorkbookControl *wbc, Sheet *sheet, Value *input,
			      int rows_per_sample, gnum_float alpha,
			      data_analysis_output_t *dao)
{
	guint           rows, i_c, i_r;
	guint           n_r, n_c, n = 0;
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
	gint return_value = 0;

	rows = input->v_range.cell.b.row - input->v_range.cell.a.row + 
		(dao->labels_flag ? 0 : 1);
	n_r = rows/rows_per_sample;
	n_c = input->v_range.cell.b.col - input->v_range.cell.a.col + 
		(dao->labels_flag ? 0 : 1);

	/* Check that correct number of rows per sample */
	if (rows % rows_per_sample != 0)
	        return 1;

	/* Check that at least two columns of data are given */
	if (n_c < 2)
	        return 3;
	/* Check that at least two data rows of data are given */
	if (n_r < 2)
	        return 3;

	if (dao->labels_flag) {
		input->v_range.cell.a.row += 1;
		input->v_range.cell.a.col += 1;
	}

	col_labels = g_ptr_array_new ();
	g_ptr_array_set_size (col_labels, n_c);
	for (i_c = 0; i_c < n_c; i_c++) 
		g_ptr_array_index (col_labels, i_c) = make_label (
			sheet, input->v_range.cell.a.col + i_c,
			input->v_range.cell.a.row - 1,
			_("Column %i"), i_c + 1, dao->labels_flag);
	row_labels = g_ptr_array_new ();
	g_ptr_array_set_size (row_labels, n_r);
	for (i_r = 0; i_r < n_r; i_r++) 
		g_ptr_array_index (row_labels, i_r) = make_label (
			sheet, 
			input->v_range.cell.a.col - 1,
			input->v_range.cell.a.row + i_r * rows_per_sample,
			_("Row %i"), i_r + 1, dao->labels_flag);

	input_cp = value_duplicate (input);
	input_cp->v_range.cell.b.row = input_cp->v_range.cell.a.row + 
		rows_per_sample - 1;
	row_data = g_ptr_array_new ();
	while (input_cp->v_range.cell.a.row < input->v_range.cell.b.row) {
		GPtrArray *col_data = NULL;
		GSList *col_input_range = NULL;
		
		col_input_range = g_slist_prepend (NULL, value_duplicate (input_cp));
		prepare_input_range (&col_input_range, GROUPED_BY_COL);
		col_data = new_data_set_list (col_input_range, GROUPED_BY_COL,
				  TRUE, FALSE, sheet);
		g_ptr_array_add (row_data, col_data);
		range_list_destroy (col_input_range);
		input_cp->v_range.cell.a.row += rows_per_sample;
		input_cp->v_range.cell.b.row += rows_per_sample;
	}	
	value_release (input_cp);

/* We have loaded the data, we should now check for missing observations */
	
	for (i_r = 0; i_r < n_r; i_r++) {
		GPtrArray *data = NULL;

		data = g_ptr_array_index (row_data, i_r);
		for (i_c = 0; i_c < n_c; i_c++) {
			data_set_t *cell_data;
			guint num;
			
			cell_data = g_ptr_array_index (data, i_c);
			num = cell_data->data->len;
			if (num == 0) 
				empty_sample = TRUE;
			else {
				if (num > max_sample_size)
					max_sample_size = num;
				missing_observations += (rows_per_sample - num);
			}
		}
	}

	if (empty_sample) {
		return_value =  4;
		goto anova_two_factor_with_r_tool_end;
	}

	missing_observations -= ((rows_per_sample - max_sample_size) * n_c * n_r);

/* We are ready to create the summary table */

	prepare_output (wbc, dao, _("Anova"));

	set_cell (dao, 0, 0, _("Anova: Two-Factor With Replication"));
	set_cell (dao, 0, 2, _("SUMMARY"));
	for (i_c = 0; i_c < n_c; i_c++)
		set_cell (dao, 1 + i_c, 2, 
			 (char *)g_ptr_array_index (col_labels, i_c));
	set_cell (dao, 1 + n_c, 2, _("Total"));
	for (i_r = 0; i_r <= n_r; i_r++) {
		set_cell_text_col (dao, 0, 4 + i_r * 6, _("/Count"
							"/Sum"
							"/Average"
							"/Variance"));
		if (i_r < n_r)
			set_cell (dao, 0, 3 + i_r * 6, 
				  (char *)g_ptr_array_index (row_labels, i_r));
	}
	set_cell (dao, 0, 3 + n_r * 6, _("Total"));

	set_cell_text_col (dao, 0, n_r * 6 + 10, _("/ANOVA"
						    "/Source of Variation"
						    "/Rows"
						    "/Columns"
						    "/Interaction"
						    "/Within"));
	set_cell (dao, 0, n_r * 6 + 17, _("Total"));

	set_cell_text_row (dao, 1,  n_r * 6 + 11, _("/SS"
						    "/df"
						    "/MS"
						    "/F"
						    "/P-value"
						    "/F critical"));

	for (i_r = 0; i_r < n_r; i_r++) {
		GPtrArray *data = NULL;
		gnum_float row_sum = 0.0;
		gnum_float row_sum_sq = 0.0;
		guint row_cnt = 0;

		data = g_ptr_array_index (row_data, i_r);
		for (i_c = 0; i_c < n_c; i_c++) {
			data_set_t *cell_data;
			gnum_float v;
			int error;
			int num;
			gnum_float *the_data;
			
			cell_data = g_ptr_array_index (data, i_c);
			the_data = (gnum_float *)cell_data->data->data;
			num = cell_data->data->len;
			row_cnt += num;

			set_cell_int (dao, 1 + i_c, 4 + i_r * 6, num);
			error = range_sum (the_data, num, &v);
			row_sum += v;
			ss_rc += v * v / num;
			set_cell_float_na (dao, 1 + i_c, 5 + i_r * 6, v, error == 0);
			set_cell_float_na (dao, 1 + i_c, 6 + i_r * 6, v / num, 
					   error == 0 && num > 0);
			error = range_var_est (the_data, num , &v);
			set_cell_float_na (dao, 1 + i_c, 7 + i_r * 6, v, error == 0);

			error = range_sumsq (the_data, num, &v);
			row_sum_sq += v;
		}
		cm += row_sum;
		n += row_cnt;
		ss_r += row_sum * row_sum / row_cnt;
		ss_total += row_sum_sq;
		set_cell_int (dao, 1 + n_c, 4 + i_r * 6, row_cnt);
		set_cell_float (dao, 1 + n_c, 5 + i_r * 6, row_sum);
		set_cell_float (dao, 1 + n_c, 6 + i_r * 6, row_sum / row_cnt);
		set_cell_float (dao, 1 + n_c, 7 + i_r * 6, 
				(row_sum_sq - row_sum * row_sum / row_cnt) / (row_cnt - 1));
	}

	for (i_c = 0; i_c < n_c; i_c++) {
		gnum_float col_sum = 0.0;
		gnum_float col_sum_sq = 0.0;
		guint col_cnt = 0;

		for (i_r = 0; i_r < n_r; i_r++) {
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
		set_cell_int (dao, 1 + i_c, 4 + n_r * 6, col_cnt);
		set_cell_float (dao, 1 + i_c, 5 + n_r * 6, col_sum);
		set_cell_float (dao, 1 + i_c, 6 + n_r * 6, col_sum / col_cnt);
		set_cell_float (dao, 1 + i_c, 7 + n_r * 6, 
				(col_sum_sq - col_sum * col_sum / col_cnt) / (col_cnt - 1));
	}

	set_cell_int (dao, 1 + n_c, 4 + n_r * 6, n);
	set_cell_float (dao, 1 + n_c, 5 + n_r * 6, cm);
	set_cell_float (dao, 1 + n_c, 6 + n_r * 6, cm / n);
	cm = cm * cm/ n;
	set_cell_float   (dao, 1 + n_c, 7 + n_r * 6, (ss_total - cm) / (n - 1));
	
	df_r = n_r - 1;
	df_c = n_c - 1;
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

		for (i_r = 0; i_r < n_r; i_r++) {
			GPtrArray *data = NULL;
			
			data = g_ptr_array_index (row_data, i_r);
			for (i_c = 0; i_c < n_c; i_c++) {
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

		for (i_r = 0; i_r < n_r; i_r++) {
			GPtrArray *data = NULL;
			gnum_float row_sum = 0.0;
			gnum_float row_sum_sq = 0.0;
			guint row_cnt = 0;
			
			data = g_ptr_array_index (row_data, i_r);
			for (i_c = 0; i_c < n_c; i_c++) {
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
		
		for (i_c = 0; i_c < n_c; i_c++) {
			gnum_float col_sum = 0.0;
			guint col_cnt = 0;
			
			for (i_r = 0; i_r < n_r; i_r++) {
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

	set_cell_float (dao, 1, n_r * 6 + 12, ss_r);
	set_cell_float (dao, 1, n_r * 6 + 13, ss_c);
	set_cell_float (dao, 1, n_r * 6 + 14, ss_rc);
	set_cell_float (dao, 1, n_r * 6 + 15, ss_e);
	set_cell_float (dao, 1, n_r * 6 + 17, ss_total);

	set_cell_int   (dao, 2, n_r * 6 + 12, df_r);
	set_cell_int   (dao, 2, n_r * 6 + 13, df_c);
	set_cell_int   (dao, 2, n_r * 6 + 14, df_rc);
	set_cell_int   (dao, 2, n_r * 6 + 15, df_e);
	set_cell_int   (dao, 2, n_r * 6 + 17, df_total);

	ms_r = ss_r / df_r;
	ms_c = ss_c / df_c;
	ms_rc = ss_rc / df_rc;
	ms_e = ss_e / df_e;

	set_cell_float_na (dao, 3, n_r * 6 + 12, ms_r, df_r > 0);
	set_cell_float_na (dao, 3, n_r * 6 + 13, ms_c, df_c > 0);
	set_cell_float_na (dao, 3, n_r * 6 + 14, ms_rc, df_rc > 0);
	set_cell_float_na (dao, 3, n_r * 6 + 15, ms_e, df_e > 0);

	f_r = ms_r / ms_e;
	f_c = ms_c / ms_e;
	f_rc = ms_rc / ms_e;

	set_cell_float_na (dao, 4, n_r * 6 + 12, f_r, ms_e != 0 && df_r > 0);
	set_cell_float_na (dao, 4, n_r * 6 + 13, f_c, ms_e != 0 && df_c > 0);
	set_cell_float_na (dao, 4, n_r * 6 + 14, f_rc, ms_e != 0 && df_rc > 0);

	p_r = 1.0 - pf (f_r, df_r, df_e);
	p_c = 1.0 - pf (f_c, df_c, df_e);
	p_rc = 1.0 - pf (f_rc, df_rc, df_e);

	set_cell_float_na (dao, 5, n_r * 6 + 12, p_r, ms_e != 0 && df_r > 0 && df_e > 0);
	set_cell_float_na (dao, 5, n_r * 6 + 13, p_c, ms_e != 0 && df_c > 0 && df_e > 0);
	set_cell_float_na (dao, 5, n_r * 6 + 14, p_rc, ms_e != 0 && df_rc > 0 && df_e > 0);

	set_cell_float_na (dao, 6, n_r * 6 + 12, qf (1 - alpha, df_r, df_e), 
			   df_r > 0 && df_e > 0);
	set_cell_float_na (dao, 6, n_r * 6 + 13, qf (1 - alpha, df_c, df_e), 
			   df_c > 0 && df_e > 0);
	set_cell_float_na (dao, 6, n_r * 6 + 14, qf (1 - alpha, df_rc, df_e), 
			   df_rc > 0 && df_e > 0);

	set_italic (dao, 0, n_r * 6 + 11, 6, n_r * 6 + 11);
	set_italic (dao, 0, 2, 1 + n_c, 2);
	set_italic (dao, 0, 0, 0, 17 + n_r * 6);

	autofit_columns (dao, 0, 6);
	autofit_columns (dao, 7, n_c);

 anova_two_factor_with_r_tool_end:

	for (i_r = 0; i_r < row_data->len; i_r++)
		destroy_data_set_list (g_ptr_array_index (row_data, i_r));
	g_ptr_array_free (row_data, TRUE);

	for (i_c = 0; i_c < n_c; i_c++)
		g_free (g_ptr_array_index (col_labels, i_c));
	g_ptr_array_free (col_labels, TRUE);
	for (i_r = 0; i_r < n_r; i_r++)
		g_free (g_ptr_array_index (row_labels, i_r));
	g_ptr_array_free (row_labels, TRUE);

	if (return_value == 0) {
		sheet_set_dirty (dao->sheet, TRUE);
		sheet_update (dao->sheet);
	}

	return return_value;
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
destroy_items (gpointer data, gpointer user_data) {
	if (((bin_t*)data)->label != NULL && ((bin_t*)data)->destroy_label)
		g_free(((bin_t*)data)->label);
	g_free (data);
}

int
histogram_tool (WorkbookControl *wbc, Sheet *sheet, GSList *input, Value *bin,
		group_by_t group_by, gboolean bin_labels, 
		gboolean pareto, gboolean percentage, gboolean cumulative,
		gboolean chart, histogram_calc_bin_info_t *bin_info,
		data_analysis_output_t *dao)
{
	GSList *input_range;
	GPtrArray *data = NULL;
	GSList *bin_range;
	GPtrArray *bin_data = NULL;
	GSList *bin_list = NULL;
	bin_t  *a_bin;
	guint  i, j, row, col;
	GSList * this;
	gnum_float *this_value;

	if (bin != NULL) {
/* read bin data */
		bin_range = g_slist_prepend (NULL, bin);
		prepare_input_range (&bin_range, GROUPED_BY_ROW);
		bin_data = new_data_set_list (bin_range, GROUPED_BY_BIN,
					      TRUE, bin_labels, sheet);
		for (i = 0; i < bin_data->len; i++) {
			if (((data_set_t *)g_ptr_array_index (bin_data, i))->data->len != 1) {
				range_list_destroy (input);
				destroy_data_set_list (bin_data);
				range_list_destroy (bin_range);
				/* inconsistent bins */
				return 2;
			}
		}
	}

/* read input data */
	input_range = input;
	prepare_input_range (&input_range, group_by);
	data = new_data_set_list (input_range, group_by,
				  TRUE, dao->labels_flag, sheet);

/* set up counter structure */
	if (bin != NULL) {
		for (i=0; i < bin_data->len; i++) {
			a_bin = g_new (bin_t, 1);
			a_bin->limit = g_array_index (
				((data_set_t *)g_ptr_array_index (bin_data, i))->data, 
				gnum_float, 0);
			a_bin->counts = g_array_new (FALSE, TRUE, sizeof (gnum_float));
			a_bin->counts = g_array_set_size (a_bin->counts, data->len);
			a_bin->label = ((data_set_t *)g_ptr_array_index (bin_data, i))->label;
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

		if (!bin_info->max_given) {
			value_set = FALSE;
			for (i = 0; i < data->len; i++) {
				GArray * the_data;
				gnum_float a_max;
				the_data = ((data_set_t *)(g_ptr_array_index (data, i)))->data;
				if (0 == range_max ((gnum_float *)the_data->data, the_data->len, 
						    &a_max)) {
					if (value_set) {
						if (a_max > bin_info->max)
							bin_info->max = a_max;
					} else {
						bin_info->max = a_max;
						value_set = TRUE;
					}
				}
			}
			if (!value_set)
				bin_info->max = 0.0;
		}
		if (!bin_info->min_given) {
			value_set = FALSE;
			for (i = 0; i < data->len; i++) {
				GArray * the_data;
				gnum_float a_min;
				the_data = ((data_set_t *)(g_ptr_array_index (data, i)))->data;
				if (0 == range_min ((gnum_float *)the_data->data, the_data->len, 
						    &a_min)) {
					if (value_set) {
						if (a_min < bin_info->min)
							bin_info->min = a_min;
					} else {
						bin_info->min = a_min;
						value_set = TRUE;
					}
				}
			}
			if (!value_set)
				bin_info->min = 0.0;
		}

		skip = (bin_info->max - bin_info->min) / bin_info->n;
		for (i = 0; (int)i < bin_info->n;  i++) {
			a_bin = g_new (bin_t, 1);
			a_bin->limit = bin_info->max - i * skip;
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
		a_bin->limit = bin_info->min;
		a_bin->counts = g_array_new (FALSE, TRUE, sizeof (gnum_float));
		a_bin->counts = g_array_set_size (a_bin->counts, data->len);
		val = value_new_float(bin_info->min);
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
			value_release(val);
		a_bin->last = FALSE;
		a_bin->first = TRUE;
		a_bin->strict = TRUE;
		bin_list = g_slist_prepend (bin_list, a_bin);			
		bin_labels = FALSE;
	}
	a_bin = g_new (bin_t, 1);
	a_bin->limit = 0.0;
	a_bin->counts = g_array_new (FALSE, TRUE, sizeof (gnum_float));
	a_bin->counts = g_array_set_size (a_bin->counts, data->len);
	a_bin->destroy_label = FALSE;
	if (bin != NULL) {
		a_bin->label = _("More");
	} else {
		char        *text;
		Value       *val;

		val = value_new_float(bin_info->max);
		text = format_value (NULL, val, NULL, 10);
		if (text) {
			a_bin->label = g_strdup_printf (_(">%s"), text);
			a_bin->destroy_label = TRUE;
			g_free (text);
		} else 
			a_bin->label = _("Too Large");
		if (val)
			value_release(val);
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
	if (pareto && (data->len > 0))
		bin_list = g_slist_sort (bin_list,
					 (GCompareFunc) bin_pareto);

	prepare_output (wbc, dao, _("Histogram"));

/* print labels */
	row = dao->labels_flag ? 1 : 0;
	if (!bin_labels) 
		set_cell (dao, 0, row, _("Bin"));
	
	this = bin_list;
	while (this != NULL) {
		row++;
		if (bin_labels || ((bin_t *)this->data)->last || ((bin_t *)this->data)->first) {
			set_cell (dao, 0, row, ((bin_t *)this->data)->label);
		} else {
			set_cell_float (dao, 0, row, ((bin_t *)this->data)->limit);
		}
		this = this->next;
	}
	set_italic (dao, 0, 0, 0, row);

	col = 1;
	for (i = 0; i < data->len; i++) {
		guint l_col = col;
		gnum_float y = 0.0;
		row = 0;

		if (dao->labels_flag) {
			set_cell (dao, col, row, 
				  ((data_set_t *)g_ptr_array_index (data, i))->label);
			row++;
		}
		set_cell (dao, col, row, _("Frequency"));
		if (percentage)
			set_cell (dao, ++l_col, row, _("%"));		
		if (cumulative)
			/* xgettext:no-c-format */
			set_cell (dao, ++l_col, row, _("Cumulative %"));
/* print data */
		this = bin_list;
		while (this != NULL) {
			gnum_float x;
			
			l_col = col;
			x = g_array_index (((bin_t *)this->data)->counts, gnum_float, i);
			row ++;
			set_cell_float (dao, col, row,  x);
			x = x/(((data_set_t *)(g_ptr_array_index (data, i)))->data->len);
			if (percentage) {
				l_col++;
				set_percent (dao, l_col, row, l_col, row);
				set_cell_float (dao, l_col, row, x);
			}
			if (cumulative) {
				y += x;
				l_col++;
				set_percent (dao, l_col, row, l_col, row);
				set_cell_float (dao, l_col, row, y);
			}
			this = this->next;
		}
		col++;
		if (percentage)
			col++;
		if (cumulative)
			col++;
	}
	set_italic (dao, 0, 0,  col - 1, dao->labels_flag ? 1 : 0);
	autofit_columns (dao, 0, col - 1);

/* finish up */
	destroy_data_set_list (data);
	range_list_destroy (input_range);
	if (bin) {
		destroy_data_set_list (bin_data);
		range_list_destroy (bin_range);
	}
	g_slist_foreach (bin_list, destroy_items, NULL);
	g_slist_free (bin_list);
	

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (dao->sheet);

	if (chart)
		g_warning ("TODO : tie this into the graph generator");
	return 0;
}
