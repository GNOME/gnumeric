/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SOLVER_H_
# define _GNM_SOLVER_H_

#include "gnumeric.h"
#include "numbers.h"
#include "dependent.h"
#include <gsf/gsf-libxml.h>


G_BEGIN_DECLS

#define SOLVER_MAX_TIME_ERR _("The maximum time exceeded. The optimal value could not be found in given time.")

/* -------------------------------------------------------------------------- */

typedef enum {
	SolverLPModel, SolverQPModel, SolverNLPModel
} SolverModelType;

typedef enum {
	LPSolve = 0, GLPKSimplex, QPDummy
} SolverAlgorithmType;

typedef struct {
	int                 max_time_sec;
	int                 max_iter;
	GnmSolverFactory   *algorithm;
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
} SolverOptions;

/* -------------------------------------------------------------------------- */

typedef enum {
        SolverMinimize, SolverMaximize, SolverEqualTo
} SolverProblemType;

struct _SolverParameters {
	SolverProblemType  problem_type;
	Sheet             *sheet;
	GnmDependent       target;
	GnmDependent       input;
	GSList             *constraints;
	int                n_constraints;
	int                n_variables;
	int                n_int_constraints;
	int                n_bool_constraints;
	int                n_total_constraints;
	SolverOptions      options;
};

/* Creates a new SolverParameters object. */
SolverParameters *gnm_solver_param_new (Sheet *sheet);

/* Duplicate a SolverParameters object. */
SolverParameters *gnm_solver_param_dup (SolverParameters const *src_param,
					Sheet *new_sheet);

/* Frees the memory resources in the solver parameter structure. */
void gnm_solver_param_free (SolverParameters *sp);

GnmValue const *gnm_solver_param_get_input (SolverParameters const *sp);
void gnm_solver_param_set_input (SolverParameters *sp, GnmValue *v);
GSList *gnm_solver_param_get_input_cells (SolverParameters const *sp);

const GnmCellRef *gnm_solver_param_get_target (SolverParameters const *sp);
void gnm_solver_param_set_target (SolverParameters *sp, GnmCellRef const *cr);
GnmCell *gnm_solver_param_get_target_cell (SolverParameters const *sp);

gboolean gnm_solver_param_valid (SolverParameters const *sp, GError **err);

/* -------------------------------------------------------------------------- */

typedef enum {
        SolverLE,
	SolverGE,
	SolverEQ,
	SolverINT,
	SolverBOOL
} SolverConstraintType;

typedef struct {
	SolverConstraintType type;

	/* Must be a range.  */
	GnmDependent lhs;

	/* Must be a constant or a range.  */
	GnmDependent rhs;
} SolverConstraint;

#ifdef GNM_ENABLE_SOLVER

SolverConstraint *gnm_solver_constraint_new (Sheet *sheet);
void gnm_solver_constraint_free (SolverConstraint *c);

void gnm_solver_constraint_set_old (SolverConstraint *c,
				    SolverConstraintType type,
				    int lhs_col, int lhs_row,
				    int rhs_col, int rhs_row,
				    int cols, int rows);

gboolean gnm_solver_constraint_has_rhs (SolverConstraint const *c);
gboolean gnm_solver_constraint_valid (SolverConstraint const *c,
				      SolverParameters const *sp);
gboolean gnm_solver_constraint_get_part (SolverConstraint const *c,
					 SolverParameters const *sp, int i,
					 GnmCell **lhs, gnm_float *cl,
					 GnmCell **rhs, gnm_float *cr);

GnmValue const *gnm_solver_constraint_get_lhs (SolverConstraint const *c);
GnmValue const *gnm_solver_constraint_get_rhs (SolverConstraint const *c);

void gnm_solver_constraint_set_lhs (SolverConstraint *c, GnmValue *v);
void gnm_solver_constraint_set_rhs (SolverConstraint *c, GnmValue *v);

void gnm_solver_constraint_side_as_str (SolverConstraint const *c,
					Sheet const *sheet,
					GString *buf, gboolean lhs);
char *gnm_solver_constraint_as_str (SolverConstraint const *c, Sheet *sheet);

#endif

/* -------------------------------------------------------------------------- */

#ifdef GNM_ENABLE_SOLVER

typedef enum {
	SolverRunning, SolverOptimal, SolverUnbounded, SolverInfeasible,
	SolverFailure, SolverMaxIterExc, SolverMaxTimeExc
} SolverStatus;

typedef enum {
        SolverOptAssumeNonNegative, SolverOptAutomaticScaling, SolverOptMaxIter,
	SolverOptMaxTimeSec
}  SolverOptionType;

typedef gpointer SolverProgram;


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
        GnmCell          **input_cells_array;
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

/* Creates the Solver's reports. */
gchar *          solver_reports        (WorkbookControl *wbc, Sheet *sheet,
					SolverResults *res,
					gboolean answer, gboolean sensitivity,
					gboolean limits, gboolean performance,
					gboolean program, gboolean dual);

/* Frees the data structure allocated for the results. */
void             solver_results_free   (SolverResults *res);

/* Returns a pointer to a input variable cell. */
GnmCell		*solver_get_input_var (SolverResults *res, int n);

/* Returns a pointer to a constraint. */
SolverConstraint* solver_get_constraint (SolverResults *res, int n);

void              solver_param_read_sax (GsfXMLIn *xin, xmlChar const **attrs);

#else /* !GNM_ENABLE_SOLVER */

#define gnm_solver_param_new() NULL
#define gnm_solver_param_dup(src_param, new_sheet) NULL
#define gnm_solver_param_free(param)		do {} while(0)
#define solver_insert_cols(sheet, col, count)	do {} while(0)
#define solver_insert_rows(sheet, row, count)	do {} while(0)
#define solver_delete_cols(sheet, col, count)	do {} while(0)
#define solver_delete_rows(sheet, row, count)	do {} while(0)
#define gnm_solver_constraint_free(c) do {} while(0)
#define solver_param_read_sax (void)

#endif

G_END_DECLS

#endif /* _GNM_SOLVER_H_ */
