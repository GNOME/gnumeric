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

static char *help_clean = {
	N_("@FUNCTION=CLEAN\n"
	   "@SYNTAX=CLEAN(string)\n"

	   "@DESCRIPTION="
	   "Cleans the string from any non-printable characters."
	   "\n"
	   
	   "@SEEALSO=")
};

static Value *
gnumeric_clean (FunctionDefinition *fn, Value *argv [], char **error_string)
{
	Value *res;
	char *copy, *p, *q;
	
	if (argv [0]->type != VALUE_STRING){
		*error_string = _("Type mismatch");
		return NULL;
	}
	p = argv [0]->v.str->str;
	copy = q = g_malloc (strlen (p) + 1);
	
	while (*p){
		if (isprint (*p))
			*q++ = *p;
		p++;
	}
	*q = 0;

	res = g_new (Value, 1);
	res->type = VALUE_STRING;
	res->v.str = string_get (copy);
	g_free (copy);

	return res;
}

static char *help_iserror = {
	N_("@FUNCTION=ISERROR\n"
	   "@SYNTAX=ISERROR(exp)\n"

	   "@DESCRIPTION="
	   "Returns a TRUE value if the expression has an error\n"
	   "\n"
	   
	   "@SEEALSO=")
};

static Value *
gnumeric_iserror (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *v, *retval;
	
	if (g_list_length (expr_node_list) != 1){
		*error_string = _("Argument mismatch");
		return NULL;
	}
	v = eval_expr (tsheet, (ExprTree *) expr_node_list->data, eval_col, eval_row, error_string);
	if (v == NULL)
		retval = value_int (1);
	else {
		retval = value_int (0);
		value_release (v);
	}
	
	return retval;
}

FunctionDefinition misc_functions [] = {
	{ "clean",   "s",  "text",             &help_clean, NULL, gnumeric_clean },
	{ "iserror", "",   "",                 &help_iserror, gnumeric_iserror, NULL },
	{ NULL, NULL }
};
