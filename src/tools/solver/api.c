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
#include <gnumeric-i18n.h>
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

#include "lp_solve/lpkit.h"
#include "api.h"
#include "glpk.h"

/* ------------------------------------------------------------------------- */


/*
 * The Solver provides an API for linear programming algorithms so that an
 * optimization algorithm can be plugged in with small possible effort.  To
 * integrate an algorithm you should just implement the following set of
 * functions that wrap the process of defining an LP program, setting up
 * various options, and fetching the results for reporting.
 *
 *
 * typedef SolverProgram (solver_init_fn)                (const SolverParameters *param);
 *   Initializes the program.
 *
 * typedef void          (solver_remove_fn)              (SolverProgram p);
 *   Frees all memory buffers previously allocated for the program.
 *
 * typedef void          (solver_lp_set_obj_fn)          (SolverProgram p,
 *					                  int col, gnm_float v);
 *   Sets a coefficent of the objective function.  The column numbering begins
 *   from zero.
 *
 * typedef void          (solver_lp_set_constr_mat_fn)   (SolverProgram p,
 *                                                        int col,
 *						          int row, gnm_float v);
 *   Sets a coefficent of a constraint.  The column and row numbering begins
 *   from zero.
 *
 * typedef void          (solver_lp_set_constr_fn)  (SolverProgram p,
 *                                                   int row, SolverConstraintType,
 *                                                   gnm_float rhs);
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
 *                                                        int col);
 *   Sets an integer constraint for a variable.  The column numbering
 *   begins from zero.
 *
 * typedef void          (solver_lp_set_bool_fn)         (SolverProgram p,
 *                                                        int col);
 *   Sets an boolean constraint for a variable.  The column numbering
 *   begins from zero.
 *
 * typedef SolverStatus  (solver_lp_solve_fn)            (SolverProgram p);
 *   Runs the solver to determine the optimal solution.
 *
 * typedef gnm_float    (solver_lp_get_obj_fn_value_fn) (SolverProgram p);
 *   Returns the final value of the objective function.  If the optimal value
 *   was not found the result is undetermined, otherwise the optimum value
 *   is returned.
 *
 * typedef gnm_float    (solver_lp_get_obj_fn_var_fn)   (SolverProgram p,
 *                                                        int col);
 *   Returns the value of a variable coeffient if the optimal value was found.
 *   The result is undetermined if the optimal value was not found.  The column
 *   numbering begins from zero.
 *
 * typedef gnm_float    (solver_lp_get_shadow_prize_fn) (SolverProgram p,
 *                                                        int row);
 *   Returns the shadow prize for a constraint.  If the optimal value was not
 *   found the result is undetermined.  The row numbering begins from zero.
 *
 * typedef void          (solver_lp_set_option_fn)       (SolverProgram p,
 *                                                        SolverOptionType option,
 *  					                  const gboolean *b_value,
 *                                                        const gnm_float *f_value,
 *                                                        const int *i_value);
 *   Sets an option for the solver algorithm.  `option' specifieces which option is
 *   to be set and `b_value', `f_value', and/or `i_value' passes the option value(s).
 *   Each one of those can point to NULL.
 *
 * typedef void          (solver_lp_printf_fn)           (SolverProgram p);
 *   Prints the program into the stdout.  Useful for debugging.
 *
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

typedef struct {
        lprec    *p;
        gboolean assume_non_negative;
} lp_solve_t;

static SolverProgram
w_lp_solve_init (const SolverParameters *param)
{
	lp_solve_t *lp;

	lp                      = g_new (lp_solve_t, 1);
	lp->assume_non_negative = param->options.assume_non_negative;
	if (lp->assume_non_negative)
	        lp->p = lp_solve_make_lp (param->n_constraints,
					  param->n_variables);
	else
	        lp->p = lp_solve_make_lp (param->n_constraints,
					  2 * param->n_variables);

	return lp;
}

static void
w_lp_solve_delete_lp (SolverProgram program)
{
	lp_solve_t *lp = (lp_solve_t *) program;

        lp_solve_delete_lp (lp->p);
	g_free (lp);
}

static void
w_lp_solve_set_maxim (SolverProgram program)
{
	lp_solve_t *lp = (lp_solve_t *) program;

        lp_solve_set_maxim (lp->p);
}

static void
w_lp_solve_set_minim (SolverProgram program)
{
	lp_solve_t *lp = (lp_solve_t *) program;

        lp_solve_set_minim (lp->p);
}

static void
w_lp_solve_set_obj_fn (SolverProgram program, int col, gnm_float value)
{
	lp_solve_t *lp = (lp_solve_t *) program;

	if (lp->assume_non_negative)
	        lp_solve_set_mat (lp->p, 0, col + 1, value);
	else {
	        lp_solve_set_mat (lp->p, 0, 2 * col + 1, value);
	        lp_solve_set_mat (lp->p, 0, 2 * col + 2, -value);
	}
}

static void
w_lp_solve_set_constr_mat (SolverProgram program, int col, int row,
			   gnm_float value)
{
	lp_solve_t *lp = (lp_solve_t *) program;

	if (lp->assume_non_negative)
	        lp_solve_set_mat (lp->p, row + 1, col + 1, value);
	else {
	        lp_solve_set_mat (lp->p, row + 1, 2 * col + 1, value);
	        lp_solve_set_mat (lp->p, row + 1, 2 * col + 2, -value);
	}
}

static void
w_lp_solve_set_constr (SolverProgram program, int row,
		       SolverConstraintType type, gnm_float rhs)
{
	lp_solve_t *lp = (lp_solve_t *) program;

        lp_solve_set_constr_type (lp->p, row + 1, type);
        lp_solve_set_rh (lp->p, row + 1, rhs);
}

static void
w_lp_solve_set_int (SolverProgram program, int col)
{
	lp_solve_t *lp = (lp_solve_t *) program;

	if (lp->assume_non_negative) 
	        lp_solve_set_int (lp->p, col + 1, TRUE);
	else {
	        lp_solve_set_int (lp->p, 2 * col + 1, TRUE);
	        lp_solve_set_int (lp->p, 2 * col + 2, TRUE);
	}
}

static void
w_lp_solve_set_bool (SolverProgram program, int col)
{
	lp_solve_t *lp = (lp_solve_t *) program;

	if (lp->assume_non_negative) {
	        lp_solve_set_int   (lp->p, col + 1, TRUE);
		lp_solve_set_upbo  (lp->p, col + 1, 1);
		lp_solve_set_lowbo (lp->p, col + 1, 0);
	} else {
	        lp_solve_set_int   (lp->p, 2 * col + 1, TRUE);
		lp_solve_set_upbo  (lp->p, 2 * col + 1, 1);
		lp_solve_set_lowbo (lp->p, 2 * col + 1, 0);

	        lp_solve_set_int   (lp->p, 2 * col + 2, TRUE);
		lp_solve_set_upbo  (lp->p, 2 * col + 2, 0);
		lp_solve_set_lowbo (lp->p, 2 * col + 2, 0);
	}
}

static void
w_lp_solve_print_lp (SolverProgram program)
{
	lp_solve_t *lp = (lp_solve_t *) program;

	lp_solve_print_lp (lp->p);
}

static SolverStatus
w_lp_solve_solve (SolverProgram program)
{
	lp_solve_t *lp = (lp_solve_t *) program;

#if SOLVER_DEBUG
	w_lp_solve_print_lp (program);
#endif
        return lp_solve_solve (lp->p);
}

static gnm_float
w_lp_solve_get_solution (SolverProgram program, int column)
{
	lp_solve_t *lp = (lp_solve_t *) program;

	if (lp->assume_non_negative)
	        return lp->p->best_solution [lp->p->rows + column + 1];
	else {
	        gnm_float x, neg_x;

	        x     = lp->p->best_solution [lp->p->rows + 2 * column + 1];
		neg_x = lp->p->best_solution [lp->p->rows + 2 * column + 2];
		if (x > neg_x)
		        return x;
		else
		        return -neg_x;
	}
}

static gnm_float
w_lp_solve_get_value_of_obj_fn (SolverProgram program)
{
	lp_solve_t *lp = (lp_solve_t *) program;

        return lp->p->best_solution [0];
}

static gnm_float
w_lp_solve_get_dual (SolverProgram program, int row)
{
	lp_solve_t *lp = (lp_solve_t *) program;

        return lp->p->duals [row + 1];
}

static int
w_lp_solve_get_iterations (SolverProgram program)
{
	lp_solve_t *lp = (lp_solve_t *) program;

        return lp->p->total_iter;
}

static gboolean
w_lp_solve_set_option (SolverProgram program, SolverOptionType option,
		       const gboolean *b_value,
		       const gnm_float *f_value, const int *i_value)
{
	lp_solve_t *lp = (lp_solve_t *) program;

        switch (option) {
	case SolverOptAutomaticScaling:
	        if (*b_value)
		        lp_solve_auto_scale (lp->p);
	        return FALSE;
	case SolverOptMaxIter:
	        lp_solve_set_max_iter (lp->p, *i_value);
	        return FALSE;
	case SolverOptMaxTimeSec:
	        lp_solve_set_max_time (lp->p, *i_value, *f_value);
	        return FALSE;
	default:
	        return TRUE;
	}
}


/* ------------------------------------------------------------------------- */

/*
 * Solver's API wrappings for the GLPK 3.2.
 *
 * Package:    GLPK
 * Version:    3.2 (Jul 15, 2002)
 * License:    GPL
 * Homepage:   http://www.gnu.org/software/glpk/glpk.html
 * Algorithm:  revised simplex method
 *
 */


typedef struct {
        LPX         *p;
	int         *rn;
	int         *cn;
	gnm_float  *a;
	int         n;
        gboolean    assume_non_negative;
        gboolean    scaling;
} glpk_simplex_t;

static SolverProgram
w_glpk_init (const SolverParameters *param)
{
        glpk_simplex_t *lp;
	int             i, cols;

	lp                      = g_new (glpk_simplex_t, 1);
	lp->p                   = lpx_create_prob ();
	lp->assume_non_negative = param->options.assume_non_negative;
	lp->scaling             = param->options.automatic_scaling;

	cols = param->n_variables;

	lpx_add_cols (lp->p, cols);
	lpx_add_rows (lp->p, param->n_constraints);
	lp->a  = g_new (gnm_float, cols * param->n_constraints + 1); 
	lp->cn = g_new (int, cols * param->n_constraints + 1); 
	lp->rn = g_new (int, cols * param->n_constraints + 1); 
	lp->n  = 1;

	if (lp->assume_non_negative)
		for (i = 0; i < cols; i++)
			lpx_set_col_bnds (lp->p, i + 1, LPX_LO, 0, 0);
	else
		for (i = 0; i < cols; i++)
			lpx_set_col_bnds (lp->p, i + 1, LPX_FR, 0, 0);

	return lp;
}

static void
w_glpk_delete_lp (SolverProgram program)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;

        lpx_delete_prob (lp->p);
	g_free (lp->cn);
	g_free (lp->rn);
	g_free (lp->a);
	g_free (lp);
}

static void
w_glpk_set_maxim (SolverProgram program)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;

        lpx_set_obj_dir (lp->p, LPX_MAX);
}

static void
w_glpk_set_minim (SolverProgram program)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;

        lpx_set_obj_dir (lp->p, LPX_MIN);
}

static void
w_glpk_set_obj_fn (SolverProgram program, int col, gnm_float value)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;

	lpx_set_col_coef (lp->p, col + 1, value);
}

static void
w_glpk_set_constr_mat (SolverProgram program, int col, int row,
		       gnm_float value)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;

	lp->cn [lp->n] = col + 1;
	lp->rn [lp->n] = row + 1;
	lp->a  [lp->n] = value;
	(lp->n)++;
}

static void
w_glpk_set_constr (SolverProgram program, int row, SolverConstraintType type,
		   gnm_float value)
{
        int            typemap [] = { LPX_UP, LPX_LO, LPX_FX, -1, -1, -1 };
        glpk_simplex_t *lp        = (glpk_simplex_t *) program;

	if (typemap [type] != -1)
	        lpx_set_row_bnds (lp->p, row + 1, typemap [type], value,
				  value);
	else
	        printf ("Error\n");
}

static void
w_glpk_set_int (SolverProgram program, int col)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;

	lpx_set_class (lp->p, LPX_MIP);
	lpx_set_col_kind (lp->p, col + 1, LPX_IV);
}

static void
w_glpk_set_bool (SolverProgram program, int col)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;

	lpx_set_class (lp->p, LPX_MIP);
	lpx_set_col_kind (lp->p, col + 1, LPX_IV);
	lpx_set_col_bnds (lp->p, col + 1, LPX_DB, 0, 1);
}

static void
w_glpk_print_lp (SolverProgram program)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;
        int            i, n, cols, rows;
	int            typex;
	gnm_float     lb, ub;

	cols = lpx_get_num_cols (lp->p);
	rows = lpx_get_num_rows (lp->p);

	printf ("\t\t");
	for (i = 0; i < cols; i++)
	        printf ("Var[%3d] ", i + 1);
	printf ("\n");

	if (lpx_get_obj_dir (lp->p) == LPX_MAX)
	        printf ("Maximize\t");
	else
	        printf ("Minimize\t");
	for (i = 0; i < cols; i++)
	        printf ("%8g ", lpx_get_col_coef (lp->p, i + 1));
	printf ("\n");

	for (i = 0; i < rows; i++) {
		gnm_float *a;
		int        *ndx, t;

	        printf ("Row[%3d]\t", i + 1);

		a   = g_new (gnm_float, cols + 1);
		ndx = g_new (int, cols + 1);
		lpx_get_mat_row (lp->p, i + 1, ndx, a);
		for (n = 0, t = 1; n < cols; n++) {
			if (ndx [t] == n + 1)
				printf ("%8g ", a[t++]);
			else
				printf ("%8g ", 0.0);
		}
		g_free (ndx);
		g_free (a);
		
		lpx_get_row_bnds (lp->p, i + 1, &typex, &lb, &ub);
		if (typex == LPX_LO)
		        printf (">= %8g\n", lb);
		else if (typex == LPX_UP)
		        printf ("<= %8g\n", ub);
		else
		        printf ("=  %8g\n", lb);
	}

	printf ("Type\t\t");
	for (i = 0; i < cols; i++)
		if (lpx_get_class (lp->p) == LPX_LP 
		    || lpx_get_col_kind (lp->p, i + 1) == LPX_CV)
			printf ("  Real\t");
		else
			printf ("  Int\t");

	printf ("\nupbo\t\t");
	for (i = 0; i < cols; i++) {
		lpx_get_col_bnds (lp->p, i + 1, &typex, &lb, &ub);
		if (typex == LPX_LO || typex == LPX_FR)
			printf ("Infinite  ");
		else
			printf ("%8g ", ub);
	}

	printf ("\nlowbo\t\t");
	for (i = 0; i < cols; i++) {
		lpx_get_col_bnds (lp->p, i + 1, &typex, &lb, &ub);
		if (typex == LPX_UP || typex == LPX_FR)
			printf ("-Infinite ");
		else
			printf ("%8g ", ub);
	}
	printf ("\n");
}

static SolverStatus
w_glpk_simplex_solve (SolverProgram program)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;
	
	if (lp->scaling)
	        lpx_scale_prob (lp->p);

	lpx_load_mat3 (lp->p, lp->n - 1, lp->rn, lp->cn, lp->a);

#if SOLVER_DEBUG
	w_glpk_print_lp (program);
#endif

	lpx_simplex (lp->p);
	if (lpx_get_class (lp->p) == LPX_MIP) {
		switch (lpx_get_status (lp->p)) {
		case LPX_OPT:
			break;
		case LPX_INFEAS:
			return SolverInfeasible;
		case LPX_UNBND:
			return SolverUnbounded;
		default:
			printf ("Error: w_glpk_simplex_solve\n");
			return SolverInfeasible;
		}

		lpx_integer (lp->p);
		switch (lpx_get_mip_stat (lp->p)) {
		case LPX_I_OPT:
			return SolverOptimal;
		case LPX_I_NOFEAS:
			return SolverInfeasible;
		default:
			printf ("Error: w_glpk_simplex_solve\n");
			return SolverInfeasible;
		}
	} else {
		if (lp->scaling)
			lpx_unscale_prob (lp->p);

		switch (lpx_get_status (lp->p)) {
		case LPX_OPT:
			return SolverOptimal;
		case LPX_INFEAS:
			return SolverInfeasible;
		case LPX_UNBND:
			return SolverUnbounded;
		default:
			printf ("Error: w_glpk_simplex_solve\n");
			return SolverInfeasible;
		}
	}
}

static gnm_float
w_glpk_get_solution (SolverProgram program, int col)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;
        gnm_float      x;

        if (lpx_get_class (lp->p) == LPX_LP)
		lpx_get_col_info (lp->p, col + 1, NULL, &x, NULL);
	else
		x = lpx_get_mip_col (lp->p, col + 1);

	return x;
}

static gnm_float
w_glpk_get_value_of_obj_fn (SolverProgram program)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;

        if (lpx_get_class (lp->p) == LPX_LP)
		return lpx_get_obj_val (lp->p);
	else
		return lpx_get_mip_obj (lp->p);
}

static gnm_float
w_glpk_get_dual (SolverProgram program, int row)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;
        gnm_float      x;

        lpx_get_row_info (lp->p, row + 1, NULL, NULL, &x);
	return x;
}

static int
w_glpk_get_iterations (SolverProgram program)
{
        return -1;
}

static gboolean
w_glpk_set_option (SolverProgram program, SolverOptionType option,
		   const gboolean *b_value,
		   const gnm_float *f_value, const int *i_value)
{
        glpk_simplex_t *lp = (glpk_simplex_t *) program;

        switch (option) {
	case SolverOptAutomaticScaling:
	        return FALSE;
	case SolverOptMaxIter:
printf ("FIXME: Max iter=%d\n", *i_value);
	        return FALSE;
	case SolverOptMaxTimeSec:
printf ("FIXME: Max time (sec.)=%d\n", *i_value);
	        return FALSE;
	default:
	        return TRUE;
	}
}


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
        printf ("w_qp_dummy_set_obj_fn %d, %" GNUM_FORMAT_g "\n", col, value);
}

static void
w_qp_dummy_set_constr_mat (SolverProgram program, int col, int row,
			   gnm_float value)
{
        printf ("w_qp_dummy_set_constr_mat %d, %d, %" GNUM_FORMAT_g "\n",
		col, row, value);
}

static void
w_qp_dummy_set_constr (SolverProgram program, int row,
		       SolverConstraintType type,
		       gnm_float value)
{
        printf ("w_qp_dummy_set_constr %d, %d, %" GNUM_FORMAT_g "\n",
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
		(solver_init_fn*)                w_lp_solve_init,
		(solver_remove_fn*)              w_lp_solve_delete_lp,
		(solver_lp_set_obj_fn*)          w_lp_solve_set_obj_fn,
		(solver_lp_set_constr_mat_fn*)   w_lp_solve_set_constr_mat,
		(solver_lp_set_constr_fn*)       w_lp_solve_set_constr,
		(solver_lp_set_maxim_fn*)        w_lp_solve_set_maxim,
		(solver_lp_set_minim_fn*)        w_lp_solve_set_minim,
		(solver_lp_set_int_fn*)          w_lp_solve_set_int,
		(solver_lp_set_bool_fn*)         w_lp_solve_set_bool,
		(solver_lp_solve_fn*)            w_lp_solve_solve,
		(solver_lp_get_obj_fn_value_fn*) w_lp_solve_get_value_of_obj_fn,
		(solver_lp_get_obj_fn_var_fn*)   w_lp_solve_get_solution,
		(solver_lp_get_shadow_prize_fn*) w_lp_solve_get_dual,
		(solver_lp_get_iterations_fn*)   w_lp_solve_get_iterations,
		(solver_lp_set_option_fn*)       w_lp_solve_set_option,
		(solver_lp_print_fn*)            w_lp_solve_print_lp
	},

#if __HAVE_GLPK__
        {
	        NULL,
		(solver_init_fn*)                w_glpk_init,
		(solver_remove_fn*)              w_glpk_delete_lp,
		(solver_lp_set_obj_fn*)          w_glpk_set_obj_fn,
		(solver_lp_set_constr_mat_fn*)   w_glpk_set_constr_mat,
		(solver_lp_set_constr_fn*)       w_glpk_set_constr,
		(solver_lp_set_maxim_fn*)        w_glpk_set_maxim,
		(solver_lp_set_minim_fn*)        w_glpk_set_minim,
		(solver_lp_set_int_fn*)          w_glpk_set_int,
		(solver_lp_set_bool_fn*)         w_glpk_set_bool,
		(solver_lp_solve_fn*)            w_glpk_simplex_solve,
		(solver_lp_get_obj_fn_value_fn*) w_glpk_get_value_of_obj_fn,
		(solver_lp_get_obj_fn_var_fn*)   w_glpk_get_solution,
		(solver_lp_get_shadow_prize_fn*) w_glpk_get_dual,
		(solver_lp_get_iterations_fn*)   w_glpk_get_iterations,
		(solver_lp_set_option_fn*)       w_glpk_set_option,
		(solver_lp_print_fn*)            w_glpk_print_lp
	},
#endif

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
