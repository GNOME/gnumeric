/*
 * fn-sheet.c:  Built in sheet functions
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

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
gnumeric_if (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	ExprTree *expr;
	Value *value;
	int err, ret, args;
		
	/* Type checking */
	args = g_list_length (expr_node_list) ;
	if (args < 1 || args > 3) {
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	/* Compute the if part */
	value = eval_expr (tsheet, (ExprTree *) expr_node_list->data, eval_col, eval_row, error_string);
	if (value == NULL)
		return NULL;

	/* Choose which expression we will evaluate */
	ret = value_get_bool (value, &err);
	value_release (value);
	if (err)
		return NULL;
	
	if (ret) {
		if (expr_node_list->next)
			expr = (ExprTree *) expr_node_list->next->data;
		else
			return value_int (1) ;
	} else {
		if (expr_node_list->next && 
		    expr_node_list->next->next)
			expr = (ExprTree *) expr_node_list->next->next->data;
		else
			return value_int (0) ;
	}

	/* Return the result */
	return eval_expr (tsheet, (ExprTree *) expr, eval_col, eval_row, error_string);
}

static char *help_selection = {
	N_("@FUNCTION=SELECTION\n"
	   "@SYNTAX=SELECTION(x)\n"

	   "@DESCRIPTION="
	   "The SELECTION function returns a list with the values in the current mouse cursor. "
	   "This is usually used to implement on-the-flight computation of values"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_selection (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Sheet *sheet = (Sheet *) tsheet;
	Value *value;
	GList *l, *array;
	
	/* Type checking */
	if (expr_node_list != NULL){
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	value = g_new (Value, 1);
	value->type = VALUE_ARRAY;
	
	/* Create an array */
	array = NULL;
	for (l = sheet->selections; l; l = l->next){
		SheetSelection *ss = (SheetSelection *) l->data;
		Value *single_value;
		CellRef *cell_ref;

		/* Create the value */
		single_value = g_new (Value, 1);
		single_value->type = VALUE_CELLRANGE;

		/* Fill it in */
		/*   start */
		cell_ref = &single_value->v.cell_range.cell_a;
		cell_ref->sheet = tsheet;
		cell_ref->col_relative = 0;
		cell_ref->row_relative = 0;
		
		cell_ref->col = ss->start_col;
		cell_ref->row = ss->start_row;

		/*   end */
		cell_ref = &single_value->v.cell_range.cell_b;
		cell_ref->sheet = tsheet;
		cell_ref->col_relative = 0;
		cell_ref->row_relative = 0;
		
		cell_ref->col = ss->end_col;
		cell_ref->row = ss->end_row;

		array = g_list_prepend (array, single_value);

	}
	value->v.array = array;

	return value;
}

FunctionDefinition sheet_functions [] = {
	{ "if",     0,       "logical_test,value_if_true,value_if_false", &help_if,
	  gnumeric_if, NULL },
	{ "selection", 0,    "", &help_selection, gnumeric_selection, NULL },
	{ NULL, NULL }
};


	  


