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

/* ------------------------------------------------------------------------- */

typedef struct {
	FunctionIterateCallback  callback;
	void                     *closure;
	gboolean                 strict;
} IterateCallbackClosure;

/*
 * iterate_cellrange_callback:
 *
 * Helper routine used by the function_iterate_do_value routine.
 * Invoked by the sheet cell range iterator.
 */
static Value *
iterate_cellrange_callback (Sheet *sheet, int col, int row,
			    Cell *cell, void *user_data)
{
	IterateCallbackClosure *data = user_data;
	EvalPosition ep;
	Value *res;

	if (cell->generation != sheet->workbook->generation){
		cell->generation = sheet->workbook->generation;

		if (cell->parsed_node && (cell->flags & CELL_QUEUED_FOR_RECALC))
			cell_eval (cell);
	}

	/* If we encounter an error for the strict case, short-circuit here.  */
	if (data->strict && (NULL != (res = cell_is_error(cell))))
		return res;

	/* All other cases -- including error -- just call the handler.  */
	return (*data->callback)(eval_pos_init (&ep, sheet, col, row),
				 cell->value, data->closure);
}

/*
 * function_iterate_do_value:
 *
 * Helper routine for function_iterate_argument_values.
 */
Value *
function_iterate_do_value (const EvalPosition      *ep,
			   FunctionIterateCallback  callback,
			   void                    *closure,
			   Value                   *value,
			   gboolean                 strict)
{
	int eval_col = ep->eval_col;
	int eval_row = ep->eval_row;
	Value *res = NULL;

	switch (value->type){
	case VALUE_BOOLEAN:
	case VALUE_ERROR:
	case VALUE_INTEGER:
	case VALUE_FLOAT:
	case VALUE_STRING:
		res = (*callback)(ep, value, closure);
			break;

	case VALUE_ARRAY:
	{
		int x, y;
		
		for (x = 0; x < value->v.array.x; x++) {
			for (y = 0; y < value->v.array.y; y++) {
				res = function_iterate_do_value (
					ep, callback, closure,
					value->v.array.vals [x][y],
					strict);
				if (res != NULL)
					return res;
			}
		}
		break;
	}
	case VALUE_CELLRANGE: {
		IterateCallbackClosure data;
		Sheet *sheet;
		int start_col, start_row, end_col, end_row;

		data.callback = callback;
		data.closure  = closure;
		data.strict   = strict;

		cell_get_abs_col_row (&value->v.cell_range.cell_a,
				      eval_col, eval_row,
				      &start_col, &start_row);

		cell_get_abs_col_row (&value->v.cell_range.cell_b,
				      eval_col, eval_row,
				      &end_col, &end_row);

		sheet = eval_sheet (value->v.cell_range.cell_a.sheet, ep->sheet);
		res = sheet_cell_foreach_range (
			sheet, TRUE,
			start_col, start_row,
			end_col, end_row,
			iterate_cellrange_callback,
			&data);
	}
	case VALUE_EMPTY: break;
	}
	return res;
}

Value *
function_iterate_argument_values (const EvalPosition      *fp,
				  FunctionIterateCallback callback,
				  void                    *callback_closure,
				  GList                   *expr_node_list,
				  gboolean                strict)
{
	Value * result = NULL;
	FunctionEvalInfo fs;

	for (; result == NULL && expr_node_list;
	     expr_node_list = expr_node_list->next){
		ExprTree const * tree = (ExprTree const *) expr_node_list->data;
		Value *val;

		func_eval_info_pos (&fs, fp);
		val = eval_expr (&fs, tree);

		if (!VALUE_IS_PROBLEM(val)) {
			result = function_iterate_do_value (
				fp, callback, callback_closure,
				val, strict);
			value_release (val);
		} else if (strict) {
			/* A strict function -- just short circuit.  */
			/* FIXME : Make the new position of the error here */
			result = (val != NULL)
			    ? value_duplicate(val) : value_terminate();
		} else {
			/* A non-strict function -- call the handler.  */
			result = (*callback) (fp, val, callback_closure);
		}
	}
	return result;
}

/* ------------------------------------------------------------------------- */

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
const char *
tokenized_help_find (TokenizedHelp *tok, const char *token)
{
	int lp;

	if (!tok || !tok->sections)
		return "Incorrect Function Description.";

	for (lp = 0; lp < tok->sections->len-1; lp++){
		const char *cmp = g_ptr_array_index (tok->sections, lp);

		if (strcasecmp (cmp, token) == 0){
			return g_ptr_array_index (tok->sections, lp+1);
		}
	}
	return "Cannot find token";
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
	int lp, lp2;
	char valid_tokens[] = "fsbraA?|";
	g_return_if_fail (fd);

	/* Check those arguements */
	if (args) {
		int lena = strlen (args);
		int lenb = strlen (valid_tokens);
		for (lp=0;lp<lena;lp++) {
			int ok = 0;
			for (lp2=0;lp2<lenb;lp2++)
				if (valid_tokens[lp2] == args[lp])
					ok = 1;
			g_return_if_fail (ok);
		}
	}

	fd->name      = name;
	fd->args      = args;
	fd->help      = help;
	fd->named_arguments = arg_names;

	symbol_install (global_symbol_table, name,
			SYMBOL_FUNCTION, fd);
}

FunctionDefinition *function_add_nodes (FunctionCategory *parent,
					char *name,
					char *args,
					char *arg_names,
					char **help,
					FunctionNodes *fn)
{
	FunctionDefinition *fd;

	g_return_val_if_fail (fn, NULL);
	g_return_val_if_fail (parent, NULL);

	fd = g_new (FunctionDefinition, 1);
	fn_def_init (fd, name, args, arg_names, help);

	fd->fn_type     = FUNCTION_NODES;
	fd->fn.fn_nodes = fn;
	parent->functions = g_list_append (parent->functions, fd);
	return fd;
}

FunctionDefinition *function_add_args (FunctionCategory *parent,
				       char *name,
				       char *args,
				       char *arg_names,
				       char **help,
				       FunctionArgs *fn)
{
	FunctionDefinition *fd;

	g_return_val_if_fail (fn, NULL);
	g_return_val_if_fail (parent, NULL);

	fd = g_new (FunctionDefinition, 1);
	fn_def_init (fd, name, args, arg_names, help);

	fd->fn_type    = FUNCTION_ARGS;
	fd->fn.fn_args = fn;
	parent->functions = g_list_append (parent->functions, fd);
	return fd;
}

Value *
function_def_call_with_values (const EvalPosition *ep,
			       FunctionDefinition *fd,
			       int                 argc,
			       Value              *values [])
{
	Value *retval;
	FunctionEvalInfo s;

	func_eval_info_pos (&s, ep);

	if (fd->fn_type == FUNCTION_NODES) {
		/*
		 * If function deals with ExprNodes, create some
		 * temporary ExprNodes with constants.
		 */
		ExprTree *tree = NULL;
		GList *l = NULL;
		int i;

		if (argc){
			tree = g_new (ExprTree, argc);

			for (i = 0; i < argc; i++){
				tree [i].oper = OPER_CONSTANT;
				tree [i].ref_count = 1;
				tree [i].u.constant = values [i];

				l = g_list_append (l, &(tree[i]));
			}
		}

		retval = fd->fn.fn_nodes (&s, l);

		if (tree){
			g_free (tree);
			g_list_free (l);
		}

	} else
		retval = fd->fn.fn_args (&s, values);

	return retval;
}

/*
 * Use this to invoke a register function: the only drawback is that
 * you have to compute/expand all of the values to use this
 */
Value *
function_call_with_values (const EvalPosition *ep, const char *name,
			   int argc, Value *values[])
{
	FunctionDefinition *fd;
	Value *retval;
	Symbol *sym;

	g_return_val_if_fail (ep, NULL);
	g_return_val_if_fail (ep->sheet != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sym = symbol_lookup (global_symbol_table, name);
	if (sym == NULL)
		return value_new_error (ep, _("Function does not exist"));
	if (sym->type != SYMBOL_FUNCTION)
		return value_new_error (ep, _("Calling non-function"));

	fd = sym->data;

	symbol_ref (sym);
	retval = function_def_call_with_values (ep, fd, argc, values);
	
	symbol_unref (sym);

	return retval;
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
