/*
 * func.c:  Built in mathematical functions and functions registration
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <math.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"
#include "eval.h"

/* The list of categories */
static GPtrArray *categories = NULL;

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

	if (cell->generation != sheet->workbook->generation){
		cell->generation = sheet->workbook->generation;

		if (cell->parsed_node && (cell->flags & CELL_QUEUED_FOR_RECALC))
			cell_eval (cell);
	}
	
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
int
function_iterate_do_value (Sheet                   *sheet,
			   FunctionIterateCallback callback,
			   void                    *closure,
			   int                     eval_col,
			   int                     eval_row,
			   Value                   *value,
			   char                    **error_string)
{
	int ret = TRUE;
	
	switch (value->type){
	case VALUE_INTEGER:
	case VALUE_FLOAT:
	case VALUE_STRING:
		ret = (*callback)(sheet, value, error_string, closure);
			break;
			
	case VALUE_ARRAY:
	{
		int x, y;

		for (x = 0; x < value->v.array.x; x++){
			for (y = 0; y < value->v.array.y; y++){
				ret = function_iterate_do_value (
					sheet, callback, closure,
					eval_col, eval_row,
					value->v.array.vals [x][y], error_string);
				if (ret == FALSE)
					return FALSE;
			}
		}
		break;
	}
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
			value->v.cell_range.cell_a.sheet, TRUE,
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

GPtrArray *
function_categories_get (void)
{
	return categories;
}

TokenizedHelp *
tokenized_help_new (FunctionDefinition *fd)
{
	TokenizedHelp *tok;

	g_return_val_if_fail (fd != NULL, NULL);

	tok = g_new (TokenizedHelp, 1);

	tok->fd = fd;

	if (fd->help && fd->help [0]){
		char *ptr;
		int seek_att = 1;
		int last_newline = 1;
		
		tok->help_copy = g_strdup (fd->help [0]);
		tok->sections = g_ptr_array_new ();
		ptr = tok->help_copy;
		
		while (*ptr){
			if (*ptr == '\\' && *(ptr+1))
				ptr+=2;
			
			if (*ptr == '@' && seek_att && last_newline){
				*ptr = 0;
				g_ptr_array_add (tok->sections, (ptr+1));
				seek_att = 0;
			} else if (*ptr == '=' && !seek_att){
				*ptr = 0;
				g_ptr_array_add (tok->sections, (ptr+1));
				seek_att = 1;
			}
			last_newline = (*ptr == '\n');

			ptr++;
		}
	} else {
		tok->help_copy = NULL;
		tok->sections = NULL;
	}
	
	return tok;
}

/**
 * Use to find a token eg. "FUNCTION"'s value.
 **/
char *
tokenized_help_find (TokenizedHelp *tok, char *token)
{
	int lp;

	if (!tok || !tok->sections)
		return "Incorrect Function Description.";
	
	for (lp = 0; lp < tok->sections->len-1; lp++){
		char *cmp = g_ptr_array_index (tok->sections, lp);

		if (strcasecmp (cmp, token) == 0){
			return g_ptr_array_index (tok->sections, lp+1);
		}
	}
	return "Can not find token";
}

void
tokenized_help_destroy (TokenizedHelp *tok)
{
	g_return_if_fail (tok != NULL);

	if (tok->help_copy)
		g_free (tok->help_copy);

	if (tok->sections)
		g_ptr_array_free (tok->sections, FALSE);

	g_free (tok);
}

void
install_symbols (FunctionDefinition *functions, gchar *description)
{
	int i;
	FunctionCategory *fn_cat = g_new (FunctionCategory, 1);
	
	g_return_if_fail (categories);

	fn_cat->name = description;
	fn_cat->functions = functions;
	g_ptr_array_add (categories, fn_cat); 
	
	for (i = 0; functions [i].name; i++){
		symbol_install (global_symbol_table, functions [i].name,
				SYMBOL_FUNCTION, &functions [i]);
	}
}

void
functions_init (void)
{
	categories = g_ptr_array_new ();

	install_symbols (math_functions, _("Maths / Trig."));
	install_symbols (sheet_functions, _("Sheet"));
	install_symbols (misc_functions, _("Miscellaneous"));
	install_symbols (date_functions, _("Date / Time"));
	install_symbols (string_functions, _("String"));
	install_symbols (stat_functions, _("Statistics"));
	install_symbols (finance_functions, _("Financial"));
	install_symbols (eng_functions, _("Engineering"));
	install_symbols (lookup_functions, _("Data / Lookup"));
	install_symbols (logical_functions, _("Logical"));
	install_symbols (database_functions, _("Database"));
	install_symbols (information_functions, _("Information"));
}

/* Initialize temporarily with statics.  The real versions from the locale
 * will be setup in constants_init 
 */
char * gnumeric_err_NULL = "#NULL!";
char * gnumeric_err_DIV0 = "#DIV/0!";
char * gnumeric_err_VALUE = "#VALUE!";
char * gnumeric_err_REF = "#REF!";
char * gnumeric_err_NAME = "#NAME?";
char * gnumeric_err_NUM = "#NUM!";
char * gnumeric_err_NA = "#N/A";

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

	/* Global helper value for arrays */
	value_zero = value_new_float (0);

	gnumeric_err_NULL = _("#NULL!");
	gnumeric_err_DIV0 = _("#DIV/0!");
	gnumeric_err_VALUE = _("#VALUE!");
	gnumeric_err_REF = _("#REF!");
	gnumeric_err_NAME = _("#NAME?");
	gnumeric_err_NUM = _("#NUM!");
	gnumeric_err_NA = _("#N/A");
}

