/*
 * func.c: Function management and utility routines.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Michael Meeks   (mmeeks@gnu.org)
 *  Morten Welinder (terra@diku.dk)
 *  Jody Goldberg   (jgoldberg@home.org)
 */
#include <config.h>
#include <gnome.h>
#include <math.h>
#include "gnumeric.h"
#include "parse-util.h"
#include "func.h"
#include "eval.h"
#include "cell.h"
#include "str.h"
#include "symbol.h"
#include "workbook.h"
#include "sheet.h"

/* These are not supported yet */
typedef enum {
	FUNCTION_RETURNS_ARRAY = 0x01, /* eg transpose(), mmult() */
	FUNCTION_RECALC_ALWAYS = 0x02, /* eg now() */

	/* For functions that are not exactly compatible with various import
	 * formats.  We need to recalc their results to avoid changing values
	 * unexpectedly when we recalc later.  This probably needs to be done
	 * on a per import format basis.  It may not belong here.
	 */
	FUNCTION_RECALC_ONLOAD = 0x04

	/* TODO : Are there other forms or recalc we need to think about ? */
} FunctionFlags;

struct _FunctionDefinition {
	FunctionGetFullInfoCallback get_full_info_callback; 
	FunctionFlags flags;
	gchar   const *name;
	gchar   const *args;
	gchar   const *named_arguments;
	gchar        **help;
	FuncType       fn_type;
	union {
		FunctionNodes *fn_nodes;
		FunctionArgs  *fn_args;
	} fn;
	gpointer     user_data;
	gint         ref_count;
};

static GList *categories = NULL;
static SymbolTable *global_symbol_table = NULL;

extern void math_functions_init        (void);
extern void sheet_functions_init       (void);
extern void date_functions_init        (void);
extern void string_functions_init      (void);
extern void stat_functions_init        (void);
extern void finance_functions_init     (void);
extern void eng_functions_init         (void);
extern void lookup_functions_init      (void);
extern void logical_functions_init     (void);
extern void database_functions_init    (void);
extern void information_functions_init (void);

void
functions_init (void)
{
	global_symbol_table = symbol_table_new ();

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

static void
dump_func_help (gpointer key, gpointer value, void *output_file)
{
	Symbol *sym = value;
	FunctionDefinition *fn_def;

	if (sym->type != SYMBOL_FUNCTION)
		return;
	fn_def = sym->data;

	if (fn_def->help)
		fprintf (output_file, "%s\n\n", _( *(fn_def->help) ) );
}

void
function_dump_defs (const char *filename)
{
	FILE *output_file;

	g_return_if_fail (filename != NULL);

	if ((output_file = fopen (filename, "w")) == NULL){
		printf (_("Cannot create file %s\n"), filename);
		exit (1);
	}

	g_hash_table_foreach (global_symbol_table->hash,
			      &dump_func_help, output_file);

	fclose (output_file);
}
/* ------------------------------------------------------------------------- */

static gint
function_category_compare (gconstpointer a, gconstpointer b)
{
	FunctionCategory const *cat_a = a;
	FunctionCategory const *cat_b = b;

	g_return_val_if_fail (cat_a->display_name != NULL, 0);
	g_return_val_if_fail (cat_b->display_name != NULL, 0);

	return g_strcasecmp (cat_a->display_name->str, cat_b->display_name->str);
}

FunctionCategory *
function_get_category (gchar const *description)
{
	FunctionCategory *cat;
	GList            *l;

	g_return_val_if_fail (description != NULL, NULL);

	for (l = categories; l != NULL; l = l->next) {
		FunctionCategory *cat;

		cat = (FunctionCategory *) l->data;
		if (g_strcasecmp (cat->internal_name->str, description) == 0) {
			return cat;
		}
	}

	cat = g_new (FunctionCategory, 1);
	cat->internal_name = string_get (description);
	cat->display_name = cat->internal_name;
	string_ref (cat->display_name);
	cat->has_translation = FALSE;
	cat->functions = NULL;
	categories = g_list_insert_sorted (
	             categories, cat, &function_category_compare);

	return cat;
}

FunctionCategory *
function_get_category_with_translation (gchar const *description,
                                        gchar const *translation)
{
	FunctionCategory *cat;

	g_return_val_if_fail (description != NULL, NULL);

	cat = function_get_category (description);
	if (cat != NULL && !cat->has_translation &&
	    translation != NULL && translation != description) {
		string_unref (cat->display_name);
		cat->display_name = string_get (translation);
		cat->has_translation = TRUE;
	}

	return cat;
}

FunctionCategory *
function_category_get_nth (int n)
{
	return g_list_nth_data (categories, n);
}

void
function_category_add_func (FunctionCategory *category,
			    FunctionDefinition *fn_def)
{
	g_return_if_fail (category != NULL);
	g_return_if_fail (fn_def != NULL);

	category->functions = g_list_append (category->functions, fn_def);
}

static void
function_category_free (FunctionCategory *category)
{
	g_return_if_fail (category != NULL);
	g_return_if_fail (category->functions == NULL);

	string_unref (category->internal_name);
	string_unref (category->display_name);
	g_free (category);
}

void
function_category_remove_func (FunctionCategory *category,
                               FunctionDefinition *fn_def)
{
	g_return_if_fail (category != NULL);
	g_return_if_fail (fn_def != NULL);

	category->functions = g_list_remove (category->functions, fn_def);
	if (category->functions == NULL) {
		categories = g_list_remove (categories, category);
		function_category_free (category);
	}
}

/******************************************************************************/

static Value *
error_function_no_full_info (FunctionEvalInfo *ei, GList *expr_node_list)
{
	return value_new_error (ei->pos, _("Function implementation not available."));
}

static inline void
function_def_get_full_info_if_needed (FunctionDefinition *fn_def)
{
	if (fn_def->get_full_info_callback != NULL) {
		gchar *args, *arg_names, **help;
		FunctionArgs *fn_args;
		FunctionNodes *fn_nodes;
		gboolean success;

		success = fn_def->get_full_info_callback (fn_def,
		                                          &args, &arg_names, &help,
		                                          &fn_args, &fn_nodes);
		if (success) {
			fn_def->args = args;
			fn_def->named_arguments = arg_names;
			fn_def->help = help;
			if (fn_args != NULL) {
				fn_def->fn_type = FUNCTION_ARGS;
				fn_def->fn.fn_args = fn_args;
			} else if (fn_nodes != NULL) {
				fn_def->fn_type = FUNCTION_NODES;
				fn_def->fn.fn_nodes = fn_nodes;
			} else {
				g_assert_not_reached ();
			}
		} else {
			fn_def->args = "";
			fn_def->named_arguments = "";
			fn_def->fn_type = FUNCTION_NODES;
			fn_def->fn.fn_nodes = &error_function_no_full_info;
		}

		fn_def->get_full_info_callback = NULL;
	}
}

void
func_ref (FunctionDefinition *fn_def)
{
	g_return_if_fail (fn_def != NULL);

	fn_def->ref_count++;
}

void
func_unref (FunctionDefinition *fn_def)
{
	g_return_if_fail (fn_def != NULL);
	g_return_if_fail (fn_def->ref_count > 0);

	fn_def->ref_count--;
}

gint
func_get_ref_count (FunctionDefinition *fn_def)
{
	g_return_val_if_fail (fn_def != NULL, 0);

	return fn_def->ref_count;
}

FunctionDefinition *
func_lookup_by_name (gchar const *fn_name, Workbook const *optional_scope)
{
	Symbol *sym;
	
	sym = symbol_lookup (global_symbol_table, fn_name);
	if (sym != NULL) {
		return sym->data;
	}

	return NULL;
}

void
function_remove (FunctionCategory *category, gchar const *name)
{
	FunctionDefinition *fn_def;
	Symbol *sym;

	g_return_if_fail (name != NULL);

	fn_def = func_lookup_by_name (name, NULL);
	g_return_if_fail (fn_def->ref_count == 0);
	function_category_remove_func (category, fn_def);
	sym = symbol_lookup (global_symbol_table, name);
	symbol_unref (sym);
	g_free (fn_def);
}

static FunctionDefinition *
fn_def_new (FunctionCategory *category,
	    char const *name,
	    char const *args,
	    char const *arg_names,
	    char **help)
{
	static char const valid_tokens[] = "fsbraAS?|";
	char const *ptr;
	FunctionDefinition *fn_def;

	/* Check those arguements */
	if (args)
		for (ptr = args ; *ptr ; ptr++) {
			g_return_val_if_fail (strchr (valid_tokens, *ptr), NULL);
		}

	fn_def = g_new (FunctionDefinition, 1);
	fn_def->get_full_info_callback = NULL;
	fn_def->flags	 = 0;
	fn_def->name      = name;
	fn_def->args      = args;
	fn_def->help      = help;
	fn_def->named_arguments = arg_names;
	fn_def->user_data = NULL;
	fn_def->ref_count = 0;

	if (category != NULL)
		function_category_add_func (category, fn_def);
	symbol_install (global_symbol_table, name, SYMBOL_FUNCTION, fn_def);

	return fn_def;
}

FunctionDefinition *
function_add_args (FunctionCategory *category,
		   char const *name,
		   char const *args,
		   char const *arg_names,
		   char **help,
		   FunctionArgs *fn)
{
	FunctionDefinition *fn_def;

	g_return_val_if_fail (fn != NULL, NULL);

	fn_def = fn_def_new (category, name, args, arg_names, help);
	if (fn_def != NULL) {
		fn_def->fn_type    = FUNCTION_ARGS;
		fn_def->fn.fn_args = fn;
	}
	return fn_def;
}

FunctionDefinition *
function_add_nodes (FunctionCategory *category,
		    char const *name,
		    char const *args,
		    char const *arg_names,
		    char **help,
		    FunctionNodes *fn)
{
	FunctionDefinition *fn_def;

	g_return_val_if_fail (fn != NULL, NULL);

	fn_def = fn_def_new (category, name, args, arg_names, help);
	if (fn_def != NULL) {
		fn_def->fn_type     = FUNCTION_NODES;
		fn_def->fn.fn_nodes = fn;
	}
	return fn_def;
}

FunctionDefinition *
function_add_name_only (FunctionCategory *category,
                        gchar const *name,
                        FunctionGetFullInfoCallback callback)
{
	FunctionDefinition *fn_def;

	fn_def = fn_def_new (category, name, NULL, NULL, NULL);
	if (fn_def != NULL) {
		fn_def->get_full_info_callback = callback;
	}

	return fn_def;
}

/* Handle unknown functions on import without losing their names */
static Value *
unknownFunctionHandler (FunctionEvalInfo *ei, GList *expr_node_list)
{
	return value_new_error (ei->pos, gnumeric_err_NAME);
}

/*
 * When importing it is useful to keep track of unknown function names.
 * We may be missing a plugin or something similar.
 *
 * TODO : Eventully we should be able to keep track of these
 *        and replace them with something else.  Possibly even reordering the
 *        arguments.
 */
FunctionDefinition *
function_add_placeholder (char const *name, char const *type)
{
	FunctionCategory *cat;
	FunctionDefinition *func = func_lookup_by_name (name, NULL);

	g_return_val_if_fail (func == NULL, func);

	cat = function_get_category_with_translation ("Unknown Function", _("Unknown Function"));

	/*
	 * TODO TODO TODO : should add a
	 *    function_add_{nodes,args}_fake
	 * This will allow a user to load a missing
	 * plugin to supply missing functions.
	 */
	func = function_add_nodes (cat, g_strdup (name),
				   "", "...", NULL,
				   &unknownFunctionHandler);

	/* WISHLIST : it would be nice to have a log if these. */
	g_warning ("Unknown %sfunction : %s", type, name);

	return func;
}

gpointer
function_def_get_user_data (const FunctionDefinition *fn_def)
{
	g_return_val_if_fail (fn_def != NULL, NULL);

	return fn_def->user_data;
}

void
function_def_set_user_data (FunctionDefinition *fn_def,
			    gpointer user_data)
{
	g_return_if_fail (fn_def != NULL);

	fn_def->user_data = user_data;
}

const char *
function_def_get_name (FunctionDefinition const *fn_def)
{
	g_return_val_if_fail (fn_def != NULL, NULL);

	return fn_def->name;
}

/**
 * function_def_count_args:
 * @fn_def: pointer to function definition
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
void
function_def_count_args (FunctionDefinition const *fn_def,
                         int *min, int *max)
{
	const char *ptr;
	int   i;
	int   vararg;

	g_return_if_fail (min != NULL);
	g_return_if_fail (max != NULL);
	g_return_if_fail (fn_def != NULL);

	function_def_get_full_info_if_needed ((FunctionDefinition *) fn_def);

	ptr = fn_def->args;

	/*
	 * FIXME: clearly for 'nodes' functions many of
	 * the type fields will need to be filled.
	 */
	if (fn_def->fn_type == FUNCTION_NODES && ptr == NULL) {
		*min = 0;
		*max = G_MAXINT;
		return;
	}

	for (i = vararg = 0; ptr && *ptr; ptr++) {
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
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Return value: the type of the argument
 **/
char
function_def_get_arg_type (FunctionDefinition const *fn_def,
                           int arg_idx)
{
	const char *ptr;

	g_return_val_if_fail (arg_idx >= 0, '?');
	g_return_val_if_fail (fn_def != NULL, '?');

	function_def_get_full_info_if_needed ((FunctionDefinition *) fn_def);

	for (ptr = fn_def->args; ptr && *ptr; ptr++) {
		if (*ptr == '|')
			continue;
		if (arg_idx-- == 0)
			return *ptr;
	}
	return '?';
}

/* ------------------------------------------------------------------------- */

static inline Value *
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
	if (t->any.oper == OPER_VAR &&
	    (arg_type == 'A' ||
	     arg_type == 'r'))
		v = value_new_cellrange (&t->var.ref, &t->var.ref,
					 ei->pos->eval.col,
					 ei->pos->eval.row);
	else
		/* force scalars whenever we are certain */
		v = eval_expr (ei->pos, t,
			       (arg_type == 'r' || arg_type == 'a' ||
				arg_type == 'A' || arg_type == '?')
			       ? EVAL_PERMIT_NON_SCALAR : EVAL_STRICT);

	switch (arg_type) {

	case 'f':
	case 'b':
		if (v->type == VALUE_CELLRANGE) {
			v = expr_implicit_intersection (ei->pos, v);
			if (v == NULL)
				break;
		} else if (v->type == VALUE_ARRAY) {
			v = expr_array_intersection (v);
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
		} else if (v->type == VALUE_ARRAY) {
			v = expr_array_intersection (v);
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
			cell_ref_make_abs (&v->v_range.cell.a,
					   &v->v_range.cell.a,
					   ei->pos);
			cell_ref_make_abs (&v->v_range.cell.b,
					   &v->v_range.cell.b,
					   ei->pos);
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
			cell_ref_make_abs (&v->v_range.cell.a,
					   &v->v_range.cell.a,
					   ei->pos);
			cell_ref_make_abs (&v->v_range.cell.b,
					   &v->v_range.cell.b,
					   ei->pos);
		}
		break;

	case 'S':
		if (v->type == VALUE_CELLRANGE) {
			v = expr_implicit_intersection (ei->pos, v);
			if (v == NULL)
				break;
		} else if (v->type == VALUE_ARRAY) {
			v = expr_array_intersection (v);
			if (v == NULL)
				break;
		}
		break;

	default :
		break;
	}

	return v;
}

static inline void
free_values (Value **values, int top)
{
	int i;

	for (i = 0; i < top; i++)
		if (values [i])
			value_release (values [i]);
	g_free (values);
}

/**
 * function_call_with_list:
 * @ei: EvalInfo containing valid fn_def!
 * @args: GList of ExprTree args.
 *
 * Do the guts of calling a function.
 *
 * Return value:
 **/
Value *
function_call_with_list (FunctionEvalInfo *ei, GList *l)
{
	FunctionDefinition const *fn_def;
	int argc, arg;
	Value *v = NULL;
	Value **values;
	int fn_argc_min = 0, fn_argc_max = 0;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_def != NULL, NULL);

	function_def_get_full_info_if_needed ((FunctionDefinition *) ei->func_def);

	/* Functions that deal with ExprNodes */
	fn_def = ei->func_def;
	if (fn_def->fn_type == FUNCTION_NODES)
		return fn_def->fn.fn_nodes (ei, l);

	/* Functions that take pre-computed Values */
	argc = g_list_length (l);
	function_def_count_args (fn_def, &fn_argc_min,
	                         &fn_argc_max);

	if (argc > fn_argc_max || argc < fn_argc_min)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	values = g_new (Value *, fn_argc_max);

	for (arg = 0; l; l = l->next, ++arg) {
		char     arg_type;
		gboolean type_mismatch;

		arg_type = function_def_get_arg_type (fn_def, arg);

		values [arg] = function_marshal_arg (ei, l->data, arg_type,
						     &type_mismatch);

		if (type_mismatch || values [arg] == NULL) {
			free_values (values, arg + 1);
			return value_new_error (ei->pos, gnumeric_err_VALUE);
		}
	}
	while (arg < fn_argc_max)
		values [arg++] = NULL;
	v = fn_def->fn.fn_args (ei, values);

	free_values (values, arg);
	return v;
}

/*
 * Use this to invoke a register function: the only drawback is that
 * you have to compute/expand all of the values to use this
 */
Value *
function_call_with_values (const EvalPos *ep, const char *fn_name,
			   int argc, Value *values [])
{
	FunctionDefinition *fn_def;

	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (fn_name != NULL, NULL);
	g_return_val_if_fail (ep->sheet != NULL, NULL);

	/* FIXME : support workbook local functions */
	fn_def = func_lookup_by_name (fn_name, NULL);
	if (fn_def == NULL)
		return value_new_error (ep, _("Function does not exist"));
	return function_def_call_with_values (ep, fn_def, argc, values);
}

Value *
function_def_call_with_values (EvalPos const *ep,
                               FunctionDefinition const *fn_def,
                               gint                argc,
                               Value              *values [])
{
	Value *retval;
	FunctionEvalInfo fs;

	function_def_get_full_info_if_needed ((FunctionDefinition *) fn_def);

	fs.pos = ep;
	fs.func_def = fn_def;

	if (fn_def->fn_type == FUNCTION_NODES) {
		/*
		 * If function deals with ExprNodes, create some
		 * temporary ExprNodes with constants.
		 */
		ExprConstant *tree = NULL;
		GList *l = NULL;
		int i;

		if (argc) {
			tree = g_new (ExprConstant, argc);

			for (i = 0; i < argc; i++) {
				/* FIXME : this looks like a leak */
				*((Operation *)&(tree [i].oper)) = OPER_CONSTANT;
				tree [i].ref_count = 1;
				tree [i].value = values [i];

				l = g_list_append (l, &(tree [i]));
			}
		}

		retval = fn_def->fn.fn_nodes (&fs, l);

		if (tree) {
			g_free (tree);
			g_list_free (l);
		}

	} else
		retval = fn_def->fn.fn_args (&fs, values);

	return retval;
}

/* ------------------------------------------------------------------------- */

typedef struct {
	FunctionIterateCB  callback;
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
	EvalPos ep;
	Value *res;

	if (cell == NULL) {
		ep.sheet = sheet;
		ep.eval.col = col;
		ep.eval.row = row;
		return (*data->callback)(&ep, NULL, data->closure);
	}

	if (cell->base.generation != sheet->workbook->generation)
		cell_eval (cell);

	/* If we encounter an error for the strict case, short-circuit here.  */
	if (data->strict && (NULL != (res = cell_is_error (cell))))
		return res;

	/* All other cases -- including error -- just call the handler.  */
	return (*data->callback)(eval_pos_init_cell (&ep, cell),
				 cell->value, data->closure);
}

/*
 * function_iterate_do_value:
 *
 * Helper routine for function_iterate_argument_values.
 */
Value *
function_iterate_do_value (EvalPos      const *ep,
			   FunctionIterateCB  callback,
			   void                    *closure,
			   Value                   *value,
			   gboolean                 strict,
			   gboolean		   ignore_blank)
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

		/* Note the order here.  */
		for (y = 0; y < value->v_array.y; y++) {
			  for (x = 0; x < value->v_array.x; x++) {
				res = function_iterate_do_value (
					ep, callback, closure,
					value->v_array.vals [x][y],
					strict, TRUE);
				if (res != NULL)
					return res;
			}
		}
		break;
	}
	case VALUE_CELLRANGE: {
		IterateCallbackClosure data;

		data.callback = callback;
		data.closure  = closure;
		data.strict   = strict;

		res = workbook_foreach_cell_in_range (ep, value, ignore_blank,
						      iterate_cellrange_callback,
						      &data);
	}
	}
	return res;
}

/*
 * function_iterate_argument_values
 *
 * @fp:               The position in a workbook at which to evaluate
 * @callback:         The routine to be invoked for every value computed
 * @callback_closure: Closure for the callback.
 * @expr_node_list:   a GList of ExprTrees (what a Gnumeric function would get).
 * @strict:           If TRUE, the function is considered "strict".  This means
 *                   that if an error value occurs as an argument, the iteration
 *                   will stop and that error will be returned.  If FALSE, an
 *                   error will be passed on to the callback (as a Value *
 *                   of type VALUE_ERROR).
 * @ignore_blank:    If TRUE blanks will not be passed to the callback.
 *
 * Return value:
 *    NULL            : if no errors were reported.
 *    Value *         : if an error was found during strict evaluation
 *    value_terminate : if the callback requested termination of the iteration.
 *
 * This routine provides a simple way for internal functions with variable
 * number of arguments to be written: this would iterate over a list of
 * expressions (expr_node_list) and will invoke the callback for every
 * Value found on the list (this means that ranges get properly expaned).
 */
Value *
function_iterate_argument_values (const EvalPos      *ep,
				  FunctionIterateCB callback,
				  void                    *callback_closure,
				  GList                   *expr_node_list,
				  gboolean                strict,
				  gboolean		  ignore_blank)
{
	Value * result = NULL;

	for (; result == NULL && expr_node_list;
	     expr_node_list = expr_node_list->next){
		ExprTree const * tree = (ExprTree const *) expr_node_list->data;
		Value *val;

		/* Permit empties and non scalars. We don't know what form the
		 * function wants its arguments */
		val = eval_expr (ep, tree, EVAL_PERMIT_NON_SCALAR|EVAL_PERMIT_EMPTY);

		if (val == NULL)
			continue;

		if (strict && val->type == VALUE_ERROR) {
			/* A strict function with an error */
			/* FIXME : Make the new position of the error here */
			return val;
		}

		result = function_iterate_do_value (ep, callback, callback_closure,
						    val, strict, ignore_blank);
		value_release (val);
	}
	return result;
}

/* ------------------------------------------------------------------------- */

TokenizedHelp *
tokenized_help_new (FunctionDefinition const *fn_def)
{
	TokenizedHelp *tok;

	g_return_val_if_fail (fn_def != NULL, NULL);

	function_def_get_full_info_if_needed ((FunctionDefinition *) fn_def);

	tok = g_new (TokenizedHelp, 1);

	tok->fndef = fn_def;

	if (fn_def->help && fn_def->help [0]){
		char *ptr;
		int seek_att = 1;
		int last_newline = 1;

		tok->help_copy = g_strdup (_(fn_def->help [0]));
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

	for (lp = 0; lp + 1 < (int)tok->sections->len; lp++) {
		const char *cmp = g_ptr_array_index (tok->sections, lp);

		if (g_strcasecmp (cmp, token) == 0){
			return g_ptr_array_index (tok->sections, lp + 1);
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
