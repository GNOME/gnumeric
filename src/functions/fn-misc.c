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
gnumeric_check_for_err (Sheet *sheet, GList *expr_node_list,
			int eval_col, int eval_row, char **error_string)
{
	Value * tmp;
	if (g_list_length (expr_node_list) != 1){
		*error_string = _("Argument mismatch");
		return -1;
	}
	tmp = eval_expr (sheet, (ExprTree *) expr_node_list->data,
			 eval_col, eval_row, error_string);

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
gnumeric_iserror (Sheet *sheet, GList *expr_node_list,
		  int eval_col, int eval_row, char **error_string)
{
	int res;
	res = gnumeric_check_for_err (sheet, expr_node_list,
				      eval_col, eval_row, error_string);

	if (res < 0)
		return NULL;
	if (res > 0) {
		*error_string = NULL;
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
gnumeric_isna (Sheet *sheet, GList *expr_node_list,
	       int eval_col, int eval_row, char **error_string)
{
	int res;
	res = gnumeric_check_for_err (sheet, expr_node_list,
				      eval_col, eval_row, error_string);
	if (res < 0)
		return NULL;
	if (res > 0) {
		gboolean is_NA = (strcmp (gnumeric_err_NA, *error_string) == 0);
		*error_string = NULL;
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
gnumeric_iserr (Sheet *sheet, GList *expr_node_list,
		int eval_col, int eval_row, char **error_string)
{
	int res;
	res = gnumeric_check_for_err (sheet, expr_node_list,
				      eval_col, eval_row, error_string);
	if (res < 0)
		return NULL;
	if (res > 0) {
		gboolean is_NA = (strcmp (gnumeric_err_NA, *error_string) == 0);
		*error_string = NULL;
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
gnumeric_error_type (Sheet *sheet, GList *expr_node_list,
		     int eval_col, int eval_row, char **error_string)
{
	int res, retval = 0;
	if (gnumeric_check_for_err (sheet, expr_node_list,
				    eval_col, eval_row, error_string))
	{
		if (!strcmp (gnumeric_err_NULL, *error_string))
			retval = 1;
		else if (!strcmp (gnumeric_err_DIV0, *error_string))
			retval = 2;
		else if (!strcmp (gnumeric_err_VALUE, *error_string))
			retval = 3;
		else if (!strcmp (gnumeric_err_REF, *error_string))
			retval = 4;
		else if (!strcmp (gnumeric_err_NAME, *error_string))
			retval = 5;
		else if (!strcmp (gnumeric_err_NUM, *error_string))
			retval = 6;
		else if (!strcmp (gnumeric_err_NA, *error_string))
			retval = 7;
		else {
			*error_string = gnumeric_err_NA;
			return NULL;
		}
		*error_string = NULL;
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
gnumeric_na (struct FunctionDefinition *n,
	     Value *argv [], char **error_string)
{
	*error_string = gnumeric_err_NA;
	return NULL;
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
	{ "iserror",   "",   "",		&help_iserror,
	  gnumeric_iserror,    NULL },	/* Handles args manually */
	{ "isna", "", "",			&help_isna,
	  gnumeric_isna, NULL },	/* Handles args manually */
	{ "iserr", "", "",			&help_iserr,
	  gnumeric_iserr, NULL },	/* Handles args manually */
	{ "error.type","",   "",		&help_error_type,
	  gnumeric_error_type, NULL },	/* Handles args manually */
	{ "na", "", "",				&help_na,
	  NULL, gnumeric_na},
	{ "error",     "s",  "text",		&help_error,
	  NULL, gnumeric_error },
	{ NULL, NULL }
};
