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
#include <application.h>
#include <number-match.h>
#include <gutils.h>

/***************************************************************************/

static GnmFuncHelp const help_sum[] = {
	/* xgettext : see po-functions/README.translators */
	{ GNM_FUNC_HELP_NAME, N_("SUM:sum of the given values")},
	/* xgettext : see po-functions/README.translators */
	{ GNM_FUNC_HELP_ARG, N_("values:a list of values to add")},
	{ GNM_FUNC_HELP_DESCRIPTION, N_("SUM computes the sum of all the values and cells referenced in the argument list.")},
	{ GNM_FUNC_HELP_EXCEL, N_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, N_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=SUM(11,15,17,21,43)" },
	{ GNM_FUNC_HELP_SEEALSO, "AVERAGE,COUNT"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
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
	/* xgettext : see po-functions/README.translators */
	{ GNM_FUNC_HELP_NAME, N_("PRODUCT:product of the given values")},
	/* xgettext : see po-functions/README.translators */
	{ GNM_FUNC_HELP_ARG, N_("values:a list of values to multiply")},
	{ GNM_FUNC_HELP_DESCRIPTION, N_("PRODUCT computes the product of all the values and cells referenced in the argument list.")},
	{ GNM_FUNC_HELP_NOTE, N_("If all cells are empty, the result will be 0.") },
	{ GNM_FUNC_HELP_EXCEL, N_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_ODF, N_("This function is OpenFormula compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=PRODUCT(2,5,9)" },
	{ GNM_FUNC_HELP_SEEALSO, "SUM,COUNT,G_PRODUCT"},
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

static GnmValue *
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
	/* xgettext : see po-functions/README.translators */
 	{ GNM_FUNC_HELP_NAME, N_("GNUMERIC_VERSION:the current version of Gnumeric")},
	{ GNM_FUNC_HELP_DESCRIPTION, N_("GNUMERIC_VERSION returns the version of gnumeric as a string.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=GNUMERIC_VERSION()" },
	{ GNM_FUNC_HELP_SEEALSO, ""},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_version (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_string (GNM_VERSION_FULL);
}

/***************************************************************************/

static GnmDependentFlags
gnumeric_table_link (GnmFuncEvalInfo *ei, gboolean qlink)
{
	GnmDependent *dep = ei->pos->dep;
	GnmRangeRef rr;

	if (!qlink)
		return DEPENDENT_NO_FLAG;

	rr.a.col_relative = rr.a.row_relative =
	rr.b.col_relative = rr.b.row_relative = FALSE;
	rr.a.sheet = rr.b.sheet = dep->sheet;

	g_return_val_if_fail (eval_pos_is_array_context (ei->pos), DEPENDENT_IGNORE_ARGS);

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
	GnmCell       *in[3], *x_iter, *y_iter;
	GnmValue      *val[3], *res;
	GnmCellPos     pos;
	int x, y;

	/* evaluation clears the dynamic deps */
	gnumeric_table_link (ei, TRUE);

	if (argc != 2 ||
	    ei->pos->eval.col < 1 ||
	    ei->pos->eval.row < 1)
		return value_new_error_REF (ei->pos);

	for (x = 0; x < 2 ; x++) {
		GnmExpr const *arg = argv[x];
		val[x] = NULL;
		if (NULL != arg && GNM_EXPR_GET_OPER (arg) == GNM_EXPR_OP_CELLREF) {
			gnm_cellpos_init_cellref (&pos,	&arg->cellref.ref,
						  &ei->pos->eval, ei->pos->sheet);
			in[x] = sheet_cell_get (ei->pos->sheet, pos.col, pos.row);
			if (NULL == in[x])
				in[x] = sheet_cell_fetch (ei->pos->sheet, pos.col, pos.row);
			else {
				val[x] = in[x]->value;
				if (gnm_cell_has_expr (in[x]) &&
				    gnm_cell_expr_is_linked (in[x]))
					dependent_unlink (&in[x]->base);
			}
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
			gnm_app_recalc_clear_caches ();
		} else
			val[0] = value_dup (x_iter->value);

		for (y = ei->pos->array->rows ; y-- > 0 ; ) {
			g_signal_emit_by_name (gnm_app_get_app (), "recalc-finished");
			y_iter = sheet_cell_get (ei->pos->sheet,
				ei->pos->eval.col-1, y + ei->pos->eval.row);
			if (NULL == y_iter)
				continue;
			gnm_cell_eval (y_iter);
			if (NULL != in[1]) {
				/* not a leak, val[] holds the original */
				in[1]->value = value_dup (y_iter->value);
				dependent_queue_recalc (&in[1]->base);
				gnm_app_recalc_clear_caches ();
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
	for (x = 0 ; x < 2 ; x++)
		if (in[x] &&
		    gnm_cell_has_expr (in[x]) &&
		    !gnm_cell_expr_is_linked (in[x]))
			dependent_link (&in[x]->base);

	for (x = 0 ; x < 3 ; x++)
		if (in[x]) {
			dependent_queue_recalc (&in[x]->base);

			/* always assign, we still point at a released value */
			if (NULL == (in[x]->value = val[x])) {
				sheet_cell_remove (ei->pos->sheet, in[x], FALSE, FALSE);
				in[x] = NULL;
			}
			gnm_app_recalc_clear_caches ();
		}
	for (x = 0 ; x < 3 ; x++)
		if (in[x])
			gnm_cell_eval (in[x]);

	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_if[] = {
	/* xgettext : see po-functions/README.translators */
	{ GNM_FUNC_HELP_NAME, N_("IF:conditional expression") },
	/* xgettext : see po-functions/README.translators */
	{ GNM_FUNC_HELP_ARG, N_("cond:condition") },
	/* xgettext : see po-functions/README.translators */
	{ GNM_FUNC_HELP_ARG, N_("trueval:value to use if condition is true") },
	/* xgettext : see po-functions/README.translators */
	{ GNM_FUNC_HELP_ARG, N_("falseval:value to use if condition is false") },
	{ GNM_FUNC_HELP_DESCRIPTION, N_("This function first evaluates the condition.  If the result is true, it will then evaluate and return the second argument.  Otherwise, it will evaluate and return the last argument.") },
        { GNM_FUNC_HELP_EXAMPLES, "=IF(1+2=3,\"x\",\"y\")" },
	{ GNM_FUNC_HELP_SEEALSO, "AND,OR,XOR,NOT,IFERROR" },
	{ GNM_FUNC_HELP_END }
};

GnmValue *
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


GnmValue *
gnumeric_if2 (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv,
	      GnmExprEvalFlags flags)
{
	gboolean err;
	int i, branch;
	GnmValue *args[3];
	GnmValue *res;

	g_return_val_if_fail (argc >= 1 && argc <= 3,
			      value_new_error_VALUE (ei->pos));

	/*
	 * In this version of IF, we evaluate the arguments ourselves,
	 * then call the regular IF.  However, arguments we do not need
	 * we do not evaluate.
	 *
	 * IF is sometimes used to avoid expensive calculations.  Always
	 * computing both branches destroys that intent.  See bug 326595.
	 */

	/* Evaluate condition.  */
	res = gnm_expr_eval (argv[0], ei->pos, 0);
	if (VALUE_IS_ERROR (res))
		return res;
	args[0] = res;

	branch = value_get_as_bool (args[0], &err) ? 1 : 2;
	for (i = 1; i <= 2; i++) {
		args[i] = NULL;
		if (i < argc && i == branch && !gnm_expr_is_empty (argv[i])) {
			args[i] = gnm_expr_eval (argv[i], ei->pos, flags);
			if (!args[i])
				args[i] = value_new_empty ();
		}
	}

	res = gnumeric_if (ei, (GnmValue const * const *)args);

	for (i = 0; i <= 2; i++)
		value_release (args[i]);

	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_number_match[] = {
	/* Not for public consumption. */
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_number_match (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	const char *text = value_peek_string (args[0]);
	const char *fmttxt = args[1] ? value_peek_string (args[1]) : NULL;
	GOFormat *fmt = NULL;
	GnmValue *v;
	GODateConventions const *date_conv = NULL;

	if (fmttxt && *fmttxt != 0) {
		fmt = go_format_new_from_XL (fmttxt);
		if (go_format_is_invalid (fmt)) {
			v = value_new_error_VALUE (ei->pos);
			goto out;
		}
	}

	v = format_match (text, fmt, date_conv);
	if (!v) v = value_new_string (text);

 out:
	go_format_unref (fmt);
	return v;
}

/***************************************************************************/

static GnmFuncGroup *math_group = NULL;
static GnmFuncGroup *gnumeric_group = NULL;
static GnmFuncGroup *logic_group = NULL;

void
func_builtin_init (void)
{
	const char *gname;
	const char *textdomain = GETTEXT_PACKAGE;
	int i = 0;

	static GnmFuncDescriptor const builtins [] = {
		/* --- Math --- */
		{	"sum",		NULL,
			help_sum,	NULL,	gnumeric_sum,
			NULL, NULL, GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
			GNM_FUNC_IMPL_STATUS_COMPLETE,
			GNM_FUNC_TEST_STATUS_BASIC
		},
		{	"product",		NULL,
			help_product,	NULL,	gnumeric_product,
			NULL, NULL, GNM_FUNC_SIMPLE,
			GNM_FUNC_IMPL_STATUS_COMPLETE,
			GNM_FUNC_TEST_STATUS_BASIC
		},
		/* --- Gnumeric --- */
		{	"gnumeric_version",	"",
			help_gnumeric_version,	gnumeric_version, NULL,
			NULL, NULL, GNM_FUNC_SIMPLE,
			GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
			GNM_FUNC_TEST_STATUS_EXHAUSTIVE
		},
		{	"table",	"",
			NULL,		NULL,	gnumeric_table,
			gnumeric_table_link,
			NULL, GNM_FUNC_SIMPLE + GNM_FUNC_INTERNAL,
			GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
			GNM_FUNC_TEST_STATUS_EXHAUSTIVE
		},
		{	"number_match", "s|s",
			help_number_match, gnumeric_number_match, NULL,
			NULL, NULL,
			GNM_FUNC_SIMPLE,
			GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
			GNM_FUNC_TEST_STATUS_BASIC },
		/* --- Logic --- */
		{	"if", "b|EE",
			help_if, gnumeric_if, NULL,
			NULL, NULL,
			GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_SECOND,
			GNM_FUNC_IMPL_STATUS_COMPLETE,
			GNM_FUNC_TEST_STATUS_BASIC },
		{ NULL }
	};

	gname = N_("Mathematics");
	math_group = gnm_func_group_fetch (gname, _(gname));
	gnm_func_add (math_group, builtins + i++, textdomain);
	gnm_func_add (math_group, builtins + i++, textdomain);

	gname = N_("Gnumeric");
	gnumeric_group = gnm_func_group_fetch (gname, _(gname));
	gnm_func_add (gnumeric_group, builtins + i++, textdomain);
	gnm_func_add (gnumeric_group, builtins + i++, textdomain);
	if (gnm_debug_flag ("testsuite"))
		gnm_func_add (gnumeric_group, builtins + i, textdomain);
	i++;

	gname = N_("Logic");
	logic_group = gnm_func_group_fetch (gname, _(gname));
	gnm_func_add (logic_group, builtins + i++, textdomain);
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
	shutdown_cat (logic_group);
}
