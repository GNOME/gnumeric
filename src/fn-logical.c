/*
 * fn-logical.c:  Built in logical functions and functions registration
 *
 * Authors:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 */
#include <config.h>
#include "gnumeric.h"
#include "utils.h"
#include "func.h"


static char *help_and = {
	N_("@FUNCTION=AND\n"
	   "@SYNTAX=AND(b1, b2, ...)\n"

	   "@DESCRIPTION=Implements the logical AND function: the result is TRUE "
	   "if all of the expression evaluates to TRUE, otherwise it returns "
	   "FALSE.\n"

	   "b1, trough bN are expressions that should evaluate to TRUE or FALSE. "
	   "If an integer or floating point value is provided zero is considered "
	   "FALSE and anything else is TRUE.\n"

	   "If the values contain strings or empty cells those values are "
	   "ignored.  If no logical values are provided, then the error '#VALUE!' "
	   "is returned. "
	   "\n"
	   "@SEEALSO=OR, NOT")
};

static int
callback_function_and (Sheet *sheet, Value *value,
		       char **error_string, void *closure)
{
	int *result = closure;
	int err;

	if (!value_get_as_bool (value, &err) && !err) {
		*result = 0;
		return FALSE;
	}
	*result = 1;

	return TRUE;
}


static Value *
gnumeric_and (Sheet *sheet, GList *expr_node_list,
	      int eval_col, int eval_row, char **error_string)
{
	int result = -1;

	function_iterate_argument_values (sheet, callback_function_and,
					  &result, expr_node_list,
					  eval_col, eval_row, error_string);

	/* See if there was any value worth using */
	if (result == -1){
		*error_string = gnumeric_err_VALUE;
		return NULL;
	}
	return value_new_bool (result);
}


static char *help_not = {
	N_("@FUNCTION=NOT\n"
	   "@SYNTAX=NOT(number)\n"

	   "@DESCRIPTION="
	   "Implements the logical NOT function: the result is TRUE if the "
	   "number is zero;  othewise the result is FALSE.\n\n"

	   "@SEEALSO=AND, OR")
};

static Value *
gnumeric_not (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
	/* FIXME: We should probably use value_get_as_bool.  */
	return value_new_bool (!value_get_as_int (argv [0]));
}

static char *help_or = {
	N_("@FUNCTION=OR\n"
	   "@SYNTAX=OR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "Implements the logical OR function: the result is TRUE if any of the "
	   "values evaluated to TRUE.\n"
	   "b1, trough bN are expressions that should evaluate to TRUE or FALSE. "
	   "If an integer or floating point value is provided zero is considered "
	   "FALSE and anything else is TRUE.\n"
	   "If the values contain strings or empty cells those values are "
	   "ignored.  If no logical values are provided, then the error '#VALUE!'"
	   "is returned.\n"

	   "@SEEALSO=AND, NOT")
};

static int
callback_function_or (Sheet *sheet, Value *value,
		      char **error_string, void *closure)
{
	int *result = closure;
	int err;

	if (value_get_as_bool (value, &err) && !err) {
		*result = 1;
		return FALSE;
	}
	*result = 0;

	return TRUE;
}

static Value *
gnumeric_or (Sheet *sheet, GList *expr_node_list,
	     int eval_col, int eval_row, char **error_string)
{
	int result = -1;

	function_iterate_argument_values (sheet, callback_function_or,
					  &result, expr_node_list,
					  eval_col, eval_row, error_string);

	/* See if there was any value worth using */
	if (result == -1){
		*error_string = gnumeric_err_VALUE;
		return NULL;
	}
	return value_new_bool (result);
}

static char *help_if = {
	N_("@FUNCTION=IF\n"
	   "@SYNTAX=IF(condition[,if-true,if-false])\n"

	   "@DESCRIPTION="
	   "Use the IF statement to evaluate conditionally other expressions "
	   "IF evaluates @condition.  If @condition returns a non-zero value "
	   "the result of the IF expression is the @if-true expression, otherwise "
	   "IF evaluates to the value of @if-false."
	   "If ommitted if-true defaults to TRUE and if-false to FALSE."
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_if (Sheet *sheet, GList *expr_node_list,
	     int eval_col, int eval_row, char **error_string)
{
	ExprTree *expr;
	Value *value;
	int err, ret, args;

	/* Type checking */
	args = g_list_length (expr_node_list);
	if (args < 1 || args > 3){
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	/* Compute the if part */
	value = eval_expr (sheet, (ExprTree *) expr_node_list->data,
			   eval_col, eval_row, error_string);
	if (value == NULL)
		return NULL;

	/* Choose which expression we will evaluate */
	ret = value_get_as_bool (value, &err);
	value_release (value);
	if (err) {
		/* FIXME: please verify error code.  */
		*error_string = gnumeric_err_VALUE;
		return NULL;
	}

	if (ret){
		if (expr_node_list->next)
			expr = (ExprTree *) expr_node_list->next->data;
		else
			return value_new_bool (TRUE);
	} else {
		if (expr_node_list->next &&
		    expr_node_list->next->next)
			expr = (ExprTree *) expr_node_list->next->next->data;
		else
			return value_new_bool (FALSE);
	}

	/* Return the result */
	return eval_expr (sheet, expr, eval_col, eval_row, error_string);
}


FunctionDefinition logical_functions [] = {
	{ "and",     0,      "",          &help_and,   gnumeric_and, NULL },
	{ "if",     0,       "logical_test,value_if_true,value_if_false", &help_if,
	  gnumeric_if, NULL },
	{ "not",     "f",    "number",    &help_not,     NULL, gnumeric_not },
	{ "or",      0,      "",          &help_or,      gnumeric_or, NULL },
	{ NULL, NULL },
};
