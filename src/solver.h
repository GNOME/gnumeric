#ifndef GNUMERIC_SOLVER_H
#define GNUMERIC_SOLVER_H

typedef enum {
        SolverMinimize, SolverMaximize, SolverEqualTo
} SolverProblemType;


typedef struct {
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
} SolverOptions;

typedef struct {
        Cell               *lhs;       /* left hand side */
        Cell               *rhs;       /* right hand side */
        gchar              *type;      /* <=, =, >=, int, bool */
        char               *str;       /* the same in string form */
} SolverConstraint;

typedef struct {
        SolverProblemType  problem_type;
        Cell               *target_cell;
        CellList           *input_cells;
        GSList             *constraints;
        SolverOptions      options;
} SolverParameters;


#endif
