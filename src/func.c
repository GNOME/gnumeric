/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * func.c: Function management and utility routines.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Michael Meeks   (mmeeks@gnu.org)
 *  Morten Welinder (terra@diku.dk)
 *  Jody Goldberg   (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "func.h"

#include "parse-util.h"
#include "eval.h"
#include "cell.h"
#include "str.h"
#include "symbol.h"
#include "workbook.h"
#include "sheet.h"
#include "value.h"
#include "expr.h"
#include "number-match.h"
#include "format.h"

#include <string.h>
#include <glib.h>
#include <libgnome/gnome-i18n.h>
#include <stdlib.h>
#include <ctype.h>

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
copy_hash_table_to_ptr_array (gpointer key, gpointer value, gpointer array)
{
	Symbol *sym = value;
	FunctionDefinition *fd = sym->data;
	if (sym->type == SYMBOL_FUNCTION && fd->name != NULL)
		g_ptr_array_add (array, fd);
}

static int
func_def_cmp (gconstpointer a, gconstpointer b)
{
	FunctionDefinition const *fda = *(FunctionDefinition const **)a ;
	FunctionDefinition const *fdb = *(FunctionDefinition const **)b ;

	return g_strcasecmp (fda->name, fdb->name);
}

void
function_dump_defs (char const *filename)
{
	FILE *output_file;
	unsigned i;
	GPtrArray *ordered;

	g_return_if_fail (filename != NULL);

	if ((output_file = fopen (filename, "w")) == NULL){
		printf (_("Cannot create file %s\n"), filename);
		exit (1);
	}

	/* TODO : Use the translated names and split by category. */
	ordered = g_ptr_array_new ();
	g_hash_table_foreach (global_symbol_table->hash,
		copy_hash_table_to_ptr_array, ordered);

	if (ordered->len > 0)
		qsort (&g_ptr_array_index (ordered, 0),
		       ordered->len, sizeof (gpointer),
		       func_def_cmp);

	for (i = 0; i < ordered->len; i++) {
		FunctionDefinition const *fd = g_ptr_array_index (ordered, i);
		if (fd->fn_type == FUNCTION_NAMEONLY)
			func_def_load ((FunctionDefinition *) fd);
		if (fd->help)
			fprintf (output_file, "%s\n\n", _( *(fd->help) ) );
	}

	g_ptr_array_free (ordered,TRUE);
	fclose (output_file);
}

/* ------------------------------------------------------------------------- */

static gint
function_category_compare (gconstpointer a, gconstpointer b)
{
	FunctionCategory const *cat_a = a;
	FunctionCategory const *cat_b = b;
	gchar *str_a, *str_b;

	g_return_val_if_fail (cat_a->display_name != NULL, 0);
	g_return_val_if_fail (cat_b->display_name != NULL, 0);

	str_a = g_alloca (strlen (cat_a->display_name->str) + 1);
	str_b = g_alloca (strlen (cat_b->display_name->str) + 1);
	g_strdown (strcpy (str_a, cat_a->display_name->str));
	g_strdown (strcpy (str_b, cat_b->display_name->str));

	return strcoll (str_a, str_b);
}

FunctionCategory *
function_get_category (gchar const *name)
{
	return function_get_category_with_translation (name, NULL);
}

FunctionCategory *
function_get_category_with_translation (gchar const *name,
                                        gchar const *translation)
{
	FunctionCategory *cat = NULL;
	gchar *int_name;
	GList *l;

	g_return_val_if_fail (name != NULL, NULL);

	int_name = g_alloca (strlen (name) + 1);
	g_strdown (strcpy (int_name, name));
	for (l = categories; l != NULL; l = l->next) {
		cat = l->data;
		if (strcmp (cat->internal_name->str, int_name) == 0) {
			break;
		}
	}

	if (l == NULL) {
		cat = g_new (FunctionCategory, 1);
		cat->internal_name = string_get (int_name);
		if (translation != NULL) {
			cat->display_name = string_get (translation);
			cat->has_translation = TRUE;
		} else {
			cat->display_name = string_get (name);
			cat->has_translation = FALSE;
		}
		cat->functions = NULL;
		categories = g_list_insert_sorted (
		             categories, cat, &function_category_compare);
	} else if (translation != NULL && translation != name &&
	           !cat->has_translation) {
		string_unref (cat->display_name);
		cat->display_name = string_get (translation);
		cat->has_translation = TRUE;
		g_list_remove_link (categories, l);
		g_list_free_1 (l);
		categories = g_list_insert_sorted (
		             categories, cat, &function_category_compare);
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

static void
extract_arg_types (FunctionDefinition *def)
{
	int i;

	function_def_count_args (def,
				 &def->fn.args.min_args,
				 &def->fn.args.max_args);
	def->fn.args.arg_types = g_malloc (def->fn.args.max_args + 1);
	for (i = 0; i < def->fn.args.max_args; i++)
		def->fn.args.arg_types[i] = function_def_get_arg_type (def, i);
	def->fn.args.arg_types[i] = 0;
}



static Value *
error_function_no_full_info (FunctionEvalInfo *ei, ExprList *expr_node_list)
{
	return value_new_error (ei->pos, _("Function implementation not available."));
}

void
func_def_load (FunctionDefinition *fn_def)
{
	gchar const *args;
	gchar const *arg_names;
	gchar const **help;
	FunctionArgs	 fn_args;
	FunctionNodes	 fn_nodes;
	FuncLinkHandle	 fn_link;
	FuncUnlinkHandle fn_unlink;
	gboolean success;

	g_return_if_fail (fn_def->fn_type == FUNCTION_NAMEONLY);

	success = fn_def->get_full_info_callback (
		  fn_def, &args, &arg_names, &help,
		  &fn_args, &fn_nodes, &fn_link, &fn_unlink);

	if (success) {
		fn_def->named_arguments = arg_names;
		fn_def->help = help;
		if (fn_args != NULL) {
			fn_def->fn_type = FUNCTION_ARGS;
			fn_def->fn.args.func = fn_args;
			fn_def->fn.args.arg_spec = args;
			extract_arg_types (fn_def);
		} else if (fn_nodes != NULL) {
			fn_def->fn_type = FUNCTION_NODES;
			fn_def->fn.fn_nodes = fn_nodes;
		} else {
			g_assert_not_reached ();
		}
		fn_def->link = fn_link;
		fn_def->unlink = fn_unlink;
	} else {
		fn_def->named_arguments = "";
		fn_def->fn_type = FUNCTION_NODES;
		fn_def->fn.fn_nodes = &error_function_no_full_info;
		fn_def->link = NULL;
		fn_def->unlink = NULL;
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

	switch (fn_def->fn_type) {
	case FUNCTION_ARGS:
		g_free (fn_def->fn.args.arg_types);
		break;
	default:
		/* Nothing.  */
		;
	}
	g_free (fn_def);
}

static FunctionDefinition *
fn_def_new (FunctionCategory *category,
	    char const *name,
	    char const *arg_names,
	    char const **help)
{
	FunctionDefinition *fn_def;

	fn_def = g_new (FunctionDefinition, 1);
	fn_def->get_full_info_callback = NULL;
	fn_def->flags	= 0;
	fn_def->name    = name;
	fn_def->help    = help;
	fn_def->named_arguments = arg_names;
	fn_def->link	= NULL;
	fn_def->unlink	= NULL;
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
		   char const **help,
		   FunctionArgs fn)
{
	static char const valid_tokens[] = "fsbraAS?|";
	FunctionDefinition *fn_def;
	char const *ptr;

	g_return_val_if_fail (fn != NULL, NULL);
	g_return_val_if_fail (args != NULL, NULL);

	/* Check those arguements */
	for (ptr = args ; *ptr ; ptr++) {
		g_return_val_if_fail (strchr (valid_tokens, *ptr), NULL);
	}

	fn_def = fn_def_new (category, name, arg_names, help);
	if (fn_def != NULL) {
		fn_def->fn_type = FUNCTION_ARGS;
		fn_def->fn.args.func = fn;
		fn_def->fn.args.arg_spec = args;
		extract_arg_types (fn_def);
	}
	return fn_def;
}

FunctionDefinition *
function_add_nodes (FunctionCategory *category,
		    char const *name,
		    char const *args,
		    char const *arg_names,
		    char const **help,
		    FunctionNodes fn)
{
	FunctionDefinition *fn_def;

	g_return_val_if_fail (fn != NULL, NULL);
	if (args && *args) {
		g_warning ("Arg spec for node function -- why?");
	}

	fn_def = fn_def_new (category, name, arg_names, help);
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

	fn_def = fn_def_new (category, name, NULL, NULL);
	if (fn_def != NULL) {
		fn_def->fn_type = FUNCTION_NAMEONLY;
		fn_def->get_full_info_callback = callback;
	}

	return fn_def;
}

/* Handle unknown functions on import without losing their names */
static Value *
unknownFunctionHandler (FunctionEvalInfo *ei, ExprList *expr_node_list)
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
				   0, "...", NULL,
				   &unknownFunctionHandler);

	/* WISHLIST : it would be nice to have a log if these. */
	g_warning ("Unknown %sfunction : %s", type, name);

	return func;
}

gpointer
function_def_get_user_data (FunctionDefinition const *fn_def)
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

char const *
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
	char const *ptr;
	int   i;
	int   vararg;

	g_return_if_fail (min != NULL);
	g_return_if_fail (max != NULL);
	g_return_if_fail (fn_def != NULL);

	if (fn_def->fn_type == FUNCTION_NAMEONLY)
		func_def_load ((FunctionDefinition *) fn_def);

	/*
	 * FIXME: clearly for 'nodes' functions many of
	 * the type fields will need to be filled.
	 */
	if (fn_def->fn_type == FUNCTION_NODES) {
		*min = 0;
		*max = G_MAXINT;
		return;
	}

	ptr = fn_def->fn.args.arg_spec;
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
 * function_set_link_handlers :
 *
 * Add callbacks for a function to perform special handling as each instance
 * of the function is linked or unlinked from the sheet.
 */
void
function_set_link_handlers (FunctionDefinition *fn_def,
			    FuncLinkHandle   link,
			    FuncUnlinkHandle unlink)
{
	/* Be paranoid for now */
	g_return_if_fail (fn_def != NULL);
	g_return_if_fail (fn_def->link == NULL);
	g_return_if_fail (fn_def->unlink == NULL);

	fn_def->link = link;
	fn_def->unlink = unlink;
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
	char const *ptr;

	g_return_val_if_fail (arg_idx >= 0, '?');
	g_return_val_if_fail (fn_def != NULL, '?');

	if (fn_def->fn_type == FUNCTION_NAMEONLY)
		func_def_load ((FunctionDefinition *) fn_def);

	for (ptr = fn_def->fn.args.arg_spec; ptr && *ptr; ptr++) {
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
		      Value            **type_mismatch)
{
	Value *v;

	*type_mismatch = NULL;

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
		v = expr_eval (t, ei->pos,
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

		if (v->type == VALUE_STRING) {
			Value *newv = format_match_number (value_peek_string (v), NULL);
			value_release (v);
			v = newv;
			break;
		}

		if (v->type == VALUE_ERROR) {
			*type_mismatch = v;
			v = NULL;
			break;
		}

		if (v->type != VALUE_INTEGER &&
		    v->type != VALUE_FLOAT &&
		    v->type != VALUE_BOOLEAN) {
			*type_mismatch = value_new_error (ei->pos,
							  gnumeric_err_VALUE);
		}
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

		if (v->type == VALUE_ERROR) {
			*type_mismatch = v;
			v = NULL;
		} else if (v->type != VALUE_STRING) {
			*type_mismatch = value_new_error (ei->pos,
							  gnumeric_err_VALUE);
		}
		break;

	case 'r':
		if (v->type != VALUE_CELLRANGE) {
			*type_mismatch = value_new_error (ei->pos,
							  gnumeric_err_VALUE);
		} else {
			cellref_make_abs (&v->v_range.cell.a,
					  &v->v_range.cell.a,
					  ei->pos);
			cellref_make_abs (&v->v_range.cell.b,
					  &v->v_range.cell.b,
					  ei->pos);
		}
		break;

	case 'a':
		if (v->type != VALUE_ARRAY) {
			*type_mismatch = value_new_error (ei->pos,
							  gnumeric_err_VALUE);
		}
		break;

	case 'A':
		if (v->type != VALUE_ARRAY &&
		    v->type != VALUE_CELLRANGE) {
			*type_mismatch = value_new_error (ei->pos,
							  gnumeric_err_VALUE);
		}

		if (v->type == VALUE_CELLRANGE) {
			cellref_make_abs (&v->v_range.cell.a,
					  &v->v_range.cell.a,
					  ei->pos);
			cellref_make_abs (&v->v_range.cell.b,
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
 * @args: ExprList of ExprTree args.
 *
 * Do the guts of calling a function.
 *
 * Return value:
 **/
Value *
function_call_with_list (FunctionEvalInfo *ei, ExprList *l)
{
	FunctionDefinition const *fn_def;
	int argc, arg;
	Value *v = NULL;
	Value **values;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_call != NULL, NULL);

	fn_def = ei->func_call->func;
	if (fn_def->fn_type == FUNCTION_NAMEONLY)
		func_def_load ((FunctionDefinition *) fn_def);

	/* Functions that deal with ExprNodes */
	if (fn_def->fn_type == FUNCTION_NODES)
		return fn_def->fn.fn_nodes (ei, l);

	/* Functions that take pre-computed Values */
	argc = expr_list_length (l);
	if (argc > fn_def->fn.args.max_args ||
	    argc < fn_def->fn.args.min_args) {
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));
	}

	values = g_new (Value *, fn_def->fn.args.max_args);

	for (arg = 0; l; l = l->next, ++arg) {
		char  arg_type;
		Value *type_mismatch;

		arg_type = fn_def->fn.args.arg_types[arg];

		values[arg] = function_marshal_arg (ei, l->data, arg_type,
						     &type_mismatch);

		if (type_mismatch || values[arg] == NULL) {
			free_values (values, arg + 1);
			if (type_mismatch)
				return type_mismatch;
			else
				return value_new_error (ei->pos, gnumeric_err_VALUE);
		}
	}
	while (arg < fn_def->fn.args.max_args)
		values [arg++] = NULL;
	v = fn_def->fn.args.func (ei, values);

	free_values (values, arg);
	return v;
}

/*
 * Use this to invoke a register function: the only drawback is that
 * you have to compute/expand all of the values to use this
 */
Value *
function_call_with_values (EvalPos const *ep, char const *fn_name,
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
                               gint    argc,
                               Value  *values [])
{
	Value *retval;
	ExprFunction	ef;
	FunctionEvalInfo fs;

	fs.pos = ep;
	fs.func_call = &ef;
	ef.func = (FunctionDefinition *)fn_def;

	if (fn_def->fn_type == FUNCTION_NAMEONLY)
		func_def_load (ef.func);

	if (fn_def->fn_type == FUNCTION_NODES) {
		/*
		 * If function deals with ExprNodes, create some
		 * temporary ExprNodes with constants.
		 */
		ExprConstant *tree = NULL;
		ExprList *l = NULL;
		int i;

		if (argc) {
			tree = g_new (ExprConstant, argc);

			for (i = 0; i < argc; i++) {
				/* FIXME : this looks like a leak */
				*((Operation *)&(tree [i].oper)) = OPER_CONSTANT;
				tree [i].ref_count = 1;
				tree [i].value = values [i];

				l = expr_list_append (l, &(tree [i]));
			}
		}

		retval = fn_def->fn.fn_nodes (&fs, l);

		if (tree) {
			g_free (tree);
			expr_list_free (l);
		}

	} else
		retval = fn_def->fn.args.func (&fs, values);

	return retval;
}

/* ------------------------------------------------------------------------- */

typedef struct {
	FunctionIterateCB  callback;
	void                     *closure;
	gboolean                 strict;
} IterateCallbackClosure;

/*
 * cb_iterate_cellrange:
 *
 * Helper routine used by the function_iterate_do_value routine.
 * Invoked by the sheet cell range iterator.
 */
static Value *
cb_iterate_cellrange (Sheet *sheet, int col, int row,
		      Cell *cell, void *user_data)
{
	IterateCallbackClosure *data = user_data;
	Value *res;
	EvalPos ep;

	if (cell == NULL) {
		ep.sheet = sheet;
		ep.dep = NULL;
		ep.eval.col = col;
		ep.eval.row = row;
		return (*data->callback)(&ep, NULL, data->closure);
	}

	cell_eval (cell);
	eval_pos_init_cell (&ep, cell);

	/* If we encounter an error for the strict case, short-circuit here.  */
	if (data->strict && (NULL != (res = cell_is_error (cell))))
		return value_new_error_str (&ep, res->v_err.mesg);

	/* All other cases -- including error -- just call the handler.  */
	return (*data->callback)(&ep, cell->value, data->closure);
}

/*
 * function_iterate_do_value:
 *
 * Helper routine for function_iterate_argument_values.
 */
Value *
function_iterate_do_value (EvalPos const *ep,
			   FunctionIterateCB  callback,
			   void		*closure,
			   Value	*value,
			   gboolean      strict,
			   gboolean	 ignore_blank)
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
						      cb_iterate_cellrange,
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
 * @expr_node_list:   a ExprList of ExprTrees (what a Gnumeric function would get).
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
function_iterate_argument_values (EvalPos const		*ep,
				  FunctionIterateCB	 callback,
				  void			*callback_closure,
				  ExprList		*expr_node_list,
				  gboolean		 strict,
				  gboolean		 ignore_blank)
{
	Value * result = NULL;

	for (; result == NULL && expr_node_list;
	     expr_node_list = expr_node_list->next) {
		ExprTree const * tree = expr_node_list->data;
		Value *val;

		/* Permit empties and non scalars. We don't know what form the
		 * function wants its arguments */
		val = expr_eval (tree, ep, EVAL_PERMIT_NON_SCALAR|EVAL_PERMIT_EMPTY);

		if (val == NULL)
			continue;

		if (strict && val->type == VALUE_ERROR) {
			/* Be careful not to make value_terminate into a real value */
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

	if (fn_def->fn_type == FUNCTION_NAMEONLY)
		func_def_load ((FunctionDefinition *) fn_def);

	tok = g_new (TokenizedHelp, 1);
	tok->fndef = fn_def;
	tok->help_copy = NULL;
	tok->sections = NULL;

	if (fn_def->help != NULL && fn_def->help [0] != '\0') {
		char *ptr, *start;
		gboolean seek_at = TRUE;
		gboolean last_newline = TRUE;

		ptr = _(fn_def->help [0]);
		tok->help_is_localized = ptr != fn_def->help [0];
		tok->help_copy = g_strdup (ptr);
		tok->sections = g_ptr_array_new ();

		for (start = ptr = tok->help_copy; *ptr ; ptr++) {
			if (ptr[0] == '\\' && ptr[1])
				ptr += 2;

			/* FIXME : This is hugely ugly.  we need a decent
			 * format for this stuff.
			 * @SECTION=content is damn ugly considering we
			 * are saying things like @r = bob in side the content.
			 * for now make the assumption that any args will
			 * always start with lower case.
			 */
			if (*ptr == '@' && isupper (*(unsigned char *)(ptr +1)) &&
			    seek_at && last_newline) {
				/* previous newline if this is not the first */
				if (ptr != start)
					*(ptr-1) = '\0';
				else
					*ptr = '\0';

				g_ptr_array_add (tok->sections, (ptr+1));
				seek_at = FALSE;
			} else if (*ptr == '=' && !seek_at){
				*ptr = 0;
				g_ptr_array_add (tok->sections, (ptr+1));
				seek_at = TRUE;
			}
			last_newline = (*ptr == '\n');
		}
	}

	return tok;
}

/**
 * Use to find a token eg. "FUNCTION"'s value.
 **/
char const *
tokenized_help_find (TokenizedHelp *tok, char const *token)
{
	int lp;

	if (!tok || !tok->sections)
		return "Incorrect Function Description.";

	for (lp = 0; lp + 1 < (int)tok->sections->len; lp++) {
		char const *cmp = g_ptr_array_index (tok->sections, lp);

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
