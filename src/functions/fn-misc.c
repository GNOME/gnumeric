/*
 * fn-misc.c:  Miscelaneous built-in functions
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <ctype.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

static char *help_iserror = {
	N_("@FUNCTION=ISERROR\n"
	   "@SYNTAX=ISERROR(exp)\n"

	   "@DESCRIPTION="
	   "Returns a TRUE value if the expression has an error\n"
	   "\n"
	   
	   "@SEEALSO=ERROR")
};

static FuncReturn *
gnumeric_iserror (FunctionEvalInfo *ei, GList *nodes)
{
	Value *v, *retval;

	if (g_list_length (nodes) != 1)
		return function_error (ei, _("Argument mismatch"));

	v = (Value *)eval_expr (ei, (ExprTree *) nodes->data);
	if (v == NULL)
		retval = value_new_int (1);
	else {
		retval = value_new_int (0);
		value_release (v);
	}

	FUNC_RETURN_VAL (retval);
}

static char *help_error = {
	N_("@FUNCTION=ERROR\n"
	   "@SYNTAX=ERROR(text)\n"

	   "@DESCRIPTION="
	   "Return the specified error\n"
	   "\n"
	   
	   "@SEEALSO=ISERROR")
};

static FuncReturn *
gnumeric_error (FunctionEvalInfo *ei, Value *argv[])
{
	if (argv [0]->type != VALUE_STRING)
		return function_error (ei, _("Type mismatch"));

	/* The error signaling system is broken.  We really cannot allocate a
	   dynamic error string.  Let's hope the string stays around for long
	   enough...  */
	return function_error_alloc (ei, g_strdup (argv [0]->v.str->str));
}

void misc_functions_init()
{
	FunctionCategory *cat = function_get_category (_("Miscellaneous"));

	function_add_args  (cat, "error",   "s",  "text",
			    &help_error,   gnumeric_error);
	function_add_nodes (cat, "iserror", "",   "",
			    &help_iserror, gnumeric_iserror);
}
	
