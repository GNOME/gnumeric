#ifndef GNUMERIC_SOLVER_H
#define GNUMERIC_SOLVER_H 1

#define SIMPLEX_OK        0
#define SIMPLEX_DONE      1
#define SIMPLEX_UNBOUNDED 2


/* Forward references for structures.  */
typedef struct _SolverOptions SolverOptions;
typedef struct _SolverConstraint SolverConstraint;
typedef struct _SolverParameters SolverParameters;

#include "cell.h"
#include "numbers.h"

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
        gchar              *type;             /* <=, =, >=, int, bool */
        char               *str;              /* the same in string form */
};

struct _SolverParameters {
        SolverProblemType  problem_type;
        Cell               *target_cell;
        CellList           *input_cells;
        GSList             *constraints;
        SolverOptions      options;
};

int  solver_simplex (Workbook *wb, Sheet *sheet, float_t **init_table,
		     float_t **final_table);

void solver_lp_reports (Workbook *wb, Sheet *sheet, GSList *ov,
			float_t ov_target, float_t *init_tbl,
			float_t *final_tbl,
			gboolean answer, gboolean sensitivity, 
			gboolean limits);

#endif
