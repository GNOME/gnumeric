/*
 * api.h:
 *
 * Author:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2002 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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

#ifndef GNUMERIC_SOLVER_API_H
#define GNUMERIC_SOLVER_API_H

#include "gnumeric.h"
#include "numbers.h"
#include "solver.h"
#include <tools/gnm-solver.h>

/*
 * Solver's API for LP solving algorithms
 */
typedef SolverProgram
        (solver_init_fn)                (SolverParameters const *param);
typedef void
        (solver_remove_fn)              (SolverProgram p);
typedef void
        (solver_lp_set_obj_fn)          (SolverProgram p, int col, gnm_float v);
typedef void
        (solver_lp_set_constr_mat_fn)   (SolverProgram p, int col, int row,
					 gnm_float v);
typedef void
        (solver_lp_set_constr_fn)       (SolverProgram p, int row,
					 GnmSolverConstraintType t, gnm_float rhs);
typedef void
        (solver_lp_set_maxim_fn)        (SolverProgram p);
typedef void
        (solver_lp_set_minim_fn)        (SolverProgram p);
typedef void
        (solver_lp_set_int_fn)          (SolverProgram p, int col);
typedef void
        (solver_lp_set_bool_fn)         (SolverProgram p, int col);
typedef SolverStatus
        (solver_lp_solve_fn)            (SolverProgram p);
typedef gnm_float
        (solver_lp_get_obj_fn_value_fn) (SolverProgram p);
typedef gnm_float
        (solver_lp_get_obj_fn_var_fn)   (SolverProgram p, int col);
typedef gnm_float
        (solver_lp_get_shadow_prize_fn) (SolverProgram p, int row);
typedef gboolean
        (solver_lp_set_option_fn)       (SolverProgram p, SolverOptionType option,
					 const gboolean *b_value,
					 const gnm_float *f_value,
					 const int *i_value);
typedef void
        (solver_lp_print_fn)            (SolverProgram p);
typedef int
        (solver_lp_get_iterations_fn)   (SolverProgram p);


typedef struct {
        char const                    *name;
        solver_init_fn                *init_fn;
        solver_remove_fn              *remove_fn;
        solver_lp_set_obj_fn          *set_obj_fn;
        solver_lp_set_constr_mat_fn   *set_constr_mat_fn;
        solver_lp_set_constr_fn       *set_constr_fn;
        solver_lp_set_maxim_fn        *maxim_fn;
        solver_lp_set_minim_fn        *minim_fn;
        solver_lp_set_int_fn          *set_int_fn;
        solver_lp_set_bool_fn         *set_bool_fn;
        solver_lp_solve_fn            *solve_fn;
        solver_lp_get_obj_fn_value_fn *get_obj_fn_value_fn;
        solver_lp_get_obj_fn_var_fn   *get_obj_fn_var_fn;
        solver_lp_get_shadow_prize_fn *get_shadow_prize_fn;
	solver_lp_get_iterations_fn   *get_iterations_fn;
        solver_lp_set_option_fn       *set_option_fn;
        solver_lp_print_fn            *print_fn;
} SolverLPAlgorithm;


extern const SolverLPAlgorithm lp_algorithm [];
extern const SolverLPAlgorithm qp_algorithm [];

#define __HAVE_GLPK__ 1
#define SOLVER_DEBUG  0

#endif
