#ifndef GNUMERIC_FUNC_UTIL_H
#define GNUMERIC_FUNC_UTIL_H

#include "numbers.h"

typedef struct {
	int N;
	gnum_float M, Q, sum;
        gboolean afun_flag;
} stat_closure_t;

void setup_stat_closure     (stat_closure_t *cl);
Value *callback_function_stat (EvalPos const *ep, Value *value,
			       void *closure);

Value *gnumeric_average     (FunctionEvalInfo *s, ExprList *nodes);
Value *gnumeric_count       (FunctionEvalInfo *s, ExprList *nodes);
Value *gnumeric_stdev       (FunctionEvalInfo *s, ExprList *nodes);
Value *gnumeric_stdevp      (FunctionEvalInfo *s, ExprList *nodes);
Value *gnumeric_var         (FunctionEvalInfo *s, ExprList *nodes);
Value *gnumeric_varp        (FunctionEvalInfo *s, ExprList *nodes);
Value *gnumeric_counta      (FunctionEvalInfo *s, ExprList *nodes);
Value *gnumeric_min         (FunctionEvalInfo *s, ExprList *nodes);
Value *gnumeric_max         (FunctionEvalInfo *s, ExprList *nodes);

Value *gnumeric_return_current_time (void);


/* Type definitions and function prototypes for criteria functions.
 * This includes the database functions and some mathematical functions
 * like COUNTIF, SUMIF...
 */
typedef gboolean (*criteria_test_fun_t) (Value const *x, Value const *y);

typedef struct {
        criteria_test_fun_t fun;
        Value               *x;
        int                 column;
} func_criteria_t;

gboolean criteria_test_equal            (Value const *x, Value const *y);
gboolean criteria_test_unequal          (Value const *x, Value const *y);
gboolean criteria_test_greater          (Value const *x, Value const *y);
gboolean criteria_test_less             (Value const *x, Value const *y);
gboolean criteria_test_greater_or_equal (Value const *x, Value const *y);
gboolean criteria_test_less_or_equal    (Value const *x, Value const *y);

void parse_criteria                 (char const *criteria,
				     criteria_test_fun_t *fun,
				     Value **test_value);
GSList *parse_criteria_range        (Sheet *sheet, int b_col, int b_row,
				     int e_col, int e_row,
				     int   *field_ind);
void free_criterias                 (GSList *criterias);
GSList *find_rows_that_match        (Sheet *sheet, int first_col,
				     int first_row, int last_col, int last_row,
				     GSList *criterias, gboolean unique_only);

#endif /* GNUMERIC_FUNC_UTIL_H */
