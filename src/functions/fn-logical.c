/*
 * fn-logical.c:  Built in logical functions and functions registration
 *
 * Authors:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
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
		       ErrorMessage *error, void *closure)
{
	Value *result = closure;

	switch (value->type){
	case VALUE_INTEGER:
		if (value->v.v_int == 0){
			result->v.v_int = 0;
			return FALSE;
		} else
			result->v.v_int = 1;
		break;

	case VALUE_FLOAT:
		if (value->v.v_float == 0.0){
			result->v.v_int = 0;
			return FALSE;
		} else
			result->v.v_int = 1;

	default:
		/* ignore strings */
		break;
	}

	return TRUE;
}

static FuncReturn *
gnumeric_and (FunctionEvalInfo *ei, GList *nodes)
{
	Value *result;

	result = value_new_int (-1);

	function_iterate_argument_values (&ei->pos, callback_function_and,
					  result, nodes,
					  ei->error);

	/* See if there was any value worth using */
	if (result->v.v_int == -1) {
		value_release (result);
		return function_error (ei, gnumeric_err_VALUE);
	}
	FUNC_RETURN_VAL (result);
}


static char *help_not = {
	N_("@FUNCTION=NOT\n"
	   "@SYNTAX=NOT(number)\n"

	   "@DESCRIPTION="
	   "Implements the logical NOT function: the result is TRUE if the "
	   "number is zero;  othewise the result is FALSE.\n\n"

	   "@SEEALSO=AND, OR")
};

static FuncReturn *
gnumeric_not (FunctionEvalInfo *ei, Value **args)
{
	int b;

	b = value_get_as_int (args[0]);

	FUNC_RETURN_VAL (value_new_int (!b));
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
		      ErrorMessage *error, void *closure)
{
	Value *result = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		if (value->v.v_int != 0){
			result->v.v_int = 1;
			return FALSE;
		} else
			result->v.v_int = 0;
		break;

	case VALUE_FLOAT:
		if (value->v.v_float != 0.0){
			result->v.v_int = 1;
			return FALSE;
		} else
			result->v.v_int = 0;

	default:
		/* ignore strings */
		break;
	}
	
	return TRUE;
}

static FuncReturn *
gnumeric_or (FunctionEvalInfo *ei, GList *nodes)
{
	Value *result;

	result = value_new_int (-1);

	function_iterate_argument_values (&ei->pos, callback_function_or,
					  result, nodes,
					  ei->error);

	/* See if there was any value worth using */
	if (result->v.v_int == -1){
		value_release (result);
		return function_error (ei, gnumeric_err_VALUE);
	}
	FUNC_RETURN_VAL (result);
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

static FuncReturn *
gnumeric_if (FunctionEvalInfo *ei, GList *nodes)
{
	ExprTree *expr;
	Value *value;
	int err, ret, args;
		
	/* Type checking */
	args = g_list_length (nodes);
	if (args < 1 || args > 3)
		return function_error (ei, _("Invalid number of arguments"));

	/* Compute the if part */
	value = (Value *)eval_expr (ei, (ExprTree *) nodes->data);
	if (value == NULL)
		return NULL;

	/* Choose which expression we will evaluate */
	ret = value_get_as_bool (value, &err);
	value_release (value);
	if (err)
		return NULL;
	
	if (ret){
		if (nodes->next)
			expr = (ExprTree *) nodes->next->data;
		else
			FUNC_RETURN_VAL (value_new_int (1));
	} else {
		if (nodes->next && 
		    nodes->next->next)
			expr = (ExprTree *) nodes->next->next->data;
		else
			FUNC_RETURN_VAL (value_new_int (0));
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
