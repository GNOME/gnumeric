#ifndef GNUMERIC_FUNC_H
#define GNUMERIC_FUNC_H

extern FunctionDefinition math_functions [];
extern FunctionDefinition sheet_functions [];
extern FunctionDefinition misc_functions [];
extern FunctionDefinition date_functions [];
extern FunctionDefinition string_functions [];
extern FunctionDefinition stat_functions [];
extern FunctionDefinition finance_functions [];

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
					   char      *name,
					   int argc,
					   Value *values [],
					   char **error_string);

Value      *function_def_call_with_values (Sheet              *sheet,
					   FunctionDefinition *fd,
					   int                 argc,
					   Value              *values [],
					   char               **error_string);

void install_symbols (FunctionDefinition *functions);

#endif /* GNUMERIC_FUNC_H */

