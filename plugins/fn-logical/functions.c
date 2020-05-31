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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <gnm-i18n.h>

#include <goffice/goffice.h>
#include <gnm-plugin.h>

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/

static GnmFuncHelp const help_and[] = {
        { GNM_FUNC_HELP_NAME, F_("AND:logical conjunction")},
        { GNM_FUNC_HELP_ARG, F_("b0:logical value")},
        { GNM_FUNC_HELP_ARG, F_("b1:logical value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("AND calculates the logical conjunction of its arguments @{b0},@{b1},...") },
	{ GNM_FUNC_HELP_NOTE, F_("If an argument is numerical, zero is considered FALSE and anything else TRUE.")},
	{ GNM_FUNC_HELP_NOTE, F_("Strings and empty values are ignored.")},
	{ GNM_FUNC_HELP_NOTE, F_("If no logical values are provided, then the error #VALUE! is returned.")},
	{ GNM_FUNC_HELP_NOTE, F_("This function is strict: if any argument is an error, the result will be the first such error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=AND(TRUE,FALSE)" },
        { GNM_FUNC_HELP_EXAMPLES, "=AND(0,1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=AND(FALSE,NA())" },
        { GNM_FUNC_HELP_SEEALSO, "OR,NOT,IF"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Logical_conjunction") },
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
        { GNM_FUNC_HELP_NAME, F_("NOT:logical negation")},
        { GNM_FUNC_HELP_ARG, F_("b:logical value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("NOT calculates the logical negation of its argument.") },
	{ GNM_FUNC_HELP_NOTE, F_("If the argument is numerical, zero is considered FALSE and anything else TRUE.")},
	{ GNM_FUNC_HELP_NOTE, F_("Strings and empty values are ignored.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=NOT(FALSE)" },
        { GNM_FUNC_HELP_EXAMPLES, "=NOT(1)" },
        { GNM_FUNC_HELP_SEEALSO, "AND,OR,IF"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Negation") },
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
        { GNM_FUNC_HELP_NAME, F_("OR:logical disjunction")},
        { GNM_FUNC_HELP_ARG, F_("b0:logical value")},
        { GNM_FUNC_HELP_ARG, F_("b1:logical value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("OR calculates the logical disjunction of its arguments @{b0},@{b1},...") },
	{ GNM_FUNC_HELP_NOTE, F_("If an argument is numerical, zero is considered FALSE and anything else TRUE.")},
	{ GNM_FUNC_HELP_NOTE, F_("Strings and empty values are ignored.")},
	{ GNM_FUNC_HELP_NOTE, F_("If no logical values are provided, then the error #VALUE! is returned.")},
	{ GNM_FUNC_HELP_NOTE, F_("This function is strict: if any argument is an error, the result will be the first such error.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=OR(TRUE,FALSE)" },
        { GNM_FUNC_HELP_EXAMPLES, "=OR(0,1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=OR(TRUE,NA())" },
        { GNM_FUNC_HELP_SEEALSO, "AND,XOR,NOT,IF"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Logical_disjunction") },
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
        { GNM_FUNC_HELP_NAME, F_("XOR:logical exclusive disjunction")},
        { GNM_FUNC_HELP_ARG, F_("b0:logical value")},
        { GNM_FUNC_HELP_ARG, F_("b1:logical value")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("XOR calculates the logical exclusive disjunction of its arguments @{b0},@{b1},...") },
	{ GNM_FUNC_HELP_NOTE, F_("If an argument is numerical, zero is considered FALSE and anything else TRUE.")},
	{ GNM_FUNC_HELP_NOTE, F_("Strings and empty values are ignored.")},
	{ GNM_FUNC_HELP_NOTE, F_("If no logical values are provided, then the error #VALUE! is returned.")},
	{ GNM_FUNC_HELP_NOTE, F_("This function is strict: if any argument is an error, the result will be the first such error.")},
        { GNM_FUNC_HELP_EXAMPLES, "=XOR(TRUE,FALSE)" },
        { GNM_FUNC_HELP_EXAMPLES, "=XOR(0,1)" },
        { GNM_FUNC_HELP_EXAMPLES, "=XOR(TRUE,NA())" },
        { GNM_FUNC_HELP_SEEALSO, "OR,AND,NOT,IF"},
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Exclusive_disjunction") },
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

static GnmFuncHelp const help_iferror[] = {
	{ GNM_FUNC_HELP_NAME, F_("IFERROR:test for error") },
	{ GNM_FUNC_HELP_ARG, F_("x:value to test for error") },
	{ GNM_FUNC_HELP_ARG, F_("y:alternate value") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function returns the first value, unless that is an error, in which case it returns the second.") },
        { GNM_FUNC_HELP_EXAMPLES, "=IFERROR(1/0,14)" },
	{ GNM_FUNC_HELP_SEEALSO, "IF,ISERROR" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_iferror (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_dup (VALUE_IS_ERROR (argv[0]) ? argv[1] : argv[0]);
}

/***************************************************************************/

static GnmFuncHelp const help_ifna[] = {
	{ GNM_FUNC_HELP_NAME, F_("IFNA:test for #N/A error") },
	{ GNM_FUNC_HELP_ARG, F_("x:value to test for #N/A error") },
	{ GNM_FUNC_HELP_ARG, F_("y:alternate value") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function returns the first value, unless that is #N/A, in which case it returns the second.") },
        { GNM_FUNC_HELP_EXAMPLES, "=IFNA(12,14)" },
        { GNM_FUNC_HELP_EXAMPLES, "=IFNA(1/0,14)" },
        { GNM_FUNC_HELP_EXAMPLES, "=IFNA(NA(),14)" },
	{ GNM_FUNC_HELP_SEEALSO, "IF,ISERROR" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ifna (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_dup ((value_error_classify (argv[0]) == GNM_ERROR_NA) ? argv[1] : argv[0]);
}

/***************************************************************************/

static GnmFuncHelp const help_ifs[] = {
	{ GNM_FUNC_HELP_NAME, F_("IFS:multi-branch conditional") },
	{ GNM_FUNC_HELP_ARG, F_("cond1:condition") },
	{ GNM_FUNC_HELP_ARG, F_("value1:value if @{condition1} is true") },
	{ GNM_FUNC_HELP_ARG, F_("cond2:condition") },
	{ GNM_FUNC_HELP_ARG, F_("value2:value if @{condition2} is true") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function returns the value after the first true conditional.  If no conditional is true, #VALUE! is returned.") },
        { GNM_FUNC_HELP_EXAMPLES, "=IFS(false,1/0,true,42)" },
	{ GNM_FUNC_HELP_SEEALSO, "IF" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_ifs (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	int a;

	for (a = 0; a + 1 <= argc; a += 2) {
		GnmValue *v;
		gboolean err, c;

		v = gnm_expr_eval (argv[a], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		// Strict in conditional arguments
		if (VALUE_IS_ERROR (v))
			return v;

		// Docs says to err on any non-boolean, but until tests
		// verify that, we use regular boolean interpretation
		c = value_get_as_bool (v, &err);
		value_release (v);
		if (err)
			break;

		if (c)
			// Flags?
			return gnm_expr_eval (argv[a + 1], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	}

	// No match
	return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_switch[] = {
	{ GNM_FUNC_HELP_NAME, F_("SWITCH:multi-branch selector") },
	{ GNM_FUNC_HELP_ARG, F_("ref:value") },
	{ GNM_FUNC_HELP_ARG, F_("choice1:first choice value") },
	{ GNM_FUNC_HELP_ARG, F_("value1:first result value") },
	{ GNM_FUNC_HELP_ARG, F_("choice2:second choice value") },
	{ GNM_FUNC_HELP_ARG, F_("value2:second result value") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function compares the reference value, @{ref}, against the choice values, @{choice1} etc., and returns the corresponding result value when it finds a match.  The choices may be followed by a default value to use.  If there are no choices that match and no default value, #N/A is return.") },
        { GNM_FUNC_HELP_EXAMPLES, "=SWITCH(WEEKDAY(TODAY()),0,\"Sunday\",1,\"Saturday\",\"not weekend\")" },
	{ GNM_FUNC_HELP_SEEALSO, "IF,IFS" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_switch (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	int a;
	GnmValue *res = NULL;
	GnmValue *ref;

	if (argc < 1)
		return value_new_error_VALUE (ei->pos);

	ref = gnm_expr_eval (argv[0], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (VALUE_IS_ERROR (ref))
		return ref;

	for (a = 1; !res && a + 1 < argc; a += 2) {
		GnmValue *v;

		v = gnm_expr_eval (argv[a], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		// Strict in case arguments
		if (VALUE_IS_ERROR (v)) {
			res = v;
			break;
		}

		// Docs are unclear on what kind of equality
		if (value_equal (v, ref))
			res = gnm_expr_eval (argv[a + 1], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);

		value_release (v);
	}

	if (res == NULL) {
		// No match
		if (a < argc)
			res = gnm_expr_eval (argv[a], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
		else
			res = value_new_error_NA (ei->pos);
	}

	value_release (ref);

	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_true[] = {
	{ GNM_FUNC_HELP_NAME, F_("TRUE:the value TRUE") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("TRUE returns the value TRUE.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=TRUE()" },
	{ GNM_FUNC_HELP_SEEALSO, "FALSE,IF" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Logical_value") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_true (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	return value_new_bool (TRUE);
}

/***************************************************************************/

static GnmFuncHelp const help_false[] = {
	{ GNM_FUNC_HELP_NAME, F_("FALSE:the value FALSE") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("FALSE returns the value FALSE.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=FALSE()" },
	{ GNM_FUNC_HELP_SEEALSO, "TRUE,IF" },
	{ GNM_FUNC_HELP_EXTREF, F_("wiki:en:Logical_value") },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_false (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	return value_new_bool (FALSE);
}

/***************************************************************************/

GnmFuncDescriptor const logical_functions[] = {
	{ "and", NULL,  help_and, NULL,
	  gnumeric_and,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "or", NULL,  help_or, NULL,
	  gnumeric_or,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "not", "b",  help_not, gnumeric_not,
	  NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
	{ "iferror", "EE",  help_iferror,
	  gnumeric_iferror, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_SECOND,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "ifna", "EE",  help_ifna,
	  gnumeric_ifna, NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_SECOND,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
	  GNM_FUNC_TEST_STATUS_NO_TESTSUITE},
	{ "ifs", NULL,  help_ifs,
	  NULL, gnumeric_ifs,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "switch", NULL,  help_switch,
	  NULL, gnumeric_switch,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
	{ "true", "", help_true, gnumeric_true,
	  NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "false", "", help_false, gnumeric_false,
	  NULL,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_EXHAUSTIVE },
	{ "xor", NULL,  help_xor, NULL,
	  gnumeric_xor,
	  GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_UNITLESS,
	  GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
        {NULL}
};
