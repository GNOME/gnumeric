/*
 * func.c:  Built in mathematical functions and functions registration
 * (C) 1998 The Free Software Foundation
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

typedef struct {
	FunctionIterateCallback  callback;
	void                     *closure;
	char                     **error_string;
} IterateCallbackClosure;

/*
 * iterate_cellrange_callback:
 *
 * Helper routine used by the function_iterate_do_value routine.
 * Invoked by the sheet cell range iterator.
 */
static int
iterate_cellrange_callback (Sheet *sheet, int col, int row, Cell *cell, void *user_data)
{
	IterateCallbackClosure *data = user_data;
	int cont;
	
	if (!cell->value){
		printf ("iterate_cellrange_callback: Cell has no value\n");
		return TRUE;
	}
	
	cont = (*data->callback)(sheet, cell->value, data->error_string, data->closure);

	return cont;
}

/*
 * function_iterate_do_value:
 *
 * Helper routine for function_iterate_argument_values.
 */ 
static int
function_iterate_do_value (Sheet                   *sheet,
			   FunctionIterateCallback callback,
			   void                    *closure,
			   int                     eval_col,
			   int                     eval_row,
			   Value                   *value,
			   char                    **error_string)
{
	GList *list;
	int ret = TRUE;
	
	switch (value->type){
	case VALUE_INTEGER:
	case VALUE_FLOAT:
	case VALUE_STRING:
		ret = (*callback)(sheet, value, error_string, closure);
			break;
			
	case VALUE_ARRAY:
		for (list = value->v.array; list; list = list->next){
			Value *array_v = (Value *) list->data;

			ret = function_iterate_do_value (
				sheet, callback, closure,
				eval_col, eval_row,
				array_v, error_string);
			
			if (ret == FALSE)
				return FALSE;
		}
		break;
		
	case VALUE_CELLRANGE: {
		IterateCallbackClosure data;
		int start_col, start_row, end_col, end_row;
		
		data.callback = callback;
		data.closure  = closure;
		data.error_string = error_string;
		
		cell_get_abs_col_row (&value->v.cell_range.cell_a,
				      eval_col, eval_row,
				      &start_col, &start_row);

		cell_get_abs_col_row (&value->v.cell_range.cell_b,
				      eval_col, eval_row,
				      &end_col, &end_row);
		
		ret = sheet_cell_foreach_range (
			sheet, TRUE,
			start_col, start_row,
			end_col, end_row,
			iterate_cellrange_callback,
			&data);
	}
	}
	return ret;
}

int
function_iterate_argument_values (Sheet                   *sheet,
				  FunctionIterateCallback callback,
				  void                    *callback_closure,
				  GList                   *expr_node_list,
				  int                     eval_col,
				  int                     eval_row,
				  char                    **error_string)
{
	int result = TRUE;

	for (; result && expr_node_list; expr_node_list = expr_node_list->next){
		ExprTree *tree = (ExprTree *) expr_node_list->data;
		Value *value;

		value = eval_expr (sheet, tree, eval_col, eval_row, error_string);

		result = function_iterate_do_value (
			sheet, callback, callback_closure,
			eval_col, eval_row, value,
			error_string);
		
		value_release (value);
	}
	return result;
}

static void
install_symbols (FunctionDefinition *functions)
{
	int i;
	
	for (i = 0; functions [i].name; i++){
		symbol_install (functions [i].name, SYMBOL_FUNCTION, &functions [i]);
	}
}

void
functions_init (void)
{
	install_symbols (math_functions);
	install_symbols (sheet_functions);
}

void
constants_init (void)
{
	Value *true, *false;

	/* FALSE */
	false = g_new (Value, 1);
	false->type = VALUE_INTEGER;
	false->v.v_int = 0;

	/* TRUE */
	true = g_new (Value, 1);
	true->type = VALUE_INTEGER;
	true->v.v_int = 1;

	symbol_install ("FALSE", SYMBOL_VALUE, false);
	symbol_install ("TRUE", SYMBOL_VALUE, true);
}
