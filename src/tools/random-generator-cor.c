/*
 * random-generator-cor.c:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2009 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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
#include <tools/random-generator-cor.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <sheet.h>
#include <func.h>
#include <numbers.h>

static gboolean tool_random_cor_engine_run (data_analysis_output_t *dao, tools_data_random_cor_t *info);
G_DEFINE_TYPE (GnmRandomCorTool, gnm_random_cor_tool, GNM_TYPE_ANALYSIS_TOOL)

static void
gnm_random_cor_tool_init (GnmRandomCorTool *tool)
{
	tool->data.wbc = NULL;
	tool->data.matrix = NULL;
	tool->data.matrix_type = random_gen_cor_type_cov;
	tool->data.count = 1;
	tool->data.variables = 1;
}

static void
gnm_random_cor_tool_finalize (GObject *obj)
{
	GnmRandomCorTool *tool = GNM_RANDOM_COR_TOOL (obj);
	if (tool->data.matrix)
		value_release (tool->data.matrix);
	G_OBJECT_CLASS (gnm_random_cor_tool_parent_class)->finalize (obj);
}

static gboolean
gnm_random_cor_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	tools_data_random_cor_t *info = &GNM_RANDOM_COR_TOOL (tool)->data;
	gint rows = 1 + info->count;
	gint cols = 2 * info->variables + 1;

	if (info->matrix_type == random_gen_cor_type_cov)
		rows += info->variables + 2;

	dao_adjust (dao, cols, rows);
	return FALSE;
}

static char *
gnm_random_cor_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Correlated Random Numbers (%s)"));
}

static gboolean
gnm_random_cor_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Correlated Random Numbers"));
	return FALSE;
}

static gboolean
gnm_random_cor_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Correlated Random Numbers"));
}

static gboolean
gnm_random_cor_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	tools_data_random_cor_t *info = &GNM_RANDOM_COR_TOOL (tool)->data;
	return tool_random_cor_engine_run (dao, info);
}

static void
gnm_random_cor_tool_class_init (GnmRandomCorToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->finalize = gnm_random_cor_tool_finalize;
	at_class->update_dao = gnm_random_cor_tool_update_dao;
	at_class->update_descriptor = gnm_random_cor_tool_update_descriptor;
	at_class->prepare_output_range = gnm_random_cor_tool_prepare_output_range;
	at_class->format_output_range = gnm_random_cor_tool_format_output_range;
	at_class->perform_calc = gnm_random_cor_tool_perform_calc;
}

GnmAnalysisTool *
gnm_random_cor_tool_new (void)
{
	return g_object_new (GNM_TYPE_RANDOM_COR_TOOL, NULL);
}

static gboolean
tool_random_cor_engine_run (data_analysis_output_t *dao,
			    tools_data_random_cor_t *info)
{
	GnmExpr const *expr_matrix = gnm_expr_new_constant (value_dup (info->matrix));
	GnmExpr const *expr_rand;
	GnmFunc *fd_rand;
	GnmFunc *fd_mmult;
	GnmFunc *fd_transpose;

	gint i, j;

	if (info->matrix_type == random_gen_cor_type_cov) {
		GnmFunc *fd_cholesky;
		GnmExpr const *expr_cholesky;

		fd_cholesky = gnm_func_get_and_use ("CHOLESKY");
		expr_cholesky = gnm_expr_new_funcall1
			(fd_cholesky, expr_matrix);

		dao_set_merge (dao, 0, 0, 2 * info->variables, 0);
		dao_set_italic (dao, 0, 0, 0, 0);
		dao_set_cell (dao, 0, 0, _("Cholesky Decomposition of the Covariance Matrix"));
		dao_set_array_expr (dao, 0, 1, info->variables, info->variables,
				    expr_cholesky);

		gnm_func_dec_usage (fd_cholesky);

		expr_matrix = dao_get_rangeref (dao, 0, 1, info->variables - 1, info->variables);
		dao->offset_row += info->variables + 2;
	}

	dao_set_merge (dao, 0, 0, info->variables - 1, 0);
	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Uncorrelated Random Variables"));

	fd_rand = gnm_func_get_and_use ("RANDNORM");
	expr_rand = gnm_expr_new_funcall2 (fd_rand,
					   gnm_expr_new_constant (value_new_int (0)),
					   gnm_expr_new_constant (value_new_int (1)));
	for (i = 0; i < info->variables; i++)
		for (j = 1; j <= info->count; j++)
			dao_set_cell_expr (dao, i, j, gnm_expr_copy (expr_rand));
	gnm_expr_free (expr_rand);
	gnm_func_dec_usage (fd_rand);

	dao->offset_col += info->variables + 1;

	fd_mmult = gnm_func_get_and_use ("MMULT");
	fd_transpose = gnm_func_get_and_use ("TRANSPOSE");

	dao_set_merge (dao, 0, 0, info->variables - 1, 0);
	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Correlated Random Variables"));

	expr_rand = gnm_expr_new_funcall2 (fd_mmult,
					   make_rangeref (-4, 0, -2, 0),
					   gnm_expr_new_funcall1
					   (fd_transpose,
					    expr_matrix));

	for (j = 1; j <= info->count; j++)
		dao_set_array_expr (dao, 0, j, info->variables, 1,
				    gnm_expr_copy (expr_rand));

	gnm_expr_free (expr_rand);

	gnm_func_dec_usage (fd_mmult);
	gnm_func_dec_usage (fd_transpose);
	dao_redraw_respan (dao);

	return FALSE;
}









