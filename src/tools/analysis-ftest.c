/*
 * analysis-ftest.c:
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
#include <tools/analysis-ftest.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>
#include <sheet-object-graph.h>
#include <goffice/goffice.h>
#include <sheet.h>

/************* F-Test Tool *********************************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 * F-Test: Two-Sample for Variances
 */


G_DEFINE_TYPE (GnmFTestTool, gnm_ftest_tool, GNM_TYPE_GENERIC_B_ANALYSIS_TOOL)

static void
gnm_ftest_tool_init (G_GNUC_UNUSED GnmFTestTool *tool)
{
}

static gboolean
gnm_ftest_tool_update_dao (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_adjust (dao, 2, 10);
	return FALSE;
}

static char *
gnm_ftest_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("F-Test (%s)"));
}

static gboolean
gnm_ftest_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("F-Test"));
	return FALSE;
}

static gboolean
gnm_ftest_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("F-Test"));
}

static gboolean
gnm_ftest_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmFTestTool *ftool = GNM_FTEST_TOOL (tool);
	GnmGenericBAnalysisTool *gtool = &ftool->parent;
	GnmValue *val_1 = value_dup (gtool->base.range_1);
	GnmValue *val_2 = value_dup (gtool->base.range_2);
	GnmExpr const *expr;
	GnmExpr const *expr_var_denum;
	GnmExpr const *expr_count_denum;
	GnmExpr const *expr_df_denum = NULL;
	GnmFunc *fd_finv = gnm_func_get_and_use ("FINV");

	dao_set_italic (dao, 0, 0, 0, 11);
	dao_set_cell (dao, 0, 0, _("F-Test"));
	set_cell_text_col (dao, 0, 1, _("/Mean"
					"/Variance"
					"/Observations"
					"/df"
					"/F"
					"/P (F<=f) right-tail"
					"/F Critical right-tail"
					"/P (f<=F) left-tail"
					"/F Critical left-tail"
					"/P two-tail"
					"/F Critical two-tail"));

	/* Label */
	dao_set_italic (dao, 0, 0, 2, 0);
	analysis_tools_write_variable_label (val_1, dao, 1, 0, gtool->base.labels, 1);
	analysis_tools_write_variable_label (val_2, dao, 2, 0, gtool->base.labels, 2);

	/* Mean */
	{
		GnmFunc *fd_mean = gnm_func_get_and_use ("AVERAGE");

		dao_set_cell_expr
			(dao, 1, 1,
			 gnm_expr_new_funcall1
			 (fd_mean,
			  gnm_expr_new_constant (value_dup (val_1))));

		dao_set_cell_expr
			(dao, 2, 1,
			 gnm_expr_new_funcall1
			 (fd_mean,
			  gnm_expr_new_constant (value_dup (val_2))));

		gnm_func_dec_usage (fd_mean);
	}

	/* Variance */
	{
		GnmFunc *fd_var = gnm_func_get_and_use ("VAR");

		dao_set_cell_expr
			(dao, 1, 2,
			 gnm_expr_new_funcall1
			 (fd_var,
			  gnm_expr_new_constant (value_dup (val_1))));

		expr_var_denum = gnm_expr_new_funcall1
			(fd_var,
			 gnm_expr_new_constant (value_dup (val_2)));
		dao_set_cell_expr (dao, 2, 2, gnm_expr_copy (expr_var_denum));

		gnm_func_dec_usage (fd_var);
	}

        /* Count */
	{
		GnmFunc *fd_count = gnm_func_get_and_use ("COUNT");

		dao_set_cell_expr
			(dao, 1, 3,
			 gnm_expr_new_funcall1
			 (fd_count,
			  gnm_expr_new_constant (value_dup (val_1))));

		expr_count_denum = gnm_expr_new_funcall1
			(fd_count,
			 gnm_expr_new_constant (value_dup (val_2)));
		dao_set_cell_expr (dao, 2, 3, gnm_expr_copy (expr_count_denum));

		gnm_func_dec_usage (fd_count);
	}

	/* df */
	{
		expr = gnm_expr_new_binary
			(make_cellref (0, -1),
			 GNM_EXPR_OP_SUB,
			 gnm_expr_new_constant (value_new_int (1)));
		dao_set_cell_expr (dao, 1, 4, gnm_expr_copy (expr));
		dao_set_cell_expr (dao, 2, 4, expr);
	}

	/* F value */
	if (dao_cell_is_visible (dao, 2, 2)) {
		expr = gnm_expr_new_binary
			(make_cellref (0, -3),
			 GNM_EXPR_OP_DIV,
			 make_cellref (1, -3));
		gnm_expr_free (expr_var_denum);
	} else {
		expr = gnm_expr_new_binary
			(make_cellref (0, -3),
			 GNM_EXPR_OP_DIV,
			 expr_var_denum);
	}
	dao_set_cell_expr (dao, 1, 5, expr);

	/* P right-tail */
	{
		GnmFunc *fd_fdist = gnm_func_get_and_use ("FDIST");
		const GnmExpr *arg3;

		if (dao_cell_is_visible (dao, 2, 2)) {
			arg3 = make_cellref (1, -2);
			gnm_expr_free (expr_count_denum);
		} else {
			expr_df_denum = gnm_expr_new_binary
				(expr_count_denum,
				 GNM_EXPR_OP_SUB,
				 gnm_expr_new_constant (value_new_int (1)));
			arg3 = gnm_expr_copy (expr_df_denum);
		}

		dao_set_cell_expr
			(dao, 1, 6,
			 gnm_expr_new_funcall3
			 (fd_fdist,
			  make_cellref (0, -1),
			  make_cellref (0, -2),
			  arg3));

		gnm_func_dec_usage (fd_fdist);
	}

	/* F critical right-tail */
	{
		const GnmExpr *arg3;

		if (expr_df_denum == NULL) {
			arg3 = make_cellref (1, -3);
		} else {
			arg3 = gnm_expr_copy (expr_df_denum);
		}

		dao_set_cell_expr
			(dao, 1, 7,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant (value_new_float (gtool->base.alpha)),
			  make_cellref (0, -3),
			  arg3));
	}

	/* P left-tail */
	dao_set_cell_expr (dao, 1, 8,
			   gnm_expr_new_binary
			   (gnm_expr_new_constant (value_new_int (1)),
			    GNM_EXPR_OP_SUB,
			    make_cellref (0, -2)));

	/* F critical left-tail */
	{
		const GnmExpr *arg3;

		if (expr_df_denum == NULL) {
			arg3 = make_cellref (1, -5);
		} else {
			arg3 = gnm_expr_copy (expr_df_denum);
		}

		dao_set_cell_expr
			(dao, 1, 9,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant
			  (value_new_float (1 - gtool->base.alpha)),
			  make_cellref (0, -5),
			  arg3));
	}

	/* P two-tail */
	{
		GnmFunc *fd_min = gnm_func_get_and_use ("MIN");

		dao_set_cell_expr
			(dao, 1, 10,
			 gnm_expr_new_binary
			 (gnm_expr_new_constant (value_new_int (2)),
			  GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall2
			  (fd_min,
			   make_cellref (0, -4),
			   make_cellref (0, -2))));
		gnm_func_dec_usage (fd_min);
	}

	/* F critical two-tail (left) */
	{
		const GnmExpr *arg3;

		if (expr_df_denum == NULL) {
			arg3 = make_cellref (1, -7);
		} else {
			arg3 = expr_df_denum;
		}

		dao_set_cell_expr
			(dao, 1, 11,
			 gnm_expr_new_funcall3
			 (fd_finv,
			  gnm_expr_new_constant
			  (value_new_float (1 - gtool->base.alpha / 2)),
			  make_cellref (0, -7),
			  arg3));
	}

	/* F critical two-tail (right) */
	dao_set_cell_expr
		(dao, 2, 11,
		 gnm_expr_new_funcall3
		 (fd_finv,
		  gnm_expr_new_constant
		  (value_new_float (gtool->base.alpha / 2)),
		  make_cellref (-1, -7),
		  make_cellref (0, -7)));

	value_release (val_1);
	value_release (val_2);

	gnm_func_dec_usage (fd_finv);

	dao_redraw_respan (dao);
	return FALSE;
}

static void
gnm_ftest_tool_class_init (GnmFTestToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_ftest_tool_update_dao;
	at_class->update_descriptor = gnm_ftest_tool_update_descriptor;
	at_class->prepare_output_range = gnm_ftest_tool_prepare_output_range;
	at_class->format_output_range = gnm_ftest_tool_format_output_range;
	at_class->perform_calc = gnm_ftest_tool_perform_calc;
}

GnmAnalysisTool *
gnm_ftest_tool_new (void)
{
	return g_object_new (GNM_TYPE_FTEST_TOOL, NULL);
}

/********************************************************************/
