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
static GList *categories = NULL;

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
function_iterate_argument_values (const EvalPosition           *fp,
				  FunctionIterateCallback callback,
				  void                    *callback_closure,
				  GList                   *expr_node_list,
				  char                    **error_string)
{
	int result = TRUE;
	FunctionEvalInfo fs;

	for (; result && expr_node_list; expr_node_list = expr_node_list->next){
		ExprTree *tree = (ExprTree *) expr_node_list->data;
		Value *val;

		val = (Value *)eval_expr (func_eval_info_pos (&fs, fp, *error_string), tree);

		if (val){
			result = function_iterate_do_value (
				fp->sheet, callback, callback_closure,
				fp->eval_col, fp->eval_row, val,
				error_string);
			
			value_release (val);
		}
	}
	if (!result)
		*error_string = fs.error_string;
	return result;
}

GList *
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

FunctionCategory *function_get_category (gchar *description)
{
	FunctionCategory *cat = g_new (FunctionCategory, 1);
	
/* FIXME: should search for name first */
	cat->name = description;
	cat->functions = NULL;
	categories = g_list_append (categories, cat);

	return cat;
}

static void
fn_def_init (FunctionDefinition *fd, char *name, char *args, char *arg_names, char **help)
{
	g_return_val_if_fail (fn, NULL);
	g_return_val_if_fail (parent, NULL);

	fd->name      = name;
	fd->args      = args;
	fd->help      = help;
	fd->named_arguments = arg_names;
}

FunctionDefinition *function_new_nodes (FunctionCategory *parent,
					char *name,
					char *args,
					char *arg_names,
					char **help,
					FunctionNodes *fn)
{
	FunctionDefinition *fd = g_new (FunctionDefinition, 1);
	fn_def_init (fd, name, args, arg_names, help);

	fd->fn_type   = FUNCTION_NODES;
	fd->fn        = fn;
	parent->functions = g_list_append (parent->functions, fd);
	return fd;
}

FunctionDefinition *function_new_args (FunctionCategory *parent,
				       char *name,
				       char *args,
				       char *arg_names,
				       char **help,
				       FunctionArgs *fn)
{
	FunctionDefinition *fd = g_new (FunctionDefinition, 1);
	fn_def_init (fd, name, args, arg_names, help);

	fd->fn_type   = FUNCTION_ARGS;
	fd->fn        = fn;
	parent->functions = g_list_append (parent->functions, fd);
	return fd;
}

void
functions_init (void)
{
	math_functions_init();
	sheet_functions_init();
	misc_functions_init();
	date_functions_init();
	string_functions_init();
	stat_functions_init();
	finance_functions_init();
	eng_functions_init();
	lookup_functions_init();
	logical_functions_init();
	database_functions_init();
	information_functions_init();
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

	/* Global helper value for arrays */
	value_zero = value_new_float (0);
}

