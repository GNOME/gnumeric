/*
 * func.c: Function management and utility routines.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Michael Meeks   (mmeeks@gnu.org)
 *  Morten Welinder (terra@gnome.org)
 *  Jody Goldberg   (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <gnm-i18n.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <dependent.h>
#include <expr.h>
#include <expr-impl.h>
#include <expr-name.h>
#include <cell.h>
#include <workbook-priv.h>
#include <sheet.h>
#include <value.h>
#include <number-match.h>
#include <func-builtin.h>
#include <command-context-stderr.h>
#include <gnm-plugin.h>
#include <gutils.h>
#include <gui-util.h>
#include <gnm-marshalers.h>

#include <goffice/goffice.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>

enum {
	PROP_0,
	PROP_NAME,
	PROP_TRANSLATION_DOMAIN,
	PROP_IN_USE,
};

enum {
	SIG_LOAD_STUB,
	SIG_LINK_DEP,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };


#define F2(func,s) dgettext ((func)->tdomain->str, (s))

static GList	    *categories;
static GnmFuncGroup *unknown_cat;

static GHashTable *functions_by_name;
static GHashTable *functions_by_localized_name;

/**
 * functions_init: (skip)
 */
void
functions_init (void)
{
	functions_by_name =
		g_hash_table_new (go_ascii_strcase_hash, go_ascii_strcase_equal);

	/* FIXME: ascii???  */
	functions_by_localized_name =
		g_hash_table_new (go_ascii_strcase_hash, go_ascii_strcase_equal);

	func_builtin_init ();
}

/**
 * functions_shutdown: (skip)
 */
void
functions_shutdown (void)
{
	while (unknown_cat != NULL && unknown_cat->functions != NULL) {
		GnmFunc *func = unknown_cat->functions->data;
		if (func->usage_count > 0) {
			g_warning ("Function %s still has %d users.\n",
				   gnm_func_get_name (func, FALSE),
				   func->usage_count);
			func->usage_count = 0;
		}
		g_object_unref (func);
	}
	func_builtin_shutdown ();

	g_hash_table_destroy (functions_by_name);
	functions_by_name = NULL;

	g_hash_table_destroy (functions_by_localized_name);
	functions_by_localized_name = NULL;
}

/**
 * gnm_func_enumerate:
 *
 * Return value: (element-type GnmFunc) (transfer container):
 */
GPtrArray *
gnm_func_enumerate (void)
{
	GPtrArray *res = g_ptr_array_new ();
	GHashTableIter hiter;
	gpointer value;

	g_hash_table_iter_init (&hiter, functions_by_name);
	while (g_hash_table_iter_next (&hiter, NULL, &value))
		g_ptr_array_add (res, value);

	return res;
}

inline void
gnm_func_load_if_stub (GnmFunc *func)
{
	if (func->fn_type == GNM_FUNC_TYPE_STUB)
		gnm_func_load_stub (func);
}

static char *
split_at_colon (char const *s, char **rest)
{
	char *dup = g_strdup (s);
	char *colon = strchr (dup, ':');
	if (colon) {
		*colon = 0;
		if (rest) *rest = colon + 1;
	} else {
		if (rest) *rest = NULL;
	}
	return dup;
}

/* ------------------------------------------------------------------------- */

static void
gnm_func_group_free (GnmFuncGroup *fn_group)
{
	g_return_if_fail (fn_group != NULL);
	g_return_if_fail (fn_group->functions == NULL);

	if (fn_group->ref_count-- > 1)
		return;

	go_string_unref (fn_group->internal_name);
	go_string_unref (fn_group->display_name);
	g_free (fn_group);
}

static GnmFuncGroup *
gnm_func_group_ref (GnmFuncGroup *fn_group)
{
	fn_group->ref_count++;
	return fn_group;
}

GType
gnm_func_group_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmFuncGroup",
			 (GBoxedCopyFunc)gnm_func_group_ref,
			 (GBoxedFreeFunc)gnm_func_group_free);
	}
	return t;
}

static gint
function_category_compare (gconstpointer a, gconstpointer b)
{
	GnmFuncGroup const *cat_a = a;
	GnmFuncGroup const *cat_b = b;

	return go_string_cmp (cat_a->display_name, cat_b->display_name);
}

GnmFuncGroup *
gnm_func_group_fetch (char const *name, char const *translation)
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
		cat->internal_name = go_string_new (name);
		cat->ref_count = 1;
		if (translation != NULL) {
			cat->display_name = go_string_new (translation);
			cat->has_translation = TRUE;
		} else {
			cat->display_name = go_string_new (name);
			cat->has_translation = FALSE;
		}
		cat->functions = NULL;
		categories = g_list_insert_sorted (
			     categories, cat, &function_category_compare);
	} else if (translation != NULL && translation != name &&
		   !cat->has_translation) {
		go_string_unref (cat->display_name);
		cat->display_name = go_string_new (translation);
		cat->has_translation = TRUE;
		categories = g_list_remove_link (categories, l);
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
gnm_func_group_add_func (GnmFuncGroup *fn_group, GnmFunc *fn_def)
{
	g_return_if_fail (fn_group != NULL);
	g_return_if_fail (fn_def != NULL);

	fn_group->functions = g_slist_prepend (fn_group->functions, fn_def);
}

static void
gnm_func_group_remove_func (GnmFuncGroup *fn_group, GnmFunc *fn_def)
{
	g_return_if_fail (fn_group != NULL);
	g_return_if_fail (fn_def != NULL);

	fn_group->functions = g_slist_remove (fn_group->functions, fn_def);
	if (fn_group->functions == NULL) {
		categories = g_list_remove (categories, fn_group);
		if (unknown_cat == fn_group)
			unknown_cat = NULL;
		gnm_func_group_free (fn_group);
	}
}

/******************************************************************************/

static void
extract_arg_types (GnmFunc *def)
{
	int i;

	gnm_func_count_args (def,
			     &def->fn.args.min_args,
			     &def->fn.args.max_args);
	def->fn.args.arg_types = g_malloc (def->fn.args.max_args + 1);
	for (i = 0; i < def->fn.args.max_args; i++)
		def->fn.args.arg_types[i] = gnm_func_get_arg_type (def, i);
	def->fn.args.arg_types[i] = 0;
}

static GnmValue *
error_function_no_full_info (GnmFuncEvalInfo *ei,
			     int argc,
			     GnmExprConstPtr const *argv)
{
	return value_new_error (ei->pos, _("Function implementation not available."));
}

static void
gnm_func_clear_arg_names (GnmFunc *fd)
{
	if (fd->arg_names_p) {
		g_ptr_array_foreach (fd->arg_names_p, (GFunc) g_free, NULL);
		g_ptr_array_free (fd->arg_names_p, TRUE);
		fd->arg_names_p = NULL;
	}
}

static void
gnm_func_create_arg_names (GnmFunc *fn_def)
{
	int i;
	GPtrArray *ptr;

	g_return_if_fail (fn_def != NULL);

	ptr = g_ptr_array_new ();
	for (i = 0;
	     fn_def->help && fn_def->help[i].type != GNM_FUNC_HELP_END;
	     i++) {
		if (fn_def->help[i].type != GNM_FUNC_HELP_ARG)
			continue;

		g_ptr_array_add
			(ptr, split_at_colon
			 (F2(fn_def, fn_def->help[i].text), NULL));
	}

	gnm_func_clear_arg_names (fn_def);
	fn_def->arg_names_p = ptr;
}


void
gnm_func_load_stub (GnmFunc *func)
{
	g_return_if_fail (func->fn_type == GNM_FUNC_TYPE_STUB);

	g_signal_emit (G_OBJECT (func), signals[SIG_LOAD_STUB], 0);

	if (func->fn_type == GNM_FUNC_TYPE_STUB) {
		static GnmFuncHelp const no_help[] = { { GNM_FUNC_HELP_END } };

		func->help = no_help;
		func->fn.nodes = &error_function_no_full_info;
		gnm_func_set_function_type (func, GNM_FUNC_TYPE_NODES);
	}
}

void
gnm_func_set_function_type (GnmFunc *func, GnmFuncType typ)
{
	g_return_if_fail (GNM_IS_FUNC (func));

	func->fn_type = typ;
	switch (typ) {
	case GNM_FUNC_TYPE_ARGS:
		extract_arg_types (func);
		gnm_func_create_arg_names (func);
		break;
	case GNM_FUNC_TYPE_NODES:
		gnm_func_create_arg_names (func);
		break;
	case GNM_FUNC_TYPE_STUB:
		break;
	}
}


static void
gnm_func_set_localized_name (GnmFunc *fd, const char *lname)
{
	gboolean in_hashes = !(fd->flags & GNM_FUNC_IS_WORKBOOK_LOCAL);

	if (g_strcmp0 (fd->localized_name, lname) == 0)
		return;

	if (in_hashes && fd->localized_name)
		g_hash_table_remove (functions_by_localized_name, fd->localized_name);
	g_free (fd->localized_name);

	fd->localized_name = g_strdup (lname);
	if (in_hashes && lname)
		g_hash_table_insert (functions_by_localized_name,
				     fd->localized_name, fd);
}

/**
 * gnm_func_inc_usage:
 * @func: (transfer none): #GnmFunc
 *
 * This function increments the usage count of @func.  A non-zero usage count
 * prevents the unloading of the function.
 *
 * Returns: (transfer full): a new reference to @func.
 */
GnmFunc *
gnm_func_inc_usage (GnmFunc *func)
{
	g_return_val_if_fail (func != NULL, NULL);

	func->usage_count++;
	if (func->usage_count == 1)
		g_object_notify (G_OBJECT (func), "in-use");
	return func;
}

/**
 * gnm_func_dec_usage:
 * @func: (transfer full): #GnmFunc
 *
 * This function decrements the usage count of @func.  When the usage count
 * reaches zero, the function may be unloaded, for example by unloading the
 * plugin that defines it.
 */
void
gnm_func_dec_usage (GnmFunc *func)
{
	g_return_if_fail (func != NULL);
	g_return_if_fail (func->usage_count > 0);

	func->usage_count--;
	if (func->usage_count == 0)
		g_object_notify (G_OBJECT (func), "in-use");
}

gboolean
gnm_func_get_in_use (GnmFunc *func)
{
	g_return_val_if_fail (func != NULL, FALSE);

	return func->usage_count > 0;
}


/**
 * gnm_func_lookup:
 * @name: name of function
 * @scope: (nullable): scope of function, %NULL for global
 *
 * Returns: (nullable) (transfer none): the function of that name.
 */
GnmFunc *
gnm_func_lookup (char const *name, Workbook *scope)
{
	GnmFunc *fd = g_hash_table_lookup (functions_by_name, name);
	if (fd != NULL)
		return fd;
	if (scope == NULL || scope->sheet_local_functions == NULL)
		return NULL;
	return g_hash_table_lookup (scope->sheet_local_functions, (gpointer)name);
}

/**
 * gnm_func_lookup_localized:
 * @name: localized name of function
 * @scope: (nullable): scope of function, %NULL for global
 *
 * Returns: (nullable) (transfer none): the function of that name.
 */
GnmFunc *
gnm_func_lookup_localized (char const *name, Workbook *scope)
{
	GnmFunc *fd;
	GHashTableIter hiter;
	gpointer value;

	/* Must localize all function names.  */
	g_hash_table_iter_init (&hiter, functions_by_name);
	while (g_hash_table_iter_next (&hiter, NULL, &value)) {
		GnmFunc *fd = value;
		(void)gnm_func_get_name (fd, TRUE);
	}

	fd = g_hash_table_lookup (functions_by_localized_name, name);
	if (fd != NULL)
		return fd;
	if (scope == NULL || scope->sheet_local_functions == NULL)
		return NULL;
	return g_hash_table_lookup (scope->sheet_local_functions, (gpointer)name);
}

/**
 * gnm_func_lookup_prefix:
 * @prefix: prefix to search for
 * @scope:
 * @trans: whether to search translated function names
 *
 * Returns: (element-type GnmFunc*) (transfer full):
 **/
GSList *
gnm_func_lookup_prefix (char const *prefix, Workbook *scope, gboolean trans)
{
	GSList *res = NULL;
	GHashTableIter hiter;
	gpointer value;

	/*
	 * Always iterate over functions_by_name as the localized name
	 * might not be set yet.
	 */
	g_hash_table_iter_init (&hiter, functions_by_name);
	while (g_hash_table_iter_next (&hiter, NULL, &value)) {
		GnmFunc *fd = value;
		if (!(fd->flags & GNM_FUNC_IS_PLACEHOLDER)) {
			const char *name = gnm_func_get_name (fd, trans);
			if (g_str_has_prefix (name, prefix)) {
				gnm_func_inc_usage (fd);
				res = g_slist_prepend (res, fd);
			}
		}
	}

	return res;
}

/**
 * gnm_func_get_translation_domain:
 * @func: #GnmFunc
 *
 * Returns: (transfer none): the translation domain for @func's help text.
 */
char const *
gnm_func_get_translation_domain (GnmFunc *func)
{
	g_return_val_if_fail (GNM_IS_FUNC (func), NULL);
	return func->tdomain->str;
}

/**
 * gnm_func_set_translation_domain:
 * @func: #GnmFunc
 * @tdomain: (nullable): Translation domain, %NULL for Gnumeric's.
 */
void
gnm_func_set_translation_domain (GnmFunc *func, const char *tdomain)
{
	g_return_if_fail (GNM_IS_FUNC (func));

	if (!tdomain)
		tdomain = GETTEXT_PACKAGE;

	if (g_strcmp0 (func->tdomain->str, tdomain) == 0)
		return;

	go_string_unref (func->tdomain);
	func->tdomain = go_string_new (tdomain);

	g_object_notify (G_OBJECT (func), "translation-domain");
}

void
gnm_func_set_function_group (GnmFunc *func, GnmFuncGroup *group)
{
	g_return_if_fail (GNM_IS_FUNC (func));
	g_return_if_fail (group != NULL);

	if (func->fn_group == group)
		return;

	if (func->fn_group)
		gnm_func_group_remove_func (func->fn_group, func);
	func->fn_group = group;
	gnm_func_group_add_func (group, func);

	if (group == unknown_cat)
		func->flags |= GNM_FUNC_IS_PLACEHOLDER;
	else
		func->flags &= ~GNM_FUNC_IS_PLACEHOLDER;
}



/**
 * gnm_func_add:
 * @group:
 * @descriptor:
 * @tdomain: (nullable):
 *
 * Returns: (transfer full): a new #GnmFunc.
 */
GnmFunc *
gnm_func_add (GnmFuncGroup *fn_group,
	      GnmFuncDescriptor const *desc,
	      const char *tdomain)
{
	static char const valid_tokens[] = "fsbraAES?|";
	GnmFunc *func;
	char const *ptr;

	g_return_val_if_fail (fn_group != NULL, NULL);
	g_return_val_if_fail (desc != NULL, NULL);

	func = g_object_new (GNM_FUNC_TYPE,
			     "name", desc->name,
			     NULL);
	gnm_func_set_translation_domain (func, tdomain);

	func->help		= desc->help ? desc->help : NULL;
	func->flags		= desc->flags;
	func->impl_status	= desc->impl_status;
	func->test_status	= desc->test_status;
	func->localized_name    = NULL;
	func->arg_names_p       = NULL;

	func->usage_count	= 0;

	if (desc->fn_args != NULL) {
		/* Check those arguments */
		for (ptr = desc->arg_spec ; *ptr ; ptr++) {
			g_return_val_if_fail (strchr (valid_tokens, *ptr), NULL);
		}

		func->fn.args.func	= desc->fn_args;
		func->fn.args.arg_spec	= desc->arg_spec;
		gnm_func_set_function_type (func, GNM_FUNC_TYPE_ARGS);
	} else if (desc->fn_nodes != NULL) {

		if (desc->arg_spec && *desc->arg_spec) {
			g_warning ("Arg spec for node function -- why?");
		}

		func->fn.nodes = desc->fn_nodes;
		gnm_func_set_function_type (func, GNM_FUNC_TYPE_NODES);
	} else {
		g_warning ("Invalid function has neither args nor nodes handler");
		g_object_unref (func);
		return NULL;
	}

	func->fn_group = fn_group;
	gnm_func_group_add_func (fn_group, func);
	if (!(func->flags & GNM_FUNC_IS_WORKBOOK_LOCAL))
		g_hash_table_insert (functions_by_name,
				     (gpointer)(func->name), func);

	return func;
}

/* Handle unknown functions on import without losing their names */
static GnmValue *
unknownFunctionHandler (GnmFuncEvalInfo *ei,
			G_GNUC_UNUSED int argc,
			G_GNUC_UNUSED GnmExprConstPtr const *argv)
{
	return value_new_error_NAME (ei->pos);
}

static char *
invent_name (const char *pref, GHashTable *h, const char *template)
{
	static int count = 0;
	char *name = g_utf8_strdown (pref, -1);

	while (g_hash_table_lookup (h, name)) {
		count++;
		g_free (name);
		name = g_strdup_printf (template, count);
	}

	return name;
}

static GnmFunc *
gnm_func_add_placeholder_full (Workbook *scope,
			       char const *gname, char const *lname,
			       char const *type)
{
	GnmFuncDescriptor desc;
	GnmFunc *func;
	char const *unknown_cat_name = N_("Unknown Function");
	gboolean copy_gname = TRUE;
	gboolean copy_lname = TRUE;

	g_return_val_if_fail (gname || lname, NULL);
	g_return_val_if_fail (gname == NULL || gnm_func_lookup (gname, scope) == NULL, NULL);
	g_return_val_if_fail (lname == NULL || gnm_func_lookup_localized (lname, scope) == NULL, NULL);

	if (!unknown_cat)
		unknown_cat = gnm_func_group_fetch
			(unknown_cat_name, _(unknown_cat_name));

	if (!gname) {
		/*
		 * This is actually a bit of a problem if we don't end up
		 * with a copy of lname (because there already is a function
		 * with that name).  We're likely to save a template name,
		 * but I don't see what else to do.
		 */
		gname = invent_name (lname, functions_by_name, "unknown%d");
		copy_gname = FALSE;
	}
	if (!lname) {
		/* xgettext: This represents a made-up translated function name.  */
		lname = invent_name (gname, functions_by_localized_name, _("unknown%d"));
		copy_lname = FALSE;
	}

	if (gnm_debug_flag ("func"))
		g_printerr ("Adding placeholder for %s (aka %s)\n", gname, lname);

	memset (&desc, 0, sizeof (GnmFuncDescriptor));
	desc.name	  = gname;
	desc.arg_spec	  = NULL;
	desc.help	  = NULL;
	desc.fn_args	  = NULL;
	desc.fn_nodes	  = &unknownFunctionHandler;
	desc.flags	  = GNM_FUNC_IS_PLACEHOLDER;
	desc.impl_status  = GNM_FUNC_IMPL_STATUS_EXISTS;
	desc.test_status  = GNM_FUNC_TEST_STATUS_UNKNOWN;

	if (scope != NULL)
		desc.flags |= GNM_FUNC_IS_WORKBOOK_LOCAL;
	else {
#if 0
		/* WISHLIST : it would be nice to have a log if these. */
		g_warning ("Unknown %s function : %s", type, desc.name);
#endif
	}

	func = gnm_func_add (unknown_cat, &desc, NULL);

	if (lname) {
		gnm_func_set_localized_name (func, lname);
		if (!copy_lname)
			g_free ((char *)lname);
	}

	if (!copy_gname)
		g_free ((char *)gname);

	if (scope != NULL) {
		if (scope->sheet_local_functions == NULL)
			scope->sheet_local_functions = g_hash_table_new_full (
				g_str_hash, g_str_equal,
				NULL, g_object_unref);
		g_hash_table_insert (scope->sheet_local_functions,
			(gpointer)func->name, func);
	}

	return func;
}

/**
 * gnm_func_add_placeholder:
 * @scope: (nullable): scope to defined placeholder, %NULL for global
 * @name: (nullable): function name
 * @type:
 *
 * Returns: (transfer none): a placeholder with the given name.
 */
GnmFunc *
gnm_func_add_placeholder (Workbook *scope,
			  char const *name, char const *type)
{
	return gnm_func_add_placeholder_full (scope, name, NULL, type);
}

/**
 * gnm_func_add_placeholder_localized:
 * @gname: (nullable): function name
 * @lname: localized function name
 *
 * Returns: (transfer none): a placeholder with the given localized name.
 */
GnmFunc *
gnm_func_add_placeholder_localized (char const *gname, char const *lname)
{
	return gnm_func_add_placeholder_full (NULL, gname, lname, "?");
}

/**
 * gnm_func_lookup_or_add_placeholder:
 * @name: function name
 *
 * Returns: (transfer none): a #GnmFunc named @name, either an existing
 * one or a placeholder.
 */
GnmFunc	*
gnm_func_lookup_or_add_placeholder (char const *name)
{
	GnmFunc	* f = gnm_func_lookup (name, NULL);
	if (f == NULL)
		f = gnm_func_add_placeholder (NULL, name, "");
	return f;
}

/**
 * gnm_func_get_name:
 * @func: #GnmFunc to query
 * @localized: if %TRUE, use localized name
 *
 * Returns: (transfer none): @func's name
 */
char const *
gnm_func_get_name (GnmFunc const *func, gboolean localized)
{
	int i;
	GnmFunc *fd = (GnmFunc *)func;

	g_return_val_if_fail (func != NULL, NULL);

	if (!localized)
		return func->name;

	if (func->localized_name)
		return func->localized_name;

	/*
	 * Deduce the translated names from the help texts.  This
	 * code doesn't currently check for clashes in translated
	 * names.
	 */

	gnm_func_load_if_stub (fd);

	for (i = 0;
	     (func->localized_name == NULL &&
	      func->help &&
	      func->help[i].type != GNM_FUNC_HELP_END);
	     i++) {
		const char *s, *sl;
		char *U;
		if (func->help[i].type != GNM_FUNC_HELP_NAME)
			continue;

		s = func->help[i].text;
		sl = F2 (func, s);
		if (s == sl) /* String not actually translated. */
			continue;

		U = split_at_colon (F2 (func, s), NULL);
		if (U) {
			char *lname = g_utf8_strdown (U, -1);
			gnm_func_set_localized_name (fd, lname);
			g_free (lname);
		}
		g_free (U);
	}

	if (!func->localized_name)
		gnm_func_set_localized_name (fd, fd->name);

	return func->localized_name;
}

/**
 * gnm_func_get_description:
 * @fn_def: the fn defintion
 *
 * Returns: (transfer none): the description of the function
 **/
char const*
gnm_func_get_description (GnmFunc const *fn_def)
{
	gint i;
	g_return_val_if_fail (fn_def != NULL, NULL);

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	for (i = 0;
	     fn_def->help && fn_def->help[i].type != GNM_FUNC_HELP_END;
	     i++) {
		const char *desc;

		if (fn_def->help[i].type != GNM_FUNC_HELP_NAME)
			continue;

		desc = strchr (F2 (fn_def, fn_def->help[i].text), ':');
		return desc ? (desc + 1) : "";
	}
	return "";
}

/**
 * gnm_func_count_args:
 * @fn_def: pointer to function definition
 * @min: (out): location for mininum args
 * @max: (out): location for mininum args
 *
 * This calculates the maximum and minimum number of args that can be passed.
 * For a vararg function, the maximum will be set to G_MAXINT.
 **/
void
gnm_func_count_args (GnmFunc const *fn_def, int *min, int *max)
{
	g_return_if_fail (min != NULL);
	g_return_if_fail (max != NULL);
	g_return_if_fail (fn_def != NULL);

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	switch (fn_def->fn_type) {
	case GNM_FUNC_TYPE_NODES:
		*min = 0;
		// Really?
		if (g_ascii_strcasecmp ("INDEX",fn_def->name) == 0)
			*max = 4;
		else
			*max = G_MAXINT;
		return;

	case GNM_FUNC_TYPE_ARGS: {
		const char *ptr = fn_def->fn.args.arg_spec;
		int i, vararg;

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
		return;
	}

	default:
		*min = 0;
		*max = G_MAXINT;
		return;
	}
}

/**
 * gnm_func_get_arg_type:
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Returns: the type of the argument
 **/
char
gnm_func_get_arg_type (GnmFunc const *fn_def, int arg_idx)
{
	char const *ptr;

	g_return_val_if_fail (arg_idx >= 0, '?');
	g_return_val_if_fail (fn_def != NULL, '?');

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

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
 * gnm_func_get_arg_type_string:
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Return value: (transfer none): the type of the argument as a string
 **/
char const *
gnm_func_get_arg_type_string (GnmFunc const *fn_def,
			      int arg_idx)
{
	switch (gnm_func_get_arg_type (fn_def, arg_idx)) {
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
		return _("Scalar, Blank, or Error");
	case 'S':
		return _("Scalar");
	case '?':
		/* Missing values will be NULL.  */
		return _("Any");

	default:
		g_warning ("Unknown arg type");
		return "Broken";
	}
}

/**
 * gnm_func_get_arg_name:
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Returns: (transfer full): the name of the argument
 **/
char *
gnm_func_get_arg_name (GnmFunc const *fn_def, guint arg_idx)
{
	g_return_val_if_fail (fn_def != NULL, NULL);

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	if ((fn_def->arg_names_p != NULL)
	    && (arg_idx < fn_def->arg_names_p->len))
		return g_strdup (g_ptr_array_index (fn_def->arg_names_p,
						    arg_idx));
	return NULL;
}

/**
 * gnm_func_get_arg_description:
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Returns: (transfer none): the description of the argument
 **/
char const*
gnm_func_get_arg_description (GnmFunc const *fn_def, guint arg_idx)
{
	gint i;
	g_return_val_if_fail (fn_def != NULL, NULL);

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	for (i = 0;
	     fn_def->help && fn_def->help[i].type != GNM_FUNC_HELP_END;
	     i++) {
		gchar const *desc;

		if (fn_def->help[i].type != GNM_FUNC_HELP_ARG)
			continue;
		if (arg_idx--)
			continue;

		desc = strchr (F2 (fn_def, fn_def->help[i].text), ':');
		if (!desc)
			return "";

		desc++;
		while (g_unichar_isspace (g_utf8_get_char (desc)))
			desc = g_utf8_next_char (desc);
		return desc;
	}

	return "";
}

/**
 * gnm_func_convert_markup_to_pango:
 * @desc: the fn or arg description string
 * @target: target widget for the markup.
 *
 * Return value: the escaped string with @{} markup converted to
 *               pango markup
 **/
char *
gnm_func_convert_markup_to_pango (char const *desc, GtkWidget *target)
{
	GString *str;
	gchar *markup, *at;
	GdkRGBA link_color;
	PangoColor pg;
	char *link_color_text, *span_text;
	size_t span_text_len;

	gnm_get_link_color (target, &link_color);
	pg.red = 65535 * link_color.red;
	pg.green = 65535 * link_color.green;
	pg.blue = 65535 * link_color.blue;
	link_color_text = pango_color_to_string (&pg);
	span_text = g_strdup_printf ("<span foreground=\"%s\">",
				     link_color_text);
	span_text_len = strlen (span_text);
	g_free (link_color_text);

	markup = g_markup_escape_text (desc, -1);
	str = g_string_new (markup);
	g_free (markup);

	while ((at = strstr (str->str, "@{"))) {
		gint len = at - str->str;
		go_string_replace (str, len, 2, span_text, -1);
		if ((at = strstr
		     (str->str + len + span_text_len, "}"))) {
			len = at - str->str;
			go_string_replace (str, len, 1, "</span>", -1);
		} else
			g_string_append (str, "</span>");
	}
	g_free (span_text);

	return g_string_free (str, FALSE);
}


/* ------------------------------------------------------------------------- */

static inline void
free_values (GnmValue **values, int top)
{
	int i;

	for (i = 0; i < top; i++)
		if (values [i])
			value_release (values [i]);
}

/* ------------------------------------------------------------------------- */

/**
 * function_call_with_exprs:
 * @ei: EvalInfo containing valid fn_def!
 *
 * Do the guts of calling a function.
 *
 * Returns the result.
 **/
GnmValue *
function_call_with_exprs (GnmFuncEvalInfo *ei)
{
	GnmFunc const *fn_def;
	int	  i, iter_count, iter_width = 0, iter_height = 0;
	char	  arg_type;
	GnmValue	 **args, *tmp = NULL;
	int	 *iter_item = NULL;
	int argc;
	GnmExprConstPtr *argv;
	GnmExprEvalFlags flags, pass_flags;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_call != NULL, NULL);

	flags = ei->flags;

	argc = ei->func_call->argc;
	argv = ei->func_call->argv;
	fn_def = ei->func_call->func;

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	/* Functions that deal with ExprNodes */
	if (fn_def->fn_type == GNM_FUNC_TYPE_NODES)
		return fn_def->fn.nodes (ei, argc, argv);

	/* Functions that take pre-computed Values */
	if (argc > fn_def->fn.args.max_args ||
	    argc < fn_def->fn.args.min_args)
		return value_new_error_NA (ei->pos);

	args = g_alloca (sizeof (GnmValue *) * fn_def->fn.args.max_args);
	iter_count = (eval_pos_is_array_context (ei->pos) &&
		      (flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR))
		? 0 : -1;

	/* Optimization for IF when implicit iteration is not used.  */
	if (ei->func_call->func->fn.args.func == gnumeric_if &&
	    iter_count == -1)
		return gnumeric_if2 (ei, argc, argv, flags);

	pass_flags = (flags &
		      (GNM_EXPR_EVAL_ARRAY_CONTEXT));

	for (i = 0; i < argc; i++) {
		char arg_type = fn_def->fn.args.arg_types[i];
		/* expr is always non-null, missing args are encoded as
		 * const = empty */
		GnmExpr const *expr = argv[i];

		if (arg_type == 'A' || arg_type == 'r') {
			tmp = args[i] = gnm_expr_eval
				(expr, ei->pos,
				 pass_flags |
				 GNM_EXPR_EVAL_PERMIT_NON_SCALAR |
				 GNM_EXPR_EVAL_WANT_REF);
			if (VALUE_IS_ERROR (tmp)) {
				free_values (args, i);
				return tmp;
			}

			if (VALUE_IS_CELLRANGE (tmp)) {
				gnm_cellref_make_abs (&tmp->v_range.cell.a,
						      &tmp->v_range.cell.a,
						      ei->pos);
				gnm_cellref_make_abs (&tmp->v_range.cell.b,
						      &tmp->v_range.cell.b,
						      ei->pos);
				/* Array args accept scalars */
			} else if (arg_type != 'A' && !VALUE_IS_ARRAY (tmp)) {
				free_values (args, i + 1);
				return value_new_error_VALUE (ei->pos);
			}
			continue;
		}

		/* force scalars whenever we are certain */
		tmp = args[i] = gnm_expr_eval
			(expr, ei->pos,
			 pass_flags |
			 GNM_EXPR_EVAL_PERMIT_EMPTY |
			 (iter_count >= 0 || arg_type == '?'
			  ? GNM_EXPR_EVAL_PERMIT_NON_SCALAR
			  : 0));

		if (arg_type == '?')	/* '?' arguments are unrestriced */
			continue;

		/* optional arguments can be blank */
		if (i >= fn_def->fn.args.min_args && VALUE_IS_EMPTY (tmp)) {
			if (arg_type == 'E' && !gnm_expr_is_empty (expr)) {
				/* An actual argument produced empty.  Make
				   sure function sees that.  */
				args[i] = value_new_empty ();
			}

			continue;
		}

		if (tmp == NULL)
			tmp = args[i] = value_new_empty ();

		/* Handle implicit intersection or iteration depending on flags */
		if (VALUE_IS_CELLRANGE (tmp) || VALUE_IS_ARRAY (tmp)) {
			if (iter_count > 0) {
				if (iter_width != value_area_get_width (tmp, ei->pos) ||
				    iter_height != value_area_get_height (tmp, ei->pos)) {
					free_values (args, i + 1);
					return value_new_error_VALUE (ei->pos);
				}
			} else {
				if (iter_count < 0) {
					g_warning ("Damn I thought this was impossible");
					iter_count = 0;
				}
				iter_item = g_alloca (sizeof (int) * argc);
				iter_width = value_area_get_width (tmp, ei->pos);
				iter_height = value_area_get_height (tmp, ei->pos);
			}
			iter_item [iter_count++] = i;

			/* no need to check type, we would fail comparing a range against a "b, f, or s" */
			continue;
		}

		/* All of these argument types must be scalars */
		switch (arg_type) {
		case 'b':
			if (VALUE_IS_STRING (tmp)) {
				gboolean err;
				gboolean b = value_get_as_bool (tmp, &err);
				if (err) {
					free_values (args, i + 1);
					return value_new_error_VALUE (ei->pos);
				}
				value_release (args[i]);
				tmp = args[i] = value_new_bool (b);
				break;
			}
			/* Fall through.  */
		case 'f':
			if (VALUE_IS_STRING (tmp)) {
				tmp = format_match_number (value_peek_string (tmp), NULL,
					sheet_date_conv (ei->pos->sheet));
				if (tmp == NULL) {
					free_values (args, i + 1);
					return value_new_error_VALUE (ei->pos);
				}
				value_release (args [i]);
				args[i] = tmp;
			} else if (VALUE_IS_ERROR (tmp)) {
				free_values (args, i);
				return tmp;
			} else if (VALUE_IS_EMPTY (tmp)) {
				value_release (args [i]);
				tmp = args[i] = value_new_int (0);
			}

			if (!VALUE_IS_NUMBER (tmp))
				return value_new_error_VALUE (ei->pos);
			break;

		case 's':
		case 'S':
			if (VALUE_IS_ERROR (tmp)) {
				free_values (args, i);
				return tmp;
			}
			break;

		case 'E': /* nothing necessary */
			break;

		/* case '?': handled above */
		default:
			g_warning ("Unknown argument type '%c'", arg_type);
			break;
		}
	}

	while (i < fn_def->fn.args.max_args)
		args [i++] = NULL;

	if (iter_item != NULL) {
		int x, y;
		GnmValue *res = value_new_array_empty (iter_width, iter_height);
		GnmValue const *elem, *err;
		GnmValue **iter_vals = g_alloca (sizeof (GnmValue *) * iter_count);
		GnmValue **iter_args = g_alloca (sizeof (GnmValue *) * iter_count);

		/* collect the args we will iterate on */
		for (i = 0 ; i < iter_count; i++)
			iter_vals[i] = args[iter_item[i]];

		for (x = iter_width; x-- > 0 ; )
			for (y = iter_height; y-- > 0 ; ) {
				/* marshal the args */
				err = NULL;
				for (i = 0 ; i < iter_count; i++) {
					elem = value_area_get_x_y (iter_vals[i], x, y, ei->pos);
					arg_type = fn_def->fn.args.arg_types[iter_item[i]];
					if  (arg_type == 'b' || arg_type == 'f') {
						if (VALUE_IS_EMPTY (elem))
							elem = value_zero;
						else if (VALUE_IS_STRING (elem)) {
							tmp = format_match_number (value_peek_string (elem), NULL,
								sheet_date_conv (ei->pos->sheet));
							if (tmp != NULL) {
								args [iter_item[i]] = iter_args [i] = tmp;
								continue;
							} else
								break;
						} else if (VALUE_IS_ERROR (elem)) {
							err = elem;
							break;
						} else if (!VALUE_IS_NUMBER (elem))
							break;
					} else if (arg_type == 's') {
						if (VALUE_IS_EMPTY (elem)) {
							args [iter_item[i]] = iter_args [i] = value_new_string ("");
							continue;
						} else if (VALUE_IS_ERROR (elem)) {
							err = elem;
							break;
						} else if (!VALUE_IS_STRING (elem))
							break;
					} else if (elem == NULL) {
						args [iter_item[i]] = iter_args [i] = value_new_empty ();
						continue;
					}
					args [iter_item[i]] = iter_args [i] = value_dup (elem);
				}

				res->v_array.vals[x][y] = (i == iter_count)
					? fn_def->fn.args.func (ei, (GnmValue const * const *)args)
					: ((err != NULL) ? value_dup (err)
							 : value_new_error_VALUE (ei->pos));
				free_values (iter_args, i);
			}

		/* free the primaries, not the already freed iteration */
		for (i = 0 ; i < iter_count; i++)
			args[iter_item[i]] = iter_vals[i];
		tmp = res;
		i = fn_def->fn.args.max_args;
	} else
		tmp = fn_def->fn.args.func (ei, (GnmValue const * const *)args);

	free_values (args, i);
	return tmp;
}

/*
 * Use this to invoke a register function: the only drawback is that
 * you have to compute/expand all of the values to use this
 */
GnmValue *
function_call_with_values (GnmEvalPos const *ep, char const *fn_name,
			   int argc, GnmValue const * const *values)
{
	GnmFunc *fn_def;

	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (fn_name != NULL, NULL);
	g_return_val_if_fail (ep->sheet != NULL, NULL);

	/* FIXME : support workbook local functions */
	fn_def = gnm_func_lookup (fn_name, NULL);
	if (fn_def == NULL)
		return value_new_error_NAME (ep);
	return function_def_call_with_values (ep, fn_def, argc, values);
}

GnmValue *
function_def_call_with_values (GnmEvalPos const *ep, GnmFunc const *fn_def,
                               int argc, GnmValue const * const *values)
{
	GnmValue *retval;
	GnmExprFunction	ef;
	GnmFuncEvalInfo fs;

	fs.pos = ep;
	fs.func_call = &ef;
	ef.func = (GnmFunc *)fn_def;

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	if (fn_def->fn_type == GNM_FUNC_TYPE_NODES) {
		/*
		 * If function deals with ExprNodes, create some
		 * temporary ExprNodes with constants.
		 */
		GnmExprConstant *expr = g_new (GnmExprConstant, argc);
		GnmExprConstPtr *argv = g_new (GnmExprConstPtr, argc);
		int i;

		for (i = 0; i < argc; i++) {
			gnm_expr_constant_init (expr + i, values[i]);
			argv[i] = (GnmExprConstPtr)(expr + i);
		}
		retval = fn_def->fn.nodes (&fs, argc, argv);
		g_free (argv);
		g_free (expr);
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

/**
 * cb_iterate_cellrange:
 *
 * Helper routine used by the function_iterate_do_value routine.
 * Invoked by the sheet cell range iterator.
 **/
static GnmValue *
cb_iterate_cellrange (GnmCellIter const *iter, gpointer user)

{
	IterateCallbackClosure *data = user;
	GnmCell  *cell;
	GnmValue *res;
	GnmEvalPos ep;

	if (NULL == (cell = iter->cell)) {
		ep.sheet = iter->pp.sheet;
		ep.dep = NULL;
		ep.eval.col = iter->pp.eval.col;
		ep.eval.row = iter->pp.eval.row;
		return (*data->callback)(&ep, NULL, data->closure);
	}

	if (data->ignore_subtotal && gnm_cell_has_expr (cell) &&
	    gnm_expr_top_contains_subtotal (cell->base.texpr))
		return NULL;

	gnm_cell_eval (cell);
	eval_pos_init_cell (&ep, cell);

	/* If we encounter an error for the strict case, short-circuit here.  */
	if (data->strict && (NULL != (res = gnm_cell_is_error (cell))))
		return value_new_error_str (&ep, res->v_err.mesg);

	/* All other cases -- including error -- just call the handler.  */
	return (*data->callback)(&ep, cell->value, data->closure);
}

/*
 * function_iterate_do_value:
 *
 * Helper routine for function_iterate_argument_values.
 */
static GnmValue *
function_iterate_do_value (GnmEvalPos const  *ep,
			   FunctionIterateCB  callback,
			   gpointer	      closure,
			   GnmValue const    *value,
			   gboolean           strict,
			   CellIterFlags      iter_flags)
{
	GnmValue *res = NULL;

	switch (value->v_any.type){
	case VALUE_ERROR:
		if (strict) {
			res = value_dup (value);
			break;
		}
		/* Fall through.  */

	case VALUE_EMPTY:
	case VALUE_BOOLEAN:
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
		data.ignore_subtotal = (iter_flags & CELL_ITER_IGNORE_SUBTOTAL) != 0;

		res = workbook_foreach_cell_in_range (ep, value, iter_flags,
						      cb_iterate_cellrange,
						      &data);
	}
	}
	return res;
}

/**
 * function_iterate_argument_values:
 * @ep:               The position in a workbook at which to evaluate
 * @callback: (scope call): The routine to be invoked for every value computed
 * @callback_closure: Closure for the callback.
 * @argc:
 * @argv:
 * @strict:           If TRUE, the function is considered "strict".  This means
 *                   that if an error value occurs as an argument, the iteration
 *                   will stop and that error will be returned.  If FALSE, an
 *                   error will be passed on to the callback (as a GnmValue *
 *                   of type VALUE_ERROR).
 * @iter_flags:
 *
 * Return value:
 *    NULL            : if no errors were reported.
 *    GnmValue *         : if an error was found during strict evaluation
 *    VALUE_TERMINATE : if the callback requested termination of the iteration.
 *
 * This routine provides a simple way for internal functions with variable
 * number of arguments to be written: this would iterate over a list of
 * expressions (expr_node_list) and will invoke the callback for every
 * GnmValue found on the list (this means that ranges get properly expaned).
 **/
GnmValue *
function_iterate_argument_values (GnmEvalPos const	*ep,
				  FunctionIterateCB	 callback,
				  void			*callback_closure,
				  int                    argc,
				  GnmExprConstPtr const *argv,
				  gboolean		 strict,
				  CellIterFlags		 iter_flags)
{
	GnmValue *result = NULL;
	int a;

	for (a = 0; result == NULL && a < argc; a++) {
		GnmExpr const *expr = argv[a];
		GnmValue *val;

		if (iter_flags & CELL_ITER_IGNORE_SUBTOTAL &&
		    gnm_expr_contains_subtotal (expr))
			continue;

		/* need to drill down into names to handle things like
		 * sum(name)  with name := (A:A,B:B) */
		while (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_NAME) {
			GnmExprTop const *texpr = expr->name.name->texpr;
			expr = texpr ? texpr->expr : NULL;
			if (expr == NULL) {
				if (strict)
					return value_new_error_REF (ep);
				break;
			}
		}
		if (!expr)
			continue;

		/* Handle sets as a special case */
		if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_SET) {
			result = function_iterate_argument_values
				(ep, callback, callback_closure,
				 expr->set.argc, expr->set.argv,
				 strict, iter_flags);
			continue;
		}

		/* We need a cleaner model of what to do here.
		 * In non-array mode
		 *	SUM(Range)
		 * will obviously return Range
		 *
		 *	SUM(INDIRECT(Range))
		 *	SUM(INDIRECT(Range):....)
		 * will do implicit intersection on Range (in non-array mode),
		 * but allow non-scalar results from indirect (no intersection)
		 *
		 *	SUM(Range=3)
		 * will do implicit intersection in non-array mode */
		if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_CONSTANT)
			val = value_dup (expr->constant.value);
		else if (eval_pos_is_array_context (ep) ||
			 GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL ||
			 GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_RANGE_CTOR ||
			 GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_INTERSECT)
			val = gnm_expr_eval (expr, ep,
				GNM_EXPR_EVAL_PERMIT_EMPTY | GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
		else
			val = gnm_expr_eval (expr, ep, GNM_EXPR_EVAL_PERMIT_EMPTY);

		if (val == NULL)
			continue;

		if (strict && VALUE_IS_ERROR (val)) {
			/* Be careful not to make VALUE_TERMINATE into a real value */
			return val;
		}

		result = function_iterate_do_value (ep, callback, callback_closure,
						    val, strict, iter_flags);
		value_release (val);
	}
	return result;
}


/**
 * gnm_eval_info_get_func:
 * @ei: #GnmFuncEvalInfo
 *
 * Returns: (transfer none): the called function.
 */
GnmFunc const *
gnm_eval_info_get_func (GnmFuncEvalInfo const *ei)
{
	return ei->func_call->func;
}

int
gnm_eval_info_get_arg_count (GnmFuncEvalInfo const *ei)
{
	return ei->func_call->argc;
}

GnmDependentFlags
gnm_func_link_dep (GnmFunc *func, GnmFuncEvalInfo *ei, gboolean qlink)
{
	int res = DEPENDENT_NO_FLAG;
	g_signal_emit (func, signals[SIG_LINK_DEP], 0, ei, qlink, &res);
	return (GnmDependentFlags)res;
}

/* ------------------------------------------------------------------------- */

static GObjectClass *parent_class;

typedef struct {
	GObjectClass parent;

	void (*load_stub) (GnmFunc *func);
	int (*link_dep) (GnmFunc *func, GnmFuncEvalInfo *ei, gboolean qlink);
} GnmFuncClass;

static void
gnm_func_finalize (GObject *obj)
{
	GnmFunc *func = GNM_FUNC (obj);

	if (func->usage_count != 0) {
		g_printerr ("Function %s still has a usage count of %d\n",
			    func->name, func->usage_count);
	}

	if (func->fn_group) {
		gnm_func_group_remove_func (func->fn_group, func);
		func->fn_group = NULL;
	}

	gnm_func_set_localized_name (func, NULL);

	if (!(func->flags & GNM_FUNC_IS_WORKBOOK_LOCAL)) {
		g_hash_table_remove (functions_by_name, func->name);
	}

	if (func->fn_type == GNM_FUNC_TYPE_ARGS)
		g_free (func->fn.args.arg_types);

	g_free ((char *)func->name);

	go_string_unref (func->tdomain);

	gnm_func_clear_arg_names (func);

	parent_class->finalize (obj);
}

static void
gnm_func_get_property (GObject *object, guint property_id,
		       GValue *value, GParamSpec *pspec)
{
	GnmFunc *func = (GnmFunc *)object;

	switch (property_id) {
	case PROP_NAME:
		g_value_set_string (value, func->name);
		break;
	case PROP_TRANSLATION_DOMAIN:
		g_value_set_string (value, func->tdomain->str);
		break;
	case PROP_IN_USE:
		g_value_set_boolean (value, func->usage_count > 0);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_func_set_property (GObject *object, guint property_id,
		       GValue const *value, GParamSpec *pspec)
{
	GnmFunc *func = (GnmFunc *)object;

	switch (property_id) {
	case PROP_NAME:
		func->name = g_value_dup_string (value);
		break;
	case PROP_TRANSLATION_DOMAIN:
		gnm_func_set_translation_domain (func,
						 g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_func_init (GnmFunc *func)
{
	func->tdomain = go_string_new (GETTEXT_PACKAGE);
}

static void
gnm_func_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->finalize         = gnm_func_finalize;
	gobject_class->get_property	= gnm_func_get_property;
	gobject_class->set_property	= gnm_func_set_property;

        g_object_class_install_property (gobject_class, PROP_NAME,
		 g_param_spec_string ("name",
				      P_("Name"),
				      P_("The name of the function."),
				      NULL,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE |
				      G_PARAM_CONSTRUCT_ONLY));

        g_object_class_install_property (gobject_class, PROP_TRANSLATION_DOMAIN,
		 g_param_spec_string ("translation-domain",
				      P_("Translation Domain"),
				      P_("The translation domain for help texts"),
				      NULL,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));

        g_object_class_install_property (gobject_class, PROP_IN_USE,
		 g_param_spec_boolean ("in-use",
				       P_("In use"),
				       P_("Is function being used?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READABLE));

	signals[SIG_LOAD_STUB] = g_signal_new
		("load-stub",
		 GNM_FUNC_TYPE,
		 G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET (GnmFuncClass, load_stub),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__VOID,
		 G_TYPE_NONE, 0);


	signals[SIG_LINK_DEP] = g_signal_new
		("link-dep",
		 GNM_FUNC_TYPE,
		 G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET (GnmFuncClass, link_dep),
		 NULL, NULL,
		 gnm__INT__POINTER_BOOLEAN,
		 // GnmDependentFlags ... GnmFuncEvalInfo
		 G_TYPE_INT, 2, G_TYPE_POINTER, G_TYPE_BOOLEAN);
}

GSF_CLASS (GnmFunc, gnm_func,
	   gnm_func_class_init, gnm_func_init, G_TYPE_OBJECT)
