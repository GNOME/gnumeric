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

static Value *
gnumeric_iserror (Sheet *sheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *v, *retval;
	
	if (g_list_length (expr_node_list) != 1){
		*error_string = _("Argument mismatch");
		return NULL;
	}
	v = eval_expr (sheet, (ExprTree *) expr_node_list->data, eval_col, eval_row, error_string);
	if (v == NULL)
		retval = value_new_int (1);
	else {
		retval = value_new_int (0);
		value_release (v);
	}
	
	return retval;
}

static char *help_error = {
	N_("@FUNCTION=ERROR\n"
	   "@SYNTAX=ERROR(text)\n"

	   "@DESCRIPTION="
	   "Return the specified error\n"
	   "\n"
	   
	   "@SEEALSO=ISERROR")
};

static Value *
gnumeric_error (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	if (argv [0]->type != VALUE_STRING){
		*error_string = _("Type mismatch");
		return NULL;
	}

	/* The error signaling system is broken.  We really cannot allocate a
	   dynamic error string.  Let's hope the string stays around for long
	   enough...  */
	*error_string = argv [0]->v.str->str;
	return NULL;
}


FunctionDefinition misc_functions [] = {
	{ "error",   "s",  "text",             &help_error,   NULL,             gnumeric_error },
	{ "iserror", "",   "",                 &help_iserror, gnumeric_iserror, NULL           },
	{ NULL, NULL }
};
