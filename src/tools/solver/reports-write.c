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
#include "gnumeric.h"
#include "numbers.h"
#include "reports-write.h"

#include "gnm-format.h"
#include "parse-util.h"
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

#ifdef HAVE_UNAME
#include <sys/utsname.h>
#endif

/* ------------------------------------------------------------------------- */

/*
 * Generates the Solver's answer report.
 */
void
solver_answer_report (WorkbookControl *wbc,
		      Sheet           *sheet,
		      SolverResults   *res)
{
        data_analysis_output_t dao;
	GnmCell                   *cell;
	int                    i, vars;

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Answer Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");


	/*
	 * Fill in the labels of `Target Cell' section.
	 */
	dao_set_cell (&dao, 1, 6, _("Cell"));
	dao_set_cell (&dao, 2, 6, _("Name"));
	dao_set_cell (&dao, 3, 6, _("Original Value"));
	dao_set_cell (&dao, 4, 6, _("Final Value"));
	dao_set_bold (&dao, 0, 6, 4, 6);

	/* Set `Cell' field (cell reference to the target cell). */
	dao_set_cell (&dao, 1, 7, cell_name (res->param->target_cell));

	/* Set `Name' field */
	dao_set_cell (&dao, 2, 7, res->target_name);

	/* Set `Original Value' field */
	dao_set_cell_float (&dao, 3, 7, res->original_value_of_obj_fn);

	/* Set `Final Value' field */
	dao_set_cell_float (&dao, 4, 7, res->value_of_obj_fn);


	/*
	 * Fill in the labels of `Adjustable Cells' section.
	 */
	dao_set_cell (&dao, 1, 11,   _("Cell"));
	dao_set_cell (&dao, 2, 11,   _("Name"));
	dao_set_cell (&dao, 3, 11,   _("Original Value"));
	dao_set_cell (&dao, 4, 11,   _("Final Value"));
	dao_set_bold (&dao, 0, 11, 4, 11);

	for (i = 0; i < vars; i++) {
		/* Set `Cell' column */
	        cell = solver_get_input_var (res, i);
		dao_set_cell (&dao, 1, 12 + i, cell_name (cell));

		/* Set `Name' column */
		dao_set_cell (&dao, 2, 12 + i, res->variable_names [i]);

		/* Set `Original Value' column */
		dao_set_cell_value (&dao, 3, 12 + i,
				value_new_float (res->original_values[i]));

		/* Set `Final Value' column */
		dao_set_cell_value (&dao, 4, 12 + i,
				value_dup (cell->value));
	}


	/*
	 * Fill in the labels of `Constraints' section.
	 */
	dao_set_cell (&dao, 1, 15 + vars, _("Cell"));
	dao_set_cell (&dao, 2, 15 + vars, _("Name"));
	dao_set_cell (&dao, 3, 15 + vars, _("Cell Value"));
	dao_set_cell (&dao, 4, 15 + vars, _("Formula"));
	dao_set_cell (&dao, 5, 15 + vars, _("Status"));
	dao_set_cell (&dao, 6, 15 + vars, _("Slack"));
	dao_set_bold (&dao, 0, 15 + vars, 6, 15 + vars);

	for (i = 0; i < res->param->n_total_constraints; i++) {
	        SolverConstraint *c = res->constraints_array[i];

		/* Set `Cell' column */
		dao_set_cell (&dao, 1, 16 + vars + i,
			  cell_coord_name (c->lhs.col, c->lhs.row));

		/* Set `Name' column */
		dao_set_cell (&dao, 2, 16 + vars + i,
			      res->constraint_names [i]);

		/* Set `Cell Value' column */
		dao_set_cell_float (&dao, 3, 16 + vars + i, res->lhs[i]);

	        /* Set `Formula' column */
	        dao_set_cell (&dao, 4, 16 + vars + i, c->str);

		if (c->type == SolverINT || c->type == SolverBOOL) {
		        dao_set_cell (&dao, 5, 16 + vars + i, _("Binding"));
		        continue;
		}

		/* Set `Status' column */
		if (res->slack[i] < 0.001  /* FIXME */)
		        dao_set_cell (&dao, 5, 16 + vars + i, _("Binding"));
		else
		        dao_set_cell (&dao, 5, 16 + vars + i, _("Not Binding"));

		/* Set `Slack' column */
		dao_set_cell_float (&dao, 6, 16 + vars + i, res->slack [i]);
	}

	/*
	 * Autofit columns to make the sheet more readable.
	 */

	dao_autofit_these_columns (&dao, 0, 5);

	/*
	 * Check if assume integer is on.
	 */
	if (res->param->options.assume_discrete) {
	        dao_set_cell (&dao, 1, 18 + vars + i,
			      _("Assume that all variables are integers."));
	}


	/*
	 * Fill in the titles.
	 */

	/* Fill in the column A labels into the answer report sheet. */
	if (res->param->problem_type == SolverMaximize)
	        dao_set_cell (&dao, 0, 5, _("Target Cell (Maximize)"));
	else
	        dao_set_cell (&dao, 0, 5, _("Target Cell (Minimize)"));

	/* Fill in the header titles. */
	dao_write_header (&dao, _("Solver"), _("Answer Report"), sheet);

	/* Fill in other titles. */
	dao_set_cell (&dao, 0, 10, _("Adjustable Cells"));
	dao_set_cell (&dao, 0, 14 + vars, _("Constraints"));
}


/*
 * Generates the Solver's sensitivity report.
 */
void
solver_sensitivity_report (WorkbookControl *wbc,
			   Sheet           *sheet,
			   SolverResults   *res)
{
        data_analysis_output_t dao;
	GnmCell                   *cell;
	int                    i, vars;

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Sensitivity Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");


	/*
	 * Fill in the labels of `Adjustable Cells' section.
	 */

	dao_set_cell (&dao, 3, 6, _("Final"));
	dao_set_cell (&dao, 4, 6, _("Reduced"));
	dao_set_cell (&dao, 5, 6, _("Objective"));
	dao_set_cell (&dao, 6, 6, _("Allowable"));
	dao_set_cell (&dao, 7, 6, _("Allowable"));

	dao_set_cell (&dao, 1, 7, _("Cell"));
	dao_set_cell (&dao, 2, 7, _("Name"));
	dao_set_cell (&dao, 3, 7, _("Value"));
	dao_set_cell (&dao, 4, 7, _("Cost"));
	dao_set_cell (&dao, 5, 7, _("Coefficient"));
	dao_set_cell (&dao, 6, 7, _("Increase"));
	dao_set_cell (&dao, 7, 7, _("Decrease"));
	dao_set_bold (&dao, 0, 6, 7, 7);

	for (i = 0; i < vars; i++) {
		/* Set `Cell' column */
	        cell = solver_get_input_var (res, i);
		dao_set_cell (&dao, 1, 8 + i, cell_name (cell));

		/* Set `Name' column */
		dao_set_cell (&dao, 2, 8 + i, res->variable_names[i]);

		/* Set `Final Value' column */
		dao_set_cell_value (&dao, 3, 8 + i,
				value_dup (cell->value));

		/* Set `Reduced Cost' column */
		/* FIXME: Set this also?? */

		/* Set `Objective Coefficient' column */
		dao_set_cell_float (&dao, 5, 8 + i, res->obj_coeff[i]);

		/* FIXME: Set this also?? */

		/* Set `Allowable Increase' column */
		/* FIXME: Set this also?? */

		/* Set `Allowable Decrease' column */
		/* FIXME: Set this also?? */
	}


	/*
	 * Fill in the labels of `Constraints' section.
	 */
	dao_set_cell (&dao, 3, 10 + vars, _("Final"));
	dao_set_cell (&dao, 4, 10 + vars, _("Shadow"));
	dao_set_cell (&dao, 5, 10 + vars, _("Constraint"));
	dao_set_cell (&dao, 6, 10 + vars, _("Allowable"));
	dao_set_cell (&dao, 7, 10 + vars, _("Allowable"));

	dao_set_cell (&dao, 1, 11 + vars, _("Cell"));
	dao_set_cell (&dao, 2, 11 + vars, _("Name"));
	dao_set_cell (&dao, 3, 11 + vars, _("Value"));
	dao_set_cell (&dao, 4, 11 + vars, _("Price"));
	dao_set_cell (&dao, 5, 11 + vars, _("R.H. Side"));
	dao_set_cell (&dao, 6, 11 + vars, _("Increase"));
	dao_set_cell (&dao, 7, 11 + vars, _("Decrease"));
	dao_set_bold (&dao, 0, 10 + vars, 7, 11 + vars);

	for (i = 0; i < res->param->n_total_constraints; i++) {
	        SolverConstraint *c = res->constraints_array[i];

		/* Set `Cell' column */
		dao_set_cell (&dao, 1, 12 + vars + i,
			  cell_coord_name (c->lhs.col, c->lhs.row));

		/* Set `Name' column */
		dao_set_cell (&dao, 2, 12 + vars + i,
			      res->constraint_names [i]);

		/* Set `Final Value' column */
		cell = sheet_cell_get (sheet, c->lhs.col, c->lhs.row);
		dao_set_cell_value (&dao, 3, 12 + vars + i,
				    value_dup (cell->value));

		/* Set `Shadow Price' */
		dao_set_cell_value (&dao, 4, 12 + vars + i,
				value_new_float (res->shadow_prizes[i]));

		/* Set `Constraint R.H. Side' column */
		dao_set_cell_float (&dao, 5, 12 + vars + i, res->rhs[i]);

		/* Set `Allowable Increase/Decrease' columns */
		if (res->slack[i] < 0.001  /* FIXME */) {
		        dao_set_cell_float (&dao, 6, 12 + vars + i,
				      res->constr_allowable_increase[i]);
		        /* FIXME */
		} else {
		        switch (c->type) {
		        case SolverLE:
			        dao_set_cell (&dao, 6, 12 + vars + i,
					      _("Infinity"));
			        dao_set_cell_float (&dao, 7, 12 + vars + i,
						    res->slack[i]);
				break;
			case SolverGE:
			        dao_set_cell_float (&dao, 6, 12 + vars + i,
						    res->slack[i]);
			        dao_set_cell (&dao, 7, 12 + vars + i,
					      _("Infinity"));
				break;
			case SolverEQ:
			        dao_set_cell_float (&dao, 6, 12 + vars + i, 0);
			        dao_set_cell_float (&dao, 7, 12 + vars + i, 0);
				break;
			default:
			        break;
			}
		}
	}


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	dao_autofit_these_columns (&dao, 0, 4);


	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	dao_write_header (&dao, _("Solver"), _("Sensitivity Report"), sheet);

	/* Fill in other titles. */
	dao_set_cell (&dao, 0, 5, _("Adjustable Cells"));
	dao_set_cell (&dao, 0, 9 + vars, _("Constraints"));
}


/*
 * Generates the Solver's limits report.
 */
void
solver_limits_report (WorkbookControl *wbc,
		      Sheet           *sheet,
		      SolverResults   *res)
{
        data_analysis_output_t dao;
	GnmCell                   *cell;
	int                    vars, i;

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Limits Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;

	/* Set thise to fool the autofit_column function.  (They will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");
	dao_set_cell (&dao, 4, 3, "A");
	dao_set_cell (&dao, 7, 3, "A");


	/*
	 * Fill in the labels.
	 */

	dao_set_cell (&dao, 2, 5, _("Target"));
	dao_set_cell (&dao, 1, 6, _("Cell"));
	dao_set_cell (&dao, 2, 6, _("Name"));
	dao_set_cell (&dao, 3, 6, _("Value"));
	dao_set_bold (&dao, 2, 5, 2, 5);
	dao_set_bold (&dao, 0, 6, 3, 6);

	dao_set_cell (&dao, 2, 10, _("Adjustable"));
	dao_set_cell (&dao, 1, 11, _("Cell"));
	dao_set_cell (&dao, 2, 11, _("Name"));
	dao_set_cell (&dao, 3, 11, _("Value"));

	dao_set_cell (&dao, 5, 10, _("Lower"));
	dao_set_cell (&dao, 6, 10, _("Target"));
	dao_set_cell (&dao, 5, 11, _("Limit"));
	dao_set_cell (&dao, 6, 11, _("Result"));

	dao_set_cell (&dao, 8, 10, _("Upper"));
	dao_set_cell (&dao, 9, 10, _("Target"));
	dao_set_cell (&dao, 8, 11, _("Limit"));
	dao_set_cell (&dao, 9, 11, _("Result"));

	dao_set_bold (&dao, 2, 10, 9, 10);
	dao_set_bold (&dao, 0, 11, 9, 11);


	/*
	 * Fill in the target cell section.
	 */

	/* Set `Target Cell' field (cell reference to the target cell). */
	dao_set_cell (&dao, 1, 7, cell_name (res->param->target_cell));

	/* Set `Target Name' field */
	dao_set_cell (&dao, 2, 7, res->target_name);

	/* Set `Target Value' field */
        cell = sheet_cell_get (sheet, res->param->target_cell->pos.col,
                               res->param->target_cell->pos.row);
        dao_set_cell_float (&dao, 3, 7, res->value_of_obj_fn);


	/*
	 * Fill in the adjustable cells and limits section.
	 */

	for (i = 0; i < vars; i++) {
		/* Set `Adjustable Cell' column */
	        cell = solver_get_input_var (res, i);
		dao_set_cell (&dao, 1, 12 + i, cell_name (cell));

		/* Set `Adjustable Name' column */
		dao_set_cell (&dao, 2, 12 + i, res->variable_names[i]);

		/* Set `Adjustable Value' column */
		dao_set_cell_value (&dao, 3, 12 + i,
				value_dup (cell->value));


		/* Set `Lower Limit' column */
		dao_set_cell_float (&dao, 5, 12 + i,
				    res->limits[i].lower_limit);

		/* Set `Target Result' column */
		dao_set_cell_float (&dao, 6, 12 + i,
				    res->limits[i].lower_result);


		/* Set `Upper Limit' column */
		dao_set_cell_float (&dao, 8, 12 + i,
				    res->limits[i].upper_limit);

		/* Set `Target Result' column */
		dao_set_cell_float (&dao, 9, 12 + i,
				    res->limits[i].upper_result);
	}


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	dao_autofit_these_columns (&dao, 0, 9);

	/* Clear these after autofit calls */
	dao_set_cell (&dao, 4, 3, "");
	dao_set_cell (&dao, 7, 3, "");

	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	dao_write_header (&dao, _("Solver"), _("Limits Report"), sheet);
}


/* Generates the Solver's performance report.  Contains some statistical
 * information regarding the program, information on how long it took
 * to be solved, and what kind of a system did the processing.
 */
void
solver_performance_report (WorkbookControl *wbc,
			   Sheet           *sheet,
			   SolverResults   *res)
{
        data_analysis_output_t dao;
	int                    mat_size, i;
	GnmValue              *v;

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Performance Report"));

	dao.sheet->hide_grid = TRUE;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");


	/*
	 * Fill in the labels of `General Info' section.
	 */

	dao_set_cell (&dao, 1, 6, _("Type"));
	dao_set_cell (&dao, 1, 7, _("Status"));
	dao_set_cell (&dao, 1, 8, _("Number of Iterations"));
	dao_set_bold (&dao, 1, 6, 1, 8);

	/* Print the problem type. */
	switch (res->param->problem_type) {
	case SolverMinimize:
	        dao_set_cell (&dao, 2, 6, _("Minimization"));
		break;
	case SolverMaximize:
	        dao_set_cell (&dao, 2, 6, _("Maximization"));
		break;
	case SolverEqualTo:
	        dao_set_cell (&dao, 2, 6, _("Target value search"));
		break;
	}

	/* Print the status. */
	switch (res->status) {
	case SolverOptimal:
	        dao_set_cell (&dao, 2, 7, _("Optimal solution found"));
		break;
	case SolverUnbounded:
	        dao_set_cell (&dao, 2, 7, _("Unbounded problem"));
		break;
	case SolverInfeasible:
	        dao_set_cell (&dao, 2, 7, _("Infeasible problem"));
		break;
	case SolverMaxIterExc:
	        dao_set_cell (&dao, 2, 7, 
			      _("Maximum number of iterations "
				"exceeded: optimization interrupted"));
		break;
	case SolverMaxTimeExc:
	        dao_set_cell (&dao, 2, 7, 
			      _("Maximum time exceeded: optimization "
				"interrupted"));
		break;
	default:
	        /* This should never occur. */
	        break;
	}

	/* Set the `Nbr of Iterations'. */
	dao_set_cell_value (&dao, 2, 8,
			    value_new_float (res->n_iterations));


	/*
	 * Fill in the labels of `Problem Size' section.
	 */

	dao_set_cell (&dao, 2, 12, _("Variables"));
	dao_set_cell (&dao, 3, 12, _("Constraints"));
	dao_set_cell (&dao, 4, 12, _("Integer Constraints"));
	dao_set_cell (&dao, 5, 12, _("Boolean Constraints"));
	dao_set_cell (&dao, 1, 13, _("Number of"));
	dao_set_bold (&dao, 0, 12, 5, 12);
	dao_set_bold (&dao, 1, 13, 1, 13);

	/* Set the `Nbr of Variables'. */
	dao_set_cell_value (&dao, 2, 13,
			    value_new_float (res->param->n_variables));

	/* Set the `Nbr of Constraints'. */
	dao_set_cell_value (&dao, 3, 13,
			    value_new_float (res->param->n_constraints));

	/* Set the `Nbr of Int Constraints'. */
	dao_set_cell_value (&dao, 4, 13, 
			    value_new_float (res->param->n_int_constraints));

	/* Set the `Nbr of Bool Constraints'. */
	dao_set_cell_value (&dao, 5, 13,
			    value_new_float (res->param->n_bool_constraints));


	/*
	 * Fill in the labels of `Data Sparsity' section.
	 */

	dao_set_cell (&dao, 2, 17, _("Matrix"));
	dao_set_cell (&dao, 2, 18, _("Elements"));
	dao_set_cell (&dao, 3, 17, _("Non-zeros in"));
	dao_set_cell (&dao, 3, 18, _("Constraints"));
	dao_set_cell (&dao, 4, 17, _("Non-zeros in"));
	dao_set_cell (&dao, 4, 18, _("Obj. fn"));
	dao_set_cell (&dao, 1, 19, _("Number of"));
	dao_set_cell (&dao, 1, 20, _("Ratio"));
	dao_set_bold (&dao, 0, 17, 4, 17);
	dao_set_bold (&dao, 0, 18, 4, 18);
	dao_set_bold (&dao, 1, 19, 1, 20);

	/* Set the `Nbr of Matrix Elements'. */
	mat_size = res->param->n_variables * res->param->n_constraints;
	dao_set_cell_value (&dao, 2, 19, value_new_float (mat_size));

	/* Set the `Ratio of Matrix Elements'. */
	v = value_new_float (1);
	value_set_fmt (v, go_format_default_percentage ());
	dao_set_cell_value (&dao, 2, 20, v);

	/* Set the `Nbr of Non-zeros (constr.)'. */
	dao_set_cell_value (&dao, 3, 19,
			    value_new_float (res->n_nonzeros_in_mat));

	/* Set the `Ratio of Non-zeros (constr.)'. */
	v = value_new_float ((gnm_float) res->n_nonzeros_in_mat / mat_size);
	value_set_fmt (v, go_format_default_percentage ());
	dao_set_cell_value (&dao, 3, 20, v);

	/* Set the `Nbr of Non-zeros (obj. fn)'. */
	dao_set_cell_value (&dao, 4, 19,
			    value_new_float (res->n_nonzeros_in_obj));

	/* Set the `Ratio of Non-zeros (obj. fn)'. */
	v = value_new_float ((gnm_float) res->n_nonzeros_in_obj /
			     res->param->n_variables);
	value_set_fmt (v, go_format_default_percentage ());
	dao_set_cell_value (&dao, 4, 20, v);
			

	/*
	 * Fill in the labels of `Computing Time' section.
	 */

	dao_set_cell (&dao, 2, 24, _("User"));
	dao_set_cell (&dao, 3, 24, _("System"));
	dao_set_cell (&dao, 4, 24, _("Real"));
	dao_set_cell (&dao, 1, 25, _("Time (sec.)"));
	dao_set_bold (&dao, 0, 24, 4, 24);
	dao_set_bold (&dao, 1, 24, 1, 25);

	/* Set the `User Time'. */
	dao_set_cell_value (&dao, 2, 25, value_new_float (res->time_user));

	/* Set the `System Time'. */
	dao_set_cell_value (&dao, 3, 25, value_new_float (res->time_system));

	/* Set the `Real Time'. */
	dao_set_cell_value (&dao, 4, 25,
			    value_new_float (gnm_fake_round
					     (res->time_real * 100) / 100.0));


	/*
	 * Fill in the labels of `System Information' section.
	 */

	dao_set_cell (&dao, 2, 29, _("CPU Model"));
	dao_set_cell (&dao, 3, 29, _("CPU MHz"));
	dao_set_cell (&dao, 4, 29, _("OS"));
	dao_set_cell (&dao, 1, 30, _("Name"));
	dao_set_bold (&dao, 0, 29, 4, 29);
	dao_set_bold (&dao, 1, 30, 1, 30);

	/* Set the `CPU Model'. */
	dao_set_cell (&dao, 2, 30, _("Unknown"));

	/* Set the `CPU Mhz'. */
	dao_set_cell (&dao, 3, 30, _("Unknown"));

	/* Set the `OS Name'. */
	{
#ifdef HAVE_UNAME
		struct utsname unamedata;
		if (uname (&unamedata) != -1) {
			GnmValue *r = value_new_string_nocopy (
				g_strdup_printf ("%s (%s)",
				unamedata.sysname, unamedata.release));
			dao_set_cell_value (&dao, 4, 30, r);
		} else
#endif
			dao_set_cell (&dao, 4, 30, _("Unknown"));
	}


	/*
	 * Fill in the labels of `Options' section.
	 */
	/* Set the labels. */
	dao_set_bold (&dao, 1, 34, 1, 38);
	dao_set_cell (&dao, 1, 34, _("Algorithm:"));
	dao_set_cell (&dao, 1, 35, _("Model Assumptions:"));
	dao_set_cell (&dao, 1, 36, _("Autoscaling:"));
	dao_set_cell (&dao, 1, 37, _("Max Iterations:"));
	dao_set_cell (&dao, 1, 38, _("Max Time:"));


	/* Set the options. */
	dao_set_cell (&dao, 2, 34, _("LP Solve"));
	dao_set_cell (&dao, 1, 35, _("Model Assumptions:"));

	/* Set the `Assumptions'. */
	i = 0;
	if (res->param->options.assume_discrete) {
	        dao_set_cell (&dao, 2 + i++, 35, _("Discrete"));
	}
	if (res->param->options.assume_non_negative) {
	        dao_set_cell (&dao, 2 + i++, 35, _("Non-Negative"));
	}
	if (i == 0)
	        dao_set_cell (&dao, 2, 35, _("None"));

	/* Set `Autoscaling'. */
	if (res->param->options.automatic_scaling)
	        dao_set_cell (&dao, 2, 36, _("Yes"));
	else
	        dao_set_cell (&dao, 2, 36, _("No"));

	/* Set Max iterations/time. */
	dao_set_cell_float (&dao, 2, 37, res->param->options.max_iter);
	dao_set_cell_float (&dao, 2, 38, res->param->options.max_time_sec);


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	dao_autofit_these_columns (&dao, 0, 6);


	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	dao_write_header (&dao, _("Solver"), _("Performance Report"), sheet);

	/* Fill in other titles. */
	dao_set_cell (&dao, 0,  5, _("General Information"));
	dao_set_cell (&dao, 0, 11, _("Problem Size"));
	dao_set_cell (&dao, 0, 16, _("Data Sparsity"));
	dao_set_cell (&dao, 0, 23, _("Computing Time"));
	dao_set_cell (&dao, 0, 28, _("System Information"));
	dao_set_cell (&dao, 0, 33, _("Options"));
}


/* Generates the Solver's program report.
 */
gboolean
solver_program_report (WorkbookControl *wbc,
		       Sheet           *sheet,
		       SolverResults   *res)
{
        data_analysis_output_t dao;
	int                    i, col, row, max_col, n, vars;

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Program Report"));

	dao.sheet->hide_grid = TRUE;
	vars                 = res->param->n_variables;


	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");
	dao_set_cell (&dao, 1, 3, "A");


	/* Print the objective function. */
	max_col = 0;
	if (res->param->options.model_type == SolverLPModel) {
	        /* This is for linear models. */
	        col = 0;
		for (i = 0; i < vars; i++) {
		        if (res->obj_coeff[i] != 0) {
				if (1 + col*3 + 3 > gnm_sheet_get_max_cols (sheet))
					goto unsuccessful;

			        /* Print the sign. */
			        if (res->obj_coeff[i] < 0)
				        dao_set_cell (&dao, 1 + col*3, 6, "-");
				else if (col > 0)
				        dao_set_cell (&dao, 1 + col*3, 6, "+");

				/* Print the coefficent. */
				if (gnm_abs (res->obj_coeff[i]) != 1)
				        dao_set_cell_float (&dao, 2 + col*3, 6,
					        gnm_abs (res->obj_coeff[i]));

				/* Print the name of the variable. */
				dao_set_cell (&dao, 3 + col*3, 6,
					      res->variable_names [i]);
				col++;
				if (col > max_col)
				        max_col = col;
			}
		}
	} else {
	        /* This is for quadratic models. (SolverQPModel) */
	}


	/* Print the constraints. */
	row = 10;
	for (i = 0; i < res->param->n_total_constraints; i++, row++) {
	        SolverConstraint const *c = res->constraints_array[i];

		/* Print the constraint function. */
		col = 0;
		if (c->type == SolverINT) {
		        dao_set_cell (&dao, col*3 + 1, row, "integer");
		        continue;
		}
		if (c->type == SolverBOOL) {
		        dao_set_cell (&dao, col*3 + 1, row, "bool");
		        continue;
		}
		for (n = 0; n < res->param->n_variables; n++) {
		        if (res->constr_coeff[i][n] != 0) {
			        /* Print the sign. */
			        if (res->constr_coeff[i][n] < 0)
				        dao_set_cell (&dao, 1 + col*3,
						      row, "-");
				else if (col > 0)
				        dao_set_cell (&dao, 1 + col*3,
						      row, "+");

				/* Print the coefficent. */
				if (gnm_abs (res->constr_coeff[i][n]) != 1)
				        dao_set_cell_float (&dao, 2 + col*3,
							    row,
					     gnm_abs (res->constr_coeff[i][n]));

				/* Print the name of the variable. */
				dao_set_cell (&dao, 3 + col*3, row,
					      res->variable_names [n]);
				col++;
				if (col > max_col)
				        max_col = col;
			}
		}


		/* Print the type. */
		switch (c->type) {
		case SolverLE:
			/* "<=" character.  */
		        dao_set_cell (&dao, col*3 + 1, row, "\xe2\x89\xa4");
			break;
		case SolverGE:
			/* ">=" character.  */
		        dao_set_cell (&dao, col*3 + 1, row, "\xe2\x89\xa5");
			break;
		case SolverEQ:
		        dao_set_cell (&dao, col*3 + 1, row, "=");
		        break;
		default :
			g_warning ("unknown constraint type %d", c->type);
		}

		/* Set RHS column. */
		dao_set_cell_float (&dao, col*3 + 2, row, res->rhs[i]);
	}


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	dao_autofit_these_columns (&dao, 0, max_col*3 + 2);


	/*
	 * Check if assume integer is on.
	 */
	if (res->param->options.assume_discrete) {
	        dao_set_cell (&dao, 1, row + 1,
			      _("Assume that all variables are integers."));
		row++;
	}

	/*
	 * Check if assume non-negative is on.
	 */
	if (res->param->options.assume_non_negative) {
	        dao_set_cell (&dao, 1, row + 1,
			      _("Assume that all variables take only positive "
				"values."));
	}


	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	dao_set_cell (&dao, 1, 3, "");
	dao_write_header (&dao, _("Solver"), _("Program Report"), sheet);

	/* Print the type of the program. */
	switch (res->param->problem_type) {
	case SolverMinimize:
	        dao_set_cell (&dao, 0, 5, _("Minimize"));
		break;
	case SolverMaximize:
	        dao_set_cell (&dao, 0, 5, _("Maximize"));
		break;
	case SolverEqualTo:
	        dao_set_cell (&dao, 0, 5, _("Equal to"));
		break;
	}
	dao_set_bold (&dao, 0, 5, 0, 5);

	/* Print `Subject to' title. */
	dao_set_cell (&dao, 0, 9, _("Subject to"));
	dao_set_bold (&dao, 0, 9, 0, 9);
	return FALSE;

 unsuccessful:

	workbook_sheet_delete (dao.sheet);
	return TRUE;
}


/* Generates the Solver's dual program report.
 */
void
solver_dual_program_report (WorkbookControl *wbc,
			    Sheet           *sheet,
			    SolverResults   *res)
{
        data_analysis_output_t dao;

	dao_init (&dao, NewSheetOutput);
        dao_prepare_output (wbc, &dao, _("Dual Program Report"));

	dao.sheet->hide_grid = TRUE;

	/* Set this to fool the autofit_column function.  (It will be
	 * overwriten). */
	dao_set_cell (&dao, 0, 0, "A");

	/*
	 * Fill in the titles.
	 */

	/* Fill in the header titles. */
	dao_write_header (&dao, _("Solver"), _("Dual Program Report"), sheet);
}
