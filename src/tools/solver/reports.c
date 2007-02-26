/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * reports.c:  Solver report generation.
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
#include <glib/gi18n-lib.h>
#include "reports.h"

#include "gnm-format.h"
#include "solver.h"
#include "func.h"
#include "cell.h"
#include "sheet.h"
#include "workbook.h"
#include "sheet-style.h"
#include "dependent.h"
#include "dialogs.h"
#include "mstyle.h"
#include "value.h"
#include "mathfunc.h"
#include "analysis-tools.h"
#include "reports-write.h"
#include "api.h"
#include "solver.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

/* ------------------------------------------------------------------------- */

static void
get_input_variable_names (SolverResults *res, Sheet *sheet)
{
        GnmCell *cell;
	int  i;

	for (i = 0; i < res->param->n_variables; i++) {
	        cell = solver_get_input_var (res, i);
		res->variable_names[i] = dao_find_name (sheet, cell->pos.col,
							cell->pos.row);
	}
}

static void
get_constraint_names (SolverResults *res, Sheet *sheet)
{
	int  i;

	for (i = 0; i < res->param->n_total_constraints; i++) {
	        SolverConstraint *c = solver_get_constraint (res, i);
		res->constraint_names[i] = dao_find_name (sheet, c->lhs.col,
							  c->lhs.row);
	}
}


/*
 * Returns the value of a cell when one of the input variables is reset.
 */
static gnm_float
get_target_cell_value (SolverResults *res, GnmCell *target_cell,
		       int col, gnm_float x, gnm_float *old_value)
{
        GnmCell *var_cell = solver_get_input_var (res, col);
	*old_value = value_get_as_float (var_cell->value);
	sheet_cell_set_value (var_cell, value_new_float (x));
	gnm_cell_eval (target_cell);
	return value_get_as_float (target_cell->value);
}

static gboolean
is_still_feasible (Sheet *sheet, SolverResults *res, int col, gnm_float value)
{
        gnm_float c_value, rhs, old_value = res->optimal_values [col];
	int        i, n;
	GnmCell       *cell;
	gboolean   status = FALSE;

	res->optimal_values[col] = value;
	for (i = 0; i < res->param->n_total_constraints; i++) {
	        SolverConstraint *c = solver_get_constraint (res, i);

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
		        if (gnm_abs (c_value - rhs) < 0.000001 /* FIXME */)
			        goto out;
			break;
		case SolverINT:
		case SolverBOOL:
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

	for (i = 0; i < param->n_total_constraints; i++) {
	        gnm_float       slack, lhs, rhs, x, y, old_val;
		SolverConstraint *c = res->constraints_array[i];
		GnmCell             *cell;

		cell  = sheet_cell_get (sheet, c->rhs.col, c->rhs.row);
		rhs   = value_get_as_float (cell->value);
		cell  = sheet_cell_get (sheet, c->lhs.col, c->lhs.row);
		lhs   = value_get_as_float (cell->value);
		slack = gnm_abs (rhs - lhs);
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
					cell = solver_get_target_cell (sheet);
					get_target_cell_value (res, cell, n,
							       x, &y);
					gnm_cell_eval (cell);
					res->limits[n].lower_result = 
					        value_get_as_float (cell->value);
				}
				if (x > res->limits[n].upper_limit) {
				        res->limits[n].upper_limit = x;
					cell = solver_get_target_cell (sheet);
					get_target_cell_value (res, cell, n,
							       x, &y);
					gnm_cell_eval (cell);
					res->limits[n].upper_result =
					        value_get_as_float (cell->value);
				}
			} else
				; /* FIXME */
			get_target_cell_value (res, cell, n, old_val, &y);
		}
	}
}

/*
 * Fetch the optimal variable values and store them into the input cells.
 */
static void
set_optimal_values_to_sheet (SolverProgram *program, Sheet *sheet,
			     SolverResults *res, const SolverLPAlgorithm *alg,
			     gnm_float *store)
{
        int  i;
	GnmCell *cell;

	for (i = 0; i < res->param->n_variables; i++) {
	        store[i] = alg->get_obj_fn_var_fn (program, i);
		cell = res->input_cells_array[i];
		sheet_cell_set_value (cell, value_new_float (store[i]));
	}
	workbook_recalc (sheet->workbook);
}

void
solver_prepare_reports (SolverProgram *program, SolverResults *res,
			Sheet *sheet)
{
	SolverParameters *param = res->param;
	const SolverLPAlgorithm *alg;

	if (res->param->options.model_type == SolverLPModel)
	        alg = &lp_algorithm[param->options.algorithm];
	else
	        alg = &qp_algorithm[param->options.algorithm];

        res->target_name = dao_find_name (sheet,
					  res->param->target_cell->pos.col,
					  res->param->target_cell->pos.row);
        get_input_variable_names (res, sheet);
        get_constraint_names (res, sheet);

}

gboolean
solver_prepare_reports_success (SolverProgram *program, SolverResults *res,
				Sheet *sheet)
{
	SolverParameters  *param = res->param;
        GnmCell              *cell;
	int               i;
	const SolverLPAlgorithm *alg;

	if (res->param->options.model_type == SolverLPModel)
	        alg = &lp_algorithm[param->options.algorithm];
	else
	        alg = &qp_algorithm[param->options.algorithm];


	/*
	 * Set optimal values into the program.
	 */
	set_optimal_values_to_sheet (program, sheet, res, alg,
				     &res->optimal_values[0]);

	/*
	 * Fetch the target cell value from the sheet since it's
	 * formula may have a constant increment or decrement.
	 */
	cell = solver_get_target_cell (sheet);
	res->value_of_obj_fn = value_get_as_float (cell->value);

	/*
	 * Initialize the limits structure.
	 */
	for (i = 0; i < param->n_variables; i++) {
	        res->limits[i].lower_limit = res->limits[i].upper_limit =
		        res->optimal_values[i];
		res->limits[i].lower_result = 
		        res->limits[i].upper_result =
		        value_get_as_float (cell->value);
	}

	/*
	 * Go through the constraints; save LHS, RHS, slack
	 */
	for (i = 0; i < param->n_constraints; i++) {
	        SolverConstraint const *c = solver_get_constraint (res, i);

		res->shadow_prizes[i] = alg->get_shadow_prize_fn (program, i);

		cell = sheet_cell_get (sheet, c->lhs.col, c->lhs.row);
		res->lhs[i] = value_get_as_float (cell->value);

		res->slack[i] = gnm_abs (res->rhs[i] - res->lhs[i]);
	}

	if (param->options.limits_report && ! res->ilp_flag)
	        calculate_limits (sheet, param, res);

	/* Get allowable increase and decrease for constraints. */
	if (param->options.sensitivity_report && ! res->ilp_flag) {
		/* gnm_float *store = g_new (gnm_float, param->n_variables);*/
	        for (i = 0; i < param->n_total_constraints; i++) {
			SolverConstraint *c = res->constraints_array[i];

			if (c->type == SolverINT || c->type == SolverBOOL)
			        continue;

			if (res->slack[i] < 0.0001 /* FIXME */) {
			        res->constr_allowable_increase[i] = 0; /* FIXME */
			}
		}
	}

	return FALSE;
}

gchar *
solver_reports (WorkbookControl *wbc, Sheet *sheet, SolverResults *res,
		gboolean answer, gboolean sensitivity, gboolean limits,
		gboolean performance, gboolean program, gboolean dual)
{
        gchar *err = NULL;

        if (answer && res->param->options.model_type == SolverLPModel)
	        solver_answer_report (wbc, sheet, res);
	if (sensitivity && ! res->ilp_flag
	    && res->param->options.model_type == SolverLPModel)
	        solver_sensitivity_report (wbc, sheet, res);
	if (limits && ! res->ilp_flag
	    && res->param->options.model_type == SolverLPModel)
	        solver_limits_report (wbc, sheet, res);
	if (performance
	    && res->param->options.model_type == SolverLPModel)
	        solver_performance_report (wbc, sheet, res);
	if (program) {
	        if (solver_program_report (wbc, sheet, res))
		        err = _("Model is too large for program report "
				"generation. Program report was not "
				"created.");
	}
	if (dual && res->param->options.model_type == SolverLPModel)
	        solver_dual_program_report (wbc, sheet, res);
	return err;
}
