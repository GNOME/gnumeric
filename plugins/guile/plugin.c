/* -*- mode: c; c-basic-offset: 8 -*- */
/*
 *
 *     Authors: Mark Probst
 *              Ariel Rios <ariel@arcavia.com>
 *	   Copyright Mark Probst, Ariel Rios 2000
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307 USA
 */

#include <config.h>
#include <glib.h>
#include <assert.h>
#include <stdio.h>
#include <libguile.h>
#include <gnome.h>

#include "gnumeric.h"
#include "plugin.h"
#include "plugin-util.h"
#include "error-info.h"
#include "module-plugin-defs.h"
#include "expr.h"
#include "gutils.h"
#include "func.h"
#include "cell.h"
#include "value.h"
#include "main.h"
#include "command-context.h"
#include "guile-support.h"
#include "smob-value.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/* This is damn ugly.
 * However, it will get things working again (I hope)
 * until someone who actually uses this thing takes
 * over maintaing it.
 */
static EvalPos const *eval_pos = NULL;

static Value*
func_scm_apply (FunctionEvalInfo *ei, GList *expr_node_list)
{
	int i;
	Value *value;
	char *symbol;
	SCM args = SCM_EOL,
		function,
		result;

	if (g_list_length(expr_node_list) < 1)
		return value_new_error (ei->pos, _("Invalid number of arguments"));

	/* Retrieve the function name,  This can be empty, but not a non scalar */
	value = eval_expr (ei->pos, (ExprTree*)expr_node_list->data, EVAL_PERMIT_EMPTY);
	if (value == NULL)
		return value_new_error (ei->pos, _("First argument to SCM must be a Guile expression"));

	symbol = value_get_as_string (value);
	if (symbol == NULL)
		/* FIXME : This looks like a leak (JEG 4/4/00) */
		return value_new_error (ei->pos, _("First argument to SCM must be a Guile expression"));

	function = scm_eval_0str(symbol);
	if (SCM_UNBNDP(function))
		return value_new_error (ei->pos, _("Undefined scheme function"));

	value_release(value);

	for (i = g_list_length(expr_node_list) - 1; i >= 1; --i)
	{
		CellRef eval_cell;

		eval_cell.col = ei->pos->eval.col;
		eval_cell.row = ei->pos->eval.row;
		eval_cell.col_relative = 0;
		eval_cell.row_relative = 0;
		eval_cell.sheet = NULL;

		/* Evaluate each argument, non scalar is ok, but empty is not */
		value = eval_expr (ei->pos, (ExprTree*)g_list_nth(expr_node_list, i)->data,
				   EVAL_PERMIT_NON_SCALAR);
		if (value == NULL)
			return value_new_error (ei->pos, _("Could not evaluate argument"));

		args = scm_cons(value_to_scm(value, eval_cell), args);
		value_release(value);
	}

	result = scm_apply(function, args, SCM_EOL);

	return scm_to_value(result);
}

static SCM
scm_gnumeric_funcall (SCM funcname, SCM arglist)
{
	int i, num_args;
	Value **values;
	CellRef cell_ref = { 0, 0, 0, 0 };

	SCM_ASSERT (SCM_NIMP (funcname) && SCM_STRINGP (funcname), funcname, SCM_ARG1, "gnumeric-funcall");
	SCM_ASSERT (SCM_NFALSEP (scm_list_p (arglist)), arglist, SCM_ARG2, "gnumeric-funcall");

	num_args = scm_ilength (arglist);
	values = g_new (Value *, num_args);
	for (i = 0; i < num_args; ++i) {
		values[i] = scm_to_value (SCM_CAR (arglist));
		arglist = SCM_CDR (arglist);
	}

	return value_to_scm (function_call_with_values (eval_pos,
							SCM_CHARS (funcname),
							num_args,
							values),
			     cell_ref);
}

static Value*
func_marshal_func (FunctionEvalInfo *ei, Value *argv[])
{
	FunctionDefinition const *fndef = ei->func_def;
	SCM args = SCM_EOL, result, function;
	CellRef dummy = { 0, 0, 0, 0 };
	EvalPos const *old_eval_pos;
	int i, min, max;

	function_def_count_args (fndef, &min, &max);

	function = (SCM) function_def_get_user_data (fndef);

	for (i = min - 1; i >= 0; --i)
		args = scm_cons (value_to_scm (argv [i], dummy), args);

	old_eval_pos = eval_pos;
	eval_pos     = ei->pos;
	result       = scm_apply (function, args, SCM_EOL);
	eval_pos     = old_eval_pos;

	return scm_to_value (result);
}

static SCM
scm_register_function (SCM scm_name, SCM scm_args, SCM scm_help, SCM scm_category, SCM scm_function)
{
	FunctionDefinition *fndef;
	FunctionCategory   *cat;
	char              **help;


	SCM_ASSERT (SCM_NIMP (scm_name) && SCM_STRINGP (scm_name), scm_name, SCM_ARG1, "scm_register_function");
	SCM_ASSERT (SCM_NIMP (scm_args) && SCM_STRINGP (scm_args), scm_args, SCM_ARG2, "scm_register_function");
	SCM_ASSERT (SCM_NIMP (scm_help) && SCM_STRINGP (scm_help), scm_help, SCM_ARG3, "scm_register_function");
	SCM_ASSERT (SCM_NIMP (scm_category) && SCM_STRINGP (scm_category),
		    scm_category, SCM_ARG4, "scm_register_function");
	SCM_ASSERT (scm_procedure_p (scm_function), scm_function, SCM_ARG5, "scm_register_function");

	scm_permanent_object (scm_function);

	help  = g_new (char *, 1);
	*help = g_strdup (SCM_CHARS (scm_help));
	cat   = function_get_category (SCM_CHARS (scm_category));
	fndef = function_add_args (cat, g_strdup (SCM_CHARS (scm_name)),
				   g_strdup (SCM_CHARS (scm_args)), NULL,
				   help, func_marshal_func);

	function_def_set_user_data (fndef, GINT_TO_POINTER (scm_function));

	return SCM_UNSPECIFIED;
}

gboolean
plugin_can_deactivate_general (void)
{
	return FALSE;
}

void
plugin_cleanup_general (ErrorInfo **ret_error)
{
	*ret_error = NULL;
}

void
plugin_init_general (ErrorInfo **ret_error)
{
	FunctionCategory *cat;
	char *name, *dir;

	*ret_error = NULL;

	if (!has_gnumeric_been_compiled_with_guile_support ()) {
		*ret_error = error_info_new_str (
		             _("Gnumeric has not been compiled with support for guile."));
		return;
	}

	/* Initialize just in case. */
	eval_pos = NULL;

	cat = function_get_category ("Guile");

	function_add_nodes (cat, "scm_apply", 0, "symbol", NULL, func_scm_apply);

	init_value_type ();

	scm_c_define_gsubr ("gnumeric-funcall", 2, 0, 0, scm_gnumeric_funcall);
	scm_c_define_gsubr ("register-function", 5, 0, 0, scm_register_function);

	dir = gnumeric_sys_data_dir ("guile");
	name = g_strconcat (dir, "gnumeric_startup.scm", NULL);
	scm_apply (scm_eval_0str ("(lambda (filename)"
				  "  (if (access? filename R_OK)"
				  "    (load filename)"
				  "    (display (string-append \"could not read Guile plug-in init file\" filename \"\n\"))))"),
		  scm_cons (scm_makfrom0str (name), SCM_EOL),
		  SCM_EOL);
	g_free (name);
	g_free (dir);
}
