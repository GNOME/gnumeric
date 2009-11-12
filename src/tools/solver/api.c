/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * api.c:  The Solver's API wrappings for various optimization algorithms.
 *
 * Author:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2002 by Jukka-Pekka Iivonen
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

#include "solver.h"
#include "func.h"
#include "cell.h"
#include "sheet.h"
#include "sheet-style.h"
#include "dependent.h"
#include "dialogs.h"
#include "mstyle.h"
#include "value.h"
#include "mathfunc.h"
#include "analysis-tools.h"

#include <math.h>
#include <stdlib.h>

#include "api.h"

/* ------------------------------------------------------------------------- */

/*
 * Solver's API wrappings for QP (currently dummy only).
 *
 * Package:    none
 * Version:    0
 * License:    GPL
 * Homepage:   -
 * Algorithm:  -
 *
 */

static SolverProgram
w_qp_dummy_init (const SolverParameters *param)
{
        printf ("w_qp_dummy_init\n");
	return NULL;
}

static void
w_qp_dummy_delete (SolverProgram program)
{
        printf ("w_qp_dummy_delete\n");
}

static void
w_qp_dummy_set_maxim (SolverProgram program)
{
        printf ("w_qp_set_maxim\n");
}

static void
w_qp_dummy_set_minim (SolverProgram program)
{
        printf ("w_qp_set_minim\n");
}

static void
w_qp_dummy_set_obj_fn (SolverProgram program, int col, gnm_float value)
{
        printf ("w_qp_dummy_set_obj_fn %d, %" GNM_FORMAT_g "\n", col, value);
}

static void
w_qp_dummy_set_constr_mat (SolverProgram program, int col, int row,
			   gnm_float value)
{
        printf ("w_qp_dummy_set_constr_mat %d, %d, %" GNM_FORMAT_g "\n",
		col, row, value);
}

static void
w_qp_dummy_set_constr (SolverProgram program, int row,
		       SolverConstraintType type,
		       gnm_float value)
{
        printf ("w_qp_dummy_set_constr %d, %d, %" GNM_FORMAT_g "\n",
		row, type, value);
}

static void
w_qp_dummy_set_int (SolverProgram program, int col)
{
        printf ("w_qp_dummy_set_int %d\n", col);
}

static void
w_qp_dummy_set_bool (SolverProgram program, int col)
{
        printf ("w_qp_dummy_set_bool %d\n", col);
}

static SolverStatus
w_qp_dummy_solve (SolverProgram program)
{
        printf ("w_qp_dummy_solve\n");
	return SolverInfeasible;
}

static gnm_float
w_qp_dummy_get_solution (SolverProgram program, int col)
{
        printf ("w_qp_dummy_get_solution %d\n", col);
	return 0;;
}

static gnm_float
w_qp_dummy_get_value_of_obj_fn (SolverProgram program)
{
        printf ("w_qp_dummy_get_value_of_obj_fn\n");
	return 0;
}

static gnm_float
w_qp_dummy_get_dual (SolverProgram program, int row)
{
        printf ("w_qp_dummy_get_dual %d\n", row);
	return 0;
}

static int
w_qp_dummy_solver_lp_get_iterations (SolverProgram p)
{
	return 0;
}

static gboolean
w_qp_dummy_set_option (SolverProgram program, SolverOptionType option,
		   const gboolean *b_value,
		   const gnm_float *f_value, const int *i_value)
{
        printf ("w_qp_dummy_set_option %d\n", option);
        return FALSE;
}

static void
w_qp_dummy_print (SolverProgram program)
{
        printf ("w_qp_dummy_print\n");
}

/* ------------------------------------------------------------------------- */

/*
 * This array contains the linear programming algorithms available.
 * Each algorithm should implement the API defined by the
 * SolverLPAlgorithm data structure.  The algorithms should be able to
 * do MILP as well.  Feel free to add new algorithms.
 */
const SolverLPAlgorithm lp_algorithm [] = {
        {
	        NULL,
		(solver_init_fn*)                w_qp_dummy_init,
		(solver_remove_fn*)              w_qp_dummy_delete,
		(solver_lp_set_obj_fn*)          w_qp_dummy_set_obj_fn,
		(solver_lp_set_constr_mat_fn*)   w_qp_dummy_set_constr_mat,
		(solver_lp_set_constr_fn*)       w_qp_dummy_set_constr,
		(solver_lp_set_maxim_fn*)        w_qp_dummy_set_maxim,
		(solver_lp_set_minim_fn*)        w_qp_dummy_set_minim,
		(solver_lp_set_int_fn*)          w_qp_dummy_set_int,
		(solver_lp_set_bool_fn*)         w_qp_dummy_set_bool,
		(solver_lp_solve_fn*)            w_qp_dummy_solve,
		(solver_lp_get_obj_fn_value_fn*) w_qp_dummy_get_value_of_obj_fn,
		(solver_lp_get_obj_fn_var_fn*)   w_qp_dummy_get_solution,
		(solver_lp_get_shadow_prize_fn*) w_qp_dummy_get_dual,
		(solver_lp_get_iterations_fn *)  w_qp_dummy_solver_lp_get_iterations,
		(solver_lp_set_option_fn*)       w_qp_dummy_set_option,
		(solver_lp_print_fn*)            w_qp_dummy_print
	},
	{ NULL }
};

const SolverLPAlgorithm qp_algorithm [] = {
        {
	        NULL,
		(solver_init_fn*)                w_qp_dummy_init,
		(solver_remove_fn*)              w_qp_dummy_delete,
		(solver_lp_set_obj_fn*)          w_qp_dummy_set_obj_fn,
		(solver_lp_set_constr_mat_fn*)   w_qp_dummy_set_constr_mat,
		(solver_lp_set_constr_fn*)       w_qp_dummy_set_constr,
		(solver_lp_set_maxim_fn*)        w_qp_dummy_set_maxim,
		(solver_lp_set_minim_fn*)        w_qp_dummy_set_minim,
		(solver_lp_set_int_fn*)          w_qp_dummy_set_int,
		(solver_lp_set_bool_fn*)         w_qp_dummy_set_bool,
		(solver_lp_solve_fn*)            w_qp_dummy_solve,
		(solver_lp_get_obj_fn_value_fn*) w_qp_dummy_get_value_of_obj_fn,
		(solver_lp_get_obj_fn_var_fn*)   w_qp_dummy_get_solution,
		(solver_lp_get_shadow_prize_fn*) w_qp_dummy_get_dual,
		(solver_lp_get_iterations_fn *)  w_qp_dummy_solver_lp_get_iterations,
		(solver_lp_set_option_fn*)       w_qp_dummy_set_option,
		(solver_lp_print_fn*)            w_qp_dummy_print
	},
	{ NULL }
};
