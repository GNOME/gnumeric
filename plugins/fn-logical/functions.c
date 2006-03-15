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
#include <glib/gi18n.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <cell.h>
#include <expr.h>
#include <value.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/***************************************************************************/

static char const *help_and = {
	N_("@FUNCTION=AND\n"
	   "@SYNTAX=AND(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "AND implements the logical AND function: the result is TRUE "
	   "if all of the expressions evaluate to TRUE, otherwise it returns "
	   "FALSE.\n"
	   "\n"
	   "@b1 trough @bN are expressions that should evaluate to TRUE "
	   "or FALSE.  If an integer or floating point value is provided, "
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

static GnmValue *
callback_function_and (const GnmEvalPos *ep, GnmValue *value, void *closure)
{
	int *result = closure;

	if (!VALUE_IS_STRING (value)) {
		gboolean err;
	*result = value_get_as_bool (value, &err) && *result;
	if (err)
		return value_new_error_VALUE (ep);
	}

	return NULL;
}

static GnmValue *
gnumeric_and (FunctionEvalInfo *ei, GnmExprList *nodes)
{
	int result = -1;

	/* Yes, AND is actually strict.  */
	GnmValue *v = function_iterate_argument_values (ei->pos,
		callback_function_and, &result, nodes,
		TRUE, CELL_ITER_IGNORE_BLANK);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error_VALUE (ei->pos);

	return value_new_bool (result);
}

/***************************************************************************/

static char const *help_not = {
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

static GnmValue *
gnumeric_not (FunctionEvalInfo *ei, GnmValue **argv)
{
	gboolean err, val = value_get_as_bool (argv [0], &err);
	if (err)
		return value_new_error (ei->pos, _("Type Mismatch"));
	return value_new_bool (!val);
}

/***************************************************************************/

static char const *help_or = {
	N_("@FUNCTION=OR\n"
	   "@SYNTAX=OR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "OR implements the logical OR function: the result is TRUE if "
	   "any of the values evaluated to TRUE.\n"
	   "\n"
	   "@b1 trough @bN are expressions that should evaluate to TRUE "
	   "or FALSE. If an integer or floating point value is provided, "
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

static GnmValue *
callback_function_or (const GnmEvalPos *ep, GnmValue *value, void *closure)
{
	int *result = closure;

	if (!VALUE_IS_STRING (value)) {
		gboolean err;
	*result = value_get_as_bool (value, &err) || *result == 1;
	if (err)
		return value_new_error_VALUE (ep);
	}

	return NULL;
}

static GnmValue *
gnumeric_or (FunctionEvalInfo *ei, GnmExprList *nodes)
{
	int result = -1;

	/* Yes, OR is actually strict.  */
	GnmValue *v = function_iterate_argument_values (ei->pos,
		callback_function_or, &result, nodes,
		TRUE, CELL_ITER_IGNORE_BLANK);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error_VALUE (ei->pos);

	return value_new_bool (result);
}

/***************************************************************************/

static char const *help_xor = {
	N_("@FUNCTION=XOR\n"
	   "@SYNTAX=XOR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "XOR implements the logical exclusive OR function: the result is "
	   "TRUE if an odd number of the values evaluated to TRUE.\n"
	   "\n"
	   "@b1 trough @bN are expressions that should evaluate to TRUE "
	   "or FALSE. If an integer or floating point value is provided, "
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

static GnmValue *
callback_function_xor (const GnmEvalPos *ep, GnmValue *value, void *closure)
{
	int *result = closure;

	if (!VALUE_IS_STRING (value)) {
		gboolean err;
	*result = value_get_as_bool (value, &err) ^ (*result == 1);
	if (err)
		return value_new_error_VALUE (ep);
	}

	return NULL;
}

static GnmValue *
gnumeric_xor (FunctionEvalInfo *ei, GnmExprList *nodes)
{
	int result = -1;

	/* Yes, XOR is actually strict.  */
	GnmValue *v = function_iterate_argument_values (ei->pos,
		callback_function_xor, &result, nodes,
		TRUE, CELL_ITER_IGNORE_BLANK);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error_VALUE (ei->pos);

	return value_new_bool (result);
}

/***************************************************************************/

static char const *help_if = {
	N_("@FUNCTION=IF\n"
	   "@SYNTAX=IF(condition[,if-true,if-false])\n"

	   "@DESCRIPTION="
	   "IF function can be used to evaluate conditionally other "
	   "expressions. IF evaluates @condition.  If @condition returns a "
	   "non-zero value the result of the IF expression is the @if-true "
	   "expression, otherwise IF evaluates to the value of @if-false.\n"
	   "\n"
	   "* If omitted @if-true defaults to TRUE and @if-false to FALSE.\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "IF(FALSE,TRUE,FALSE) equals FALSE.\n"
	   "\n"
	   "@SEEALSO=")
};

static GnmValue *
gnumeric_if (FunctionEvalInfo *ei, GnmValue **args)
{
	gboolean err;
	int argcount;
	int res = value_get_as_bool (args[0], &err) ? 1 : 2;

	if (args[res])
		return value_dup (args[res]);

	argcount = gnm_expr_get_func_argcount ((const GnmExpr *)ei->func_call);
	if (argcount < res + 1)
		/* arg-not-there: default to TRUE/FALSE.  */
		return value_new_bool (res == 1);
	else
		/* arg blank: default to 0.  */
		return value_new_int (0);
}

/***************************************************************************/

static char const *help_true = {
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

static GnmValue *
gnumeric_true (FunctionEvalInfo *ei, GnmValue **args)
{
	return value_new_bool (TRUE);
}

/***************************************************************************/

static char const *help_false = {
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

static GnmValue *
gnumeric_false (FunctionEvalInfo *ei, GnmValue **args)
{
	return value_new_bool (FALSE);
}

/***************************************************************************/

const GnmFuncDescriptor logical_functions[] = {
	{ "and", NULL, N_("number,number,"), &help_and, NULL,
	  gnumeric_and, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "or", NULL, N_("number,number,"), &help_or, NULL,
	  gnumeric_or, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "not", "b", N_("number"), &help_not, gnumeric_not,
	  NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "if", "b|EE", N_("condition,if true,if false"), &help_if,
	  gnumeric_if, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_SECOND,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "true", "", "", &help_true, gnumeric_true,
	  NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "false", "", "", &help_false, gnumeric_false,
	  NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "xor", NULL, N_("number,number,"), &help_xor, NULL,
	  gnumeric_xor, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
        {NULL}
};
