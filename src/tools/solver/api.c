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
 * typedef void          (solver_lp_set_constr_fn)  (SolverProgram p,
 *                                                   int row, SolverConstraintType,
 *                                                   gnum_float rhs);
 *   Sets the type and the right hand side value of a constraint.  The row numbering 
 *   begins from zero.
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
 * typedef SolverStatus  (solver_lp_solve_fn)            (SolverProgram p);
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
 * typedef void          (solver_lp_set_option_fn)       (SolverProgram p,
 *                                                        SolverOptionType option,
 *  					                  const gboolean *b_value,
 *                                                        const gnum_float *f_value,
 *                                                        const int *i_value);
 *   Sets an option for the solver algorithm.  `option' specifieces which option is
 *   to be set and `b_value', `f_value', and/or `i_value' passes the option value(s).
 *   Each one of those can point to NULL.
*/

/* ------------------------------------------------------------------------- */

/*
 * Solver's API wrappings for the LP Solve 3.2.
 *
 * Package:    LP Solve
 * Version:    3.2
 * License:    LGPL
 * Homepage:   
 */

SolverProgram
w_lp_solve_init (int n_vars, int n_constraints)
{
        return lp_solve_make_lp (n_constraints, n_vars);
}

void
w_lp_solve_delete_lp (SolverProgram lp)
{
        lp_solve_delete_lp (lp);
}

void
w_lp_solve_set_maxim (SolverProgram lp)
{
        lp_solve_set_maxim (lp);
}

void
w_lp_solve_set_minim (SolverProgram lp)
{
        lp_solve_set_minim (lp);
}

void
w_lp_solve_set_obj_fn (SolverProgram lp, int col, gnum_float value)
{
        lp_solve_set_mat (lp, 0, col + 1, value);
}

void
w_lp_solve_set_constr_mat (SolverProgram lp, int col, int row, gnum_float value)
{
        lp_solve_set_mat (lp, row + 1, col + 1, value);
}

void
w_lp_solve_set_constr (SolverProgram lp, int row, SolverConstraintType type,
		       gnum_float rhs)
{
        lp_solve_set_constr_type (lp, row + 1, type);
        lp_solve_set_rh (lp, row + 1, rhs);
}

void
w_lp_solve_set_int (SolverProgram lp, int col, gboolean must_be_int)
{
	lp_solve_set_int (lp, col, must_be_int);
}

SolverStatus
w_lp_solve_solve (SolverProgram lp)
{
        return lp_solve_solve (lp);
}

gnum_float
w_lp_solve_get_solution (SolverProgram lp, int column)
{
        lprec *p = (lprec *) lp;

        return p->best_solution [p->rows + column];
}

gnum_float
w_lp_solve_get_value_of_obj_fn (SolverProgram lp)
{
        lprec *p = (lprec *) lp;

        return p->best_solution [0];
}

gnum_float
w_lp_solve_get_dual (SolverProgram lp, int row)
{
        lprec *p = (lprec *) lp;

        return p->duals [row + 1];
}

gboolean
w_lp_solve_set_option (SolverProgram lp, SolverOptionType option,
		       const gboolean *b_value,
		       const gnum_float *f_value, const int *i_value)
{
	lprec *p = (lprec *) lp;
        int   i;

        switch (option) {
	case SolverOptAssumeNonNegative:
	        if (! *b_value)
		        for (i = 0; i < p->columns; i++)
			        lp_solve_set_lowbo (p, i + 1, -p->infinite);
	        return FALSE;
	case SolverOptAutomaticScaling:
	        if (*b_value)
		        lp_solve_auto_scale (p);
	        return FALSE;
	case SolverOptMaxIter:
printf("FIXME: Max iter=%d\n", *i_value);
	        return FALSE;
	case SolverOptMaxTimeSec:
printf("FIXME: Max time (sec.)=%d\n", *i_value);
	        return FALSE;
	default:
	        return TRUE;
	}
}


/* ------------------------------------------------------------------------- */

#if __HAVE_GLPK__

/*
 * Solver's API wrappings for the GLPK 3.0.5.
 *
 * Package:    GLPK
 * Version:    3.0.5 (Jan 29, 2002)
 * License:    GPL
 * Homepage:   http://www.gnu.org/software/glpk/glpk.html
 * Algorithm:  revised simplex method
 *
 */

#include "glpk.h"

typedef struct {
        LPI         *p;
        struct spx2 *param;
} glpk_simplex2_t;

SolverProgram
w_glpk_init (int n_vars, int n_constraints)
{
        glpk_simplex2_t *lp;
	int             i;
	GString         *str;

	lp        = g_new (glpk_simplex2_t, 1);
	lp->p     = glp_create_prob ("p");
	lp->param = g_new (struct spx2, 1);

	glp_init_spx2 (lp->param);

	for (i = 0; i < n_vars; i++) {
	        str = g_string_new ("");
		g_string_sprintfa (str, "X%d", i);
		glp_new_col (lp->p, str->str);
		g_string_free (str, FALSE);
	}

	for (i = 0; i < n_constraints; i++) {
	        str = g_string_new ("");
		g_string_sprintfa (str, "C%d", i);
		glp_new_row (lp->p, str->str);
		g_string_free (str, FALSE);
	}

	return lp;
}

void
w_glpk_delete_lp (SolverProgram program)
{
        glpk_simplex2_t *lp = (glpk_simplex2_t *) program;

        glp_delete_prob (lp->p);
	g_free (lp->param);
	g_free (lp);
}

void
w_glpk_set_maxim (SolverProgram program)
{
        glpk_simplex2_t *lp = (glpk_simplex2_t *) program;

        glp_set_obj_sense(lp->p, '+');
}

void
w_glpk_set_minim (SolverProgram program)
{
        glpk_simplex2_t *lp = (glpk_simplex2_t *) program;

        glp_set_obj_sense(lp->p, '-');
}

void
w_glpk_set_obj_fn (SolverProgram program, int col, gnum_float value)
{
        glpk_simplex2_t *lp = (glpk_simplex2_t *) program;

        glp_set_obj_coef (lp->p, col + 1, value);
}

void
w_glpk_set_constr_mat (SolverProgram program, int col, int row, gnum_float value)
{
        glpk_simplex2_t *lp = (glpk_simplex2_t *) program;

        glp_new_aij (lp->p, row + 1, col + 1, value);
}

void
w_glpk_set_constr (SolverProgram program, int row, SolverConstraintType type,
		   gnum_float value)
{
        int             typemap [] = { 'U', 'L', 'S', -1, -1, -1 };
        glpk_simplex2_t *lp        = (glpk_simplex2_t *) program;

	if (typemap [type] == -1)
	        printf("Error\n");
	else
	        glp_set_row_bnds (lp->p, row + 1, typemap [type], value, value);
}

void
w_glpk_set_int (SolverProgram program, int col, gboolean must_be_int)
{
        glpk_simplex2_t *lp = (glpk_simplex2_t *) program;

        if (must_be_int)
	        glp_set_col_kind (lp->p, col + 1, 'I');
	else
	        glp_set_col_kind (lp->p, col + 1, 'C');
}

SolverStatus
w_glpk_simplex2_solve (SolverProgram program)
{
        glpk_simplex2_t *lp = (glpk_simplex2_t *) program;

        glp_simplex2 (lp->p, lp->param);
	switch (glp_get_status (lp->p)) {
	case GLP_OPT:
	        return SolverOptimal;
	case GLP_INFEAS:
	        return SolverInfeasible;
	case GLP_UNBND:
	        return SolverUnbounded;
	default:
	        printf ("Error: w_glpk_simplex2_solve\n");
	        return SolverInfeasible;
	}
}

gnum_float
w_glpk_get_solution (SolverProgram program, int column)
{
        glpk_simplex2_t *lp = (glpk_simplex2_t *) program;
        double          x;

	glp_get_col_soln (lp->p, column, NULL, &x, NULL);
	return x;
}

gnum_float
w_glpk_get_value_of_obj_fn (SolverProgram program)
{
        glpk_simplex2_t *lp = (glpk_simplex2_t *) program;

        return glp_get_obj_val (lp->p);
}

gnum_float
w_glpk_get_dual (SolverProgram program, int row)
{
        glpk_simplex2_t *lp = (glpk_simplex2_t *) program;
        double          x;

        glp_get_row_soln (lp->p, row + 1, NULL, NULL, &x);
	return x;
}

gboolean
w_glpk_set_option (SolverProgram program, SolverOptionType option,
		   const gboolean *b_value,
		   const gnum_float *f_value, const int *i_value)
{
        glpk_simplex2_t *lp = (glpk_simplex2_t *) program;

        switch (option) {
	case SolverOptAssumeNonNegative:
printf("FIXME: assume_non_negative=%d\n", *b_value);
	        return FALSE;
	case SolverOptAutomaticScaling:
	        lp->param->scale = *b_value;
	        return FALSE;
	case SolverOptMaxIter:
printf("FIXME: Max iter=%d\n", *i_value);
	        return FALSE;
	case SolverOptMaxTimeSec:
printf("FIXME: Max time (sec.)=%d\n", *i_value);
	        return FALSE;
	default:
	        return TRUE;
	}
}

#endif


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
		(solver_lp_init_fn*)             w_lp_solve_init,
		(solver_lp_remove_fn*)           w_lp_solve_delete_lp,
		(solver_lp_set_obj_fn*)          w_lp_solve_set_obj_fn,
		(solver_lp_set_constr_mat_fn*)   w_lp_solve_set_constr_mat,
		(solver_lp_set_constr_fn*)       w_lp_solve_set_constr,
		(solver_lp_set_maxim_fn*)        w_lp_solve_set_maxim,
		(solver_lp_set_minim_fn*)        w_lp_solve_set_minim,
		(solver_lp_set_int_fn*)          w_lp_solve_set_int,
		(solver_lp_solve_fn*)            w_lp_solve_solve,
		(solver_lp_get_obj_fn_value_fn*) w_lp_solve_get_value_of_obj_fn,
		(solver_lp_get_obj_fn_var_fn*)   w_lp_solve_get_solution,
		(solver_lp_get_shadow_prize_fn*) w_lp_solve_get_dual,
		(solver_lp_set_option_fn*)       w_lp_solve_set_option
	},

#if __HAVE_GLPK__
        {
	        NULL,
		(solver_lp_init_fn*)             w_glpk_init,
		(solver_lp_remove_fn*)           w_glpk_delete_lp,
		(solver_lp_set_obj_fn*)          w_glpk_set_obj_fn,
		(solver_lp_set_constr_mat_fn*)   w_glpk_set_constr_mat,
		(solver_lp_set_constr_fn*)       w_glpk_set_constr,
		(solver_lp_set_maxim_fn*)        w_glpk_set_maxim,
		(solver_lp_set_minim_fn*)        w_glpk_set_minim,
		(solver_lp_set_int_fn*)          w_glpk_set_int,
		(solver_lp_solve_fn*)            w_glpk_simplex2_solve,
		(solver_lp_get_obj_fn_value_fn*) w_glpk_get_value_of_obj_fn,
		(solver_lp_get_obj_fn_var_fn*)   w_glpk_get_solution,
		(solver_lp_get_shadow_prize_fn*) w_glpk_get_dual,
		(solver_lp_set_option_fn*)       w_glpk_set_option
	},
#endif

	{ NULL }
};
