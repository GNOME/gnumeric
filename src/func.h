#ifndef GNUMERIC_FUNC_H
#define GNUMERIC_FUNC_H

#include <gnome.h>
#include "gnumeric.h"
#include "value.h"
#include "expr.h"

extern void math_functions_init        (void);
extern void sheet_functions_init       (void);
extern void date_functions_init        (void);
extern void string_functions_init      (void);
extern void stat_functions_init        (void);
extern void finance_functions_init     (void);
extern void eng_functions_init         (void);
extern void lookup_functions_init      (void);
extern void logical_functions_init     (void);
extern void database_functions_init    (void);
extern void information_functions_init (void);

typedef Value * (*FunctionIterateCallback)(const EvalPos *ep, Value *value,
					   void *);

/*
 * function_iterate_argument_values
 *
 * fp:               The position in a workbook at which to evaluate
 * callback:         The routine to be invoked for every value computed
 * callback_closure: Closure for the callback.
 * expr_node_list:   a GList of ExprTrees (what a Gnumeric function would get).
 * strict:           If TRUE, the function is considered "strict".  This means
 *                   that if an error value occurs as an argument, the iteration
 *                   will stop and that error will be returned.  If FALSE, an
 *                   error will be passed on to the callback (as a Value *
 *                   of type VALUE_ERROR).
 *
 * Return value:
 *    NULL            : if no errors were reported.
 *    Value *         : if an error was found during strict evaluation
 *    value_terminate : if the callback requested termination of the iteration.
 *
 * This routine provides a simple way for internal functions with variable
 * number of arguments to be written: this would iterate over a list of
 * expressions (expr_node_list) and will invoke the callback for every
 * Value found on the list (this means that ranges get properly expaned).
 */
Value *
function_iterate_argument_values (const EvalPos      *fp,
				  FunctionIterateCallback callback,
				  void                    *callback_closure,
				  GList                   *expr_node_list,
				  gboolean                strict);
				  
/*
 * function_call_with_values
 */
Value      *function_call_with_values     (const EvalPos *ep,
					   const char         *name,
					   int                 argc,
					   Value              *values []);

Value      *function_def_call_with_values (const EvalPos *ep,
					   FunctionDefinition *fndef,
					   int                 argc,
					   Value              *values []);

Value      *function_iterate_do_value     (const EvalPos      *fp,
					   FunctionIterateCallback callback,
					   void                    *closure,
					   Value                   *value,
					   gboolean                strict,
					   gboolean		   ignore_blank);

Value      *function_call_with_list       (FunctionEvalInfo        *ei,
					   GList                   *args);

/*
 * Gnumeric function defintion API.
 */
typedef struct _FunctionCategory FunctionCategory;
struct _FunctionCategory {
	String const *name;
	GList *functions;
};
FunctionCategory   *function_get_category (gchar const *description);

/*
 * Functions come in two fashions:  Those that only deal with
 * very specific data types and a constant number of arguments,
 * and those who don't.
 *
 * The former kind of functions receives a precomputed array of
 * Value pointers.
 *
 * The latter sort of functions receives the plain ExprNodes and
 * it is up to that routine to do the value computations and range
 * processing.
 */

/**
 *  Argument tokens passed in 'args'
 **
 *  The types accepted: see writing-functions.smgl ( bottom )
 * f for float
 * s for string
 * b for boolean
 * r for cell range
 * a for cell array
 * A for 'area': either range or array
 * ? for any kind
 *  For optional arguments do:
 * "ff|ss" where the strings are optional
 **/
FunctionDefinition *function_add_args  (FunctionCategory *parent,
				        char const *name,
				        char const *args,
				        char const *arg_names,
				        char **help,
				        FunctionArgs *fn);
FunctionDefinition *function_add_nodes (FunctionCategory *parent,
					char const *name,
					char const *args,
					char const *arg_names,
					char **help,
					FunctionNodes *fn);

gboolean            function_is_unused (FunctionDefinition *fndef);
void                function_remove    (FunctionCategory   *parent,
					FunctionDefinition *fndef);

gpointer function_def_get_fn           (const FunctionDefinition *fndef);
void     function_def_set_user_data    (FunctionDefinition *fndef,
					gpointer user_data);

gpointer function_def_get_user_data    (const FunctionDefinition *fndef);

void     function_def_count_args       (const FunctionDefinition *fndef,
					int *min, int *max);
/* 0 based arg_index */
char     function_def_get_arg_type     (const FunctionDefinition *fndef,
					int arg_idx);
const char *function_def_get_name      (const FunctionDefinition *fndef);

/* Used to build manual */
void     function_dump_defs            (const char *filename);

FunctionCategory const * function_category_get_nth (int const n);

typedef struct {
	GPtrArray *sections;
	char      *help_copy;
	FunctionDefinition const *fndef;
} TokenizedHelp;

typedef struct {
	int N;
	float_t M, Q, sum;
        gboolean afun_flag;
} stat_closure_t;

TokenizedHelp *tokenized_help_new     (FunctionDefinition const *fndef);
const char    *tokenized_help_find    (TokenizedHelp *tok, const char *token);
void           tokenized_help_destroy (TokenizedHelp *tok);

float_t        combin (int n, int k);
float_t        fact   (int n);

void setup_stat_closure     (stat_closure_t *cl);
Value *callback_function_stat (const EvalPos *ep, Value *value,
			       void *closure);

Value *gnumeric_average     (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_count       (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_stdev       (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_stdevp      (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_var         (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_varp        (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_counta      (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_min         (FunctionEvalInfo *s, GList *nodes);
Value *gnumeric_max         (FunctionEvalInfo *s, GList *nodes);

Value *gnumeric_return_current_time (void);


/* Type definitions and function prototypes for criteria functions.
 * This includes the database functions and some mathematical functions
 * like COUNTIF, SUMIF...
 */
typedef int (*criteria_test_fun_t) (Value *x, Value *y);

typedef struct {
        criteria_test_fun_t fun;
        Value               *x;
        int                 column;
} func_criteria_t;

int  criteria_test_equal            (Value *x, Value *y);
int  criteria_test_unequal          (Value *x, Value *y);
int  criteria_test_greater          (Value *x, Value *y);
int  criteria_test_less             (Value *x, Value *y);
int  criteria_test_greater_or_equal (Value *x, Value *y);
int  criteria_test_less_or_equal    (Value *x, Value *y);
void parse_criteria                 (const char *criteria,
				     criteria_test_fun_t *fun,
				     Value **test_value);
GSList *parse_criteria_range        (Sheet *sheet, int b_col, int b_row,
				     int e_col, int e_row,
				     int   *field_ind);
void free_criterias                 (GSList *criterias);
GSList *find_rows_that_match        (Sheet *sheet, int first_col,
				     int first_row, int last_col, int last_row,
				     GSList *criterias, gboolean unique_only);


#endif /* GNUMERIC_FUNC_H */
