/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * solver.h: Solver's APIs and data structures.
 *
 * Author:
 *  Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * (C) Copyright 2000, 2002 by Jukka-Pekka Iivonen <iivonen@iki.fi>
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
#ifndef GNUMERIC_SOLVER_H
#define GNUMERIC_SOLVER_H 1

#include "gnumeric.h"
#include "numbers.h"


typedef enum {
        SolverRunning, SolverOptimal, SolverUnbounded, SolverInfeasible,
	SolverFailure, SolverMilpFailure, SolverMaxIterExc, SolverMaxTimeExc
} SolverStatus;

/* Forward references for structures.  */
typedef struct _SolverOptions SolverOptions;
typedef struct _SolverConstraint SolverConstraint;

typedef enum {
        SolverMinimize, SolverMaximize, SolverEqualTo
} SolverProblemType;

typedef enum {
        SolverLE, SolverGE, SolverEQ, SolverINT, SolverBOOL, SolverOF
} SolverConstraintType;

typedef enum {
        LPSolve = 0, GLPKSimplex, QPDummy
} SolverAlgorithmType;

typedef enum {
        SolverOptAssumeNonNegative, SolverOptAutomaticScaling, SolverOptMaxIter,
	SolverOptMaxTimeSec
}  SolverOptionType;

typedef gpointer SolverProgram;


/*
 * Solver's API for LP solving algorithms
 */
typedef SolverProgram
        (solver_init_fn)                (const SolverParameters *param);
typedef void
        (solver_remove_fn)              (SolverProgram p);
typedef void
        (solver_lp_set_obj_fn)          (SolverProgram p, int col, gnm_float v);
typedef void
        (solver_lp_set_constr_mat_fn)   (SolverProgram p, int col, int row,
					 gnm_float v);
typedef void
        (solver_lp_set_constr_fn)       (SolverProgram p, int row,
					 SolverConstraintType t, gnm_float rhs);
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
        const char                    *name;
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

typedef enum {
        SolverLPModel, SolverQPModel, SolverNLPModel
} SolverModelType;

struct _SolverOptions {
        int                 max_time_sec;
        int                 max_iter;
        SolverModelType     model_type;
        gboolean            assume_non_negative;
        gboolean            assume_discrete;
        gboolean            automatic_scaling;
        gboolean            show_iter_results;
        gboolean            answer_report;
        gboolean            sensitivity_report;
        gboolean            limits_report;
        gboolean            performance_report;
        gboolean            program_report;
        gboolean            dual_program_report;
	gboolean            add_scenario;
	gchar               *scenario_name;
        SolverAlgorithmType algorithm;
};

struct _SolverConstraint {
        CellPos              lhs;		/* left hand side */
        CellPos              rhs;  		/* right hand side */
        gint                 rows;              /* number of rows */
        gint                 cols;              /* number of columns */
        SolverConstraintType type;	        /* <=, =, >=, int, bool */
        char                 *str;		/* the same in string form */
};

struct _SolverParameters {
        SolverProblemType  problem_type;
        Cell               *target_cell;
        GSList		   *input_cells;
        GSList             *constraints;
        char               *input_entry_str;
        int                n_constraints;
        int                n_variables;
        int                n_int_constraints;
        int                n_bool_constraints;
        int                n_total_constraints;
        SolverOptions      options;
};

typedef struct {
        gnm_float lower_limit;
        gnm_float lower_result;
        gnm_float upper_limit;
        gnm_float upper_result;
} SolverLimits;

typedef struct {
        int              n_variables;
        int              n_constraints;
        int              n_nonzeros_in_mat;
        int              n_nonzeros_in_obj;
	int              n_iterations;
        gnm_float       time_user;
        gnm_float       time_system;
        gnm_float       time_real;
        gchar            *target_name;
        gchar            **variable_names;
        gchar            **constraint_names;
        gnm_float       value_of_obj_fn;
        gnm_float       original_value_of_obj_fn;
        gnm_float       *optimal_values;
        gnm_float       *original_values;
        gnm_float       *shadow_prizes;
        gnm_float       *slack;
        gnm_float       *lhs;
        gnm_float       *rhs;
        gnm_float       *constr_allowable_increase;
        gnm_float       *constr_allowable_decrease;
        SolverStatus     status;
        gboolean         ilp_flag;   /* This is set if the problem has INT
				      * constraints.  Some reports cannot
				      * be created if there are any. */
        Cell             **input_cells_array;
        SolverConstraint **constraints_array;
        gnm_float       *obj_coeff;
        gnm_float       **constr_coeff;
        SolverLimits     *limits;
        SolverParameters *param;
} SolverResults;


/**************************************************************************
 *
 * The API functions to the Solver tool.
 */

/* Runs the solver.  Returs a pointer to a data structure containing
 * the results of the run.  Note that it should be freed when no longer
 * needed (by calling solver_results_free). */
SolverResults    *solver               (WorkbookControl *wbc, Sheet *sheet,
					gchar **errmsg);

/* Creates the Solver's reports. */
gchar *          solver_reports        (WorkbookControl *wbc, Sheet *sheet,
					SolverResults *res,
					gboolean answer, gboolean sensitivity, 
					gboolean limits, gboolean performance,
					gboolean program, gboolean dual);

char             *write_constraint_str (int lhs_col, int lhs_row,
					int rhs_col, int rhs_row,
					SolverConstraintType type,
					int cols, int rows);

/* Initializes the Solver's data structure containing the parameters.
 * Each sheet can currently have one copy of this data structure. */
SolverParameters *solver_param_new     (void);

/* Frees the memory resources in the solver parameter structure. */
void             solver_param_destroy  (SolverParameters *);

/* Creates a copy of the Solver's data structures when a sheet is
 * duplicated. */
SolverParameters *solver_lp_copy       (const SolverParameters *src_param,
					Sheet *new_sheet);

/* Frees the data structure allocated for the results. */
void             solver_results_free   (SolverResults *res);

/* Returns a pointer to the target cell of the model attached to the
 * given sheet. */
Cell*            solver_get_target_cell (Sheet *sheet);

/* Returns a pointer to a input variable cell. */
Cell*            solver_get_input_var (SolverResults *res, int n);

/* Retruns a pointer to a constraint. */
SolverConstraint* solver_get_constraint (SolverResults *res, int n);

void              solver_insert_cols    (Sheet *sheet, int col, int count);
void              solver_insert_rows    (Sheet *sheet, int row, int count);
void              solver_delete_rows    (Sheet *sheet, int row, int count);
void              solver_delete_cols    (Sheet *sheet, int row, int count);

#endif
