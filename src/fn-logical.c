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
		       char **error_string, void *closure)
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

static Value *
gnumeric_and (Sheet *sheet, GList *expr_node_list,
	      int eval_col, int eval_row, char **error_string)
{
	Value *result;

	result = value_new_int (-1);

	function_iterate_argument_values (sheet, callback_function_and,
					  result, expr_node_list,
					  eval_col, eval_row, error_string);

	/* See if there was any value worth using */
	if (result->v.v_int == -1){
		value_release (result);
		*error_string = _("#VALUE");
		return NULL;
	}
	return result;
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
	int b;

	b = value_get_as_int (argv [0]);

	return value_new_int (!b);
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

static Value *
gnumeric_or (Sheet *sheet, GList *expr_node_list,
	     int eval_col, int eval_row, char **error_string)
{
	Value *result;

	result = value_new_int (-1);

	function_iterate_argument_values (sheet, callback_function_or,
					  result, expr_node_list,
					  eval_col, eval_row, error_string);

	/* See if there was any value worth using */
	if (result->v.v_int == -1){
		value_release (result);
		*error_string = _("#VALUE");
		return NULL;
	}
	return result;
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
	if (err)
		return NULL;
	
	if (ret){
		if (expr_node_list->next)
			expr = (ExprTree *) expr_node_list->next->data;
		else
			return value_new_int (1);
	} else {
		if (expr_node_list->next && 
		    expr_node_list->next->next)
			expr = (ExprTree *) expr_node_list->next->next->data;
		else
			return value_new_int (0);
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

void logical_functions_init()
{
	FunctionCategory *cat = function_get_category (_("Logical"));
}
