/*
 * analysis-tools.c: 
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 */

#include <config.h>
#include <gnome.h>
#include <string.h>
#include <math.h>
#include "mathfunc.h"
#include "numbers.h"
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "utils.h"
#include "tools.h"


typedef struct {
        GSList  *array;
        float_t sum;
        float_t sum2;    /* square of the sum */
        float_t sqrsum;
        float_t min;
        float_t max;
        int     n;
} data_set_t;



/***** Some general routines ***********************************************/

static int
int_compare (const void *px, const void *py)
{
	const int *x = px;
	const int *y = py;
	
        if (*x < *y)
	        return -1;
	else if (*x == *y)
	        return 0;
	else
	        return 1;
}

static gint
float_compare (const float_t *a, const float_t *b)
{
        if (*a < *b)
                return -1;
        else if (*a == *b)
                return 0;
        else
                return 1;
}

static gint
float_compare_desc (const float_t *a, const float_t *b)
{
        if (*a < *b)
                return 1;
        else if (*a == *b)
                return 0;
        else
                return -1;
}

static Cell *
set_cell (data_analysis_output_t *dao, int col, int row, char *text)
{
        Cell *cell;

	/* Check that the output is in the given range */
	if (dao->type == RangeOutput && (col >= dao->cols || row >= dao->rows))
	        return NULL;

	cell = sheet_cell_get (dao->sheet, dao->start_col+col, 
			       dao->start_row+row);
	if (cell == NULL)
	        cell = sheet_cell_new (dao->sheet, dao->start_col+col,
				       dao->start_row+row);
	cell_set_text (cell, text);

	return cell;
}

static void
get_data(Sheet *sheet, Range *range, data_set_t *data)
{
        gpointer p;
	Cell     *cell;
	Value    *v;
	float_t  x;
	int      row, col;

	data->sum = 0;
	data->sum2 = 0;
	data->sqrsum = 0;
	data->n = 0;
	data->array = NULL;

	for (col=range->start.col; col<=range->end.col; col++)
	        for (row=range->start.row; row<=range->end.row; row++) {
		        cell = sheet_cell_get(sheet, col, row);
			if (cell != NULL && cell->value != NULL) {
			        v = cell->value;
				if (VALUE_IS_NUMBER(v))
				        x = value_get_as_float (v);
				else
				        x = 0;

				p = g_new(float_t, 1);
				*((float_t *) p) = x;
				data->array = g_slist_append(data->array, p);
				data->sum += x;
				data->sqrsum += x*x;
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

static void
get_data_groupped_by_columns(Sheet *sheet, Range *range, int col,
			     data_set_t *data)
{
        gpointer p;
	Cell     *cell;
	Value    *v;
	float_t  x;
	int      row;

	data->sum = 0;
	data->sum2 = 0;
	data->sqrsum = 0;
	data->n = 0;
	data->array = NULL;

	for (row=range->start.row; row<=range->end.row; row++) {
	       cell = sheet_cell_get(sheet, col, row);
	       if (cell != NULL && cell->value != NULL) {
		       v = cell->value;
		       if (VALUE_IS_NUMBER(v))
			       x = value_get_as_float (v);
		       else
			       x = 0;

		       p = g_new(float_t, 1);
		       *((float_t *) p) = x;
		       data->array = g_slist_append(data->array, p);
		       data->sum += x;
		       data->sqrsum += x*x;
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

static void
get_data_groupped_by_rows(Sheet *sheet, Range *range, int row,
			  data_set_t *data)
{
        gpointer p;
	Cell     *cell;
	Value    *v;
	float_t  x;
	int      col;

	data->sum = 0;
	data->sum2 = 0;
	data->sqrsum = 0;
	data->n = 0;
	data->array = NULL;

	for (col=range->start.col; col<=range->end.col; col++) {
	       cell = sheet_cell_get(sheet, col, row);
	       if (cell != NULL && cell->value != NULL) {
		       v = cell->value;
		       if (VALUE_IS_NUMBER(v))
			       x = value_get_as_float (v);
		       else
			       x = 0;

		       p = g_new(float_t, 1);
		       *((float_t *) p) = x;
		       data->array = g_slist_append(data->array, p);
		       data->sum += x;
		       data->sqrsum += x*x;
		       data->n++;
	       }
	}

	data->sum2 = data->sum * data->sum;
}

static void
free_data_set(data_set_t *data)
{
        GSList *current = data->array;

	while (current != NULL) {
	        g_free(current->data);
		current=current->next;
	}

	g_slist_free(data->array);
}

static void
prepare_output(Workbook *wb, data_analysis_output_t *dao, char *name)
{
	if (dao->type == NewSheetOutput) {
	        dao->sheet = sheet_new(wb, name);
		dao->start_col = dao->start_row = 0;
		workbook_attach_sheet(wb, dao->sheet);
	} else if (dao->type == NewWorkbookOutput) {
		wb = workbook_new ();
		dao->sheet = sheet_new(wb, name);
		dao->start_col = dao->start_row = 0;
		workbook_attach_sheet(wb, dao->sheet);
		gtk_widget_show (wb->toplevel);
	}
}


/************* Correlation Tool *******************************************
 *
 * The correlation tool calculates the correlation coefficient of two
 * data sets.  The two data sets can be groupped by rows or by columns.
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/


static float_t
correl(data_set_t *set_one, data_set_t *set_two, int *error_flag)
{
        GSList  *current_one, *current_two;
	float_t sum_xy = 0, c=0;
	float_t tmp;

	*error_flag = 0;
	current_one = set_one->array;
	current_two = set_two->array;

	while (current_one != NULL && current_two != NULL) {
	        sum_xy += *((float_t *) current_one->data) *
		  *((float_t *) current_two->data);
	        current_one = current_one->next;
	        current_two = current_two->next;
	}

	if (current_one != NULL || current_two != NULL)
	        *error_flag = 1;
	else {
		tmp = (set_one->sqrsum - (set_one->sum2 / set_one->n)) *
		  (set_two->sqrsum - (set_two->sum2 / set_two->n));
		if (tmp == 0)
		        *error_flag = 2;
		else
		        c = (sum_xy - (set_one->sum*set_two->sum/set_one->n)) /
			     sqrt(tmp);
	}

	return c;
}


/* If columns_flag is set, the data entries are groupped by columns 
 * otherwise by rows.
 */
int
correlation_tool (Workbook *wb, Sheet *sheet, 
		  Range *input_range, int columns_flag,
		  data_analysis_output_t *dao)
{
        data_set_t *data_sets;
	char       buf[256];
	Cell       *cell;
	int        vars, cols, rows, col, row, i;
	int        error;

	cols = input_range->end.col - input_range->start.col + 1;
	rows = input_range->end.row - input_range->start.row + 1;

	prepare_output(wb, dao, "Correlations");

	set_cell (dao, 0, 0, "");

	if (columns_flag) {
	        vars = cols;
		if (dao->labels_flag) {
		        rows--;
			for (col=0; col<vars; col++) {
			        char *s;
			        cell = sheet_cell_get
				  (sheet, input_range->start.col+col, 
				   input_range->start.row);
				if (cell != NULL && cell->value != NULL) {
				        s = value_get_as_string(cell->value);
				        set_cell (dao, 0, col+1, s);
				        set_cell (dao, col+1, 0, s);
				} else
				        return 1;
			}
			input_range->start.row++;
		} else
		        for (col=0; col<vars; col++) {
			        sprintf(buf, "Column %d", col+1);
				set_cell (dao, 0, col+1, buf);
				set_cell (dao, col+1, 0, buf);
			}

		data_sets = g_new(data_set_t, vars);
		for (i=0; i<vars; i++)
		        get_data_groupped_by_columns(sheet,
						     input_range, i, 
						     &data_sets[i]);
	} else {
	        vars = rows;
		if (dao->labels_flag) {
		        cols--;
			for (col=0; col<vars; col++) {
			        char *s;
			        cell = sheet_cell_get
				  (sheet, input_range->start.col,
				   input_range->start.row+col);
				if (cell != NULL && cell->value != NULL) {
				        s = value_get_as_string(cell->value);
				        set_cell (dao, 0, col+1, s);
				        set_cell (dao, col+1, 0, s);
				} else
				        return 1;
			}
			input_range->start.col++;
		} else 
		        for (col=0; col<vars; col++) {
			        sprintf(buf, "Row %d", col+1);
				set_cell (dao, 0, col+1, buf);
				set_cell (dao, col+1, 0, buf);
			}
 
		data_sets = g_new(data_set_t, vars);

		for (i=0; i<vars; i++)
		        get_data_groupped_by_rows(sheet,
						  input_range, i, 
						  &data_sets[i]);
	}

	for (row=0; row<vars; row++) {
		  for (col=0; col<vars; col++) {
		        if (row == col) {
			        set_cell (dao, col+1, row+1, "1");
				break;
			} else {
			        sprintf(buf, "%f", correl(&data_sets[col],
							  &data_sets[row],
							  &error));
				if (error)
				        set_cell (dao, col+1, row+1, "#N/A");
				else
				        set_cell (dao, col+1, row+1, buf);
			}
		}
	}

	for (i=0; i<vars; i++)
	        free_data_set(&data_sets[i]);
	g_free (data_sets);

	return 0;
}



/************* Covariance Tool ********************************************
 *
 * The covariance tool calculates the covariance of two data sets.
 * The two data sets can be groupped by rows or by columns.  The
 * results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/


static float_t
covar(data_set_t *set_one, data_set_t *set_two, int *error_flag)
{
        GSList  *current_one, *current_two;
	float_t sum = 0, c=0;
	float_t mean1, mean2, x, y;

	*error_flag = 0;
	current_one = set_one->array;
	current_two = set_two->array;

	mean1 = set_one->sum / set_one->n;
	mean2 = set_two->sum / set_two->n;

	while (current_one != NULL && current_two != NULL) {
	        x = *((float_t *) current_one->data);
	        y = *((float_t *) current_two->data);
	        sum += (x - mean1) * (y - mean2);
	        current_one = current_one->next;
	        current_two = current_two->next;
	}

	if (current_one != NULL || current_two != NULL)
	        *error_flag = 1;

	c = sum / set_one->n;

	return c;
}

/* If columns_flag is set, the data entries are groupped by columns 
 * otherwise by rows.
 */
int
covariance_tool (Workbook *wb, Sheet *sheet, 
		 Range *input_range, int columns_flag,
		 data_analysis_output_t *dao)
{
        data_set_t *data_sets;
	char       buf[256];
	Cell       *cell;
	int        vars, cols, rows, col, row, i;
	int        error;

	prepare_output(wb, dao, "Covariances");

	cols = input_range->end.col - input_range->start.col + 1;
	rows = input_range->end.row - input_range->start.row + 1;

	set_cell (dao, 0, 0, "");

	if (columns_flag) {
	        vars = cols;
		if (dao->labels_flag) {
		        rows--;
			for (col=0; col<vars; col++) {
			        char *s;
			        cell = sheet_cell_get
				  (sheet, input_range->start.col+col, 
				   input_range->start.row);
				if (cell != NULL && cell->value != NULL) {
				        s = value_get_as_string(cell->value);
				        set_cell (dao, 0, col+1, s);
				        set_cell (dao, col+1, 0, s);
				} else
				        return 1;
			}
			input_range->start.row++;
		} else
		        for (col=0; col<vars; col++) {
			        sprintf(buf, "Column %d", col+1);
				set_cell (dao, 0, col+1, buf);
				set_cell (dao, col+1, 0, buf);
			}

		data_sets = g_new(data_set_t, vars);
		for (i=0; i<vars; i++)
		        get_data_groupped_by_columns(sheet,
						     input_range, i, 
						     &data_sets[i]);
	} else {
	        vars = rows;
		if (dao->labels_flag) {
		        cols--;
			for (col=0; col<vars; col++) {
			        char *s;
			        cell = sheet_cell_get
				  (sheet, input_range->start.col,
				   input_range->start.row+col);
				if (cell != NULL && cell->value != NULL) {
				        s = value_get_as_string(cell->value);
				        set_cell (dao, 0, col+1, s);
				        set_cell (dao, col+1, 0, s);
				} else
				        return 1;
			}
			input_range->start.col++;
		} else 
		        for (col=0; col<vars; col++) {
			        sprintf(buf, "Row %d", col+1);
				set_cell (dao, 0, col+1, buf);
				set_cell (dao, col+1, 0, buf);
			}
 
		data_sets = g_new(data_set_t, vars);

		for (i=0; i<vars; i++)
		        get_data_groupped_by_rows(sheet,
						  input_range, i, 
						  &data_sets[i]);
	}

	for (row=0; row<vars; row++) {
		  for (col=0; col<vars; col++) {
		        if (row < col)
				break;
			else {
			        sprintf(buf, "%f", covar(&data_sets[col],
							 &data_sets[row],
							  &error));
				if (error)
				        set_cell (dao, col+1, row+1, "#N/A");
				else
				        set_cell (dao, col+1, row+1, buf);
			}
		}
	}

	for (i=0; i<vars; i++)
	        free_data_set(&data_sets[i]);
	g_free (data_sets);

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


static float_t
kurt(data_set_t *data, int *error_flag)
{
        GSList  *current;
	float_t sum = 0;
	float_t mean, stdev, x;
        float_t num, dem, d;
	float_t n = data->n;

	if (n < 4) {
	        *error_flag = 1;
		return 0;
	} else
	        *error_flag = 0;

	mean = data->sum / n;
	stdev = sqrt((data->sqrsum - data->sum2/n) / (n - 1));

	current = data->array;
	while (current != NULL) {
	        x = *((float_t *) current->data);
	        x = (x - mean) / stdev;
		sum += (x * x) * (x * x);
	        current = current->next;
	}

	num = n * (n + 1);
	dem = (n - 1) * (n - 2) * (n - 3);
	d = (3 * (n - 1) * (n - 1)) / ((n - 2) * (n - 3));

	return sum * (num / dem) - d;
}

static float_t
skew(data_set_t *data, int *error_flag)
{
        GSList  *current;
	float_t x3, m, s, x, dxn;
	float_t n = data->n;

	if (n < 3) {
	        *error_flag = 1;
		return 0;
	} else
	        *error_flag = 0;

	x3 = 0;
	m = data->sum / n;
	s = sqrt((data->sqrsum - data->sum2/n) / (n - 1));

	current = data->array;
	while (current != NULL) {
	        x = *((float_t *) current->data);
		dxn = (x - m) / s;
		x3 += dxn * dxn * dxn;
	        current = current->next;
	}

	return ((x3 * n) / (n - 1)) / (n - 2);
}

static void
summary_statistics(Workbook *wb, data_set_t *data_set, int vars,
		   data_analysis_output_t *dao)
{
        char    buf[256];
	float_t x;
	int     col, error;

	prepare_output(wb, dao, "Summary Statistics");

        set_cell (dao, 0, 0, "");
	for (col=0; col<vars; col++) {
	        sprintf(buf, "Column %d", col+1);
		set_cell (dao, col+1, 0, buf);
	}

        set_cell (dao, 0, 1, "Mean");
        set_cell (dao, 0, 2, "Standard Error");
        set_cell (dao, 0, 3, "Median");
        set_cell (dao, 0, 4, "Mode");
        set_cell (dao, 0, 5, "Standard Deviation");
        set_cell (dao, 0, 6, "Sample Variance");
        set_cell (dao, 0, 7, "Kurtosis");
        set_cell (dao, 0, 8, "Skewness");
        set_cell (dao, 0, 9, "Range");
        set_cell (dao, 0, 10, "Minimum");
        set_cell (dao, 0, 11, "Maximum");
        set_cell (dao, 0, 12, "Sum");
        set_cell (dao, 0, 13, "Count");

	for (col=0; col<vars; col++) {
	        float_t var, stdev;

		var = (data_set[col].sqrsum - 
			       data_set[col].sum2/data_set[col].n) /
		        (data_set[col].n - 1);
		stdev = sqrt(var);

		data_set[col].array = 
		        g_slist_sort(data_set[col].array, 
				     (GCompareFunc) float_compare);

	        /* Mean */
	        sprintf(buf, "%f", data_set[col].sum / data_set[col].n);
		set_cell (dao, col+1, 1, buf);

		/* Standard Error */
	        sprintf(buf, "%f", stdev / sqrt(data_set[col].n));
		set_cell (dao, col+1, 2, buf);

		/* Median */
		if (data_set[col].n % 2 == 1)
		        x = *((float_t *)g_slist_nth_data(data_set[col].array, 
							  data_set[col].n/2));
		else {
		        x=*((float_t *)g_slist_nth_data(data_set[col].array, 
							data_set[col].n/2));
		        x+=*((float_t *)g_slist_nth_data(data_set[col].array, 
							 data_set[col].n/2-1));
			x /= 2;
		}
	        sprintf(buf, "%f", x);
		set_cell (dao, col+1, 3, buf);

		/* Mode */
		/* TODO */

		/* Standard Deviation */
	        sprintf(buf, "%f", stdev);
		set_cell (dao, col+1, 5, buf);

		/* Sample Variance */
	        sprintf(buf, "%f", var);
		set_cell (dao, col+1, 6, buf);

		/* Kurtosis */
		if (data_set[col].n > 3) {
		        x = kurt(&data_set[col], &error);
			sprintf(buf, "%f", x);
		} else
		        sprintf(buf, "#N/A");
		set_cell (dao, col+1, 7, buf);

		/* Skewness */
		if (data_set[col].n > 2) {
		        x = skew(&data_set[col], &error);
			sprintf(buf, "%f", x);
		} else
		        sprintf(buf, "#N/A");
		set_cell (dao, col+1, 8, buf);

		/* Range */
	        sprintf(buf, "%f", data_set[col].max - data_set[col].min);
		set_cell (dao, col+1, 9, buf);

		/* Minimum */
	        sprintf(buf, "%f", data_set[col].min);
		set_cell (dao, col+1, 10, buf);

		/* Maximum */
	        sprintf(buf, "%f", data_set[col].max);
		set_cell (dao, col+1, 11, buf);

		/* Sum */
	        sprintf(buf, "%f", data_set[col].sum);
		set_cell (dao, col+1, 12, buf);

		/* Count */
	        sprintf(buf, "%d", data_set[col].n);
		set_cell (dao, col+1, 13, buf);
	}
}

static void
confidence_level(Workbook *wb, data_set_t *data_set, int vars, float_t c_level,
		 data_analysis_output_t *dao)
{
        float_t x;
        char    buf[256];
        int col;

	prepare_output(wb, dao, "Confidence Level");

	for (col=0; col<vars; col++) {
	        float_t stdev = sqrt((data_set[col].sqrsum - 
				      data_set[col].sum2/data_set[col].n) /
				     (data_set[col].n - 1));

	        sprintf(buf, "Column %d", col+1);
		set_cell (dao, col+1, 0, buf);

		x = -qnorm(c_level / 2, 0, 1) * (stdev/sqrt(data_set[col].n));
		sprintf(buf, "%f", x);
		set_cell (dao, col+1, 2, buf);
	}
	sprintf(buf, "Confidence Level(%g%%)", c_level*100);
        set_cell (dao, 0, 2, buf);
}

static void
kth_largest(Workbook *wb, data_set_t *data_set, int vars, int k,
	    data_analysis_output_t *dao)
{
        float_t x;
        char    buf[256];
        int     col;

	prepare_output(wb, dao, "Kth Largest");

	for (col=0; col<vars; col++) {
	        sprintf(buf, "Column %d", col+1);
		set_cell (dao, col+1, 0, buf);

		data_set[col].array = 
		        g_slist_sort(data_set[col].array, 
				     (GCompareFunc) float_compare_desc);

		x = *((float_t *) g_slist_nth_data(data_set[col].array, k-1));
		sprintf(buf, "%f", x);
		set_cell(dao, col+1, 2, buf);
	}
	sprintf(buf, "Largest(%d)", k);
        set_cell (dao, 0, 2, buf);
}

static void
kth_smallest(Workbook *wb, data_set_t *data_set, int vars, int k,
	     data_analysis_output_t *dao)
{
        float_t x;
        char    buf[256];
        int     col;

	prepare_output(wb, dao, "Kth Smallest");

	for (col=0; col<vars; col++) {
	        sprintf(buf, "Column %d", col+1);
		set_cell (dao, col+1, 0, buf);

		data_set[col].array = 
		        g_slist_sort(data_set[col].array, 
				     (GCompareFunc) float_compare);

		x = *((float_t *) g_slist_nth_data(data_set[col].array, k-1));
		sprintf(buf, "%f", x);
		set_cell(dao, col+1, 2, buf);
	}
	sprintf(buf, "Smallest(%d)", k);
        set_cell (dao, 0, 2, buf);
}

/* Descriptive Statistics
 */
int
descriptive_stat_tool (Workbook *wb, Sheet *current_sheet, 
                       Range *input_range, int columns_flag,
		       descriptive_stat_tool_t *ds,
		       data_analysis_output_t *dao)
{
        data_set_t *data_sets;
        int        vars, cols, rows, i;

	cols = input_range->end.col - input_range->start.col + 1;
	rows = input_range->end.row - input_range->start.row + 1;

	if (columns_flag) {
	        vars = cols;
		data_sets = g_new(data_set_t, vars);
		for (i=0; i<vars; i++)
		        get_data_groupped_by_columns(current_sheet,
						     input_range, i, 
						     &data_sets[i]);
	} else {
	        vars = rows;
		data_sets = g_new(data_set_t, vars);
		for (i=0; i<vars; i++)
		        get_data_groupped_by_rows(current_sheet,
						  input_range, i, 
						  &data_sets[i]);
	}

        if (ds->summary_statistics) {
                summary_statistics(wb, data_sets, vars, dao);
		if (dao->type == RangeOutput)
		        dao->start_row += 15;
	}
        if (ds->confidence_level) {
                confidence_level(wb, data_sets, vars, ds->c_level, dao);
		if (dao->type == RangeOutput)
		        dao->start_row += 4;
	}
        if (ds->kth_largest) {
                kth_largest(wb, data_sets, vars, ds->k_largest, dao);
		if (dao->type == RangeOutput)
		        dao->start_row += 4;
	}
        if (ds->kth_smallest)
                kth_smallest(wb, data_sets, vars, ds->k_smallest, dao);

	for (i=0; i<vars; i++)
	        free_data_set(&data_sets[i]);
	g_free (data_sets);

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


/* Returns 1 if error occured, for example random sample size is
 * larger than the data set.
 **/
int sampling_tool (Workbook *wb, Sheet *sheet, Range *input_range,
		   gboolean periodic_flag, int size,
		   data_analysis_output_t *dao)
{
        data_set_t data_set;
	char       buf[256];
	float_t    x;

	prepare_output(wb, dao, "Sample");

	get_data(sheet, input_range, &data_set);

	if (periodic_flag) {
	        GSList *current = data_set.array;
		int    counter = size-1;
		int    row = 0;

		while (current != NULL) {
		        if (++counter == size) {
			        x = *((float_t *) current->data);
				sprintf(buf, "%f", x);
				set_cell (dao, 0, row++, buf);
				counter = 0;
			}
		        current = current->next;
		}
	} else {
	        int     *index_tbl;
		int     i, n, x;
		GSList  *current = data_set.array;
		int     counter = 0;
		int     row=0;

	        if (size > data_set.n)
		        return 1;

		if (size <= data_set.n/2) {
		        index_tbl = g_new(int, size);

			for (i=0; i<size; i++) {
			try_again_1:
			        x = random_01() * size;
				for (n=0; n<i; n++)
				        if (index_tbl[n] == x)
					        goto try_again_1;
				index_tbl[i] = x;
			}
			qsort(index_tbl, size, sizeof(int), 
			      int_compare);
			n = 0;
			while (n < size) {
			        if (counter++ == index_tbl[n]) {
					sprintf(buf, "%f", 
						*((float_t *) current->data));
					set_cell (dao, 0, row++, buf);
					++n;
				}
				current = current->next;
			}
			g_free(index_tbl);
		} else {
		        if (data_set.n != size)
			        index_tbl = g_new(int, data_set.n-size);
			else {
			        index_tbl = g_new(int, 1);
				index_tbl[0] = -1;
			}

			for (i=0; i<data_set.n-size; i++) {
			try_again_2:
			        x = random_01() * size;
				for (n=0; n<i; n++)
				        if (index_tbl[n] == x)
					        goto try_again_2;
				index_tbl[i] = x;
			}
			if (data_set.n != size)
			        qsort(index_tbl, size, sizeof(int), 
				      int_compare);
			n = 0;
			while (current != NULL) {
			        if (counter++ == index_tbl[n])
					++n;
				else {
					sprintf(buf, "%f", 
						*((float_t *) current->data));
					set_cell (dao, 0, row++, buf);
				}
				current = current->next;
			}
			g_free(index_tbl);
		}
		
	}

	free_data_set (&data_set);

	return 0;
}



/************* z-Test: Two Sample for Means ******************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/


int ztest_tool (Workbook *wb, Sheet *sheet, Range *input_range1, 
		Range *input_range2, float_t mean_diff, float_t known_var1,
		float_t known_var2, float_t alpha,
		data_analysis_output_t *dao)
{
        data_set_t set_one, set_two;
	float_t    mean1, mean2, var1, var2, z, p;
	char       buf[256];

	prepare_output(wb, dao, "z-Test");

	get_data(sheet, input_range1, &set_one);
	get_data(sheet, input_range2, &set_two);

        set_cell (dao, 0, 0, "");

	if (dao->labels_flag) {
	        char *s;
		Cell *cell;

		cell = sheet_cell_get(sheet, input_range1->start.col, 
				      input_range1->start.row);
		if (cell != NULL && cell->value != NULL) {
		        s = value_get_as_string(cell->value);
			set_cell (dao, 1, 0, s);
		}
		cell = sheet_cell_get(sheet, input_range2->start.col, 
				      input_range2->start.row);
		if (cell != NULL && cell->value != NULL) {
		        s = value_get_as_string(cell->value);
			set_cell (dao, 2, 0, s);
		}

		input_range1->start.row++;
		input_range2->start.row++;
	} else {
	        set_cell (dao, 1, 0, "Variable 1");
		set_cell (dao, 2, 0, "Variable 2");
	}

        set_cell (dao, 0, 1, "Mean");
        set_cell (dao, 0, 2, "Known Variance");
        set_cell (dao, 0, 3, "Observations");
        set_cell (dao, 0, 4, "Hypothesized Mean Difference");
        set_cell (dao, 0, 5, "z");
        set_cell (dao, 0, 6, "P(Z<=z) one-tail");
        set_cell (dao, 0, 7, "z Critical one-tail");
        set_cell (dao, 0, 8, "P(Z<=z) two-tail");
        set_cell (dao, 0, 9, "z Critical two-tail");

	mean1 = set_one.sum / set_one.n;
	mean2 = set_two.sum / set_two.n;
	var1 = (set_one.sqrsum - set_one.sum2/set_one.n) / (set_one.n - 1);
	var2 = (set_two.sqrsum - set_two.sum2/set_two.n) / (set_two.n - 1);
	z = (mean1 - mean2 - mean_diff) / 
	  sqrt(known_var1/set_one.n + known_var2/set_two.n);
	p = 1 - pnorm(z, 0, 1);

	/* Mean */
	sprintf(buf, "%f", mean1);
	set_cell(dao, 1, 1, buf);
	sprintf(buf, "%f", mean2);
	set_cell(dao, 2, 1, buf);

	/* Known Variance */
	sprintf(buf, "%f", known_var1);
	set_cell(dao, 1, 2, buf);
	sprintf(buf, "%f", known_var2);
	set_cell(dao, 2, 2, buf);

	/* Observations */
	sprintf(buf, "%d", set_one.n);
	set_cell(dao, 1, 3, buf);
	sprintf(buf, "%d", set_two.n);
	set_cell(dao, 2, 3, buf);

	/* Hypothesized Mean Difference */
	sprintf(buf, "%f", mean_diff);
	set_cell(dao, 1, 4, buf);

	/* z */
	sprintf(buf, "%f", z);
	set_cell(dao, 1, 5, buf);

	/* P(Z<=z) one-tail */
	sprintf(buf, "%f", p);
	set_cell(dao, 1, 6, buf);

	/* z Critical one-tail */
	sprintf(buf, "%f", qnorm(alpha, 0, 1));
	set_cell(dao, 1, 7, buf);

	/* P(Z<=z) two-tail */
	sprintf(buf, "%f", 2*p);
	set_cell(dao, 1, 8, buf);

	/* z Critical two-tail */
	sprintf(buf, "%f", qnorm(1.0-(1.0-alpha)/2, 0, 1));
	set_cell(dao, 1, 9, buf);

	free_data_set (&set_one);
	free_data_set (&set_two);

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
ttest_paired_tool (Workbook *wb, Sheet *sheet, Range *input_range1, 
		   Range *input_range2, float_t mean_diff, float_t alpha,
		   data_analysis_output_t *dao)
{
        data_set_t set_one, set_two;
	GSList     *current_one, *current_two;
	float_t    mean1, mean2, pearson, var1, var2, t, p, df, sum_xy, sum;
	float_t    dx, dm, M, Q, N, s;
	char       buf[256];

	get_data(sheet, input_range1, &set_one);
	get_data(sheet, input_range2, &set_two);

	if (set_one.n != set_two.n) {
	        free_data_set(&set_one);
		free_data_set(&set_two);
	        return 1;
	}

	prepare_output(wb, dao, "t-Test");

        set_cell (dao, 0, 0, "");

	if (dao->labels_flag) {
	        char *s;
		Cell *cell;

		cell = sheet_cell_get(sheet, input_range1->start.col, 
				      input_range1->start.row);
		if (cell != NULL && cell->value != NULL) {
		        s = value_get_as_string(cell->value);
			set_cell (dao, 1, 0, s);
		}
		cell = sheet_cell_get(sheet, input_range2->start.col, 
				      input_range2->start.row);
		if (cell != NULL && cell->value != NULL) {
		        s = value_get_as_string(cell->value);
			set_cell (dao, 2, 0, s);
		}

		input_range1->start.row++;
		input_range2->start.row++;
	} else {
	        set_cell (dao, 1, 0, "Variable 1");
		set_cell (dao, 2, 0, "Variable 2");
	}

        set_cell (dao, 0, 1, "Mean");
        set_cell (dao, 0, 2, "Variance");
        set_cell (dao, 0, 3, "Observations");
        set_cell (dao, 0, 4, "Pearson Correlation");
        set_cell (dao, 0, 5, "Hypothesized Mean Difference");
        set_cell (dao, 0, 6, "df");
        set_cell (dao, 0, 7, "t Stat");
        set_cell (dao, 0, 8, "P(T<=t) one-tail");
        set_cell (dao, 0, 9, "t Critical one-tail");
        set_cell (dao, 0, 10, "P(T<=t) two-tail");
        set_cell (dao, 0, 11, "t Critical two-tail");

	current_one = set_one.array;
	current_two = set_two.array;
	sum = sum_xy = 0;
	dx = dm = M = Q = N = 0;

	while (current_one != NULL && current_two != NULL) {
	        float_t x, y, d;

		x = *((float_t *) current_one->data);
		y = *((float_t *) current_two->data);
		sum_xy += x*y;
		d = x-y;
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

	var1 = (set_one.sqrsum - set_one.sum2/set_one.n) / (set_one.n - 1);
	var2 = (set_two.sqrsum - set_two.sum2/set_two.n) / (set_two.n - 1);

	df = set_one.n - 1;
	pearson = ((set_one.n*sum_xy - set_one.sum*set_two.sum) /
		   sqrt((set_one.n*set_one.sqrsum - set_one.sum2) *
			(set_one.n*set_two.sqrsum - set_two.sum2)));
	s = sqrt(Q / (N - 1));
	t = (sum/set_one.n - mean_diff)/(s/sqrt(set_one.n));
	p = 1.0 - pt(fabs(t), df);

	/* Mean */
	sprintf(buf, "%f", mean1);
	set_cell(dao, 1, 1, buf);
	sprintf(buf, "%f", mean2);
	set_cell(dao, 2, 1, buf);

	/* Variance */
	sprintf(buf, "%f", var1);
	set_cell(dao, 1, 2, buf);
	sprintf(buf, "%f", var2);
	set_cell(dao, 2, 2, buf);

	/* Observations */
	sprintf(buf, "%d", set_one.n);
	set_cell(dao, 1, 3, buf);
	sprintf(buf, "%d", set_two.n);
	set_cell(dao, 2, 3, buf);

	/* Pearson Correlation */
	sprintf(buf, "%f", pearson);
	set_cell(dao, 1, 4, buf);

	/* Hypothesized Mean Difference */
	sprintf(buf, "%f", mean_diff);
	set_cell(dao, 1, 5, buf);

	/* df */
	sprintf(buf, "%f", df);
	set_cell(dao, 1, 6, buf);

	/* t */
	sprintf(buf, "%f", t);
	set_cell(dao, 1, 7, buf);

	/* P(T<=t) one-tail */
	sprintf(buf, "%f", p);
	set_cell(dao, 1, 8, buf);

	/* t Critical one-tail */
	sprintf(buf, "%f", qt(alpha, df));
	set_cell(dao, 1, 9, buf);

	/* P(T<=t) two-tail */
	sprintf(buf, "%f", 2*p);
	set_cell(dao, 1, 10, buf);

	/* t Critical two-tail */
	sprintf(buf, "%f", qt(1.0-(1.0-alpha)/2, df));
	set_cell(dao, 1, 11, buf);

        free_data_set(&set_one);
        free_data_set(&set_two);

	return 0;
}


/* t-Test: Two-Sample Assuming Equal Variances.
 */
int
ttest_eq_var_tool (Workbook *wb, Sheet *sheet, Range *input_range1, 
		   Range *input_range2, float_t mean_diff, float_t alpha,
		   data_analysis_output_t *dao)
{
        data_set_t set_one, set_two;
	float_t    mean1, mean2, var, var1, var2, t, p, df;
	char       buf[256];

	prepare_output(wb, dao, "t-Test");

	get_data(sheet, input_range1, &set_one);
	get_data(sheet, input_range2, &set_two);

        set_cell (dao, 0, 0, "");

	if (dao->labels_flag) {
	        char *s;
		Cell *cell;

		cell = sheet_cell_get(sheet, input_range1->start.col, 
				      input_range1->start.row);
		if (cell != NULL && cell->value != NULL) {
		        s = value_get_as_string(cell->value);
			set_cell (dao, 1, 0, s);
		}
		cell = sheet_cell_get(sheet, input_range2->start.col, 
				      input_range2->start.row);
		if (cell != NULL && cell->value != NULL) {
		        s = value_get_as_string(cell->value);
			set_cell (dao, 2, 0, s);
		}

		input_range1->start.row++;
		input_range2->start.row++;
	} else {
	        set_cell (dao, 1, 0, "Variable 1");
		set_cell (dao, 2, 0, "Variable 2");
	}

        set_cell (dao, 0, 1, "Mean");
        set_cell (dao, 0, 2, "Variance");
        set_cell (dao, 0, 3, "Observations");
        set_cell (dao, 0, 4, "Pooled Variance");
        set_cell (dao, 0, 5, "Hypothesized Mean Difference");
        set_cell (dao, 0, 6, "df");
        set_cell (dao, 0, 7, "t Stat");
        set_cell (dao, 0, 8, "P(T<=t) one-tail");
        set_cell (dao, 0, 9, "t Critical one-tail");
        set_cell (dao, 0, 10, "P(T<=t) two-tail");
        set_cell (dao, 0, 11, "t Critical two-tail");

	mean1 = set_one.sum / set_one.n;
	mean2 = set_two.sum / set_two.n;

	var1 = (set_one.sqrsum - set_one.sum2/set_one.n) / (set_one.n - 1);
	var2 = (set_two.sqrsum - set_two.sum2/set_two.n) / (set_two.n - 1);

	var = (set_one.sqrsum+set_two.sqrsum - (set_one.sum+set_two.sum)*
	       (set_one.sum+set_two.sum)/(set_one.n+set_two.n)) /
	  (set_one.n+set_two.n-1);  /* TODO: Correct??? */

	df = set_one.n + set_two.n - 2;
	t = fabs(mean1 - mean2 - mean_diff) /
	  sqrt(var1/set_one.n + var2/set_two.n);
	p = 1.0 - pt(t, df);

	/* Mean */
	sprintf(buf, "%f", mean1);
	set_cell(dao, 1, 1, buf);
	sprintf(buf, "%f", mean2);
	set_cell(dao, 2, 1, buf);

	/* Variance */
	sprintf(buf, "%f", var1);
	set_cell(dao, 1, 2, buf);
	sprintf(buf, "%f", var2);
	set_cell(dao, 2, 2, buf);

	/* Observations */
	sprintf(buf, "%d", set_one.n);
	set_cell(dao, 1, 3, buf);
	sprintf(buf, "%d", set_two.n);
	set_cell(dao, 2, 3, buf);

	/* Pooled Variance */
	sprintf(buf, "%f", var);
	set_cell(dao, 1, 4, buf);

	/* Hypothesized Mean Difference */
	sprintf(buf, "%f", mean_diff);
	set_cell(dao, 1, 5, buf);

	/* df */
	sprintf(buf, "%f", df);
	set_cell(dao, 1, 6, buf);

	/* t */
	sprintf(buf, "%f", t);
	set_cell(dao, 1, 7, buf);

	/* P(T<=t) one-tail */
	sprintf(buf, "%f", p);
	set_cell(dao, 1, 8, buf);

	/* t Critical one-tail */
	sprintf(buf, "%f", qt(alpha, df));
	set_cell(dao, 1, 9, buf);

	/* P(T<=t) two-tail */
	sprintf(buf, "%f", 2*p);
	set_cell(dao, 1, 10, buf);

	/* t Critical two-tail */
	sprintf(buf, "%f", qt(1.0-(1.0-alpha)/2, df));
	set_cell(dao, 1, 11, buf);

        free_data_set(&set_one);
        free_data_set(&set_two);

	return 0;
}


/* t-Test: Two-Sample Assuming Unequal Variances.
 */
int
ttest_neq_var_tool (Workbook *wb, Sheet *sheet, Range *input_range1, 
		    Range *input_range2, float_t mean_diff, float_t alpha,
		    data_analysis_output_t *dao)
{
        data_set_t set_one, set_two;
	float_t    mean1, mean2, var1, var2, t, p, df, c;
	char       buf[256];

	prepare_output(wb, dao, "t-Test");

	get_data(sheet, input_range1, &set_one);
	get_data(sheet, input_range2, &set_two);

        set_cell (dao, 0, 0, "");

	if (dao->labels_flag) {
	        char *s;
		Cell *cell;

		cell = sheet_cell_get(sheet, input_range1->start.col, 
				      input_range1->start.row);
		if (cell != NULL && cell->value != NULL) {
		        s = value_get_as_string(cell->value);
			set_cell (dao, 1, 0, s);
		}
		cell = sheet_cell_get(sheet, input_range2->start.col, 
				      input_range2->start.row);
		if (cell != NULL && cell->value != NULL) {
		        s = value_get_as_string(cell->value);
			set_cell (dao, 2, 0, s);
		}

		input_range1->start.row++;
		input_range2->start.row++;
	} else {
	        set_cell (dao, 1, 0, "Variable 1");
		set_cell (dao, 2, 0, "Variable 2");
	}

        set_cell (dao, 0, 1, "Mean");
        set_cell (dao, 0, 2, "Variance");
        set_cell (dao, 0, 3, "Observations");
        set_cell (dao, 0, 4, "Hypothesized Mean Difference");
        set_cell (dao, 0, 5, "df");
        set_cell (dao, 0, 6, "t Stat");
        set_cell (dao, 0, 7, "P(T<=t) one-tail");
        set_cell (dao, 0, 8, "t Critical one-tail");
        set_cell (dao, 0, 9, "P(T<=t) two-tail");
        set_cell (dao, 0, 10, "t Critical two-tail");

	mean1 = set_one.sum / set_one.n;
	mean2 = set_two.sum / set_two.n;

	var1 = (set_one.sqrsum - set_one.sum2/set_one.n) / (set_one.n - 1);
	var2 = (set_two.sqrsum - set_two.sum2/set_two.n) / (set_two.n - 1);

	c = (var1/set_one.n) / (var1/set_one.n+var2/set_two.n);
	df = 1.0 / ((c*c) / (set_one.n-1.0) + ((1-c)*(1-c)) / (set_two.n-1.0));

	t = fabs(mean1 - mean2 - mean_diff) /
	  sqrt(var1/set_one.n + var2/set_two.n);
	p = 1.0 - pt(t, df);

	/* Mean */
	sprintf(buf, "%f", mean1);
	set_cell(dao, 1, 1, buf);
	sprintf(buf, "%f", mean2);
	set_cell(dao, 2, 1, buf);

	/* Variance */
	sprintf(buf, "%f", var1);
	set_cell(dao, 1, 2, buf);
	sprintf(buf, "%f", var2);
	set_cell(dao, 2, 2, buf);

	/* Observations */
	sprintf(buf, "%d", set_one.n);
	set_cell(dao, 1, 3, buf);
	sprintf(buf, "%d", set_two.n);
	set_cell(dao, 2, 3, buf);

	/* Hypothesized Mean Difference */
	sprintf(buf, "%f", mean_diff);
	set_cell(dao, 1, 4, buf);

	/* df */
	sprintf(buf, "%f", df);
	set_cell(dao, 1, 5, buf);

	/* t */
	sprintf(buf, "%f", t);
	set_cell(dao, 1, 6, buf);

	/* P(T<=t) one-tail */
	sprintf(buf, "%f", p);
	set_cell(dao, 1, 7, buf);

	/* t Critical one-tail */
	sprintf(buf, "%f", qt(alpha, df));
	set_cell(dao, 1, 8, buf);

	/* P(T<=t) two-tail */
	sprintf(buf, "%f", 2*p);
	set_cell(dao, 1, 9, buf);

	/* t Critical two-tail */
	sprintf(buf, "%f", qt(1.0-(1.0-alpha)/2, df));
	set_cell(dao, 1, 10, buf);

        free_data_set(&set_one);
        free_data_set(&set_two);

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
ftest_tool (Workbook *wb, Sheet *sheet, Range *input_range1, 
	    Range *input_range2, float_t alpha,
	    data_analysis_output_t *dao)
{
        data_set_t set_one, set_two;
	float_t    mean1, mean2, var1, var2, f, p, df1, df2, c;
	char       buf[256];

	prepare_output(wb, dao, "F-Test");

        set_cell (dao, 0, 0, "");

	if (dao->labels_flag) {
	        char *s;
		Cell *cell;

		cell = sheet_cell_get(sheet, input_range1->start.col, 
				      input_range1->start.row);
		if (cell != NULL && cell->value != NULL) {
		        s = value_get_as_string(cell->value);
			set_cell (dao, 1, 0, s);
		}
		cell = sheet_cell_get(sheet, input_range2->start.col, 
				      input_range2->start.row);
		if (cell != NULL && cell->value != NULL) {
		        s = value_get_as_string(cell->value);
			set_cell (dao, 2, 0, s);
		}

		input_range1->start.row++;
		input_range2->start.row++;
	} else {
	        set_cell (dao, 1, 0, "Variable 1");
		set_cell (dao, 2, 0, "Variable 2");
	}

	get_data(sheet, input_range1, &set_one);
	get_data(sheet, input_range2, &set_two);

        set_cell (dao, 0, 1, "Mean");
        set_cell (dao, 0, 2, "Variance");
        set_cell (dao, 0, 3, "Observations");
        set_cell (dao, 0, 4, "df");
        set_cell (dao, 0, 5, "F");
        set_cell (dao, 0, 6, "P(F<=f) one-tail");
        set_cell (dao, 0, 7, "F Critical one-tail");

	mean1 = set_one.sum / set_one.n;
	mean2 = set_two.sum / set_two.n;

	var1 = (set_one.sqrsum - set_one.sum2/set_one.n) / (set_one.n - 1);
	var2 = (set_two.sqrsum - set_two.sum2/set_two.n) / (set_two.n - 1);

	c = (var1/set_one.n) / (var1/set_one.n+var2/set_two.n);
	df1 = set_one.n-1;
	df2 = set_two.n-1;

	f = var1 / var2;
	p = 1.0 - pf(f, df1, df2);

	/* Mean */
	sprintf(buf, "%f", mean1);
	set_cell(dao, 1, 1, buf);
	sprintf(buf, "%f", mean2);
	set_cell(dao, 2, 1, buf);

	/* Variance */
	sprintf(buf, "%f", var1);
	set_cell(dao, 1, 2, buf);
	sprintf(buf, "%f", var2);
	set_cell(dao, 2, 2, buf);

	/* Observations */
	sprintf(buf, "%d", set_one.n);
	set_cell(dao, 1, 3, buf);
	sprintf(buf, "%d", set_two.n);
	set_cell(dao, 2, 3, buf);

	/* df */
	sprintf(buf, "%f", df1);
	set_cell(dao, 1, 4, buf);
	sprintf(buf, "%f", df2);
	set_cell(dao, 2, 4, buf);

	/* F */
	sprintf(buf, "%f", f);
	set_cell(dao, 1, 5, buf);

	/* P(F<=f) one-tail */
	sprintf(buf, "%f", p);
	set_cell(dao, 1, 6, buf);

	/* F Critical one-tail */
	sprintf(buf, "%f", qf(alpha, df1, df2));
	set_cell(dao, 1, 7, buf);

        free_data_set(&set_one);
        free_data_set(&set_two);

	return 0;
}



/************* Random Number Generation Tool ******************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

int random_tool (Workbook *wb, Sheet *sheet, int vars, int count,
		 random_distribution_t distribution,
		 random_tool_t *param, data_analysis_output_t *dao)
{
	char       buf[256];
	int        i, n, j;
	float_t    range, p;
	float_t    *prob, *cumul_p;
	Value      **values, *v;
	Cell       *cell;

	prepare_output(wb, dao, "Random");

	switch (distribution) {
	case DiscreteDistribution:
	        n = param->discrete.end_row-param->discrete.start_row + 1;
	        prob = g_new(float_t, n);
		cumul_p = g_new(float_t, n);
		values = g_new(Value *, n);

                p = 0;
		j = 0;
	        for (i=param->discrete.start_row;
		     i<=param->discrete.end_row; i++, j++) {
		        cell = sheet_cell_get(sheet,
					      param->discrete.start_col+1, i);
			if (cell != NULL && cell->value != NULL) {
			        v = cell->value;
				if (VALUE_IS_NUMBER(v))
				        prob[j] = value_get_as_float (v);
				else {
				        g_free(prob);
					g_free(cumul_p);
					g_free(values);

					return 1;
				}
			} else {
			        g_free(prob);
				g_free(cumul_p);
				g_free(values);

			        return 1;
			}

			p += prob[j];
			cumul_p[j] = p;

		        cell = sheet_cell_get(sheet,
					      param->discrete.start_col, i);
			if (cell != NULL && cell->value != NULL)
			        values[j] = value_duplicate(cell->value);
			else {
			        g_free(prob);
				g_free(cumul_p);
				g_free(values);

			        return 1;
			}
		}
		
		if (p != 1) {
		        g_free(prob);
			g_free(cumul_p);
			g_free(values);

		        return 2;
		}
			
	        for (i=0; i<vars; i++) {
		        for (n=0; n<count; n++) {
			        float_t x = random_01();

				for (j=0; cumul_p[j] < x; j++)
				        ;

				if (VALUE_IS_NUMBER(values[j]))
				        sprintf(buf, "%f", 
						value_get_as_float(values[j]));
				else
				        sprintf(buf, "%s",
						value_get_as_string(values[j]));
				set_cell(dao, i, n, buf);
			}
		}
	        break;
	case NormalDistribution:
	        for (i=0; i<vars; i++) {
		        for (n=0; n<count; n++) {
			        sprintf(buf, "%f", 
					qnorm(random_01(),
					      param->normal.mean,
					      param->normal.stdev));
				set_cell(dao, i, n, buf);
			}
		}
	        break;
	case BernoulliDistribution:
	        for (i=0; i<vars; i++) {
		        for (n=0; n<count; n++) {
			        sprintf(buf, "%d", 
					(random_01() <= param->bernoulli.p) ?
					1 : 0);
				set_cell(dao, i, n, buf);
			}
		}
	        break;
	case UniformDistribution:
	        range = param->uniform.upper_limit-param->uniform.lower_limit;
	        for (i=0; i<vars; i++) {
		        for (n=0; n<count; n++) {
			        sprintf(buf, "%f", range*random_01() +
					param->uniform.lower_limit);
				set_cell(dao, i, n, buf);
			}
		}
		break;
	default:
	        printf("Not implemented yet.\n");
		break;
	}

	return 0;
}



/************* Regression Tool *********************************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

int regression_tool (Workbook *wb, Sheet *sheet, Range *input_range1, 
		     Range *input_range2, float_t alpha,
		     data_analysis_output_t *dao)
{
        data_set_t set_one, set_two;
	float_t    mean1, mean2;
	float_t    r, ss_xy, ss_xx, ss_yy;
	char       buf[256];
	GSList     *current_one, *current_two;

	get_data(sheet, input_range1, &set_one);
	get_data(sheet, input_range2, &set_two);

	if (set_one.n != set_two.n) {
	        free_data_set(&set_one);
		free_data_set(&set_two);

		return 1;
	}

	prepare_output(wb, dao, "Regression");

        set_cell (dao, 0, 0, "SUMMARY OUTPUT");

	set_cell (dao, 0, 2, "Regression Statistics");
        set_cell (dao, 0, 3, "Multiple R");
        set_cell (dao, 0, 4, "R Square");
        set_cell (dao, 0, 5, "Adjusted R Square");
        set_cell (dao, 0, 6, "Standard Error");
        set_cell (dao, 0, 7, "Observations");

        set_cell (dao, 0, 9, "ANOVA");
        set_cell (dao, 0, 11, "Regression");
        set_cell (dao, 0, 12, "Residual");
        set_cell (dao, 0, 13, "Total");

        set_cell (dao, 0, 16, "Intercept");
        set_cell (dao, 0, 17, "X Variable 1");

        set_cell (dao, 1, 10, "df");
        set_cell (dao, 2, 10, "SS");
        set_cell (dao, 3, 10, "MS");
        set_cell (dao, 4, 10, "F");
        set_cell (dao, 5, 10, "Significance F");

        set_cell (dao, 1, 15, "Coefficients");
        set_cell (dao, 2, 15, "Standard Error");
        set_cell (dao, 3, 15, "t Stat");
        set_cell (dao, 4, 15, "P-value");
        set_cell (dao, 5, 15, "Lower 95%");
        set_cell (dao, 6, 15, "Upper 95%");


	mean1 = set_one.sum / set_one.n;
	mean2 = set_two.sum / set_two.n;

	current_one = set_one.array;
	current_two = set_two.array;
	ss_xy = ss_xx = ss_yy = 0;

	while (current_one != NULL && current_two != NULL) {
	        float_t x, y;

		x = *((float_t *) current_one->data);
		y = *((float_t *) current_two->data);
	        ss_xy += (x - mean1) * (y - mean2);
		ss_xx += (x - mean1) * (x - mean1);
		ss_yy += (y - mean2) * (y - mean2);
	        current_one = current_one->next;
	        current_two = current_two->next;
	}

	r = ss_xy/sqrt(ss_xx*ss_yy);

	/* Multiple R */
	sprintf(buf, "%f", r);
	set_cell(dao, 1, 3, buf);

	/* R Square */
	sprintf(buf, "%f", r*r);
	set_cell(dao, 1, 4, buf);

	/* Observations */
	sprintf(buf, "%d", set_one.n);
	set_cell(dao, 1, 7, buf);

	/* Total / df */
	sprintf(buf, "%d", set_one.n-1);
	set_cell(dao, 1, 13, buf);

	/* Total / SS */
	sprintf(buf, "%f", ss_xx);
	set_cell(dao, 2, 13, buf);

	/* TODO: Fill in the rest of the outputs */

        free_data_set(&set_one);
        free_data_set(&set_two);

	return 0;
}


/************* Moving Average Tool *****************************************
 *
 * The moving average tool calculates moving averages of given data
 * set.  The results are given in a table which can be printed out in
 * a new sheet, in a new workbook, or simply into an existing sheet.
 *
 **/


int average_tool (Workbook *wb, Sheet *sheet, Range *range, int interval, 
		  int std_error_flag, data_analysis_output_t *dao)
{
        data_set_t data_set;
	GSList     *current;
	char       buf[256];
	float_t    *prev, sum;
	int        cols, rows, row, add_cursor, del_cursor, count;

	/* TODO: Standard error output */
	cols = range->end.col - range->start.col + 1;
	rows = range->end.row - range->start.row + 1;

	if ((cols != 1 && rows != 1) || interval < 1)
	        return 1;

	prepare_output(wb, dao, "Moving Averages");

	prev = g_new(float_t, interval);

	get_data(sheet, range, &data_set);
	current = data_set.array;
	count = add_cursor = del_cursor = row = 0;
	sum = 0;

	while (current != NULL) {
	        prev[add_cursor] = *((float_t *) current->data);
		if (count == interval-1) {
			sum += prev[add_cursor];
			sprintf(buf, "%f", sum / interval);
			set_cell(dao, 0, row, buf);
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
			sprintf(buf, "#N/A");
			set_cell(dao, 0, row++, buf);
		}
		current = current->next;
	}

	g_free(prev);

	free_data_set(&data_set);

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
        float_t x;
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

int ranking_tool (Workbook *wb, Sheet *sheet, Range *input_range,
		  int columns_flag, data_analysis_output_t *dao)
{
        data_set_t *data_sets;
	GSList     *current, *inner;
	char       buf[256];
	int        vars, cols, rows, col, i, n;

	prepare_output(wb, dao, "Ranks");

	cols = input_range->end.col - input_range->start.col + 1;
	rows = input_range->end.row - input_range->start.row + 1;

	if (columns_flag) {
	        vars = cols;
		for (col=0; col<vars; col++) {
		        set_cell (dao, col*4, 0, "Point");
			if (dao->labels_flag) {
			        char *s;
			        Cell *cell = sheet_cell_get
				  (sheet, input_range->start.col+col, 
				   input_range->start.row);
				if (cell != NULL && cell->value != NULL) {
				        s = value_get_as_string(cell->value);
				        set_cell (dao, col*4+1, 0, s);
				}
			} else {
			        sprintf(buf, "Column %d", col+1);
				set_cell (dao, col*4+1, 0, buf);
			}
			set_cell (dao, col*4+2, 0, "Rank");
			set_cell (dao, col*4+3, 0, "Percent");
		}
		data_sets = g_new(data_set_t, vars);

		if (dao->labels_flag)
		        input_range->start.row++;

		for (i=0; i<vars; i++)
		        get_data_groupped_by_columns(sheet,
						     input_range, i, 
						     &data_sets[i]);
	} else {
	        vars = rows;
		for (col=0; col<vars; col++) {
		        set_cell (dao, col*4, 0, "Point");

			if (dao->labels_flag) {
			        char *s;
			        Cell *cell = sheet_cell_get
				  (sheet, input_range->start.col, 
				   input_range->start.row+col);
				if (cell != NULL && cell->value != NULL) {
				        s = value_get_as_string(cell->value);
				        set_cell (dao, col*4+1, 0, s);
				}
			} else {
		                sprintf(buf, "Row %d", col+1);
				set_cell (dao, col*4+1, 0, buf);
			}
			set_cell (dao, col*4+2, 0, "Rank");
			set_cell (dao, col*4+3, 0, "Percent");
		}
		data_sets = g_new(data_set_t, vars);

		if (dao->labels_flag)
		        input_range->start.col++;

		for (i=0; i<vars; i++)
		        get_data_groupped_by_rows(sheet,
						  input_range, i, 
						  &data_sets[i]);
	}

	for (i=0; i<vars; i++) {
	        rank_t *rank;
	        n = 0;
	        current = data_sets[i].array;
		rank = g_new(rank_t, data_sets[i].n);

		while (current != NULL) {
		        float_t x = *((float_t *) current->data);

			rank[n].point = n+1;
			rank[n].x = x;
			rank[n].rank = 1;
			rank[n].same_rank_count = -1;

			inner = data_sets[i].array;
			while (inner != NULL) {
			        float_t y = *((float_t *) inner->data);
				if (y > x)
				        rank[n].rank++;
				else if (y == x)
				        rank[n].same_rank_count++;
				inner = inner->next;
			}
			n++;
			current = current->next;
		}
		qsort ((rank_t *) rank, data_sets[i].n,
		       sizeof (rank_t), (void *) &rank_compare);

		for (n=0; n<data_sets[i].n; n++) {
			/* Point number */
			sprintf(buf, "%d", rank[n].point);
			set_cell (dao, i*4+0, n+1, buf);

			/* Value */
			sprintf(buf, "%f", rank[n].x);
			set_cell (dao, i*4+1, n+1, buf);

			/* Rank */
			sprintf(buf, "%d", rank[n].rank);
			set_cell (dao, i*4+2, n+1, buf);

			/* Percent */
			sprintf(buf, "%.2f%%", 
				100.0-(100.0 * (rank[n].rank-1)/
				       (data_sets[i].n-1)));
			set_cell (dao, i*4+3, n+1, buf);
		}
		g_free(rank);
	}

	for (i=0; i<vars; i++)
	        free_data_set(&data_sets[i]);
	g_free (data_sets);

	return 0;
}


/************* Anova: Single Factor Tool **********************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

int anova_single_factor_tool (Workbook *wb, Sheet *sheet, Range *range,
			      int columns_flag, float_t alpha, 
			      data_analysis_output_t *dao)
{
        data_set_t *data_sets;
	char       buf[256];
	int        vars, cols, rows, col, i;
	float_t    *mean, mean_total, sum_total, n_total, ssb, ssw, sst;
	float_t    ms_b, ms_w, f, p, f_c;
	int        df_b, df_w, df_t;

	prepare_output(wb, dao, "Anova");

	cols = range->end.col - range->start.col + 1;
	rows = range->end.row - range->start.row + 1;

	set_cell (dao, 0, 0, "Anova: Single Factor");
	set_cell (dao, 0, 2, "SUMMARY");
	set_cell (dao, 0, 3, "Groups");
	set_cell (dao, 1, 3, "Count");
	set_cell (dao, 2, 3, "Sum");
	set_cell (dao, 3, 3, "Average");
	set_cell (dao, 4, 3, "Variance");

	if (columns_flag) {
	        vars = cols;
		for (col=0; col<vars; col++) {
			if (dao->labels_flag) {
			        char *s;
			        Cell *cell = sheet_cell_get
				  (sheet, range->start.col+col, 
				   range->start.row);
				if (cell != NULL && cell->value != NULL) {
				        s = value_get_as_string(cell->value);
				        set_cell (dao, 0, col+4, s);
				}
			} else {
			        sprintf(buf, "Column %d", col+1);
				set_cell (dao, 0, col+4, buf);
			}
		}
		data_sets = g_new(data_set_t, vars);

		if (dao->labels_flag)
		        range->start.row++;

		for (i=0; i<vars; i++)
		        get_data_groupped_by_columns(sheet,
						     range, i, 
						     &data_sets[i]);
	} else {
	        vars = rows;
		for (col=0; col<vars; col++) {
			if (dao->labels_flag) {
			        char *s;
			        Cell *cell = sheet_cell_get
				  (sheet, range->start.col, 
				   range->start.row+col);
				if (cell != NULL && cell->value != NULL) {
				        s = value_get_as_string(cell->value);
				        set_cell (dao, 0, col+4, s);
				}
			} else {
		                sprintf(buf, "Row %d", col+1);
				set_cell (dao, 0, col+4, buf);
			}
		}
		data_sets = g_new(data_set_t, vars);

		if (dao->labels_flag)
		        range->start.col++;

		for (i=0; i<vars; i++)
		        get_data_groupped_by_rows(sheet,
						  range, i, 
						  &data_sets[i]);
	}

	/* SUMMARY */
	for (i=0; i<vars; i++) {
	        float_t v;

	        /* Count */
	        sprintf(buf, "%d", data_sets[i].n);
		set_cell (dao, 1, i+4, buf);

		/* Sum */
		sprintf(buf, "%f", data_sets[i].sum);
		set_cell (dao, 2, i+4, buf);

		/* Average */
		sprintf(buf, "%f", data_sets[i].sum / data_sets[i].n);
		set_cell (dao, 3, i+4, buf);

		/* Variance */
		v = (data_sets[i].sqrsum - data_sets[i].sum2/data_sets[i].n) /
		  (data_sets[i].n - 1);
		sprintf(buf, "%f", v);
		set_cell (dao, 4, i+4, buf);
	}

	set_cell (dao, 0, vars+6, "ANOVA");
	set_cell (dao, 0, vars+7, "Source of Variation");
	set_cell (dao, 1, vars+7, "SS");
	set_cell (dao, 2, vars+7, "df");
	set_cell (dao, 3, vars+7, "MS");
	set_cell (dao, 4, vars+7, "F");
	set_cell (dao, 5, vars+7, "P-value");
	set_cell (dao, 6, vars+7, "F critical");
	set_cell (dao, 0, vars+8, "Between Groups");
	set_cell (dao, 0, vars+9, "Within Groups");
	set_cell (dao, 0, vars+11, "Total");

	/* ANOVA */
	mean = g_new(float_t, vars);
	sum_total = n_total = 0;
	ssb = ssw = sst = 0;
	for (i=0; i<vars; i++) {
	        mean[i] = data_sets[i].sum / data_sets[i].n;
		sum_total += data_sets[i].sum;
		n_total += data_sets[i].n;
	}
	mean_total = sum_total / n_total;
	for (i=0; i<vars; i++) {
	        float_t t;
		t = mean[i] - mean_total;
		ssb += t * t * data_sets[i].n;
	}
	for (i=0; i<vars; i++) {
	        GSList  *current = data_sets[i].array;
		float_t t, x;

		while (current != NULL) {
			x = *((float_t *) current->data);
			t = x - mean[i];
		        ssw += t * t;
			t = x - mean_total;
			sst += t * t;
			current = current->next;
		}
	}
	df_b = cols-1;
	df_w = n_total - cols;
	df_t = n_total - 1;
	ms_b = ssb / df_b;
	ms_w = ssw / df_w;
	f    = ms_b / ms_w;
	p    = 1.0 - pf(f, df_b, df_w);
	f_c  = qf(alpha, df_b, df_w);

	sprintf(buf, "%f", ssb);
	set_cell (dao, 1, vars+8, buf);
	sprintf(buf, "%f", ssw);
	set_cell (dao, 1, vars+9, buf);
	sprintf(buf, "%f", sst);
	set_cell (dao, 1, vars+11, buf);
	sprintf(buf, "%d", df_b);
	set_cell (dao, 2, vars+8, buf);
	sprintf(buf, "%d", df_w);
	set_cell (dao, 2, vars+9, buf);
	sprintf(buf, "%d", df_t);
	set_cell (dao, 2, vars+11, buf);
	sprintf(buf, "%f", ms_b);
	set_cell (dao, 3, vars+8, buf);
	sprintf(buf, "%f", ms_w);
	set_cell (dao, 3, vars+9, buf);
	sprintf(buf, "%f", f);
	set_cell (dao, 4, vars+8, buf);
	sprintf(buf, "%f", p);
	set_cell (dao, 5, vars+8, buf);
	sprintf(buf, "%f", f_c);
	set_cell (dao, 6, vars+8, buf);

	g_free(mean);

	for (i=0; i<vars; i++)
	        free_data_set(&data_sets[i]);
	g_free (data_sets);

        return 0;
}


/************* Anova: Two-Factor Without Replication Tool ****************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

int anova_two_factor_without_r_tool (Workbook *wb, Sheet *sheet, Range *range,
				     float_t alpha, 
				     data_analysis_output_t *dao)
{
        data_set_t *data_sets;
	char       buf[256];
	int        cols, rows, i, n;
	float_t    *row_mean, *col_mean, mean, sum;
	float_t    ss_r, ss_c, ss_e, ss_t;
	float_t    ms_r, ms_c, ms_e, f1, f2, p1, p2, f1_c, f2_c;
	int        df_r, df_c, df_e, df_t;

	prepare_output(wb, dao, "Anova");

	cols = range->end.col - range->start.col + 1;
	rows = range->end.row - range->start.row + 1;

	set_cell (dao, 0, 0, "Anova: Two-Factor Without Replication");
	set_cell (dao, 0, 2, "SUMMARY");
	set_cell (dao, 1, 2, "Count");
	set_cell (dao, 2, 2, "Sum");
	set_cell (dao, 3, 2, "Average");
	set_cell (dao, 4, 2, "Variance");

	data_sets = g_new(data_set_t, cols);
	col_mean = g_new(float_t, cols);
	for (i=0; i<cols; i++) {
	        float_t v;

	        get_data_groupped_by_columns(sheet, range, i, &data_sets[i]);
	        sprintf(buf, "Column %d", i+1);
		set_cell (dao, 0, i+4+rows, buf);
	        sprintf(buf, "%d", data_sets[i].n);
		set_cell (dao, 1, i+4+rows, buf);
	        sprintf(buf, "%f", data_sets[i].sum);
		set_cell (dao, 2, i+4+rows, buf);
	        sprintf(buf, "%f", data_sets[i].sum/data_sets[i].n);
		set_cell (dao, 3, i+4+rows, buf);
		v = (data_sets[i].sqrsum - data_sets[i].sum2/data_sets[i].n) /
		  (data_sets[i].n - 1);
		sprintf(buf, "%f", v);
		set_cell (dao, 4, i+4+rows, buf);
		col_mean[i] = data_sets[i].sum / data_sets[i].n;
	}
	g_free(data_sets);

	data_sets = g_new(data_set_t, rows);
	row_mean = g_new(float_t, rows);
	for (i=n=sum=0; i<rows; i++) {
	        float_t v;

	        get_data_groupped_by_rows(sheet, range, i, &data_sets[i]);
	        sprintf(buf, "Row %d", i+1);
		set_cell (dao, 0, i+3, buf);
	        sprintf(buf, "%d", data_sets[i].n);
		set_cell (dao, 1, i+3, buf);
	        sprintf(buf, "%f", data_sets[i].sum);
		set_cell (dao, 2, i+3, buf);
	        sprintf(buf, "%f", data_sets[i].sum/data_sets[i].n);
		set_cell (dao, 3, i+3, buf);
		v = (data_sets[i].sqrsum - data_sets[i].sum2/data_sets[i].n) /
		  (data_sets[i].n - 1);
		sprintf(buf, "%f", v);
		set_cell (dao, 4, i+3, buf);
		n += data_sets[i].n;
		sum += data_sets[i].sum;
		row_mean[i] = data_sets[i].sum / data_sets[i].n;
	}
	ss_e = ss_t = 0;
	mean = sum / n;
	for (i=0; i<rows; i++) {
	        GSList *current = data_sets[i].array;
		float_t t, x;
		n = 0;

	        while (current != NULL) {
		        x = *((float_t *) current->data);
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
	g_free(data_sets);

	ss_r = ss_c = 0;
	for (i=0; i<rows; i++) {
	        float_t t;

		t = row_mean[i] - mean;
		t *= t;
	        ss_r += t;
	}
	ss_r *= cols;
	for (i=0; i<cols; i++) {
	        float_t t;

		t = col_mean[i] - mean;
		t *= t;
	        ss_c += t;
	}
	g_free(col_mean);
	g_free(row_mean);

	ss_c *= rows;

	df_r = rows-1;
	df_c = cols-1;
	df_e = (rows-1) * (cols-1);
	df_t = (rows*cols)-1;
	ms_r = ss_r / df_r;
	ms_c = ss_c / df_c;
	ms_e = ss_e / df_e;
	f1   = ms_r / ms_e;
	f2   = ms_c / ms_e;
	p1   = 1.0 - pf(f1, df_r, df_e);
	p2   = 1.0 - pf(f2, df_c, df_e);
	f1_c = qf(alpha, df_r, df_e);
	f2_c = qf(alpha, df_c, df_e);

	set_cell (dao, 0, 6+rows+cols, "ANOVA");
	set_cell (dao, 0, 7+rows+cols, "Source of Variation");
	set_cell (dao, 1, 7+rows+cols, "SS");
	set_cell (dao, 2, 7+rows+cols, "df");
	set_cell (dao, 3, 7+rows+cols, "MS");
	set_cell (dao, 4, 7+rows+cols, "F");
	set_cell (dao, 5, 7+rows+cols, "P-value");
	set_cell (dao, 6, 7+rows+cols, "F critical");
	set_cell (dao, 0, 8+rows+cols, "Rows");
	set_cell (dao, 0, 9+rows+cols, "Columns");
	set_cell (dao, 0, 10+rows+cols, "Error");
	set_cell (dao, 0, 12+rows+cols, "Total");

	sprintf(buf, "%f", ss_r);
	set_cell (dao, 1, 8+rows+cols, buf);
	sprintf(buf, "%f", ss_c);
	set_cell (dao, 1, 9+rows+cols, buf);
	sprintf(buf, "%f", ss_e);
	set_cell (dao, 1, 10+rows+cols, buf);
	sprintf(buf, "%f", ss_t);
	set_cell (dao, 1, 12+rows+cols, buf);
	sprintf(buf, "%d", df_r);
	set_cell (dao, 2, 8+rows+cols, buf);
	sprintf(buf, "%d", df_c);
	set_cell (dao, 2, 9+rows+cols, buf);
	sprintf(buf, "%d", df_e);
	set_cell (dao, 2, 10+rows+cols, buf);
	sprintf(buf, "%d", df_t);
	set_cell (dao, 2, 12+rows+cols, buf);
	sprintf(buf, "%f", ms_r);
	set_cell (dao, 3, 8+rows+cols, buf);
	sprintf(buf, "%f", ms_c);
	set_cell (dao, 3, 9+rows+cols, buf);
	sprintf(buf, "%f", ms_e);
	set_cell (dao, 3, 10+rows+cols, buf);
	sprintf(buf, "%f", f1);
	set_cell (dao, 4, 8+rows+cols, buf);
	sprintf(buf, "%f", f2);
	set_cell (dao, 4, 9+rows+cols, buf);
	sprintf(buf, "%f", p1);
	set_cell (dao, 5, 8+rows+cols, buf);
	sprintf(buf, "%f", p2);
	set_cell (dao, 5, 9+rows+cols, buf);
	sprintf(buf, "%f", f1_c);
	set_cell (dao, 6, 8+rows+cols, buf);
	sprintf(buf, "%f", f2_c);
	set_cell (dao, 6, 9+rows+cols, buf);

	return 0;
}
