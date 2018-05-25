/*
 * analysis-one-mean-test.c:
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
#include <tools/analysis-one-mean-test.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>

static gboolean
analysis_tool_one_mean_test_engine_run (data_analysis_output_t *dao,
				      analysis_tools_data_one_mean_test_t *info)
{
	guint    col;
	GSList  *data = info->base.input;
	gboolean first = TRUE;

	GnmFunc *fd_mean;
	GnmFunc *fd_var;
	GnmFunc *fd_sqrt;
	GnmFunc *fd_abs;
	GnmFunc *fd_tdist;
	GnmFunc *fd_iferror;
	GnmFunc *fd_count;

	fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
	gnm_func_inc_usage (fd_count);
	fd_mean = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_mean);
	fd_var = gnm_func_lookup_or_add_placeholder ("VAR");
	gnm_func_inc_usage (fd_var);
	fd_sqrt = gnm_func_lookup_or_add_placeholder ("SQRT");
	gnm_func_inc_usage (fd_sqrt);
	fd_abs = gnm_func_lookup_or_add_placeholder ("ABS");
	gnm_func_inc_usage (fd_abs);
	fd_tdist = gnm_func_lookup_or_add_placeholder ("TDIST");
	gnm_func_inc_usage (fd_tdist);
	fd_iferror = gnm_func_lookup_or_add_placeholder ("IFERROR");
	gnm_func_inc_usage (fd_iferror);

	dao_set_italic (dao, 0, 0, 0, 9);
	set_cell_text_col (dao, 0, 0, _("/Student-t Test"
					"/N"
					"/Observed Mean"
					"/Hypothesized Mean"
					"/Observed Variance"
					"/Test Statistic"
					"/df"
					"/\xce\xb1"
					"/P(T\xe2\x89\xa4t) one-tailed"
					"/P(T\xe2\x89\xa4t) two-tailed"));

	for (col = 1; data != NULL; data = data->next, col++) {
		GnmValue *val_org = value_dup (data->data);
		GnmExpr const *expr;
		GnmExpr const *expr_org;
		GnmExpr const *expr_range_clean;
		GnmExpr const *expr_stddev;
		GnmExpr const *expr_abs;

		/* Note that analysis_tools_write_label may modify val_org */
		dao_set_italic (dao, col, 0, col, 0);
		analysis_tools_write_label (val_org, dao, &info->base, col, 0, col);
		expr_org = gnm_expr_new_constant (val_org);
		expr_range_clean = gnm_expr_new_funcall2
			(fd_iferror, gnm_expr_copy (expr_org), gnm_expr_new_constant (value_new_string("")));

		if (first) {
			dao_set_cell_float (dao, col, 3, info->mean);
			dao_set_cell_float (dao, col, 7, info->alpha);
			first = FALSE;
		} else {
			dao_set_cell_expr (dao, col, 3, make_cellref (-1,0));
			dao_set_cell_expr (dao, col, 7, make_cellref (-1,0));
		}

		expr = gnm_expr_new_funcall1 (fd_count, expr_org);
		dao_set_cell_expr (dao, col, 1, expr);

		expr = gnm_expr_new_funcall1 (fd_mean, gnm_expr_copy (expr_range_clean));
		dao_set_cell_array_expr (dao, col, 2, expr);

		expr = gnm_expr_new_funcall1 (fd_var, expr_range_clean);
		dao_set_cell_array_expr (dao, col, 4, expr);

		dao_set_cell_expr (dao, col, 6,  gnm_expr_new_binary
				   (make_cellref (0,-5), GNM_EXPR_OP_SUB, gnm_expr_new_constant (value_new_int (1))));

		expr_stddev = gnm_expr_new_funcall1
			(fd_sqrt, gnm_expr_new_binary (make_cellref (0,-1), GNM_EXPR_OP_DIV, make_cellref (0,-4)));
		expr = gnm_expr_new_binary
			(gnm_expr_new_binary (make_cellref (0,-3), GNM_EXPR_OP_SUB, make_cellref (0,-2)),
			 GNM_EXPR_OP_DIV,
			 expr_stddev);
		dao_set_cell_array_expr (dao, col, 5, expr);

		expr_abs = gnm_expr_new_funcall1 (fd_abs, make_cellref (0,-3));
		expr = gnm_expr_new_funcall3 (fd_tdist, expr_abs, make_cellref (0,-2),
					      gnm_expr_new_constant (value_new_int (1)));
		dao_set_cell_expr (dao, col, 8, expr);

		expr_abs = gnm_expr_new_funcall1 (fd_abs, make_cellref (0,-4));
		expr = gnm_expr_new_funcall3 (fd_tdist, expr_abs, make_cellref (0,-3),
					      gnm_expr_new_constant (value_new_int (2)));
		dao_set_cell_expr (dao, col, 9, expr);
	}
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_sqrt);
	gnm_func_dec_usage (fd_tdist);
	gnm_func_dec_usage (fd_iferror);

	dao_redraw_respan (dao);

	return FALSE;
}


gboolean
analysis_tool_one_mean_test_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_one_mean_test_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor
			(dao, _("Student-t Test (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		dao_adjust (dao, 1 + g_slist_length (info->base.input), 10);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Student-t Test"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Student-t Test"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_one_mean_test_engine_run (dao, specs);
	}
	return TRUE;
}

