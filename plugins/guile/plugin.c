/* -*- mode: c; c-basic-offset: 8 -*- */
/*
 *
 *     Authors: Mark Probst
 *              Ariel Rios <ariel@arcavia.com>
 *	   Copyright Mark Probst, Ariel Rios 2000, 2001
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

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <glib.h>
#include <assert.h>
#include <stdio.h>
#include <libguile.h>
/* Deprecated, but we want gh_scm2newstr */
#include <guile/gh.h>
#include <gnome.h>

#include "plugin.h"
#include "plugin-util.h"
#include "error-info.h"
#include "module-plugin-defs.h"
#include "expr.h"
#include "expr-impl.h"
#include "gutils.h"
#include "func.h"
#include "cell.h"
#include "value.h"
#include "libgnumeric.h"
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

static SCM
scm_gnumeric_funcall (SCM funcname, SCM arglist)
{
	int i, num_args;
	Value **argvals;
	Value *retval;
	SCM retsmob;
	CellRef cell_ref = { 0, 0, 0, 0 };

	SCM_ASSERT (SCM_NIMP (funcname) && SCM_STRINGP (funcname), funcname, SCM_ARG1, "gnumeric-funcall");
	SCM_ASSERT (SCM_NFALSEP (scm_list_p (arglist)), arglist, SCM_ARG2, "gnumeric-funcall");

	num_args = scm_ilength (arglist);
	argvals = g_new (Value *, num_args);
	for (i = 0; i < num_args; ++i) {
		argvals[i] = scm_to_value (SCM_CAR (arglist));
		arglist = SCM_CDR (arglist);
	}

	retval = function_call_with_values (eval_pos, SCM_CHARS (funcname),
					    num_args,argvals);
	retsmob = value_to_scm (retval, cell_ref);
	value_release (retval);
	return retsmob;
}

typedef struct {
	SCM function;
	SCM args;
} GnmGuileCallRec;

/* This gets called from scm_internal_stack_catch when calling scm_apply. */
static SCM
gnm_guile_helper (void *data)
{
	GnmGuileCallRec *ggcr = (GnmGuileCallRec *) data;
	return scm_apply_0 (ggcr->function, ggcr->args);
}

/*
 * This gets called if scm_apply throws an error.
 *
 * We use gh_scm2newstr to convert from Guile string to Scheme string. The
 * GH interface is deprecated, but doing it in scm takes more code. We'll
 * convert later if we have to.
 */
static SCM
gnm_guile_catcher (void *data, SCM tag, SCM throw_args)
{
	const char *header = _("Guile error");
	SCM smob;
	SCM func;
	SCM res;
	char *guilestr = NULL;
	char *msg;
	char buf[256];
	Value *v;

	func = scm_c_eval_string ("gnm:error->string");
	if (scm_procedure_p (func)) {
		res = scm_apply (func, tag,
				 scm_cons (throw_args, scm_listofnull));
		if (scm_string_p (res))
			guilestr = gh_scm2newstr (res, NULL);
	}
	
	if (guilestr != NULL) {
		snprintf (buf, sizeof buf, "%s: %s", header, guilestr);
		free (guilestr);
		msg = buf;
	} else {
		msg = (char *) header;
	}

	v = value_new_error (NULL, msg);
	smob = make_new_smob (v);
	value_release (v);
	return smob;
}

static Value*
func_marshal_func (FunctionEvalInfo *ei, Value *argv[])
{
	GnmFunc const *fndef;
	SCM args = SCM_EOL, result, function;
	CellRef dummy = { 0, 0, 0, 0 };
	EvalPos const *old_eval_pos;
	GnmGuileCallRec ggcr;
	int i, min, max;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_call != NULL, NULL);
	g_return_val_if_fail (ei->func_call->func != NULL, NULL);

	fndef = ei->func_call->func;
	function_def_count_args (fndef, &min, &max);

	function = (SCM) gnm_func_get_user_data (fndef);

	for (i = min - 1; i >= 0; --i)
		args = scm_cons (value_to_scm (argv [i], dummy), args);

	old_eval_pos  = eval_pos;
	eval_pos      = ei->pos;
	ggcr.function = function;
	ggcr.args     = args;
	result        = scm_internal_stack_catch (SCM_BOOL_T,
						  gnm_guile_helper, &ggcr,
						  gnm_guile_catcher, NULL);
	eval_pos     = old_eval_pos;

	return scm_to_value (result);
}

/*
 * FIXME: If we clean up at exit, removing the registered functions, we get
 * rid of the 'Leaking string [Guile] with ref_count=1' warnings. The way we
 * do this for other plugins, including Python, we deactivate the
 * plugin. However, it is not possible to finalize Guile.
 */
static SCM
scm_register_function (SCM scm_name, SCM scm_args, SCM scm_help, SCM scm_category, SCM scm_function)
{
	GnmFunc *fndef;
	FunctionCategory   *cat;
	GnmFuncDescriptor    desc;
	char     *help;

	SCM_ASSERT (SCM_NIMP (scm_name) && SCM_STRINGP (scm_name), scm_name, SCM_ARG1, "scm_register_function");
	SCM_ASSERT (SCM_NIMP (scm_args) && SCM_STRINGP (scm_args), scm_args, SCM_ARG2, "scm_register_function");
	SCM_ASSERT (SCM_NIMP (scm_help) && SCM_STRINGP (scm_help), scm_help, SCM_ARG3, "scm_register_function");
	SCM_ASSERT (SCM_NIMP (scm_category) && SCM_STRINGP (scm_category),
		    scm_category, SCM_ARG4, "scm_register_function");
	SCM_ASSERT (scm_procedure_p (scm_function), scm_function, SCM_ARG5, "scm_register_function");

	scm_permanent_object (scm_function);

	desc.name	= g_strdup (SCM_CHARS (scm_name));
	desc.arg_spec	= g_strdup (SCM_CHARS (scm_args));
	desc.arg_names	= NULL;
	help            = g_strdup (SCM_CHARS (scm_help));
	desc.help       = &help;
	desc.fn_args    = func_marshal_func;
	desc.fn_nodes   = NULL;
	desc.linker     = NULL;
	desc.unlinker   = NULL;
	desc.flags      = 0;
	desc.ref_notify = NULL;
	desc.impl_status = GNM_FUNC_IMPL_STATUS_NOT_IN_EXCEL;
	desc.test_status = GNM_FUNC_TEST_STATUS_UNKNOWN;

	cat   = function_get_category (SCM_CHARS (scm_category));
	fndef = gnm_func_add (cat, &desc);

	gnm_func_set_user_data (fndef, GINT_TO_POINTER (scm_function));

	return SCM_UNSPECIFIED;
}

void
plugin_cleanup_general (ErrorInfo **ret_error)
{
	*ret_error = NULL;
}

void
plugin_init_general (ErrorInfo **ret_error)
{
	char *name, *dir;

	*ret_error = NULL;

	 scm_init_guile ();

	/* Initialize just in case. */
	eval_pos = NULL;

	init_value_type ();

	scm_c_define_gsubr ("gnumeric-funcall", 2, 0, 0, scm_gnumeric_funcall);
	scm_c_define_gsubr ("register-function", 5, 0, 0, scm_register_function);

	dir = gnumeric_sys_data_dir ("guile");
	name = g_strconcat (dir, "gnumeric_startup.scm", NULL);
	scm_apply (scm_c_eval_string ("(lambda (filename)"
				  "  (if (access? filename R_OK)"
				  "    (load filename)"
				  "    (display (string-append \"could not read Guile plug-in init file\" filename \"\n\"))))"),
		  scm_cons (scm_makfrom0str (name), SCM_EOL),
		  SCM_EOL);
	g_free (name);
	g_free (dir);
	/* Don't try to deactivate the plugin */
	gnm_plugin_use_ref (PLUGIN);
}
