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
        GnmSolverConstraint **constraints_array;
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
GnmSolverConstraint* solver_get_constraint (SolverResults *res, int n);

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
