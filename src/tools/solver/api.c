/*
 * api.c:  The Solver's API wrappings for various optimization algorithms.
 *
 * Author:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * (C) Copyright 2002 by Jukka-Pekka Iivonen
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


/*
 * The Solver provides an API for linear programming algorithms so that an
 * optimization algorithm can be plugged in with small possible effort.  To
 * integrate an algorithm you should just implement the following set of
 * functions that wrap the process of defining an LP program, setting up
 * various options, and fetching the results for reporting.
 *
 *
 * typedef SolverProgram (solver_lp_init_fn)             (int n_vars,
 *					                  int n_constraints);
 *   Initializes the program.
 *
 * typedef void          (solver_lp_remove_fn)           (SolverProgram p);
 *   Frees all memory buffers previously allocated for the program.
 *
 * typedef void          (solver_lp_set_obj_fn)          (SolverProgram p,
 *					                  int col, gnum_float v);
 *   Sets a coefficent of the objective function.  The column numbering begins
 *   from zero.
 *
 * typedef void          (solver_lp_set_constr_mat_fn)   (SolverProgram p,
 *                                                        int col,
 *						          int row, gnum_float v);
 *   Sets a coefficent of a constraint.  The column and row numbering begins
 *   from zero.
 *
 * typedef void          (solver_lp_set_constr_type_fn)  (SolverProgram p,
 *                                                        int row,
 *						          SolverConstraintType);
 *   Sets the type of a constraint.  The row numbering begins from zero.
 *
 * typedef void          (solver_lp_set_constr_rhs_fn)   (SolverProgram p,
 *						          int row,
 *                                                        gnum_float rhs);
 *   Sets the right hand side value of a constraint.  The row numbering begins
 *   from zero.
 *
 * typedef void          (solver_lp_set_maxim_fn)        (SolverProgram p);
 *   Sets the program type to be a maximization program.
 *
 * typedef void          (solver_lp_set_minim_fn)        (SolverProgram p);
 *   Sets the program type to be a minimization program.
 *
 * typedef void          (solver_lp_set_int_fn)          (SolverProgram p,
 *                                                        int col,
 *						          gboolean must_be_int);
 *   Sets or resets an integer constraint for a variable.  The column numbering
 *   begins from zero.
 *
 * typedef int           (solver_lp_solve_fn)            (SolverProgram p);
 *   Runs the solver to determine the optimal solution.
 *
 * typedef gnum_float    (solver_lp_get_obj_fn_value_fn) (SolverProgram p);
 *   Returns the final value of the objective function.  If the optimal value
 *   was not found the result is undetermined, otherwise the optimum value
 *   is returned.
 *
 * typedef gnum_float    (solver_lp_get_obj_fn_var_fn)   (SolverProgram p,
 *                                                        int col);
 *   Returns the value of a variable coeffient if the optimal value was found.
 *   The result is undetermined if the optimal value was not found.  The column
 *   numbering begins from zero.
 *
 * typedef gnum_float    (solver_lp_get_shadow_prize_fn) (SolverProgram p,
 *                                                        int row);
 *   Returns the shadow prize for a constraint.  If the optimal value was not
 *   found the result is undetermined.  The row numbering begins from zero.
 *
*/

/*
 * Solver's API wrappings for the LP Solve 3.2.
 *
 */

lprec *
lp_solve_init (int n_vars, int n_constraints)
{
        return lp_solve_make_lp (n_constraints, n_vars);
}

void
lp_solve_set_maxim (lprec *lp)
{
        lpkit_set_maxim (lp);
}

void
lp_solve_set_minim (lprec *lp)
{
        lpkit_set_minim (lp);
}

void
lp_solve_set_obj_fn (lprec *lp, int col, gnum_float value)
{
        set_mat (lp, 0, col + 1, value);
}

void
lp_solve_set_constr_mat (lprec *lp, int col, int row, gnum_float value)
{
        set_mat (lp, row + 1, col + 1, value);
}

void
lp_solve_set_constr_rhs (lprec *lp, int row, gnum_float value)
{
        set_rh (lp, row + 1, value);
}

void
lp_solve_set_constr_type (lprec *lp, int row, SolverConstraintType type)
{
        set_constr_type (lp, row + 1, type);
}

gnum_float
lp_solve_get_solution (lprec *lp, int column)
{
        return lp->best_solution [lp->rows + column];
}

gnum_float
lp_solve_get_value_of_obj_fn (lprec *lp)
{
        return lp->best_solution [0];
}

/* ------------------------------------------------------------------------- */


/*
 * This array contains the linear programming algorithms available.
 * Each algorithm should implement the API defined by the
 * SolverLPAlgorithm data structure.  The algorithms should be able to
 * do MILP as well.  Feel free to add new algorithms.
 */
SolverLPAlgorithm lp_algorithm [] = {
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
