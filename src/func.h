#ifndef GNUMERIC_FUNC_H
#define GNUMERIC_FUNC_H

extern FunctionDefinition math_functions [];
extern FunctionDefinition sheet_functions [];

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
				  

#endif /* GNUMERIC_FUNC_H */

