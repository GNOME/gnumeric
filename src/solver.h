/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SOLVER_H_
# define _GNM_SOLVER_H_

#include "gnumeric.h"
#include "numbers.h"
#include <gsf/gsf-libxml.h>


G_BEGIN_DECLS

#define SOLVER_MAX_TIME_ERR _("The maximum time exceeded. The optimal value could not be found in given time.")

/* -------------------------------------------------------------------------- */

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
        GnmSolverParameters *param;
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

/* Returns a pointer to a input variable cell. */
GnmCell		*solver_get_input_var (SolverResults *res, int n);

G_END_DECLS

#endif /* _GNM_SOLVER_H_ */
