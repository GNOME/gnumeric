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
static Value *
gnumeric_check_for_err (FunctionEvalInfo *eval_info, GList *expr_node_list,
			Value ** err)
{
	Value * tmp;

	if (g_list_length (expr_node_list) != 1) {
		*err = value_new_error(&eval_info->pos, _("Argument mismatch"));
		return NULL;
	}
	tmp = eval_expr (eval_info, (ExprTree *) expr_node_list->data);

	if (tmp != NULL) {
		if (tmp->type == VALUE_ERROR)
			return tmp;
		value_release (tmp);
	}
	return NULL;
}

/***************************************************************************/

static char *help_iserror = {
	N_("@FUNCTION=ISERROR\n"
	   "@SYNTAX=ISERROR(exp)\n"

	   "@DESCRIPTION="
	   "Returns a TRUE value if the expression has an error\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ERROR")
};

static Value *
gnumeric_iserror (FunctionEvalInfo *eval_info, GList *expr_node_list)
{
	Value * res, *err = NULL;
	res = gnumeric_check_for_err (eval_info, expr_node_list, &err);
	if (err != NULL)
		return err;

	if (res) {
		value_release (res);
		return value_new_bool (TRUE);
	} else
		return value_new_bool (FALSE);
}

/***************************************************************************/

static char *help_isna = {
	N_("@FUNCTION=ISNA\n"
	   "@SYNTAX=ISNA()\n"

	   "@DESCRIPTION="
	   "ISNA returns TRUE if the value is the #N/A error value. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

/*
 * We need to operator directly in the input expression in order to bypass
 * the error handling mechanism
 */
static Value *
gnumeric_isna (FunctionEvalInfo *eval_info, GList *expr_node_list)
{
	Value * res, *err = NULL;
	gboolean b;

	res = gnumeric_check_for_err (eval_info, expr_node_list, &err);
	if (err != NULL)
		return err;

	b = (res && !strcmp (gnumeric_err_NA, res->v.error.mesg->str));
	if (res) value_release (res);
	return value_new_bool (b);
}

/***************************************************************************/

static char *help_iserr = {
	N_("@FUNCTION=ISERR\n"
	   "@SYNTAX=ISERR()\n"

	   "@DESCRIPTION="
	   "ISERR returns TRUE if the value is any error value except #N/A. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_iserr (FunctionEvalInfo *eval_info, GList *expr_node_list)
{
	Value * res, *err = NULL;
	gboolean b;

	res = gnumeric_check_for_err (eval_info, expr_node_list, &err);
	if (err != NULL)
		return err;

	b = (res && strcmp (gnumeric_err_NA, res->v.error.mesg->str));
	if (res) value_release (res);
	return value_new_bool (b);
}

/***************************************************************************/

static char *help_error_type = {
	N_("@FUNCTION=ERROR.TYPE\n"
	   "@SYNTAX=ERROR(exp)\n"

	   "@DESCRIPTION="
	   "FIXME"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ISERROR")
};

static Value *
gnumeric_error_type (FunctionEvalInfo *eval_info, GList *expr_node_list)
{
	int retval = -1;
	char const * mesg;
	Value * res, *err = NULL;
	res = gnumeric_check_for_err (eval_info, expr_node_list, &err);
	if (err != NULL)
		return err;
	if (res == NULL)
		return value_new_error (&eval_info->pos, gnumeric_err_NA);
	
	mesg = res->v.error.mesg->str;
	if (!strcmp (gnumeric_err_NULL, mesg))
		retval = 1;
	else if (!strcmp (gnumeric_err_DIV0, mesg))
		retval = 2;
	else if (!strcmp (gnumeric_err_VALUE, mesg))
		retval = 3;
	else if (!strcmp (gnumeric_err_REF, mesg))
		retval = 4;
	else if (!strcmp (gnumeric_err_NAME, mesg))
		retval = 5;
	else if (!strcmp (gnumeric_err_NUM, mesg))
		retval = 6;
	else if (!strcmp (gnumeric_err_NA, mesg))
		retval = 7;
	else {
		value_release (res);
		return value_new_error (&eval_info->pos, gnumeric_err_NA);
	}

	value_release (res);
	return value_new_int (retval);
}

/***************************************************************************/

static char *help_na = {
	N_("@FUNCTION=NA\n"
	   "@SYNTAX=NA()\n"

	   "@DESCRIPTION="
	   "NA returns the error value #N/A. "
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_na (FunctionEvalInfo *eval_info, Value **argv)
{
	return value_new_error (&eval_info->pos, gnumeric_err_NA);
}

/***************************************************************************/

static char *help_error = {
	N_("@FUNCTION=ERROR\n"
	   "@SYNTAX=ERROR(text)\n"

	   "@DESCRIPTION="
	   "Return the specified error\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=ISERROR")
};

static Value *
gnumeric_error (FunctionEvalInfo *eval_info, Value *argv[])
{
	if (argv [0]->type != VALUE_STRING)
		return value_new_error (&eval_info->pos, _("Type mismatch"));

	return value_new_error (&eval_info->pos, argv [0]->v.str->str);
}

/***************************************************************************/

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
