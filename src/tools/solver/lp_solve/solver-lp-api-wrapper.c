/*
 * solver-lp-api-wrapper.c:  The wrapper of LP Solve functions for the
 * Gnumeric Solver.
 *
 * Author:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
*/
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "numbers.h"

#include "parse-util.h"
#include "solver.h"
#include "lpkit.h"


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
        set_mat (lp, 0, col, value);
}

void
lp_solve_set_constr_mat (lprec *lp, int col, int row, gnum_float value)
{
        set_mat (lp, row + 1, col + 1, value);
}

void
lp_solve_set_constr_rhs (lprec *lp, int row, int value)
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
        return lp->best_solution[lp->rows + column];
}

gnum_float
lp_solve_get_value_of_obj_fn (lprec *lp)
{
        return lp->best_solution[0];
}

