/*
 * analysis-ztest.h:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2008 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 * (C) Copyright 2026 by Morten Welinder <terra@gnome.org>
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
#include <tools/analysis-ztest.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>
#include <sheet-object-graph.h>
#include <goffice/goffice.h>
#include <sheet.h>

/************* z-Test: Two Sample for Means ******************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

G_DEFINE_TYPE (GnmZTestTool, gnm_ztest_tool, GNM_TYPE_GENERIC_B_ANALYSIS_TOOL)

static void
gnm_ztest_tool_init (GnmZTestTool *tool)
{
	tool->mean_diff = 0.0;
	tool->var1 = 0.0;
	tool->var2 = 0.0;
}

static gboolean
gnm_ztest_tool_update_dao (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_adjust (dao, 3, 11);
	return FALSE;
}

static char *
gnm_ztest_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("z-Test (%s)"));
}

static gboolean
gnm_ztest_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("z-Test"));
	return FALSE;
}

static gboolean
gnm_ztest_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("z-Test"));
}

static gboolean
gnm_ztest_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmZTestTool *ztool = GNM_ZTEST_TOOL (tool);
	GnmGenericBAnalysisTool *gtool = &ztool->parent;
	GnmValue *val_1;
	GnmValue *val_2;
	GnmExpr const *expr_1;
	GnmExpr const *expr_2;
	GnmExpr const *expr_mean_1;
	GnmExpr const *expr_mean_2;
	GnmExpr const *expr_count_1;
	GnmExpr const *expr_count_2;

	dao_set_italic (dao, 0, 0, 0, 11);
	dao_set_italic (dao, 0, 0, 2, 0);

        dao_set_cell (dao, 0, 0, "");
        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Known Variance"
					"/Observations"
					"/Hypothesized Mean Difference"
					"/Observed Mean Difference"
					"/z"
					"/P (Z<=z) one-tail"
					"/z Critical one-tail"
					"/P (Z<=z) two-tail"
					"/z Critical two-tail"));

	GnmFunc *fd_mean = gnm_func_get_and_use ("AVERAGE");
	GnmFunc *fd_normsdist = gnm_func_get_and_use ("NORMSDIST");
	GnmFunc *fd_abs = gnm_func_get_and_use ("ABS");
	GnmFunc *fd_sqrt = gnm_func_get_and_use ("SQRT");
	GnmFunc *fd_count = gnm_func_get_and_use ("COUNT");
	GnmFunc *fd_normsinv = gnm_func_get_and_use ("NORMSINV");

	val_1 = value_dup (gtool->base.range_1);
	expr_1 = gnm_expr_new_constant (value_dup (val_1));

	val_2 = value_dup (gtool->base.range_2);
	expr_2 = gnm_expr_new_constant (value_dup (val_2));

	/* Labels */
	analysis_tools_write_variable_label (val_1, dao, 1, 0,
					  gtool->base.labels, 1);
	analysis_tools_write_variable_label (val_2, dao, 2, 0,
					  gtool->base.labels, 2);


	/* Mean */
	expr_mean_1 = gnm_expr_new_funcall1 (fd_mean, gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 1, expr_mean_1);
	expr_mean_2 = gnm_expr_new_funcall1 (fd_mean, gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 1, gnm_expr_copy (expr_mean_2));

	/* Known Variance */
	dao_set_cell_float (dao, 1, 2, ztool->var1);
	dao_set_cell_float (dao, 2, 2, ztool->var2);

	/* Observations */
	expr_count_1 = gnm_expr_new_funcall1 (fd_count, expr_1);
	dao_set_cell_expr (dao, 1, 3, expr_count_1);
	expr_count_2 = gnm_expr_new_funcall1 (fd_count, expr_2);
	dao_set_cell_expr (dao, 2, 3, gnm_expr_copy (expr_count_2));

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 4, ztool->mean_diff);

	/* Observed Mean Difference */
	if (dao_cell_is_visible (dao, 2, 1)) {
		gnm_expr_free (expr_mean_2);
		expr_mean_2 = make_cellref (1, -4);
	}

	{
		dao_set_cell_expr (dao, 1, 5,
				   gnm_expr_new_binary
				   (make_cellref (0, -4),
				    GNM_EXPR_OP_SUB,
				    expr_mean_2));
	}

	/* z */
	{
		GnmExpr const *expr_var_1 = make_cellref (0, -4);
		GnmExpr const *expr_var_2 = NULL;
		GnmExpr const *expr_count_1 = make_cellref (0, -3);
		GnmExpr const *expr_a = NULL;
		GnmExpr const *expr_b = NULL;
		GnmExpr const *expr_count_2_adj = NULL;

		if (dao_cell_is_visible (dao, 2, 2)) {
			expr_var_2 = make_cellref (1, -4);
		} else {
			expr_var_2 = gnm_expr_new_constant
			(value_new_float (ztool->var2));
		}

		if (dao_cell_is_visible (dao, 2, 3)) {
			gnm_expr_free (expr_count_2);
			expr_count_2_adj = make_cellref (1, -3);
		} else
			expr_count_2_adj = expr_count_2;

		expr_a = gnm_expr_new_binary (expr_var_1, GNM_EXPR_OP_DIV,
					      expr_count_1);
		expr_b = gnm_expr_new_binary (expr_var_2, GNM_EXPR_OP_DIV,
					      expr_count_2_adj);

		dao_set_cell_expr (dao, 1, 6,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (make_cellref (0, -1),
				     GNM_EXPR_OP_SUB,
				     make_cellref (0, -2)),
				    GNM_EXPR_OP_DIV,
				    gnm_expr_new_funcall1
				    (fd_sqrt,
				     gnm_expr_new_binary
				     (expr_a,
				      GNM_EXPR_OP_ADD,
				      expr_b))));
	}

	/* P (Z<=z) one-tail */
	/* FIXME: 1- looks like a bad idea.  */
	dao_set_cell_expr
		(dao, 1, 7,
		 gnm_expr_new_binary
		 (gnm_expr_new_constant (value_new_int (1)),
		  GNM_EXPR_OP_SUB,
		  gnm_expr_new_funcall1
		  (fd_normsdist,
		   gnm_expr_new_funcall1
		   (fd_abs,
		    make_cellref (0, -1)))));


	/* Critical Z, one right tail */
	dao_set_cell_expr
		(dao, 1, 8,
		 gnm_expr_new_unary
		 (GNM_EXPR_OP_UNARY_NEG,
		  gnm_expr_new_funcall1
		  (fd_normsinv,
		   gnm_expr_new_constant
		   (value_new_float (gtool->base.alpha)))));

	/* P (T<=t) two-tail */
	dao_set_cell_expr
		(dao, 1, 9,
		 gnm_expr_new_binary
		 (gnm_expr_new_constant (value_new_int (2)),
		  GNM_EXPR_OP_MULT,
		  gnm_expr_new_funcall1
		  (fd_normsdist,
		   gnm_expr_new_unary
		   (GNM_EXPR_OP_UNARY_NEG,
		    gnm_expr_new_funcall1
		    (fd_abs,
		     make_cellref (0, -3))))));

	/* Critical Z, two tails */
	dao_set_cell_expr
		(dao, 1, 10,
		 gnm_expr_new_unary
		 (GNM_EXPR_OP_UNARY_NEG,
		  gnm_expr_new_funcall1
		  (fd_normsinv,
		   gnm_expr_new_binary
		   (gnm_expr_new_constant
		    (value_new_float (gtool->base.alpha)),
		    GNM_EXPR_OP_DIV,
		    gnm_expr_new_constant (value_new_int (2))))));

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_normsdist);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_sqrt);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_normsinv);

	/* And finish up */

	value_release (val_1);
	value_release (val_2);

	dao_redraw_respan (dao);

        return FALSE;
}

static void
gnm_ztest_tool_class_init (GnmZTestToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_ztest_tool_update_dao;
	at_class->update_descriptor = gnm_ztest_tool_update_descriptor;
	at_class->prepare_output_range = gnm_ztest_tool_prepare_output_range;
	at_class->format_output_range = gnm_ztest_tool_format_output_range;
	at_class->perform_calc = gnm_ztest_tool_perform_calc;
}

GnmAnalysisTool *
gnm_ztest_tool_new (void)
{
	return g_object_new (GNM_TYPE_ZTEST_TOOL, NULL);
}
