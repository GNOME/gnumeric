/*
 * func.c:  Built in mathematical functions and functions registration
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Michael Meeks   (mmeeks@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <math.h>
#include "gnumeric.h"
#include "utils.h"
#include "func.h"
#include "eval.h"
#include "workbook.h"

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

	if (cell->generation != sheet->workbook->generation)
		cell_eval (cell);

	/* If we encounter an error for the strict case, short-circuit here.  */
	if (data->strict && (NULL != (res = cell_is_error (cell))))
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
function_iterate_do_value (EvalPosition      const *ep,
			   FunctionIterateCallback  callback,
			   void                    *closure,
			   Value                   *value,
			   gboolean                 strict)
{
	Value *res = NULL;

	switch (value->type){
	case VALUE_ERROR:
		if (strict) {
			res = value_duplicate (value);
			break;
		}
		/* Fall through.  */

	case VALUE_EMPTY:
	case VALUE_BOOLEAN:
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

		cell_get_abs_col_row (&value->v.cell_range.cell_a, &ep->eval,
				      &start_col, &start_row);

		cell_get_abs_col_row (&value->v.cell_range.cell_b, &ep->eval,
				      &end_col, &end_row);

		sheet = eval_sheet (value->v.cell_range.cell_a.sheet, ep->sheet);
		res = sheet_cell_foreach_range (
			sheet, TRUE,
			start_col, start_row,
			end_col, end_row,
			iterate_cellrange_callback,
			&data);
	}
	}
	return res;
}

Value *
function_iterate_argument_values (const EvalPosition      *ep,
				  FunctionIterateCallback callback,
				  void                    *callback_closure,
				  GList                   *expr_node_list,
				  gboolean                strict)
{
	Value * result = NULL;

	for (; result == NULL && expr_node_list;
	     expr_node_list = expr_node_list->next){
		ExprTree const * tree = (ExprTree const *) expr_node_list->data;
		Value *val;

		val = eval_expr_empty (ep, tree);

		if (val == NULL)
			continue;

		if (strict && val->type == VALUE_ERROR) {
			/* A strict function with an error */
			/* FIXME : Make the new position of the error here */
			return val;
		}

		result = function_iterate_do_value (ep, callback, callback_closure,
						    val, strict);
		value_release (val);
	}
	return result;
}

/* ------------------------------------------------------------------------- */

struct _FunctionDefinition {
	char  const *name;
	char  const *args;
	char  const *named_arguments;
	char       **help;
	FuncType     fn_type;
	union {
		FunctionNodes *fn_nodes;
		FunctionArgs  *fn_args;
	} fn;
	gpointer     user_data;
};

/**
 * function_def_get_fn:
 * @fn_def: the function definition
 * 
 * This returns a pointer to the handler of this function
 * this is so that the same C function can map to two 
 * slightly different gnumeric functions.
 * 
 * Return value: function pointer.
 **/
gpointer
function_def_get_fn (FunctionDefinition *fndef)
{
	g_return_val_if_fail (fndef != NULL, NULL);

	if (fndef->fn_type == FUNCTION_NODES)
		return fndef->fn.fn_nodes;
	else if (fndef->fn_type == FUNCTION_ARGS)
		return fndef->fn.fn_args;

	g_warning ("Unknown function type");
	return NULL;
}

gpointer
function_def_get_user_data (FunctionDefinition *fndef)
{
	g_return_val_if_fail (fndef != NULL, NULL);

	return fndef->user_data;
}

void
function_def_set_user_data (FunctionDefinition *fndef,
			    gpointer user_data)
{
	g_return_if_fail (fndef != NULL);
	
	fndef->user_data = user_data;
}

const char *
function_def_get_name (FunctionDefinition *fndef)
{
	g_return_val_if_fail (fndef != NULL, NULL);

	return fndef->name;
}

/**
 * function_def_count_args:
 * @fndef: pointer to function definition
 * @min: pointer to min. args
 * @max: pointer to max. args
 * 
 * This calculates the max and min args that
 * can be passed; NB max can be G_MAXINT for
 * a vararg function.
 * NB. this data is not authoratitive for a
 * 'nodes' function.
 * 
 **/
inline void
function_def_count_args (FunctionDefinition const *fndef,
			 int *min, int *max)
{
	const char *ptr;
	int   i;
	int   vararg;

	g_return_if_fail (min != NULL);
	g_return_if_fail (max != NULL);
	g_return_if_fail (fndef != NULL);

	/*
	 * FIXME: clearly for 'nodes' functions many of
	 * the type fields will need to be filled.
	 */
	if (fndef->args == NULL) {
		*min = 1;
		*max = G_MAXINT;
	}

	i = vararg = 0;
	for (ptr = fndef->args; *ptr; ptr++) {
		if (*ptr == '|') {
			vararg = 1;
			*min = i;
		} else
			i++;
	}
	*max = i;
	if (!vararg)
		*min = i;
}

/**
 * function_def_get_arg_type:
 * @fndef: the fn defintion
 * @arg_idx: zero based argument offset
 * 
 * Return value: the type of the argument
 **/
inline char
function_def_get_arg_type (FunctionDefinition const *fndef,
			   int arg_idx)
{
	const char *ptr;

	g_return_val_if_fail (arg_idx >= 0, '?');
	g_return_val_if_fail (fndef != NULL, '?');

	for (ptr = fndef->args; *ptr; ptr++) {
		if (*ptr == '|')
			continue;
		if (arg_idx-- == 0)
			return *ptr;
	}
	return '?';
}

/* ------------------------------------------------------------------------- */

inline static void
free_values (Value **values, int top)
{
	int i;

	for (i = 0; i < top; i++)
		if (values [i])
			value_release (values [i]);
	g_free (values);
}

static void
cell_ref_make_absolute (CellRef *cell_ref,
			const EvalPosition *ep)
{
	g_return_if_fail (cell_ref != NULL);

	if (cell_ref->col_relative)
		cell_ref->col = ep->eval.col + cell_ref->col;

	if (cell_ref->row_relative)
		cell_ref->row = ep->eval.row + cell_ref->row;

	cell_ref->row_relative = 0;
	cell_ref->col_relative = 0;
}

inline static Value *
function_marshal_arg (FunctionEvalInfo *ei,
		      ExprTree         *t,
		      char              arg_type,
		      gboolean         *type_mismatch)
{
	Value *v;

	*type_mismatch = FALSE;

	/*
	 *  This is so we don't dereference 'A1' by accident
	 * when we want a range instead.
	 */
	if (t->oper == OPER_VAR &&
	    (arg_type == 'A' ||
	     arg_type == 'r'))
		v = value_new_cellrange (&t->u.ref, &t->u.ref);
	else
		v = eval_expr (ei->pos, t);
		
	switch (arg_type) {

	case 'f':
	case 'b':
		if (v->type == VALUE_CELLRANGE) {
			v = expr_implicit_intersection (ei->pos, v);
			if (v == NULL)
				break;
		}

		if (v->type != VALUE_INTEGER &&
		    v->type != VALUE_FLOAT &&
		    v->type != VALUE_BOOLEAN)
			*type_mismatch = TRUE;
		break;

	case 's':
		if (v->type == VALUE_CELLRANGE) {
			v = expr_implicit_intersection (ei->pos, v);
			if (v == NULL)
				break;
		}

		if (v->type != VALUE_STRING)
			*type_mismatch = TRUE;
		break;

	case 'r':
		if (v->type != VALUE_CELLRANGE)
			*type_mismatch = TRUE;
		else {
			cell_ref_make_absolute (&v->v.cell_range.cell_a, ei->pos);
			cell_ref_make_absolute (&v->v.cell_range.cell_b, ei->pos);
		}
		break;

	case 'a':
		if (v->type != VALUE_ARRAY)
			*type_mismatch = TRUE;
		break;

	case 'A':
		if (v->type != VALUE_ARRAY &&
		    v->type != VALUE_CELLRANGE)
			*type_mismatch = TRUE;
			
		if (v->type == VALUE_CELLRANGE) {
			cell_ref_make_absolute (&v->v.cell_range.cell_a, ei->pos);
			cell_ref_make_absolute (&v->v.cell_range.cell_b, ei->pos);
		}
		break;
	}

	return v;
}
/**
 * function_call_with_list:
 * @ei: EvalInfo containing valid fd!
 * @args: GList of ExprTree args.
 * 
 * Do the guts of calling a function.
 * 
 * Return value: 
 **/
Value *
function_call_with_list (FunctionEvalInfo        *ei,
			 GList                   *l)
{
	FunctionDefinition const *fd;
	int argc, arg;
	Value *v = NULL;
	Value **values;
	int fn_argc_min = 0, fn_argc_max = 0;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_def != NULL, NULL);

	/* Functions that deal with ExprNodes */		
	fd = ei->func_def;
	if (fd->fn_type == FUNCTION_NODES)
	        return fd->fn.fn_nodes (ei, l);
	
	/* Functions that take pre-computed Values */
	argc = g_list_length (l);
	function_def_count_args (fd, &fn_argc_min,
				 &fn_argc_max);

	if (argc > fn_argc_max || argc < fn_argc_min)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	values = g_new (Value *, fn_argc_max);

	for (arg = 0; l; l = l->next, ++arg) {
		char     arg_type;
		gboolean type_mismatch;

		arg_type = function_def_get_arg_type (fd, arg);

		values [arg] = function_marshal_arg (ei, l->data, arg_type,
						     &type_mismatch);

		if (type_mismatch || values [arg] == NULL) {
			free_values (values, arg + 1);
			return value_new_error (ei->pos, gnumeric_err_VALUE);
		}
	}
	while (arg < fn_argc_max)
		values [arg++] = NULL;
	v = fd->fn.fn_args (ei, values);
	
	free_values (values, arg);
	return v;	
}

/* ------------------------------------------------------------------------- */

GList *
function_categories_get (void)
{
	return categories;
}

TokenizedHelp *
tokenized_help_new (FunctionDefinition *fndef)
{
	TokenizedHelp *tok;

	g_return_val_if_fail (fndef != NULL, NULL);

	tok = g_new (TokenizedHelp, 1);

	tok->fndef = fndef;

	if (fndef->help && fndef->help [0]){
		char *ptr;
		int seek_att = 1;
		int last_newline = 1;

		tok->help_copy = g_strdup (fndef->help [0]);
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

	for (lp = 0; lp < tok->sections->len-1; lp++) {
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
		g_ptr_array_free (tok->sections, TRUE);

	g_free (tok);
}

static gint
function_category_compare (gconstpointer a, gconstpointer b)
{
	FunctionCategory const *cat_a = a;
	FunctionCategory const *cat_b = b;

	return g_strcasecmp (cat_a->name, cat_b->name);
}

FunctionCategory *
function_get_category (gchar const *description)
{
	FunctionCategory *cat;
	FunctionCategory  tmp;

	g_return_val_if_fail (description != NULL, NULL);

	tmp.name = description;
       	cat = (FunctionCategory *)
		g_list_find_custom (categories, &tmp,
				    &function_category_compare);
	
	if (cat != NULL)
		return cat;

       	cat = g_new (FunctionCategory, 1);
	cat->name = description;
	cat->functions = NULL;
	categories = g_list_insert_sorted (
		categories, cat, &function_category_compare);

	return cat;
}

static void
fn_def_init (FunctionDefinition *fndef,
	     char const *name, char const *args, char const *arg_names, char **help)
{
	int lp, lp2;
	char valid_tokens[] = "fsbraA?|";

	g_return_if_fail (fndef != NULL);

	/* Check those arguements */
	if (args) {
		int lena = strlen (args);
		int lenb = strlen (valid_tokens);
		for (lp = 0; lp < lena; lp++) {
			int ok = 0;
			for (lp2 = 0; lp2 < lenb; lp2++)
				if (valid_tokens [lp2] == args [lp])
					ok = 1;
			g_return_if_fail (ok);
		}
	}

	fndef->name      = name;
	fndef->args      = args;
	fndef->help      = help;
	fndef->named_arguments = arg_names;
	fndef->user_data = NULL;

	symbol_install (global_symbol_table, name,
			SYMBOL_FUNCTION, fndef);
}

FunctionDefinition *
function_add_nodes (FunctionCategory *parent,
		    char const *name,
		    char const *args,
		    char const *arg_names,
		    char **help,
		    FunctionNodes *fn)
{
	FunctionDefinition *fndef;

	g_return_val_if_fail (fn != NULL, NULL);
	g_return_val_if_fail (parent != NULL, NULL);

	fndef = g_new (FunctionDefinition, 1);
	fn_def_init (fndef, name, args, arg_names, help);

	fndef->fn_type     = FUNCTION_NODES;
	fndef->fn.fn_nodes = fn;
	parent->functions = g_list_append (parent->functions, fndef);
	return fndef;
}

FunctionDefinition *
function_add_args (FunctionCategory *parent,
		   char const *name,
		   char const *args,
		   char const *arg_names,
		   char **help,
		   FunctionArgs *fn)
{
	FunctionDefinition *fndef;

	g_return_val_if_fail (fn != NULL, NULL);
	g_return_val_if_fail (parent != NULL, NULL);

	fndef = g_new (FunctionDefinition, 1);
	fn_def_init (fndef, name, args, arg_names, help);

	fndef->fn_type    = FUNCTION_ARGS;
	fndef->fn.fn_args = fn;
	parent->functions = g_list_append (parent->functions, fndef);
	return fndef;
}

Value *
function_def_call_with_values (const EvalPosition *ep,
			       FunctionDefinition *fndef,
			       int                 argc,
			       Value              *values [])
{
	Value *retval;
	FunctionEvalInfo fs;

	fs.pos = ep;
	fs.func_def = fndef;

	if (fndef->fn_type == FUNCTION_NODES) {
		/*
		 * If function deals with ExprNodes, create some
		 * temporary ExprNodes with constants.
		 */
		ExprTree *tree = NULL;
		GList *l = NULL;
		int i;

		if (argc) {
			tree = g_new (ExprTree, argc);

			for (i = 0; i < argc; i++) {
				tree [i].oper = OPER_CONSTANT;
				tree [i].ref_count = 1;
				tree [i].u.constant = values [i];

				l = g_list_append (l, &(tree [i]));
			}
		}

		retval = fndef->fn.fn_nodes (&fs, l);

		if (tree) {
			g_free (tree);
			g_list_free (l);
		}

	} else
		retval = fndef->fn.fn_args (&fs, values);

	return retval;
}

/*
 * Use this to invoke a register function: the only drawback is that
 * you have to compute/expand all of the values to use this
 */
Value *
function_call_with_values (const EvalPosition *ep, const char *name,
			   int argc, Value *values [])
{
	FunctionDefinition *fndef;
	Value *retval;
	Symbol *sym;

	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (ep->sheet != NULL, NULL);

	sym = symbol_lookup (global_symbol_table, name);
	if (sym == NULL)
		return value_new_error (ep, _("Function does not exist"));
	if (sym->type != SYMBOL_FUNCTION)
		return value_new_error (ep, _("Calling non-function"));

	fndef = sym->data;

	symbol_ref (sym);
	retval = function_def_call_with_values (ep, fndef, argc, values);
	
	symbol_unref (sym);

	return retval;
}

void
functions_init (void)
{
	math_functions_init ();
	sheet_functions_init ();
	date_functions_init ();
	string_functions_init ();
	stat_functions_init ();
	finance_functions_init ();
	eng_functions_init ();
	lookup_functions_init ();
	logical_functions_init ();
	database_functions_init ();
	information_functions_init ();
}

/* ------------------------------------------------------------------------- */

static FILE *output_file;

static void
dump_func_help (gpointer key, gpointer value, gpointer user_data)
{
	Symbol *sym = value;
	FunctionDefinition *fd;
	
	if (sym->type != SYMBOL_FUNCTION)
		return;
	fd = sym->data;

	if (fd->help)
		fprintf (output_file, "%s\n\n", _( *(fd->help) ) );
}

void
function_dump_defs (const char *filename)
{
	g_return_if_fail (filename != NULL);
	
	if ((output_file = fopen (filename, "w")) == NULL){
		printf (_("Can not create file %s\n"), filename);
		exit (1);
	}

	g_hash_table_foreach (global_symbol_table->hash, dump_func_help, NULL);

	fclose (output_file);
}

gboolean
function_is_unused (FunctionDefinition *fndef)
{
	Symbol *sym;

	g_return_val_if_fail (fndef != NULL, FALSE);
	g_return_val_if_fail (fndef->name != NULL, FALSE);
	
	sym = symbol_lookup (global_symbol_table, fndef->name);

	if (!sym)
		return FALSE;

	return symbol_is_unused (sym);
}

void
function_remove (FunctionCategory   *parent,
		 FunctionDefinition *fndef)
{
	Symbol *sym;

	g_return_if_fail (fndef != NULL);
	g_return_if_fail (parent != NULL);
	g_return_if_fail (function_is_unused (fndef));

	sym = symbol_lookup (global_symbol_table, fndef->name);
	g_return_if_fail (sym != NULL);

	symbol_remove (global_symbol_table, sym);
	parent->functions = g_list_remove (parent->functions, fndef);

	fndef->fn_type = -1;
	g_free (fndef);
}

