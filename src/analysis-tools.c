/*
 * analysis-tools.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * Modified 2001 to use range_* functions of mathfunc.h
 */

#include <config.h>
#include <glib.h>
#include <string.h>
#include <math.h>
#include "mathfunc.h"
#include "rangefunc.h"
#include "numbers.h"
#include "gnumeric.h"
#include "dialogs.h"
#include "parse-util.h"
#include "tools.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "style.h"
#include "regression.h"
#include "sheet-style.h"
#include "workbook.h"

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>

typedef struct {
        GSList  *array;
        gnum_float sum;
        gnum_float sum2;    /* square of the sum */
        gnum_float sqrsum;
        gnum_float min;
        gnum_float max;
        int     n;
} data_set_t;

typedef struct {
	gnum_float mean;
	gint       error_mean;
	gnum_float var;
	gint       error_var;
	gint      len;
} desc_stats_t;


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

void
set_cell_value (data_analysis_output_t *dao, int col, int row, Value *v)
{
        Cell *cell;

	/* Check that the output is in the given range */
	if (dao->type == RangeOutput && (col >= dao->cols || row >= dao->rows))
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
		set_cell_value (dao, col, row, value_new_string (""));
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


/* Returns 1 if non-numeric data was found, 0 otherwise.
 */
static int
get_data (Sheet *sheet, Range *range, data_set_t *data)
{
        gpointer p;
	Cell     *cell;
	Value    *v;
	gnum_float  x;
	int      row, col, status = 0;

	data->sum = 0;
	data->sum2 = 0;
	data->sqrsum = 0;
	data->n = 0;
	data->array = NULL;

	for (col = range->start.col; col <= range->end.col; col++)
	        for (row = range->start.row; row <= range->end.row; row++) {
		        cell = sheet_cell_get (sheet, col, row);
			if (cell != NULL && cell->value != NULL) {
			        v = cell->value;
				if (VALUE_IS_NUMBER (v))
				        x = value_get_as_float (v);
				else {
				        x = 0;
					status = 1;
				}

				p = g_new (gnum_float, 1);
				* ((gnum_float *) p) = x;
				data->array = g_slist_append (data->array, p);
				data->sum += x;
				data->sqrsum += x * x;
				if (data->n == 0) {
				        data->min = x;
					data->max = x;
				} else {
				        if (data->min > x)
					        data->min = x;
					if (data->max < x)
					        data->max = x;
				}
				data->n++;
			} else
			        status = 1;
		}

	data->sum2 = data->sum * data->sum;

	return status;
}

static void
get_data_groupped_by_columns (Sheet *sheet, const Range *range, int col,
			      data_set_t *data)
{
        gpointer p;
	Cell     *cell;
	Value    *v;
	gnum_float  x;
	int      row;

	data->sum = 0;
	data->sum2 = 0;
	data->sqrsum = 0;
	data->n = 0;
	data->array = NULL;

	for (row = range->start.row; row <= range->end.row; row++) {
		cell = sheet_cell_get (sheet, col + range->start.col, row);
		if (cell != NULL && cell->value != NULL) {
			v = cell->value;
			if (VALUE_IS_NUMBER (v))
				x = value_get_as_float (v);
			else
				x = 0;

			p = g_new (gnum_float, 1);
			* ((gnum_float *) p) = x;
			data->array = g_slist_append (data->array, p);
			data->sum += x;
			data->sqrsum += x * x;
			if (data->n == 0) {
				data->min = x;
				data->max = x;
			} else {
				if (data->min > x)
					data->min = x;
				if (data->max < x)
					data->max = x;
			}
			data->n++;
		}
	}

	data->sum2 = data->sum * data->sum;
}

/* Callbacks for write */
static Value *
WriteData_ForeachCellCB_slow (Sheet *sheet, int col, int row,
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

static Value *
WriteData_ForeachCellCB_fast (Sheet *sheet, int col, int row,
			      Cell *cell, GArray* data)
{
	gnum_float  x;
	if (data->len == 0)
		return (value_terminate ());
	if (cell == NULL)
		cell = sheet_cell_new (sheet, col, row);
	x = g_array_index (data, gnum_float, data->len - 1);
	g_array_remove_index_fast (data, data->len - 1);
	sheet_cell_set_value (cell, value_new_float (x), NULL);
	return NULL;
}

static void
write_data (WorkbookControl *wbc, data_analysis_output_t *dao, GArray *data, gboolean fast)
{
	gint st_row = 0, end_row = data->len - 1, st_col = 0, end_col = 0;

	if (dao->type == RangeOutput) {
		st_row = dao->start_row;
		end_row = st_row + dao->rows - 1;
		st_col = dao->start_col;
		end_col = st_col;
/* If we weren't trying to be XL compatible, we should have:  */
/* end_col = dao->start_col + dao->cols - 1;  */
	}

	if (fast) {
		sheet_foreach_cell_in_range (dao->sheet, FALSE, st_col, st_row, end_col, end_row,
					     (ForeachCellCB)&WriteData_ForeachCellCB_fast, data);
	} else {
		sheet_foreach_cell_in_range (dao->sheet, FALSE, st_col, st_row, end_col, end_row,
					     (ForeachCellCB)&WriteData_ForeachCellCB_slow, data);
	}
}


/* Callback for get_data_grouped_by_cols and get_data_grouped_by_rows */
static Value *
StoreData_ForeachCellCB (Sheet *sheet, int col, int row,
			 Cell *cell, GArray* data)
{
	gnum_float  x;

	if (VALUE_IS_NUMBER (cell->value)) {
		x = value_get_as_float (cell->value);
		g_array_append_val (data, x);
	}
	return NULL;
}

/* get_data_by_cb returns 0 if all values read are numbers  */
/* it reads the numbers in no particular order    */
static int
get_data_by_cb (Sheet *sheet, const Range *range, GArray *data)
{
	int n_of_data = (range->end.row - range->start.row + 1) *
		(range->end.col - range->start.col + 1);

	sheet_foreach_cell_in_range (sheet, TRUE, range->start.col, range->start.row,
				     range->end.col, range->end.row,
				     (ForeachCellCB)&StoreData_ForeachCellCB, data);

	return (n_of_data - data->len);
}


/* get_data_grouped_by_cols returns 0 if all values read are numbers  */
/* it reads the numbers in each column and stores them in an array    */
static int
get_data_grouped_by_cols (Sheet *sheet, const Range *range, GPtrArray *data)
{
	int  col;
	GArray * this_col;
	int error = 0;
	int n_of_data = range->end.row - range->start.row + 1;


	for (col = range->start.col; col <= range->end.col;  col++) {
		this_col = g_array_new (FALSE, FALSE, sizeof (gnum_float));
		g_ptr_array_add (data, this_col);

/* Note: order of reading cells matters only in so   */
/*       far that we should proceed from left top to */
/*       right bottom since this is a 1 cell strip.  */

		sheet_foreach_cell_in_range (sheet, TRUE, col, range->start.row,
					    col, range->end.row,
					    (ForeachCellCB)&StoreData_ForeachCellCB, this_col);
		error += n_of_data - this_col->len;
	}
	return (error);
}
/* get_data_grouped_by_rows returns 0 if all values read are numbers  */
/* it reads the numbers in each row and stores them in an array    */

static int
get_data_grouped_by_rows (Sheet *sheet, const Range *range, GPtrArray *data)
{
	int      row;
	GArray * this_row;
	int error = 0;
	int n_of_data = range->end.col - range->start.col + 1;


	for (row = range->start.row; row <= range->end.row; row++) {
		this_row = g_array_new (FALSE, FALSE, sizeof (gnum_float));
		g_ptr_array_add (data, this_row);

/* Note: order of reading cells matters only in so   */
/*       far that we should proceed from left top to */
/*       right bottom since this is a 1 cell strip.  */

		sheet_foreach_cell_in_range (sheet, TRUE, range->start.col, row,
					     range->end.col, row,
					     (ForeachCellCB)&StoreData_ForeachCellCB, this_row);
		error += n_of_data - this_row->len;
	}
	return (error);
}

/* Callback for get_text_col & get_text_row          */
/* FIXME: why isn't this just static? */
Value *StoreText_ForeachCellCB (Sheet *sheet, int col, int row,
			        Cell *cell, GPtrArray* data);

Value *
StoreText_ForeachCellCB (Sheet *sheet, int col, int row,
			 Cell *cell, GPtrArray* data)
{
	if (cell == NULL) {
		g_ptr_array_add (data, NULL);
	} else {
		g_ptr_array_add (data, value_get_as_string (cell->value));
	}
	return NULL;
}

/* get_text_col                                      */
/* reads the strings in the first col of the range   */
/*                                                   */
/* We are using value_get_as_string rather than      */
/* value_peek_string such that all our labels,       */
/* whether created on the fly or not, need to be     */
/* freed. For teh same reason we use a NULL pointer  */
/* ratehr than an empty string.                      */
static void
get_text_col (Sheet *sheet, const Range *range, GPtrArray *data)
{
/* Note: order of reading cells matters only in so   */
/*       far that we should proceed from left top to */
/*       right bottom since this is a 1 cell strip.  */

	sheet_foreach_cell_in_range (sheet, FALSE, range->start.col, range->start.row,
				     range->start.col, range->end.row,
				     (ForeachCellCB)&StoreText_ForeachCellCB, data);
}

/* get_text_row                                      */
/* reads the strings in the first row of the range   */
static void
get_text_row (Sheet *sheet, const Range *range, GPtrArray *data)
{
/* Note: order of reading cells matters only in so   */
/*       far that we should proceed from left top to */
/*       right bottom since this is a 1 cell strip.  */

	sheet_foreach_cell_in_range (sheet, FALSE, range->start.col, range->start.row,
				     range->end.col, range->start.row,
				     (ForeachCellCB)&StoreText_ForeachCellCB, data);
}

static void
get_data_groupped_by_rows (Sheet *sheet, const Range *range, int row,
			   data_set_t *data)
{
        gpointer p;
	Cell     *cell;
	Value    *v;
	gnum_float  x;
	int      col;

	data->sum = 0;
	data->sum2 = 0;
	data->sqrsum = 0;
	data->n = 0;
	data->array = NULL;

	for (col = range->start.col; col <= range->end.col; col++) {
		cell = sheet_cell_get (sheet, col, row + range->start.row);
		if (cell != NULL && cell->value != NULL) {
			v = cell->value;
			if (VALUE_IS_NUMBER (v))
				x = value_get_as_float (v);
			else
				x = 0;

			p = g_new (gnum_float, 1);
			* ((gnum_float *) p) = x;
			data->array = g_slist_append (data->array, p);
			data->sum += x;
			data->sqrsum += x * x;
			data->n++;
		}
	}

	data->sum2 = data->sum * data->sum;
}

static void
free_data_set (data_set_t *data)
{
        GSList *current = data->array;

	while (current != NULL) {
	        g_free (current->data);
		current = current->next;
	}

	g_slist_free (data->array);
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

	if (dao->type == NewSheetOutput ||
	    dao->type == NewWorkbookOutput)
	        actual_col = col;
	else
	        actual_col = dao->start_col + col;

	ideal_size = sheet_col_size_fit_pixels (dao->sheet, actual_col);
	if (ideal_size == 0)
	        return;

	sheet_col_set_size_pixels (dao->sheet, actual_col, ideal_size, TRUE);
	sheet_recompute_spans_for_col (dao->sheet, col);
}

static void
set_italic (data_analysis_output_t *dao, int col1, int row1,
	    int col2, int row2)
{
	MStyle *mstyle = mstyle_new ();
	Range  range;
	int    col_inc, row_inc;

	if (dao->type == RangeOutput) {
	        col_inc = dao->start_col;
		row_inc = dao->start_row;
	} else
	        col_inc = row_inc = 0;

	range.start.col = col1 + col_inc;
	range.start.row = row1 + row_inc;
	range.end.col   = col2 + col_inc;
	range.end.row   = row2 + row_inc;

	mstyle_set_font_italic (mstyle, TRUE);
	sheet_style_apply_range (dao->sheet, &range, mstyle);
}

static int
get_labels_n_data (Sheet *sheet, GPtrArray  *labels, GPtrArray  *data,
		  Range *range, int columns_flag,
		  data_analysis_output_t *dao)
{
	gchar      *label;
	int        cols, rows, i;
	int        error = 0;

	if (columns_flag) {
		if (dao->labels_flag) {
			get_text_row (sheet, range, labels);
			range->start.row++;
		} else {
			cols = range->end.col - range->start.col + 1;
			for (i = 1; i <= cols; i++) {
				label = g_strdup_printf (_("Column %d"), i);
				g_ptr_array_add (labels, label);
			}
		}
		error = get_data_grouped_by_cols (sheet, range, data);
	} else {
		if (dao->labels_flag) {
			get_text_col (sheet, range, labels);
			range->start.col++;
		} else {
			rows = range->end.row - range->start.row + 1;
			for (i = 1; i <= rows; i++) {
				label = g_strdup_printf (_("Row %d"), i);
				g_ptr_array_add (labels, label);
			}
		}
		error = get_data_grouped_by_rows (sheet, range, data);
	}
	return error;

}

static void
set_labels_in_row (GPtrArray  *labels, int row, int rowstart,
		   data_analysis_output_t *dao)
{
	gchar      *label;
	guint col;

        set_cell (dao, 0, 0, NULL);
	for (col = 0; col < (labels->len); col++) {
	        label = (gchar *) g_ptr_array_index (labels, col);
		set_cell (dao, col + rowstart, row, label);
	}
	set_italic (dao, rowstart, row, rowstart + labels->len - 1, row);
}

static void
set_labels_in_col (GPtrArray  *labels, int col, int colstart,
		  data_analysis_output_t *dao)
{
	gchar      *label;
	guint row;

        set_cell (dao, 0, 0, NULL);
	for (row = 0; row < (labels->len); row++) {
	        label = (gchar *) g_ptr_array_index (labels, row);
		set_cell (dao, col, row + colstart, label);
	}
	set_italic (dao, col, colstart, col, colstart + labels->len - 1);
}




/************* Correlation Tool *******************************************
 *
 * The correlation tool calculates the correlation coefficient of two
 * data sets.  The two data sets can be grouped by rows or by columns.
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

/* If columns_flag is set, the data entries are grouped by columns
 * otherwise by rows.
 */
int
correlation_tool (WorkbookControl *wbc, Sheet *sheet,
		  Range *range, int columns_flag,
		  data_analysis_output_t *dao)
{
        GPtrArray *labels = g_ptr_array_new ();
	GPtrArray *data = g_ptr_array_new ();
	guint col, row;
	int error;
	gnum_float x;
	GArray *the_col, *the_row;

	if (0 != get_labels_n_data (sheet, labels, data, range, columns_flag, dao)) {
		g_ptr_array_free (labels, TRUE);
		g_ptr_array_free (data, TRUE);
		return 1;
	}

	prepare_output (wbc, dao, _("Correlations"));

	set_labels_in_row (labels, 0, 1, dao);
	set_labels_in_col (labels, 0, 1, dao);

        set_cell (dao, 0, 0, NULL);

	for (row = 0; row < data->len; row++) {
		for (col = 0; col < data->len; col++) {
		        if (row == col) {
			        set_cell_int (dao, col + 1, row + 1, 1);
				break;
			} else {
				if (row < col) {
					set_cell (dao, col + 1, row + 1, NULL);
				} else {
					the_col = g_ptr_array_index (data, col);
					the_row = g_ptr_array_index (data, row);

					error =  range_correl_pop
						((gnum_float *)(the_col->data),
						 (gnum_float *)(the_row->data),
						 the_col->len, &x);
					if (error)
						set_cell_na (dao, col + 1, row + 1);
					else
						set_cell_float (dao, col + 1, row + 1, x);
				}

			}
		}
	}

	/* labels contains char* and data contains g_array of gnum_float, passing TRUE should */
	/* dispose that memory correctly                                                      */
	g_ptr_array_free (labels, TRUE);
	g_ptr_array_free (data, TRUE);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

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
		 Range *range, int columns_flag,
		 data_analysis_output_t *dao)
{
        GPtrArray     *labels = g_ptr_array_new ();
	GPtrArray     *data = g_ptr_array_new ();
	guint        col, row;
	int        error;
	gnum_float x;
	GArray     *the_col, *the_row;

	if (0 != get_labels_n_data (sheet, labels, data, range, columns_flag, dao)) {
		g_ptr_array_free (labels, TRUE);
		g_ptr_array_free (data, TRUE);
		return 1;
	}

	prepare_output (wbc, dao, _("Correlations"));

	set_labels_in_row (labels, 0, 1, dao);
	set_labels_in_col (labels, 0, 1, dao);

        set_cell (dao, 0, 0, NULL);

	for (row = 0; row < data->len; row++) {
		for (col = 0; col < data->len; col++) {
		        if (row == col) {
			        set_cell_int (dao, col + 1, row + 1, 1);
				break;
			} else {
				if (row < col) {
					set_cell (dao, col + 1, row + 1, NULL);
				} else {
					the_col = g_ptr_array_index (data, col);
					the_row = g_ptr_array_index (data, row);

					error =  range_covar
						((gnum_float *)(the_col->data),
						 (gnum_float *)(the_row->data),
						 the_col->len, &x);
					if (error)
						set_cell_na (dao, col + 1, row + 1);
					else
						set_cell_float (dao, col + 1, row + 1, x);
				}

			}
		}
	}

	/* labels contains char* and data contains g_array of gnum_float, passing TRUE should */
	/* dispose that memory correctly                                                      */
	g_ptr_array_free (labels, TRUE);
	g_ptr_array_free (data, TRUE);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

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


static void
summary_statistics (WorkbookControl *wbc, GPtrArray *data, GPtrArray* labels,
		    data_analysis_output_t *dao, GArray *basic_stats)
{
	guint     col;

	prepare_output (wbc, dao, _("Summary Statistics"));

        set_cell (dao, 0, 0, NULL);
	set_labels_in_row (labels, 0, 1, dao);

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
		GArray *the_col = g_ptr_array_index (data, col);
		const gnum_float *the_data = (gnum_float *)the_col->data;
		int the_col_len = the_col->len;
		gnum_float x, xmin, xmax;
		int error, error2;
	        desc_stats_t info = g_array_index(basic_stats, desc_stats_t, col);

	        /* Mean */
		if (info.error_mean == 0) {
			set_cell_float (dao, col + 1, 1, info.mean);
		} else {
			set_cell_na (dao, col + 1, 1);
		}

		if (info.error_var == 0) {
			/* Standard Error */
			set_cell_float (dao, col + 1, 2, sqrt (info.var / info.len));
			/* Standard Deviation */
			set_cell_float (dao, col + 1, 5, sqrt (info.var));
			/* Sample Variance */
			set_cell_float (dao, col + 1, 6, info.var);
		} else {
			set_cell_na (dao, col + 1, 2);
			set_cell_na (dao, col + 1, 5);
			set_cell_na (dao, col + 1, 6);
		}

		/* Median */
		error = range_median_inter (the_data, the_col_len, &x);
		if (error == 0) {
			set_cell_float (dao, col + 1, 3, x);
		} else {
			set_cell_na (dao, col + 1, 3);
		}

		/* Mode */

		error = range_mode (the_data, the_col_len, &x);
		if (error == 0) {
			set_cell_float (dao, col + 1, 4, x);
		} else {
			set_cell_na (dao, col + 1, 4);
		}

		/* Kurtosis */
		error = range_kurtosis_m3_est (the_data, the_col_len, &x);
		if (error == 0) {
			set_cell_float (dao, col + 1, 7, x);
		} else {
			set_cell_na (dao, col + 1, 7);
		}

		/* Skewness */
		error = range_skew_est (the_data, the_col_len, &x);
		if (error == 0) {
			set_cell_float (dao, col + 1, 8, x);
		} else {
			set_cell_na (dao, col + 1, 8);
		}

		/* Minimum */
		error = range_min (the_data, the_col_len, &xmin);
		if (error == 0) {
			set_cell_float (dao, col + 1, 10, xmin);
		} else {
			set_cell_na (dao, col + 1, 10);
		}

		/* Maximum */
		error2 = range_max (the_data, the_col_len, &xmax);
		if (error2 == 0) {
			set_cell_float (dao, col + 1, 11, xmax);
		} else {
			set_cell_na (dao, col + 1, 11);
		}


		/* Range */
		if (error == 0 && error2 == 0) {
			set_cell_float (dao, col + 1, 9, xmax - xmin);
		} else {
			set_cell_na (dao, col + 1, 9);
		}

		/* Sum */
		error = range_sum (the_data, the_col_len, &x);
		if (error == 0) {
			set_cell_float (dao, col + 1, 12, x);
		} else {
			set_cell_na (dao, col + 1, 12);
		}

		/* Count */
		set_cell_int (dao, col + 1, 13, the_col_len);
	}
}

static void
confidence_level (WorkbookControl *wbc, GPtrArray *data, gnum_float c_level,
		  GPtrArray* labels, data_analysis_output_t *dao, GArray *basic_stats)
{
        gnum_float x;
        guint col;
	char *buffer;
	desc_stats_t info;

	prepare_output (wbc, dao, _("Confidence Interval for the Mean"));
	buffer = g_strdup_printf (_("/%g%% CI for the Mean from"
					"/to"), c_level * 100);
	set_cell_text_col (dao, 0, 1, buffer);
        g_free(buffer);

        set_cell (dao, 0, 0, NULL);
	set_labels_in_row (labels, 0, 1, dao);

	for (col = 0; col < labels->len; col++) {
		if ((c_level < 1) && (c_level >= 0)) {
			info = g_array_index(basic_stats, desc_stats_t, col);
			if (info.error_mean == 0 && info.error_var == 0) {
				x = -qt ((1 - c_level) / 2,info.len - 1) * sqrt (info.var / info.len);
				set_cell_float (dao, col + 1, 1, info.mean - x);
				set_cell_float (dao, col + 1, 2, info.mean + x);
				continue;
			}
		}
		set_cell_na (dao, col + 1, 1);
	}
}


static void
kth_largest (WorkbookControl *wbc, GPtrArray *data, int k,
	     GPtrArray* labels, data_analysis_output_t *dao)
{
        gnum_float x;
        guint col;
	gint error;
	GArray *the_col;

	prepare_output (wbc, dao, _("Kth Largest"));
        set_cell_printf (dao, 0, 1, _("Largest (%d)"), k);

        set_cell (dao, 0, 0, NULL);
	set_labels_in_row (labels, 0, 1, dao);

	for (col = 0; col < labels->len; col++) {
		the_col = g_ptr_array_index (data, col);
		error = range_min_k_nonconst ((gnum_float *)(the_col->data),
					      the_col->len, &x, the_col->len - k);
		if (error == 0) {
			set_cell_float (dao, col + 1, 1, x);
		} else {
			set_cell_na (dao, col + 1, 1);
		}
	}
}

static void
kth_smallest (WorkbookControl *wbc, GPtrArray  *data, int k,
	      GPtrArray* labels, data_analysis_output_t *dao)
{
        gnum_float x;
        guint col;
	gint error;
	GArray *the_col;

	prepare_output (wbc, dao, _("Kth Smallest"));
        set_cell_printf (dao, 0, 1, _("Smallest (%d)"), k);

        set_cell (dao, 0, 0, NULL);
	set_labels_in_row (labels, 0, 1, dao);

	for (col = 0; col < labels->len; col++) {
		the_col = g_ptr_array_index (data, col);
		error = range_min_k_nonconst ((gnum_float *)(the_col->data), the_col->len,
					      &x, k - 1);
		if (error == 0) {
			set_cell_float (dao, col + 1, 1, x);
		} else {
			set_cell_na (dao, col + 1, 1);
		}
	}
}

/* Descriptive Statistics
 */
int
descriptive_stat_tool (WorkbookControl *wbc, Sheet *sheet,
                       Range *range, int columns_flag,
		       descriptive_stat_tool_t *ds,
		       data_analysis_output_t *dao)
{
        GPtrArray     *labels = g_ptr_array_new ();
	GPtrArray     *data = g_ptr_array_new ();
	GArray        *basic_stats = NULL;
	GArray        *the_col;
        desc_stats_t  info;
	guint         col;
	const gnum_float *the_data;

	get_labels_n_data (sheet, labels, data, range, columns_flag, dao);

	if (ds->summary_statistics || ds->confidence_level) {
		basic_stats = g_array_new(FALSE, FALSE, sizeof(desc_stats_t));
		for (col = 0; col < data->len; col++) {
			the_col = g_ptr_array_index (data, col);
			the_data = (gnum_float *)the_col->data;
                        info.len = the_col->len;
			info.error_mean = range_average (the_data, info.len, &(info.mean));
			info.error_var = range_var_est (the_data, info.len, &(info.var));
			g_array_append_val(basic_stats, info);
		}
	}

        if (ds->summary_statistics) {
                summary_statistics (wbc, data, labels, dao, basic_stats);
		if (dao->type == RangeOutput) {
		        dao->start_row += 16;
			dao->rows -= 16;
		}
	}
	if (dao->rows < 1)
		return 0;
        if (ds->confidence_level) {
                confidence_level (wbc, data, ds->c_level, labels,  dao, basic_stats);
		if (dao->type == RangeOutput) {
		        dao->start_row += 4;
			dao->rows -= 4;
		}
	}
	if (dao->rows < 1)
		return 0;
        if (ds->kth_largest) {
                kth_largest (wbc, data, ds->k_largest, labels, dao);
		if (dao->type == RangeOutput) {
		        dao->start_row += 4;
			dao->rows -= 4;
		}
	}
	if (dao->rows < 1)
		return 0;
        if (ds->kth_smallest)
                kth_smallest (wbc, data, ds->k_smallest, labels, dao);

	/* labels contains char* and data contains g_array of gnum_float, passing TRUE should */
	/* dispose that memory correctly                                                      */
	g_ptr_array_free (labels, TRUE);
	g_ptr_array_free (data, TRUE);
	if (basic_stats != NULL)
		g_array_free(basic_stats, TRUE);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

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


/* Returns 1 if error occurred, for example random sample size is
 * larger than the data set.
 **/
int
sampling_tool (WorkbookControl *wbc, Sheet *sheet, Range *range,
	       gboolean periodic_flag, int size,
	       data_analysis_output_t *dao)
{
        GArray * data = g_array_new (FALSE, FALSE, sizeof (gnum_float));
	GArray * sample = g_array_new (FALSE, FALSE, sizeof (gnum_float));
	int i, data_len;
	gnum_float x;

	prepare_output (wbc, dao, _("Sample"));
	get_data_by_cb (sheet, range, data);
	data_len =  data->len;

	if (periodic_flag) {
		if ((size < 0) || (size > data_len))
			return 1;
		for (i = size - 1; i < data_len; i += size) {
			x = g_array_index (data, gnum_float, i);
			g_array_append_val (sample, x);
		}
		write_data (wbc, dao, sample, FALSE);
	} else {
		if ((size < 0) || (size > data_len))
			return 1;

		for (i = 0; i < size; i++) {
			int random_index;

			random_index = random_01 () * data_len;
			if (random_index == data_len)
				random_index--;
			x = g_array_index (data, gnum_float, random_index);
			g_array_remove_index_fast (data, random_index);
			g_array_append_val (sample, x);
		}

		write_data (wbc, dao, sample, TRUE);
	}


	g_array_free (data, TRUE);
	g_array_free (sample, TRUE);

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
ztest_tool (WorkbookControl *wbc, Sheet *sheet, Range *input_range1,
	    Range *input_range2, gnum_float mean_diff, gnum_float known_var1,
	    gnum_float known_var2, gnum_float alpha,
	    data_analysis_output_t *dao)
{
        data_set_t set_one, set_two;
	gnum_float mean1, mean2, var1, var2, z, p;

	prepare_output (wbc, dao, _("z-Test"));

	get_data (sheet, input_range1, &set_one);
	get_data (sheet, input_range2, &set_two);

        set_cell (dao, 0, 0, "");

	if (dao->labels_flag) {
		Cell *cell;

		cell = sheet_cell_get (sheet, input_range1->start.col,
				       input_range1->start.row);
		if (cell != NULL && cell->value != NULL)
			set_cell_value (dao, 1, 0, value_duplicate (cell->value));

		cell = sheet_cell_get (sheet, input_range2->start.col,
				       input_range2->start.row);
		if (cell != NULL && cell->value != NULL)
			set_cell_value (dao, 2, 0, value_duplicate (cell->value));

		input_range1->start.row++;
		input_range2->start.row++;
	} else {
	        set_cell (dao, 1, 0, _("Variable 1"));
		set_cell (dao, 2, 0, _("Variable 2"));
	}

        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Known Variance"
					"/Observations"
					"/Hypothesized Mean Difference"
					"/z"
					"/P (Z<=z) one-tail"
					"/z Critical one-tail"
					"/P (Z<=z) two-tail"
					"/z Critical two-tail"));

	mean1 = set_one.sum / set_one.n;
	mean2 = set_two.sum / set_two.n;
	var1 = (set_one.sqrsum - set_one.sum2 / set_one.n) / (set_one.n - 1);
	var2 = (set_two.sqrsum - set_two.sum2 / set_two.n) / (set_two.n - 1);
	z = (mean1 - mean2 - mean_diff) /
		sqrt (known_var1 / set_one.n + known_var2 / set_two.n);
	p = 1 - pnorm (z, 0, 1);

	/* Mean */
	set_cell_float (dao, 1, 1, mean1);
	set_cell_float (dao, 2, 1, mean2);

	/* Known Variance */
	set_cell_float (dao, 1, 2, known_var1);
	set_cell_float (dao, 2, 2, known_var2);

	/* Observations */
	set_cell_int (dao, 1, 3, set_one.n);
	set_cell_int (dao, 2, 3, set_two.n);

	/* Hypothesized Mean Difference */
	set_cell_float (dao, 1, 4, mean_diff);

	/* z */
	set_cell_float (dao, 1, 5, z);

	/* P (Z<=z) one-tail */
	set_cell_float (dao, 1, 6, p);

	/* z Critical one-tail */
	set_cell_float (dao, 1, 7, qnorm (alpha, 0, 1));

	/* P (Z<=z) two-tail */
	set_cell_float (dao, 1, 8, 2 * p);

	/* z Critical two-tail */
	set_cell_float (dao, 1, 9, qnorm (1.0 - (1.0 - alpha) / 2, 0, 1));

	free_data_set (&set_one);
	free_data_set (&set_two);

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
ttest_paired_tool (WorkbookControl *wbc, Sheet *sheet, Range *input_range1,
		   Range *input_range2, gnum_float mean_diff, gnum_float alpha,
		   data_analysis_output_t *dao)
{
        data_set_t set_one, set_two;
	GSList     *current_one, *current_two;
	gnum_float    mean1, mean2, pearson, var1, var2, t, p, df, sum_xy, sum;
	gnum_float    dx, dm, M, Q, N, s;

	get_data (sheet, input_range1, &set_one);
	get_data (sheet, input_range2, &set_two);

	if (set_one.n != set_two.n) {
	        free_data_set (&set_one);
		free_data_set (&set_two);
	        return 1;
	}

	prepare_output (wbc, dao, _("t-Test"));

        set_cell (dao, 0, 0, "");

	if (dao->labels_flag) {
		Cell *cell;

		cell = sheet_cell_get (sheet, input_range1->start.col,
				       input_range1->start.row);
		if (cell != NULL && cell->value != NULL)
			set_cell_value (dao, 1, 0, value_duplicate (cell->value));

		cell = sheet_cell_get (sheet, input_range2->start.col,
				       input_range2->start.row);
		if (cell != NULL && cell->value != NULL)
			set_cell_value (dao, 2, 0, value_duplicate (cell->value));

		input_range1->start.row++;
		input_range2->start.row++;
	} else {
	        set_cell (dao, 1, 0, _("Variable 1"));
		set_cell (dao, 2, 0, _("Variable 2"));
	}

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

	current_one = set_one.array;
	current_two = set_two.array;
	sum = sum_xy = 0;
	dx = dm = M = Q = N = 0;

	while (current_one != NULL && current_two != NULL) {
	        gnum_float x, y, d;

		x = * ((gnum_float *) current_one->data);
		y = * ((gnum_float *) current_two->data);
		sum_xy += x * y;
		d = x - y;
		sum += d;
		dx = d - M;
		dm = dx / (N + 1);
		M += dm;
		Q += N * dx * dm;
		N++;
	        current_one = current_one->next;
	        current_two = current_two->next;
	}

	mean1 = set_one.sum / set_one.n;
	mean2 = set_two.sum / set_two.n;

	var1 = (set_one.sqrsum - set_one.sum2 / set_one.n) / (set_one.n - 1);
	var2 = (set_two.sqrsum - set_two.sum2 / set_two.n) / (set_two.n - 1);

	df = set_one.n - 1;
	pearson = ((set_one.n * sum_xy - set_one.sum * set_two.sum) /
		   sqrt ((set_one.n * set_one.sqrsum - set_one.sum2) *
			 (set_one.n * set_two.sqrsum - set_two.sum2)));
	s = sqrt (Q / (N - 1));
	t = (sum / set_one.n - mean_diff) / (s / sqrt (set_one.n));
	p = 1.0 - pt (fabs (t), df);

	/* Mean */
	set_cell_float (dao, 1, 1, mean1);
	set_cell_float (dao, 2, 1, mean2);

	/* Variance */
	set_cell_float (dao, 1, 2, var1);
	set_cell_float (dao, 2, 2, var2);

	/* Observations */
	set_cell_int (dao, 1, 3, set_one.n);
	set_cell_int (dao, 2, 3, set_two.n);

	/* Pearson Correlation */
	set_cell_float (dao, 1, 4, pearson);

	/* Hypothesized Mean Difference */
	set_cell_float (dao, 1, 5, mean_diff);

	/* df */
	set_cell_float (dao, 1, 6, df);

	/* t */
	set_cell_float (dao, 1, 7, t);

	/* P (T<=t) one-tail */
	set_cell_float (dao, 1, 8, p);

	/* t Critical one-tail */
	set_cell_float (dao, 1, 9, qt (alpha, df));

	/* P (T<=t) two-tail */
	set_cell_float (dao, 1, 10, 2 * p);

	/* t Critical two-tail */
	set_cell_float (dao, 1, 11, qt (1.0 - (1.0 - alpha) / 2, df));

        free_data_set (&set_one);
        free_data_set (&set_two);

	set_italic (dao, 0, 0, 0, 11);
	set_italic (dao, 0, 0, 2, 0);

	autofit_column (dao, 0);
	autofit_column (dao, 1);
	autofit_column (dao, 2);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
}


/* t-Test: Two-Sample Assuming Equal Variances.
 */
int
ttest_eq_var_tool (WorkbookControl *wbc, Sheet *sheet, Range *input_range1,
		   Range *input_range2, gnum_float mean_diff, gnum_float alpha,
		   data_analysis_output_t *dao)
{
        data_set_t set_one, set_two;
	gnum_float    mean1, mean2, var, var1, var2, t, p, df;

	prepare_output (wbc, dao, _("t-Test"));

	get_data (sheet, input_range1, &set_one);
	get_data (sheet, input_range2, &set_two);

        set_cell (dao, 0, 0, "");

	if (dao->labels_flag) {
		Cell *cell;

		cell = sheet_cell_get (sheet, input_range1->start.col,
				       input_range1->start.row);
		if (cell != NULL && cell->value != NULL)
			set_cell_value (dao, 1, 0, value_duplicate (cell->value));

		cell = sheet_cell_get (sheet, input_range2->start.col,
				       input_range2->start.row);
		if (cell != NULL && cell->value != NULL)
			set_cell_value (dao, 2, 0, value_duplicate (cell->value));

		input_range1->start.row++;
		input_range2->start.row++;
	} else {
	        set_cell (dao, 1, 0, _("Variable 1"));
		set_cell (dao, 2, 0, _("Variable 2"));
	}

        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/Pooled Variance"
					"/Hypothesized Mean Difference"
					"/df"
					"/t Stat"
					"/P (T<=t) one-tail"
					"/t Critical one-tail"
					"/P (T<=t) two-tail"
					"/t Critical two-tail"));

	mean1 = set_one.sum / set_one.n;
	mean2 = set_two.sum / set_two.n;

	var1 = (set_one.sqrsum - set_one.sum2 / set_one.n) / (set_one.n - 1);
	var2 = (set_two.sqrsum - set_two.sum2 / set_two.n) / (set_two.n - 1);

	df = set_one.n + set_two.n - 2;

	var = ((set_one.sqrsum - set_one.sum2 / set_one.n) +
	       (set_two.sqrsum - set_two.sum2 / set_two.n)) / df;

	t = fabs (mean1 - mean2 - mean_diff) /
		sqrt (var / set_one.n + var / set_two.n);
	p = 1.0 - pt (t, df);

	/* Mean */
	set_cell_float (dao, 1, 1, mean1);
	set_cell_float (dao, 2, 1, mean2);

	/* Variance */
	set_cell_float (dao, 1, 2, var1);
	set_cell_float (dao, 2, 2, var2);

	/* Observations */
	set_cell_int (dao, 1, 3, set_one.n);
	set_cell_int (dao, 2, 3, set_two.n);

	/* Pooled Variance */
	set_cell_float (dao, 1, 4, var);

	/* Hypothesized Mean Difference */
	set_cell_float (dao, 1, 5, mean_diff);

	/* df */
	set_cell_float (dao, 1, 6, df);

	/* t */
	set_cell_float (dao, 1, 7, t);

	/* P (T<=t) one-tail */
	set_cell_float (dao, 1, 8, p);

	/* t Critical one-tail */
	set_cell_float (dao, 1, 9, qt (alpha, df));

	/* P (T<=t) two-tail */
	set_cell_float (dao, 1, 10, 2 * p);

	/* t Critical two-tail */
	set_cell_float (dao, 1, 11, qt (1.0 - (1.0 - alpha) / 2, df));

        free_data_set (&set_one);
        free_data_set (&set_two);

	set_italic (dao, 0, 0, 0, 11);
	set_italic (dao, 0, 0, 2, 0);

	autofit_column (dao, 0);
	autofit_column (dao, 1);
	autofit_column (dao, 2);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
}


/* t-Test: Two-Sample Assuming Unequal Variances.
 */
int
ttest_neq_var_tool (WorkbookControl *wbc, Sheet *sheet, Range *input_range1,
		    Range *input_range2, gnum_float mean_diff, gnum_float alpha,
		    data_analysis_output_t *dao)
{
        data_set_t set_one, set_two;
	gnum_float    mean1, mean2, var1, var2, t, p, df, c;

	prepare_output (wbc, dao, _("t-Test"));

	get_data (sheet, input_range1, &set_one);
	get_data (sheet, input_range2, &set_two);

        set_cell (dao, 0, 0, "");

	if (dao->labels_flag) {
		Cell *cell;

		cell = sheet_cell_get (sheet, input_range1->start.col,
				       input_range1->start.row);
		if (cell != NULL && cell->value != NULL)
			set_cell_value (dao, 1, 0, value_duplicate (cell->value));

		cell = sheet_cell_get (sheet, input_range2->start.col,
				       input_range2->start.row);
		if (cell != NULL && cell->value != NULL)
			set_cell_value (dao, 2, 0, value_duplicate (cell->value));

		input_range1->start.row++;
		input_range2->start.row++;
	} else {
	        set_cell (dao, 1, 0, _("Variable 1"));
		set_cell (dao, 2, 0, _("Variable 2"));
	}

        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/Hypothesized Mean Difference"
					"/df"
					"/t Stat"
					"/P (T<=t) one-tail"
					"/t Critical one-tail"
					"/P (T<=t) two-tail"
					"/t Critical two-tail"));

	mean1 = set_one.sum / set_one.n;
	mean2 = set_two.sum / set_two.n;

	var1 = (set_one.sqrsum - set_one.sum2 / set_one.n) / (set_one.n - 1);
	var2 = (set_two.sqrsum - set_two.sum2 / set_two.n) / (set_two.n - 1);

	c = (var1 / set_one.n) / (var1 / set_one.n + var2 / set_two.n);
	df = 1.0 / ((c * c) / (set_one.n - 1.0) + ((1 - c)* (1 - c)) / (set_two.n - 1.0));

	t = fabs (mean1 - mean2 - mean_diff) /
		sqrt (var1 / set_one.n + var2 / set_two.n);
	p = 1.0 - pt (t, df);

	/* Mean */
	set_cell_float (dao, 1, 1, mean1);
	set_cell_float (dao, 2, 1, mean2);

	/* Variance */
	set_cell_float (dao, 1, 2, var1);
	set_cell_float (dao, 2, 2, var2);

	/* Observations */
	set_cell_int (dao, 1, 3, set_one.n);
	set_cell_int (dao, 2, 3, set_two.n);

	/* Hypothesized Mean Difference */
	set_cell_float (dao, 1, 4, mean_diff);

	/* df */
	set_cell_float (dao, 1, 5, df);

	/* t */
	set_cell_float (dao, 1, 6, t);

	/* P (T<=t) one-tail */
	set_cell_float (dao, 1, 7, p);

	/* t Critical one-tail */
	set_cell_float (dao, 1, 8, qt (alpha, df));

	/* P (T<=t) two-tail */
	set_cell_float (dao, 1, 9, 2 * p);

	/* t Critical two-tail */
	set_cell_float (dao, 1, 10, qt (1.0 - (1.0 - alpha) / 2, df));

        free_data_set (&set_one);
        free_data_set (&set_two);

	set_italic (dao, 0, 0, 0, 11);
	set_italic (dao, 0, 0, 2, 0);

	autofit_column (dao, 0);
	autofit_column (dao, 1);
	autofit_column (dao, 2);

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
ftest_tool (WorkbookControl *wbc, Sheet *sheet, Range *input_range1,
	    Range *input_range2, gnum_float alpha,
	    data_analysis_output_t *dao)
{
        data_set_t set_one, set_two;
	gnum_float    mean1, mean2, var1, var2, f, p, df1, df2, c;

	prepare_output (wbc, dao, _("F-Test"));

        set_cell (dao, 0, 0, "");

	if (dao->labels_flag) {
		Cell *cell;

		cell = sheet_cell_get (sheet, input_range1->start.col,
				       input_range1->start.row);
		if (cell != NULL && cell->value != NULL)
			set_cell_value (dao, 1, 0, value_duplicate (cell->value));

		cell = sheet_cell_get (sheet, input_range2->start.col,
				       input_range2->start.row);
		if (cell != NULL && cell->value != NULL)
			set_cell_value (dao, 2, 0, value_duplicate (cell->value));

		input_range1->start.row++;
		input_range2->start.row++;
	} else {
	        set_cell (dao, 1, 0, _("Variable 1"));
		set_cell (dao, 2, 0, _("Variable 2"));
	}

	get_data (sheet, input_range1, &set_one);
	get_data (sheet, input_range2, &set_two);

        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/df"
					"/F"
					"/P (F<=f) one-tail"
					"/F Critical one-tail"));

	mean1 = set_one.sum / set_one.n;
	mean2 = set_two.sum / set_two.n;

	var1 = (set_one.sqrsum - set_one.sum2 / set_one.n) / (set_one.n - 1);
	var2 = (set_two.sqrsum - set_two.sum2 / set_two.n) / (set_two.n - 1);

	c = (var1 / set_one.n) / (var1 / set_one.n + var2 / set_two.n);
	df1 = set_one.n - 1;
	df2 = set_two.n - 1;

	f = var1 / var2;
	p = 1.0 - pf (f, df1, df2);

	/* Mean */
	set_cell_float (dao, 1, 1, mean1);
	set_cell_float (dao, 2, 1, mean2);

	/* Variance */
	set_cell_float (dao, 1, 2, var1);
	set_cell_float (dao, 2, 2, var2);

	/* Observations */
	set_cell_int (dao, 1, 3, set_one.n);
	set_cell_int (dao, 2, 3, set_two.n);

	/* df */
	set_cell_float (dao, 1, 4, df1);
	set_cell_float (dao, 2, 4, df2);

	/* F */
	set_cell_float (dao, 1, 5, f);

	/* P (F<=f) one-tail */
	set_cell_float (dao, 1, 6, p);

	/* F Critical one-tail */
	set_cell_float (dao, 1, 7, qf (alpha, df1, df2));

        free_data_set (&set_one);
        free_data_set (&set_two);

	set_italic (dao, 0, 0, 0, 11);
	set_italic (dao, 0, 0, 2, 0);

	autofit_column (dao, 0);
	autofit_column (dao, 1);
	autofit_column (dao, 2);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
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
	int        i, n, j;
	gnum_float    range, p, tmp;
	gnum_float    *prob, *cumul_p;
	Value      **values, *v;
	Cell       *cell;

	prepare_output (wbc, dao, _("Random"));

	switch (distribution) {
	case DiscreteDistribution:
	        n = param->discrete.end_row-param->discrete.start_row + 1;
	        prob = g_new (gnum_float, n);
		cumul_p = g_new (gnum_float, n);
		values = g_new (Value *, n);

                p = 0;
		j = 0;
	        for (i = param->discrete.start_row;
		     i <= param->discrete.end_row; i++, j++) {
		        cell = sheet_cell_get (sheet,
					       param->discrete.start_col + 1, i);
			if (cell != NULL && cell->value != NULL) {
			        v = cell->value;
				if (VALUE_IS_NUMBER (v))
				        prob[j] = value_get_as_float (v);
				else {
				        g_free (prob);
					g_free (cumul_p);
					g_free (values);

					return 1;
				}
			} else {
			        g_free (prob);
				g_free (cumul_p);
				g_free (values);

			        return 1;
			}

			p += prob[j];
			cumul_p[j] = p;

		        cell = sheet_cell_get (sheet,
					       param->discrete.start_col, i);
			if (cell != NULL && cell->value != NULL)
			        values[j] = value_duplicate (cell->value);
			else {
			        g_free (prob);
				g_free (cumul_p);
				g_free (values);

			        return 1;
			}
		}

		if (p != 1) {
		        g_free (prob);
			g_free (cumul_p);
			g_free (values);

		        return 2;
		}

	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
			        gnum_float x = random_01 ();

				for (j = 0; cumul_p[j] < x; j++)
				        ;

				set_cell_value (dao, i, n, value_duplicate (values[j]));
			}
		}

		/* FIXME: Leak! */

	        break;
	case NormalDistribution:
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = param->normal.stdev * random_normal () + param->normal.mean;
				set_cell_float (dao, i, n, v);
			}
		}
	        break;
	case BernoulliDistribution:
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
			        tmp = random_bernoulli (param->bernoulli.p);
				set_cell_int (dao, i, n, (int)tmp);
			}
		}
	        break;
	case UniformDistribution:
	        range = param->uniform.upper_limit - param->uniform.lower_limit;
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
				v = range * random_01 () + param->uniform.lower_limit;
				set_cell_float (dao, i, n, v);
			}
		}
		break;
	case PoissonDistribution:
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_poisson (param->poisson.lambda);
				set_cell_float (dao, i, n, v);
			}
		}
	        break;
	case ExponentialDistribution:
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_exponential (param->exponential.b);
				set_cell_float (dao, i, n, v);
			}
		}
	        break;
	case BinomialDistribution:
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_binomial (param->binomial.p,
						     param->binomial.trials);
				set_cell_float (dao, i, n, v);
			}
		}
	        break;
	case NegativeBinomialDistribution:
	        for (i = 0; i < vars; i++) {
		        for (n = 0; n < count; n++) {
				gnum_float v;
			        v = random_negbinom (param->negbinom.p,
						     param->negbinom.f);
				set_cell_float (dao, i, n, v);
			}
		}
	        break;
	default:
	        printf (_("Not implemented yet.\n"));
		break;
	}

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

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
regression_tool (WorkbookControl *wbc, Sheet *sheet, Range *input_range_y,
		 Range *input_range_xs, gnum_float alpha,
		 data_analysis_output_t *dao, int intercept, int xdim)
{
        data_set_t          set_y, *setxs;
	gnum_float             mean_y, *mean_xs;
	gnum_float             r, ss_xy, *ss_xx, ss_yy, ss_xx_total;
	gnum_float             *res, *ys, **xss;
	int                 i, j, err;
	GSList              *current_x, *current_y;
	regression_stat_t   extra_stat;

	setxs = g_new (data_set_t, xdim);
	get_data (sheet, input_range_y, &set_y);
	for (i = 0; i < xdim; i++)
		get_data (sheet, &input_range_xs[i], &setxs[i]);

	for (i = 0; i < xdim; i++) {
		if (setxs[i].n != set_y.n) {
			free_data_set (&set_y);
			for (i = 0; i < xdim; i++)
				free_data_set (&setxs[i]);
			g_free (setxs);
			return 3; /* Different number of data points */
		}
	}

	xss = g_new (gnum_float *, xdim);
	res = g_new (gnum_float, xdim + 1);
	mean_xs = g_new (gnum_float, xdim);

	for (i = 0; i < xdim; i++) {
		j = 0;
		xss[i] = g_new (gnum_float, set_y.n);
		current_x = setxs[i].array;
		while (current_x != NULL) {
			xss[i][j] = * ((gnum_float *) current_x->data);
			current_x = current_x->next;
			j++;
		}
	}

	ys = g_new (gnum_float, set_y.n);
	current_y = set_y.array;
	i = 0;
	while (current_y != NULL) {
		ys[i] = * ((gnum_float *) current_y->data);
		current_y = current_y->next;
		i++;
	}

	err = linear_regression (xss, xdim, ys, set_y.n,
				 intercept, res, &extra_stat);

	if (err) return err;

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

	for (i = 1; i <= xdim; i++)
		set_cell_printf (dao, 0, 16 + i, _("X Variable %d"), i);

        set_cell_text_row (dao, 1, 10, _("/df"
					 "/SS"
					 "/MS"
					 "/F"
					 "/Significance F"));

        set_cell_text_row (dao, 1, 15, _("/Coefficients"
					 "/Standard Error"
					 "/t Stat"
					 "/P-value"
					 "/Lower 95%"
					 "/Upper 95%"));

	for (i = 0; i < xdim; i++)
		mean_xs[i] = setxs[i].sum / setxs[i].n;
	mean_y = set_y.sum / set_y.n;

	ss_xx = g_new (gnum_float, xdim);
	ss_xy = ss_yy = ss_xx_total = 0;
	current_y = set_y.array;
	while (current_y != NULL) {
		gnum_float y = * ((gnum_float *) current_y->data);
		ss_yy += (y - mean_y) * (y - mean_y);
		current_y = current_y->next;
	}

	for (i = 0; i < xdim; i++) {
		ss_xx[i] = 0;
		current_x = setxs[i].array;
		current_y = set_y.array;
		while (current_x != NULL && current_y != NULL) {
		        gnum_float x, y;

			x = * ((gnum_float *) current_x->data);
			y = * ((gnum_float *) current_y->data);
		        ss_xy += (x - mean_xs[i]) * (y - mean_y);
			ss_xx[i] += (x - mean_xs[i]) * (x - mean_xs[i]);
		        current_y = current_y->next;
		        current_x = current_x->next;
		}
		ss_xx_total += ss_xx[i];
	}

	if (xdim == 1)
		r = ss_xy / sqrt (ss_xx[0] * ss_yy);
	else r = sqrt (extra_stat.sqr_r); /* Can this be negative? */

/* TODO User-defined confidence intervals (alpha). Also, figure out
   how to write values to cells in scientific notation, since many of
these values can be tiny.*/

	/* Multiple R */
	set_cell_float (dao, 1, 3, r);

	/* R Square */
	set_cell_float (dao, 1, 4, extra_stat.sqr_r);

	/* Adjusted R Square */
	set_cell_float (dao, 1, 5, extra_stat.adj_sqr_r);

	/* Standard Error */
	set_cell_float (dao, 1, 6, sqrt (extra_stat.var));

	/* Observations */
	set_cell_float (dao, 1, 7, set_y.n);

	/* Regression / df */
	set_cell_float (dao, 1, 11, xdim);

	/* Residual / df */
	set_cell_float (dao, 1, 12, set_y.n - intercept - xdim);

	/* Total / df */
	set_cell_float (dao, 1, 13, set_y.n - intercept);

	/* Residual / SS */
	set_cell_float (dao, 2, 12, extra_stat.ss_resid);

	/* Total / SS */
	set_cell_float (dao, 2, 13, ss_yy);

	/* Regression / SS */
	set_cell_float (dao, 2, 11, ss_yy - extra_stat.ss_resid);

	/* Regression / MS */
	set_cell_float (dao, 3, 11, (ss_yy - extra_stat.ss_resid) / xdim);

	/* Residual / MS */
	set_cell_float (dao, 3, 12, extra_stat.ss_resid / (set_y.n - 1 - xdim));

	/* F */
	set_cell_float (dao, 4, 11, extra_stat.F);

	/* Significance of F */
	set_cell_float (dao, 5, 11, 1 - pf (extra_stat.F, xdim - intercept,
					    set_y.n - xdim));

	/* Intercept / Coefficient */
	set_cell_float (dao, 1, 16, res[0]);

	if (!intercept)
		for (i = 2; i <= 6; i++)
			set_cell_na (dao, i, 16);
	else {
		gnum_float t;

		t = qt (1 - 0.025, set_y.n - xdim - 1);

		/* Intercept / Standard Error */
		set_cell_float (dao, 2, 16, extra_stat.se[0]);

		/* Intercept / t Stat */
		set_cell_float (dao, 3, 16, extra_stat.t[0]);

		/* Intercept / p values */
		set_cell_float (dao, 4, 16, 2.0 * (1.0 - pt (extra_stat.t[0],
							     set_y.n - xdim - 1)));

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
						 set_y.n - xdim - intercept)));

		t = qt (1 - 0.025, set_y.n - xdim - intercept);

		/* Slope / Lower 95% */
		set_cell_float (dao, 5, 17 + i,
				res[i + 1] - t * extra_stat.se[intercept + i]);

		/* Slope / Upper 95% */
		set_cell_float (dao, 6, 17 + i,
				res[i + 1] + t * extra_stat.se[intercept + i]);
	}

	for (i = 0; i < xdim; i++) {
		free_data_set (&setxs[i]);
	}
	g_free (setxs);
	g_free (extra_stat.se);
	g_free (extra_stat.xbar);
	g_free (extra_stat.t);
	g_free (mean_xs);
	g_free (ss_xx);
	g_free (ys);
	free_data_set (&set_y);
	for (i = 0; i < xdim; i++)
		g_free (xss[i]);
	g_free (xss);
	g_free (res);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

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
average_tool (WorkbookControl *wbc, Sheet *sheet, Range *range, int interval,
	      int std_error_flag, data_analysis_output_t *dao)
{
        data_set_t data_set;
	GSList     *current;
	gnum_float    *prev, sum;
	int        cols, rows, row, add_cursor, del_cursor, count;

	/* TODO: Standard error output */
	cols = range->end.col - range->start.col + 1;
	rows = range->end.row - range->start.row + 1;

	if ((cols != 1 && rows != 1) || interval < 1)
	        return 1;

	prepare_output (wbc, dao, _("Moving Averages"));

	prev = g_new (gnum_float, interval);

	get_data (sheet, range, &data_set);
	current = data_set.array;
	count = add_cursor = del_cursor = row = 0;
	sum = 0;

	while (current != NULL) {
	        prev[add_cursor] = * ((gnum_float *) current->data);
		if (count == interval - 1) {
			sum += prev[add_cursor];
			set_cell_float (dao, 0, row, sum / interval);
			++row;
		        sum -= prev[del_cursor];
			if (++add_cursor == interval)
			        add_cursor = 0;
			if (++del_cursor == interval)
			        del_cursor = 0;
		} else {
		        ++count;
		        sum += prev[add_cursor];
			++add_cursor;
			set_cell_na (dao, 0, row++);
		}
		current = current->next;
	}

	g_free (prev);

	free_data_set (&data_set);

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
ranking_tool (WorkbookControl *wbc, Sheet *sheet, Range *input_range,
	      int columns_flag, data_analysis_output_t *dao)
{
        data_set_t *data_sets;
	GSList     *current, *inner;
	int        vars, cols, rows, col, i, n;

	prepare_output (wbc, dao, _("Ranks"));

	cols = input_range->end.col - input_range->start.col + 1;
	rows = input_range->end.row - input_range->start.row + 1;

	if (columns_flag) {
	        vars = cols;
		for (col = 0; col < vars; col++) {
		        set_cell (dao, col * 4, 0, _("Point"));
			if (dao->labels_flag) {
			        Cell *cell = sheet_cell_get
					(sheet, input_range->start.col + col,
					 input_range->start.row);
				if (cell != NULL && cell->value != NULL)
				        set_cell_value (dao, col * 4 + 1, 0, value_duplicate (cell->value));
			} else {
				set_cell_printf (dao, col * 4 + 1, 0, _("Column %d"), col + 1);
			}
			set_cell (dao, col * 4 + 2, 0, _("Rank"));
			set_cell (dao, col * 4 + 3, 0, _("Percent"));
		}
		data_sets = g_new (data_set_t, vars);

		if (dao->labels_flag)
		        input_range->start.row++;

		for (i = 0; i < vars; i++)
		        get_data_groupped_by_columns (sheet,
						      input_range, i,
						      &data_sets[i]);
	} else {
	        vars = rows;
		for (col = 0; col < vars; col++) {
		        set_cell (dao, col * 4, 0, _("Point"));

			if (dao->labels_flag) {
			        Cell *cell = sheet_cell_get
					(sheet, input_range->start.col,
					 input_range->start.row + col);
				if (cell != NULL && cell->value != NULL)
				        set_cell_value (dao, col * 4 + 1, 0, value_duplicate (cell->value));
			} else {
				set_cell_printf (dao, col * 4 + 1, 0, _("Row %d"), col + 1);
			}
			set_cell (dao, col * 4 + 2, 0, _("Rank"));
			set_cell (dao, col * 4 + 3, 0, _("Percent"));
		}
		data_sets = g_new (data_set_t, vars);

		if (dao->labels_flag)
		        input_range->start.col++;

		for (i = 0; i < vars; i++)
		        get_data_groupped_by_rows (sheet,
						   input_range, i,
						   &data_sets[i]);
	}

	for (i = 0; i < vars; i++) {
	        rank_t *rank;
	        n = 0;
	        current = data_sets[i].array;
		rank = g_new (rank_t, data_sets[i].n);

		while (current != NULL) {
		        gnum_float x = * ((gnum_float *) current->data);

			rank[n].point = n + 1;
			rank[n].x = x;
			rank[n].rank = 1;
			rank[n].same_rank_count = -1;

			inner = data_sets[i].array;
			while (inner != NULL) {
			        gnum_float y = * ((gnum_float *) inner->data);
				if (y > x)
				        rank[n].rank++;
				else if (y == x)
				        rank[n].same_rank_count++;
				inner = inner->next;
			}
			n++;
			current = current->next;
		}
		qsort (rank, data_sets[i].n,
		       sizeof (rank_t), (void *) &rank_compare);

		for (n = 0; n < data_sets[i].n; n++) {
			/* Point number */
			set_cell_int (dao, i * 4 + 0, n + 1, rank[n].point);

			/* Value */
			set_cell_float (dao, i * 4 + 1, n + 1, rank[n].x);

			/* Rank */
			set_cell_int (dao, i * 4 + 2, n + 1, rank[n].rank);

			/* Percent */
			set_cell_printf (dao, i * 4 + 3, n + 1,
					 "%.2f%%",
					 100.0 - (100.0 * (rank[n].rank - 1)/
						  (data_sets[i].n - 1)));
		}
		g_free (rank);
	}

	for (i = 0; i < vars; i++)
	        free_data_set (&data_sets[i]);
	g_free (data_sets);

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
anova_single_factor_tool (WorkbookControl *wbc, Sheet *sheet, Range *range,
			  int columns_flag, gnum_float alpha,
			  data_analysis_output_t *dao)
{
        data_set_t *data_sets;
	int        vars, cols, rows, col, i;
	gnum_float *mean, mean_total, sum_total, n_total, ssb, ssw, sst;
	gnum_float ms_b, ms_w, f, p, f_c;
	int        df_b, df_w, df_t;

	prepare_output (wbc, dao, _("Anova"));

	cols = range->end.col - range->start.col + 1;
	rows = range->end.row - range->start.row + 1;

	set_cell (dao, 0, 0, _("Anova: Single Factor"));
	set_cell (dao, 0, 2, _("SUMMARY"));
	set_cell_text_row (dao, 0, 3, _("/Groups"
					"/Count"
					"/Sum"
					"/Average"
					"/Variance"));

	if (columns_flag) {
	        vars = cols;
		for (col = 0; col < vars; col++) {
			if (dao->labels_flag) {
			        Cell *cell = sheet_cell_get
					(sheet, range->start.col + col,
					 range->start.row);
				if (cell != NULL && cell->value != NULL)
				        set_cell_value (dao, 0, col + 4, value_duplicate (cell->value));
			} else {
				set_cell_printf (dao, 0, col + 4, _("Column %d"), col + 1);
			}
		}
		data_sets = g_new (data_set_t, vars);

		if (dao->labels_flag)
		        range->start.row++;

		for (i = 0; i < vars; i++)
		        get_data_groupped_by_columns (sheet,
						      range, i,
						      &data_sets[i]);
	} else {
	        vars = rows;
		for (col = 0; col < vars; col++) {
			if (dao->labels_flag) {
			        Cell *cell = sheet_cell_get
					(sheet, range->start.col,
					 range->start.row + col);
				if (cell != NULL && cell->value != NULL)
				        set_cell_value (dao, 0, col + 4, value_duplicate (cell->value));
			} else {
				set_cell_printf (dao, 0, col + 4, _("Row %d"), col + 1);
			}
		}
		data_sets = g_new (data_set_t, vars);

		if (dao->labels_flag)
		        range->start.col++;

		for (i = 0; i < vars; i++)
		        get_data_groupped_by_rows (sheet,
						   range, i,
						   &data_sets[i]);
	}

	/* SUMMARY */
	for (i = 0; i < vars; i++) {
	        gnum_float v;

	        /* Count */
		set_cell_int (dao, 1, i + 4, data_sets[i].n);

		/* Sum */
		set_cell_float (dao, 2, i + 4, data_sets[i].sum);

		/* Average */
		set_cell_float (dao, 3, i + 4, data_sets[i].sum / data_sets[i].n);

		/* Variance */
		v = (data_sets[i].sqrsum - data_sets[i].sum2 / data_sets[i].n) /
			(data_sets[i].n - 1);
		set_cell_float (dao, 4, i + 4, v);
	}

	set_cell_text_col (dao, 0, vars + 6, _("/ANOVA"
					       "/Source of Variation"
					       "/Between Groups"
					       "/Within Groups"
					       "/Total"));
	set_cell_text_row (dao, 1, vars + 7, _("/SS"
					       "/df"
					       "/MS"
					       "/F"
					       "/P-value"
					       "/F critical"));

	/* ANOVA */
	mean = g_new (gnum_float, vars);
	sum_total = n_total = 0;
	ssb = ssw = sst = 0;
	for (i = 0; i < vars; i++) {
	        mean[i] = data_sets[i].sum / data_sets[i].n;
		sum_total += data_sets[i].sum;
		n_total += data_sets[i].n;
	}
	mean_total = sum_total / n_total;
	for (i = 0; i < vars; i++) {
	        gnum_float t;
		t = mean[i] - mean_total;
		ssb += t * t * data_sets[i].n;
	}
	for (i = 0; i < vars; i++) {
	        GSList  *current = data_sets[i].array;
		gnum_float t, x;

		while (current != NULL) {
			x = * ((gnum_float *) current->data);
			t = x - mean[i];
		        ssw += t * t;
			t = x - mean_total;
			sst += t * t;
			current = current->next;
		}
	}
	df_b = vars - 1;
	df_w = n_total - vars;
	df_t = n_total - 1;
	ms_b = ssb / df_b;
	ms_w = ssw / df_w;
	f    = ms_b / ms_w;
	p    = 1.0 - pf (f, df_b, df_w);
	f_c  = qf (alpha, df_b, df_w);

	set_cell_float (dao, 1, vars + 8, ssb);
	set_cell_float (dao, 1, vars + 9, ssw);
	set_cell_float (dao, 1, vars + 11, sst);
	set_cell_int (dao, 2, vars + 8, df_b);
	set_cell_int (dao, 2, vars + 9, df_w);
	set_cell_int (dao, 2, vars + 11, df_t);
	set_cell_float (dao, 3, vars + 8, ms_b);
	set_cell_float (dao, 3, vars + 9, ms_w);
	set_cell_float (dao, 4, vars + 8, f);
	set_cell_float (dao, 5, vars + 8, p);
	set_cell_float (dao, 6, vars + 8, f_c);

	g_free (mean);

	for (i = 0; i < vars; i++)
	        free_data_set (&data_sets[i]);
	g_free (data_sets);

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
anova_two_factor_without_r_tool (WorkbookControl *wbc, Sheet *sheet, Range *range,
				 gnum_float alpha,
				 data_analysis_output_t *dao)
{
        data_set_t *data_sets;
	int        cols, rows, i, n;
	gnum_float    *row_mean, *col_mean, mean, sum;
	gnum_float    ss_r, ss_c, ss_e, ss_t;
	gnum_float    ms_r, ms_c, ms_e, f1, f2, p1, p2, f1_c, f2_c;
	int        df_r, df_c, df_e, df_t;

	prepare_output (wbc, dao, _("Anova"));

	cols = range->end.col - range->start.col + 1;
	rows = range->end.row - range->start.row + 1;

	set_cell (dao, 0, 0, _("Anova: Two-Factor Without Replication"));
	set_cell_text_row (dao, 0, 2, _("/SUMMARY"
					"/Count"
					"/Sum"
					"/Average"
					"/Variance"));

	data_sets = g_new (data_set_t, cols);
	col_mean = g_new (gnum_float, cols);
	for (i = 0; i < cols; i++) {
	        gnum_float v;

	        get_data_groupped_by_columns (sheet, range, i, &data_sets[i]);
		set_cell_printf (dao, 0, i + 4 + rows, _("Column %d"), i + 1);
		set_cell_int (dao, 1, i + 4 + rows, data_sets[i].n);
		set_cell_float (dao, 2, i + 4 + rows, data_sets[i].sum);
		set_cell_float (dao, 3, i + 4 + rows, data_sets[i].sum / data_sets[i].n);
		v = (data_sets[i].sqrsum - data_sets[i].sum2 / data_sets[i].n) /
			(data_sets[i].n - 1);
		set_cell_float (dao, 4, i + 4 + rows, v);
		col_mean[i] = data_sets[i].sum / data_sets[i].n;
	}
	g_free (data_sets);

	data_sets = g_new (data_set_t, rows);
	row_mean = g_new (gnum_float, rows);
	for (i = n = sum = 0; i < rows; i++) {
	        gnum_float v;

	        get_data_groupped_by_rows (sheet, range, i, &data_sets[i]);
		set_cell_printf (dao, 0, i + 3, _("Row %d"), i + 1);
		set_cell_int (dao, 1, i + 3, data_sets[i].n);
		set_cell_float (dao, 2, i + 3, data_sets[i].sum);
		set_cell_float (dao, 3, i + 3, data_sets[i].sum / data_sets[i].n);
		v = (data_sets[i].sqrsum - data_sets[i].sum2 / data_sets[i].n) /
			(data_sets[i].n - 1);
		set_cell_float (dao, 4, i + 3, v);
		n += data_sets[i].n;
		sum += data_sets[i].sum;
		row_mean[i] = data_sets[i].sum / data_sets[i].n;
	}
	ss_e = ss_t = 0;
	mean = sum / n;
	for (i = 0; i < rows; i++) {
	        GSList *current = data_sets[i].array;
		gnum_float t, x;
		n = 0;

	        while (current != NULL) {
		        x = * ((gnum_float *) current->data);
			t = x - col_mean[n] - row_mean[i] + mean;
			t *= t;
			ss_e += t;
			t = x - mean;
			t *= t;
			ss_t += t;
			n++;
			current = current->next;
		}
	}
	g_free (data_sets);

	ss_r = ss_c = 0;
	for (i = 0; i < rows; i++) {
	        gnum_float t;

		t = row_mean[i] - mean;
		t *= t;
	        ss_r += t;
	}
	ss_r *= cols;
	for (i = 0; i < cols; i++) {
	        gnum_float t;

		t = col_mean[i] - mean;
		t *= t;
	        ss_c += t;
	}
	g_free (col_mean);
	g_free (row_mean);

	ss_c *= rows;

	df_r = rows - 1;
	df_c = cols - 1;
	df_e = (rows - 1) * (cols - 1);
	df_t = (rows * cols) - 1;
	ms_r = ss_r / df_r;
	ms_c = ss_c / df_c;
	ms_e = ss_e / df_e;
	f1   = ms_r / ms_e;
	f2   = ms_c / ms_e;
	p1   = 1.0 - pf (f1, df_r, df_e);
	p2   = 1.0 - pf (f2, df_c, df_e);
	f1_c = qf (alpha, df_r, df_e);
	f2_c = qf (alpha, df_c, df_e);

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
	set_cell_float (dao, 1, 12 + rows + cols, ss_t);
	set_cell_int (dao, 2, 8 + rows + cols, df_r);
	set_cell_int (dao, 2, 9 + rows + cols, df_c);
	set_cell_int (dao, 2, 10 + rows + cols, df_e);
	set_cell_int (dao, 2, 12 + rows + cols, df_t);
	set_cell_float (dao, 3, 8 + rows + cols, ms_r);
	set_cell_float (dao, 3, 9 + rows + cols, ms_c);
	set_cell_float (dao, 3, 10 + rows + cols, ms_e);
	set_cell_float (dao, 4, 8 + rows + cols, f1);
	set_cell_float (dao, 4, 9 + rows + cols, f2);
	set_cell_float (dao, 5, 8 + rows + cols, p1);
	set_cell_float (dao, 5, 9 + rows + cols, p2);
	set_cell_float (dao, 6, 8 + rows + cols, f1_c);
	set_cell_float (dao, 6, 9 + rows + cols, f2_c);

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

int
anova_two_factor_with_r_tool (WorkbookControl *wbc, Sheet *sheet, Range *range,
			      int rows_per_sample, gnum_float alpha,
			      data_analysis_output_t *dao)
{
	int           cols, rows, i, j, k, n, a, b;
	int           count, gr_count, tot_count;
	gnum_float    sum, sqrsum, x, v;
	gnum_float    gr_sum, gr_sqrsum;
	gnum_float    tot_sum, tot_sqrsum;
	int           *col_count;
	gnum_float    *col_sum, *col_sqrsum;
	gnum_float    col_sum_sqrsum, gr_sum_sqrsum, sample_sum_sqrsum;
	gnum_float    df_col, df_gr, df_inter, df_within;
	gnum_float    ss_col, ss_gr, ss_inter, ss_within, ss_tot;
	gnum_float    ms_col, ms_gr, ms_inter, ms_within;
	gnum_float    f_col, f_gr, f_inter;
	gnum_float    p_col, p_gr, p_inter;
	Cell          *cell;

	cols = range->end.col - range->start.col + 1;
	rows = range->end.row - range->start.row + 1;

	/* Check that correct number of rows per sample */
	if (rows_per_sample == 0)
	        return 1;
	if ((rows - 1) % rows_per_sample != 0)
	        return 1;

	/* Check that at least two columns of data are given */
	if (cols < 3)
	        return 3;

	/* Check that the data contains numbers only */
	for (i = 1; i < rows; i++) {
	       for (j = 1; j < cols; j++) {
		       cell = sheet_cell_get (sheet, range->start.col + j,
						     range->start.row + i);
			      if (cell == NULL
				  || !VALUE_IS_NUMBER (cell->value))
				      return 2;
	       }
	}

	prepare_output (wbc, dao, _("Anova"));

	set_cell (dao, 0, 0, _("Anova: Two-Factor With Replication"));
	set_cell (dao, 0, 2, _("SUMMARY"));

	col_count = g_new (int, cols);
	col_sum = g_new (gnum_float, cols);
	col_sqrsum = g_new (gnum_float, cols);

	for (i = 1; i < cols; i++) {
	       cell = sheet_cell_get (sheet, range->start.col+i,
				      range->start.row);
	       if (cell->value != NULL)
		       set_cell_value (dao, i, 2, cell->value);
	       col_count[i - 1] = 0;
	       col_sum[i - 1] = 0;
	       col_sqrsum[i - 1] = 0;
	}
	set_italic (dao, 1, 2, i - 1, 2);
	set_cell (dao, i, 2, _("Total"));

	tot_count = 0;
	tot_sum = 0;
	tot_sqrsum = 0;

	sample_sum_sqrsum = 0;

	gr_sum_sqrsum = 0;

	for (i = n = 0; i < rows - 1; i += rows_per_sample) {
	       cell = sheet_cell_get (sheet, range->start.col,
				      range->start.row + i + 1);

	       /* Print the names of the groups */
	       if (cell != NULL)
		       set_cell_value (dao, 0, n * 6 + 3, cell->value);
	       set_italic (dao, 0, n * 6 + 3, 0, n * 6 + 3);

	       set_cell (dao, 0, n * 6 + 4, "Count");
	       set_cell (dao, 0, n * 6 + 5, "Sum");
	       set_cell (dao, 0, n * 6 + 6, "Average");
	       set_cell (dao, 0, n * 6 + 7, "Variance");

	       gr_count = 0;
	       gr_sum = 0;
	       gr_sqrsum = 0;

	       for (j = 1; j < cols; j++) {
		      count = 0;
		      sum = 0;
		      sqrsum = 0;
		      for (k = 0; k < rows_per_sample; k++) {
			      cell = sheet_cell_get (sheet, range->start.col + j,
						     range->start.row + k + 1 +
						     n * rows_per_sample);
			      ++count;
			      x = value_get_as_float (cell->value);
			      sum += x;
			      sqrsum += x * x;
			      col_count[j - 1]++;
			      col_sum[j - 1] += x;
			      col_sqrsum[j - 1] += x * x;
		      }
		      v = (sqrsum - (sum * sum) / count) / (count - 1);
		      gr_count += count;
		      gr_sum += sum;
		      gr_sqrsum += sqrsum;

  		      sample_sum_sqrsum += sum * sum;

		      tot_count += count;
		      tot_sum += sum;
		      tot_sqrsum += sqrsum;

		      set_cell_int   (dao, j, n * 6 + 4, count);
		      set_cell_float (dao, j, n * 6 + 5, sum);
		      set_cell_float (dao, j, n * 6 + 6, sum / count);
		      set_cell_float (dao, j, n * 6 + 7, v);
	       }
	       v = (gr_sqrsum - (gr_sum * gr_sum) / gr_count) / (gr_count - 1);
	       set_cell_int   (dao, j, n * 6 + 4, gr_count);
	       set_cell_float (dao, j, n * 6 + 5, gr_sum);
	       set_cell_float (dao, j, n * 6 + 6, gr_sum / gr_count);
	       set_cell_float (dao, j, n * 6 + 7, v);

	       gr_sum_sqrsum += (gr_sum * gr_sum);

	       ++n;
	}

	set_cell (dao, 0, n * 6 + 3, _("Total"));
	set_italic (dao, 0, n * 6 + 3, 0, n * 6 + 3);
	set_cell (dao, 0, n * 6 + 4, _("Count"));
	set_cell (dao, 0, n * 6 + 5, _("Sum"));
	set_cell (dao, 0, n * 6 + 6, _("Average"));
	set_cell (dao, 0, n * 6 + 7, _("Variance"));

	col_sum_sqrsum = 0;

	for (i = 1; i < cols; i++) {
	        v = (col_sqrsum[i - 1] - (col_sum[i - 1] * col_sum[i - 1]) /
		     col_count[i - 1]) / (col_count[i - 1] - 1);
	        set_cell_int   (dao, i, n * 6 + 4, col_count[i - 1]);
	        set_cell_float (dao, i, n * 6 + 5, col_sum[i - 1]);
	        set_cell_float (dao, i, n * 6 + 6, col_sum[i - 1] / col_count[i - 1]);
	        set_cell_float (dao, i, n * 6 + 7, v);
		col_sum_sqrsum +=  (col_sum[i - 1] * col_sum[i - 1]);
	}

	a = (rows - 1) / rows_per_sample;
	b = cols -1;
	ss_gr = (gr_sum_sqrsum - (tot_sum * tot_sum) / a) / (b * rows_per_sample) ;
	ss_col = (col_sum_sqrsum - (tot_sum * tot_sum) / b) / (a * rows_per_sample) ;
	ss_within = tot_sqrsum - sample_sum_sqrsum / rows_per_sample;
	ss_tot = tot_sqrsum - (tot_sum * tot_sum) / (a * b * rows_per_sample);
	ss_inter = ss_tot - ss_within - ss_col - ss_gr;

	df_gr = a - 1;
	df_col = b - 1;
	df_inter = (a -1) * (b - 1);
	df_within = a * b * (rows_per_sample - 1);

	ms_gr = ss_gr / df_gr;
	ms_col = ss_col / df_col;
	ms_inter = ss_inter / df_inter;
	ms_within = ss_within / df_within;

	f_gr = ms_gr / ms_within;
	f_col = ms_col / ms_within;
	f_inter = ms_inter / ms_within;

	p_gr = 1.0 - pf (f_gr, df_gr, df_within);
	p_col = 1.0 - pf (f_col, df_col, df_within);
	p_inter = 1.0 - pf (f_inter, df_inter, df_within);

	set_cell (dao, 0, n * 6 + 10, _("ANOVA"));
	set_cell (dao, 0, n * 6 + 11, _("Source of Variation"));
	set_cell (dao, 0, n * 6 + 12, _("Sample"));
	set_cell (dao, 0, n * 6 + 13, _("Columns"));
	set_cell (dao, 0, n * 6 + 14, _("Interaction"));
	set_cell (dao, 0, n * 6 + 15, _("Within"));
	set_cell (dao, 0, n * 6 + 17, _("Total"));

	set_cell (dao, 1, n * 6 + 11, _("SS"));
	set_cell_float (dao, 1, n * 6 + 12, ss_gr);
	set_cell_float (dao, 1, n * 6 + 13, ss_col);
	set_cell_float (dao, 1, n * 6 + 14, ss_inter);
	set_cell_float (dao, 1, n * 6 + 15, ss_within);
	set_cell_float (dao, 1, n * 6 + 17, ss_tot);

	set_cell (dao, 2, n * 6 + 11, _("df"));
	set_cell_int   (dao, 2, n * 6 + 12, df_gr);
	set_cell_int   (dao, 2, n * 6 + 13, df_col);
	set_cell_int   (dao, 2, n * 6 + 14, df_inter);
	set_cell_int   (dao, 2, n * 6 + 15, df_within);
	set_cell_int   (dao, 2, n * 6 + 17, tot_count-1);

	/* Note: a * b * rows_per_sample, tot_count and df_gr+df_col+df_inter+df_within should all be the same*/

	set_cell (dao, 3, n * 6 + 11, _("MS"));
	set_cell_float (dao, 3, n * 6 + 12, ms_gr);
	set_cell_float (dao, 3, n * 6 + 13, ms_col);
	set_cell_float (dao, 3, n * 6 + 14, ms_inter);
	set_cell_float (dao, 3, n * 6 + 15, ms_within);


	set_cell (dao, 4, n * 6 + 11, _("F"));
	set_cell_float (dao, 4, n * 6 + 12, f_gr);
	set_cell_float (dao, 4, n * 6 + 13, f_col);
	set_cell_float (dao, 4, n * 6 + 14, f_inter);


	set_cell (dao, 5, n * 6 + 11, _("P-value"));
	set_cell_float (dao, 5, n * 6 + 12, p_gr);
	set_cell_float (dao, 5, n * 6 + 13, p_col);
	set_cell_float (dao, 5, n * 6 + 14, p_inter);

	set_cell (dao, 6, n * 6 + 11, _("F crit"));
	set_cell_float (dao, 6, n * 6 + 12, qf (alpha, df_gr, df_within));
	set_cell_float (dao, 6, n * 6 + 13, qf (alpha, df_col, df_within));
	set_cell_float (dao, 6, n * 6 + 14, qf (alpha, df_inter, df_within));

	set_italic (dao, 0, n * 6 + 11, 6, n * 6 + 11);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	g_free (col_count);
	g_free (col_sum);
	g_free (col_sqrsum);

	return 0;
}


/************* Histogram Tool *********************************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

int
histogram_tool (WorkbookControl *wbc, Sheet *sheet, Range *range, Range *bin_range,
		gboolean labels, gboolean sorted, gboolean percentage,
		gboolean chart, data_analysis_output_t *dao)
{
        data_set_t bin_set, set;
	GSList     *list;
	int        i, j, cols, rows, cum_sum;
	gnum_float *intval;
	int        *count;

	cols = bin_range->end.col - bin_range->start.col + 1;
	rows = bin_range->end.row - bin_range->start.row + 1;

	if (get_data (sheet, range, &set)) {
	        free_data_set (&set);
	        return 1;
	}

	if (get_data (sheet, bin_range, &bin_set)) {
	        free_data_set (&set);
	        free_data_set (&bin_set);
	        return 2;
	}

	bin_set.array = g_slist_sort (bin_set.array,
				      (GCompareFunc) float_compare);

	prepare_output (wbc, dao, _("Histogram"));

	i = 1;
	set_cell (dao, 0, 0, _("Bin"));
	set_cell (dao, 1, 0, _("Frequency"));
	if (percentage)
		/* xgettext:no-c-format */
	        set_cell (dao, ++i, 0, _("Cumulative %"));

	set_italic (dao, 0, 0, i, 0);

	count = g_new (int, bin_set.n + 1);
	intval = g_new (gnum_float, bin_set.n);

	list = bin_set.array;
	for (i = 0; i < bin_set.n; i++) {
	        gnum_float x = *((gnum_float *) list->data);
	        set_cell_float (dao, 0, i + 1, x);
		intval[i] = x;
		count[i] = 0;
		list = list->next;
	}
	set_cell (dao, 0, i + 1, "More");
	count[i] = 0;

	list = set.array;
	for (i = 0; i < set.n; i++) {
	        gnum_float x = *((gnum_float *) list->data);
		/* FIXME: Slow!, O(n^2) */
		for (j = 0; j < bin_set.n; j++)
		        if (x <= intval[j]) {
			        count[j]++;
				goto next;
			}
		count[j]++;
	next:
		list = list->next;
	}

	cum_sum = 0;
	for (i = 0; i <= bin_set.n; i++) {
	        set_cell_int (dao, 1, i + 1, count[i]);
		cum_sum += count[i];
		if (percentage)
		        set_cell_float (dao, 2, i + 1,
					(gnum_float) cum_sum / set.n);
	}

	free_data_set (&set);
	free_data_set (&bin_set);

	sheet_set_dirty (dao->sheet, TRUE);
	sheet_update (sheet);

	return 0;
}
