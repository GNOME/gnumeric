/*
 * func.c:  Built in mathematical functions and functions registration
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
		/*
		 * FIXME: If this is a formula, is it worth recursing on
		 * this one? IFF !(cell->flags & CELL_ERROR) &&
		 * cell->generation != cell->sheet->workbook->generation?
		 */
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
		Value *val;

		val = eval_expr (sheet, tree, eval_col, eval_row, error_string);

		if (val){
			result = function_iterate_do_value (
				sheet, callback, callback_closure,
				eval_col, eval_row, val,
				error_string);
			
			value_release (val);
		}
	}
	return result;
}

void
install_symbols (FunctionDefinition *functions)
{
	int i;
	
	for (i = 0; functions [i].name; i++){
		symbol_install (global_symbol_table, functions [i].name,
				SYMBOL_FUNCTION, &functions [i]);
	}
}

void
functions_init (void)
{
	install_symbols (math_functions);
	install_symbols (sheet_functions);
	install_symbols (misc_functions);
	install_symbols (date_functions);
	install_symbols (string_functions);
}

void
constants_init (void)
{
	Value *true, *false, *version;

	/* FALSE */
	false = g_new (Value, 1);
	false->type = VALUE_INTEGER;
	false->v.v_int = 0;

	/* TRUE */
	true = g_new (Value, 1);
	true->type = VALUE_INTEGER;
	true->v.v_int = 1;

	/* GNUMERIC_VERSION */
	version = g_new (Value, 1);
	version->type = VALUE_FLOAT;
	version->v.v_float = atof (GNUMERIC_VERSION);
	
	symbol_install (global_symbol_table, "FALSE", SYMBOL_VALUE, false);
	symbol_install (global_symbol_table, "TRUE", SYMBOL_VALUE, true);
	symbol_install (global_symbol_table, "GNUMERIC_VERSION", SYMBOL_VALUE, version);
}
