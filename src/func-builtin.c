/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * func-builtin.c:  Built in functions.
 *
 * Authors:
 *   Morten Welinder (terra@gnome.org)
 *   Jody Goldberg (jody@gnome.org)
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <func.h>
#include <func-builtin.h>
#include <rangefunc.h>
#include <collect.h>
#include <value.h>
#include <selection.h>
#include <expr.h>
#include <expr-impl.h>
#include <sheet.h>
#include <cell.h>

/***************************************************************************/

static GnmFuncHelp const help_sum[] = {
    { GNM_FUNC_HELP_OLD,
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
	   "SUM(A1:A5) equals 107.")
    },
    { GNM_FUNC_HELP_SEEALSO, "AVERAGE,COUNT" },
    { GNM_FUNC_HELP_END }
};

GnmValue *
gnumeric_sum (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     gnm_range_sum,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_product[] = {
    { GNM_FUNC_HELP_OLD,
	N_("@FUNCTION=PRODUCT\n"
	   "@SYNTAX=PRODUCT(value1, value2, ...)\n"

	   "@DESCRIPTION="
	   "PRODUCT returns the product of all the values and cells "
	   "referenced in the argument list.\n\n"
	   "* This function is Excel compatible.  In particular, this means "
	   "that if all cells are empty, the result will be 0.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "PRODUCT(2,5,9) equals 90.")
    },
    { GNM_FUNC_HELP_SEEALSO, "SUM,COUNT,G_PRODUCT" },
    { GNM_FUNC_HELP_END }
};

static int
range_bogusproduct (gnm_float const *xs, int n, gnm_float *res)
{
	if (n == 0) {
		*res = 0;  /* Severe Excel brain damange.  */
		return 0;
	} else
		return gnm_range_product (xs, n, res);
}

GnmValue *
gnumeric_product (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return float_range_function (argc, argv, ei,
				     range_bogusproduct,
				     COLLECT_IGNORE_STRINGS |
				     COLLECT_IGNORE_BOOLS |
				     COLLECT_IGNORE_BLANKS,
				     GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_gnumeric_version[] = {
    { GNM_FUNC_HELP_OLD,
	N_("@FUNCTION=GNUMERIC_VERSION\n"
	   "@SYNTAX=GNUMERIC_VERSION()\n"

	   "@DESCRIPTION="
	   "GNUMERIC_VERSION returns the version of gnumeric as a string."

	   "\n"
	   "@EXAMPLES=\n"
	   "GNUMERIC_VERSION().")
    },
    { GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_version (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_string (GNM_VERSION_FULL);
}

/***************************************************************************/

static DependentFlags
gnumeric_table_link (GnmFuncEvalInfo *ei)
{
	GnmDependent *dep = ei->pos->dep;
	GnmRangeRef rr;

	rr.a.col_relative = rr.a.row_relative =
	rr.b.col_relative = rr.b.row_relative = FALSE;
	rr.a.sheet = rr.b.sheet = dep->sheet;

	g_return_val_if_fail (ei->pos->array != NULL, DEPENDENT_IGNORE_ARGS);

	g_return_val_if_fail (ei->pos->eval.col > 0, DEPENDENT_IGNORE_ARGS);
	rr.a.col = rr.b.col = ei->pos->eval.col - 1;
	rr.a.row = ei->pos->eval.row;
	rr.b.row = rr.a.row + ei->pos->array->rows - 1;
	dependent_add_dynamic_dep (dep, &rr);

	g_return_val_if_fail (ei->pos->eval.row > 0, DEPENDENT_IGNORE_ARGS);
	rr.a.row = rr.b.row = ei->pos->eval.row - 1;
	rr.a.col = ei->pos->eval.col;
	rr.b.col = rr.a.col + ei->pos->array->cols - 1;
	dependent_add_dynamic_dep (dep, &rr);

	return DEPENDENT_IGNORE_ARGS;
}

static GnmValue *
gnumeric_table (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmExpr const *arg;
	GnmCell       *in[3], *x_iter, *y_iter;
	GnmValue      *val[3], *res;
	GnmCellPos     pos;
	int x, y;

	/* evaluation clears the dynamic deps */
	gnumeric_table_link (ei);

	if (argc != 2 ||
	    ei->pos->eval.col < 1 ||
	    ei->pos->eval.row < 1)
		return value_new_error_REF (ei->pos);

	for (x = 0; x < 2 ; x++) {
		arg = (x < argc) ? argv[x] : NULL;
		val[x] = NULL;
		if (NULL != arg && GNM_EXPR_GET_OPER (arg) == GNM_EXPR_OP_CELLREF) {
			gnm_cellpos_init_cellref (&pos,
				&arg->cellref.ref, &ei->pos->eval);
			in[x] = sheet_cell_get (ei->pos->sheet, pos.col, pos.row);
			if (NULL == in[x])
				in[x] = sheet_cell_fetch (ei->pos->sheet, pos.col, pos.row);
			else
				val[x] = in[x]->value;
		} else
			in[x] = NULL;
	}

	val[2] = NULL;
	if (NULL != in[0] && NULL != in[1]) {
		in[2] = sheet_cell_get (ei->pos->sheet,
			ei->pos->eval.col - 1, ei->pos->eval.row - 1);
		if (NULL == in[2])
			in[2] = sheet_cell_fetch (ei->pos->sheet,
				ei->pos->eval.col - 1, ei->pos->eval.row - 1);
		else
			val[2] = value_dup (in[2]->value);
	} else
		in[2] = NULL;

	res = value_new_array (ei->pos->array->cols, ei->pos->array->rows);
	for (x = ei->pos->array->cols ; x-- > 0 ; ) {
		x_iter = sheet_cell_get (ei->pos->sheet,
			x + ei->pos->eval.col, ei->pos->eval.row-1);
		if (NULL == x_iter)
			continue;
		if (NULL != in[0]) {
			gnm_cell_eval (x_iter);
			in[0]->value = value_dup (x_iter->value);
			dependent_queue_recalc (&in[0]->base);
		} else
			val[0] = value_dup (x_iter->value);

		for (y = ei->pos->array->rows ; y-- > 0 ; ) {
			y_iter = sheet_cell_get (ei->pos->sheet,
				ei->pos->eval.col-1, y + ei->pos->eval.row);
			if (NULL == y_iter)
				continue;
			gnm_cell_eval (y_iter);
			if (NULL != in[1]) {
				/* not a leak, val[] holds the original */
				in[1]->value = value_dup (y_iter->value);
				dependent_queue_recalc (&in[1]->base);
				if (NULL != in[0]) {
					gnm_cell_eval (in[2]);
					value_array_set (res, x, y, value_dup (in[2]->value));
				} else {
					gnm_cell_eval (x_iter);
					value_array_set (res, x, y, value_dup (x_iter->value));
				}
				value_release (in[1]->value);
			} else
				value_array_set (res, x, y, value_dup (y_iter->value));
		}
		if (NULL == in[0]) {
			value_release (x_iter->value);
			x_iter->value = val[0];
			val[0] = NULL;
		} else
			value_release (in[0]->value);
	}
	if (NULL != in[2])
		value_release (in[2]->value);
	for (x = 0 ; x < 3 ; x++)
		if (in[x]) {
			dependent_queue_recalc (&in[x]->base);

			/* always assign, we still point at a released value */
			if (NULL == (in[x]->value = val[x])) {
				sheet_cell_remove (ei->pos->sheet, in[x], FALSE, FALSE);
				in[x] = NULL;
			}
		}
	for (x = 0 ; x < 3 ; x++)
		if (in[x])
			gnm_cell_eval (in[x]);

	return res;
}
/***************************************************************************/

static GnmFuncGroup *math_group = NULL;
static GnmFuncGroup *gnumeric_group = NULL;

void
func_builtin_init (void)
{
	static GnmFuncDescriptor const builtins [] = {
		{	"sum",		NULL,	N_("number,number,"),
			help_sum,	NULL,	gnumeric_sum,
			NULL, NULL, NULL, GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
			GNM_FUNC_IMPL_STATUS_COMPLETE,
			GNM_FUNC_TEST_STATUS_BASIC
		},
		{	"product",		NULL,	N_("number,number,"),
			help_product,	NULL,	gnumeric_product,
			NULL, NULL, NULL, GNM_FUNC_SIMPLE,
			GNM_FUNC_IMPL_STATUS_COMPLETE,
			GNM_FUNC_TEST_STATUS_BASIC
		},
		{	"gnumeric_version",	"",	"",
			help_gnumeric_version,	gnumeric_version, NULL,
			NULL, NULL, NULL, GNM_FUNC_SIMPLE,
			GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
			GNM_FUNC_TEST_STATUS_EXHAUSTIVE
		},
		{	"table",	"",	"",
			NULL,		NULL,	gnumeric_table,
			gnumeric_table_link, NULL,
			NULL, GNM_FUNC_SIMPLE + GNM_FUNC_INTERNAL,
			GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
			GNM_FUNC_TEST_STATUS_EXHAUSTIVE
		},
		{ NULL }
	};

	math_group = gnm_func_group_fetch (N_("Mathematics"));
	gnm_func_add (math_group, builtins + 0);
	gnm_func_add (math_group, builtins + 1);

	gnumeric_group = gnm_func_group_fetch (N_("Gnumeric"));
	gnm_func_add (gnumeric_group, builtins + 2);
	gnm_func_add (gnumeric_group, builtins + 3);
}

static void
shutdown_cat (GnmFuncGroup *group)
{
	GSList *ptr, *list = g_slist_copy (group->functions);
	for (ptr = list; ptr; ptr = ptr->next)
		gnm_func_free (ptr->data);
	g_slist_free (list);
}

void
func_builtin_shutdown (void)
{
	shutdown_cat (math_group);
	shutdown_cat (gnumeric_group);
}
