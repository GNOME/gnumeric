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
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"



typedef struct {
        GSList  *array;
        float_t sum;
        float_t sum2;    /* square of the sum */
        float_t sqrsum;
        int     n;
} data_set_t;



/***** Some general routines ***********************************************/

static Cell *
set_cell (Sheet *sheet, int col, int row, char *text)
{
        Cell *cell;
	cell = sheet_cell_get (sheet, col, row);
	if (cell == NULL)
	        cell = sheet_cell_new (sheet, col, row);
	cell_set_text (cell, text);

	return cell;
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

	for (row=range->start_row; row<=range->end_row; row++) {
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

	for (col=range->start_col; col<=range->end_col; col++) {
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


/************* Correlation Tool *******************************************
 *
 * The correlation tool calculates the correlation coefficient of two
 * data sets.  The two data sets can be groupped by rows or by columns.
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 * TODO: a new workbook output and output to an existing sheet
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
void
correlation_tool (Workbook *wb, Sheet *current_sheet, 
		  Range *input_range, int columns_flag)
{
        data_set_t *data_sets;
        Sheet      *sheet;
	char       buf[256];
	Cell       *cell;
	int        vars, cols, rows, col, row, i;
	int        error;

	sheet = sheet_new(wb, "Correlations");
	workbook_attach_sheet(wb, sheet);

	cols = input_range->end_col - input_range->start_col + 1;
	rows = input_range->end_row - input_range->start_row + 1;

	set_cell (sheet, 0, 0, "");

	if (columns_flag) {
	        vars = cols;
		for (col=0; col<vars; col++) {
		        sprintf(buf, "Column %d", col+1);
			cell = set_cell (sheet, 0, col+1, buf);
		}
		for (row=0; row<vars; row++) {
		        sprintf(buf, "Column %d", row+1);
			cell = set_cell (sheet, 1+row, 0, buf);
		}
		data_sets = g_new(data_set_t, vars);

		for (i=0; i<vars; i++)
		        get_data_groupped_by_columns(current_sheet,
						     input_range, i, 
						     &data_sets[i]);
	} else {
	        vars = rows;
		for (col=0; col<vars; col++) {
		        sprintf(buf, "Row %d", col+1);
			cell = set_cell (sheet, 0, col+1, buf);
		}
		for (row=0; row<vars; row++) {
		        sprintf(buf, "Row %d", row+1);
			cell = set_cell (sheet, 1+row, 0, buf);
		}
		data_sets = g_new(data_set_t, vars);

		for (i=0; i<vars; i++)
		        get_data_groupped_by_rows(current_sheet,
						  input_range, i, 
						  &data_sets[i]);
	}

	for (row=0; row<vars; row++) {
		  for (col=0; col<vars; col++) {
		        if (row == col) {
			        set_cell (sheet, col+1, row+1, "1");
				break;
			} else {
			        sprintf(buf, "%f", correl(&data_sets[col],
							  &data_sets[row],
							  &error));
				if (error)
				        set_cell (sheet, col+1, row+1, "--");
				else
				        set_cell (sheet, col+1, row+1, buf);
			}
		}
	}

	for (i=0; i<vars; i++)
	        free_data_set(&data_sets[i]);
}



/************* Covariance Tool ********************************************
 *
 * The covariance tool calculates the covariance of two data sets.
 * The two data sets can be groupped by rows or by columns.  The
 * results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 * TODO: a new workbook output and output to an existing sheet
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
void
covariance_tool (Workbook *wb, Sheet *current_sheet, 
		 Range *input_range, int columns_flag)
{
        data_set_t *data_sets;
        Sheet      *sheet;
	char       buf[256];
	Cell       *cell;
	int        vars, cols, rows, col, row, i;
	int        error;

	sheet = sheet_new(wb, "Covariances");
	workbook_attach_sheet(wb, sheet);

	cols = input_range->end_col - input_range->start_col + 1;
	rows = input_range->end_row - input_range->start_row + 1;

	set_cell (sheet, 0, 0, "");

	if (columns_flag) {
	        vars = cols;
		for (col=0; col<vars; col++) {
		        sprintf(buf, "Column %d", col+1);
			cell = set_cell (sheet, 0, col+1, buf);
		}
		for (row=0; row<vars; row++) {
		        sprintf(buf, "Column %d", row+1);
			cell = set_cell (sheet, 1+row, 0, buf);
		}
		data_sets = g_new(data_set_t, vars);

		for (i=0; i<vars; i++)
		        get_data_groupped_by_columns(current_sheet,
						     input_range, i, 
						     &data_sets[i]);
	} else {
	        vars = rows;
		for (col=0; col<vars; col++) {
		        sprintf(buf, "Row %d", col+1);
			cell = set_cell (sheet, 0, col+1, buf);
		}
		for (row=0; row<vars; row++) {
		        sprintf(buf, "Row %d", row+1);
			cell = set_cell (sheet, 1+row, 0, buf);
		}
		data_sets = g_new(data_set_t, vars);

		for (i=0; i<vars; i++)
		        get_data_groupped_by_rows(current_sheet,
						  input_range, i, 
						  &data_sets[i]);
	}

	for (row=0; row<vars; row++) {
		  for (col=0; col<vars; col++) {
		        if (row == col) {
			        set_cell (sheet, col+1, row+1, "1");
				break;
			} else {
			        sprintf(buf, "%f", covar(&data_sets[col],
							 &data_sets[row],
							  &error));
				if (error)
				        set_cell (sheet, col+1, row+1, "--");
				else
				        set_cell (sheet, col+1, row+1, buf);
			}
		}
	}

	for (i=0; i<vars; i++)
	        free_data_set(&data_sets[i]);
}
