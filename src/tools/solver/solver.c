/*
 * solver.c:  The Solver's core system.
 *
 * Author:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 1999, 2000, 2002 by Jukka-Pekka Iivonen
*/
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "numbers.h"

#include "parse-util.h"
#include "solver.h"
#include "func.h"
#include "cell.h"
#include "sheet.h"
#include "sheet-style.h"
#include "eval.h"
#include "dialogs.h"
#include "mstyle.h"
#include "value.h"
#include "mathfunc.h"
#include "analysis-tools.h"

#include <math.h>
#include <stdlib.h>
#include <libgnome/gnome-i18n.h>

#include "lp_solve/lpkit.h"

/* ------------------------------------------------------------------------- */


/* This array contains the linear programming algorithms available.
 * Each algorithm should implement the API defined by the
 * SolverLPAlgorithm data structure.  The algorithms should be able to
 * do MILP as well.  Feel free to add new algorithms.
 */
static SolverLPAlgorithm lp_algorithm[] = {
        {
	        NULL,
		(solver_lp_init_fn*)             lp_solve_init,
		(solver_lp_remove_fn*)           lp_solve_delete_lp,
		(solver_lp_set_obj_fn*)          lp_solve_set_obj_fn,
		(solver_lp_set_constr_mat_fn*)   lp_solve_set_constr_mat,
		(solver_lp_set_constr_type_fn*)  lp_solve_set_constr_type,
		(solver_lp_set_constr_rhs_fn*)   lp_solve_set_constr_rhs,
		(solver_lp_set_maxim_fn*)        lp_solve_set_maxim,
		(solver_lp_set_minim_fn*)        lp_solve_set_minim,
		(solver_lp_set_int_fn*)          lp_solve_set_int,
		(solver_lp_solve_fn*)            lp_solve_solve,
		(solver_lp_get_obj_fn_value_fn*) lp_solve_get_value_of_obj_fn,
		(solver_lp_get_obj_fn_var_fn*)   lp_solve_get_solution,
		(solver_lp_get_shadow_prize_fn*) lp_solve_get_dual
	},
	{ NULL }
};


SolverParameters *
solver_param_new (void)
{
	SolverParameters *res = g_new0 (SolverParameters, 1);

	res->options.assume_linear_model = TRUE;
	res->options.assume_non_negative = TRUE;
	res->options.algorithm           = LPSolve;
	res->input_entry_str             = g_strdup ("");
	res->problem_type                = SolverMaximize;
	res->constraints                 = NULL;
	res->target_cell                 = NULL;

	return res;
}

void
solver_param_destroy (SolverParameters *sp)
{
	g_free (sp->input_entry_str);
	g_free (sp);
}

SolverResults *
solver_results_init (const SolverParameters *sp)
{
        SolverResults *res     = g_new (SolverResults, 1);

	res->optimal_values    = g_new (gnum_float, sp->n_variables);
	res->original_values   = g_new (gnum_float, sp->n_variables);
	res->variable_names    = g_new0 (gchar *, sp->n_variables);
	res->constraint_names  = g_new0 (gchar *, sp->n_constraints);
	res->shadow_prizes     = g_new0 (gnum_float, sp->n_constraints +
					 sp->n_int_bool_constraints);
	res->n_variables       = sp->n_variables;
	res->n_constraints     = sp->n_constraints;
	res->n_nonzeros_in_obj = 0;
	res->n_nonzeros_in_mat = 0;
	res->time_user         = 0;
	res->time_system       = 0;
	res->time_real         = 0;
	res->ilp_flag          = FALSE;
	res->input_cells_array = NULL;
	res->constraints_array = NULL;
	res->obj_coeff         = NULL;
	res->constr_coeff      = NULL;
	res->limits            = NULL;

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
	g_free (res);
}

/* ------------------------------------------------------------------------- */

Cell*
get_solver_target_cell (Sheet *sheet)
{
        return sheet_cell_get (sheet,
			       sheet->solver_parameters->target_cell->pos.col,
			       sheet->solver_parameters->target_cell->pos.row);
}

Cell*
get_solver_input_var (SolverResults *res, int n)
{
        return res->input_cells_array[n];
}

SolverConstraint*
get_solver_constraint (SolverResults *res, int n)
{
        return res->constraints_array[n];
}

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
	c->rows = 1;
	c->cols = 1;
	c->type = type;
	c->str  = write_constraint_str (lhs_col, lhs_row, rhs_col,
					rhs_row, type, 1, 1);

	return c;
}

/*
 * This function implements a simple way to determine linear
 * coefficents.  For example, if we have the minization target cell in
 * `target' and the first input variable in `change', this function
 * returns the coefficent of the first variable of the objective
 * function.
 */
static gnum_float
get_lp_coeff (Cell *target, Cell *change)
{
        gnum_float x0, x1;

	sheet_cell_set_value (change, value_new_float (0.0));
	cell_eval (target);
	x0 = value_get_as_float (target->value);

	sheet_cell_set_value (change, value_new_float (1.0));
	cell_eval (target);
	x1 = value_get_as_float (target->value);

	return x1 - x0;
}

/*
 * Returns the value of a cell when one of the input variables is reset.
 */
static gnum_float
get_target_cell_value (SolverResults *res, Cell *target_cell,
		       int col, gnum_float x, gnum_float *old_value)
{
        Cell *var_cell;

	var_cell = get_solver_input_var (res, col);
	*old_value = value_get_as_float (var_cell->value);
	sheet_cell_set_value (var_cell, value_new_float (x));
	cell_eval (target_cell);

	return value_get_as_float (target_cell->value);
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
  
	CellList *inputs = param->input_cells;
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

	cell = get_solver_target_cell (sheet);
	res->original_value_of_obj_fn = value_get_as_float (cell->value);
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

/*
 * Initializes the program according to the information given in the
 * solver dialog and the related sheet.  After the call, the LP
 * program is ready to run.
 */
static SolverProgram
lp_solver_init (Sheet *sheet, const SolverParameters *param, SolverResults *res)
{
        SolverProgram  program;
	Cell          *target;
	gnum_float    x;
	int           i, n;

	/* Initialize the SolverProgram structure. */
	program = lp_algorithm[param->options.algorithm].init_fn
	        (param->n_variables, param->n_constraints);

	/* Set up the objective function coefficients. */
	target = get_solver_target_cell (sheet);
	for (i = 0; i < param->n_variables; i++) {
	        x = get_lp_coeff (target, get_solver_input_var (res, i));
		if (x != 0) {
		        lp_algorithm[param->options.algorithm].
			        set_obj_fn (program, i+1, x);
			res->n_nonzeros_in_obj += 1;
			res->obj_coeff[i] = x;
		}
	}
			 
	/* Add constraints. */
	for (i = 0; i < param->n_constraints + param->n_int_bool_constraints;
	     i++) {
	        SolverConstraint *c = get_solver_constraint (res, i);
		target = sheet_cell_get (sheet, c->lhs.col, c->lhs.row);

		if (c->type == SolverINT) {
		        lp_algorithm[param->options.algorithm].
			        set_int_fn (program, i, TRUE);
			res->ilp_flag = TRUE;
		        continue;
		}
		for (n = 0; n < param->n_variables; n++) {
		        x = get_lp_coeff (target,
					  get_solver_input_var (res, n));
			if (x != 0) {
			        res->n_nonzeros_in_mat += 1;
				lp_algorithm[param->options.algorithm].
				        set_constr_mat_fn (program, n, i, x);
				res->constr_coeff[i][n] = x;
			}
		}
		target = sheet_cell_get (sheet, c->rhs.col, c->rhs.row);
		x = value_get_as_float (target->value);
		lp_algorithm[param->options.algorithm].
		        set_constr_rhs_fn (program, i, x);
		lp_algorithm[param->options.algorithm].
		        set_constr_type_fn (program, i, c->type);
	}

	/* Set up the problem type. */
	switch (param->problem_type) {
	case SolverMinimize:
	        lp_algorithm[param->options.algorithm].minim_fn (program);
	        break;
	case SolverMaximize:
	        lp_algorithm[param->options.algorithm].maxim_fn (program);
	        break;
	case SolverEqualTo:
	        return NULL; /* FIXME: Equal to feature not yet implemented. */
	default:
	        return NULL;
	}

	print_lp(program);

	return program;
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
	CellList         *inputs;
	GSList           *c;
	Cell             *cell;
	int              i, n;
	Cell             **input_cells_array;
	SolverConstraint **constraints_array;

	param->n_variables = 0;

	/*
	 * Checks for the Target cell.
	 */

	/* Check that target cell is not empty. */
	cell = get_solver_target_cell (sheet);
	if (cell == NULL || cell->value == NULL
	    || VALUE_IS_EMPTY (cell->value)) {
	        *errmsg = _("Target cell is empty. The objective function "
			    "cannot be determined.");
		return TRUE;
	}

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

	param->n_constraints = 0;
	param->n_int_bool_constraints = 0;
	i = 0;
 	for (c = param->constraints; c ; c = c->next) {
	        SolverConstraint *sc = (SolverConstraint *) c->data;

		if (sc->type == SolverINT || sc->type == SolverBOOL)
		        param->n_int_bool_constraints +=
			        MAX (sc->rows, sc->cols);
		else
		        param->n_constraints += MAX (sc->rows, sc->cols);
	}
	constraints_array = g_new (SolverConstraint *,
				   param->n_constraints +
				   param->n_int_bool_constraints);
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

	(*res)->input_cells_array = input_cells_array;
	(*res)->constraints_array = constraints_array;
	(*res)->obj_coeff = g_new0 (gnum_float, param->n_variables);

	(*res)->constr_coeff = g_new (gnum_float *, param->n_constraints
				      + param->n_int_bool_constraints);
	for (i = 0; i < param->n_constraints
	       + param->n_int_bool_constraints; i++)
	        (*res)->constr_coeff[i] = g_new0 (gnum_float,
						  param->n_variables);
	(*res)->limits = g_new (SolverLimits, param->n_variables);

	return FALSE;  /* Everything Ok. */
}

static gboolean
is_still_feasible (Sheet *sheet, SolverResults *res, int col, gnum_float value)
{
        gnum_float c_value, rhs, old_value = res->optimal_values [col];
	int        i, n;
	Cell       *cell;
	gboolean   status = FALSE;

	res->optimal_values[col] = value;
	for (i = 0; i < res->param->n_constraints
	       + res->param->n_int_bool_constraints; i++) {
	        SolverConstraint *c = get_solver_constraint (res, i);

		c_value = 0;
		for (n = 0; n < res->param->n_variables; n++)
		        c_value += res->constr_coeff[i][n]
			  * res->optimal_values[n];
		cell = sheet_cell_get (sheet, c->rhs.col, c->rhs.row);
		rhs  = value_get_as_float (cell->value);

		switch (c->type) {
		case SolverLE:
		        if (c_value - 0.000001 /* FIXME */ > rhs)
			        goto out;
			break;
		case SolverGE:
		        if (c_value + 0.000001 /* FIXME */ < rhs)
			        goto out;
			break;
		case SolverEQ:
		        if (gnumabs (c_value - rhs) < 0.000001 /* FIXME */)
			        goto out;
			break;
		case SolverINT:
		case SolverBOOL:
		case SolverOF:
		        break;
		}
	}
	status = TRUE;
 out:
	res->optimal_values[col] = old_value;
	return status;
}

/*
 * Calculate upper and lower limits for the limits reporting.
 */
static void
calculate_limits (Sheet *sheet, SolverParameters *param, SolverResults *res)
{
        int i, n;

	for (i = 0; i < param->n_constraints
	       + param->n_int_bool_constraints; i++) {
	        gnum_float       slack, lhs, rhs, x, y, old_val;
		SolverConstraint *c = res->constraints_array[i];
		Cell             *cell;

		cell  = sheet_cell_get (sheet, c->rhs.col, c->rhs.row);
		rhs   = value_get_as_float (cell->value);
		cell  = sheet_cell_get (sheet, c->lhs.col, c->lhs.row);
		lhs   = value_get_as_float (cell->value);
		slack = gnumabs (rhs - lhs);
		for (n = 0; n < param->n_variables; n++) {
		        x = get_target_cell_value (res, cell, n, 0, &old_val);
			x = rhs - x;
			if (res->constr_coeff[i][n] != 0) {
			        x = x / res->constr_coeff[i][n];
				if (! is_still_feasible (sheet, res, n, x)) {
				        get_target_cell_value (res, cell, n,
							       old_val, &y);
				        continue;
				}
				if (x < res->limits[n].lower_limit
				    && (x >= 0 ||
					!param->options.assume_non_negative)) {
				        res->limits[n].lower_limit = x;
					cell = get_solver_target_cell (sheet);
					get_target_cell_value (res, cell, n,
							       x, &y);
					cell_eval (cell);
					res->limits[n].lower_result = 
					        value_get_as_float (cell->value);
				}
				if (x > res->limits[n].upper_limit) {
				        res->limits[n].upper_limit = x;
					cell = get_solver_target_cell (sheet);
					get_target_cell_value (res, cell, n,
							       x, &y);
					cell_eval (cell);
					res->limits[n].upper_result =
					        value_get_as_float (cell->value);
				}
			} else ; /* FIXME */
			get_target_cell_value (res, cell, n, old_val, &y);
		}
	}
}

SolverResults *
solver (WorkbookControl *wbc, Sheet *sheet, gchar **errmsg)
{
	SolverParameters *param = sheet->solver_parameters;
	SolverProgram     program;
	SolverResults    *res;
	Cell             *cell;
	int               i;
	GTimeVal          start, end;

	if (check_program_definition_failures (sheet, param, &res, errmsg))
	        return NULL;

	save_original_values (res, param, sheet);

	program              = lp_solver_init (sheet, param, res);

	g_get_current_time (&start);
        res->status          = lp_algorithm[param->options.algorithm]
	        .solve_fn (program);
	g_get_current_time (&end);
	res->time_real = end.tv_sec - start.tv_sec
	        + (end.tv_usec - start.tv_usec) / 1000000.0;

	res->param = sheet->solver_parameters;
	if (res->status == SOLVER_LP_OPTIMAL) {
	        res->value_of_obj_fn = lp_algorithm[param->options.algorithm]
		        .get_obj_fn_value_fn (program);
		for (i = 0; i < param->n_variables; i++) {
		        res->optimal_values[i] =
			        lp_algorithm[param->options.algorithm]
			                 .get_obj_fn_var_fn (program, i + 1);
			cell = res->input_cells_array[i];
			sheet_cell_set_value (cell, value_new_float
					      (res->optimal_values[i]));
		}

		/* Initialize the limits. */
		cell = get_solver_target_cell (sheet);
		cell_eval (cell);
		for (i = 0; i < param->n_variables; i++) {
		        res->limits[i].lower_limit =
			        res->limits[i].upper_limit =
			        res->optimal_values[i];
			res->limits[i].lower_result = 
			        res->limits[i].upper_result =
			        value_get_as_float (cell->value);
		}

		for (i = 0; i < param->n_constraints
		       + param->n_int_bool_constraints; i++) {
		        res->shadow_prizes[i] =
			        lp_algorithm[param->options.algorithm]
			                .get_shadow_prize_fn (program, i);
		}
		if (param->options.limits_report)
		        calculate_limits (sheet, param, res);
	}

	lp_algorithm[param->options.algorithm].remove_fn (program);
	return res;
}


SolverParameters *
solver_lp_copy (const SolverParameters *src_param, Sheet *new_sheet)
{
	SolverParameters *dst_param = solver_param_new ();
	GSList   *constraints;
	CellList *inputs;

	if (src_param->target_cell != NULL)
	        dst_param->target_cell =
		    sheet_cell_fetch (new_sheet,
				      src_param->target_cell->pos.row,
				      src_param->target_cell->pos.col);

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

		/* FIXME: O(n^2).  */
		dst_param->constraints = g_slist_append (dst_param->constraints, new);
	}

	/* Copy the input cell list */
	for (inputs = src_param->input_cells; inputs ; inputs = inputs->next) {
		Cell *cell = inputs->data;
		Cell *new_cell;

		new_cell = cell_copy (cell);
		new_cell->base.sheet = new_sheet;
		dst_param->input_cells = (CellList *)
			g_slist_append ((GSList *)dst_param->input_cells,
					(gpointer) new_cell);
	}

	return dst_param;
}
