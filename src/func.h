#ifndef GNUMERIC_FUNC_H
#define GNUMERIC_FUNC_H

#include "expr.h"
#include "sheet.h"

extern FunctionDefinition math_functions [];
extern FunctionDefinition sheet_functions [];
extern FunctionDefinition misc_functions [];
extern FunctionDefinition date_functions [];
extern FunctionDefinition string_functions [];
extern FunctionDefinition stat_functions [];
extern FunctionDefinition finance_functions [];
extern FunctionDefinition eng_functions [];
extern FunctionDefinition lookup_functions [];
extern FunctionDefinition logical_functions [];
extern FunctionDefinition database_functions [];
extern FunctionDefinition information_functions [];

typedef int (*FunctionIterateCallback)(Sheet *sheet, Value *value, char **error_string, void *);

/*
 * function_iterate_argument_values
 *
 * sheet:            The sheet on which the expression is evaluated.
 * callback:         The routine to be invoked for every value computed
 * callback_closure: Closure for the callback.
 * expr_node_list:   a GList of ExprTrees (what a Gnumeric function would get).
 * eval_col:         Context column in which expressions are evaluated
 * eval_row:         Context row in which expressions are evaluated
 * error_string:     a pointer to a char* where an error message is stored.
 *
 * Return value:
 *    TRUE  if no errors were reported.
 *    FALSE if an error was found during evaluation.
 *
 * This routine provides a simple way for internal functions with variable
 * number of arguments to be written: this would iterate over a list of
 * expressions (expr_node_list) and will invoke the callback for every
 * Value found on the list (this means that ranges get properly expaned).
 */
int
function_iterate_argument_values (Sheet                   *sheet,
				  FunctionIterateCallback callback,
				  void                    *callback_closure,
				  GList                   *expr_node_list,
				  int                     eval_col,
				  int			  eval_row,
				  char                    **error_string);
				  

/*
 * function_call_with_values
 *
 */
Value      *function_call_with_values     (Sheet     *sheet,
					   const char      *name,
					   int argc,
					   Value *values [],
					   char **error_string);

Value      *function_def_call_with_values (Sheet              *sheet,
					   FunctionDefinition *fd,
					   int                 argc,
					   Value              *values [],
					   char               **error_string);


void        install_symbols               (FunctionDefinition *functions,
					   gchar *description);


typedef struct {
	gchar *name ;
	FunctionDefinition *functions;
} FunctionCategory;

GPtrArray *function_categories_get (void);

typedef struct {
	GPtrArray *sections ;
	char      *help_copy ;
	FunctionDefinition *fd ;
} TokenizedHelp;

typedef struct {
	int N;
	float_t M, Q;
} stat_closure_t;

TokenizedHelp *tokenized_help_new     (FunctionDefinition *fd) ;
char          *tokenized_help_find    (TokenizedHelp *tok, char *token) ;
void           tokenized_help_destroy (TokenizedHelp *tok) ;

float_t combin (int n, int k);
float_t fact   (int n);
void setup_stat_closure (stat_closure_t *cl);
int callback_function_stat (Sheet *sheet, Value *value, char **error_string,
			    void *closure);

Value *gnumeric_average     (Sheet *sheet, GList *expr_node_list,
			     int eval_col, int eval_row,
			     char **error_string);

Value *gnumeric_count       (Sheet *sheet, GList *expr_node_list,
			     int eval_col, int eval_row,
			     char **error_string);

Value *gnumeric_sum         (Sheet *sheet, GList *expr_node_list,
			     int eval_col, int eval_row,
			     char **error_string);

/* Type definitions and function prototypes for criteria functions.
 * This includes the database functions and some mathematical functions
 * like COUNTIF, SUMIF...
 */
typedef int (*criteria_test_fun_t) (Value *x, Value *y);

typedef struct {
        criteria_test_fun_t fun;
        Value                *x;
} func_criteria_t;

int criteria_test_equal(Value *x, Value *y);
int criteria_test_unequal(Value *x, Value *y);
int criteria_test_greater(Value *x, Value *y);
int criteria_test_less(Value *x, Value *y);
int criteria_test_greater_or_equal(Value *x, Value *y);
int criteria_test_less_or_equal(Value *x, Value *y);
void parse_criteria(char *criteria, criteria_test_fun_t *fun,
		    Value **test_value);


int solver_simplex(Workbook *wb, Sheet *sheet);

#endif /* GNUMERIC_FUNC_H */
