#ifndef GNUMERIC_SOLVER_H
#define GNUMERIC_SOLVER_H 1

#include "gnumeric.h"
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

typedef enum {
        SolverMinimize, SolverMaximize, SolverEqualTo
} SolverProblemType;

struct _SolverOptions {
        int                max_time_sec;
        int                iterations;
        gnum_float         precision;
        gnum_float         tolerance;
        gnum_float         convergence;
        gnum_float         equal_to_value;
        gboolean           assume_linear_model;
        gboolean           assume_non_negative;
        gboolean           automatic_scaling;
        gboolean           show_iteration_results;
};

struct _SolverConstraint {
        CellPos     lhs;		/* left hand side */
        CellPos     rhs;  		/* right hand side */
        gint        rows, cols;		/* number of rows and columns */
        char const *type;		/* <=, =, >=, int, bool */
        char       *str;		/* the same in string form */
};

struct _SolverParameters {
        SolverProblemType  problem_type;
        Cell               *target_cell;
        CellList           *input_cells;
        GSList             *constraints;
        char               *input_entry_str;
        SolverOptions      options;
};

int  solver_simplex (WorkbookControl *wbc, Sheet *sheet, gnum_float **init_table,
		     gnum_float **final_table);

int solver_affine_scaling (WorkbookControl *wbc, Sheet *sheet,
			   gnum_float **x,    /* the optimal solution */
			   gnum_float **sh_pr /* the shadow prizes */);

gboolean solver_lp (WorkbookControl *wbc, Sheet *sheet, gnum_float **opt_x,
		    gnum_float **sh_pr, gboolean *ilp);

void solver_lp_reports (WorkbookControl *wbc, Sheet *sheet, GSList *ov,
			gnum_float ov_target, gnum_float *init_tbl,
			gnum_float *final_tbl,
			gboolean answer, gboolean sensitivity,
			gboolean limits);

char *write_constraint_str (int lhs_col, int lhs_row, int rhs_col, int rhs_row,
			    const char *type_str, int cols, int rows);

SolverParameters *solver_lp_new (void);
void solver_lp_destroy (SolverParameters *);
SolverParameters *solver_lp_copy (const SolverParameters *src_param, Sheet *new_sheet);

#endif
