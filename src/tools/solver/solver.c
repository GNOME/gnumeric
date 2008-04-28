/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
#include <glib/gi18n-lib.h>
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
#include "gutils.h"
#include <goffice/utils/go-glib-extras.h>
#include "xml-io.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef HAVE_TIMES
#include <sys/times.h>
#endif

#define GNM 0
#define CXML2C(s) ((char const *)(s))

/* ------------------------------------------------------------------------- */


SolverParameters *
solver_param_new (void)
{
	SolverParameters *res = g_new0 (SolverParameters, 1);

	res->options.model_type          = SolverLPModel;
	res->options.assume_non_negative = TRUE;
	res->options.algorithm           = GLPKSimplex;
	res->options.scenario_name       = g_strdup ("Optimal");
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
	go_slist_free_custom (sp->constraints,
			      (GFreeFunc)solver_constraint_destroy);
	g_slist_free (sp->input_cells);
	g_free (sp->input_entry_str);
	g_free (sp->options.scenario_name);
	g_free (sp);
}

static void
solver_constr_start (GsfXMLIn *xin, xmlChar const **attrs)
{
	int type;
	SolverConstraint *c;
	int i;
	Sheet *sheet = gnm_xml_in_cur_sheet (xin);
	SolverParameters *sp = sheet->solver_parameters;

	c = g_new0 (SolverConstraint, 1);

	for (i = 0; attrs != NULL && attrs[i] && attrs[i + 1] ; i += 2) {
		if (gnm_xml_attr_int (attrs+i, "Lcol", &c->lhs.col) ||
		    gnm_xml_attr_int (attrs+i, "Lrow", &c->lhs.row) ||
		    gnm_xml_attr_int (attrs+i, "Rcol", &c->rhs.col) ||
		    gnm_xml_attr_int (attrs+i, "Rrow", &c->rhs.row) ||
		    gnm_xml_attr_int (attrs+i, "Cols", &c->cols) ||
		    gnm_xml_attr_int (attrs+i, "Rows", &c->rows) ||
		    gnm_xml_attr_int (attrs+i, "Type", &type))
			; /* Nothing */
	}

	switch (type) {
	case 1: c->type = SolverLE; break;
	case 2: c->type = SolverGE; break;
	case 4: c->type = SolverEQ; break;
	case 8: c->type = SolverINT; break;
	case 16: c->type = SolverBOOL; break;
	default: c->type = SolverLE; break;
	}

#ifdef GNM_ENABLE_SOLVER
	c->str = write_constraint_str (c->lhs.col, c->lhs.row,
				       c->rhs.col, c->rhs.row,
				       c->type, c->cols, c->rows);
#endif
	sp->constraints = g_slist_append (sp->constraints, c);
}

void
solver_param_read_sax (GsfXMLIn *xin, xmlChar const **attrs)
{
	Sheet *sheet = gnm_xml_in_cur_sheet (xin);
	SolverParameters *sp = sheet->solver_parameters;
	int i;
	int col = -1, row = -1;
	int ptype;

	static GsfXMLInNode const dtd[] = {
	  GSF_XML_IN_NODE (SHEET_SOLVER_CONSTR, SHEET_SOLVER_CONSTR, GNM, "Constr", GSF_XML_NO_CONTENT, &solver_constr_start, NULL),
	  GSF_XML_IN_NODE_END
	};
	static GsfXMLInDoc *doc;

	for (i = 0; attrs != NULL && attrs[i] && attrs[i + 1] ; i += 2) {
		if (gnm_xml_attr_int (attrs+i, "ProblemType", &ptype))
			sp->problem_type = (SolverProblemType)ptype;
		else 		if (strcmp (CXML2C (attrs[i]), "Inputs") == 0) {
			g_free (sp->input_entry_str);
			sp->input_entry_str = g_strdup (CXML2C (attrs[i+1]));
		} else if (gnm_xml_attr_int (attrs+i, "TargetCol", &col) ||
			   gnm_xml_attr_int (attrs+i, "TargetRow", &row) ||
			   gnm_xml_attr_int (attrs+i, "MaxTime", &(sp->options.max_time_sec)) ||
			   gnm_xml_attr_int (attrs+i, "MaxIter", &(sp->options.max_iter)) ||
			   gnm_xml_attr_bool (attrs+i, "NonNeg", &(sp->options.assume_non_negative)) ||
			   gnm_xml_attr_bool (attrs+i, "Discr", &(sp->options.assume_discrete)) ||
			   gnm_xml_attr_bool (attrs+i, "AutoScale", &(sp->options.automatic_scaling)) ||
			   gnm_xml_attr_bool (attrs+i, "ShowIter", &(sp->options.show_iter_results)) ||
			   gnm_xml_attr_bool (attrs+i, "AnswerR", &(sp->options.answer_report)) ||
			   gnm_xml_attr_bool (attrs+i, "SensitivityR", &(sp->options.sensitivity_report)) ||
			   gnm_xml_attr_bool (attrs+i, "LimitsR", &(sp->options.limits_report)) ||
			   gnm_xml_attr_bool (attrs+i, "PerformR", &(sp->options.performance_report)) ||
			   gnm_xml_attr_bool (attrs+i, "ProgramR", &(sp->options.program_report)))
			; /* Nothing */
	}

	if (col >= 0 && col < gnm_sheet_get_max_cols (sheet) &&
	    row >= 0 && row < gnm_sheet_get_max_rows (sheet))
		sp->target_cell = sheet_cell_fetch (sheet, col, row);

	if (!doc)
		doc = gsf_xml_in_doc_new (dtd, NULL);
	gsf_xml_in_push_state (xin, doc, NULL, NULL, attrs);
}



static SolverResults *
solver_results_init (const SolverParameters *sp)
{
        SolverResults *res     = g_new (SolverResults, 1);

	res->optimal_values    = g_new (gnm_float,  sp->n_variables);
	res->original_values   = g_new (gnm_float,  sp->n_variables);
	res->variable_names    = g_new0 (gchar *,   sp->n_variables);
	res->constraint_names  = g_new0 (gchar *,   sp->n_total_constraints);
	res->shadow_prizes     = g_new0 (gnm_float, sp->n_total_constraints);
	res->slack             = g_new0 (gnm_float, sp->n_total_constraints);
	res->lhs               = g_new0 (gnm_float, sp->n_total_constraints);
	res->rhs               = g_new0 (gnm_float, sp->n_total_constraints);
	res->n_variables       = sp->n_variables;
	res->n_constraints     = sp->n_constraints;
	res->n_nonzeros_in_obj = 0;
	res->n_nonzeros_in_mat = 0;
	res->n_iterations      = 0;
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
	        g_new0 (gnm_float, sp->n_total_constraints);
	res->constr_allowable_decrease =
	        g_new0 (gnm_float, sp->n_total_constraints);

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

GnmCell *
solver_get_target_cell (Sheet *sheet)
{
        return sheet_cell_get (sheet,
			       sheet->solver_parameters->target_cell->pos.col,
			       sheet->solver_parameters->target_cell->pos.row);
}

GnmCell *
solver_get_input_var (SolverResults *res, int n)
{
        return res->input_cells_array[n];
}

SolverConstraint*
solver_get_constraint (SolverResults *res, int n)
{
        return res->constraints_array[n];
}

void
solver_constraint_destroy (SolverConstraint *c)
{
	g_free (c->str);
	g_free (c);
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
	GString    *buf = g_string_new (NULL);
	const char *type_str[] = { "\xe2\x89\xa4" /* "<=" */,
				   "\xe2\x89\xa5" /* ">=" */,
				   "=", "Int", "Bool" };

	if (cols == 1 && rows == 1)
		g_string_append_printf (buf, "%s %s ",
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

	return g_string_free (buf, FALSE);
}

/* ------------------------------------------------------------------------- */

/*
 * This function implements a simple way to determine linear
 * coefficents.  For example, if we have the minization target cell in
 * `target' and the first input variable in `change', this function
 * returns the coefficent of the first variable of the objective
 * function.
 */
static gnm_float
get_lp_coeff (GnmCell *target, GnmCell *change)
{
        gnm_float x0, x1;

	gnm_cell_set_value (change, value_new_float (1));
	cell_queue_recalc (change);
	gnm_cell_eval (target);
	x1 = value_get_as_float (target->value);

	gnm_cell_set_value (change, value_new_float (0));
	cell_queue_recalc (change);
	gnm_cell_eval (target);
	x0 = value_get_as_float (target->value);

	return x1 - x0;
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
	GnmCell  *cell;
	int      i = 0;

	while (inputs != NULL) {
	        cell = (GnmCell *) inputs->data;

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

static void
restore_original_values (SolverResults *res)
{
	GSList *ptr;
	int     i = 0;

	for (ptr = res->param->input_cells; ptr != NULL ; ptr = ptr->next)
		sheet_cell_set_value (ptr->data,
			value_new_float (res->original_values[i++]));
}

/************************************************************************
 */

static int
get_col_nbr (SolverResults *res, GnmCellPos const *pos)
{
        int  i;
        GnmCell *cell;

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
		   gnm_float start_time, GTimeVal start, const gchar **errmsg)
{
        SolverProgram     program;
	GnmCell          *target;
	gnm_float         x;
	int               i, n, ind;

	/* Initialize the SolverProgram structure. */
	program = alg->init_fn (param);

	/* Set up the objective function coefficients. */
	target = solver_get_target_cell (sheet);
	clear_input_vars (param->n_variables, res);

	gnm_cell_eval (target);

	if (param->options.model_type == SolverLPModel) {
	        for (i = 0; i < param->n_variables; i++) {
		        x = get_lp_coeff (target,
					  solver_get_input_var (res, i));
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
	        SolverConstraint const *c = solver_get_constraint (res, i);
		GTimeVal cur_time;
		const GnmValue *lval;
		const GnmValue *rval;
		gnm_float lx, rx;

		target = sheet_cell_get (sheet, c->lhs.col, c->lhs.row);
		if (target) {
			gnm_cell_eval (target);
			lval = target->value;
		} else
			lval = NULL;

		/* Check that LHS is a number type. */
		if (lval == NULL || !VALUE_IS_NUMBER (lval)) {
			*errmsg = _("The LHS cells should contain formulas "
				    "that yield proper numerical values.  "
				    "Specify valid LHS entries.");
			solver_results_free (res);
			return NULL;
		}
		lx = value_get_as_float (lval);

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
		for (n = 0; n < param->n_variables; n++) {
		        x = get_lp_coeff (target,
					  solver_get_input_var (res, n));
			if (x != 0) {
			        res->n_nonzeros_in_mat += 1;
				alg->set_constr_mat_fn (program, n, ind, x);
				res->constr_coeff[i][n] = x;
			}
		}

		target = sheet_cell_get (sheet, c->rhs.col, c->rhs.row);
		if (target) {
			gnm_cell_eval (target);
			rval = target->value;
		} else
			rval = NULL;

		/* Check that RHS is a number type. */
		if (rval == NULL || !VALUE_IS_NUMBER (rval)) {
			*errmsg = _("The RHS cells should contain proper "
				    "numerical values only.  Specify valid "
				    "RHS entries.");
			solver_results_free (res);
			return NULL;
		}
		rx = value_get_as_float (rval);

		x = rx - lx;
		alg->set_constr_fn (program, ind, c->type, x);
		res->rhs[i] = x; 
		ind++;

		/* Check that max time has not elapsed. */
		g_get_current_time (&cur_time);
		if (cur_time.tv_sec - start.tv_sec >
		    param->options.max_time_sec) {
			*errmsg = SOLVER_MAX_TIME_ERR;
			solver_results_free (res);
			return NULL;
		}
		
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
		*errmsg = _("EqualTo models are not supported yet.  Please use Min or Max");
		solver_results_free (res);
	        return NULL; /* FIXME: Equal to feature not yet implemented. */
	default:
		g_warning ("unknown problem type %d", param->problem_type);
		solver_results_free (res);
	        return NULL;
	}

	/* Set options. */
	if (alg->set_option_fn (program, SolverOptAutomaticScaling,
				&(param->options.automatic_scaling),
				NULL, NULL)) {
		*errmsg = _("Failure setting automatic scaling with this solver, try a different algorithm.");
		solver_results_free (res);
	        return NULL;
	}
	if (alg->set_option_fn (program, SolverOptMaxIter, NULL, NULL,
				&(param->options.max_iter))) {
		*errmsg = _("Failure setting the maximum number of iterations with this solver, try a different algorithm.");
		solver_results_free (res);
	        return NULL;
	}
	if (alg->set_option_fn (program, SolverOptMaxTimeSec, NULL, &start_time,
				&(param->options.max_time_sec))) {
		*errmsg = _("Failure setting the maximum solving time with this solver, try a different algorithm.");
		solver_results_free (res);
	        return NULL;
	}

	/* Assume Integer (Discrete) button. */
	if (param->options.assume_discrete) {
	        for (i = 0; i < param->n_variables; i++)
		        alg->set_int_fn (program, i);
		res->ilp_flag = TRUE;
	}

	alg->print_fn (program);

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
				   const gchar      **errmsg)
{
	GSList           *inputs;
	GSList           *c;
	GnmCell          *cell;
	int               i, n;
	GnmCell          **input_cells_array;
	SolverConstraint **constraints_array;

	param->n_variables = 0;

	/*
	 * Checks for the Input cells.
	 */

	/* Count the nbr of the input cells and check that each cell
	 * is in the list only once. */
 	for (inputs = param->input_cells; inputs ; inputs = inputs->next) {
	        cell = (GnmCell *) inputs->data;

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
	input_cells_array = g_new (GnmCell *, param->n_variables);
	i = 0;
 	for (inputs = param->input_cells; inputs ; inputs = inputs->next)
	        input_cells_array[i++] = (GnmCell *) inputs->data;

	param->n_constraints      = 0;
	param->n_int_constraints  = 0;
	param->n_bool_constraints = 0;
	i = 0;
 	for (c = param->constraints; c ; c = c->next) {
	        SolverConstraint *sc = c->data;

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
	        SolverConstraint *sc = c->data;

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
	(*res)->obj_coeff = g_new0 (gnm_float, param->n_variables);

	(*res)->constr_coeff = g_new0 (gnm_float *, param->n_total_constraints);
	for (i = 0; i < param->n_total_constraints; i++)
	        (*res)->constr_coeff[i] = g_new0 (gnm_float,
						  param->n_variables);
	(*res)->limits = g_new (SolverLimits, param->n_variables);

	return FALSE;  /* Everything Ok. */
}

static SolverResults *
solver_run (WorkbookControl *wbc, Sheet *sheet,
	    const SolverLPAlgorithm *alg, const gchar **errmsg)
{
	SolverParameters  *param = sheet->solver_parameters;
	SolverProgram     program;
	SolverResults     *res;
	GTimeVal          start, end;
#ifdef HAVE_TIMES
	struct tms        buf;

	times (&buf);
#warning what is the equivalent of times for win32
#endif

	g_get_current_time (&start);
	if (check_program_definition_failures (sheet, param, &res, errmsg))
	        return NULL;

#ifdef HAVE_SYSCONF
	res->time_user   = - buf.tms_utime / (gnm_float) sysconf (_SC_CLK_TCK);
	res->time_system = - buf.tms_stime / (gnm_float) sysconf (_SC_CLK_TCK);
#else
#warning TODO
#endif
	res->time_real   = - (start.tv_sec +
			      start.tv_usec / (gnm_float) G_USEC_PER_SEC);
	save_original_values (res, param, sheet);

	program              = lp_qp_solver_init (sheet, param, res, alg, 
						  -res->time_real, start,
						  errmsg);
	if (program == NULL)
	        return NULL;

        res->status = alg->solve_fn (program);
	g_get_current_time (&end);
#ifdef HAVE_TIMES
	times (&buf);
#ifdef HAVE_SYSCONF
	res->time_user   += buf.tms_utime / (gnm_float) sysconf (_SC_CLK_TCK);
	res->time_system += buf.tms_stime / (gnm_float) sysconf (_SC_CLK_TCK);
#else
#warning TODO
#endif
#else
	res->time_user   = 0;
	res->time_system = 0;
#endif
	res->time_real   += end.tv_sec + end.tv_usec /
	        (gnm_float) G_USEC_PER_SEC;
	res->n_iterations = alg->get_iterations_fn (program);

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
solver (WorkbookControl *wbc, Sheet *sheet, const gchar **errmsg)
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

	g_free (dst_param->options.scenario_name);
	dst_param->options = src_param->options;
	dst_param->options.scenario_name = g_strdup (src_param->options.scenario_name);
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
		GnmCell const *old_cell = inputs->data;
		GnmCell *new_cell = sheet_cell_fetch (new_sheet,
						      old_cell->pos.col,
						      old_cell->pos.row);
		dst_param->input_cells =
			g_slist_prepend (dst_param->input_cells, new_cell);
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
	GSList	 *constraints;
        GnmValue *input_range;
	GnmRange	  range;

	/* Adjust the input range. */
	input_range = value_new_cellrange_str (sheet, param->input_entry_str);
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
	GSList	 *constraints;
        GnmValue *input_range;
	GnmRange	  range;

	/* Adjust the input range. */
	input_range = value_new_cellrange_str (sheet, param->input_entry_str);
	if (input_range != NULL &&
	    input_range->v_range.cell.a.col >= col) {
		range.start.col = input_range->v_range.cell.a.col + count;
		range.start.row = input_range->v_range.cell.a.row;
		range.end.col   = input_range->v_range.cell.b.col + count;
		range.end.row   = input_range->v_range.cell.b.row;
		param->input_entry_str = g_strdup (
			global_range_name (sheet, &range));
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
	GSList	 *constraints;
        GnmValue *input_range;
	GnmRange	  range;

	/* Adjust the input range. */
	input_range = value_new_cellrange_str (sheet, param->input_entry_str);
	if (input_range != NULL &&
	    input_range->v_range.cell.a.row >= row) {
		range.start.col = input_range->v_range.cell.a.col;
		range.start.row = input_range->v_range.cell.a.row - count;
		range.end.col   = input_range->v_range.cell.b.col;
		range.end.row   = input_range->v_range.cell.b.row - count;
		if (range.start.row < row || range.end.row < row)
			param->input_entry_str = g_strdup ("");
		else
			param->input_entry_str = g_strdup (
				global_range_name (sheet, &range));
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
	GSList	 *constraints;
        GnmValue *input_range;
	GnmRange	  range;

	/* Adjust the input range. */
	input_range = value_new_cellrange_str (sheet, param->input_entry_str);
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
