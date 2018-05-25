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
#include <tools/dao.h>
#include <tools/dao.h>
#include <sheet.h>
#include <func.h>
#include <numbers.h>

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

		fd_cholesky = gnm_func_lookup_or_add_placeholder ("CHOLESKY");
		gnm_func_inc_usage (fd_cholesky);
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

	fd_rand = gnm_func_lookup_or_add_placeholder ("RANDNORM");
	gnm_func_inc_usage (fd_rand);
	expr_rand = gnm_expr_new_funcall2 (fd_rand,
					   gnm_expr_new_constant (value_new_int (0)),
					   gnm_expr_new_constant (value_new_int (1)));
	for (i = 0; i < info->variables; i++)
		for (j = 1; j <= info->count; j++)
			dao_set_cell_expr (dao, i, j, gnm_expr_copy (expr_rand));
	gnm_expr_free (expr_rand);
	gnm_func_dec_usage (fd_rand);

	dao->offset_col += info->variables + 1;

	fd_mmult = gnm_func_lookup_or_add_placeholder ("MMULT");
	gnm_func_inc_usage (fd_mmult);
	fd_transpose = gnm_func_lookup_or_add_placeholder ("TRANSPOSE");
	gnm_func_inc_usage (fd_transpose);

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

static gboolean
tool_random_cor_clean (gpointer specs)
{
	tools_data_random_cor_t *info = specs;

	value_release (info->matrix);
	info->matrix = NULL;

	return FALSE;
}


gboolean
tool_random_cor_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	tools_data_random_cor_t *data = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor
			(dao,_("Correlated Random Numbers (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		dao_adjust (dao, 2 * data->variables + 1,
			    data->variables + data->count + 3 );
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return tool_random_cor_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Correlated Random Numbers"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Correlated Random Numbers"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return tool_random_cor_engine_run (dao, specs);
	}
	return TRUE;
}




