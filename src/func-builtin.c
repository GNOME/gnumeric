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
#include <gnumeric.h>
#include <func.h>
#include <libgnome/gnome-i18n.h>
#include <rangefunc.h>
#include <auto-format.h>
#include <collect.h>
#include <value.h>
#include <selection.h>

/***************************************************************************/

static const char *help_sum = {
	N_("@FUNCTION=SUM\n"
	   "@SYNTAX=SUM(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "SUM computes the sum of all the values and cells referenced "
	   "in the argument list.\n"
	   "This function is Excel compatible."
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
	   "referenced in the argument list.\n"
	   "This function is Excel compatible.  In particular, this means "
	   "that if all cells are empty, the result will be 0."
	   "\n"
	   "@EXAMPLES=\n"
	   "PRODUCT(2,5,9) equals 90.\n"
	   "\n"
	   "@SEEALSO=SUM, COUNT, G_PRODUCT")
};

static int
range_bogusproduct (const gnum_float *xs, int n, gnum_float *res)
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

static const char *help_selection = {
	N_("@FUNCTION=SELECTION\n"
	   "@SYNTAX=SELECTION(permit_intersection)\n"

	   "@DESCRIPTION="
	   "SELECTION function returns a list with the values in the current "
	   "selection.  This is usually used to implement on-the-fly computation "
	   "of values.\n"
	   "If @permit_intersection is TRUE the user specifed selection "
	   "ranges are returned, EVEN IF THEY OVERLAP.  "
	   "If @permit_intersection is FALSE a distict set of regions is "
	   "returned, however, there may be more of them than "
	   "the user initially specified."

	   "\n"
	   "@EXAMPLES=\n"
	   "\n"
	   "@SEEALSO=")
};

typedef struct
{
	GSList * res;
	int	index;
} selection_accumulator;

static void
accumulate_regions (Sheet *sheet,  Range const *r, gpointer closure)
{
	selection_accumulator *accum = closure;
	CellRef a, b;

	/* Fill it in */
	/* start */
	a.sheet = sheet;
	a.col_relative = a.row_relative = FALSE;
	a.col = r->start.col;
	a.row = r->start.row;

	/* end */
	b.sheet = sheet;
	b.col_relative = b.row_relative = FALSE;
	b.col = r->end.col;
	b.row = r->end.row;

	/* Dummy up the eval pos it does not matter */
	accum->res = g_slist_prepend (accum->res,
				      value_new_cellrange(&a, &b, 0, 0));
	accum->index++;
}

/* This routine is used to implement the auto_expr functionality.  It is called
 * to provide the selection to the defined functions.
 */
static Value *
gnumeric_selection (FunctionEvalInfo *ei, Value *argv [])
{
	Sheet * const sheet = ei->pos->sheet;
	gboolean const permit_intersection = argv [0]->v_bool.val;
	Value * res;
	int i;

	selection_accumulator accum;
	accum.res = NULL;
	accum.index = 0;
	selection_apply (sheet, &accumulate_regions,
			 permit_intersection, &accum);

	i = accum.index;
	res = value_new_array_empty (i, 1);
	while (i-- > 0) {
		/* pop the 1st element off the list */
		Value *range = accum.res->data;
		accum.res = g_slist_remove (accum.res, range);

		value_array_set (res, i, 0, range);
	}
	return res;
}

/***************************************************************************/

static const char *mathcatname = N_("Maths / Trig.");
static GSList *mathfuncs = NULL;

static const char *gnumericcatname = N_("Gnumeric");
static GSList *gnumericfuncs = NULL;

static const char *sheetcatname = N_("Sheet");
static GSList *sheetfuncs = NULL;

void
func_builtin_init (void)
{
	FunctionDefinition *def;
	FunctionCategory *mathcat = function_get_category_with_translation
		(mathcatname, _(mathcatname));
	FunctionCategory *gnumericcat = function_get_category_with_translation
		(gnumericcatname, _(gnumericcatname));
	FunctionCategory *sheetcat = function_get_category_with_translation
		(sheetcatname, _(sheetcatname));

	def = function_add_nodes (mathcat, "sum",     0,
				  "number1,number2,...",
				  &help_sum, gnumeric_sum);
	auto_format_function_result (def, AF_FIRST_ARG_FORMAT);
	mathfuncs = g_slist_prepend (mathfuncs, def);

	def = function_add_nodes (mathcat, "product", 0,
				  "number",    &help_product,  gnumeric_product);
	mathfuncs = g_slist_prepend (mathfuncs, def);

	def = function_add_args (gnumericcat, "gnumeric_version", "",  "",
				 &help_gnumeric_version, gnumeric_version);
	gnumericfuncs = g_slist_prepend (gnumericfuncs, def);

	def = function_add_args (sheetcat, "selection", "b",  "permit_intersection",
				 &help_selection, gnumeric_selection);
	sheetfuncs = g_slist_prepend (sheetfuncs, def);
}

static void
shutdown_cat (const char *catname, GSList **funcs)
{
	GSList *tmp;

	FunctionCategory *cat =
		function_get_category_with_translation (catname, _(catname));

	for (tmp = *funcs; tmp; tmp = tmp->next) {
		FunctionDefinition *def = tmp->data;
		const char *name = function_def_get_name (def);

		auto_format_function_result_remove (name);
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
	shutdown_cat (sheetcatname, &sheetfuncs);
}
