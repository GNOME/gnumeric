#ifndef GNUMERIC_SOLVER_H
#define GNUMERIC_SOLVER_H 1

#include "numbers.h"

#define SOLVER_LP_RUNNING        0
#define SOLVER_LP_OPTIMAL        1
#define SOLVER_LP_UNBOUNDED      2
#define SOLVER_LP_INFEASIBLE     3
#define SOLVER_LP_INVALID_RHS    4
#define SOLVER_LP_INVALID_LHS    5


/* Forward references for structures.  */
typedef struct _SolverOptions SolverOptions;
typedef struct _SolverConstraint SolverConstraint;
typedef struct _SolverParameters SolverParameters;

typedef enum {
        SolverMinimize, SolverMaximize, SolverEqualTo
} SolverProblemType;

struct _SolverOptions {
        int                max_time_sec;
        int                iterations;
        float_t            precision;
        float_t            tolerance;
        float_t            convergence;
        float_t            equal_to_value;
        gboolean           assume_linear_model;
        gboolean           assume_non_negative;
        gboolean           automatic_scaling;
        gboolean           show_iteration_results;
};

struct _SolverConstraint {
        gint               lhs_col, lhs_row;  /* left hand side */
        gint               rhs_col, rhs_row;  /* right hand side */
        gint               rows, cols;        /* number of rows and columns */
        gchar              *type;             /* <=, =, >=, int, bool */
        char               *str;              /* the same in string form */
};

struct _SolverParameters {
        SolverProblemType  problem_type;
        Cell               *target_cell;
        CellList           *input_cells;
        GSList             *constraints;
        char               *input_entry_str;
        SolverOptions      options;
};

int  solver_simplex (Workbook *wb, Sheet *sheet, float_t **init_table,
		     float_t **final_table);

int solver_affine_scaling (Workbook *wb, Sheet *sheet,
			   float_t **x,    /* the optimal solution */
			   float_t **sh_pr /* the shadow prizes */);

gboolean solver_lp (Workbook *wb, Sheet *sheet, float_t **opt_x,
		    float_t **sh_pr, gboolean *ilp);

void solver_lp_reports (Workbook *wb, Sheet *sheet, GSList *ov,
			float_t ov_target, float_t *init_tbl,
			float_t *final_tbl,
			gboolean answer, gboolean sensitivity, 
			gboolean limits);

void write_constraint_str (char *buf, int lhs_col, int lhs_row, int rhs_col,
			   int rhs_row, char *type_str, int cols, int rows);

#endif
