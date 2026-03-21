/*
 * analysis-wilcoxon-mann-whitney.c:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2010 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
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
#include <tools/analysis-wilcoxon-mann-whitney.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>
#include <sheet.h>

static GnmExpr const *
analysis_tool_combine_area (GnmValue *val_1, GnmValue *val_2)
{
	GnmExpr const *expr;
	GnmFunc *fd_array = gnm_func_get_and_use ("ARRAY");

	if (VALUE_IS_CELLRANGE (val_1) && VALUE_IS_CELLRANGE (val_2) &&
	    val_1->v_range.cell.a.sheet == val_2->v_range.cell.a.sheet) {
		GnmRange r_1, r_2;
		gboolean combined = FALSE;

		range_init_rangeref (&r_1, &val_1->v_range.cell);
		range_init_rangeref (&r_2, &val_2->v_range.cell);

		if (r_1.start.row == r_2.start.row &&
		    range_height (&r_1) == range_height (&r_2)) {
			if (r_1.end.col == r_2.start.col - 1) {
				combined = TRUE;
				r_1.end.col = r_2.end.col;
			} else if (r_2.end.col == r_1.start.col - 1) {
				combined = TRUE;
				r_1.start.col = r_2.start.col;
			}
		} else if (r_1.start.col == r_2.start.col &&
			   range_width (&r_1) == range_width (&r_2)) {
			if (r_1.end.row == r_2.start.row - 1) {
				combined = TRUE;
				r_1.end.row = r_2.end.row;
			} else if (r_2.end.row == r_1.start.row - 1) {
				combined = TRUE;
				r_1.start.row = r_2.start.row;
			}
		}

		if (combined) {
			GnmValue *val = value_new_cellrange_r (val_1->v_range.cell.a.sheet, &r_1);
			return gnm_expr_new_constant (val);
		}
	}

	expr = gnm_expr_new_funcall2 (fd_array,
				      gnm_expr_new_constant (value_dup (val_1)),
				      gnm_expr_new_constant (value_dup (val_2)));

	gnm_func_dec_usage (fd_array);

	return expr;
}


G_DEFINE_TYPE (GnmWilcoxonMannWhitneyTool, gnm_wilcoxon_mann_whitney_tool, GNM_TYPE_GENERIC_B_ANALYSIS_TOOL)

static void
gnm_wilcoxon_mann_whitney_tool_init (G_GNUC_UNUSED GnmWilcoxonMannWhitneyTool *tool)
{
}

static gboolean
gnm_wilcoxon_mann_whitney_tool_update_dao (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_adjust (dao, 4, 9);
	return FALSE;
}

static char *
gnm_wilcoxon_mann_whitney_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Wilcoxon-Mann-Whitney Test (%s)"));
}

static gboolean
gnm_wilcoxon_mann_whitney_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Wilcoxon-Mann-Whitney Test"));
	return FALSE;
}

static gboolean
gnm_wilcoxon_mann_whitney_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Wilcoxon-Mann-Whitney Test"));
}

static gboolean
gnm_wilcoxon_mann_whitney_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmWilcoxonMannWhitneyTool *wtool = GNM_WILCOXON_MANN_WHITNEY_TOOL (tool);
	GnmGenericBAnalysisTool *gtool = &wtool->parent;
	GnmExpr const *expr_total;
	GnmExpr const *expr_pop_1;
	GnmExpr const *expr_pop_2;
	GnmExpr const *expr_u;
	GnmExpr const *expr_count_total;
	GnmValue *val_1 = value_dup (gtool->base.range_1);
	GnmValue *val_2 = value_dup (gtool->base.range_2);
	GnmFunc *fd_count = gnm_func_get_and_use ("COUNT");
	GnmFunc *fd_sum = gnm_func_get_and_use ("SUM");
	GnmFunc *fd_rows = gnm_func_get_and_use ("ROWS");
	GnmFunc *fd_rank_avg = gnm_func_get_and_use ("RANK.AVG");
	GnmFunc *fd_rank = gnm_func_get_and_use ("RANK");
	GnmFunc *fd_min = gnm_func_get_and_use ("MIN");
	GnmFunc *fd_normdist = gnm_func_get_and_use ("NORMDIST");
	GnmFunc *fd_sqrt = gnm_func_get_and_use ("SQRT");
	GnmFunc *fd_if = gnm_func_get_and_use ("IF");
	GnmFunc *fd_isblank = gnm_func_get_and_use ("ISBLANK");

	dao_set_italic (dao, 0, 0, 0, 8);
	dao_set_italic (dao, 0, 1, 3, 1);
	dao_set_merge (dao, 0, 0, 3, 0);
	dao_set_cell (dao, 0, 0, _("Wilcoxon-Mann-Whitney Test"));
	set_cell_text_col (dao, 0, 2, _("/Rank-Sum"
					"/N"
					"/U"
					"/Ties"
					"/Statistic"
					"/U-Statistic"
					"/p-Value"));
	dao_set_cell (dao, 3, 1, _("Total"));

	/* Label */
	analysis_tools_write_variable_label (val_1, dao, 1, 1, gtool->base.labels, 1);
	analysis_tools_write_variable_label (val_2, dao, 2, 1, gtool->base.labels, 2);

	expr_total = analysis_tool_combine_area (val_1, val_2);
	expr_pop_1 = gnm_expr_new_constant (val_1);
	expr_pop_2 = gnm_expr_new_constant (val_2);

	/* =sum(if(isblank(region1),0,rank.avg(region1,combined_regions,1))) */

	dao_set_cell_array_expr (dao, 1, 2,
				 gnm_expr_new_funcall1
				 (fd_sum,
				  gnm_expr_new_funcall3
				  (fd_if,
				   gnm_expr_new_funcall1
				  (fd_isblank,
				   gnm_expr_copy (expr_pop_1)),
				   gnm_expr_new_constant (value_new_int (0)),
				   gnm_expr_new_funcall3
				   (fd_rank_avg,
				    gnm_expr_copy (expr_pop_1),
				    gnm_expr_copy (expr_total),
				    gnm_expr_new_constant (value_new_int (1))))));
	dao_set_cell_array_expr (dao, 2, 2,
				 gnm_expr_new_funcall1
				 (fd_sum,
				  gnm_expr_new_funcall3
				  (fd_if,
				   gnm_expr_new_funcall1
				  (fd_isblank,
				   gnm_expr_copy (expr_pop_2)),
				   gnm_expr_new_constant (value_new_int (0)),
				   gnm_expr_new_funcall3
				   (fd_rank_avg,
				    gnm_expr_copy (expr_pop_2),
				    gnm_expr_copy (expr_total),
				    gnm_expr_new_constant (value_new_int (1))))));

	expr_count_total = gnm_expr_new_funcall1
		(fd_count, gnm_expr_copy (expr_total));
	dao_set_cell_expr (dao, 3, 2,
			   gnm_expr_new_binary
			   (gnm_expr_new_binary
			    (gnm_expr_copy (expr_count_total),
			     GNM_EXPR_OP_MULT,
			     gnm_expr_new_binary
			     (gnm_expr_copy (expr_count_total),
			      GNM_EXPR_OP_ADD,
			      gnm_expr_new_constant (value_new_int (1)))),
			    GNM_EXPR_OP_DIV,
			    gnm_expr_new_constant (value_new_int (2))));

	dao_set_cell_expr (dao, 1, 3,
			   gnm_expr_new_funcall1
			   (fd_count,
			    expr_pop_1));
	dao_set_cell_expr (dao, 2, 3,
			   gnm_expr_new_funcall1
			   (fd_count,
			    expr_pop_2));
	dao_set_cell_expr (dao, 3, 3,
			   gnm_expr_new_funcall1
			   (fd_count,
			    gnm_expr_copy (expr_total)));

	expr_u = gnm_expr_new_binary
		(make_cellref (0,- 2), GNM_EXPR_OP_SUB,
		 gnm_expr_new_binary
		 (gnm_expr_new_binary
		  (make_cellref (0,- 1),
		   GNM_EXPR_OP_MULT,
		   gnm_expr_new_binary
		   (make_cellref (0,- 1),
		    GNM_EXPR_OP_ADD,
		    gnm_expr_new_constant (value_new_int (1)))),
		  GNM_EXPR_OP_DIV,
		  gnm_expr_new_constant (value_new_int (2))));

	dao_set_cell_expr (dao, 1, 4, gnm_expr_copy (expr_u));
	dao_set_cell_expr (dao, 2, 4, expr_u);
	dao_set_cell_expr (dao, 3, 4,
			   gnm_expr_new_binary
			   (make_cellref (-2,-1),
			    GNM_EXPR_OP_MULT,
			    make_cellref (-1,-1)));

	dao_set_cell_array_expr (dao, 1, 5,
				 gnm_expr_new_funcall1
				 (fd_sum,
				  gnm_expr_new_binary
				  (gnm_expr_new_funcall2
				   (fd_rank_avg,
				    gnm_expr_copy (expr_total),
				    gnm_expr_copy (expr_total)),
				   GNM_EXPR_OP_SUB,
				   gnm_expr_new_funcall2
				   (fd_rank,
				    gnm_expr_copy (expr_total),
				    gnm_expr_copy (expr_total)))));

	if (dao_cell_is_visible (dao, 2, 4)) {
		GnmExpr const *expr_prod;
		GnmExpr const *expr_sqrt;
		GnmExpr const *expr_normdist;

		expr_prod = gnm_expr_new_binary
			(make_cellref (0,-5),
			 GNM_EXPR_OP_MULT,
			 make_cellref (1,-5));
		expr_sqrt = gnm_expr_new_funcall1
			(fd_sqrt,
			 gnm_expr_new_binary
			 (gnm_expr_new_binary
			  (gnm_expr_copy (expr_prod),
			   GNM_EXPR_OP_MULT,
			   gnm_expr_new_binary
			   (gnm_expr_new_binary
			    (make_cellref (0,-5),
			     GNM_EXPR_OP_ADD,
			     make_cellref (1,-5)),
			    GNM_EXPR_OP_ADD,
			    gnm_expr_new_constant (value_new_int (1)))),
			  GNM_EXPR_OP_DIV,
			  gnm_expr_new_constant (value_new_int (12))));
		expr_normdist = gnm_expr_new_funcall4
			(fd_normdist,
			 make_cellref (0,-1),
			 gnm_expr_new_binary
			 (expr_prod,
			  GNM_EXPR_OP_DIV,
			  gnm_expr_new_constant (value_new_int (2))),
			 expr_sqrt,
			 gnm_expr_new_constant (value_new_bool (TRUE)));

		dao_set_cell_expr (dao, 1, 6,
				   gnm_expr_new_funcall2
				   (fd_min,
				    make_cellref (0,-4),
				    make_cellref (1,-4)));
		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_funcall2
				   (fd_min,
				    make_cellref (0,-3),
				    make_cellref (1,-3)));

		dao_set_cell_expr (dao, 1, 8,
				   gnm_expr_new_binary
				   (gnm_expr_new_constant (value_new_int (2)),
				    GNM_EXPR_OP_MULT,
				    expr_normdist));
		dao_set_cell_comment (dao, 1, 8,
				      _("This p-value is calculated using a\n"
					"normal approximation, so it is\n"
					"only valid for large samples of\n"
					"at least 15 observations in each\n"
					"population, and few if any ties."));
	} else {
		dao_set_cell_na (dao, 1, 6);
		dao_set_cell_comment (dao, 1, 6,
				      _("Since there is insufficient space\n"
					"for the third column of output,\n"
					"this value is not calculated."));
		dao_set_cell_na (dao, 1, 7);
		dao_set_cell_comment (dao, 1, 7,
				      _("Since there is insufficient space\n"
					"for the third column of output,\n"
					"this value is not calculated."));
		dao_set_cell_na (dao, 1, 8);
		dao_set_cell_comment (dao, 1, 8,
				      _("Since there is insufficient space\n"
					"for the third column of output,\n"
					"this value is not calculated."));
	}


	gnm_expr_free (expr_count_total);

	gnm_expr_free (expr_total);

	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_rows);
	gnm_func_dec_usage (fd_rank_avg);
	gnm_func_dec_usage (fd_rank);
	gnm_func_dec_usage (fd_min);
	gnm_func_dec_usage (fd_normdist);
	gnm_func_dec_usage (fd_sqrt);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_isblank);

	dao_redraw_respan (dao);
	return 0;
}

static void
gnm_wilcoxon_mann_whitney_tool_class_init (GnmWilcoxonMannWhitneyToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_wilcoxon_mann_whitney_tool_update_dao;
	at_class->update_descriptor = gnm_wilcoxon_mann_whitney_tool_update_descriptor;
	at_class->prepare_output_range = gnm_wilcoxon_mann_whitney_tool_prepare_output_range;
	at_class->format_output_range = gnm_wilcoxon_mann_whitney_tool_format_output_range;
	at_class->perform_calc = gnm_wilcoxon_mann_whitney_tool_perform_calc;
}

GnmAnalysisTool *
gnm_wilcoxon_mann_whitney_tool_new (void)
{
	return g_object_new (GNM_TYPE_WILCOXON_MANN_WHITNEY_TOOL, NULL);
}
