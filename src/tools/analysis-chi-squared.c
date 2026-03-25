/*
 * analysis-chi-squared.c:
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
#include <tools/analysis-chi-squared.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <sheet.h>

G_DEFINE_TYPE (GnmChiSquaredTool, gnm_chi_squared_tool, GNM_ANALYSIS_TOOL_TYPE)

static void
gnm_chi_squared_tool_init (GnmChiSquaredTool *tool)
{
	tool->input = NULL;
	tool->labels = FALSE;
	tool->independence = FALSE;
	tool->alpha = 0.05;
}

enum {
	CHI_SQUARED_PROP_0,
	CHI_SQUARED_PROP_ALPHA,
	CHI_SQUARED_PROP_INDEPENDENCE,
	CHI_SQUARED_PROP_LABELS
};

static void
gnm_chi_squared_tool_set_property (GObject      *obj,
				   guint         property_id,
				   GValue const *value,
				   GParamSpec   *pspec)
{
	GnmChiSquaredTool *tool = GNM_CHI_SQUARED_TOOL (obj);

	switch (property_id) {
	case CHI_SQUARED_PROP_ALPHA:
		tool->alpha = g_value_get_double (value);
		break;
	case CHI_SQUARED_PROP_INDEPENDENCE:
		tool->independence = g_value_get_boolean (value);
		break;
	case CHI_SQUARED_PROP_LABELS:
		tool->labels = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
gnm_chi_squared_tool_get_property (GObject    *obj,
				   guint       property_id,
				   GValue     *value,
				   GParamSpec *pspec)
{
	GnmChiSquaredTool *tool = GNM_CHI_SQUARED_TOOL (obj);

	switch (property_id) {
	case CHI_SQUARED_PROP_ALPHA:
		g_value_set_double (value, tool->alpha);
		break;
	case CHI_SQUARED_PROP_INDEPENDENCE:
		g_value_set_boolean (value, tool->independence);
		break;
	case CHI_SQUARED_PROP_LABELS:
		g_value_set_boolean (value, tool->labels);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
gnm_chi_squared_tool_finalize (GObject *obj)
{
	GnmChiSquaredTool *tool = GNM_CHI_SQUARED_TOOL (obj);
	value_release (tool->input);
	G_OBJECT_CLASS (gnm_chi_squared_tool_parent_class)->finalize (obj);
}

static gboolean
gnm_chi_squared_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmChiSquaredTool *ctool = GNM_CHI_SQUARED_TOOL (tool);
	GnmRange range;

	if (NULL != range_init_value (&range, ctool->input)) {
		ctool->n_c = range_width (&range) - (ctool->labels ? 1 : 0);
		ctool->n_r = range_height (&range) - (ctool->labels ? 1 : 0);
	} else
		return TRUE;

	if (ctool->n_c < 2 || ctool->n_r < 2)
		return TRUE;

	dao_adjust (dao, 2, 5);
	return FALSE;
}

static char *
gnm_chi_squared_tool_update_descriptor (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmChiSquaredTool *ctool = GNM_CHI_SQUARED_TOOL (tool);
	return dao_command_descriptor (dao, ctool->independence ?
					  _("Test of Independence (%s)")
					  : _("Test of Homogeneity (%s)"));
}

static gboolean
gnm_chi_squared_tool_prepare_output_range (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmChiSquaredTool *ctool = GNM_CHI_SQUARED_TOOL (tool);
	dao_prepare_output (wbc, dao, ctool->independence ?
			    _("Test of Independence")
			    : _("Test of Homogeneity"));
	return FALSE;
}

static gboolean
gnm_chi_squared_tool_format_output_range (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmChiSquaredTool *ctool = GNM_CHI_SQUARED_TOOL (tool);
	return dao_format_output (wbc, dao, ctool->independence ?
				  _("Test of Independence")
				  : _("Test of Homogeneity"));
}

static gboolean
gnm_chi_squared_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmChiSquaredTool *ctool = GNM_CHI_SQUARED_TOOL (tool);
	GnmExpr const *expr_check;
	GnmExpr const *expr_region;
	GnmExpr const *expr_statistic;
	GnmExpr const *expr_row_ones;
	GnmExpr const *expr_col_ones;
	GnmExpr const *expr_row;
	GnmExpr const *expr_column;
	GnmExpr const *expr_expect;

	GnmFunc *fd_mmult     = gnm_func_get_and_use ("MMULT");
	GnmFunc *fd_row       = gnm_func_get_and_use ("ROW");
	GnmFunc *fd_column    = gnm_func_get_and_use ("COLUMN");
	GnmFunc *fd_transpose = gnm_func_get_and_use ("TRANSPOSE");
	GnmFunc *fd_sum       = gnm_func_get_and_use ("SUM");
	GnmFunc *fd_min       = gnm_func_get_and_use ("MIN");
	GnmFunc *fd_chiinv    = gnm_func_get_and_use ("CHIINV");
	GnmFunc *fd_chidist   = gnm_func_get_and_use ("CHIDIST");
	char const *label;
	char *cc;

	label = (ctool->independence)
	/* translator info: The quotation marks in the next strings need to */
	/* remain since these are Excel-style format strings */
		? _("[>=5]\"Test of Independence\";[<5][Red]\"Invalid Test of Independence\"")
		: _("[>=5]\"Test of Homogeneity\";[<5][Red]\"Invalid Test of Homogeneity\"");

	dao_set_italic (dao, 0, 1, 0, 4);
	set_cell_text_col (dao, 0, 1, _("/Test Statistic"
					"/Degrees of Freedom"
					"/p-Value"
					"/Critical Value"));
	cc = g_strdup_printf ("%s = %.2" GNM_FORMAT_f, "\xce\xb1", ctool->alpha);
	dao_set_cell_comment (dao, 0, 4, cc);
	g_free (cc);

	GnmValue *input = value_dup (ctool->input);
	analysis_tools_adjust_areas (input);
	if (ctool->labels) {
		input->v_range.cell.a.col++;
		input->v_range.cell.a.row++;
	}
	expr_region = gnm_expr_new_constant (input);

	expr_row = gnm_expr_new_funcall1 (fd_row, gnm_expr_copy (expr_region));
	expr_column = gnm_expr_new_funcall1 (fd_column, gnm_expr_copy (expr_region));
	expr_col_ones = gnm_expr_new_funcall1 (fd_transpose,
					       gnm_expr_new_binary (gnm_expr_copy (expr_column),
								    GNM_EXPR_OP_DIV,
								    expr_column));
	expr_row_ones = gnm_expr_new_funcall1 (fd_transpose,
					       gnm_expr_new_binary (gnm_expr_copy (expr_row),
								    GNM_EXPR_OP_DIV,
								    expr_row));
	expr_expect = gnm_expr_new_binary (gnm_expr_new_funcall2
					   (fd_mmult,
					    gnm_expr_new_funcall2
					    (fd_mmult,
					     gnm_expr_copy (expr_region),
					     expr_col_ones),
					    gnm_expr_new_funcall2
					    (fd_mmult,
					     expr_row_ones,
					     gnm_expr_copy (expr_region))),
					   GNM_EXPR_OP_DIV,
					   gnm_expr_new_funcall1 (fd_sum, gnm_expr_copy (expr_region)));

	expr_check = gnm_expr_new_funcall1 (fd_min, gnm_expr_copy (expr_expect));
	dao_set_merge (dao, 0, 0, 1, 0);
	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell_expr (dao, 0, 0, expr_check);
	dao_set_format (dao, 0, 0, 0, 0, label);
	dao_set_align (dao, 0, 0, 0, 0, GNM_HALIGN_CENTER, GNM_VALIGN_BOTTOM);

	expr_statistic = gnm_expr_new_funcall1 (fd_sum,
						gnm_expr_new_binary
						(gnm_expr_new_binary (gnm_expr_new_binary
								      (gnm_expr_copy (expr_region),
								       GNM_EXPR_OP_SUB,
								       gnm_expr_copy (expr_expect)),
								      GNM_EXPR_OP_EXP,
								      gnm_expr_new_constant (value_new_int (2))),
						 GNM_EXPR_OP_DIV,
						 gnm_expr_copy (expr_expect)));
	dao_set_cell_array_expr (dao, 1, 1, expr_statistic);

	dao_set_cell_int (dao, 1, 2, (ctool->n_r - 1)*(ctool->n_c - 1));
	dao_set_cell_expr(dao, 1, 3, gnm_expr_new_funcall2
			  (fd_chidist, make_cellref (0,-2),  make_cellref (0,-1)));
	dao_set_cell_expr(dao, 1, 4, gnm_expr_new_funcall2
			  (fd_chiinv,
			   gnm_expr_new_constant (value_new_float (ctool->alpha)),
			   make_cellref (0,-2)));

	gnm_func_dec_usage (fd_mmult);
	gnm_func_dec_usage (fd_row);
	gnm_func_dec_usage (fd_column);
	gnm_func_dec_usage (fd_transpose);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_min);
	gnm_func_dec_usage (fd_chiinv);
	gnm_func_dec_usage (fd_chidist);

	gnm_expr_free (expr_expect);
	gnm_expr_free (expr_region);
	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_chi_squared_tool_class_init (GnmChiSquaredToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_chi_squared_tool_set_property;
	gobject_class->get_property = gnm_chi_squared_tool_get_property;
	gobject_class->finalize = gnm_chi_squared_tool_finalize;
	at_class->update_dao = gnm_chi_squared_tool_update_dao;
	at_class->update_descriptor = gnm_chi_squared_tool_update_descriptor;
	at_class->prepare_output_range = gnm_chi_squared_tool_prepare_output_range;
	at_class->format_output_range = gnm_chi_squared_tool_format_output_range;
	at_class->perform_calc = gnm_chi_squared_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		CHI_SQUARED_PROP_ALPHA,
		g_param_spec_double ("alpha", NULL, NULL,
				     0.0, 1.0, 0.05, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		CHI_SQUARED_PROP_INDEPENDENCE,
		g_param_spec_boolean ("independence", NULL, NULL,
				      FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		CHI_SQUARED_PROP_LABELS,
		g_param_spec_boolean ("labels", NULL, NULL,
				      FALSE, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_chi_squared_tool_new (void)
{
	return g_object_new (GNM_TYPE_CHI_SQUARED_TOOL, NULL);
}
