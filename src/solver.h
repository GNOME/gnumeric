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
        Cell               *lhs;       /* left hand side */
        Cell               *rhs;       /* right hand side */
        gchar              *type;      /* <=, =, >=, int, bool */
        char               *str;       /* the same in string form */
};

struct _SolverParameters {
        SolverProblemType  problem_type;
        Cell               *target_cell;
        CellList           *input_cells;
        GSList             *constraints;
        SolverOptions      options;
};

#endif
