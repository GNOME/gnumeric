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
#include "math.h"
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
	   
	   "@SEEALSO=")
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
		retval = value_int (1);
	else {
		retval = value_int (0);
		value_release (v);
	}
	
	return retval;
}

FunctionDefinition misc_functions [] = {
	{ "iserror", "",   "",                 &help_iserror, gnumeric_iserror, NULL },
	{ NULL, NULL }
};
