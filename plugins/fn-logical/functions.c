/*
 * fn-logical.c:  Built in logical functions and functions registration
 *
 * Authors:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 */

#include <config.h>
#include "func.h"
#include "parse-util.h"
#include "cell.h"
#include "auto-format.h"

/***************************************************************************/

static char *help_and = {
	N_("@FUNCTION=AND\n"
	   "@SYNTAX=AND(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "AND implements the logical AND function: the result is TRUE "
	   "if all of the expressions evaluate to TRUE, otherwise it returns "
	   "FALSE.\n"

	   "@b1, trough @bN are expressions that should evaluate to TRUE "
	   "or FALSE.  If an integer or floating point value is provided "
	   "zero is considered FALSE and anything else is TRUE.\n"

	   "If the values contain strings or empty cells those values are "
	   "ignored. "
	   "If no logical values are provided, then the error #VALUE! "
	   "is returned. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "AND(TRUE,TRUE) equals TRUE.\n"
	   "AND(TRUE,FALSE) equals FALSE.\n\n"
	   "Let us assume that A1 holds number five and A2 number one.  Then\n"
	   "AND(A1>3,A2<2) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=OR, NOT")
};

static Value *
callback_function_and (const EvalPos *ep, Value *value, void *closure)
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
	Value *v = function_iterate_argument_values (ei->pos,
						     callback_function_and,
						     &result, nodes,
						     TRUE, TRUE);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_bool (result);
}

/***************************************************************************/

static char *help_not = {
	N_("@FUNCTION=NOT\n"
	   "@SYNTAX=NOT(number)\n"

	   "@DESCRIPTION="
	   "NOT implements the logical NOT function: the result is TRUE "
	   "if the @number is zero;  otherwise the result is FALSE.\n"
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "NOT(0) equals TRUE.\n"
	   "NOT(TRUE) equals FALSE.\n"
	   "\n"
	   "@SEEALSO=AND, OR")
};

static Value *
gnumeric_not (FunctionEvalInfo *ei, Value **argv)
{
	gboolean err, val = value_get_as_bool (argv [0], &err);
	if (err)
		return value_new_error (ei->pos, _("Type Mismatch"));
	return value_new_bool (!val);
}

/***************************************************************************/

static char *help_or = {
	N_("@FUNCTION=OR\n"
	   "@SYNTAX=OR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "OR implements the logical OR function: the result is TRUE if "
	   "any of the values evaluated to TRUE.\n"
	   "@b1, trough @bN are expressions that should evaluate to TRUE "
	   "or FALSE. If an integer or floating point value is provided "
	   "zero is considered FALSE and anything else is TRUE.\n"
	   "If the values contain strings or empty cells those values are "
	   "ignored.  If no logical values are provided, then the error "
	   "#VALUE! is returned. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "OR(TRUE,FALSE) equals TRUE.\n"
	   "OR(3>4,4<3) equals FALSE.\n"
	   "\n"
	   "@SEEALSO=AND, NOT")
};

static Value *
callback_function_or (const EvalPos *ep, Value *value, void *closure)
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
	Value *v = function_iterate_argument_values (ei->pos,
						     callback_function_or,
						     &result, nodes,
						     TRUE, TRUE);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_bool (result);
}

/***************************************************************************/

static char *help_if = {
	N_("@FUNCTION=IF\n"
	   "@SYNTAX=IF(condition[,if-true,if-false])\n"

	   "@DESCRIPTION="
	   "Use the IF statement to evaluate conditionally other expressions "
	   "IF evaluates @condition.  If @condition returns a non-zero value "
	   "the result of the IF expression is the @if-true expression, "
	   "otherwise IF evaluates to the value of @if-false. "
	   "If ommitted @if-true defaults to TRUE and @if-false to FALSE. "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "IF(FALSE,TRUE,FALSE) equals FALSE.\n"
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
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	/* Compute the if part */
	value = eval_expr (ei->pos, (ExprTree *) expr_node_list->data, EVAL_STRICT);
	if (VALUE_IS_EMPTY_OR_ERROR(value))
		return value;

	/* Choose which expression we will evaluate */
	ret = value_get_as_bool (value, &err);
	value_release (value);
	if (err)
		/* FIXME: please verify error code.  */
		return value_new_error (ei->pos, gnumeric_err_VALUE);

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
	return eval_expr (ei->pos, expr, EVAL_PERMIT_NON_SCALAR);
}

/***************************************************************************/

static char *help_true = {
	N_("@FUNCTION=TRUE\n"
	   "@SYNTAX=TRUE()\n"

	   "@DESCRIPTION="
	   "TRUE returns boolean value true.  "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "TRUE() equals TRUE.\n"
	   "\n"
	   "@SEEALSO=FALSE")
};

static Value *
gnumeric_true (FunctionEvalInfo *ei, Value **args)
{
	return value_new_bool (TRUE);
}

/***************************************************************************/

static char *help_false = {
	N_("@FUNCTION=FALSE\n"
	   "@SYNTAX=FALSE()\n"

	   "@DESCRIPTION="
	   "FALSE returns boolean value false.  "
	   "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "FALSE() equals FALSE.\n"
	   "\n"
	   "@SEEALSO=TRUE")
};

static Value *
gnumeric_false (FunctionEvalInfo *ei, Value **args)
{
	return value_new_bool (FALSE);
}

/***************************************************************************/

void logical_functions_init (void);
void
logical_functions_init (void)
{
	FunctionDefinition *def;
	FunctionCategory *cat = function_get_category_with_translation ("Logical", _("Logical"));

	function_add_nodes (cat,"and",     0,
			    "",
			    &help_and, gnumeric_and);

	function_add_args (cat,"false",    "",
			   "",
			   &help_false,  gnumeric_false);
	def = function_add_nodes (cat,"if",      0,
				  "logical_test,value_if_true,value_if_false",
				  &help_if,  gnumeric_if);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT2);

	function_add_args  (cat,"not",     "f",
			    "number",
			    &help_not, gnumeric_not);
	function_add_nodes (cat,"or",      0,
			    "",
			    &help_or,  gnumeric_or);
	function_add_args (cat,"true",    "",
			   "",
			   &help_true,  gnumeric_true);
}
