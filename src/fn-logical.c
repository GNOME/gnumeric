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

static Value *
callback_function_and (const EvalPosition *ep, Value *value, void *closure)
{
	int *result = closure;
	gboolean err;

	*result = value_get_as_bool (value, &err) && *result;
	if (err)
		return value_new_error (ep, gnumeric_err_VALUE);

	return NULL;
}

static Value *
gnumeric_and (FunctionEvalInfo *ei, GList *nodes)
{
	int result = -1;

	/* Yes, AND is actually strict.  */
	Value *v = function_iterate_argument_values (&ei->pos,
						     callback_function_and,
						     &result, nodes, TRUE);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error (&ei->pos, gnumeric_err_VALUE);

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
gnumeric_not (FunctionEvalInfo *ei, Value **argv)
{
	gboolean err, val = value_get_as_bool (argv [0], &err);
	if (err)
		return value_new_error (&ei->pos, _("Type Mismatch"));
	return value_new_bool (!val);
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
	   "is returned."
	   "\n"
	   "@SEEALSO=AND, NOT")
};

static Value *
callback_function_or (const EvalPosition *ep, Value *value, void *closure)
{
	int *result = closure;
	gboolean err;

	*result = value_get_as_bool (value, &err) || *result == 1;
	if (err)
		return value_new_error (ep, gnumeric_err_VALUE);

	return NULL;
}

static Value *
gnumeric_or (FunctionEvalInfo *ei, GList *nodes)
{
	int result = -1;

	/* Yes, OR is actually strict.  */
	Value *v = function_iterate_argument_values (&ei->pos,
						     callback_function_or,
						     &result, nodes, TRUE);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error (&ei->pos, gnumeric_err_VALUE);

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
gnumeric_if (FunctionEvalInfo *ei, GList *expr_node_list)
{
	ExprTree *expr;
	Value *value;
	int ret, args;
	gboolean err;

	/* Type checking */
	args = g_list_length (expr_node_list);
	if (args < 1 || args > 3)
		return value_new_error (&ei->pos, _("Invalid number of arguments"));

	/* Compute the if part */
	value = eval_expr (ei, (ExprTree *) expr_node_list->data);
	if (value == NULL)
		return NULL;

	/* Choose which expression we will evaluate */
	ret = value_get_as_bool (value, &err);
	value_release (value);
	if (err)
		/* FIXME: please verify error code.  */
		return value_new_error (&ei->pos, gnumeric_err_VALUE);

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
	return eval_expr (ei, expr);
}

void logical_functions_init()
{
	FunctionCategory *cat = function_get_category (_("Logical"));

	function_add_nodes (cat,"and",     0,      "",          &help_and, gnumeric_and);
	function_add_nodes (cat,"if",      0,      "logical_test,value_if_true,value_if_false", &help_if,
			    gnumeric_if);
	function_add_args  (cat,"not",     "f",    "number",    &help_not, gnumeric_not);
	function_add_nodes (cat,"or",      0,      "",          &help_or,  gnumeric_or);
}
