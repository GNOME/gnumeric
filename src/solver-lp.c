/*
 * solver-lp:  Linear programming methods.
 *
 * Author:
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
*/
#include <config.h>
#include <gnome.h>
#include <math.h>
#include <stdlib.h>
#include "numbers.h"
#include "gnumeric.h"
#include "utils.h"
#include "solver.h"
#include "func.h"
#include "cell.h"
#include "eval.h"



/************************************************************************
 *
 * S I M P L E X   Algorithm
 *
 *
 */


/* STEP 1: Construct an initial solution table.
 */
static float_t *
simplex_step_one(Sheet *sheet, int target_col, int target_row,
		 CellList *inputs, GSList *constraints, int *table_cols,
		 int *table_rows, gboolean max_flag)
{
        SolverConstraint *c;
        CellList *input_list = inputs;
	GSList   *current;
        Cell     *cell, *target, *lhs, *rhs;
	float_t  init_value, value, *table;
        int      n, i, n_neq_c, n_eq_c, size, n_vars;

	for (n = 0; inputs != NULL; inputs = inputs->next)
		n++;

	n_neq_c = n_eq_c = 0;
	for (current = constraints; current != NULL; current = current->next) {
	        c = (SolverConstraint *) current->data;
		if (strcmp(c->type, "=") == 0)
		         ++n_eq_c;
		else
		         ++n_neq_c;
	}

	n_vars = n;
	if (n_vars < 1 || n_neq_c + n_eq_c < 1)
	        return NULL;

	size = *table_cols = 2 + n + n_neq_c;
	*table_rows = 1 + n_neq_c + n_eq_c;
	size *= *table_rows;

	table = g_new(float_t, size);
	for (i=0; i<size; i++)
	        table[i] = 0;

	inputs = input_list;
	target = sheet_cell_fetch(sheet, target_col, target_row);
	for (i=2; inputs != NULL; inputs = inputs->next) {
	        cell = (Cell *) inputs->data;

		cell_set_value (cell, value_new_float (0.0));
		cell_eval_content(target);
		init_value = value_get_as_float(target->value);

		current = constraints;
		n = 1;
		while (current != NULL) {
		        c = (SolverConstraint *) current->data;
			lhs = sheet_cell_fetch(sheet, c->lhs_col, c->lhs_row);
			cell_eval_content(lhs);
			table[i + n**table_cols] =
			        -value_get_as_float(lhs->value);
			current = current->next;
			++n;
		}

		cell_set_value (cell, value_new_float (1.0));
		cell_eval_content(target);
		value = value_get_as_float(target->value);
		current = constraints;
		n = 1;
		while (current != NULL) {
		        c = (SolverConstraint *) current->data;
			lhs = sheet_cell_fetch(sheet, c->lhs_col, c->lhs_row);
			cell_eval_content(lhs);
			table[i + n * *table_cols] +=
			        value_get_as_float(lhs->value);
			current = current->next;
			++n;
		}

		cell_set_value (cell, value_new_float (0.0));

		if (max_flag)
		        table[i] = value - init_value;
		else
		        table[i] = init_value - value;
		++i;
	}

	n = 1;
	i = 1;
	while (constraints != NULL) {
	        c = (SolverConstraint *) constraints->data;
	        table[i * *table_cols] = i-1 + n_vars;
		rhs = sheet_cell_fetch(sheet, c->rhs_col, c->rhs_row);
		table[1 + i * *table_cols] = value_get_as_float(rhs->value);
		if (strcmp(c->type, "<=") == 0) {
		        table[1 + n_vars + n + i* *table_cols] = 1;
			++n;
		} else if (strcmp(c->type, ">=") == 0) {
		        table[1 + n_vars + n + i* *table_cols] = -1;
			++n;
		}
	        printf ("%-30s (col=%d, row=%d  %s  col=%d, row=%d\n",
		       c->str, c->lhs_col, c->lhs_row,
		       c->type, c->rhs_col, c->rhs_row);
		constraints = constraints->next;
		i++;
	}

	return table;
}

/* STEP 2: Find the pivot column.  Most positive rule is applied.
 */
static int
simplex_step_two(float_t *table, int tbl_cols, int *status)
{
        float_t max = 0;
	int     i, ind = -1;

	table += 2;
	for (i=0; i<tbl_cols-2; i++)
	        if (table[i] > max) {
		        max = table[i];
			ind = i;
		}

	if (ind == -1)
	        *status = SIMPLEX_DONE;

	return ind;
}

/* STEP 3: Find the pivot row.
 */
static int
simplex_step_three(float_t *table, int col, int tbl_cols, int tbl_rows,
		   int *status)
{
        float_t a, min, test;
	int     i, min_i=-1;

	table += tbl_cols;
	for (i=0; i<tbl_rows-1; i++) {
	        a = table[2 + col + i*tbl_cols];
		if (a > 0) {
		        test = table[1 + i*tbl_cols] / a;
			if (min_i == -1 || test < min) {
			        min = test;
				min_i = i;
			}
		}
		
	}

	if (min_i == -1 || min < 0)
	        *status = SIMPLEX_UNBOUNDED;

	return min_i;
}

/* STEP 4: Perform pivot operations.
 */
static void
simplex_step_four(float_t *table, int col, int row, int tbl_cols, int tbl_rows)
{
        float_t pivot, *pivot_row, c;
	int     i, j;

	pivot = table[2 + col + (row+1)*tbl_cols];
	pivot_row = &table[1 + (row+1)*tbl_cols];
	table[(row+1)*tbl_cols] = col;

	for (i=0; i<tbl_cols-1; i++)
	        pivot_row[i] /= pivot;

	for (i=0; i<tbl_rows; i++)
	        if (i != row+1) {
		        c = table[2 + col + i*tbl_cols];
			for (j=0; j<tbl_cols-1; j++)
			        table[j + 1 + i*tbl_cols] -= pivot_row[j] * c;
		}
}

static void
display_table(float_t *table, int cols, int rows)
{
        int i, n;

	for (i=0; i<rows; i++) {
	        for (n=0; n<cols; n++)
		        printf("%5.1f ", table[n+i*cols]);
		printf("\n");
	}
	printf("\n");
}

int solver_simplex (Workbook *wb, Sheet *sheet)
{
        int i, n;
	SolverParameters *param = &sheet->solver_parameters;
        CellList *cell_list = param->input_cells;
	GSList   *constraints = param->constraints;
	Cell     *cell;

	float_t  *table;
	int      col, row, status, tbl_rows, tbl_cols;
	gboolean max_flag;

	max_flag = param->problem_type == SolverMaximize;
	status = SIMPLEX_OK;

	table = simplex_step_one(sheet, 
				 param->target_cell->col->pos,
				 param->target_cell->row->pos,
				 cell_list, constraints, 
				 &tbl_cols, &tbl_rows, max_flag);

	if (table == NULL)
	        return SIMPLEX_UNBOUNDED;

	for (;;) {
	        display_table (table, tbl_cols, tbl_rows);
	        col = simplex_step_two(table, tbl_cols, &status);

		if (status == SIMPLEX_DONE)
		        break;
		row = simplex_step_three(table, col, tbl_cols, tbl_rows,
					 &status);

		if (status == SIMPLEX_UNBOUNDED)
		        break;
		simplex_step_four(table, col, row, tbl_cols, tbl_rows);
	}

	if (status != SIMPLEX_DONE)
	        return status;

	for (i=1; i<tbl_rows; i++) {
	        Cell *c;
	        cell_list = param->input_cells;
		c = (Cell *) cell_list->data;
	        for (n=0; n < (int) table[i*tbl_cols]; n++) {
			cell_list = cell_list->next;
			if (cell_list == NULL)
			        goto skip;
		        c = (Cell *) cell_list->data;
		}
		cell = sheet_cell_fetch(sheet, c->col->pos, c->row->pos);
		cell_set_value (cell, value_new_float (table[1+i*tbl_cols]));
	skip:
	}
	cell = sheet_cell_fetch(sheet, param->target_cell->col->pos, 
				param->target_cell->row->pos);
	cell_eval_content(cell);

	/* FIXME: Do not do the following loop.  Instead recalculate 
	 * everything that depends on the input variables (the list of
	 * cells in params->input_cells).
	 */

	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *) constraints->data;

		cell = sheet_cell_fetch(sheet, c->lhs_col, c->lhs_row);
		cell_eval_content(cell);
		constraints = constraints->next;
	}

        return SIMPLEX_DONE;
}
