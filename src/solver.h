#ifndef GNUMERIC_SOLVER_H
#define GNUMERIC_SOLVER_H 1

#include "gnumeric.h"
#include "numbers.h"

#define SOLVER_LP_RUNNING        0
#define SOLVER_LP_OPTIMAL        1
#define SOLVER_LP_UNBOUNDED      2
#define SOLVER_LP_INFEASIBLE     3
#define SOLVER_LP_FAILURE        4
#define SOLVER_LP_MILP_FAIL      5
#define SOLVER_LP_INVALID_RHS    6
#define SOLVER_LP_INVALID_LHS    7


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
        LPSolve = 0
} SolverLPAlgorithmType;

typedef gpointer SolverProgram;


/*
 * Solver's API for LP solving algorithms
 */
typedef SolverProgram (solver_lp_init_fn)             (int n_vars,
						       int n_constraints);
typedef void          (solver_lp_remove_fn)           (SolverProgram p);
typedef void          (solver_lp_set_obj_fn)          (SolverProgram p,
						       int col, gnum_float v);
typedef void          (solver_lp_set_constr_mat_fn)   (SolverProgram p, int col,
						       int row, gnum_float v);
typedef void          (solver_lp_set_constr_type_fn)  (SolverProgram p, int row,
						       SolverConstraintType t);
typedef void          (solver_lp_set_constr_rhs_fn)   (SolverProgram p,
						       int row, gnum_float rhs);
typedef void          (solver_lp_set_maxim_fn)        (SolverProgram p);
typedef void          (solver_lp_set_minim_fn)        (SolverProgram p);
typedef void          (solver_lp_set_int_fn)          (SolverProgram p, int col,
						       gboolean must_be_int);
typedef int           (solver_lp_solve_fn)            (SolverProgram p);
typedef gnum_float    (solver_lp_get_obj_fn_value_fn) (SolverProgram p);
typedef gnum_float    (solver_lp_get_obj_fn_var_fn)   (SolverProgram p, int col);
typedef gnum_float    (solver_lp_get_shadow_prize_fn) (SolverProgram p, int row);


typedef struct {
        const char                    *name;
        solver_lp_init_fn             *init_fn;
        solver_lp_remove_fn           *remove_fn;
        solver_lp_set_obj_fn          *set_obj_fn;
        solver_lp_set_constr_mat_fn   *set_constr_mat_fn;
        solver_lp_set_constr_type_fn  *set_constr_type_fn;
        solver_lp_set_constr_rhs_fn   *set_constr_rhs_fn;
        solver_lp_set_maxim_fn        *maxim_fn;
        solver_lp_set_minim_fn        *minim_fn;
        solver_lp_set_int_fn          *set_int_fn;
        solver_lp_solve_fn            *solve_fn;
        solver_lp_get_obj_fn_value_fn *get_obj_fn_value_fn;
        solver_lp_get_obj_fn_var_fn   *get_obj_fn_var_fn;
        solver_lp_get_shadow_prize_fn *get_shadow_prize_fn;
} SolverLPAlgorithm;

struct _SolverOptions {
        int                   max_time_sec;
        int                   iterations;
        gnum_float            precision;
        gnum_float            tolerance;
        gnum_float            convergence;
        gnum_float            equal_to_value;
        gboolean              assume_linear_model;
        gboolean              assume_non_negative;
        gboolean              automatic_scaling;
        gboolean              show_iteration_results;
        SolverLPAlgorithmType algorithm;
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
        CellList           *input_cells;
        GSList             *constraints;
        char               *input_entry_str;
        int                n_constraints;
        int                n_variables;
        int                n_int_bool_constraints;
        SolverOptions      options;
        Cell               **input_cells_array;
        SolverConstraint   **constraints_array;
};

typedef struct {
        int              n_variables;
        int              n_constraints;
        int              n_nonzeros_in_mat;
        int              n_nonzeros_in_obj;
        gnum_float       time_user;
        gnum_float       time_system;
        gnum_float       time_real;
        gchar            **variable_names;
        gchar            **constraint_names;
        gnum_float       value_of_obj_fn;
        gnum_float       original_value_of_obj_fn;
        gnum_float       *optimal_values;
        gnum_float       *original_values;
        gnum_float       *shadow_prizes;
        int              status;
        gboolean         ilp_flag;   /* This is set if the problem has INT
				      * constraints.  Some reports cannot
				      * be created if there are any. */
        SolverParameters *param;
} SolverResults;


SolverResults    *solver               (WorkbookControl *wbc, Sheet *sheet,
					gchar **errmsg);
void             solver_lp_reports     (WorkbookControl *wbc, Sheet *sheet,
					SolverResults *res,
					gboolean answer, gboolean sensitivity, 
					gboolean limits, gboolean performance,
					gboolean program, gboolean dual);
char             *write_constraint_str (int lhs_col, int lhs_row,
					int rhs_col, int rhs_row,
					SolverConstraintType type,
					int cols, int rows);
SolverParameters *solver_param_new     (void);
SolverParameters *solver_lp_copy       (const SolverParameters *src_param,
					Sheet *new_sheet);
void             solver_param_destroy  (SolverParameters *);
void             solver_results_free   (SolverResults *res);

Cell             *get_solver_input_var (Sheet *sheet, int n);

#endif
