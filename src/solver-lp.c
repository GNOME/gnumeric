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
#include "gnumeric-sheet.h"
#include "utils.h"
#include "solver.h"
#include "func.h"
#include "cell.h"
#include "eval.h"



/************************************************************************
 *
 * S I M P L E X
 *
 *
 */


/* STEP 1: Construct an initial solution table.
 */
static float_t *
simplex_step_one(Sheet *sheet, int target_col, int target_row,
		 CellList *inputs, int *n_vars, 
		 GSList *constraints, int *n_constraints, gboolean max_flag)
{
        SolverConstraint *c;
        CellList *input_list = inputs;
	GSList   *current;
        Cell     *cell, *target, *lhs, *rhs;
	float_t  init_value, value, *table;
        int      n, i, step;

	for (n = 0; inputs != NULL; inputs = inputs->next)
		n++;
	*n_vars = n;
	*n_constraints = g_slist_length(constraints);
	if (n < 1 || *n_constraints < 1)
	        return NULL;

	step = n + *n_constraints + 2;

	table = g_new(float_t, (*n_constraints + 1) * step);
	for (i=0; i<(*n_constraints + 1)*step; i++)
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
			lhs = sheet_cell_fetch(sheet, c->lhs->col->pos,
					       c->lhs->row->pos);
			cell_eval_content(lhs);
			table[i + n*step] = -value_get_as_float(lhs->value);
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
			lhs = sheet_cell_fetch(sheet, c->lhs->col->pos,
					       c->lhs->row->pos);
			cell_eval_content(lhs);
			table[i + n*step] += value_get_as_float(lhs->value);
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
	while (constraints != NULL) {
	        c = (SolverConstraint *) constraints->data;
	        table[n*step] = n-1 + *n_vars;
		rhs = sheet_cell_fetch(sheet, c->rhs->col->pos,
				       c->rhs->row->pos);
		if (strcmp(c->type, "<=") == 0)
		        table[1 + n*step] = value_get_as_float(rhs->value);
		else if (strcmp(c->type, ">=") == 0)
		        table[1 + n*step] = -value_get_as_float(rhs->value);

	        table[1 + *n_vars + n + n*step] = 1;
	        printf ("%-30s (col=%d, row=%d  %s  col=%d, row=%d\n",
		       c->str, c->lhs->col->pos, c->lhs->row->pos,
		       c->type, c->rhs->col->pos, c->rhs->row->pos);
		constraints = constraints->next;
		++n;
	}

	return table;
}

/* STEP 2: Find the pivot column.  Most positive rule is applied.
 */
static int
simplex_step_two(float_t *table, int n_vars, int *status)
{
        float_t max = 0;
	int     i, ind = -1;

	table += 2;
	for (i=0; i<n_vars; i++)
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
simplex_step_three(float_t *table, int col, int n_vars, int n_rows,
		   int *status)
{
        float_t min, test;
	int     i, step, min_i;

	step = n_rows + n_vars + 2;
	table += step;
	min = table[1] / table[2+col];
	min_i = 0;

	for (i=1; i<n_rows; i++) {
	        test = table[1 + i*step] / table[2 + col + i*step];
		if (test < min) {
		        min = test;
			min_i = i;
		}
	}

	if (min < 0)
	        *status = SIMPLEX_UNBOUNDED;

	return min_i;
}

/* STEP 4: Perform pivot operations.
 */
static void
simplex_step_four(float_t *table, int col, int row, int n_vars, int n_rows)
{
        float_t pivot, *pivot_row, c;
	int     i, j, step;

	step = n_rows + n_vars + 2;
	pivot = table[2 + col + (row+1)*step];
	pivot_row = &table[1 + (row+1)*step];
	table[(row+1)*step] = col;

	for (i=0; i<n_vars+n_rows+1; i++)
	        pivot_row[i] /= pivot;

	for (i=0; i<n_rows+1; i++)
	        if (i != row+1) {
		        c = table[2 + col + i*step];
			for (j=0; j<n_vars+n_rows+1; j++)
			        table[j + 1 + i*step] -= pivot_row[j] * c;
		}
}

int solver_simplex (Workbook *wb, Sheet *sheet)
{
        int i, j, n;
	SolverParameters *param = &sheet->solver_parameters;
        CellList *cell_list = param->input_cells;
	GSList   *constraints = param->constraints;
	Cell     *cell;

	float_t *table;
	int     col, row, status, step;
	int     n_vars;
	int     n_constraints;
	gboolean max_flag;

	max_flag = param->problem_type == SolverMaximize;
	status = SIMPLEX_OK;

	table = simplex_step_one(sheet, 
				 param->target_cell->col->pos,
				 param->target_cell->row->pos,
				 cell_list, &n_vars,
				 constraints, &n_constraints, max_flag);

	if (table == NULL)
	        return SIMPLEX_UNBOUNDED;

	step = n_vars + n_constraints + 2;
	for (;;) {
	        col = simplex_step_two(table, n_vars, &status);
		if (status == SIMPLEX_DONE)
		        break;
		row = simplex_step_three(table, col, n_vars,
					 n_constraints, &status);
		if (status == SIMPLEX_UNBOUNDED)
		        break;
		simplex_step_four(table, col, row, n_vars, n_constraints);
	}

	if (status != SIMPLEX_DONE)
	        return status;

	for (i=1; i<n_constraints+1; i++) {
	        Cell *c;
	        cell_list = param->input_cells;
		c = (Cell *) cell_list->data;
	        for (n=0; n < (int) table[i*step]; n++) {
			cell_list = cell_list->next;
		        c = (Cell *) cell_list->data;
		}
		cell = sheet_cell_fetch(sheet, c->col->pos, c->row->pos);
		cell_set_value (cell, value_new_float (table[1+i*step]));
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

		cell = sheet_cell_fetch(sheet, c->lhs->col->pos,
					c->lhs->row->pos);
		cell_eval_content(cell);
		constraints = constraints->next;
	}

        return SIMPLEX_DONE;
}
