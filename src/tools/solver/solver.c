/*
 * solver.c:  The Solver's core system.
 *
 * Author:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 1999, 2000, 2002 by Jukka-Pekka Iivonen
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
#include "reports.h"

#include "parse-util.h"
#include "solver.h"
#include "func.h"
#include "cell.h"
#include "sheet.h"
#include "sheet-style.h"
#include "dependent.h"
#include "dialogs.h"
#include "mstyle.h"
#include "value.h"
#include "ranges.h"
#include "mathfunc.h"
#include "analysis-tools.h"
#include "api.h"

#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/times.h>
#include <libgnome/gnome-i18n.h>

/* ------------------------------------------------------------------------- */


SolverParameters *
solver_param_new (void)
{
	SolverParameters *res = g_new0 (SolverParameters, 1);

	res->options.model_type          = SolverLPModel;
	res->options.assume_non_negative = TRUE;
#if __HAVE_GLPK__
	res->options.algorithm           = GLPKSimplex;
#else
	res->options.algorithm           = LPSolve;
#endif
	res->input_entry_str             = g_strdup ("");
	res->problem_type                = SolverMaximize;
	res->constraints                 = NULL;
	res->target_cell                 = NULL;
	res->input_cells		 = NULL;
	res->constraints		 = NULL;

	return res;
}

void
solver_param_destroy (SolverParameters *sp)
{
	g_free (sp->input_entry_str);
	g_free (sp);
}

static SolverResults *
solver_results_init (const SolverParameters *sp)
{
        SolverResults *res     = g_new (SolverResults, 1);

	res->optimal_values    = g_new (gnum_float,  sp->n_variables);
	res->original_values   = g_new (gnum_float,  sp->n_variables);
	res->variable_names    = g_new0 (gchar *,    sp->n_variables);
	res->constraint_names  = g_new0 (gchar *,    sp->n_total_constraints);
	res->shadow_prizes     = g_new0 (gnum_float, sp->n_total_constraints);
	res->slack             = g_new0 (gnum_float, sp->n_total_constraints);
	res->lhs               = g_new0 (gnum_float, sp->n_total_constraints);
	res->rhs               = g_new0 (gnum_float, sp->n_total_constraints);
	res->n_variables       = sp->n_variables;
	res->n_constraints     = sp->n_constraints;
	res->n_nonzeros_in_obj = 0;
	res->n_nonzeros_in_mat = 0;
	res->time_user         = 0;
	res->time_system       = 0;
	res->time_real         = 0;
	res->ilp_flag          = FALSE;
	res->target_name       = NULL;
	res->input_cells_array = NULL;
	res->constraints_array = NULL;
	res->obj_coeff         = NULL;
	res->constr_coeff      = NULL;
	res->limits            = NULL;
	res->constr_allowable_increase  =
	        g_new0 (gnum_float, sp->n_total_constraints);
	res->constr_allowable_decrease =
	        g_new0 (gnum_float, sp->n_total_constraints);

	return res;
}

void
solver_results_free (SolverResults *res)
{
        int i;

	for (i = 0; i < res->n_variables; i++)
	        g_free (res->variable_names[i]);
	for (i = 0; i < res->n_constraints; i++)
	        g_free (res->constraint_names[i]);

        g_free (res->optimal_values);
	g_free (res->original_values);
	g_free (res->target_name);
	g_free (res->variable_names);
	g_free (res->constraint_names);
	g_free (res->shadow_prizes);
	g_free (res->input_cells_array);
	g_free (res->constraints_array);
	g_free (res->obj_coeff);
	if (res->constr_coeff != NULL)
	        for (i = 0; i < res->n_constraints; i++)
		        g_free (res->constr_coeff[i]);
	g_free (res->constr_coeff);
	g_free (res->limits);
	g_free (res->constr_allowable_increase);
	g_free (res->constr_allowable_decrease);
	g_free (res->slack);
	g_free (res->lhs);
	g_free (res->rhs);
	g_free (res);
}

/* ------------------------------------------------------------------------- */

Cell *
solver_get_target_cell (Sheet *sheet)
{
        return sheet_cell_get (sheet,
			       sheet->solver_parameters->target_cell->pos.col,
			       sheet->solver_parameters->target_cell->pos.row);
}

Cell *
solver_get_input_var (SolverResults *res, int n)
{
        return res->input_cells_array[n];
}

SolverConstraint*
solver_get_constraint (SolverResults *res, int n)
{
        return res->constraints_array[n];
}

/* ------------------------------------------------------------------------- */

static SolverConstraint*
create_solver_constraint (int lhs_col, int lhs_row, int rhs_col, int rhs_row,
			  SolverConstraintType type)
{
        SolverConstraint *c;

	c = g_new (SolverConstraint, 1);
	c->lhs.col = lhs_col;
	c->lhs.row = lhs_row;
	c->rhs.col = rhs_col;
	c->rhs.row = rhs_row;
	c->rows    = 1;
	c->cols    = 1;
	c->type    = type;
	c->str     = write_constraint_str (lhs_col, lhs_row, rhs_col,
					   rhs_row, type, 1, 1);

	return c;
}

char *
write_constraint_str (int lhs_col, int lhs_row, int rhs_col,
		      int rhs_row, SolverConstraintType type,
		      int cols, int rows)
{
	GString    *buf = g_string_new ("");
	const char *type_str[] = { "<=", ">=", "=", "Int", "Bool" };
	char       *result;

	if (cols == 1 && rows == 1)
		g_string_sprintfa (buf, "%s %s ",
				   cell_coord_name (lhs_col, lhs_row),
				   type_str[type]);
	else {
	        g_string_append (buf, cell_coord_name (lhs_col, lhs_row));
		g_string_append_c (buf, ':');
		g_string_append (buf,
				 cell_coord_name (lhs_col + cols - 1,
						  lhs_row + rows - 1));
		g_string_append_c (buf, ' ');
		g_string_append (buf, type_str[type]);
		g_string_append_c (buf, ' ');
	}

	if (type != SolverINT && type != SolverBOOL) {
	        if (cols == 1 && rows == 1)
		        g_string_append (buf, cell_coord_name (rhs_col,
							       rhs_row));
		else {
		        g_string_append (buf, cell_coord_name (rhs_col,
							       rhs_row));
			g_string_append_c (buf, ':');
		        g_string_append (buf,
					 cell_coord_name (rhs_col + cols - 1,
							  rhs_row + rows - 1));
		}
	}

	result = buf->str;
	g_string_free (buf, FALSE);
	return result;
}

/* ------------------------------------------------------------------------- */

/*
 * This function implements a simple way to determine linear
 * coefficents.  For example, if we have the minization target cell in
 * `target' and the first input variable in `change', this function
 * returns the coefficent of the first variable of the objective
 * function.
 */
static inline gnum_float
get_lp_coeff (Cell *target, Cell *change, gnum_float *x0)
{
        gnum_float tmp = *x0;

	cell_set_value (change, value_new_float (1.0));
	cell_queue_recalc (change);

	cell_eval (target);
	*x0 = value_get_as_float (target->value);

	return *x0 - tmp;
}

/*
 * Saves the original values of the input variables into a
 * SolverResults entry.
 */
static void
save_original_values (SolverResults          *res,
		      const SolverParameters *param,
		      Sheet                  *sheet)
{
  
	GSList   *inputs = param->input_cells;
	Cell     *cell;
	int      i = 0;

	while (inputs != NULL) {
	        cell = (Cell *) inputs->data;

		if (cell == NULL || cell->value == NULL)
		        res->original_values[i] = 0;
		else
		        res->original_values[i] =
			        value_get_as_float (cell->value);
		inputs = inputs->next;
		++i;
	}

	cell = solver_get_target_cell (sheet);
	res->original_value_of_obj_fn = value_get_as_float (cell->value);
}

/*
 * Restores the original values of the input variables.
 */
static void
restore_original_values (SolverResults *res)
{
  
	GSList   *inputs = res->param->input_cells;
	Cell     *cell;
	int      i = 0;

	while (inputs != NULL) {
	        cell = (Cell *) inputs->data;

		sheet_cell_set_value (cell,
				      value_new_float(res->original_values[i]));
		inputs = inputs->next;
		i++;
	}
}

/************************************************************************
 */

#if 0
static void
callback (int iter, gnum_float *x, gnum_float bv, gnum_float cx, int n,
	  void *data)
{
        int     i;

	printf ("Iteration=%3d ", iter + 1);
	printf ("bv=%9.4" GNUM_FORMAT_f
		" cx=%9.4" GNUM_FORMAT_f
		" gap=%9.4" GNUM_FORMAT_f "\n",
		bv, cx, gnumabs (bv - cx));
	for (i = 0; i < n; i++)
	        printf ("%8.4" GNUM_FORMAT_f " ", x[i]);
        printf ("\n");
}
#endif

static int
get_col_nbr (SolverResults *res, CellPos *pos)
{
        int  i;
        Cell *cell;

	for (i = 0; i < res->param->n_variables; i++) {
	        cell = solver_get_input_var (res, i);
		if (cell->pos.row == pos->row && cell->pos.col == pos->col)
		        return i;
	}
	return -1;
}

/* ------------------------------------------------------------------------- */

static void
clear_input_vars (int n_variables, SolverResults *res)
{
        int i;

	for (i = 0; i < n_variables; i++)
		sheet_cell_set_value (solver_get_input_var (res, i),
				      value_new_float (0.0));
}

/*
 * Initializes the program according to the information given in the
 * solver dialog and the related sheet.  After the call, the LP
 * program is ready to run.
 */
static SolverProgram
lp_qp_solver_init (Sheet *sheet, const SolverParameters *param,
		   SolverResults *res, const SolverLPAlgorithm *alg,
		   gnum_float start_time, gchar **errmsg)
{
        SolverProgram     program;
	Cell              *target;
	gnum_float        x, x0, base;
	int               i, n, ind;

	/* Initialize the SolverProgram structure. */
	program = alg->init_fn (param);

	/* Set up the objective function coefficients. */
	target = solver_get_target_cell (sheet);
	clear_input_vars (param->n_variables, res);

	cell_eval (target);
	x0 = base = value_get_as_float (target->value);

	if (param->options.model_type == SolverLPModel) {
	        for (i = 0; i < param->n_variables; i++) {
		        x = get_lp_coeff (target,
					  solver_get_input_var (res, i), &x0);
			if (x != 0) {
			        alg->set_obj_fn (program, i, x);
				res->n_nonzeros_in_obj += 1;
				res->obj_coeff[i] = x;
			}
		}
		/* Check that the target cell contains a formula. */
		if (! res->n_nonzeros_in_obj) {
		        *errmsg = _("Target cell should contain a formula.");
			solver_results_free (res);
			return NULL;
		}
	} else {
	        /* FIXME: Init qp */
	}

	/* Add constraints. */
	for (i = ind = 0; i < param->n_total_constraints; i++) {
	        SolverConstraint *c = solver_get_constraint (res, i);
		target = sheet_cell_get (sheet, c->lhs.col, c->lhs.row);

		/* Check that LHS is a number type. */
		if (! (target->value == NULL || VALUE_IS_EMPTY (target->value)
		              || VALUE_IS_NUMBER (target->value))) {
		          *errmsg = _("The LHS cells should contain formulas "
				      "that yield proper numerical values.  "
				      "Specify valid LHS entries.");
			  solver_results_free (res);
			  return NULL;
		}

		if (c->type == SolverINT) {
		        n = get_col_nbr (res, &c->lhs);
			if (n == -1)
			        return NULL;
		        alg->set_int_fn (program, n);
			res->ilp_flag = TRUE;
		        continue;
		}
		if (c->type == SolverBOOL) {
		        n = get_col_nbr (res, &c->lhs);
			if (n == -1)
			        return NULL;
		        alg->set_bool_fn (program, n);
			res->ilp_flag = TRUE;
		        continue;
		}
		clear_input_vars (param->n_variables, res);
		x0 = base;
		for (n = 0; n < param->n_variables; n++) {
		        x = get_lp_coeff (target,
					  solver_get_input_var (res, n), &x0);
			if (x != 0) {
			        res->n_nonzeros_in_mat += 1;
				alg->set_constr_mat_fn (program, n, ind, x);
				res->constr_coeff[i][n] = x;
			}
		}
		target = sheet_cell_get (sheet, c->rhs.col, c->rhs.row);

		/* Check that RHS is a number type. */
		if (! (target->value == NULL || VALUE_IS_EMPTY (target->value)
		       || VALUE_IS_NUMBER (target->value))) {
		          *errmsg = _("The RHS cells should contain proper "
				      "numerical values only.  Specify valid "
				      "RHS entries.");
			  solver_results_free (res);
			  return NULL;
		}

		x = value_get_as_float (target->value);
		alg->set_constr_fn (program, ind, c->type, x);
		ind++;
	}

	/* Set up the problem type. */
	switch (param->problem_type) {
	case SolverMinimize:
	        alg->minim_fn (program);
	        break;
	case SolverMaximize:
	        alg->maxim_fn (program);
	        break;
	case SolverEqualTo:
	        return NULL; /* FIXME: Equal to feature not yet implemented. */
	default:
	        return NULL;
	}

	/* Set options. */
	if (alg->set_option_fn (program, SolverOptAutomaticScaling,
				&(param->options.automatic_scaling),
				NULL, NULL))
	        return NULL;
	if (alg->set_option_fn (program, SolverOptMaxIter, NULL, NULL,
				&(param->options.max_iter)))
	        return NULL;
	if (alg->set_option_fn (program, SolverOptMaxTimeSec, NULL, &start_time,
				&(param->options.max_time_sec)))
	        return NULL;

	/* Assume Integer (Discrete) button. */
	if (param->options.assume_discrete) {
	        for (i = 0; i < param->n_variables; i++)
		        alg->set_int_fn (program, i);
		res->ilp_flag = TRUE;
	}

#if 0
	alg->print_fn (program);
#endif

	return program;
}

/*
 * Returns TRUE if an error is found in the program definition.
 * Otherwise, return FALSE (Ok).
 */
static gboolean
check_program_definition_failures (Sheet            *sheet,
				   SolverParameters *param,
				   SolverResults    **res,
				   gchar            **errmsg)
{
	GSList           *inputs;
	GSList           *c;
	Cell             *cell;
	int              i, n;
	Cell             **input_cells_array;
	SolverConstraint **constraints_array;

	param->n_variables = 0;

	/*
	 * Checks for the Input cells.
	 */

	/* Count the nbr of the input cells and check that each cell
	 * is in the list only once. */
 	for (inputs = param->input_cells; inputs ; inputs = inputs->next) {
	        cell = (Cell *) inputs->data;

		/* Check that the cell contains a number or is empty. */
		if (! (cell->value == NULL || VALUE_IS_EMPTY (cell->value)
		       || VALUE_IS_NUMBER (cell->value))) {
		        *errmsg = _("Some of the input cells contain "
				    "non-numeric values.  Specify a valid "
				    "input range.");
			return TRUE;
		}
		
	        param->n_variables += 1;
	}
	input_cells_array = g_new (Cell *, param->n_variables);
	i = 0;
 	for (inputs = param->input_cells; inputs ; inputs = inputs->next)
	        input_cells_array[i++] = (Cell *) inputs->data;

	param->n_constraints      = 0;
	param->n_int_constraints  = 0;
	param->n_bool_constraints = 0;
	i = 0;
 	for (c = param->constraints; c ; c = c->next) {
	        SolverConstraint *sc = (SolverConstraint *) c->data;

		if (sc->type == SolverINT)
		        param->n_int_constraints +=
			        MAX (sc->rows, sc->cols);
		else if (sc->type == SolverBOOL)
		        param->n_bool_constraints +=
			        MAX (sc->rows, sc->cols);
		else
		        param->n_constraints += MAX (sc->rows, sc->cols);
	}
	param->n_total_constraints = param->n_constraints +
	        param->n_int_constraints + param->n_bool_constraints;
	constraints_array = g_new (SolverConstraint *,
				   param->n_total_constraints);
	i = 0;
 	for (c = param->constraints; c ; c = c->next) {
	        SolverConstraint *sc = (SolverConstraint *) c->data;

		if (sc->rows == 1 && sc->cols == 1)
		        constraints_array[i++] = sc;
		else {
		        if (sc->rows > 1)
			        for (n = 0; n < sc->rows; n++)
				        constraints_array[i++] =
					  create_solver_constraint
					  (sc->lhs.col, sc->lhs.row + n,
					   sc->rhs.col, sc->rhs.row + n,
					   sc->type);
			else
			        for (n = 0; n < sc->cols; n++)
				        constraints_array[i++] =
					  create_solver_constraint
					  (sc->lhs.col + n, sc->lhs.row,
					   sc->rhs.col + n, sc->rhs.row,
					   sc->type);
		}
	}

	*res = solver_results_init (param);

	(*res)->param = param;
	(*res)->input_cells_array = input_cells_array;
	(*res)->constraints_array = constraints_array;
	(*res)->obj_coeff = g_new0 (gnum_float, param->n_variables);

	(*res)->constr_coeff = g_new0 (gnum_float *, param->n_total_constraints);
	for (i = 0; i < param->n_total_constraints; i++)
	        (*res)->constr_coeff[i] = g_new0 (gnum_float,
						  param->n_variables);
	(*res)->limits = g_new (SolverLimits, param->n_variables);

	return FALSE;  /* Everything Ok. */
}

static SolverResults *
solver_run (WorkbookControl *wbc, Sheet *sheet,
	    const SolverLPAlgorithm *alg, gchar **errmsg)
{
	SolverParameters  *param = sheet->solver_parameters;
	SolverProgram     program;
	SolverResults     *res;
	GTimeVal          start, end;
	struct tms        buf;

	g_get_current_time (&start);
	times (&buf);
	if (check_program_definition_failures (sheet, param, &res, errmsg))
	        return NULL;

	res->time_user   = - buf.tms_utime / (gnum_float) sysconf (_SC_CLK_TCK);
	res->time_system = - buf.tms_stime / (gnum_float) sysconf (_SC_CLK_TCK);
	res->time_real   = - (start.tv_sec +
			      start.tv_usec / (gnum_float) G_USEC_PER_SEC);
	save_original_values (res, param, sheet);

	program              = lp_qp_solver_init (sheet, param, res, alg, 
						  -res->time_real, errmsg);
	if (program == NULL)
	        return NULL;

        res->status = alg->solve_fn (program);
	g_get_current_time (&end);
	times (&buf);
	res->time_user   += buf.tms_utime / (gnum_float) sysconf (_SC_CLK_TCK);
	res->time_system += buf.tms_stime / (gnum_float) sysconf (_SC_CLK_TCK);
	res->time_real   += end.tv_sec + end.tv_usec /
	        (gnum_float) G_USEC_PER_SEC;

	solver_prepare_reports (program, res, sheet);
	if (res->status == SolverOptimal) {
	        if (solver_prepare_reports_success (program, res, sheet)) {
		        alg->remove_fn (program);
			return NULL;
		}
	} else
	        restore_original_values (res);

	alg->remove_fn (program);

	return res;
}

SolverResults *
solver (WorkbookControl *wbc, Sheet *sheet, gchar **errmsg)
{
	const SolverLPAlgorithm *alg = NULL;
	SolverParameters  *param = sheet->solver_parameters;

        switch (sheet->solver_parameters->options.model_type) {
	case SolverLPModel:
	        alg = &lp_algorithm [param->options.algorithm];
		break;
	case SolverQPModel:
	        alg = &qp_algorithm [param->options.algorithm];
		break;
	case SolverNLPModel:
	        return NULL;

	default :
		g_assert_not_reached ();
	}

	return solver_run (wbc, sheet, alg, errmsg);
}


SolverParameters *
solver_lp_copy (const SolverParameters *src_param, Sheet *new_sheet)
{
	SolverParameters *dst_param = solver_param_new ();
	GSList           *constraints;
	GSList           *inputs;

	if (src_param->target_cell != NULL)
	        dst_param->target_cell =
		        sheet_cell_fetch (new_sheet,
					  src_param->target_cell->pos.col,
					  src_param->target_cell->pos.row);

	dst_param->problem_type = src_param->problem_type;
	g_free (dst_param->input_entry_str);
	dst_param->input_entry_str = g_strdup (src_param->input_entry_str);

	dst_param->options = src_param->options;
	/* Had there been any non-scalar options, we'd copy them here.  */

	/* Copy the constraints */
	for (constraints = src_param->constraints; constraints;
	     constraints = constraints->next) {
		SolverConstraint *old = constraints->data;
		SolverConstraint *new;

		new = g_new (SolverConstraint, 1);
		*new = *old;
		new->str = g_strdup (old->str);

		dst_param->constraints =
		        g_slist_prepend (dst_param->constraints, new);
	}
	dst_param->constraints = g_slist_reverse (dst_param->constraints);

	/* Copy the input cell list */
	for (inputs = src_param->input_cells; inputs ; inputs = inputs->next) {
		Cell *cell = inputs->data;
		Cell *new_cell;

		new_cell = cell_copy (cell);
		new_cell->base.sheet = new_sheet;
		dst_param->input_cells =
		        g_slist_prepend (dst_param->input_cells,
					 (gpointer) new_cell);
	}
	dst_param->input_cells = g_slist_reverse (dst_param->input_cells);

	dst_param->n_constraints       = src_param->n_constraints;
	dst_param->n_variables         = src_param->n_variables;
	dst_param->n_int_constraints   = src_param->n_int_constraints;
	dst_param->n_bool_constraints  = src_param->n_bool_constraints;
	dst_param->n_total_constraints = src_param->n_total_constraints;

	return dst_param;
}

/*
 * Adjusts the row indecies in the Solver's data structures when rows
 * are inserted.
 */
void
solver_insert_rows (Sheet *sheet, int row, int count)
{
	SolverParameters *param = sheet->solver_parameters;
	GSList           *constraints;
        Value            *input_range;
	Range            range;

	/* Adjust the input range. */
	input_range = global_range_parse (sheet, param->input_entry_str);
	if (input_range != NULL) {
	        if (input_range->v_range.cell.a.row >= row) {
		        range.start.col = input_range->v_range.cell.a.col;
		        range.start.row = input_range->v_range.cell.a.row +
			        count;
		        range.end.col   = input_range->v_range.cell.b.col;
		        range.end.row   = input_range->v_range.cell.b.row +
			        count;
			param->input_entry_str =
			        g_strdup (global_range_name (sheet, &range));
		}
	}

	/* Adjust the constraints. */
	for (constraints = param->constraints; constraints;
	     constraints = constraints->next) {
		SolverConstraint *c = constraints->data;

		if (c->lhs.row >= row)
		        c->lhs.row += count;
		if (c->rhs.row >= row)
		        c->rhs.row += count;
		g_free (c->str);
		c->str = write_constraint_str (c->lhs.col, c->lhs.row,
					       c->rhs.col, c->rhs.row,
					       c->type, c->cols, c->rows);
	}
}

/*
 * Adjusts the column indecies in the Solver's data structures when columns
 * are inserted.
 */
void
solver_insert_cols (Sheet *sheet, int col, int count)
{
	SolverParameters *param = sheet->solver_parameters;
	GSList           *constraints;
        Value            *input_range;
	Range            range;

	/* Adjust the input range. */
	input_range = global_range_parse (sheet, param->input_entry_str);
	if (input_range != NULL) {
	        if (input_range->v_range.cell.a.col >= col) {
		        range.start.col = input_range->v_range.cell.a.col +
			        count;
		        range.start.row = input_range->v_range.cell.a.row;
		        range.end.col   = input_range->v_range.cell.b.col +
			        count;
		        range.end.row   = input_range->v_range.cell.b.row;
			param->input_entry_str =
			        g_strdup (global_range_name (sheet, &range));
		}
	}

	/* Adjust the constraints. */
	for (constraints = param->constraints; constraints;
	     constraints = constraints->next) {
		SolverConstraint *c = constraints->data;

		if (c->lhs.col >= col)
		        c->lhs.col += count;
		if (c->rhs.col >= col)
		        c->rhs.col += count;
		g_free (c->str);
		c->str = write_constraint_str (c->lhs.col, c->lhs.row,
					       c->rhs.col, c->rhs.row,
					       c->type, c->cols, c->rows);
	}
}

/*
 * Adjusts the row indecies in the Solver's data structures when rows
 * are deleted.
 */
void
solver_delete_rows (Sheet *sheet, int row, int count)
{
	SolverParameters *param = sheet->solver_parameters;
	GSList           *constraints;
        Value            *input_range;
	Range            range;

	/* Adjust the input range. */
	input_range = global_range_parse (sheet, param->input_entry_str);
	if (input_range != NULL) {
	        if (input_range->v_range.cell.a.row >= row) {
		        range.start.col = input_range->v_range.cell.a.col;
		        range.start.row = input_range->v_range.cell.a.row -
			        count;
		        range.end.col   = input_range->v_range.cell.b.col;
		        range.end.row   = input_range->v_range.cell.b.row -
			        count;
			if (range.start.row < row || range.end.row < row)
			        param->input_entry_str = g_strdup ("");
			else
			        param->input_entry_str =
				         g_strdup (global_range_name (sheet,
								      &range));
		}
	}

	/* Adjust the constraints. */
	for (constraints = param->constraints; constraints;
	     constraints = constraints->next) {
		SolverConstraint *c = constraints->data;

		if (c->lhs.row >= row)
		        c->lhs.row -= count;
		if (c->rhs.row >= row)
		        c->rhs.row -= count;
		g_free (c->str);
		c->str = write_constraint_str (c->lhs.col, c->lhs.row,
					       c->rhs.col, c->rhs.row,
					       c->type, c->cols, c->rows);
	}
}

/*
 * Adjusts the column indecies in the Solver's data structures when columns
 * are deleted.
 */
void
solver_delete_cols (Sheet *sheet, int col, int count)
{
	SolverParameters *param = sheet->solver_parameters;
	GSList           *constraints;
        Value            *input_range;
	Range            range;

	/* Adjust the input range. */
	input_range = global_range_parse (sheet, param->input_entry_str);
	if (input_range != NULL) {
	        if (input_range->v_range.cell.a.col >= col) {
		        range.start.col = input_range->v_range.cell.a.col -
			        count;
		        range.start.row = input_range->v_range.cell.a.row;
		        range.end.col   = input_range->v_range.cell.b.col -
			        count;
		        range.end.row   = input_range->v_range.cell.b.row;
			if (range.start.col < col || range.end.col < col)
			        param->input_entry_str = g_strdup ("");
			else
			        param->input_entry_str =
				         g_strdup (global_range_name (sheet,
								      &range));
		}
	}

	/* Adjust the constraints. */
	for (constraints = param->constraints; constraints;
	     constraints = constraints->next) {
		SolverConstraint *c = constraints->data;

		if (c->lhs.col >= col)
		        c->lhs.col -= count;
		if (c->rhs.col >= col)
		        c->rhs.col -= count;
		g_free (c->str);
		c->str = write_constraint_str (c->lhs.col, c->lhs.row,
					       c->rhs.col, c->rhs.row,
					       c->type, c->cols, c->rows);
	}
}


