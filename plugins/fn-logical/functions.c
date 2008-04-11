/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-logical.c:  Built in logical functions and functions registration
 *
 * Authors:
 *   Miguel de Icaza (miguel@gnu.org)
 *   Jukka-Pekka Iivonen (iivonen@iki.fi)
 *   Morten Welinder (terra@gnome.org)
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
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <cell.h>
#include <expr.h>
#include <expr-impl.h>
#include <value.h>
#include <gnm-i18n.h>

#include <goffice/app/go-plugin.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/

static GnmFuncHelp const help_and[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=AND\n"
	   "@SYNTAX=AND(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "AND implements the logical AND function: the result is TRUE "
	   "if all of the expressions evaluate to TRUE, otherwise it returns "
	   "FALSE.\n"
	   "\n"
	   "@b1 through @bN are expressions that should evaluate to TRUE "
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
callback_function_and (GnmEvalPos const *ep, GnmValue const *value, void *closure)
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
gnumeric_and (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	int result = -1;

	/* Yes, AND is actually strict.  */
	GnmValue *v = function_iterate_argument_values
		(ei->pos, callback_function_and, &result,
		 argc, argv, TRUE, CELL_ITER_IGNORE_BLANK);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error_VALUE (ei->pos);

	return value_new_bool (result);
}

/***************************************************************************/

static GnmFuncHelp const help_not[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=NOT\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_not (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gboolean err, val = value_get_as_bool (argv [0], &err);
	if (err)
		return value_new_error (ei->pos, _("Type Mismatch"));
	return value_new_bool (!val);
}

/***************************************************************************/

static GnmFuncHelp const help_or[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=OR\n"
	   "@SYNTAX=OR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "OR implements the logical OR function: the result is TRUE if "
	   "any of the values evaluated to TRUE.\n"
	   "\n"
	   "@b1 through @bN are expressions that should evaluate to TRUE "
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
callback_function_or (GnmEvalPos const *ep, GnmValue const *value, void *closure)
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
gnumeric_or (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	int result = -1;

	/* Yes, OR is actually strict.  */
	GnmValue *v = function_iterate_argument_values
		(ei->pos, callback_function_or, &result,
		 argc, argv, TRUE, CELL_ITER_IGNORE_BLANK);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error_VALUE (ei->pos);

	return value_new_bool (result);
}

/***************************************************************************/

static GnmFuncHelp const help_xor[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=XOR\n"
	   "@SYNTAX=XOR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "XOR implements the logical exclusive OR function: the result is "
	   "TRUE if an odd number of the values evaluated to TRUE.\n"
	   "\n"
	   "@b1 through @bN are expressions that should evaluate to TRUE "
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
callback_function_xor (GnmEvalPos const *ep, GnmValue const *value, void *closure)
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
gnumeric_xor (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	int result = -1;

	/* Yes, XOR is actually strict.  */
	GnmValue *v = function_iterate_argument_values
		(ei->pos, callback_function_xor, &result,
		 argc, argv, TRUE, CELL_ITER_IGNORE_BLANK);
	if (v != NULL)
		return v;

	/* See if there was any value worth using */
	if (result == -1)
		return value_new_error_VALUE (ei->pos);

	return value_new_bool (result);
}

/***************************************************************************/

static GnmFuncHelp const help_if[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=IF\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_if (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	gboolean err;
	int res = value_get_as_bool (args[0], &err) ? 1 : 2;

	if (args[res])
		return value_dup (args[res]);

	if (ei->func_call->argc < res + 1)
		/* arg-not-there: default to TRUE/FALSE.  */
		return value_new_bool (res == 1);
	else
		/* arg blank: default to 0.  */
		return value_new_int (0);
}

/***************************************************************************/

static GnmFuncHelp const help_iferror[] = {
	{ GNM_FUNC_HELP_NAME, F_("IFERROR:Test for error.") },
	{ GNM_FUNC_HELP_ARG, F_("x:value to test for error.") },
	{ GNM_FUNC_HELP_ARG, F_("y:alternate value.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function returns the first value, unless that is an error, in which case it returns the second.") },
	{ GNM_FUNC_HELP_SEEALSO, "IF,ISERROR" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_iferror (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_dup (VALUE_IS_ERROR (argv[0]) ? argv[1] : argv[0]);
}

/***************************************************************************/

static GnmFuncHelp const help_true[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=TRUE\n"
	   "@SYNTAX=TRUE()\n"

	   "@DESCRIPTION="
	   "TRUE returns boolean value true.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "TRUE() equals TRUE.\n"
	   "\n"
	   "@SEEALSO=FALSE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_true (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	return value_new_bool (TRUE);
}

/***************************************************************************/

static GnmFuncHelp const help_false[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=FALSE\n"
	   "@SYNTAX=FALSE()\n"

	   "@DESCRIPTION="
	   "FALSE returns boolean value false.\n\n"
	   "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "FALSE() equals FALSE.\n"
	   "\n"
	   "@SEEALSO=TRUE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_false (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	return value_new_bool (FALSE);
}

/***************************************************************************/

GnmFuncDescriptor const logical_functions[] = {
	{ "and", NULL, N_("number,number,"), help_and, NULL,
	  gnumeric_and, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "or", NULL, N_("number,number,"), help_or, NULL,
	  gnumeric_or, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "not", "b", N_("number"), help_not, gnumeric_not,
	  NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "if", "b|EE", N_("condition,if true,if false"), help_if,
	  gnumeric_if, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_SECOND,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "iferror", "EE", N_("value,value"), help_iferror,
	  gnumeric_iferror, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "true", "", "", help_true, gnumeric_true,
	  NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "false", "", "", help_false, gnumeric_false,
	  NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "xor", NULL, N_("number,number,"), help_xor, NULL,
	  gnumeric_xor, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
        {NULL}
};
