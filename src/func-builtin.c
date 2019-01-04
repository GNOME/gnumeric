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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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
#include <expr-deriv.h>
#include <sheet.h>
#include <cell.h>
#include <application.h>
#include <number-match.h>
#include <gutils.h>
#include <ranges.h>

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

static GnmExpr const *
gnumeric_sum_deriv (GnmFunc *func,
		    GnmExpr const *expr,
		    GnmEvalPos const *ep,
		    GnmExprDeriv *info)
{
	GnmExprList *l, *args = gnm_expr_deriv_collect (expr, ep, info);
	gboolean bad = FALSE;

	for (l = args; l; l = l->next) {
		GnmExpr const *e = l->data;
		GnmExpr const *d = gnm_expr_deriv (e, ep, info);
		if (d) {
			gnm_expr_free (e);
			l->data = (gpointer)d;
		} else {
			bad = TRUE;
			break;
		}
	}

	if (bad) {
		for (l = args; l; l = l->next)
			gnm_expr_free (l->data);
		gnm_expr_list_free (args);
		return NULL;
	} else
		return gnm_expr_new_funcall (func, args);
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
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_version (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_string (GNM_VERSION_FULL);
}

/***************************************************************************/

static GnmFuncHelp const help_table[] = {
	{ GNM_FUNC_HELP_NAME, N_("TABLE:internal function for data tables")},
	{ GNM_FUNC_HELP_DESCRIPTION, N_("This function should not be called directly.")},
	{ GNM_FUNC_HELP_END }
};


static int //GnmDependentFlags
gnumeric_table_link (const GnmFunc *func, GnmFuncEvalInfo *ei, gboolean qlink)
{
	GnmDependent *dep = ei->pos->dep;
	GnmRangeRef rr;
	int cols, rows;

	if (!qlink)
		return DEPENDENT_NO_FLAG;

	if (!eval_pos_is_array_context (ei->pos))
		return DEPENDENT_IGNORE_ARGS;

	gnm_expr_top_get_array_size (ei->pos->array_texpr, &cols, &rows);

	rr.a.col_relative = rr.a.row_relative =
	rr.b.col_relative = rr.b.row_relative = FALSE;
	rr.a.sheet = rr.b.sheet = dep->sheet;

	g_return_val_if_fail (ei->pos->eval.col > 0, DEPENDENT_IGNORE_ARGS);
	rr.a.col = rr.b.col = ei->pos->eval.col - 1;
	rr.a.row = ei->pos->eval.row;
	rr.b.row = rr.a.row + rows - 1;
	dependent_add_dynamic_dep (dep, &rr);

	g_return_val_if_fail (ei->pos->eval.row > 0, DEPENDENT_IGNORE_ARGS);
	rr.a.row = rr.b.row = ei->pos->eval.row - 1;
	rr.a.col = ei->pos->eval.col;
	rr.b.col = rr.a.col + cols - 1;
	dependent_add_dynamic_dep (dep, &rr);

	return DEPENDENT_IGNORE_ARGS;
}

static GnmValue *
gnumeric_table (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmCell       *in[3], *x_iter, *y_iter;
	GnmValue      *val[3], *res;
	GnmCellPos     pos;
	GnmEvalPos const *ep = ei->pos;
	int x, y;
	int cols, rows;

	/* evaluation clears the dynamic deps */
	gnumeric_table_link (gnm_eval_info_get_func (ei), ei, TRUE);

	if (argc != 2 ||
	    ep->eval.col < 1 ||
	    ep->eval.row < 1 ||
	    !eval_pos_is_array_context (ep))
		return value_new_error_REF (ep);

	for (x = 0; x < 2 ; x++) {
		GnmExpr const *arg = argv[x];
		GnmCellRef const *cr = arg ? gnm_expr_get_cellref (arg) : NULL;
		in[x] = NULL;
		val[x] = NULL;

		if (cr) {
			gnm_cellpos_init_cellref (&pos,	cr, &ep->eval, ep->sheet);
			in[x] = sheet_cell_get (ep->sheet, pos.col, pos.row);
			if (NULL == in[x])
				in[x] = sheet_cell_fetch (ep->sheet, pos.col, pos.row);
			else {
				val[x] = value_dup (in[x]->value);
				if (gnm_cell_has_expr (in[x]) &&
				    gnm_cell_expr_is_linked (in[x]))
					dependent_unlink (GNM_CELL_TO_DEP (in[x]));
			}
		}
	}

	in[2] = NULL;
	val[2] = NULL;
	if (NULL != in[0] && NULL != in[1]) {
		in[2] = sheet_cell_get (ep->sheet,
					ep->eval.col - 1, ep->eval.row - 1);
		if (NULL == in[2])
			in[2] = sheet_cell_fetch (ep->sheet,
				ep->eval.col - 1, ep->eval.row - 1);
		else
			val[2] = value_dup (in[2]->value);
	}

	gnm_expr_top_get_array_size (ei->pos->array_texpr, &cols, &rows);

	res = value_new_array (cols, rows);
	for (x = cols ; x-- > 0 ; ) {
		x_iter = sheet_cell_get (ep->sheet,
			x + ep->eval.col, ep->eval.row-1);
		if (NULL == x_iter)
			continue;
		gnm_cell_eval (x_iter);
		if (NULL != in[0]) {
			GnmValue *v0 = value_dup (x_iter->value);
			value_release (in[0]->value);
			in[0]->value = v0;
			dependent_queue_recalc (GNM_CELL_TO_DEP (in[0]));
			gnm_app_recalc_clear_caches ();
		} else {
			value_release (val[0]);
			val[0] = value_dup (x_iter->value);
		}

		for (y = rows ; y-- > 0 ; ) {
			g_signal_emit_by_name (gnm_app_get_app (), "recalc-finished");
			y_iter = sheet_cell_get (ep->sheet,
				ep->eval.col-1, y + ep->eval.row);
			if (NULL == y_iter)
				continue;
			gnm_cell_eval (y_iter);
			if (NULL != in[1]) {
				GnmValue *v1 = value_dup (in[1]->value);
				GnmValue *vy = value_dup (y_iter->value);
				value_release (in[1]->value);
				in[1]->value = vy;
				dependent_queue_recalc (GNM_CELL_TO_DEP (in[1]));
				gnm_app_recalc_clear_caches ();
				if (NULL != in[0]) {
					gnm_cell_eval (in[2]);
					value_array_set (res, x, y, value_dup (in[2]->value));
				} else {
					gnm_cell_eval (x_iter);
					value_array_set (res, x, y, value_dup (x_iter->value));
				}
				value_release (in[1]->value);
				in[1]->value = v1;
			} else
				value_array_set (res, x, y, value_dup (y_iter->value));
		}
		if (in[0]) {
			value_release (in[0]->value);
			in[0]->value = value_dup (val[0]);
		}
	}
	if (NULL != in[2]) {
		value_release (in[2]->value);
		in[2]->value = NULL;
	}
	for (x = 0 ; x < 2 ; x++)
		if (in[x] &&
		    gnm_cell_has_expr (in[x]) &&
		    !gnm_cell_expr_is_linked (in[x]))
			dependent_link (&in[x]->base);

	for (x = 0 ; x < 3 ; x++) {
		int y;
		for (y = x + 1; y < 3; y++) {
			if (in[x] == in[y])
				in[y] = NULL;
		}

		if (in[x]) {
			gboolean had_cell = (val[x] != NULL);

			value_release (in[x]->value);
			in[x]->value = val[x];
			val[x] = NULL;

			dependent_queue_recalc (GNM_CELL_TO_DEP (in[x]));

			/* always assign, we still point at a released value */
			if (!had_cell) {
				sheet_cell_remove (ep->sheet, in[x], FALSE, FALSE);
				in[x] = NULL;
			}
			gnm_app_recalc_clear_caches ();
		}
	}

	for (x = 0 ; x < 3 ; x++) {
		if (in[x])
			gnm_cell_eval (in[x]);
		value_release (val[x]);
	}

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

static GnmValue *
gnumeric_if (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	gboolean err;
	int res = value_get_as_bool (args[0], &err) ? 1 : 2;

	if (args[res])
		return value_dup (args[res]);

	if (gnm_eval_info_get_arg_count (ei) < res + 1)
		/* arg-not-there: default to TRUE/FALSE.  */
		return value_new_bool (res == 1);
	else
		/* arg blank: default to 0.  */
		return value_new_int (0);
}

/**
 * gnumeric_if2: (skip)
 */
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

static GnmFuncHelp const help_deriv[] = {
	/* Not for public consumption. */
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_deriv (GnmFuncEvalInfo *ei, GnmValue const * const *args)
{
	GnmValue const *vy = args[0];
	GnmValue const *vx = args[1];
	Sheet *sy, *sy2, *sx, *sx2;
	GnmRange ry, rx;
	GnmCell *cy, *cx;

	if (!VALUE_IS_CELLRANGE (vy) ||
	    !VALUE_IS_CELLRANGE (vx))
		return value_new_error_VALUE (ei->pos);

	gnm_rangeref_normalize (value_get_rangeref (vy), ei->pos, &sy, &sy2, &ry);
	gnm_rangeref_normalize (value_get_rangeref (vx), ei->pos, &sx, &sx2, &rx);
	if (!range_is_singleton (&ry) || sy2 != sy ||
	    !range_is_singleton (&rx) || sx2 != sx)
		return value_new_error_VALUE (ei->pos);

	cy = sheet_cell_get (sy, ry.start.col, ry.start.row);
	cx = sheet_cell_get (sx, rx.start.col, rx.start.row);
	if (!cy || !cx)
		return value_new_error_VALUE (ei->pos);

	return value_new_float (gnm_expr_cell_deriv_value (cy, cx));
}

/***************************************************************************/

static GnmFuncGroup *math_group = NULL;
static GnmFuncGroup *gnumeric_group = NULL;
static GnmFuncGroup *logic_group = NULL;

static GnmFuncDescriptor const builtins [] = {
	/* --- Math --- */
	{	"sum",		NULL,
		help_sum,	NULL,	gnumeric_sum,
		GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_FIRST,
		GNM_FUNC_IMPL_STATUS_COMPLETE,
		GNM_FUNC_TEST_STATUS_BASIC
	},
	{	"product",		NULL,
		help_product,	NULL,	gnumeric_product,
		GNM_FUNC_SIMPLE,
		GNM_FUNC_IMPL_STATUS_COMPLETE,
		GNM_FUNC_TEST_STATUS_BASIC
	},
	/* --- Gnumeric --- */
	{	"gnumeric_version",	"",
		help_gnumeric_version,	gnumeric_version, NULL,
		GNM_FUNC_SIMPLE,
		GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
		GNM_FUNC_TEST_STATUS_EXHAUSTIVE
	},
	{	"table", NULL,
		help_table,		NULL,	gnumeric_table,
		GNM_FUNC_SIMPLE + GNM_FUNC_INTERNAL,
		GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
		GNM_FUNC_TEST_STATUS_EXHAUSTIVE
	},
	{	"number_match", "s|s", // Only in test suite
		help_number_match, gnumeric_number_match, NULL,
		GNM_FUNC_INTERNAL,
		GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
		GNM_FUNC_TEST_STATUS_BASIC },
	{	"deriv", "r|r",  // Only in test suite
		help_deriv, gnumeric_deriv, NULL,
		GNM_FUNC_INTERNAL,
		GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC,
		GNM_FUNC_TEST_STATUS_BASIC },
	/* --- Logic --- */
	{	"if", "b|EE",
		help_if, gnumeric_if, NULL,
		GNM_FUNC_SIMPLE + GNM_FUNC_AUTO_SECOND,
		GNM_FUNC_IMPL_STATUS_COMPLETE,
		GNM_FUNC_TEST_STATUS_BASIC },
	{ NULL }
};

/**
 * gnm_func_builtin_init: (skip)
 */
void
gnm_func_builtin_init (void)
{
	const char *gname;
	const char *tdomain = GETTEXT_PACKAGE;
	int i = 0;

	gname = N_("Mathematics");
	math_group = gnm_func_group_fetch (gname, _(gname));
	gnm_func_add (math_group, builtins + i++, tdomain);
	gnm_func_add (math_group, builtins + i++, tdomain);

	gname = N_("Gnumeric");
	gnumeric_group = gnm_func_group_fetch (gname, _(gname));
	gnm_func_add (gnumeric_group, builtins + i++, tdomain);
	gnm_func_add (gnumeric_group, builtins + i++, tdomain);
	if (gnm_debug_flag ("testsuite")) {
		gnm_func_add (gnumeric_group, builtins + i, tdomain);
		gnm_func_add (gnumeric_group, builtins + i + 1, tdomain);
	}
	i += 2;

	gname = N_("Logic");
	logic_group = gnm_func_group_fetch (gname, _(gname));
	gnm_func_add (logic_group, builtins + i++, tdomain);

	g_signal_connect (gnm_func_lookup ("table", NULL),
			  "link-dep", G_CALLBACK (gnumeric_table_link), NULL);

	g_signal_connect (gnm_func_lookup ("sum", NULL),
			  "derivative", G_CALLBACK (gnumeric_sum_deriv), NULL);
}

/**
 * gnm_func_builtin_shutdown: (skip)
 */
void
gnm_func_builtin_shutdown (void)
{
	int i;

	for (i = 0; builtins[i].name; i++) {
		GnmFunc *func = gnm_func_lookup (builtins[i].name, NULL);
		if (func)
			g_object_unref (func);
	}
}
