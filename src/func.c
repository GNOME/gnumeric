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

/*
 * Error constants used to display error messages.
 */
char *gnumeric_err_NULL;
char *gnumeric_err_DIV0;
char *gnumeric_err_VALUE;
char *gnumeric_err_REF;
char *gnumeric_err_NAME;
char *gnumeric_err_NUM;
char *gnumeric_err_NA;

/* The list of categories */
static GList *categories = NULL;

/* ------------------------------------------------------------------------- */

typedef struct {
	FunctionIterateCallback  callback;
	void                     *closure;
	ErrorMessage             *error;
	gboolean                 strict;
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
	EvalPosition ep;

	if (cell->generation != sheet->workbook->generation){
		cell->generation = sheet->workbook->generation;

		if (cell->parsed_node && (cell->flags & CELL_QUEUED_FOR_RECALC))
			cell_eval (cell);
	}

	/* If we encounter an error for the strict case, short-circuit here.  */
	if (data->strict && !cell->value) {
		if (cell->text)
			error_message_set (data->error, cell->text->str);
		else
			error_message_set (data->error, _("Unknown error"));
		return FALSE;
	}

	/* All other cases -- including error -- just call the handler.  */
	return (*data->callback)(eval_pos_init (&ep, sheet, col, row),
				 cell->value, data->error, data->closure);
}

/*
 * function_iterate_do_value:
 *
 * Helper routine for function_iterate_argument_values.
 */
int
function_iterate_do_value (const EvalPosition      *ep,
			   FunctionIterateCallback callback,
			   void                    *closure,
			   Value                   *value,
			   ErrorMessage            *error,
			   gboolean                strict)
{
	int eval_col = ep->eval_col;
	int eval_row = ep->eval_row;
	int ret = TRUE;

	switch (value->type){
	case VALUE_INTEGER:
	case VALUE_FLOAT:
	case VALUE_STRING:
		ret = (*callback)(ep, value, error, closure);
			break;

	case VALUE_ARRAY:
	{
		int x, y;
		
		for (x = 0; x < value->v.array.x; x++) {
			for (y = 0; y < value->v.array.y; y++) {
				ret = function_iterate_do_value (
					ep, callback, closure,
					value->v.array.vals [x][y],
					error, strict);
				if (ret == FALSE)
					return FALSE;
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
		data.error    = error;
		data.strict   = strict;

		cell_get_abs_col_row (&value->v.cell_range.cell_a,
				      eval_col, eval_row,
				      &start_col, &start_row);

		cell_get_abs_col_row (&value->v.cell_range.cell_b,
				      eval_col, eval_row,
				      &end_col, &end_row);

		if (!(sheet = value->v.cell_range.cell_a.sheet))
			sheet = ep->sheet;
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
function_iterate_argument_values (const EvalPosition      *fp,
				  FunctionIterateCallback callback,
				  void                    *callback_closure,
				  GList                   *expr_node_list,
				  ErrorMessage            *error,
				  gboolean                strict)
{
	int result = TRUE;
	FunctionEvalInfo fs;

	for (; result && expr_node_list; expr_node_list = expr_node_list->next){
		ExprTree *tree = (ExprTree *) expr_node_list->data;
		Value *val;

		func_eval_info_pos (&fs, fp);
		error_message_free (fs.error);
		fs.error = error;
		val = eval_expr (&fs, tree);

		if (val) {
			result = function_iterate_do_value (
				fp, callback, callback_closure,
				val, error, strict);
			value_release (val);
		} else if (strict) {
			/* A strict function -- just short circuit.  */
			result = FALSE;
		} else {
			/* A non-strict function -- call the handler.  */
			result = (*callback) (fp, val, error, callback_closure);
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

/* Initialize temporarily with statics.  The real versions from the locale
 * will be setup in constants_init
 */
char *gnumeric_err_NULL  = "#NULL!";
char *gnumeric_err_DIV0  = "#DIV/0!";
char *gnumeric_err_VALUE = "#VALUE!";
char *gnumeric_err_REF   = "#REF!";
char *gnumeric_err_NAME  = "#NAME?";
char *gnumeric_err_NUM   = "#NUM!";
char *gnumeric_err_NA    = "#N/A";

void
constants_init (void)
{
	symbol_install (global_symbol_table, "FALSE", SYMBOL_VALUE,
			value_new_bool (FALSE));
	symbol_install (global_symbol_table, "TRUE", SYMBOL_VALUE,
			value_new_bool (TRUE));
	symbol_install (global_symbol_table, "GNUMERIC_VERSION", SYMBOL_VALUE,
			value_new_float (atof (GNUMERIC_VERSION)));

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
