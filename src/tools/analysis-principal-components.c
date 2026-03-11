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
#include <sheet.h>

static gboolean analysis_tool_principal_components_engine_run (GnmPrincipalComponentsTool *ptool, data_analysis_output_t *dao);

G_DEFINE_TYPE (GnmPrincipalComponentsTool, gnm_principal_components_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

static void
gnm_principal_components_tool_init (G_GNUC_UNUSED GnmPrincipalComponentsTool *tool)
{
}

static gboolean
gnm_principal_components_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	int l;

	analysis_tool_prepare_input_range (gtool);
	if (!analysis_tool_check_input_homogeneity (gtool)) {
		gtool->base.err = gtool->base.group_by + 1;
		return TRUE;
	}

	l = g_slist_length (gtool->base.input);
	dao_adjust (dao, 1 + l, 11 + 3 * l);
	return FALSE;
}

static char *
gnm_principal_components_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Principal Components Analysis (%s)"));
}

static gboolean
gnm_principal_components_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Principal Components Analysis"));
	return FALSE;
}

static gboolean
gnm_principal_components_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Principal Components Analysis"));
}

static gboolean
gnm_principal_components_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmPrincipalComponentsTool *ptool = GNM_PRINCIPAL_COMPONENTS_TOOL (tool);
	return analysis_tool_principal_components_engine_run (ptool, dao);
}

static void
gnm_principal_components_tool_class_init (GnmPrincipalComponentsToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_principal_components_tool_update_dao;
	at_class->update_descriptor = gnm_principal_components_tool_update_descriptor;
	at_class->prepare_output_range = gnm_principal_components_tool_prepare_output_range;
	at_class->format_output_range = gnm_principal_components_tool_format_output_range;
	at_class->perform_calc = gnm_principal_components_tool_perform_calc;
}

GnmAnalysisTool *
gnm_principal_components_tool_new (void)
{
	return g_object_new (GNM_TYPE_PRINCIPAL_COMPONENTS_TOOL, NULL);
}

static gboolean
analysis_tool_principal_components_engine_run (GnmPrincipalComponentsTool *ptool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = &ptool->parent;
	int l = g_slist_length (gtool->base.input), i;
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

	fd_mean = gnm_func_get_and_use ("AVERAGE");
	fd_var = gnm_func_get_and_use ("VAR");
	fd_eigen = gnm_func_get_and_use ("EIGEN");
	fd_mmult = gnm_func_get_and_use ("MMULT");
	fd_munit = gnm_func_get_and_use ("MUNIT");
	fd_sqrt = gnm_func_get_and_use ("SQRT");
	fd_count = gnm_func_get_and_use ("COUNT");
	fd_sum = gnm_func_get_and_use ("SUM");
	fd_and = gnm_func_get_and_use ("AND");
	fd_if = gnm_func_get_and_use ("IF");

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
	analysis_tool_table (gtool, dao, _("Covariances"), "COVAR", TRUE);
	dao->offset_row--;

	for (i = 1, inputdata = gtool->base.input; inputdata != NULL; i++, inputdata = inputdata->next)
		analysis_tools_write_label (gtool, inputdata->data, dao, 0, 9 + 2 * l + i, i);

	eval_pos_init_sheet (&ep,
			     ((GnmValue *)(gtool->base.input->data))->v_range.cell.a.sheet);
	data_points = value_area_get_width (gtool->base.input->data, &ep) *
		value_area_get_height (gtool->base.input->data, &ep);

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

	for (i = 1, inputdata = gtool->base.input; inputdata != NULL; i++, inputdata = inputdata->next) {
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

