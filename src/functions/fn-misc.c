/*
 * fn-misc.c:  Miscelaneous built-in functions
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include "gnumeric.h"
#include "utils.h"
#include "func.h"

/* A utility routine to evaluate a single argument and return any errors
 * directly 
 */
static int
gnumeric_check_for_err (FunctionEvalInfo *ei, GList *expr_node_list)
{
	Value * tmp;
	if (g_list_length (expr_node_list) != 1){
		error_message_set (ei->error, _("Argument mismatch"));
		return -1;
	}
	tmp = eval_expr (ei, (ExprTree *) expr_node_list->data);

	if (tmp) {
		value_release (tmp);
		return 0;
	}
	return 1;
}


static char *help_iserror = {
	N_("@FUNCTION=ISERROR\n"
	   "@SYNTAX=ISERROR(exp)\n"

	   "@DESCRIPTION="
	   "Returns a TRUE value if the expression has an error\n"
	   "\n"

	   "@SEEALSO=ERROR")
};

static Value *
gnumeric_iserror (FunctionEvalInfo *ei, GList *expr_node_list)
{
	int res;
	res = gnumeric_check_for_err (ei, expr_node_list);

	if (res < 0)
		return NULL;
	if (res > 0) {
		error_message_set_small (ei->error, "");
		return value_new_bool (TRUE);
	}
	return value_new_bool (FALSE);
}


static char *help_isna = {
	N_("@FUNCTION=ISNA\n"
	   "@SYNTAX=ISNA()\n"

	   "@DESCRIPTION="
	   "ISNA Returns TRUE if the value is the #N/A error value. "
	   "\n"
	   "@SEEALSO=")
};

/*
 * We need to operator directly in the input expression in order to bypass
 * the error handling mechanism
 */
static Value *
gnumeric_isna (FunctionEvalInfo *ei, GList *expr_node_list)
{
	int res;
	res = gnumeric_check_for_err (ei, expr_node_list);
	if (res < 0)
		return NULL;
	if (res > 0) {
		gboolean is_NA = (strcmp (gnumeric_err_NA,
					  error_message_txt(ei->error)) == 0);
		error_message_set_small (ei->error, "");
		return value_new_bool (is_NA);
	}
	return value_new_bool (FALSE);
}


static char *help_iserr = {
	N_("@FUNCTION=ISERR\n"
	   "@SYNTAX=ISERR()\n"

	   "@DESCRIPTION="
	   "ISERR Returns TRUE if the value is any error value except #N/A. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_iserr (FunctionEvalInfo *ei, GList *expr_node_list)
{
	int res;
	res = gnumeric_check_for_err (ei, expr_node_list);

	if (res < 0)
		return NULL;
	if (res > 0) {
		gboolean is_NA = (strcmp (gnumeric_err_NA,
					  error_message_txt (ei->error)) == 0);
		error_message_set_small (ei->error, "");
		return value_new_bool (!is_NA);
	}
	return value_new_bool (FALSE);
}


static char *help_error_type = {
	N_("@FUNCTION=ERROR.TYPE\n"
	   "@SYNTAX=ERROR(exp)\n"

	   "@DESCRIPTION="
	   "FIXME"
	   "\n"

	   "@SEEALSO=ISERROR")
};

static Value *
gnumeric_error_type (FunctionEvalInfo *ei, GList *expr_node_list)
{
	int retval = 0;
	
	if (gnumeric_check_for_err (ei, expr_node_list)) {
		if (!strcmp (gnumeric_err_NULL, error_message_txt (ei->error)))
			retval = 1;
		else if (!strcmp (gnumeric_err_DIV0, error_message_txt (ei->error)))
			retval = 2;
		else if (!strcmp (gnumeric_err_VALUE, error_message_txt (ei->error)))
			retval = 3;
		else if (!strcmp (gnumeric_err_REF, error_message_txt (ei->error)))
			retval = 4;
		else if (!strcmp (gnumeric_err_NAME, error_message_txt (ei->error)))
			retval = 5;
		else if (!strcmp (gnumeric_err_NUM, error_message_txt (ei->error)))
			retval = 6;
		else if (!strcmp (gnumeric_err_NA, error_message_txt (ei->error)))
			retval = 7;
		else
			return function_error (ei, gnumeric_err_NA);
		error_message_set_small (ei->error, "");
	}
	return value_new_int (retval);
}


static char *help_na = {
	N_("@FUNCTION=NA\n"
	   "@SYNTAX=NA()\n"

	   "@DESCRIPTION="
	   "NA Returns the error value #N/A. "
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_na (FunctionEvalInfo *ei, Value **argv)
{
	return function_error (ei, gnumeric_err_NA);
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

	function_add_nodes (cat, "iserror", "",   "",
			    &help_iserror, gnumeric_iserror);
	function_add_nodes (cat, "isna", "",   "",
			    &help_isna,    gnumeric_isna);
	function_add_nodes (cat, "iserr", "",   "",
			    &help_iserr,   gnumeric_iserr);
	function_add_nodes (cat, "error.type", "", "",
			    &help_error_type, gnumeric_error_type);
	function_add_args  (cat, "na",      "",  "",
			    &help_na,      gnumeric_na);
	function_add_args  (cat, "error",   "s",  "text",
			    &help_error,   gnumeric_error);
}
