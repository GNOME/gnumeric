/*
 * func-builtin.c:  Built in functions.
 *
 * Authors:
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
#include <func-builtin.h>
#include <rangefunc.h>
#include <collect.h>
#include <value.h>
#include <selection.h>

/***************************************************************************/

static const char *help_sum = {
	N_("@FUNCTION=SUM\n"
	   "@SYNTAX=SUM(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "SUM computes the sum of all the values and cells referenced "
	   "in the argument list.\n\n"
	   "* This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "Let us assume that the cells A1, A2, ..., A5 contain numbers "
	   "11, 15, 17, 21, and 43.  Then\n"
	   "SUM(A1:A5) equals 107.\n"
	   "\n"
	   "@SEEALSO=AVERAGE, COUNT")
};

Value *
gnumeric_sum (FunctionEvalInfo *ei, GnmExprList *nodes)
{
	return float_range_function (nodes, ei,
				     range_sum,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static const char *help_product = {
	N_("@FUNCTION=PRODUCT\n"
	   "@SYNTAX=PRODUCT(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "PRODUCT returns the product of all the values and cells "
	   "referenced in the argument list.\n\n"
	   "* This function is Excel compatible.  In particular, this means "
	   "that if all cells are empty, the result will be 0.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "PRODUCT(2,5,9) equals 90.\n"
	   "\n"
	   "@SEEALSO=SUM, COUNT, G_PRODUCT")
};

static int
range_bogusproduct (const gnm_float *xs, int n, gnm_float *res)
{
	if (n == 0) {
		*res = 0;  /* Severe Excel brain damange.  */
		return 0;
	} else
		return range_product (xs, n, res);
}

Value *
gnumeric_product (FunctionEvalInfo *ei, GnmExprList *nodes)
{
	return float_range_function (nodes, ei,
				     range_bogusproduct,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     gnumeric_err_VALUE);
}

/***************************************************************************/

static const char *help_gnumeric_version = {
	N_("@FUNCTION=GNUMERIC_VERSION\n"
	   "@SYNTAX=GNUMERIC_VERSION()\n"

	   "@DESCRIPTION="
	   "GNUMERIC_VERSION return the version of gnumeric as a string."

	   "\n"
	   "@EXAMPLES=\n"
	   "GNUMERIC_VERSION().\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_version (FunctionEvalInfo *ei, Value *argv [])
{
	return value_new_string (GNUMERIC_VERSION);
}

/***************************************************************************/

static const char *mathcatname = N_("Maths / Trig.");
static GSList *mathfuncs = NULL;

static const char *gnumericcatname = N_("Gnumeric");
static GSList *gnumericfuncs = NULL;

void
func_builtin_init (void)
{
	static GnmFuncDescriptor const builtins [] = {
		{	"sum",		NULL,	N_("number,number,"),
			&help_sum,	NULL,	gnumeric_sum,
			NULL, NULL, NULL, GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
			GNM_FUNC_IMPL_STATUS_COMPLETE,
			GNM_FUNC_TEST_STATUS_BASIC
		},
		{	"product",		NULL,	N_("number,number,"),
			&help_product,	NULL,	gnumeric_product,
			NULL, NULL, NULL, GNM_FUNC_SIMPLE,
			GNM_FUNC_IMPL_STATUS_COMPLETE,
			GNM_FUNC_TEST_STATUS_BASIC
		},
		{	"gnumeric_version",	"",	"",
			&help_gnumeric_version,	gnumeric_version,
			NULL, NULL, NULL, NULL, GNM_FUNC_SIMPLE,
			GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
			GNM_FUNC_TEST_STATUS_EXHAUSTIVE
		},
		{ NULL }
	};

	GnmFunc *func;
	GnmFuncGroup *mathcat = gnm_func_group_fetch (mathcatname);
	GnmFuncGroup *gnumericcat = gnm_func_group_fetch (gnumericcatname);

	func = gnm_func_add (mathcat, builtins + 0);
	mathfuncs = g_slist_prepend (mathfuncs, func);

	func = gnm_func_add (mathcat, builtins + 1);
	mathfuncs = g_slist_prepend (mathfuncs, func);

	func = gnm_func_add (gnumericcat, builtins + 2);
	gnumericfuncs = g_slist_prepend (gnumericfuncs, func);
}

static void
shutdown_cat (const char *catname, GSList **funcs)
{
	GSList *tmp;
	GnmFuncGroup *cat = gnm_func_group_fetch (catname);

	for (tmp = *funcs; tmp; tmp = tmp->next) {
		GnmFunc *def = tmp->data;
		char const *name = gnm_func_get_name (def);
		function_remove (cat, name);
	}
	g_slist_free (*funcs);
	*funcs = NULL;
}


void
func_builtin_shutdown (void)
{
	shutdown_cat (mathcatname, &mathfuncs);
	shutdown_cat (gnumericcatname, &gnumericfuncs);
}
