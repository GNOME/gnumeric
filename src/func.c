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
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "func.h"

#include "parse-util.h"
#include "dependent.h"
#include "expr.h"
#include "expr-impl.h"
#include "cell.h"
#include "str.h"
#include "symbol.h"
#include "workbook.h"
#include "sheet.h"
#include "value.h"
#include "number-match.h"
#include "format.h"
#include "func-builtin.h"

#include <string.h>
#include <glib.h>
#include <stdlib.h>

static GList *categories;
static SymbolTable *global_symbol_table;

static GnmFuncGroup *unknown_cat;
static GSList *unknown_functions;

void
functions_init (void)
{
	global_symbol_table = symbol_table_new ();
	func_builtin_init ();
}

void
functions_shutdown (void)
{
	g_assert ((unknown_cat == NULL) == (unknown_functions == NULL));

	while (unknown_functions) {
		GnmFunc *func = unknown_functions->data;
		function_remove (unknown_cat, gnm_func_get_name (func));
		unknown_functions = g_slist_remove (unknown_functions, func);
	}

	func_builtin_shutdown ();

	symbol_table_destroy (global_symbol_table);
	global_symbol_table = NULL;
}

static void
copy_hash_table_to_ptr_array (gpointer key, gpointer value, gpointer array)
{
	Symbol *sym = value;
	GnmFunc *fd = sym->data;
	if (sym->type == SYMBOL_FUNCTION && fd->name != NULL) {
		if (fd->fn_type == GNM_FUNC_TYPE_STUB)
			gnm_func_load_stub ((GnmFunc *) fd);
		if (fd->help != NULL)
			g_ptr_array_add (array, fd);
	}
}

static int
func_def_cmp (gconstpointer a, gconstpointer b)
{
	GnmFunc const *fda = *(GnmFunc const **)a ;
	GnmFunc const *fdb = *(GnmFunc const **)b ;

	g_return_val_if_fail (fda->name != NULL, 0);
	g_return_val_if_fail (fdb->name != NULL, 0);

	if (fda->fn_group != NULL && fdb->fn_group != NULL) {
		int res = strcmp (fda->fn_group->display_name->str, fdb->fn_group->display_name->str);
		if (res != 0)
			return res;
	}

	return g_ascii_strcasecmp (fda->name, fdb->name);
}

void
function_dump_defs (char const *filename, gboolean as_def)
{
	FILE *output_file;
	unsigned i;
	GPtrArray *ordered;
	GnmFuncGroup const *group = NULL;

	g_return_if_fail (filename != NULL);

	if ((output_file = fopen (filename, "w")) == NULL){
		printf (_("Cannot create file %s\n"), filename);
		exit (1);
	}

	/* TODO : Use the translated names and split by fn_group. */
	ordered = g_ptr_array_new ();
	g_hash_table_foreach (global_symbol_table->hash,
		copy_hash_table_to_ptr_array, ordered);

	if (ordered->len > 0)
		qsort (&g_ptr_array_index (ordered, 0),
		       ordered->len, sizeof (gpointer),
		       func_def_cmp);

	if (!as_def)
		fprintf (output_file, "<table border=1>\n");

	for (i = 0; i < ordered->len; i++) {
		GnmFunc const *fd = g_ptr_array_index (ordered, i);
		if (as_def) {
			fprintf (output_file, "%s\n\n", _(fd->help));
		} else {
			static struct {
				char const *name;
				char const *colour_str;
			} const testing [] = {
				{ "Unknown",		"FFFFFF" },
				{ "No Testsuite",	"FF7662" },
				{ "Basic",		"FFF79D" },
				{ "Exhaustive",		"AEF8B5" },
				{ "Under Development",	"FF6C00" }
			};
			static struct {
				char const *name;
				char const *colour_str;
			} const implementation [] = {
				{ "Exists",			"FFFFFF" },
				{ "Unimplemented",		"FF7662" },
				{ "Subset",			"FFF79D" },
				{ "Complete",			"AEF8B5" },
				{ "Superset",			"16E49E" },
				{ "Subset with_extensions",	"59FFF2" },
				{ "Under development",		"FF6C00" },
				{ "Unique to Gnumeric",		"44BE18" },
			};
			if (group != fd->fn_group) {
				group = fd->fn_group;
				fprintf (output_file, "<tr><td><h1>%s</h1></td><td><b>Function</b></td><td><b>Implementation</b></td><td><b>Testing</b></td></tr>\n", group->display_name->str);
			}
			fprintf (output_file, "<tr><td></td>\n");
			fprintf (output_file,
"    <td><a href =\"doc/gnumeric-%s.html\">%s</a></td>\n", fd->name, fd->name);
			fprintf (output_file,
"    <td bgcolor=#%s><a href=\"mailto:gnumeric-list@gnome.org?subject=Re: %s implementation\">%s</a></td>\n",
implementation[fd->impl_status].colour_str, fd->name, implementation[fd->impl_status].name);
			fprintf (output_file,
"    <td bgcolor=#%s><a href=\"mailto:gnumeric-list@gnome.org?subject=Re: %s testing\">%s</a></td>\n",
testing[fd->test_status].colour_str, fd->name, testing[fd->test_status].name);
			fprintf (output_file,"</tr>\n");
		}
	}
	if (!as_def)
		fprintf (output_file, "</table>\n");

	g_ptr_array_free (ordered, TRUE);
	fclose (output_file);
}

/* ------------------------------------------------------------------------- */

static void
gnm_func_group_free (GnmFuncGroup *fn_group)
{
	g_return_if_fail (fn_group != NULL);
	g_return_if_fail (fn_group->functions == NULL);

	string_unref (fn_group->internal_name);
	string_unref (fn_group->display_name);
	g_free (fn_group);
}

static gint
function_category_compare (gconstpointer a, gconstpointer b)
{
	GnmFuncGroup const *cat_a = a;
	GnmFuncGroup const *cat_b = b;

	return g_utf8_collate (cat_a->display_name->str,
			       cat_b->display_name->str);
}

GnmFuncGroup *
gnm_func_group_fetch (char const *name)
{
	return gnm_func_group_fetch_with_translation (name, _(name));
}

GnmFuncGroup *
gnm_func_group_fetch_with_translation (char const *name,
				       char const *translation)
{
	GnmFuncGroup *cat = NULL;
	GList *l;

	g_return_val_if_fail (name != NULL, NULL);

	for (l = categories; l != NULL; l = l->next) {
		cat = l->data;
		if (strcmp (cat->internal_name->str, name) == 0) {
			break;
		}
	}

	if (l == NULL) {
		cat = g_new (GnmFuncGroup, 1);
		cat->internal_name = string_get (name);
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

GnmFuncGroup *
gnm_func_group_get_nth (int n)
{
	return g_list_nth_data (categories, n);
}

static void
gnm_func_group_add_func (GnmFuncGroup *fn_group,
			 GnmFunc *fn_def)
{
	g_return_if_fail (fn_group != NULL);
	g_return_if_fail (fn_def != NULL);

	fn_group->functions = g_list_append (fn_group->functions, fn_def);
}

static void
gnm_func_group_remove_func (GnmFuncGroup *fn_group, GnmFunc *func)
{
	g_return_if_fail (fn_group != NULL);
	g_return_if_fail (func != NULL);

	fn_group->functions = g_list_remove (fn_group->functions, func);
	if (fn_group->functions == NULL) {
		categories = g_list_remove (categories, fn_group);
		gnm_func_group_free (fn_group);
	}
}

/******************************************************************************/

static void
extract_arg_types (GnmFunc *def)
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
error_function_no_full_info (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return value_new_error (ei->pos, _("Function implementation not available."));
}

void
gnm_func_load_stub (GnmFunc *func)
{
	GnmFuncDescriptor desc;

	g_return_if_fail (func->fn_type == GNM_FUNC_TYPE_STUB);

	/* default the content to 0 in case we add new fields
	 * later and the services do not fill them in
	 */
	memset (&desc, 0, sizeof (GnmFuncDescriptor));

	if (func->fn.load_desc (func, &desc)) {
		func->arg_names	 = desc.arg_names;
		func->help	 = desc.help ? *desc.help : NULL;
		if (desc.fn_args != NULL) {
			func->fn_type		= GNM_FUNC_TYPE_ARGS;
			func->fn.args.func	= desc.fn_args;
			func->fn.args.arg_spec	= desc.arg_spec;
			extract_arg_types (func);
		} else if (desc.fn_nodes != NULL) {
			func->fn_type		= GNM_FUNC_TYPE_NODES;
			func->fn.nodes		= desc.fn_nodes;
		} else {
			g_warning ("Invalid function descriptor with no function");
		}
		func->linker	  = desc.linker;
		func->unlinker	  = desc.unlinker;
		func->impl_status = desc.impl_status;
		func->test_status = desc.test_status;
		func->flags	  = desc.flags;
	} else {
		func->arg_names = "";
		func->fn_type = GNM_FUNC_TYPE_NODES;
		func->fn.nodes = &error_function_no_full_info;
		func->linker   = NULL;
		func->unlinker = NULL;
	}
}

void
gnm_func_ref (GnmFunc *func)
{
	g_return_if_fail (func != NULL);

	func->ref_count++;
	if (func->ref_count == 1 && func->ref_notify != NULL)
		func->ref_notify (func, 1);
}

void
gnm_func_unref (GnmFunc *func)
{
	g_return_if_fail (func != NULL);
	g_return_if_fail (func->ref_count > 0);

	func->ref_count--;
	if (func->ref_count == 0 && func->ref_notify != NULL)
		func->ref_notify (func, 0);
}

GnmFunc *
gnm_func_lookup (char const *name, Workbook const *optional_scope)
{
	Symbol *sym = symbol_lookup (global_symbol_table, name);
	if (sym != NULL)
		return sym->data;
	return NULL;
}

void
function_remove (GnmFuncGroup *fn_group, char const *name)
{
	GnmFunc *func;
	Symbol *sym;

	g_return_if_fail (name != NULL);

	func = gnm_func_lookup (name, NULL);
	g_return_if_fail (func != NULL);
	g_return_if_fail (func->ref_count == 0);

	gnm_func_group_remove_func (fn_group, func);
	sym = symbol_lookup (global_symbol_table, name);
	symbol_unref (sym);

	switch (func->fn_type) {
	case GNM_FUNC_TYPE_ARGS:
		g_free (func->fn.args.arg_types);
		break;
	default:
		/* Nothing.  */
		;
	}
	if (func->flags & GNM_FUNC_FREE_NAME)
		g_free ((char *)func->name);
	g_free (func);
}

#if 0
function_add_args
function_add_nodes
#endif
GnmFunc *
gnm_func_add (GnmFuncGroup *fn_group,
	      GnmFuncDescriptor const *desc)
{
	static char const valid_tokens[] = "fsbraAES?|";
	GnmFunc *func;
	char const *ptr;

	g_return_val_if_fail (fn_group != NULL, NULL);
	g_return_val_if_fail (desc != NULL, NULL);

	func = g_new (GnmFunc, 1);
	if (func == NULL)
		return NULL;

	func->name		= desc->name;
	func->arg_names		= desc->arg_names;
	func->help		= desc->help ? *desc->help : NULL;
	func->linker		= desc->linker;
	func->unlinker		= desc->unlinker;
	func->ref_notify	= desc->ref_notify;
	func->flags		= desc->flags;
	func->impl_status	= desc->impl_status;
	func->test_status	= desc->test_status;

	func->user_data		= NULL;
	func->ref_count		= 0;

	if (desc->fn_args != NULL) {
		/* Check those arguements */
		for (ptr = desc->arg_spec ; *ptr ; ptr++) {
			g_return_val_if_fail (strchr (valid_tokens, *ptr), NULL);
		}

		func->fn_type		= GNM_FUNC_TYPE_ARGS;
		func->fn.args.func	= desc->fn_args;
		func->fn.args.arg_spec	= desc->arg_spec;
		extract_arg_types (func);
	} else if (desc->fn_nodes != NULL) {

		if (desc->arg_spec && *desc->arg_spec) {
			g_warning ("Arg spec for node function -- why?");
		}

		func->fn_type  = GNM_FUNC_TYPE_NODES;
		func->fn.nodes = desc->fn_nodes;
	} else {
		g_warning ("Invalid function has neither args nor nodes handler");
		g_free (func);
		return NULL;
	}

	func->fn_group = fn_group;
	if (fn_group != NULL)
		gnm_func_group_add_func (fn_group, func);
	symbol_install (global_symbol_table, func->name, SYMBOL_FUNCTION, func);

	return func;
}

/* Handle unknown functions on import without losing their names */
static Value *
unknownFunctionHandler (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	return value_new_error_NAME (ei->pos);
}

GnmFunc *
gnm_func_add_stub (GnmFuncGroup *fn_group,
		   char const 	    *name,
		   GnmFuncLoadDesc   load_desc,
		   GnmFuncRefNotify  opt_ref_notify)
{
	GnmFunc *func = g_new0 (GnmFunc, 1);
	if (func == NULL)
		return NULL;

	func->name		= name;
	func->ref_notify	= opt_ref_notify;
	func->fn_type		= GNM_FUNC_TYPE_STUB;
	func->fn.load_desc	= load_desc;

	func->fn_group = fn_group;
	if (fn_group != NULL)
		gnm_func_group_add_func (fn_group, func);
	symbol_install (global_symbol_table, func->name, SYMBOL_FUNCTION, func);

	return func;
}

/*
 * When importing it is useful to keep track of unknown function names.
 * We may be missing a plugin or something similar.
 *
 * TODO : Eventully we should be able to keep track of these
 *        and replace them with something else.  Possibly even reordering the
 *        arguments.
 */
GnmFunc *
gnm_func_add_placeholder (char const *name, char const *type,
			  gboolean copy_name)
{
	GnmFuncDescriptor desc;
	GnmFunc *func = gnm_func_lookup (name, NULL);
	char const *unknown_cat_name = N_("Unknown Function");

	g_return_val_if_fail (func == NULL, func);

	if (!unknown_cat)
		unknown_cat = gnm_func_group_fetch (unknown_cat_name);

	memset (&desc, 0, sizeof (GnmFuncDescriptor));
	desc.name	  = copy_name ? g_strdup (name) : name;
	desc.arg_spec	  = NULL;
	desc.arg_names	  = "...";
	desc.help	  = NULL;
	desc.fn_args	  = NULL;
	desc.fn_nodes	  = &unknownFunctionHandler;
	desc.linker	  = NULL;
	desc.unlinker	  = NULL;
	desc.ref_notify	  = NULL;
	desc.flags	  = GNM_FUNC_IS_PLACEHOLDER | (copy_name ? GNM_FUNC_FREE_NAME : 0);
	desc.impl_status  = GNM_FUNC_IMPL_STATUS_EXISTS;
	desc.test_status  = GNM_FUNC_TEST_STATUS_UNKNOWN;

	func = gnm_func_add (unknown_cat, &desc);
	unknown_functions = g_slist_prepend (unknown_functions, func);

	/* WISHLIST : it would be nice to have a log if these. */
	g_warning ("Unknown %sfunction : %s", type, name);

	return func;
}

/* See type GnmParseFunctionHandler */
GnmExpr const *
gnm_func_placeholder_factory (const char *name,
			      GnmExprList *args,
			      G_GNUC_UNUSED GnmExprConventions *convs)
{
	GnmFunc *f = gnm_func_add_placeholder (name, "", TRUE);
	return gnm_expr_new_funcall (f, args);
}


gpointer
gnm_func_get_user_data (GnmFunc const *func)
{
	g_return_val_if_fail (func != NULL, NULL);

	return func->user_data;
}

void
gnm_func_set_user_data (GnmFunc *func, gpointer user_data)
{
	g_return_if_fail (func != NULL);

	func->user_data = user_data;
}

char const *
gnm_func_get_name (GnmFunc const *func)
{
	g_return_val_if_fail (func != NULL, NULL);

	return func->name;
}

/**
 * function_def_count_args:
 * @func: pointer to function definition
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
function_def_count_args (GnmFunc const *fn_def,
                         int *min, int *max)
{
	char const *ptr;
	int   i;
	int   vararg;

	g_return_if_fail (min != NULL);
	g_return_if_fail (max != NULL);
	g_return_if_fail (fn_def != NULL);

	if (fn_def->fn_type == GNM_FUNC_TYPE_STUB)
		gnm_func_load_stub ((GnmFunc *) fn_def);

	/*
	 * FIXME: clearly for 'nodes' functions many of
	 * the type fields will need to be filled.
	 */
	if (fn_def->fn_type == GNM_FUNC_TYPE_NODES) {
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
 * function_def_get_arg_type:
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Return value: the type of the argument
 **/
char
function_def_get_arg_type (GnmFunc const *fn_def,
                           int arg_idx)
{
	char const *ptr;

	g_return_val_if_fail (arg_idx >= 0, '?');
	g_return_val_if_fail (fn_def != NULL, '?');

	if (fn_def->fn_type == GNM_FUNC_TYPE_STUB)
		gnm_func_load_stub ((GnmFunc *) fn_def);

	switch (fn_def->fn_type) {
	case GNM_FUNC_TYPE_ARGS:
		for (ptr = fn_def->fn.args.arg_spec; ptr && *ptr; ptr++) {
			if (*ptr == '|')
				continue;
			if (arg_idx-- == 0)
				return *ptr;
		}
		return '?';

	case GNM_FUNC_TYPE_NODES:
		return '?'; /* Close enough for now.  */

	case GNM_FUNC_TYPE_STUB:
#ifndef DEBUG_SWITCH_ENUM
	default:
#endif
		g_assert_not_reached ();
		return '?';
	}
}

/**
 * function_def_get_arg_type_string:
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Return value: the type of the argument as a string
 **/
char const *
function_def_get_arg_type_string (GnmFunc const *fn_def,
				  int arg_idx)
{
	switch (function_def_get_arg_type (fn_def, arg_idx)) {
	case 'f':
		return _("Number");
	case 's':
		return _("String");
	case 'b':
		return _("Boolean");
	case 'r':
		return _("Cell Range");
	case 'A':
		return _("Area");
	case 'E':
		return _("Scalar or Error");
	case 'S':
		return _("Scalar");
	case '?':
		return _("Any");

	default:
		g_warning ("Unkown arg type");
		return "Broken";
	}
}

/**
 * function_def_get_arg_name:
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Return value: the name of the argument (must be freed)
 **/
char*
function_def_get_arg_name (GnmFunc const *fn_def,
                           int arg_idx)
{
	char **names, **o_names;
	char *name;
	char *translated_arguments;
	char delimiter[2];

	g_return_val_if_fail (arg_idx >= 0, NULL);
	g_return_val_if_fail (fn_def != NULL, NULL);

	if (fn_def->fn_type == GNM_FUNC_TYPE_STUB)
		gnm_func_load_stub ((GnmFunc *) fn_def);

	if (!fn_def->arg_names)
		return NULL;

	translated_arguments = _(fn_def->arg_names);
	delimiter[0] =
		strcmp (translated_arguments, fn_def->arg_names) == 0
		? ','
		: format_get_arg_sep ();
	delimiter[1] = 0;
	names = g_strsplit (translated_arguments, delimiter, G_MAXINT);
	o_names = names;

	while (arg_idx-- && *names) {
		names++;
	}

	if (*names == NULL)
		return NULL;
	name = g_strdup (*names);
	g_strfreev (o_names);
	return name;
}

/* ------------------------------------------------------------------------- */

static inline void
free_values (Value **values, int top)
{
	int i;

	for (i = 0; i < top; i++)
		if (values [i])
			value_release (values [i]);
}

/**
 * function_call_with_list:
 * @ei: EvalInfo containing valid fn_def!
 * @args: GnmExprList of GnmExpr args.
 * @flags :
 *
 * Do the guts of calling a function.
 *
 * Returns the result.
 **/
Value *
function_call_with_list (FunctionEvalInfo *ei, GnmExprList *l,
			 GnmExprEvalFlags flags)
{
	GnmFunc const *fn_def;
	int	  argc, i, optional, iter_count, iter_width = 0, iter_height = 0;
	char	  arg_type;
	Value	 **args, *tmp = NULL;
	GnmExpr  *expr;
	int 	 *iter_item = NULL;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_call != NULL, NULL);

	fn_def = ei->func_call->func;
	if (fn_def->fn_type == GNM_FUNC_TYPE_STUB)
		gnm_func_load_stub ((GnmFunc *) fn_def);

	/* Functions that deal with ExprNodes */
	if (fn_def->fn_type == GNM_FUNC_TYPE_NODES)
		return fn_def->fn.nodes (ei, l);

	/* Functions that take pre-computed Values */
	argc = gnm_expr_list_length (l);
	if (argc > fn_def->fn.args.max_args ||
	    argc < fn_def->fn.args.min_args) {
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));
	}

	optional = 0;
	args = g_alloca (sizeof (Value *) * fn_def->fn.args.max_args);
	iter_count = (flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR) ? 0 : -1;

	for (i = 0; l; l = l->next, ++i) {
		arg_type = fn_def->fn.args.arg_types[i];
		expr = l->data;

		if (i >= fn_def->fn.args.min_args)
			optional = GNM_EXPR_EVAL_PERMIT_EMPTY;

		if (arg_type == 'A' || arg_type == 'r') {
			if (expr->any.oper == GNM_EXPR_OP_CELLREF) {
				CellRef r;
				cellref_make_abs (&r, &expr->cellref.ref, ei->pos);
				args[i] = value_new_cellrange_unsafe (&r, &r);
				/* TODO decide on the semantics of these argument types */
#warning do we need to force an eval here ?
			} else {
				tmp = args[i] = gnm_expr_eval (expr, ei->pos,
					optional | GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
				if (tmp->type == VALUE_CELLRANGE) {
					cellref_make_abs (&tmp->v_range.cell.a,
							  &tmp->v_range.cell.a,
							  ei->pos);
					cellref_make_abs (&tmp->v_range.cell.b,
							  &tmp->v_range.cell.b,
							  ei->pos);
				} else if (tmp->type != VALUE_ARRAY || arg_type != 'A') {
					free_values (args, i + 1);
					return value_new_error_VALUE (ei->pos);
				}
			}
			continue;
		}

		/* force scalars whenever we are certain */
		tmp = args[i] = gnm_expr_eval (expr, ei->pos, optional |
		       ((iter_count >= 0 || arg_type == '?')
			       ? GNM_EXPR_EVAL_PERMIT_NON_SCALAR
			       : GNM_EXPR_EVAL_SCALAR_NON_EMPTY));

		if (tmp == NULL ||	/* Optional arguments can be empty */
		    arg_type == '?')	/* '?' arguments are unrestriced */
			continue;

		/* Handle implicit intersection or iteration depending on flags */
		if (tmp->type == VALUE_CELLRANGE || tmp->type == VALUE_ARRAY) {
			if (iter_count > 0) {
				if (iter_width != value_area_get_width (tmp, ei->pos) ||
				    iter_height != value_area_get_height (tmp, ei->pos)) {
					/* no need to free inter_vals, there is nothing there yet */
					free_values (args, i + 1);
					return value_new_error_VALUE (ei->pos);
				}
			} else {
				if (iter_count < 0) {
					g_warning ("Damn I though this was impossible");
					iter_count = 0;
				}
				iter_item = g_alloca (sizeof (int) * argc);
				iter_width = value_area_get_width (tmp, ei->pos);
				iter_height = value_area_get_height (tmp, ei->pos);
			}
			iter_item[iter_count] = iter_count;
			iter_count++;
		}

		/* All of these argument types must be scalars */
		switch (arg_type) {
		case 'b':
		case 'f':
			if (tmp->type == VALUE_STRING) {
				tmp = format_match_number (value_peek_string (tmp), NULL,
					workbook_date_conv (ei->pos->sheet->workbook));
				if (tmp == NULL) {
					free_values (args, i + 1);
					return value_new_error_VALUE (ei->pos);
				}
				value_release (args [i]);
				args[i] = tmp;
			} else if (tmp->type == VALUE_ERROR) {
				free_values (args, i);
				return tmp;
			}

			if (tmp->type != VALUE_INTEGER &&
			    tmp->type != VALUE_FLOAT &&
			    tmp->type != VALUE_BOOLEAN) {
				free_values (args, i+1);
				return value_new_error_VALUE (ei->pos);
			}
			break;

		case 's':
			if (tmp->type == VALUE_ERROR) {
				free_values (args, i);
				return tmp;
			} else if (tmp->type != VALUE_STRING) {
				free_values (args, i+1);
				return value_new_error_VALUE (ei->pos);
			}
			break;

		case 'S':
			if (tmp->type == VALUE_ERROR) {
				free_values (args, i);
				return tmp;
			}
			break;

		case 'E': /* nothing necessary */
			break;

		/* case '?': handled above */
		default :
			g_warning ("Unknown argument type '%c'", arg_type);
			break;
		}
	}

	while (i < fn_def->fn.args.max_args)
		args [i++] = NULL;

	if (iter_item != NULL) {
		int x, y;
		Value *res = value_new_array_empty (iter_width, iter_height);
		Value const *elem, *err;
		Value **iter_vals = g_alloca (sizeof (Value *) * iter_count);
		Value **iter_args = g_alloca (sizeof (Value *) * iter_count);

		/* collect the args we will iterate on */
		for (i = 0 ; i < iter_count; i++)
			iter_vals[i] = args[iter_item[i]];

		for (x = iter_width; x-- > 0 ; )
			for (y = iter_height; y-- > 0 ; ) {
				/* marshal the args */
				err = NULL;
				for (i = 0 ; i < iter_count; i++) {
					elem = value_area_fetch_x_y (iter_vals[i], x, y, ei->pos);
					arg_type = fn_def->fn.args.arg_types[iter_item[i]];
					if  (arg_type == 'b' || arg_type == 'f') {
						if (elem->type == VALUE_STRING) {
							tmp = format_match_number (value_peek_string (elem), NULL,
								workbook_date_conv (ei->pos->sheet->workbook));
							if (tmp != NULL) {
								args [iter_item[i]] = iter_args [i] = tmp;
								continue;
							} else
								break;
						} else if (elem->type == VALUE_ERROR) {
							err = elem;
							break;
						} else if (elem->type != VALUE_INTEGER &&
							   elem->type != VALUE_FLOAT &&
							   elem->type != VALUE_BOOLEAN)
							break;
					} else if (arg_type == 's') {
						if (elem->type == VALUE_ERROR) {
							err = elem;
							break;
						} else if (tmp->type != VALUE_STRING)
							break;
					}
					args [iter_item[i]] = iter_args [i] = value_duplicate (elem);
				}

				res->v_array.vals[x][y] = (i == iter_count)
					? fn_def->fn.args.func (ei, args)
					: ((err != NULL) ? value_duplicate (err)
							 : value_new_error_VALUE (ei->pos));
				free_values (iter_args, i);
			}

		/* free the primaries, not the already freed iteration */
		for (i = 0 ; i < iter_count; i++)
			args[iter_item[i]] = iter_vals[i];
		tmp = res;
		i = fn_def->fn.args.max_args;
	} else
		tmp = fn_def->fn.args.func (ei, args);

	free_values (args, i);
	return tmp;
}

/*
 * Use this to invoke a register function: the only drawback is that
 * you have to compute/expand all of the values to use this
 */
Value *
function_call_with_values (EvalPos const *ep, char const *fn_name,
			   int argc, Value *values [])
{
	GnmFunc *fn_def;

	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (fn_name != NULL, NULL);
	g_return_val_if_fail (ep->sheet != NULL, NULL);

	/* FIXME : support workbook local functions */
	fn_def = gnm_func_lookup (fn_name, NULL);
	if (fn_def == NULL)
		return value_new_error (ep, _("Function does not exist"));
	return function_def_call_with_values (ep, fn_def, argc, values);
}

Value *
function_def_call_with_values (EvalPos const *ep,
                               GnmFunc const *fn_def,
                               gint    argc,
                               Value  *values [])
{
	Value *retval;
	GnmExprFunction	ef;
	FunctionEvalInfo fs;

	fs.pos = ep;
	fs.func_call = &ef;
	ef.func = (GnmFunc *)fn_def;

	if (fn_def->fn_type == GNM_FUNC_TYPE_STUB)
		gnm_func_load_stub (ef.func);

	if (fn_def->fn_type == GNM_FUNC_TYPE_NODES) {
		/*
		 * If function deals with ExprNodes, create some
		 * temporary ExprNodes with constants.
		 */
		GnmExprConstant *expr = NULL;
		GnmExprList *l = NULL;
		int i;

		if (argc) {
			expr = g_alloca (argc * sizeof (GnmExprConstant));
			for (i = 0; i < argc; i++) {
				expr [i].oper = GNM_EXPR_OP_CONSTANT;
				expr [i].value = values [i];
				expr [i].ref_count = 1;

				l = gnm_expr_list_append (l, expr + i);
			}
		}

		retval = fn_def->fn.nodes (&fs, l);

		if (l != NULL)
			gnm_expr_list_free (l);
	} else
		retval = fn_def->fn.args.func (&fs, values);

	return retval;
}

/* ------------------------------------------------------------------------- */

typedef struct {
	FunctionIterateCB  callback;
	void              *closure;
	gboolean           strict;
	gboolean           ignore_subtotal;
} IterateCallbackClosure;

/*
 * cb_iterate_cellrange:
 *
 * Helper routine used by the function_iterate_do_value routine.
 * Invoked by the sheet cell range iterator.
 */
static Value *
cb_iterate_cellrange (Sheet *sheet, int col, int row,
		      Cell *cell, gpointer user_data)
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

	if (data->ignore_subtotal && cell_has_expr (cell) &&
	    gnm_expr_containts_subtotal (cell->base.expression))
		return NULL;

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
			   gpointer	 closure,
			   Value	*value,
			   gboolean      strict,
			   CellIterFlags iter_flags)
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

	case VALUE_ARRAY: {
		int x, y;

		/* Note the order here.  */
		for (y = 0; y < value->v_array.y; y++) {
			  for (x = 0; x < value->v_array.x; x++) {
				res = function_iterate_do_value (
					ep, callback, closure,
					value->v_array.vals [x][y],
					strict, CELL_ITER_IGNORE_BLANK);
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
		data.ignore_subtotal = (iter_flags & CELL_ITER_IGNORE_SUBTOTAL);

		res = workbook_foreach_cell_in_range (ep, value, iter_flags,
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
 * @expr_node_list:   a GnmExprList of ExprTrees (what a Gnumeric function would get).
 * @strict:           If TRUE, the function is considered "strict".  This means
 *                   that if an error value occurs as an argument, the iteration
 *                   will stop and that error will be returned.  If FALSE, an
 *                   error will be passed on to the callback (as a Value *
 *                   of type VALUE_ERROR).
 * @iter_flags:
 *
 * Return value:
 *    NULL            : if no errors were reported.
 *    Value *         : if an error was found during strict evaluation
 *    VALUE_TERMINATE : if the callback requested termination of the iteration.
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
				  GnmExprList		*expr_node_list,
				  gboolean		 strict,
				  CellIterFlags		 iter_flags)
{
	Value * result = NULL;

	for (; result == NULL && expr_node_list;
	     expr_node_list = expr_node_list->next) {
		GnmExpr const *expr = expr_node_list->data;
		Value *val;

		if (iter_flags & CELL_ITER_IGNORE_SUBTOTAL &&
		    gnm_expr_containts_subtotal (expr))
			continue;

		/* Permit empties and non scalars. We don't know what form the
		 * function wants its arguments */
		val = gnm_expr_eval (expr, ep, GNM_EXPR_EVAL_PERMIT_NON_SCALAR|GNM_EXPR_EVAL_PERMIT_EMPTY);

		if (val == NULL)
			continue;

		if (strict && val->type == VALUE_ERROR) {
			/* Be careful not to make VALUE_TERMINATE into a real value */
			/* FIXME : Make the new position of the error here */
			return val;
		}

		result = function_iterate_do_value (ep, callback, callback_closure,
						    val, strict, iter_flags);
		value_release (val);
	}
	return result;
}

/* ------------------------------------------------------------------------- */

TokenizedHelp *
tokenized_help_new (GnmFunc const *func)
{
	TokenizedHelp *tok;

	g_return_val_if_fail (func != NULL, NULL);

	if (func->fn_type == GNM_FUNC_TYPE_STUB)
		gnm_func_load_stub ((GnmFunc *) func);

	tok = g_new (TokenizedHelp, 1);
	tok->fndef = func;
	tok->help_copy = NULL;
	tok->sections = NULL;

	if (func->help != NULL && func->help [0] != '\0') {
		char *ptr, *start;
		gboolean seek_at = TRUE;
		gboolean last_newline = TRUE;

		ptr = _(func->help);
		tok->help_is_localized = ptr != func->help;
		tok->help_copy = g_strdup (ptr);
		tok->sections = g_ptr_array_new ();

		for (start = ptr = tok->help_copy; *ptr ; ptr++) {
			if (ptr[0] == '\\' && ptr[1]) {
				ptr = g_utf8_next_char (ptr + 1);
				continue;
			}

			/* FIXME : This is hugely ugly.  we need a decent
			 * format for this stuff.
			 * @SECTION=content is damn ugly considering we
			 * are saying things like @r = bob in side the content.
			 * for now make the assumption that any args will
			 * always start with lower case.
			 */
			if (*ptr == '@' &&
			    g_unichar_isupper (g_utf8_get_char (ptr + 1)) &&
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

		if (g_ascii_strcasecmp (cmp, token) == 0){
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
