/*
 * solver-lp:  Linear programming methods.
 *
 * Author:
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * (C) Copyright 1999, 2000 by Jukka-Pekka Iivonen
*/
#include <config.h>
#include "gnumeric.h"
#include "parse-util.h"
#include "solver.h"
#include "func.h"
#include "cell.h"
#include "sheet.h"
#include "sheet-style.h"
#include "eval.h"
#include "dialogs.h"
#include "mstyle.h"
#include "mathfunc.h"
#include "analysis-tools.h"

#include <math.h>
#include <stdlib.h>

static void
set_bold (Sheet *sheet, int col1, int row1, int col2, int row2)
{
	MStyle *mstyle = mstyle_new ();
	Range  range;

	range.start.col = col1;
	range.start.row = row1;
	range.end.col   = col2;
	range.end.row   = row2;

	mstyle_set_font_bold (mstyle, TRUE);
	sheet_style_apply_range (sheet, &range, mstyle);
}


#if 0

/************************************************************************
 *
 * S I M P L E X   Algorithm
 *
 *
 */


/* STEP 1: Construct an initial solution table.
 */
static gnum_float *
simplex_step_one(Sheet *sheet, int target_col, int target_row,
		 CellList *inputs, GSList *constraints, int *table_cols,
		 int *table_rows, gboolean max_flag)
{
        SolverConstraint *c;
        CellList *input_list = inputs;
	GSList   *current;
        Cell     *cell, *target, *lhs, *rhs;
	gnum_float  init_value, value, *table;
        int      n, i, n_neq_c, n_eq_c, size, n_vars;

	for (n = 0; inputs != NULL; inputs = inputs->next)
		n++;

	n_neq_c = n_eq_c = 0;
	for (current = constraints; current != NULL; current = current->next) {
	        c = (SolverConstraint *)current->data;
		if (strcmp (c->type, "=") == 0)
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

	table = g_new(gnum_float, size);
	for (i = 0; i < size; i++)
	        table[i] = 0;

	inputs = input_list;
	target = sheet_cell_fetch (sheet, target_col, target_row);
	for (i = 2; inputs != NULL; inputs = inputs->next) {
	        cell = (Cell *) inputs->data;

		sheet_cell_set_value (cell, value_new_float (0.0), NULL);
		cell_eval (target);
		init_value = value_get_as_float (target->value);

		current = constraints;
		n = 1;
		while (current != NULL) {
		        c = (SolverConstraint *)current->data;
			lhs = sheet_cell_fetch (sheet, c->lhs.col, c->lhs.row);
			cell_eval (lhs);
			table[i + n**table_cols] =
			        -value_get_as_float (lhs->value);
			current = current->next;
			++n;
		}

		sheet_cell_set_value (cell, value_new_float (1.0), NULL);
		cell_eval (target);
		value = value_get_as_float (target->value);
		current = constraints;
		n = 1;
		while (current != NULL) {
		        c = (SolverConstraint *)current->data;
			lhs = sheet_cell_fetch (sheet, c->lhs.col, c->lhs.row);
			cell_eval (lhs);
			table[i + n * *table_cols] +=
			        value_get_as_float (lhs->value);
			current = current->next;
			++n;
		}

		sheet_cell_set_value (cell, value_new_float (0.0), NULL);

		if (max_flag)
		        table[i] = value - init_value;
		else
		        table[i] = init_value - value;
		++i;
	}

	n = 1;
	i = 1;
	while (constraints != NULL) {
	        c = (SolverConstraint *)constraints->data;
	        table[i * *table_cols] = i - 1 + n_vars;
		rhs = sheet_cell_fetch (sheet, c->rhs.col, c->rhs.row);
		table[1 + i * *table_cols] = value_get_as_float (rhs->value);
		if (strcmp (c->type, "<=") == 0) {
		        table[1 + n_vars + n + i * *table_cols] = 1;
			++n;
		} else if (strcmp (c->type, ">=") == 0) {
		        table[1 + n_vars + n + i * *table_cols] = -1;
			++n;
		}
	        printf ("%-30s (col=%d, row=%d  %s  col=%d, row=%d\n",
		       c->str, c->lhs.col, c->lhs.row,
		       c->type, c->rhs.col, c->rhs.row);
		constraints = constraints->next;
		i++;
	}

	return table;
}

/* STEP 2a: Convert negative RHS's to positive.
 */
static void
simplex_step_bvs(gnum_float *table, int tbl_cols, int tbl_rows)
{
        int i, n;

	for (i = 1; i < tbl_rows; i++)
	        if (table[i * tbl_cols + 1] < 0)
		        for (n = 1; n < tbl_cols; n++)
			        table[i * tbl_cols + n] *= -1;
}

/* STEP 2: Find the pivot column.  Most positive rule is applied.
 */
static int
simplex_step_two(gnum_float *table, int tbl_cols, int *status)
{
        gnum_float max = 0;
	int     i, ind = -1;

	table += 2;
	for (i = 0; i < tbl_cols-2; i++)
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
simplex_step_three(gnum_float *table, int col, int tbl_cols, int tbl_rows,
		   int *status)
{
        gnum_float a, min = 0, test;
	int     i, min_i = -1;

	table += tbl_cols;
	for (i = 0; i < tbl_rows - 1; i++) {
	        a = table[2 + col + i * tbl_cols];
		if (a > 0) {
		        test = table[1 + i * tbl_cols] / a;
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
simplex_step_four(gnum_float *table, int col, int row, int tbl_cols, int tbl_rows)
{
        gnum_float pivot, *pivot_row, c;
	int     i, j;

	pivot = table[2 + col + (row + 1) * tbl_cols];
	pivot_row = &table[1 + (row + 1) * tbl_cols];
	table[(row + 1) * tbl_cols] = col;

	for (i = 0; i < tbl_cols - 1; i++)
	        pivot_row[i] /= pivot;

	for (i = 0; i < tbl_rows; i++)
	        if (i != row + 1) {
		        c = table[2 + col + i * tbl_cols];
			for (j = 0; j < tbl_cols - 1; j++)
			        table[j + 1 + i * tbl_cols] -= pivot_row[j] * c;
		}
}

static void
display_table(gnum_float *table, int cols, int rows)
{
        int i, n;

	for (i = 0; i < rows; i++) {
	        for (n = 0; n < cols; n++)
		        printf ("%5.1f ", table[n + i * cols]);
		printf ("\n");
	}
	printf ("\n");
}

static gnum_float *
simplex_copy_table (gnum_float *table, int cols, int rows)
{
        gnum_float *tbl;
        int     i;

	tbl = g_new (gnum_float, cols*rows);
	for (i = 0; i < cols * rows; i++)
	        tbl[i] = table[i];

	return tbl;
}

int
solver_simplex (WorkbookControl *wbc, Sheet *sheet, gnum_float **init_tbl,
		gnum_float **final_tbl)
{
        int i, n;
	SolverParameters *param = &sheet->solver_parameters;
        CellList *cell_list = param->input_cells;
	GSList   *constraints = param->constraints;
	Cell     *cell;

	gnum_float  *table;
	int      col, row, status, tbl_rows, tbl_cols;
	gboolean max_flag;

	max_flag = param->problem_type == SolverMaximize;
	status = SIMPLEX_OK;

	table = simplex_step_one(sheet,
				 param->target_cell->pos.col,
				 param->target_cell->pos.row,
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

	for (i = 1; i < tbl_rows; i++) {
	        Cell *c;
	        cell_list = param->input_cells;
		c = (Cell *) cell_list->data;
	        for (n = 0; n < (int) table[i * tbl_cols]; n++) {
			cell_list = cell_list->next;
			if (cell_list == NULL)
			        goto skip;
		        c = (Cell *) cell_list->data;
		}
		cell = sheet_cell_fetch (sheet, c->pos.col, c->pos.row);
		sheet_cell_set_value (cell, value_new_float (table[1 + i * tbl_cols]), NULL);
	skip:
	}
	cell = sheet_cell_fetch (sheet, param->target_cell->pos.col,
				param->target_cell->pos.row);
	cell_eval (cell);

	/* FIXME: Do not do the following loop.  Instead recalculate
	 * everything that depends on the input variables (the list of
	 * cells in params->input_cells).
	 */

	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *)constraints->data;

		cell = sheet_cell_fetch (sheet, c->lhs.col, c->lhs.row);
		cell_eval (cell);
		constraints = constraints->next;
	}

	*final_tbl = table;

        return SIMPLEX_DONE;
}

#endif

static gnum_float
get_lp_coeff (Cell *target, Cell *change)
{
        gnum_float x0, x1;

	sheet_cell_set_value (change, value_new_float (0.0), NULL);
	cell_eval (target);
	x0 = value_get_as_float (target->value);

	sheet_cell_set_value (change, value_new_float (1.0), NULL);
	cell_eval (target);
	x1 = value_get_as_float (target->value);

	return x1 - x0;
}

/************************************************************************
 */

static void
callback (int iter, gnum_float *x, gnum_float bv, gnum_float cx, int n, void *data)
{
        int     i;

	printf ("Iteration=%3d ", iter + 1);
	printf ("bv=%9.4f cx=%9.4f gap=%9.4f\n", bv, cx, fabs (bv-cx));
	for (i = 0; i < n; i++)
	        printf ("%8.4f ", x[i]);
        printf ("\n");
}

static void
count_dimensions (gboolean assume_non_negative,
		  GSList *constraints, CellList *inputs,
		  int *n_vars, int *n_constrs)
{
        Cell *cell;
	int  n_constraints = 0;
	int  n_variables = 0;
	int  n_inputs = 0;

	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *)constraints->data;

		if (strcmp (c->type, "<=") == 0
		    || strcmp (c->type, ">=") == 0) {
		        n_variables += MAX (c->cols, c->rows);
			n_constraints += MAX (c->cols, c->rows);
		} else if (strcmp (c->type, "=") == 0)
			n_constraints += MAX (c->cols, c->rows);

		constraints = constraints->next;
	}

	while (inputs != NULL) {
	        cell = (Cell *)inputs->data;

		n_inputs++;
		inputs = inputs->next;
	}

	*n_constrs = n_constraints;
	if (assume_non_negative)
	        *n_vars = n_variables + n_inputs;
	else
	        *n_vars = n_variables + 2 *n_inputs;
}

static int
make_solver_arrays (Sheet *sheet, SolverParameters *param, int n_variables,
		    int n_constraints, gnum_float **A_, gnum_float **b_,
		    gnum_float **c_)
{
	GSList     *constraints;
	CellList   *inputs;
	Cell       *target, *cell;
	gnum_float    *A;
	gnum_float    *b;
	gnum_float    *c;
	int        i, j, n, var;

	if (n_variables < 1)
		return SOLVER_LP_INVALID_LHS;
	if (n_constraints < 1)
		return SOLVER_LP_INVALID_RHS;

	A = g_new (gnum_float, n_variables * n_constraints);
	b = g_new (gnum_float, n_constraints);
	c = g_new (gnum_float, n_variables);

	for (i = 0; i < n_variables; i++)
	        for (j = 0; j < n_constraints; j++)
		        A[j + i * n_constraints] = 0;
	for (i = 0; i < n_variables; i++)
	        c[i] = 0;

	inputs = param->input_cells;
	var = 0;
	target = sheet_cell_get (sheet, param->target_cell->pos.col,
				 param->target_cell->pos.row);
	if (target == NULL)
	        return SOLVER_LP_INVALID_RHS; /* FIXME */

	while (inputs != NULL) {
	        cell = (Cell *)inputs->data;

		c[var] = get_lp_coeff (target, cell);
		if (param->options.assume_non_negative)
		        var++;
		else {
		        c[var + 1] = -c[var];
		        var += 2;
		}
		inputs = inputs->next;
	}

	constraints = param->constraints;
	i = 0;
	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *)constraints->data;

		if (strcmp (c->type, "Int") == 0 ||
		    strcmp (c->type, "Bool") == 0)
		        goto skip;

		/* Set the constraint coefficients */
		for (n = 0; n < MAX (c->cols, c->rows); n++) {
		        if (c->cols > 1)
			        target = sheet_cell_get (sheet, c->lhs.col + n,
							 c->lhs.row);
			else
			        target = sheet_cell_get (sheet, c->lhs.col,
							 c->lhs.row + n);
			if (target == NULL)
			        return SOLVER_LP_INVALID_LHS;

			inputs = param->input_cells;
			j = 0;
			while (inputs != NULL) {
			        cell = (Cell *)inputs->data;

				A[i + n + j*n_constraints] =
				  get_lp_coeff (target, cell);
				if (param->options.assume_non_negative)
				        j++;
				else {
				        A[i + n + (j+1)*n_constraints] =
					  -A[i + n + j*n_constraints];
					j += 2;

				}
				inputs = inputs->next;
			}

			/* Set the slack/surplus variables */
			if (strcmp (c->type, "<=") == 0) {
			        A[i + n + var*n_constraints] = 1;
				var++;
			} else if (strcmp (c->type, ">=") == 0) {
			        A[i + n + var*n_constraints] = -1;
				var++;
			}

			/* Fetch RHS for b */
			if (c->cols > 1)
			        cell = sheet_cell_get (sheet, c->rhs.col + n,
						       c->rhs.row);
			else
			        cell = sheet_cell_get (sheet, c->rhs.col,
						       c->rhs.row + n);

			if (cell == NULL)
			        return SOLVER_LP_INVALID_RHS;

			b[i + n] = value_get_as_float (cell->value);
		}
		i += n;
	skip:
		constraints = constraints->next;
	}
	*A_ = A;
	*b_ = b;
	*c_ = c;

	return 0;
}

int
solver_affine_scaling (WorkbookControl *wbc, Sheet *sheet,
		       gnum_float **x,    /* the optimal solution */
		       gnum_float **sh_pr /* the shadow prizes */)
{
	SolverParameters *param = &sheet->solver_parameters;
	GSList           *constraints;
	CellList         *inputs;
	Cell             *cell;
	gnum_float          *A;
	gnum_float          *b;
	gnum_float          *c;
	gboolean         max_flag;
	gboolean         found;

	int n_constraints;
	int n_variables;
	int i;

	max_flag = param->problem_type == SolverMaximize;
	constraints = param->constraints;
	inputs = param->input_cells;

	count_dimensions (param->options.assume_non_negative, constraints,
			  inputs, &n_variables, &n_constraints);

	*x = g_new (gnum_float, n_variables);
	*sh_pr = g_new (gnum_float, n_variables);

	i = make_solver_arrays (sheet, param, n_variables, n_constraints,
				&A, &b, &c);
	if (i)
	        return i;

	found = affine_init (A, b, c, n_constraints, n_variables, *x);
	if (! found)
	        return SOLVER_LP_INFEASIBLE;

	affine_scale (A, b, c, *x,
		      n_constraints, n_variables, max_flag,
		      0.00000001, 1000,
		      callback, NULL);
	if (! found)
	        return SOLVER_LP_UNBOUNDED;

	inputs = param->input_cells;
	i = 0;
	if (param->options.assume_non_negative)
	        while (inputs != NULL) {
			Value * v = value_new_float ((*x)[i++]);

			cell = (Cell *)inputs->data;
			sheet_cell_set_value (cell, v, NULL);
			inputs = inputs->next;
		}
	else
	        while (inputs != NULL) {
			Value * v = value_new_float ((*x)[i] - (*x)[i + 1]);

			cell = (Cell *)inputs->data;
			sheet_cell_set_value (cell, v, NULL);
			i += 2;
			inputs = inputs->next;
		}

	/* FIXME: Do not do the following loop.  Instead recalculate
	 * everything that depends on the input variables (the list of
	 * cells in params->input_cells).
	 */

	constraints = param->constraints;
	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *)constraints->data;

		cell = sheet_cell_fetch (sheet, c->lhs.col, c->lhs.row);
		cell_eval (cell);
		constraints = constraints->next;
	}

	cell = sheet_cell_get (sheet, param->target_cell->pos.col,
			       param->target_cell->pos.row);
	cell_eval (cell);

	g_free (A);
	g_free (b);
	g_free (c);

	return SOLVER_LP_OPTIMAL;
}

char *
write_constraint_str (int lhs_col, int lhs_row, int rhs_col,
		      int rhs_row, const char *type_str, int cols, int rows)
{
	GString *buf = g_string_new ("");
	char *result;

	if (cols == 1 && rows == 1)
		g_string_sprintfa (buf, "%s %s ", cell_coord_name (lhs_col, lhs_row), type_str);
	else {
	        g_string_append (buf, cell_coord_name (lhs_col, lhs_row));
		g_string_append_c (buf, ':');
		g_string_append (buf, cell_coord_name (lhs_col + cols-1, lhs_row + rows - 1));
		g_string_append_c (buf, ' ');
		g_string_append (buf, type_str);
		g_string_append_c (buf, ' ');
	}

	if (strcmp (type_str, "Int") != 0 && strcmp (type_str, "Bool") != 0) {
	        if (cols == 1 && rows == 1)
		        g_string_append (buf, cell_coord_name (rhs_col, rhs_row));
		else {
		        g_string_append (buf, cell_coord_name (rhs_col, rhs_row));
			g_string_append_c (buf, ':');
		        g_string_append (buf, cell_coord_name (rhs_col + cols - 1, rhs_row + rows - 1));
		}
	}

	result = buf->str;
	g_string_free (buf, FALSE);
	return result;
}

static void
make_int_array (SolverParameters *param, CellList *inputs, gboolean int_r[],
		int n_variables)
{
	GSList   *constraints;
	CellList *list;
	int      i, n;

	for (i = 0; i < n_variables; i++)
	        int_r[i] = FALSE;

        constraints = param->constraints;
	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *)constraints->data;

		if (strcmp (c->type, "Int") == 0) {
		        for (n = 0; n < MAX (c->cols, c->rows); n++) {
			        i = 0;
				for (list = inputs; list != NULL;
				     list = list->next) {
				        Cell *cell = list->data;
					if (c->cols > 1) {
					        if (cell->pos.col ==
						    c->lhs.col + n &&
						    cell->pos.row ==
						    c->lhs.row) {
						        int_r[i] = TRUE;
							break;
						}
					} else {
					        if (cell->pos.col ==
						    c->lhs.col &&
						    cell->pos.row ==
						    c->lhs.row + n) {
						        int_r[i] = TRUE;
							break;
						}
					}
					i++;
				}
			}
		}
		constraints = constraints->next;
	}
}

static gboolean
solver_branch_and_bound (WorkbookControl *wbc, Sheet *sheet, gnum_float **opt_x)
{
	SolverParameters *param = &sheet->solver_parameters;
	GSList           *constraints;
	CellList         *inputs;
	Cell             *cell;
	gnum_float          *A;
	gnum_float          *b;
	gnum_float          *c;
	gboolean         max_flag;
	gboolean         found;
	gnum_float          best;

	int      n_constraints;
	int      n_variables;
	gboolean *int_r;
	int      i;

	max_flag = param->problem_type == SolverMaximize;
	constraints = param->constraints;
	inputs = param->input_cells;

	count_dimensions (param->options.assume_non_negative, constraints,
			  inputs, &n_variables, &n_constraints);

	*opt_x = g_new (gnum_float, n_variables);
	int_r = g_new (gboolean, n_variables);

	i = make_solver_arrays (sheet, param, n_variables, n_constraints,
				&A, &b, &c);
	if (i)
	        return i;

	make_int_array (param, inputs, int_r, n_variables);

	if (max_flag)
	        best = -1e10;
	else
	        best = 1e10;

	found = branch_and_bound (A, b, c, *opt_x, n_constraints, n_variables,
				  n_variables, max_flag, 0.000001, 1000,
				  int_r, callback, NULL, &best);

	if (! found)
	        return SOLVER_LP_INFEASIBLE;

	inputs = param->input_cells;
	i = 0;
	if (param->options.assume_non_negative)
	        while (inputs != NULL) {
			Value *v = value_new_float ((*opt_x)[i++]);

			cell = (Cell *)inputs->data;
			sheet_cell_set_value (cell, v, NULL);
			inputs = inputs->next;
		}
	else
	        while (inputs != NULL) {
			Value *v = value_new_float ((*opt_x)[i] - (*opt_x)[i + 1]);
			cell = (Cell *)inputs->data;
			sheet_cell_set_value (cell, v, NULL);
			i += 2;
			inputs = inputs->next;
		}

	/* FIXME: Do not do the following loop.  Instead recalculate
	 * everything that depends on the input variables (the list of
	 * cells in params->input_cells).
	 */

	constraints = param->constraints;
	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *)constraints->data;

		if (strcmp (c->type, "Int") == 0 ||
		    strcmp (c->type, "Bool") == 0)
		        goto skip;

		for (i = 0; i < MAX (c->cols, c->rows); i++) {
		        if (c->cols > 1)
			        cell = sheet_cell_fetch (sheet, c->lhs.col + i,
							 c->lhs.row);
			else
			        cell = sheet_cell_fetch (sheet, c->lhs.col,
							 c->lhs.row + i);
			cell_eval (cell);
		}
	skip:
		constraints = constraints->next;
	}

	cell = sheet_cell_get (sheet, param->target_cell->pos.col,
			       param->target_cell->pos.row);
	cell_eval (cell);

	g_free (A);
	g_free (b);
	g_free (c);

	return SOLVER_LP_OPTIMAL;
}


gboolean
solver_lp (WorkbookControl *wbc, Sheet *sheet,
	   gnum_float **opt_x, gnum_float **sh_pr, gboolean *ilp)
{
	SolverParameters *param = &sheet->solver_parameters;
	GSList           *constraints;

	*ilp = FALSE;
        constraints = param->constraints;
	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *)constraints->data;

		if (strcmp (c->type, "Int") == 0) {
		        *ilp = TRUE;
			break;
		}
		constraints = constraints->next;
	}
	if (*ilp)
	        return solver_branch_and_bound (wbc, sheet, opt_x);
	else
	        return solver_affine_scaling (wbc, sheet, opt_x, sh_pr);
}

static char *
find_name (Sheet *sheet, int col, int row)
{
        static char *str = NULL;
	const char  *col_str = "";
	const char  *row_str = "";
        int         col_n, row_n;

	for (col_n = col - 1; col_n >= 0; col_n--) {
	        Cell *cell = sheet_cell_get (sheet, col_n, row);
		if (cell && !VALUE_IS_NUMBER (cell->value)) {
			col_str = value_peek_string (cell->value);
		        break;
		}
	}

	for (row_n = row - 1; row_n >= 0; row_n--) {
	        Cell *cell = sheet_cell_get (sheet, col, row_n);
		if (cell && !VALUE_IS_NUMBER (cell->value)) {
			row_str = value_peek_string (cell->value);
		        break;
		}
	}

	if (str)
	        g_free (str);
	str = g_new (char, strlen (col_str) + strlen (row_str) + 2);

	if (*col_str)
	        sprintf (str, "%s %s", col_str, row_str);
	else
	        sprintf (str, "%s", row_str);

	return str;
}

static void
solver_answer_report (WorkbookControl *wbc, Sheet *sheet, GSList *ov,
		      gnum_float ov_target)
{
        data_analysis_output_t dao;
	SolverParameters       *param = &sheet->solver_parameters;
	GSList                 *constraints;
        CellList               *cell_list = param->input_cells;

	Cell *cell;
	int  row, i;

	dao.type = NewSheetOutput;
        prepare_output (wbc, &dao, _("Answer Report"));
	set_cell (&dao, 0, 0, _("Gnumeric Solver Answer Report"));
	set_bold (dao.sheet, 0, 0, 0, 0);

	if (param->problem_type == SolverMaximize)
	        set_cell (&dao, 0, 1, _("Target Cell (Maximize)"));
	else
	        set_cell (&dao, 0, 1, _("Target Cell (Minimize)"));
	set_cell (&dao, 0, 2, _("Cell"));
	set_cell (&dao, 1, 2, _("Name"));
	set_cell (&dao, 2, 2, _("Original Value"));
	set_cell (&dao, 3, 2, _("Final Value"));

	set_bold (dao.sheet, 0, 2, 3, 2);

	/* Set `Cell' field */
	set_cell (&dao, 0, 3, cell_name (param->target_cell));

	/* Set `Name' field */
	set_cell (&dao, 1, 3, find_name (sheet, param->target_cell->pos.col,
					 param->target_cell->pos.row));

	/* Set `Original Value' field */
	set_cell_float (&dao, 2, 3, ov_target);

	/* Set `Final Value' field */
	cell = sheet_cell_fetch (sheet, param->target_cell->pos.col,
				 param->target_cell->pos.row);
	set_cell_value (&dao, 3, 3, value_duplicate (cell->value));

	row = 4;
	set_cell (&dao, 0, row++, _("Adjustable Cells"));
	set_cell (&dao, 0, row, _("Cell"));
	set_cell (&dao, 1, row, _("Name"));
	set_cell (&dao, 2, row, _("Original Value"));
	set_cell (&dao, 3, row, _("Final Value"));
	set_bold (dao.sheet, 0, row, 3, row);
	row++;

	while (cell_list != NULL) {
	        char *str = (char *)ov->data;
	        cell = (Cell *)cell_list->data;

		/* Set `Cell' column */
		set_cell (&dao, 0, row, cell_name (cell));

		/* Set `Name' column */
		set_cell (&dao, 1, row, find_name (sheet, cell->pos.col,
						   cell->pos.row));

		/* Set `Original Value' column */
		set_cell (&dao, 2, row, str);

		/* Set `Final Value' column */
		cell = sheet_cell_fetch (sheet, cell->pos.col,
					 cell->pos.row);
		set_cell_value (&dao, 3, row, value_duplicate (cell->value));

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
	set_bold (dao.sheet, 0, row, 5, row);

	row++;
	constraints = param->constraints;
	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *)constraints->data;
		gnum_float lhs, rhs;
		int     tc, tr, sc, sr;

		if (strcmp (c->type, "Int") == 0 ||
		    strcmp (c->type, "Bool") == 0)
		        goto skip;

		for (i = 0; i < MAX (c->cols, c->rows); i++) {
		        if (c->cols > 1) {
			        tc = c->lhs.col + i;
				tr = c->lhs.row;
			        sc = c->rhs.col + i;
				sr = c->rhs.row;
			} else {
			        tc = c->lhs.col;
				tr = c->lhs.row + i;
			        sc = c->rhs.col;
				sr = c->rhs.row + i;
			}

		        /* Set `Cell' column */
			set_cell (&dao, 0, row, cell_coord_name (tc, tr));

			/* Set `Name' column */
			set_cell (&dao, 1, row, find_name (sheet, tc, tr));

			/* Set `Cell Value' column */
			cell = sheet_cell_fetch (sheet, sc, sr);
			rhs = value_get_as_float (cell->value);
			set_cell_value (&dao, 2, row, value_duplicate (cell->value));

			/* Set `Formula' column */
			set_cell (&dao, 3, row, c->str);

			/* Set `Status' column */
			cell = sheet_cell_fetch (sheet, tc, tr);
			lhs = value_get_as_float (cell->value);

			if (fabs (lhs - rhs) < 0.001)
			        set_cell (&dao, 4, row, _("Binding"));
			else
			        set_cell (&dao, 4, row, _("Not Binding"));

			/* Set `Slack' column */
			set_cell_float (&dao, 5, row, fabs (lhs - rhs));

			/* Go to next row */
			++row;
		}
	skip:
		constraints = constraints->next;
	}

	autofit_column (&dao, 0);
	autofit_column (&dao, 1);
	autofit_column (&dao, 2);
	autofit_column (&dao, 3);
	autofit_column (&dao, 4);
	autofit_column (&dao, 5);
}

static void
solver_sensitivity_report (WorkbookControl *wbc, Sheet *sheet, gnum_float *x,
			   gnum_float *shadow_prize)
{
        data_analysis_output_t dao;
	SolverParameters       *param = &sheet->solver_parameters;
	GSList                 *constraints;
        CellList               *cell_list = param->input_cells;

	Cell *cell;
	int  row = 0, i;

	dao.type = NewSheetOutput;
        prepare_output (wbc, &dao, _("Sensitivity Report"));
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
	        cell = (Cell *)cell_list->data;

		/* Set `Cell' column */
		set_cell (&dao, 0, row, cell_name (cell));

		/* Set `Name' column */
		set_cell (&dao, 1, row, find_name (sheet, cell->pos.col,
						   cell->pos.row));

		/* Set `Final Value' column */
		cell = sheet_cell_fetch (sheet, cell->pos.col, cell->pos.row);
		set_cell_value (&dao, 2, row, value_duplicate (cell->value));

		/* Set `Reduced Cost' column */
		set_cell_float (&dao, 3, row, x[i]);

		/* Set `Objective Coefficient' column */
		set_cell_float (&dao, 4, row, x[i]);

		/* Go to next row */
	        cell_list = cell_list->next;
		row++;
		i++;
	}

	set_bold (dao.sheet, 0, 0, 0, 0);
	set_bold (dao.sheet, 2, 2, 6, 2);
	set_bold (dao.sheet, 0, 3, 6, 3);

	set_cell (&dao, 0, row++, _("Constraints"));
	set_bold (dao.sheet, 2, row, 6, row);
	set_cell (&dao, 2, row, _("Final"));
	set_cell (&dao, 3, row, _("Shadow"));
	set_cell (&dao, 4, row, _("Constraint"));
	set_cell (&dao, 5, row, _("Allowable"));
	set_cell (&dao, 6, row++, _("Allowable"));
	set_bold (dao.sheet, 0, row, 6, row);
	set_cell (&dao, 0, row, _("Cell"));
	set_cell (&dao, 1, row, _("Name"));
	set_cell (&dao, 2, row, _("Value"));
	set_cell (&dao, 3, row, _("Price"));
	set_cell (&dao, 4, row, _("R.H. Side"));
	set_cell (&dao, 5, row, _("Increase"));
	set_cell (&dao, 6, row++, _("Decrease"));

	constraints = param->constraints;
	while (constraints != NULL) {
	        SolverConstraint *c = (SolverConstraint *)constraints->data;

		/* Set `Cell' column */
		set_cell (&dao, 0, row, cell_pos_name (&c->lhs));

		/* Set `Name' column */
		set_cell (&dao, 1, row, find_name (sheet, c->lhs.col,
						   c->lhs.row));

		/* Set `Final Value' column */
		cell = sheet_cell_fetch (sheet, c->lhs.col, c->lhs.row);
		set_cell_value (&dao, 2, row, value_duplicate (cell->value));

#if 0
		/* Set `Shadow Prize' column */
		set_cell_float (&dao, 3, row, shadow_prize[i]);
#endif

		/* Set `R.H. Side Value' column */
		cell = sheet_cell_fetch (sheet, c->rhs.col, c->rhs.row);
		set_cell_value (&dao, 4, row, value_duplicate (cell->value));

		/* Go to next row */
		++row;
		++i;
		constraints = constraints->next;
	}

	autofit_column (&dao, 0);
	autofit_column (&dao, 1);
	autofit_column (&dao, 2);
	autofit_column (&dao, 3);
	autofit_column (&dao, 4);
	autofit_column (&dao, 5);
	autofit_column (&dao, 6);
}

static void
solver_limits_report (WorkbookControl *wbc)
{
}

void
solver_lp_reports (WorkbookControl *wbc, Sheet *sheet, GSList *ov, gnum_float ov_target,
		   gnum_float *opt_x, gnum_float *shadow_prize,
		   gboolean answer, gboolean sensitivity, gboolean limits)
{
        if (answer)
	        solver_answer_report (wbc, sheet, ov, ov_target);
	if (sensitivity)
	        solver_sensitivity_report (wbc, sheet, opt_x, shadow_prize);
	if (limits)
	        solver_limits_report (wbc);
}


void
solver_lp_copy (SolverParameters const *src_param, Sheet *new_sheet)
{
	SolverParameters *dst_param;
	GSList   *constraints;
	CellList *inputs;

	dst_param = &new_sheet->solver_parameters;

	if (src_param->target_cell != NULL)
	        dst_param->target_cell =
		    sheet_cell_fetch (new_sheet,
				      src_param->target_cell->pos.row,
				      src_param->target_cell->pos.col);

	dst_param->problem_type = src_param->problem_type;
	g_free (dst_param->input_entry_str);
	dst_param->input_entry_str = g_strdup (src_param->input_entry_str);

	/* Copy the options */
	dst_param->options.max_time_sec           = src_param->options.max_time_sec;
	dst_param->options.iterations             = src_param->options.iterations;
	dst_param->options.precision              = src_param->options.precision;
	dst_param->options.tolerance              = src_param->options.tolerance;
	dst_param->options.convergence            = src_param->options.convergence;
	dst_param->options.equal_to_value         = src_param->options.equal_to_value;
	dst_param->options.assume_linear_model    = src_param->options.assume_linear_model;
	dst_param->options.assume_non_negative    = src_param->options.assume_non_negative;
	dst_param->options.automatic_scaling      = src_param->options.automatic_scaling;
	dst_param->options.show_iteration_results = src_param->options.show_iteration_results;

	/* Copy the constraints */
	for (constraints = src_param->constraints; constraints; constraints = constraints->next) {
		SolverConstraint *old = (SolverConstraint *) constraints->data;
		SolverConstraint *new;

		new = g_new (SolverConstraint, 1);
		new->lhs.col = old->lhs.col;
		new->lhs.row = old->lhs.row;
		new->rhs.col = old->rhs.col;
		new->rhs.row = old->rhs.row;
		new->cols    = old->cols;
		new->rows    = old->rows;
		new->type    = g_strdup (old->type);
		new->str     = g_strdup (old->str);
		dst_param->constraints = g_slist_append (dst_param->constraints, new);
	}

	/* Copy the input cell list */
	for (inputs = src_param->input_cells; inputs ; inputs = inputs->next) {
		Cell *cell = (Cell *) inputs->data;
		Cell *new_cell;
		new_cell = cell_copy (cell);
		new_cell->base.sheet = new_sheet;
		dst_param->input_cells = (CellList *)
			g_slist_append ((GSList *)dst_param->input_cells,
					(gpointer) new_cell);
	}
}
