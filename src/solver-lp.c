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
#include "dialogs.h"
#include "mstyle.h"



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

/* STEP 2a: Convert negative RHS's to positive.
 */
static void
simplex_step_bvs(float_t *table, int tbl_cols, int tbl_rows)
{
        int i, n;

	for (i=1; i<tbl_rows; i++)
	        if (table[i*tbl_cols + 1] < 0)
		        for (n=1; n<tbl_cols; n++)
			        table[i*tbl_cols + n] *= -1;
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
        float_t a, min=0, test;
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

static float_t *
simplex_copy_table (float_t *table, int cols, int rows)
{
        float_t *tbl;
        int     i;

	tbl = g_new (float_t, cols*rows);
	for (i=0; i<cols*rows; i++)
	        tbl[i] = table[i];

	return tbl;
}

int solver_simplex (Workbook *wb, Sheet *sheet, float_t **init_tbl,
		    float_t **final_tbl)
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

	*init_tbl = simplex_copy_table (table, tbl_cols, tbl_rows);

	for (;;) {
	        display_table (table, tbl_cols, tbl_rows);
		simplex_step_bvs(table, tbl_cols, tbl_rows);
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

	*final_tbl = table;

        return SIMPLEX_DONE;
}

static char *
find_name (Sheet *sheet, int col, int row)
{
        static char *str = NULL;
        Cell        *cell;
	char        *col_str, *row_str;
        int         col_n, row_n;

	for (col_n = col-1; col_n >= 0; col_n--) {
	        cell = sheet_cell_get (sheet, col_n, row);
		if (cell && !VALUE_IS_NUMBER (cell->value))
		        break;
	}
	if (col_n >= 0)
	        col_str = value_get_as_string (cell->value);
	else
	        col_str = g_strdup ("");

	for (row_n = row-1; row_n >= 0; row_n--) {
	        cell = sheet_cell_get (sheet, col, row_n);
		if (cell && !VALUE_IS_NUMBER (cell->value))
		        break;
	}

	if (row_n >= 0)
	        row_str = value_get_as_string (cell->value);
	else
	        row_str = g_strdup ("");

	if (str)
	        g_free (str);
	str = g_new (char, strlen(col_str) + strlen(row_str) + 2);

	if (*col_str)
	        sprintf(str, "%s %s", col_str, row_str);
	else
	        sprintf(str, "%s", row_str);

	g_free (col_str);
	g_free (row_str);

	return str;
}

static void
solver_answer_report (Workbook *wb, Sheet *sheet, GSList *ov,
		      float_t ov_target)
{
        data_analysis_output_t dao;
	SolverParameters       *param = &sheet->solver_parameters;
	GSList                 *constraints;
        CellList               *cell_list = param->input_cells;
	
	Cell *cell;
	char buf[256];
	char *str;
	int  row;

	dao.type = NewSheetOutput;
        prepare_output (wb, &dao, _("Answer Report"));
	set_cell (&dao, 0, 0, _("Gnumeric Solver Answer Report"));
	if (param->problem_type == SolverMaximize)
	        set_cell (&dao, 0, 1, _("Target Cell (Maximize)"));
	else
	        set_cell (&dao, 0, 1, _("Target Cell (Minimize)"));
	set_cell (&dao, 0, 2, _("Cell"));
	set_cell (&dao, 1, 2, _("Name"));
	set_cell (&dao, 2, 2, _("Original Value"));
	set_cell (&dao, 3, 2, _("Final Value"));

	/* Set `Cell' field */
	set_cell (&dao, 0, 3, (char*) cell_name(param->target_cell->col->pos,
						param->target_cell->row->pos));

	/* Set `Name' field */
	set_cell (&dao, 1, 3, find_name (sheet, param->target_cell->col->pos,
					 param->target_cell->row->pos));

	/* Set `Original Value' field */
	sprintf (buf, "%f", ov_target);
	set_cell (&dao, 2, 3, buf);

	/* Set `Final Value' field */
	cell = sheet_cell_fetch (sheet, param->target_cell->col->pos,
				 param->target_cell->row->pos);
	str = value_get_as_string (cell->value);
	set_cell (&dao, 3, 3, str);
	g_free (str);
	
	row = 4;
	set_cell (&dao, 0, row++, _("Adjustable Cells"));
	set_cell (&dao, 0, row, _("Cell"));
	set_cell (&dao, 1, row, _("Name"));
	set_cell (&dao, 2, row, _("Original Value"));
	set_cell (&dao, 3, row, _("Final Value"));
	row++;

	while (cell_list != NULL) {
	        char *str = (char *) ov->data;
	        cell = (Cell *) cell_list->data;

		/* Set `Cell' column */
		set_cell (&dao, 0, row, (char *) cell_name(cell->col->pos,
							   cell->row->pos));

		/* Set `Name' column */
		set_cell (&dao, 1, row, find_name (sheet, cell->col->pos,
						   cell->row->pos));

		/* Set `Original Value' column */
		set_cell (&dao, 2, row, str);

		/* Set `Final Value' column */
		cell = sheet_cell_fetch (sheet, cell->col->pos,
					 cell->row->pos);
		str = value_get_as_string (cell->value);
		set_cell (&dao, 3, row, str);
		g_free (str);

		/* Go to next row */
	        cell_list = cell_list->next;
		ov = ov->next;
		row++;
	}

	set_cell (&dao, 0, row++, _("Constraints"));
	set_cell (&dao, 0, row, _("Cell"));
	set_cell (&dao, 1, row, _("Name"));
	set_cell (&dao, 2, row, _("Cell Value"));
	set_cell (&dao, 3, row, _("Formula"));
	set_cell (&dao, 4, row, _("Status"));
	set_cell (&dao, 5, row, _("Slack"));

	row++;
	constraints = param->constraints;
	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *) constraints->data;
		float_t lhs, rhs;

		/* Set `Cell' column */
		set_cell (&dao, 0, row,
			  (char *) cell_name (c->lhs_col, c->lhs_row));

		/* Set `Name' column */
		set_cell (&dao, 1, row, find_name (sheet, c->lhs_col,
						   c->lhs_row));

		/* Set `Cell Value' column */
		cell = sheet_cell_fetch (sheet, c->rhs_col, c->rhs_row);
		str = value_get_as_string (cell->value);
		rhs = value_get_as_float (cell->value);
		set_cell (&dao, 2, row, str);
		g_free (str);

		/* Set `Formula' column */
		set_cell (&dao, 3, row, c->str);

		/* Set `Status' column */
		cell = sheet_cell_fetch (sheet, c->lhs_col, c->lhs_row);
		lhs = value_get_as_float (cell->value);

		if (fabs (lhs-rhs) < 0.0001)
		        set_cell (&dao, 4, row, _("Binding"));
		else
		        set_cell (&dao, 4, row, _("Not Binding"));

		/* Set `Slack' column */
		sprintf(buf, "%f", fabs (lhs-rhs));
		set_cell (&dao, 5, row, buf);

		/* Go to next row */
		++row;
		constraints = constraints->next;
	}
}

static void
solver_sensitivity_report (Workbook *wb, Sheet *sheet, float_t *init_tbl,
			   float_t *final_tbl)
{
        data_analysis_output_t dao;
	SolverParameters       *param = &sheet->solver_parameters;
	GSList                 *constraints;
        CellList               *cell_list = param->input_cells;

	Cell *cell;
	char *str, buf[256];
	int  row=0, i;

	dao.type = NewSheetOutput;
        prepare_output (wb, &dao, _("Sensitivity Report"));
	set_cell (&dao, 0, row++, _("Gnumeric Solver Sensitivity Report"));
	set_cell (&dao, 0, row++, _("Adjustable Cells"));
	set_cell (&dao, 2, row, _("Final"));
	set_cell (&dao, 3, row, _("Reduced"));
	set_cell (&dao, 4, row, _("Objective"));
	set_cell (&dao, 5, row, _("Allowable"));
	set_cell (&dao, 6, row++, _("Allowable"));
	set_cell (&dao, 0, row, _("Cell"));
	set_cell (&dao, 1, row, _("Name"));
	set_cell (&dao, 2, row, _("Value"));
	set_cell (&dao, 3, row, _("Cost"));
	set_cell (&dao, 4, row, _("Coefficient"));
	set_cell (&dao, 5, row, _("Increase"));
	set_cell (&dao, 6, row++, _("Decrease"));

	i = 2;
	while (cell_list != NULL) {
	        cell = (Cell *) cell_list->data;

		/* Set `Cell' column */
		set_cell (&dao, 0, row, (char *) cell_name(cell->col->pos,
							   cell->row->pos));

		/* Set `Name' column */
		set_cell (&dao, 1, row, find_name (sheet, cell->col->pos,
						   cell->row->pos));

		/* Set `Final Value' column */
		cell = sheet_cell_fetch (sheet, cell->col->pos,
					 cell->row->pos);
		str = value_get_as_string (cell->value);
		set_cell (&dao, 2, row, str);
		g_free (str);

		/* Set `Reduced Cost' column */
		sprintf(buf, "%f", final_tbl[i]);
		set_cell (&dao, 3, row, buf);

		/* Set `Objective Coefficient' column */
		sprintf(buf, "%f", init_tbl[i]);
		set_cell (&dao, 4, row, buf);

		/* Go to next row */
	        cell_list = cell_list->next;
		row++;
		i++;
	}

	set_cell (&dao, 0, row++, _("Constraints"));
	set_cell (&dao, 2, row, _("Final"));
	set_cell (&dao, 3, row, _("Shadow"));
	set_cell (&dao, 4, row, _("Constraint"));
	set_cell (&dao, 5, row, _("Allowable"));
	set_cell (&dao, 6, row++, _("Allowable"));
	set_cell (&dao, 0, row, _("Cell"));
	set_cell (&dao, 1, row, _("Name"));
	set_cell (&dao, 2, row, _("Value"));
	set_cell (&dao, 3, row, _("Price"));
	set_cell (&dao, 4, row, _("R.H. Side"));
	set_cell (&dao, 5, row, _("Increase"));
	set_cell (&dao, 6, row++, _("Decrease"));
	
	constraints = param->constraints;
	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *) constraints->data;

		/* Set `Cell' column */
		set_cell (&dao, 0, row,
			  (char *) cell_name (c->lhs_col, c->lhs_row));

		/* Set `Name' column */
		set_cell (&dao, 1, row, find_name (sheet, c->lhs_col,
						   c->lhs_row));

		/* Set `Final Value' column */
		cell = sheet_cell_fetch (sheet, c->lhs_col, c->lhs_row);
		str = value_get_as_string (cell->value);
		set_cell (&dao, 2, row, str);
		g_free (str);

		/* Set `Shadow Prize' column */
		sprintf(buf, "%f", -final_tbl[i]);
		set_cell (&dao, 3, row, buf);

		/* Set `R.H. Side Value' column */
		cell = sheet_cell_fetch (sheet, c->rhs_col, c->rhs_row);
		str = value_get_as_string (cell->value);
		set_cell (&dao, 4, row, str);
		g_free (str);

		/* Go to next row */
		++row;
		++i;
		constraints = constraints->next;
	}
}

static void
solver_limits_report (Workbook *wb)
{
}

void
solver_lp_reports (Workbook *wb, Sheet *sheet, GSList *ov, float_t ov_target,
		   float_t *init_table, float_t *final_table,
		   gboolean answer, gboolean sensitivity, gboolean limits)
{
        if (answer)
	        solver_answer_report (wb, sheet, ov, ov_target);
	if (sensitivity)
	        solver_sensitivity_report (wb, sheet, init_table, final_table);
	if (limits)
	        solver_limits_report (wb);
}

