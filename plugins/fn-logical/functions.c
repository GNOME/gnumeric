/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-logical.c:  Built in logical functions and functions registration
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Morten Welinder (terra@diku.dk)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <auto-format.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/***************************************************************************/

static const char *help_and = {
	N_("@FUNCTION=AND\n"
	   "@SYNTAX=AND(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "AND implements the logical AND function: the result is TRUE "
	   "if all of the expressions evaluate to TRUE, otherwise it returns "
	   "FALSE.\n"
	   "\n"
	   "@b1, trough @bN are expressions that should evaluate to TRUE "
	   "or FALSE.  If an integer or floating point value is provided "
	   "zero is considered FALSE and anything else is TRUE.\n"
	   "\n"
	   "* If the values contain strings or empty cells those values are "
	   "ignored.\n"
	   "* If no logical values are provided, then the error #VALUE! "
	   "is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "AND(TRUE,TRUE) equals TRUE.\n"
	   "AND(TRUE,FALSE) equals FALSE.\n\n"
	   "Let us assume that A1 holds number five and A2 number one.  Then\n"
	   "AND(A1>3,A2<2) equals TRUE.\n"
	   "\n"
	   "@SEEALSO=OR, NOT")
};

static Value *
callback_function_and (const EvalPos *ep, Value *value, void *closure)
{
	int *result = closure;
	gboolean err;

	*result = value_get_as_bool (value, &err) && *result;
	if (err)
		return value_new_error (ep, gnumeric_err_VALUE);

	return NULL;
}

static Value *
gnumeric_and (FunctionEvalInfo *ei, GnmExprList *nodes)
{
	int result = -1;

	/* Yes, AND is actually strict.  */
	Value *v = function_iterate_argument_values (ei->pos,
		callback_function_and, &result, nodes,
		TRUE, CELL_ITER_IGNORE_BLANK);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_bool (result);
}

/***************************************************************************/

static const char *help_not = {
	N_("@FUNCTION=NOT\n"
	   "@SYNTAX=NOT(number)\n"

	   "@DESCRIPTION="
	   "NOT implements the logical NOT function: the result is TRUE "
	   "if the @number is zero;  otherwise the result is FALSE.\n"
	   "\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "NOT(0) equals TRUE.\n"
	   "NOT(TRUE) equals FALSE.\n"
	   "\n"
	   "@SEEALSO=AND, OR")
};

static Value *
gnumeric_not (FunctionEvalInfo *ei, Value **argv)
{
	gboolean err, val = value_get_as_bool (argv [0], &err);
	if (err)
		return value_new_error (ei->pos, _("Type Mismatch"));
	return value_new_bool (!val);
}

/***************************************************************************/

static const char *help_or = {
	N_("@FUNCTION=OR\n"
	   "@SYNTAX=OR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "OR implements the logical OR function: the result is TRUE if "
	   "any of the values evaluated to TRUE.\n"
	   "\n"
	   "@b1, trough @bN are expressions that should evaluate to TRUE "
	   "or FALSE. If an integer or floating point value is provided "
	   "zero is considered FALSE and anything else is TRUE.\n"
	   "\n"
	   "* If the values contain strings or empty cells those values are "
	   "ignored.\n"
	   "* If no logical values are provided, then the error "
	   "#VALUE! is returned.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "OR(TRUE,FALSE) equals TRUE.\n"
	   "OR(3>4,4<3) equals FALSE.\n"
	   "\n"
	   "@SEEALSO=AND, NOT")
};

static Value *
callback_function_or (const EvalPos *ep, Value *value, void *closure)
{
	int *result = closure;
	gboolean err;

	*result = value_get_as_bool (value, &err) || *result == 1;
	if (err)
		return value_new_error (ep, gnumeric_err_VALUE);

	return NULL;
}

static Value *
gnumeric_or (FunctionEvalInfo *ei, GnmExprList *nodes)
{
	int result = -1;

	/* Yes, OR is actually strict.  */
	Value *v = function_iterate_argument_values (ei->pos,
		callback_function_or, &result, nodes,
		TRUE, CELL_ITER_IGNORE_BLANK);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_bool (result);
}

/***************************************************************************/

static const char *help_xor = {
	N_("@FUNCTION=XOR\n"
	   "@SYNTAX=XOR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "XOR implements the logical exclusive OR function: the result is "
	   "TRUE if an odd number of the values evaluated to TRUE.\n"
	   "\n"
	   "@b1, trough @bN are expressions that should evaluate to TRUE "
	   "or FALSE. If an integer or floating point value is provided "
	   "zero is considered FALSE and anything else is TRUE.\n"
	   "\n"
	   "* If the values contain strings or empty cells those values are "
	   "ignored.\n"
	   "* If no logical values are provided, then the error "
	   "#VALUE! is returned.\n"
	   "@EXAMPLES=\n"
	   "XOR(TRUE,FALSE) equals TRUE.\n"
	   "XOR(3>4,4<3) equals FALSE.\n"
	   "\n"
	   "@SEEALSO=OR, AND, NOT")
};

static Value *
callback_function_xor (const EvalPos *ep, Value *value, void *closure)
{
	int *result = closure;
	gboolean err;

	*result = value_get_as_bool (value, &err) ^ (*result == 1);
	if (err)
		return value_new_error (ep, gnumeric_err_VALUE);

	return NULL;
}

static Value *
gnumeric_xor (FunctionEvalInfo *ei, GnmExprList *nodes)
{
	int result = -1;

	/* Yes, XOR is actually strict.  */
	Value *v = function_iterate_argument_values (ei->pos,
		callback_function_xor, &result, nodes,
		TRUE, CELL_ITER_IGNORE_BLANK);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	return value_new_bool (result);
}

/***************************************************************************/

static const char *help_if = {
	N_("@FUNCTION=IF\n"
	   "@SYNTAX=IF(condition[,if-true,if-false])\n"

	   "@DESCRIPTION="
	   "IF function can be used to evaluate conditionally other "
	   "expressions. IF evaluates @condition.  If @condition returns a "
	   "non-zero value the result of the IF expression is the @if-true "
	   "expression, otherwise IF evaluates to the value of @if-false.\n"
	   "\n"
	   "* If ommitted @if-true defaults to TRUE and @if-false to FALSE.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IF(FALSE,TRUE,FALSE) equals FALSE.\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_if (FunctionEvalInfo *ei, GnmExprList *expr_node_list)
{
	GnmExpr *expr;
	Value *value;
	int ret, args;
	gboolean err;

	/* Type checking */
	args = gnm_expr_list_length (expr_node_list);
	if (args < 1 || args > 3)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	/* Compute the if part */
	value = gnm_expr_eval (expr_node_list->data, ei->pos, 0);
	if (VALUE_IS_EMPTY_OR_ERROR(value))
		return value;

	/* Choose which expression we will evaluate */
	ret = value_get_as_bool (value, &err);
	value_release (value);
	if (err)
		/* FIXME: please verify error code.  */
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (ret){
		if (expr_node_list->next)
			expr = (GnmExpr *) expr_node_list->next->data;
		else
			return value_new_bool (TRUE);
	} else {
		if (expr_node_list->next &&
		    expr_node_list->next->next)
			expr = (GnmExpr *) expr_node_list->next->next->data;
		else
			return value_new_bool (FALSE);
	}

	/* Return the result */
	return gnm_expr_eval (expr, ei->pos, GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
}

/***************************************************************************/

static const char *help_true = {
	N_("@FUNCTION=TRUE\n"
	   "@SYNTAX=TRUE()\n"

	   "@DESCRIPTION="
	   "TRUE returns boolean value true.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "TRUE() equals TRUE.\n"
	   "\n"
	   "@SEEALSO=FALSE")
};

static Value *
gnumeric_true (FunctionEvalInfo *ei, Value **args)
{
	return value_new_bool (TRUE);
}

/***************************************************************************/

static const char *help_false = {
	N_("@FUNCTION=FALSE\n"
	   "@SYNTAX=FALSE()\n"

	   "@DESCRIPTION="
	   "FALSE returns boolean value false.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "FALSE() equals FALSE.\n"
	   "\n"
	   "@SEEALSO=TRUE")
};

static Value *
gnumeric_false (FunctionEvalInfo *ei, Value **args)
{
	return value_new_bool (FALSE);
}

/***************************************************************************/

const ModulePluginFunctionInfo logical_functions[] = {
	{ "and", 0, N_("number,number,"), &help_and, NULL,
	  gnumeric_and, NULL, NULL },
	{ "or", 0, N_("number,number,"), &help_or, NULL,
	  gnumeric_or, NULL, NULL },
	{ "xor", 0, N_("number,number,"), &help_xor, NULL,
	  gnumeric_xor, NULL, NULL },
	{ "not", "f", N_("number"), &help_not, gnumeric_not, NULL, NULL, NULL },
	{ "if", 0, N_("condition,if true,if false"), &help_if, NULL,
	  gnumeric_if, NULL, NULL },
	{ "true", "", "", &help_true, gnumeric_true, NULL, NULL, NULL },
	{ "false", "", "", &help_false, gnumeric_false, NULL, NULL, NULL },
        {NULL}
};

/* FIXME: Should be merged into the above.  */
static const struct {
	const char *func;
	AutoFormatTypes typ;
} af_info[] = {
	{ "if", AF_FIRST_ARG_FORMAT2 },
	{ NULL, AF_UNKNOWN }
};

void
plugin_init (void)
{
	int i;
	for (i = 0; af_info[i].func; i++)
		auto_format_function_result_by_name (af_info[i].func, af_info[i].typ);
}

void
plugin_cleanup (void)
{
	int i;
	for (i = 0; af_info[i].func; i++)
		auto_format_function_result_remove (af_info[i].func);
}
