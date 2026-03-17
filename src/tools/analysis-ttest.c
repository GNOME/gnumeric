/*
 * analysis-ttest.c:
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
#include <tools/analysis-ttest.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>
#include <sheet-object-graph.h>
#include <goffice/goffice.h>
#include <sheet.h>

/************* t-Test Tools ********************************************
 *
 * The t-Test tool set consists of three kinds of tests to test the
 * mean of two variables.  The tests are: Student's t-test for paired
 * sample, Student's t-test for two samples assuming equal variance
 * and the same test assuming unequal variance.  The results are given
 * in a table which can be printed out in a new sheet, in a new
 * workbook, or simply into an existing sheet.
 *
 **/

/* t-Test: Paired Two Sample for Means.
 */

G_DEFINE_TYPE (GnmTTestPairedTool, gnm_ttest_paired_tool, GNM_TYPE_GENERIC_B_ANALYSIS_TOOL)

static void
gnm_ttest_paired_tool_init (GnmTTestPairedTool *tool)
{
	tool->mean_diff = 0.0;
	tool->var1 = 0.0;
	tool->var2 = 0.0;
}

static gboolean
gnm_ttest_paired_tool_update_dao (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_adjust (dao, 3, 14);
	return FALSE;
}

static char *
gnm_ttest_paired_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("t-Test, paired (%s)"));
}

static gboolean
gnm_ttest_paired_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("t-Test"));
	return FALSE;
}

static gboolean
gnm_ttest_paired_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("t-Test"));
}

static gboolean
gnm_ttest_paired_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmTTestPairedTool *ttool = GNM_TTEST_PAIRED_TOOL (tool);
	GnmGenericBAnalysisTool *gtool = &ttool->parent;
	GnmValue *val_1;
	GnmValue *val_2;
	GnmExpr const *expr_1;
	GnmExpr const *expr_2;
	GnmExpr const *expr_diff;
	GnmExpr const *expr_ifisnumber;
	GnmExpr const *expr_ifisoddifisnumber;

	dao_set_italic (dao, 0, 0, 0, 13);
	dao_set_italic (dao, 0, 0, 2, 0);

        dao_set_cell (dao, 0, 0, "");
        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/Pearson Correlation"
					"/Hypothesized Mean Difference"
					"/Observed Mean Difference"
					"/Variance of the Differences"
					"/df"
					"/t Stat"
					"/P (T<=t) one-tail"
					"/t Critical one-tail"
					"/P (T<=t) two-tail"
					"/t Critical two-tail"));

	GnmFunc *fd_mean = gnm_func_get_and_use ("AVERAGE");
	GnmFunc *fd_var = gnm_func_get_and_use ("VAR");
	GnmFunc *fd_count = gnm_func_get_and_use ("COUNT");
	GnmFunc *fd_correl = gnm_func_get_and_use ("CORREL");
	GnmFunc *fd_tinv = gnm_func_get_and_use ("TINV");
	GnmFunc *fd_tdist = gnm_func_get_and_use ("TDIST");
	GnmFunc *fd_abs = gnm_func_get_and_use ("ABS");
	GnmFunc *fd_isodd = gnm_func_get_and_use ("ISODD");
	GnmFunc *fd_isnumber = gnm_func_get_and_use ("ISNUMBER");
	GnmFunc *fd_if = gnm_func_get_and_use ("IF");
	GnmFunc *fd_sum = gnm_func_get_and_use ("SUM");

	val_1 = value_dup (gtool->base.range_1);
	val_2 = value_dup (gtool->base.range_2);

	/* Labels */
	analysis_tools_write_variable_label (val_1, dao, 1, 0,
					  gtool->base.labels, 1);
	analysis_tools_write_variable_label (val_2, dao, 2, 0,
					  gtool->base.labels, 2);

	/* Mean */

	expr_1 = gnm_expr_new_constant (value_dup (val_1));
	dao_set_cell_expr (dao, 1, 1,
			   gnm_expr_new_funcall1 (fd_mean,
						  gnm_expr_copy (expr_1)));

	expr_2 = gnm_expr_new_constant (value_dup (val_2));
	dao_set_cell_expr (dao, 2, 1,
			   gnm_expr_new_funcall1 (fd_mean,
						  gnm_expr_copy (expr_2)));

	/* Variance */
	dao_set_cell_expr (dao, 1, 2,
			   gnm_expr_new_funcall1 (fd_var,
						  gnm_expr_copy (expr_1)));
	dao_set_cell_expr (dao, 2, 2,
			   gnm_expr_new_funcall1 (fd_var,
						  gnm_expr_copy (expr_2)));

	/* Observations */
	dao_set_cell_expr (dao, 1, 3,
			   gnm_expr_new_funcall1 (fd_count,
						  gnm_expr_copy (expr_1)));
	dao_set_cell_expr (dao, 2, 3,
			   gnm_expr_new_funcall1 (fd_count,
						  gnm_expr_copy (expr_2)));

	/* Pearson Correlation */
	dao_set_cell_expr (dao, 1, 4,
			   gnm_expr_new_funcall2 (fd_correl,
						  gnm_expr_copy (expr_1),
						  gnm_expr_copy (expr_2)));

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 5, ttool->mean_diff);

	/* Some useful expressions for the next field */

	expr_diff = gnm_expr_new_binary (expr_1, GNM_EXPR_OP_SUB, expr_2);

	/* IF (ISNUMBER (area1), 1, 0) * IF (ISNUMBER (area2), 1, 0)  */
	expr_ifisnumber = gnm_expr_new_binary (gnm_expr_new_funcall3 (
						       fd_if,
						       gnm_expr_new_funcall1 (
							       fd_isnumber,
							       gnm_expr_copy (expr_1)),
						       gnm_expr_new_constant (value_new_int (1)),
						       gnm_expr_new_constant (value_new_int (0))),
					       GNM_EXPR_OP_MULT,
					       gnm_expr_new_funcall3 (
						       fd_if,
						       gnm_expr_new_funcall1 (
							       fd_isnumber,
							       gnm_expr_copy (expr_2)),
						       gnm_expr_new_constant (value_new_int (1)),
						       gnm_expr_new_constant (value_new_int (0)))
		);
	/* IF (ISODD (expr_ifisnumber), area1-area2, "NA")*/
	expr_ifisoddifisnumber = gnm_expr_new_funcall3 (fd_if,
							gnm_expr_new_funcall1 (fd_isodd,
									       gnm_expr_copy (expr_ifisnumber)),
							expr_diff,
							gnm_expr_new_constant (value_new_string ("NA")));

	/* Observed Mean Difference */
	dao_set_cell_array_expr (dao, 1, 6,
				 gnm_expr_new_funcall1 (fd_mean,
							gnm_expr_copy (expr_ifisoddifisnumber)));

	/* Variance of the Differences */
	dao_set_cell_array_expr (dao, 1, 7,
				 gnm_expr_new_funcall1 (fd_var,
							expr_ifisoddifisnumber));

	/* df */
	dao_set_cell_array_expr (dao, 1, 8,
				 gnm_expr_new_binary
				 (gnm_expr_new_funcall1 (
					 fd_sum,
					 expr_ifisnumber),
				  GNM_EXPR_OP_SUB,
				  gnm_expr_new_constant (value_new_int (1))));

	/* t */
	/* E24 = (E21-E20)/(E22/(E23+1))^0.5 */
	{
		GnmExpr const *expr_num;
		GnmExpr const *expr_denom;

		expr_num = gnm_expr_new_binary (make_cellref (0, -3),
						GNM_EXPR_OP_SUB,
						make_cellref (0,-4));

		expr_denom = gnm_expr_new_binary
			(gnm_expr_new_binary
			 (make_cellref (0, -2),
			  GNM_EXPR_OP_DIV,
			  gnm_expr_new_binary
			  (make_cellref (0, -1),
			   GNM_EXPR_OP_ADD,
			   gnm_expr_new_constant
			   (value_new_int (1)))),
			 GNM_EXPR_OP_EXP,
			 gnm_expr_new_constant
			 (value_new_float (0.5)));

		dao_set_cell_expr (dao, 1, 9,
				   gnm_expr_new_binary
				   (expr_num, GNM_EXPR_OP_DIV, expr_denom));
	}

	/* P (T<=t) one-tail */
	dao_set_cell_expr
		(dao, 1, 10,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1
		  (fd_abs,
		   make_cellref (0, -1)),
		  make_cellref (0, -2),
		  gnm_expr_new_constant (value_new_int (1))));

	/* t Critical one-tail */
	dao_set_cell_expr
		(dao, 1, 11,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_binary
		  (gnm_expr_new_constant (value_new_int (2)),
		   GNM_EXPR_OP_MULT,
		   gnm_expr_new_constant
		   (value_new_float (gtool->base.alpha))),
		  make_cellref (0, -3)));

	/* P (T<=t) two-tail */
	dao_set_cell_expr
		(dao, 1, 12,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1 (fd_abs, make_cellref (0, -3)),
		  make_cellref (0, -4),
		  gnm_expr_new_constant (value_new_int (2))));

	/* t Critical two-tail */
	dao_set_cell_expr
		(dao, 1, 13,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_constant
		  (value_new_float (gtool->base.alpha)),
		  make_cellref (0, -5)));

	/* And finish up */

	value_release (val_1);
	value_release (val_2);

	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_correl);
	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_tinv);
	gnm_func_dec_usage (fd_tdist);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_isodd);
	gnm_func_dec_usage (fd_isnumber);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_sum);

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_ttest_paired_tool_class_init (GnmTTestPairedToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_ttest_paired_tool_update_dao;
	at_class->update_descriptor = gnm_ttest_paired_tool_update_descriptor;
	at_class->prepare_output_range = gnm_ttest_paired_tool_prepare_output_range;
	at_class->format_output_range = gnm_ttest_paired_tool_format_output_range;
	at_class->perform_calc = gnm_ttest_paired_tool_perform_calc;
}

GnmAnalysisTool *
gnm_ttest_paired_tool_new (void)
{
	return g_object_new (GNM_TYPE_TTEST_PAIRED_TOOL, NULL);
}

/********************************************************************/

/* t-Test: Two-Sample Assuming Equal Variances.
 */

G_DEFINE_TYPE (GnmTTestEqVarTool, gnm_ttest_eqvar_tool, GNM_TYPE_GENERIC_B_ANALYSIS_TOOL)

static void
gnm_ttest_eqvar_tool_init (GnmTTestEqVarTool *tool)
{
	tool->mean_diff = 0.0;
}

static gboolean
gnm_ttest_eqvar_tool_update_dao (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_adjust (dao, 3, 13);
	return FALSE;
}

static char *
gnm_ttest_eqvar_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("t-Test (%s)"));
}

static gboolean
gnm_ttest_eqvar_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("t-Test"));
	return FALSE;
}

static gboolean
gnm_ttest_eqvar_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("t-Test"));
}

static gboolean
gnm_ttest_eqvar_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmTTestEqVarTool *ttool = GNM_TTEST_EQVAR_TOOL (tool);
	GnmGenericBAnalysisTool *gtool = &ttool->parent;
	GnmValue *val_1;
	GnmValue *val_2;
	GnmExpr const *expr_1;
	GnmExpr const *expr_2;
	GnmExpr const *expr_mean_1;
	GnmExpr const *expr_mean_2;
	GnmExpr const *expr_var_1;
	GnmExpr const *expr_var_2;
	GnmExpr const *expr_count_1;
	GnmExpr const *expr_count_2;

	dao_set_italic (dao, 0, 0, 0, 12);
	dao_set_italic (dao, 0, 0, 2, 0);

        dao_set_cell (dao, 0, 0, "");
	set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/Pooled Variance"
					"/Hypothesized Mean Difference"
					"/Observed Mean Difference"
					"/df"
					"/t Stat"
					"/P (T<=t) one-tail"
					"/t Critical one-tail"
					"/P (T<=t) two-tail"
					"/t Critical two-tail"));


	val_1 = value_dup (gtool->base.range_1);
	val_2 = value_dup (gtool->base.range_2);

	GnmFunc *fd_mean = gnm_func_get_and_use ("AVERAGE");
	GnmFunc *fd_count = gnm_func_get_and_use ("COUNT");
	GnmFunc *fd_var = gnm_func_get_and_use ("VAR");
	GnmFunc *fd_tdist = gnm_func_get_and_use ("TDIST");
	GnmFunc *fd_abs = gnm_func_get_and_use ("ABS");
	GnmFunc *fd_tinv = gnm_func_get_and_use ("TINV");

	/* Labels */
	analysis_tools_write_variable_label (val_1, dao, 1, 0,
					  gtool->base.labels, 1);
	analysis_tools_write_variable_label (val_2, dao, 2, 0,
					  gtool->base.labels, 2);


	/* Mean */
	expr_1 = gnm_expr_new_constant (value_dup (val_1));
	expr_mean_1 = gnm_expr_new_funcall1 (fd_mean,
					     gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 1, expr_mean_1);
	expr_2 = gnm_expr_new_constant (value_dup (val_2));
	expr_mean_2 = gnm_expr_new_funcall1 (fd_mean,
					     gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 1, gnm_expr_copy (expr_mean_2));

	/* Variance */
	expr_var_1 = gnm_expr_new_funcall1 (fd_var, gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 2, expr_var_1);
	expr_var_2 = gnm_expr_new_funcall1 (fd_var, gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 2, gnm_expr_copy (expr_var_2));

	/* Observations */
	expr_count_1 = gnm_expr_new_funcall1 (fd_count, expr_1);
	dao_set_cell_expr (dao, 1, 3, expr_count_1);
	expr_count_2 = gnm_expr_new_funcall1 (fd_count, expr_2);
	dao_set_cell_expr (dao, 2, 3, gnm_expr_copy (expr_count_2));

        /* Pooled Variance */
	{
		GnmExpr const *expr_var_2_adj = NULL;
		GnmExpr const *expr_count_2_adj = NULL;
		GnmExpr const *expr_var_1 = make_cellref (0, -2);
		GnmExpr const *expr_count_1 = make_cellref (0, -1);
		GnmExpr const *expr_one = gnm_expr_new_constant
			(value_new_int (1));
		GnmExpr const *expr_count_1_minus_1;
		GnmExpr const *expr_count_2_minus_1;

		if (dao_cell_is_visible (dao, 2, 2)) {
			gnm_expr_free (expr_var_2);
			expr_var_2_adj = make_cellref (1, -2);
		} else
			expr_var_2_adj = expr_var_2;

		if (dao_cell_is_visible (dao, 2, 3)) {
			expr_count_2_adj = make_cellref (1, -1);
		} else
			expr_count_2_adj = gnm_expr_copy (expr_count_2);

		expr_count_1_minus_1 = gnm_expr_new_binary
			(expr_count_1,
			 GNM_EXPR_OP_SUB,
			 gnm_expr_copy (expr_one));
		expr_count_2_minus_1 = gnm_expr_new_binary
			(expr_count_2_adj, GNM_EXPR_OP_SUB, expr_one);

		dao_set_cell_expr (dao, 1, 4,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (gnm_expr_new_binary
				     (gnm_expr_copy (expr_count_1_minus_1),
				      GNM_EXPR_OP_MULT,
				      expr_var_1),
				     GNM_EXPR_OP_ADD,
				     gnm_expr_new_binary
				     (gnm_expr_copy (expr_count_2_minus_1),
				      GNM_EXPR_OP_MULT,
				      expr_var_2_adj)),
				    GNM_EXPR_OP_DIV,
				    gnm_expr_new_binary
				    (expr_count_1_minus_1,
				     GNM_EXPR_OP_ADD,
				     expr_count_2_minus_1)));

	}

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 5, ttool->mean_diff);

	/* Observed Mean Difference */
	if (dao_cell_is_visible (dao, 2,1)) {
		gnm_expr_free (expr_mean_2);
		expr_mean_2 = make_cellref (1, -5);
	}
	dao_set_cell_expr (dao, 1, 6,
			   gnm_expr_new_binary
			   (make_cellref (0, -5),
			    GNM_EXPR_OP_SUB,
			    expr_mean_2));

	/* df */
	{
		GnmExpr const *expr_count_1 = make_cellref (0, -4);
		GnmExpr const *expr_count_2_adj;
		GnmExpr const *expr_two = gnm_expr_new_constant
			(value_new_int (2));

		if (dao_cell_is_visible (dao, 2,3)) {
			expr_count_2_adj = make_cellref (1, -4);
		} else
			expr_count_2_adj = gnm_expr_copy (expr_count_2);

		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (expr_count_1,
				     GNM_EXPR_OP_ADD,
				     expr_count_2_adj),
				    GNM_EXPR_OP_SUB,
				    expr_two));
	}

	/* t */
	{
		GnmExpr const *expr_var = make_cellref (0, -4);
		GnmExpr const *expr_count_1 = make_cellref (0, -5);
		GnmExpr const *expr_a;
		GnmExpr const *expr_b;
		GnmExpr const *expr_count_2_adj;

		if (dao_cell_is_visible (dao, 2,3)) {
			gnm_expr_free (expr_count_2);
			expr_count_2_adj = make_cellref (1, -5);
		} else
			expr_count_2_adj = expr_count_2;

		expr_a = gnm_expr_new_binary (gnm_expr_copy (expr_var),
					      GNM_EXPR_OP_DIV,
					      expr_count_1);
		expr_b = gnm_expr_new_binary (expr_var,
					      GNM_EXPR_OP_DIV,
					      expr_count_2_adj);

		dao_set_cell_expr (dao, 1, 8,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (make_cellref (0, -2),
				     GNM_EXPR_OP_SUB,
				     make_cellref (0, -3)),
				    GNM_EXPR_OP_DIV,
				    gnm_expr_new_binary
					     (gnm_expr_new_binary
					      (expr_a,
					       GNM_EXPR_OP_ADD,
					       expr_b),
					      GNM_EXPR_OP_EXP,
					      gnm_expr_new_constant
					      (value_new_float (0.5)))));

	}

	/* P (T<=t) one-tail */
	dao_set_cell_expr
		(dao, 1, 9,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1
		  (fd_abs,
		   make_cellref (0, -1)),
		  make_cellref (0, -2),
		  gnm_expr_new_constant (value_new_int (1))));

	/* t Critical one-tail */
	dao_set_cell_expr
		(dao, 1, 10,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_binary
		  (gnm_expr_new_constant (value_new_int (2)),
		   GNM_EXPR_OP_MULT,
		   gnm_expr_new_constant
		   (value_new_float (gtool->base.alpha))),
		  make_cellref (0, -3)));

	/* P (T<=t) two-tail */
	dao_set_cell_expr
		(dao, 1, 11,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1
		  (fd_abs,
		   make_cellref (0, -3)),
		  make_cellref (0, -4),
		  gnm_expr_new_constant (value_new_int (2))));

	/* t Critical two-tail */
	dao_set_cell_expr
		(dao, 1, 12,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_constant
		  (value_new_float (gtool->base.alpha)),
		  make_cellref (0, -5)));

	/* And finish up */

	value_release (val_1);
	value_release (val_2);

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_tdist);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_tinv);

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_ttest_eqvar_tool_class_init (GnmTTestEqVarToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_ttest_eqvar_tool_update_dao;
	at_class->update_descriptor = gnm_ttest_eqvar_tool_update_descriptor;
	at_class->prepare_output_range = gnm_ttest_eqvar_tool_prepare_output_range;
	at_class->format_output_range = gnm_ttest_eqvar_tool_format_output_range;
	at_class->perform_calc = gnm_ttest_eqvar_tool_perform_calc;
}

GnmAnalysisTool *
gnm_ttest_eqvar_tool_new (void)
{
	return g_object_new (GNM_TYPE_TTEST_EQVAR_TOOL, NULL);
}

/********************************************************************/

/* t-Test: Two-Sample Assuming Unequal Variances.
 */

G_DEFINE_TYPE (GnmTTestNeqVarTool, gnm_ttest_neqvar_tool, GNM_TYPE_GENERIC_B_ANALYSIS_TOOL)

static void
gnm_ttest_neqvar_tool_init (GnmTTestNeqVarTool *tool)
{
	tool->mean_diff = 0.0;
}

static gboolean
gnm_ttest_neqvar_tool_update_dao (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_adjust (dao, 3, 12);
	return FALSE;
}

static char *
gnm_ttest_neqvar_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("t-Test (%s)"));
}

static gboolean
gnm_ttest_neqvar_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("t-Test"));
	return FALSE;
}

static gboolean
gnm_ttest_neqvar_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("t-Test"));
}

static gboolean
gnm_ttest_neqvar_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmTTestNeqVarTool *ttool = GNM_TTEST_NEQVAR_TOOL (tool);
	GnmGenericBAnalysisTool *gtool = &ttool->parent;
	GnmValue *val_1;
	GnmValue *val_2;
	GnmExpr const *expr_1;
	GnmExpr const *expr_2;
	GnmExpr const *expr_mean_1;
	GnmExpr const *expr_mean_2;
	GnmExpr const *expr_var_1;
	GnmExpr const *expr_var_2;
	GnmExpr const *expr_count_1;
	GnmExpr const *expr_count_2;

	dao_set_italic (dao, 0, 0, 0, 11);
	dao_set_italic (dao, 0, 0, 2, 0);

        dao_set_cell (dao, 0, 0, "");
        set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/Hypothesized Mean Difference"
					"/Observed Mean Difference"
					"/df"
					"/t Stat"
					"/P (T<=t) one-tail"
					"/t Critical one-tail"
					"/P (T<=t) two-tail"
					"/t Critical two-tail"));


	val_1 = value_dup (gtool->base.range_1);
	val_2 = value_dup (gtool->base.range_2);

	GnmFunc *fd_mean = gnm_func_get_and_use ("AVERAGE");
	GnmFunc *fd_var = gnm_func_get_and_use ("VAR");
	GnmFunc *fd_count = gnm_func_get_and_use ("COUNT");
	GnmFunc *fd_tdist = gnm_func_get_and_use ("TDIST");
	GnmFunc *fd_abs = gnm_func_get_and_use ("ABS");
	GnmFunc *fd_tinv = gnm_func_get_and_use ("TINV");

	/* Labels */
	analysis_tools_write_variable_label (val_1, dao, 1, 0,
					  gtool->base.labels, 1);
	analysis_tools_write_variable_label (val_2, dao, 2, 0,
					  gtool->base.labels, 2);


	/* Mean */
	expr_1 = gnm_expr_new_constant (value_dup (val_1));
	expr_mean_1 = gnm_expr_new_funcall1 (fd_mean, gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 1, expr_mean_1);
	expr_2 = gnm_expr_new_constant (value_dup (val_2));
	expr_mean_2 = gnm_expr_new_funcall1 (fd_mean, gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 1, gnm_expr_copy (expr_mean_2));

	/* Variance */
	expr_var_1 = gnm_expr_new_funcall1 (fd_var, gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 2, expr_var_1);
	expr_var_2 = gnm_expr_new_funcall1 (fd_var, gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 2, gnm_expr_copy (expr_var_2));

	/* Observations */
	expr_count_1 = gnm_expr_new_funcall1 (fd_count, expr_1);
	dao_set_cell_expr (dao, 1, 3, expr_count_1);
	expr_count_2 = gnm_expr_new_funcall1 (fd_count, expr_2);
	dao_set_cell_expr (dao, 2, 3, gnm_expr_copy (expr_count_2));

	/* Hypothesized Mean Difference */
	dao_set_cell_float (dao, 1, 4, ttool->mean_diff);

	/* Observed Mean Difference */
	if (dao_cell_is_visible (dao, 2,1)) {
		gnm_expr_free (expr_mean_2);
		expr_mean_2 = make_cellref (1, -4);
	}
	dao_set_cell_expr (dao, 1, 5,
			   gnm_expr_new_binary
			   (make_cellref (0, -4),
			    GNM_EXPR_OP_SUB,
			    expr_mean_2));

	/* df */

	{
		GnmExpr const *expr_var_1 = make_cellref (0, -4);
		GnmExpr const *expr_count_1 = make_cellref (0, -3);
		GnmExpr const *expr_a;
		GnmExpr const *expr_b;
		GnmExpr const *expr_var_2_adj;
		GnmExpr const *expr_count_2_adj;
		GnmExpr const *expr_two = gnm_expr_new_constant
			(value_new_int (2));
		GnmExpr const *expr_one = gnm_expr_new_constant
			(value_new_int (1));

		if (dao_cell_is_visible (dao, 2,2)) {
			expr_var_2_adj = make_cellref (1, -4);
		} else
			expr_var_2_adj = gnm_expr_copy (expr_var_2);

		if (dao_cell_is_visible (dao, 2,3)) {
			expr_count_2_adj = make_cellref (1, -3);
		} else
			expr_count_2_adj = gnm_expr_copy (expr_count_2);

		expr_a = gnm_expr_new_binary (expr_var_1,
					      GNM_EXPR_OP_DIV,
					      gnm_expr_copy (expr_count_1));
		expr_b = gnm_expr_new_binary (expr_var_2_adj,
					      GNM_EXPR_OP_DIV,
					      gnm_expr_copy (expr_count_2_adj));

		dao_set_cell_expr (dao, 1, 6,
				   gnm_expr_new_binary (
					   gnm_expr_new_binary
					   (gnm_expr_new_binary
					    (gnm_expr_copy (expr_a),
					     GNM_EXPR_OP_ADD,
					     gnm_expr_copy (expr_b)),
					    GNM_EXPR_OP_EXP,
					    gnm_expr_copy (expr_two)),
					   GNM_EXPR_OP_DIV,
					   gnm_expr_new_binary
					   (gnm_expr_new_binary
					    (gnm_expr_new_binary
					     (expr_a,
					      GNM_EXPR_OP_EXP,
					      gnm_expr_copy (expr_two)),
					     GNM_EXPR_OP_DIV,
					     gnm_expr_new_binary
					     (expr_count_1,
					      GNM_EXPR_OP_SUB,
					      gnm_expr_copy (expr_one))),
					    GNM_EXPR_OP_ADD,
					    gnm_expr_new_binary
					    (gnm_expr_new_binary
					     (expr_b,
					      GNM_EXPR_OP_EXP,
					      expr_two),
					     GNM_EXPR_OP_DIV,
					     gnm_expr_new_binary
					     (expr_count_2_adj,
					      GNM_EXPR_OP_SUB,
					      expr_one)))));
	}

	/* t */

	{
		GnmExpr const *expr_var_1 = make_cellref (0, -5);
		GnmExpr const *expr_count_1 = make_cellref (0, -4);
		GnmExpr const *expr_a;
		GnmExpr const *expr_b;
		GnmExpr const *expr_var_2_adj;
		GnmExpr const *expr_count_2_adj;

		if (dao_cell_is_visible (dao, 2,2)) {
			gnm_expr_free (expr_var_2);
			expr_var_2_adj = make_cellref (1, -5);
		} else
			expr_var_2_adj = expr_var_2;
		if (dao_cell_is_visible (dao, 2,3)) {
			gnm_expr_free (expr_count_2);
			expr_count_2_adj = make_cellref (1, -4);
		} else
			expr_count_2_adj = expr_count_2;

		expr_a = gnm_expr_new_binary (expr_var_1, GNM_EXPR_OP_DIV,
					      expr_count_1);
		expr_b = gnm_expr_new_binary (expr_var_2_adj, GNM_EXPR_OP_DIV,
					      expr_count_2_adj);

		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (make_cellref (0, -2),
				     GNM_EXPR_OP_SUB,
				     make_cellref (0, -3)),
				    GNM_EXPR_OP_DIV,
				    gnm_expr_new_binary
					     (gnm_expr_new_binary
					      (expr_a,
					       GNM_EXPR_OP_ADD,
					       expr_b),
					      GNM_EXPR_OP_EXP,
					      gnm_expr_new_constant
					      (value_new_float (0.5)))));

	}

	/* P (T<=t) one-tail */
	/* I9: =tdist(abs(Sheet1!I8),Sheet1!I7,1) */
	dao_set_cell_expr
		(dao, 1, 8,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1 (fd_abs,
					 make_cellref (0, -1)),
		  make_cellref (0, -2),
		  gnm_expr_new_constant (value_new_int (1))));

	/* t Critical one-tail */
        /* H10 = tinv(2*alpha,Sheet1!H7) */
	dao_set_cell_expr
		(dao, 1, 9,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_binary
		  (gnm_expr_new_constant (value_new_int (2)),
		   GNM_EXPR_OP_MULT,
		   gnm_expr_new_constant
		   (value_new_float (gtool->base.alpha))),
		  make_cellref (0, -3)));

	/* P (T<=t) two-tail */
	/* I11: =tdist(abs(Sheet1!I8),Sheet1!I7,1) */
	dao_set_cell_expr
		(dao, 1, 10,
		 gnm_expr_new_funcall3
		 (fd_tdist,
		  gnm_expr_new_funcall1 (fd_abs,
					 make_cellref (0, -3)),
		  make_cellref (0, -4),
		  gnm_expr_new_constant (value_new_int (2))));

	/* t Critical two-tail */
	dao_set_cell_expr
		(dao, 1, 11,
		 gnm_expr_new_funcall2
		 (fd_tinv,
		  gnm_expr_new_constant
		  (value_new_float (gtool->base.alpha)),
		  make_cellref (0, -5)));

	/* And finish up */

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_tdist);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_tinv);

	value_release (val_1);
	value_release (val_2);

	dao_redraw_respan (dao);
	return FALSE;
}

static void
gnm_ttest_neqvar_tool_class_init (GnmTTestNeqVarToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_ttest_neqvar_tool_update_dao;
	at_class->update_descriptor = gnm_ttest_neqvar_tool_update_descriptor;
	at_class->prepare_output_range = gnm_ttest_neqvar_tool_prepare_output_range;
	at_class->format_output_range = gnm_ttest_neqvar_tool_format_output_range;
	at_class->perform_calc = gnm_ttest_neqvar_tool_perform_calc;
}

GnmAnalysisTool *
gnm_ttest_neqvar_tool_new (void)
{
	return g_object_new (GNM_TYPE_TTEST_NEQVAR_TOOL, NULL);
}

/********************************************************************/

