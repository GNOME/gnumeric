/*
 * analysis-principal-components.c:
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
#include <tools/analysis-principal-components.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>

static gboolean
analysis_tool_principal_components_engine_run (data_analysis_output_t *dao,
					       analysis_tools_data_generic_t *info)
{
	int l = g_slist_length (info->input), i;
	GSList *inputdata;

	GnmFunc *fd_mean;
	GnmFunc *fd_var;
	GnmFunc *fd_eigen;
	GnmFunc *fd_mmult;
	GnmFunc *fd_munit;
	GnmFunc *fd_sqrt;
	GnmFunc *fd_count;
	GnmFunc *fd_sum;
	GnmFunc *fd_and;
	GnmFunc *fd_if;

	GnmExpr const *expr;
	GnmExpr const *expr_count;
	GnmExpr const *expr_munit;
	GnmExpr const *expr_and;

	int data_points;
	GnmExprList *and_args = NULL;
	GnmEvalPos ep;

	if (!dao_cell_is_visible (dao, l, 9 + 3 * l)) {
		dao_set_bold (dao, 0, 0, 0, 0);
		dao_set_italic (dao, 0, 0, 0, 0);
		dao_set_cell (dao, 0, 0,
			      _("Principal components analysis has "
				"insufficient space."));
		return 0;
	}

	fd_mean = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_mean);
	fd_var = gnm_func_lookup_or_add_placeholder ("VAR");
	gnm_func_inc_usage (fd_var);
	fd_eigen = gnm_func_lookup_or_add_placeholder ("EIGEN");
	gnm_func_inc_usage (fd_eigen);
	fd_mmult = gnm_func_lookup_or_add_placeholder ("MMULT");
	gnm_func_inc_usage (fd_mmult);
	fd_munit = gnm_func_lookup_or_add_placeholder ("MUNIT");
	gnm_func_inc_usage (fd_munit);
	fd_sqrt = gnm_func_lookup_or_add_placeholder ("SQRT");
	gnm_func_inc_usage (fd_sqrt);
	fd_count = gnm_func_lookup_or_add_placeholder ("COUNT");
	gnm_func_inc_usage (fd_count);
	fd_sum = gnm_func_lookup_or_add_placeholder ("SUM");
	gnm_func_inc_usage (fd_sum);
	fd_and = gnm_func_lookup_or_add_placeholder ("AND");
	gnm_func_inc_usage (fd_and);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF");
	gnm_func_inc_usage (fd_if);

	dao_set_bold (dao, 0, 0, 0, 0);
	dao_set_italic (dao, 0, 0, 0, 11 + 3 * l);
	dao_set_format (dao, 0, 0, 0, 0,
	/* translator info: The quotation marks in the next strings need to */
	/* remain since these are Excel-style format strings */
			_("\"Principal Components Analysis\";"
			  "[Red]\"Principal Components Analysis is invalid.\""));
	dao_set_align (dao, 0, 0, 0, 0,
		       GNM_HALIGN_LEFT, GNM_VALIGN_BOTTOM);

	dao->offset_row++;
	analysis_tool_table (dao, info, _("Covariances"), "COVAR", TRUE);
	dao->offset_row--;

	for (i = 1, inputdata = info->input; inputdata != NULL; i++, inputdata = inputdata->next)
		analysis_tools_write_label (inputdata->data, dao, info, 0, 9 + 2 * l + i, i);

	eval_pos_init_sheet (&ep,
			     ((GnmValue *)(info->input->data))->v_range.cell.a.sheet);
	data_points = value_area_get_width (info->input->data, &ep) *
		value_area_get_height (info->input->data, &ep);

	for (i = 0; i < l; i++)
		and_args = gnm_expr_list_prepend
			(and_args,
			 gnm_expr_new_binary
			 (gnm_expr_new_constant (value_new_int (data_points)),
			  GNM_EXPR_OP_EQUAL,
			  make_cellref (1 + i, 3 + l)));
	expr_and = gnm_expr_new_funcall	(fd_and, and_args);
	dao_set_cell_expr (dao, 0, 0,
			   gnm_expr_new_funcall3
			   (fd_if,
			    expr_and,
			    gnm_expr_new_constant (value_new_int (1)),
			    gnm_expr_new_constant (value_new_int (-1))));
	dao_set_merge (dao,0,0,2,0);
	set_cell_text_col (dao, 0, 3 + l,
			   _("/Count"
			     "/Mean"
			     "/Variance"
			     "//Eigenvalues"
			     "/Eigenvectors"));
	dao_set_cell (dao, 0, 11 + 3 * l, _("Percent of Trace"));
	dao_set_italic (dao, 0, 9 + 2 * l, 1 + l, 9 + 2 * l);
	dao_set_percent (dao, 1, 11 + 3 * l, 1 + l, 11 + 3 * l);

	for (i = 1, inputdata = info->input; inputdata != NULL; i++, inputdata = inputdata->next) {
		expr = gnm_expr_new_constant (value_dup (inputdata->data));

		dao_set_cell_expr (dao, i, 3 + l,
				   gnm_expr_new_funcall1 (fd_count, gnm_expr_copy (expr)));
		dao_set_cell_expr (dao, i, 4 + l,
				   gnm_expr_new_funcall1 (fd_mean, gnm_expr_copy (expr)));
		dao_set_cell_expr (dao, i, 5 + l,
				   gnm_expr_new_funcall1 (fd_var, expr));
	}

	expr_count = gnm_expr_new_binary (make_cellref (0,-4), GNM_EXPR_OP_DIV,
					  gnm_expr_new_binary (make_cellref (0,-4), GNM_EXPR_OP_SUB,
							       gnm_expr_new_constant (value_new_int (1))));
	expr = gnm_expr_new_funcall1
		(fd_eigen, gnm_expr_new_binary
		 (expr_count, GNM_EXPR_OP_MULT, make_rangeref (0, - (5 + l), l - 1, - 6)));
	dao_set_array_expr (dao, 1, 7 + l, l, l + 1, expr);

	for (i = 1; i <= l; i++) {
		dao_set_align (dao, i, 9 + 2 * l, i, 9 + 2 * l,
			       GNM_HALIGN_CENTER, GNM_VALIGN_BOTTOM);
		dao_set_cell_printf (dao, i, 9 + 2 * l, "\xce\xbe%i", i);
		dao_set_cell_expr (dao, i, 11 + 3 * l,
				   gnm_expr_new_binary (make_cellref (0,- 4 - 2 * l),
							GNM_EXPR_OP_DIV,
							gnm_expr_new_funcall1
							(fd_sum,
							 dao_get_rangeref (dao, 1, 7 + l, l, 7 + l))));
	}

	expr_munit =  gnm_expr_new_funcall1 (fd_munit, gnm_expr_new_constant (value_new_int (l)));
	expr = gnm_expr_new_funcall2 (fd_mmult,
				      gnm_expr_new_binary
				      (gnm_expr_new_funcall1
				       (fd_sqrt, gnm_expr_new_binary
					(gnm_expr_new_constant (value_new_int (1)),
					 GNM_EXPR_OP_DIV,
					 make_rangeref (0, - 5 - l, l - 1, - 5 - l))),
				       GNM_EXPR_OP_MULT,
				       gnm_expr_copy (expr_munit)),
				      make_rangeref (0, - 2 - l, l - 1, - 3));
	expr = gnm_expr_new_funcall2 (fd_mmult, expr,
				      gnm_expr_new_binary
				      (gnm_expr_new_funcall1
				       (fd_sqrt, make_rangeref (0, - 3 - l, l - 1, - 3 - l)),
				       GNM_EXPR_OP_MULT,
				       expr_munit));
	dao_set_array_expr (dao, 1, 10 + 2 * l, l, l, expr);

	gnm_func_dec_usage (fd_mean);
	gnm_func_dec_usage (fd_var);
	gnm_func_dec_usage (fd_eigen);
	gnm_func_dec_usage (fd_mmult);
	gnm_func_dec_usage (fd_munit);
	gnm_func_dec_usage (fd_sqrt);
	gnm_func_dec_usage (fd_count);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_and);
	gnm_func_dec_usage (fd_if);

	dao_redraw_respan (dao);
	return 0;
}

gboolean
analysis_tool_principal_components_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
				   analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_generic_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor
			(dao, _("Principal Components Analysis (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->input, info->group_by);
		dao_adjust (dao, 1 + g_slist_length (info->input),
			    12 + 3 * g_slist_length (info->input));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Principal Components Analysis"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Principal Components Analysis"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_principal_components_engine_run (dao, specs);
	}
	return TRUE;  /* We shouldn't get here */
}
