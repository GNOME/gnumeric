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

static Value *
gnumeric_if (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	ExprTree *expr;
	Value *value;
	int err, ret;
		
	/* Type checking */
	if (g_list_length (expr_node_list) != 3){
		*error_string = _("Invalid number of arguments");
		return NULL;
	}

	/* Compute the if part */
	value = eval_expr (tsheet, (ExprTree *) expr_node_list->data, eval_col, eval_row, error_string);
	if (value == NULL)
		return NULL;

	/* Choose which expression we will evaluate */
	ret = value_get_bool (value, &err);
	if (err)
		return NULL;
	
	if (ret)
		expr = (ExprTree *) expr_node_list->next->data;
	else
		expr = (ExprTree *) expr_node_list->next->next->data;

	value_release (value);

	/* Return the result */
	return eval_expr (tsheet, (ExprTree *) expr, eval_col, eval_row, error_string);
}

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
	{ "if",     0,       "logical_test,value_if_true,value_if_false", NULL,
	  gnumeric_if, NULL },
	{ "selection", 0,    "", NULL, gnumeric_selection, NULL },
	{ NULL, NULL }
};


	  


