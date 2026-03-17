/*
 * analysis-regression.c:
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
#include <tools/analysis-regression.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>
#include <sheet-object-graph.h>
#include <goffice/goffice.h>
#include <sheet.h>

static gboolean analysis_tool_regression_engine_run (GnmRegressionTool *rtool, data_analysis_output_t *dao);
static gboolean analysis_tool_regression_simple_engine_run (GnmRegressionTool *rtool, data_analysis_output_t *dao);

G_DEFINE_TYPE (GnmRegressionTool, gnm_regression_tool, GNM_TYPE_GENERIC_B_ANALYSIS_TOOL)

static void
gnm_regression_tool_init (GnmRegressionTool *tool)
{
	tool->group_by = GNM_TOOL_GROUPED_BY_COL;
	tool->intercept = TRUE;
	tool->multiple_regression = TRUE;
	tool->multiple_y = FALSE;
	tool->residual = TRUE;
}

enum {
	REGRESSION_PROP_0,
	REGRESSION_PROP_GROUP_BY,
	REGRESSION_PROP_INTERCEPT,
	REGRESSION_PROP_MULTIPLE_REGRESSION,
	REGRESSION_PROP_MULTIPLE_Y,
	REGRESSION_PROP_RESIDUAL
};

static void
gnm_regression_tool_set_property (GObject      *obj,
				  guint         property_id,
				  GValue const *value,
				  GParamSpec   *pspec)
{
	GnmRegressionTool *tool = GNM_REGRESSION_TOOL (obj);

	switch (property_id) {
	case REGRESSION_PROP_GROUP_BY:
		tool->group_by = g_value_get_enum (value);
		break;
	case REGRESSION_PROP_INTERCEPT:
		tool->intercept = g_value_get_boolean (value);
		break;
	case REGRESSION_PROP_MULTIPLE_REGRESSION:
		tool->multiple_regression = g_value_get_boolean (value);
		break;
	case REGRESSION_PROP_MULTIPLE_Y:
		tool->multiple_y = g_value_get_boolean (value);
		break;
	case REGRESSION_PROP_RESIDUAL:
		tool->residual = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
gnm_regression_tool_get_property (GObject    *obj,
				  guint       property_id,
				  GValue     *value,
				  GParamSpec *pspec)
{
	GnmRegressionTool *tool = GNM_REGRESSION_TOOL (obj);

	switch (property_id) {
	case REGRESSION_PROP_GROUP_BY:
		g_value_set_enum (value, tool->group_by);
		break;
	case REGRESSION_PROP_INTERCEPT:
		g_value_set_boolean (value, tool->intercept);
		break;
	case REGRESSION_PROP_MULTIPLE_REGRESSION:
		g_value_set_boolean (value, tool->multiple_regression);
		break;
	case REGRESSION_PROP_MULTIPLE_Y:
		g_value_set_boolean (value, tool->multiple_y);
		break;
	case REGRESSION_PROP_RESIDUAL:
		g_value_set_boolean (value, tool->residual);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
gnm_regression_tool_finalize (GObject *obj)
{
	GnmRegressionTool *tool = GNM_REGRESSION_TOOL (obj);
	range_list_destroy (tool->indep_vars);
	tool->indep_vars = NULL;
	G_OBJECT_CLASS (gnm_regression_tool_parent_class)->finalize (obj);
}

static gboolean
gnm_regression_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmRegressionTool *rtool = GNM_REGRESSION_TOOL (tool);
	gint xdim = analysis_tools_calculate_xdim (rtool->parent.base.range_1, rtool->group_by);
	gint cols, rows;

	if (rtool->multiple_regression) {
		cols = 7;
		rows = 17 + xdim;
		rtool->indep_vars = NULL;
		if (rtool->residual) {
			gint residual_cols = xdim + 4;
			GnmValue *val = rtool->parent.base.range_1;

			rows += 2 + analysis_tools_calculate_n_obs (val, rtool->group_by);
			residual_cols += 4;
			if (cols < residual_cols)
				cols = residual_cols;
		}
	} else {
		rtool->indep_vars = g_slist_prepend (NULL, rtool->parent.base.range_1);
		rtool->parent.base.range_1 = NULL;
		analysis_tool_prepare_input_range_full (&rtool->indep_vars, rtool->group_by);
		cols = 6;
		rows = 3 + xdim;
	}
	dao_adjust (dao, cols, rows);
	return FALSE;
}

static char *
gnm_regression_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Regression (%s)"));
}

static gboolean
gnm_regression_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Regression"));
	return FALSE;
}

static gboolean
gnm_regression_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Regression"));
}

static gboolean
gnm_regression_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmRegressionTool *rtool = GNM_REGRESSION_TOOL (tool);
	if (rtool->multiple_regression)
		return analysis_tool_regression_engine_run (rtool, dao);
	else
		return analysis_tool_regression_simple_engine_run (rtool, dao);
}

static void
gnm_regression_tool_class_init (GnmRegressionToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_regression_tool_set_property;
	gobject_class->get_property = gnm_regression_tool_get_property;
	gobject_class->finalize = gnm_regression_tool_finalize;
	at_class->update_dao = gnm_regression_tool_update_dao;
	at_class->update_descriptor = gnm_regression_tool_update_descriptor;
	at_class->prepare_output_range = gnm_regression_tool_prepare_output_range;
	at_class->format_output_range = gnm_regression_tool_format_output_range;
	at_class->perform_calc = gnm_regression_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		REGRESSION_PROP_GROUP_BY,
		g_param_spec_enum ("group-by", NULL, NULL,
				   GNM_TOOL_GROUP_BY_TYPE, GNM_TOOL_GROUPED_BY_COL,
				   G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		REGRESSION_PROP_INTERCEPT,
		g_param_spec_boolean ("intercept", NULL, NULL,
				      TRUE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		REGRESSION_PROP_MULTIPLE_REGRESSION,
		g_param_spec_boolean ("multiple-regression", NULL, NULL,
				      TRUE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		REGRESSION_PROP_MULTIPLE_Y,
		g_param_spec_boolean ("multiple-y", NULL, NULL,
				      FALSE, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		REGRESSION_PROP_RESIDUAL,
		g_param_spec_boolean ("residual", NULL, NULL,
				      TRUE, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_regression_tool_new (void)
{
	return g_object_new (GNM_TYPE_REGRESSION_TOOL, NULL);
}

/*
 * analysis_tools_write_a_label:
 * @val: range to extract label from
 * @dao: data_analysis_output_t, where to write to
 * @labels: boolean
 * @group_by: grouping info
 * @x: output col number
 * @y: output row number
 */
static void
analysis_tools_write_a_label (GnmValue *val, data_analysis_output_t *dao,
			      gboolean labels, gnm_tool_group_by_t group_by,
			      int x, int y)
{
	if (labels) {
		GnmValue *label = value_dup (val);

		label->v_range.cell.b = label->v_range.cell.a;
		dao_set_cell_expr (dao, x, y, gnm_expr_new_constant (label));
		analysis_tools_remove_label (val, labels, group_by);
	} else {
		char const *str = ((group_by == GNM_TOOL_GROUPED_BY_ROW) ? "row" : "col");
		char const *label = ((group_by == GNM_TOOL_GROUPED_BY_ROW) ? _("Row") : _("Column"));
		GnmFunc *fd_concatenate = gnm_func_get_and_use ("CONCATENATE");
		GnmFunc *fd_cell = gnm_func_get_and_use ("CELL");

		dao_set_cell_expr (dao, x, y, gnm_expr_new_funcall3
				   (fd_concatenate, gnm_expr_new_constant (value_new_string (label)),
				    gnm_expr_new_constant (value_new_string (" ")),
				    gnm_expr_new_funcall2 (fd_cell,
							   gnm_expr_new_constant (value_new_string (str)),
							   gnm_expr_new_constant (value_dup (val)))));

		gnm_func_dec_usage (fd_concatenate);
		gnm_func_dec_usage (fd_cell);
	}
}

static gboolean
analysis_tool_regression_engine_run (GnmRegressionTool *rtool, data_analysis_output_t *dao)
{
	GnmGenericBAnalysisTool *gtool = &rtool->parent;
	gint xdim = analysis_tools_calculate_xdim (gtool->base.range_1, rtool->group_by);
	gint i;

	GnmValue *val_1 = value_dup (gtool->base.range_1);
	GnmValue *val_2 = value_dup (gtool->base.range_2);
	GnmValue *val_1_cp = NULL;
	GnmValue *val_2_cp = NULL;

	GnmExpr const *expr_x;
	GnmExpr const *expr_y;
	GnmExpr const *expr_linest;
	GnmExpr const *expr_intercept;
	GnmExpr const *expr_ms;
	GnmExpr const *expr_sum;
	GnmExpr const *expr_tstat;
	GnmExpr const *expr_pvalue;
	GnmExpr const *expr_n;
	GnmExpr const *expr_df;
	GnmExpr const *expr_lower;
	GnmExpr const *expr_upper;
	GnmExpr const *expr_confidence;

	GnmFunc *fd_linest    = gnm_func_get_and_use ("LINEST");
	GnmFunc *fd_index     = gnm_func_get_and_use ("INDEX");
	GnmFunc *fd_fdist     = gnm_func_get_and_use ("FDIST");
	GnmFunc *fd_sum       = gnm_func_get_and_use ("SUM");
	GnmFunc *fd_sqrt      = gnm_func_get_and_use ("SQRT");
	GnmFunc *fd_tdist     = gnm_func_get_and_use ("TDIST");
	GnmFunc *fd_abs       = gnm_func_get_and_use ("ABS");
	GnmFunc *fd_tinv      = gnm_func_get_and_use ("TINV");
	GnmFunc *fd_transpose = gnm_func_get_and_use ("TRANSPOSE");
	GnmFunc *fd_concatenate = NULL;
	GnmFunc *fd_cell = NULL;
	GnmFunc *fd_offset = NULL;
	GnmFunc *fd_sumproduct = NULL;
	GnmFunc *fd_leverage = NULL;

	char const *str = ((rtool->group_by == GNM_TOOL_GROUPED_BY_ROW) ? "row" : "col");
	char const *label = ((rtool->group_by == GNM_TOOL_GROUPED_BY_ROW) ? _("Row")
			     : _("Column"));

	if (!gtool->base.labels) {
		fd_concatenate = gnm_func_get_and_use ("CONCATENATE");
		fd_cell        = gnm_func_get_and_use ("CELL");
		fd_offset      = gnm_func_get_and_use ("OFFSET");
	}
	if (rtool->residual) {
		fd_sumproduct  = gnm_func_get_and_use ("SUMPRODUCT");
		fd_leverage = gnm_func_get_and_use ("LEVERAGE");
	}

	analysis_tools_adjust_areas (val_1);
	analysis_tools_adjust_areas (val_2);

	dao_set_italic (dao, 0, 0, 0, 16 + xdim);
        set_cell_text_col (dao, 0, 0, _("/SUMMARY OUTPUT"
					"/"
					"/Regression Statistics"
					"/Multiple R"
					"/R^2"
					"/Standard Error"
					"/Adjusted R^2"
					"/Observations"
					"/"
					"/ANOVA"
					"/"
					"/Regression"
					"/Residual"
					"/Total"
					"/"
					"/"
					"/Intercept"));
	dao_set_merge (dao, 0, 0, 1, 0);
	dao_set_italic (dao, 2, 0, 3, 0);
	dao_set_cell (dao, 2, 0, _("Response Variable"));
	dao_set_merge (dao, 0, 2, 1, 2);

	if (gtool->base.labels) {

		dao_set_cell_expr (dao, 3, 0,
				   gnm_expr_new_funcall1 (fd_index, gnm_expr_new_constant (value_dup (val_2))));

		val_1_cp =  value_dup (val_1);
		val_2_cp =  value_dup (val_2);
		if (rtool->group_by == GNM_TOOL_GROUPED_BY_ROW) {
			val_1->v_range.cell.a.col++;
			val_2->v_range.cell.a.col++;
			val_1_cp->v_range.cell.b.col = val_1_cp->v_range.cell.a.col;
			dao_set_array_expr (dao, 0, 17, 1, xdim, gnm_expr_new_constant
					    (value_dup (val_1_cp)));
		} else {
			val_1->v_range.cell.a.row++;
			val_2->v_range.cell.a.row++;
			val_1_cp->v_range.cell.b.row = val_1_cp->v_range.cell.a.row;
			dao_set_array_expr (dao, 0, 17, 1, xdim, gnm_expr_new_funcall1
					    (fd_transpose,
					     gnm_expr_new_constant (value_dup (val_1_cp))));
		}
	} else {
		dao_set_cell_expr (dao, 3, 0, gnm_expr_new_funcall3
				   (fd_concatenate, gnm_expr_new_constant (value_new_string (label)),
				    gnm_expr_new_constant (value_new_string (" ")),
				    gnm_expr_new_funcall2 (fd_cell,
							   gnm_expr_new_constant (value_new_string (str)),
							   gnm_expr_new_constant (value_dup (val_2)))));
	}

	dao_set_italic (dao, 1, 10, 5, 10);
        set_cell_text_row (dao, 1, 10, _("/df"
					 "/SS"
					 "/MS"
					 "/F"
					 "/Significance of F"));

	dao_set_italic (dao, 1, 15, 6, 15);
	set_cell_text_row (dao, 1, 15, _("/Coefficients"
					 "/Standard Error"
					 "/t-Statistics"
					 "/p-Value"));

	/* xgettext: this is an Excel-style number format.  Use "..." quotes and do not translate the 0% */
	dao_set_format  (dao, 5, 15, 5, 15, _("\"Lower\" 0%"));
	/* xgettext: this is an Excel-style number format.  Use "..." quotes and do not translate the 0% */
	dao_set_format  (dao, 6, 15, 6, 15, _("\"Upper\" 0%"));
	dao_set_align (dao, 5, 15, 5, 15, GNM_HALIGN_LEFT, GNM_VALIGN_TOP);
	dao_set_align (dao, 6, 15, 6, 15, GNM_HALIGN_RIGHT, GNM_VALIGN_TOP);

	dao_set_cell_float (dao, 5, 15, 1 - gtool->base.alpha);
	dao_set_cell_expr (dao, 6, 15, make_cellref (-1, 0));
	expr_confidence = dao_get_cellref (dao, 5, 15);

	dao_set_cell_comment (dao, 4, 15,
			      _("Probability of observing a t-statistic\n"
				"whose absolute value is at least as large\n"
				"as the absolute value of the actually\n"
				"observed t-statistic, assuming the null\n"
				"hypothesis is in fact true."));
	if (!rtool->intercept)
		dao_set_cell_comment (dao, 0, 4,
			      _("This value is not the square of R\n"
				"but the uncentered version of the\n"
				"coefficient of determination; that\n"
				"is, the proportion of the sum of\n"
				"squares explained by the model."));

	expr_x = gnm_expr_new_constant (value_dup (val_1));
	expr_y = gnm_expr_new_constant (value_dup (val_2));

	expr_intercept = gnm_expr_new_constant (value_new_bool (rtool->intercept));

	expr_linest = gnm_expr_new_funcall4 (fd_linest,
					     expr_y,
					     expr_x,
					     expr_intercept,
					     gnm_expr_new_constant (value_new_bool (TRUE)));


	/* Multiple R */
	if (rtool->intercept) {
		if (dao_cell_is_visible (dao, 1, 4))
			dao_set_cell_expr (dao, 1, 3, gnm_expr_new_funcall1 (fd_sqrt, make_cellref (0, 1)));
		else
			dao_set_cell_expr (dao, 1, 3,
					   gnm_expr_new_funcall1 (fd_sqrt, gnm_expr_new_funcall3
								  (fd_index,
								   gnm_expr_copy (expr_linest),
								   gnm_expr_new_constant (value_new_int (3)),
								   gnm_expr_new_constant (value_new_int (1)))));
	} else
			dao_set_cell_expr (dao, 1, 3,
					   gnm_expr_new_funcall1 (fd_sqrt, gnm_expr_new_funcall3
								  (fd_index,
								   gnm_expr_new_funcall4
								   (fd_linest,
								    gnm_expr_new_constant (value_dup (val_2)),
								    gnm_expr_new_constant (value_dup (val_1)),
								    gnm_expr_new_constant (value_new_bool (TRUE)),
								    gnm_expr_new_constant (value_new_bool (TRUE))),
								   gnm_expr_new_constant (value_new_int (3)),
								   gnm_expr_new_constant (value_new_int (1)))));


	/* R Square */
	dao_set_cell_array_expr (dao, 1, 4,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (3)),
							gnm_expr_new_constant (value_new_int (1))));

	/* Standard Error */
	dao_set_cell_array_expr (dao, 1, 5,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (3)),
							gnm_expr_new_constant (value_new_int (2))));

	/* Adjusted R Square */
	if (dao_cell_is_visible (dao, 1, 7))
		expr_n = make_cellref (0, 1);
	else
		expr_n = gnm_expr_new_funcall3 (fd_sum,
						gnm_expr_new_constant (value_new_int (xdim)),
						gnm_expr_new_funcall3 (fd_index,
								       gnm_expr_copy (expr_linest),
								       gnm_expr_new_constant (value_new_int (4)),
								       gnm_expr_new_constant (value_new_int (2))),
						gnm_expr_new_constant (value_new_int (1)));

	dao_set_cell_expr (dao, 1, 6, gnm_expr_new_binary
			   (gnm_expr_new_constant (value_new_int (1)),
			    GNM_EXPR_OP_SUB,
			    gnm_expr_new_binary
			    (gnm_expr_new_binary
			     (gnm_expr_new_binary
			      (gnm_expr_copy (expr_n),
			       GNM_EXPR_OP_SUB,
			       gnm_expr_new_constant (value_new_int (1))),
			      GNM_EXPR_OP_DIV,
			      gnm_expr_new_binary
			      (expr_n,
			       GNM_EXPR_OP_SUB,
			       gnm_expr_new_constant (value_new_int (xdim + (rtool->intercept?1:0))))),
			     GNM_EXPR_OP_MULT,
			     gnm_expr_new_binary
			     (gnm_expr_new_constant (value_new_int (1)),
			      GNM_EXPR_OP_SUB,
			      make_cellref (0, -2)))));

	/* Observations */

	if (dao_cell_is_visible (dao, 1, 13))
		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_funcall2 (fd_sum,
							  make_cellref (0, 6),
							  gnm_expr_new_constant (value_new_int (rtool->intercept?1:0))));
	else if (dao_cell_is_visible (dao, 1, 12))
		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_funcall3 (fd_sum,
							  make_cellref (0, 4),
							  make_cellref (0, 5),
							  gnm_expr_new_constant (value_new_int (rtool->intercept?1:0))));
	else
		dao_set_cell_expr (dao, 1, 7,
				   gnm_expr_new_funcall3 (fd_sum,
							  gnm_expr_new_constant (value_new_int (xdim)),
							  gnm_expr_new_funcall3 (fd_index,
										 gnm_expr_copy (expr_linest),
										 gnm_expr_new_constant (value_new_int (4)),
										 gnm_expr_new_constant (value_new_int (2))),
							  gnm_expr_new_constant (value_new_int (rtool->intercept?1:0))));



	/* Regression / df */

	dao_set_cell_int (dao, 1, 11, xdim);

	/* Residual / df */
	dao_set_cell_array_expr (dao, 1, 12,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (4)),
							gnm_expr_new_constant (value_new_int (2))));


	/* Total / df */
	expr_sum = gnm_expr_new_binary (make_cellref (0, -2),
				       GNM_EXPR_OP_ADD,
				       make_cellref (0, -1));
	dao_set_cell_expr (dao, 1, 13, gnm_expr_copy (expr_sum));

	/* Regression / SS */
	dao_set_cell_array_expr (dao, 2, 11,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (5)),
							gnm_expr_new_constant (value_new_int (1))));

	/* Residual / SS */
	dao_set_cell_array_expr (dao, 2, 12,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (5)),
							gnm_expr_new_constant (value_new_int (2))));


	/* Total / SS */
	dao_set_cell_expr (dao, 2, 13, expr_sum);


	/* Regression / MS */
	expr_ms = gnm_expr_new_binary (make_cellref (-1, 0),
				       GNM_EXPR_OP_DIV,
				       make_cellref (-2, 0));
	dao_set_cell_expr (dao, 3, 11, gnm_expr_copy (expr_ms));

	/* Residual / MS */
	dao_set_cell_expr (dao, 3, 12, expr_ms);


	/* F */
	dao_set_cell_array_expr (dao, 4, 11,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (4)),
							gnm_expr_new_constant (value_new_int (1))));

	/* Significance of F */

	if (dao_cell_is_visible (dao, 1, 12))
		dao_set_cell_expr (dao, 5, 11, gnm_expr_new_funcall3 (fd_fdist,
								      make_cellref (-1, 0),
								      make_cellref (-4, 0),
								      make_cellref (-4, 1)));
	else
		dao_set_cell_expr (dao, 5, 11, gnm_expr_new_funcall3 (fd_fdist,
								      make_cellref (-1, 0),
								      make_cellref (-4, 0),
								      gnm_expr_new_funcall3
								      (fd_index,
								       gnm_expr_copy (expr_linest),
								       gnm_expr_new_constant (value_new_int (4)),
								       gnm_expr_new_constant (value_new_int (2)))));


	/* Intercept */


	expr_tstat = gnm_expr_new_binary (make_cellref (-2, 0),
				       GNM_EXPR_OP_DIV,
				       make_cellref (-1, 0));
	expr_df = dao_get_cellref (dao, 1, 12);
	expr_pvalue = gnm_expr_new_funcall3 (fd_tdist, gnm_expr_new_funcall1 (fd_abs, make_cellref (-1, 0)),
					     gnm_expr_copy (expr_df),
					     gnm_expr_new_constant (value_new_int (2)));
	expr_lower = gnm_expr_new_binary (make_cellref (-4, 0),
				      GNM_EXPR_OP_SUB,
				      gnm_expr_new_binary (make_cellref (-3, 0),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_funcall2
							   (fd_tinv,
							    gnm_expr_new_binary
							    (gnm_expr_new_constant (value_new_float (1.0)),
							     GNM_EXPR_OP_SUB,
							     gnm_expr_copy (expr_confidence)),
							    gnm_expr_copy (expr_df))));
	expr_upper = gnm_expr_new_binary (make_cellref (-5, 0),
				      GNM_EXPR_OP_ADD,
				      gnm_expr_new_binary (make_cellref (-4, 0),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_funcall2
							   (fd_tinv,
							    gnm_expr_new_binary
							    (gnm_expr_new_constant (value_new_float (1.0)),
							     GNM_EXPR_OP_SUB,
							     expr_confidence),
							    expr_df)));


	/* Intercept */

	if (!rtool->intercept) {
		dao_set_cell_int (dao, 1, 16, 0);
		for (i = 2; i <= 6; i++)
			dao_set_cell_na (dao, i, 16);
	} else {
		dao_set_cell_array_expr (dao, 1, 16,
					 gnm_expr_new_funcall3
					 (fd_index,
					  gnm_expr_copy (expr_linest),
					  gnm_expr_new_constant (value_new_int (1)),
					  gnm_expr_new_constant (value_new_int (xdim+1))));
		dao_set_cell_array_expr (dao, 2, 16,
					 gnm_expr_new_funcall3
					 (fd_index,
					  gnm_expr_copy (expr_linest),
					  gnm_expr_new_constant (value_new_int (2)),
					  gnm_expr_new_constant (value_new_int (xdim+1))));
		dao_set_cell_expr (dao, 3, 16, gnm_expr_copy (expr_tstat));
		dao_set_cell_expr (dao, 4, 16, gnm_expr_copy (expr_pvalue));
		dao_set_cell_expr (dao, 5, 16, gnm_expr_copy (expr_lower));
		dao_set_cell_expr (dao, 6, 16, gnm_expr_copy (expr_upper));
	}

	/* Coefficients */

	dao->offset_row += 17;

	for (i = 0; i < xdim; i++) {
		if (!gtool->base.labels) {
			GnmExpr const *expr_offset;

			if (rtool->group_by == GNM_TOOL_GROUPED_BY_ROW)
				expr_offset = gnm_expr_new_funcall3
					(fd_offset, gnm_expr_new_constant (value_dup (val_1)),
					 gnm_expr_new_constant (value_new_int (i)),
					 gnm_expr_new_constant (value_new_int (0)));
			else
				expr_offset = gnm_expr_new_funcall3
					(fd_offset, gnm_expr_new_constant (value_dup (val_1)),
					 gnm_expr_new_constant (value_new_int (0)),
					 gnm_expr_new_constant (value_new_int (i)));

			dao_set_cell_expr (dao, 0, i, gnm_expr_new_funcall3
					   (fd_concatenate, gnm_expr_new_constant (value_new_string (label)),
					    gnm_expr_new_constant (value_new_string (" ")),
					    gnm_expr_new_funcall2
					    (fd_cell,
					     gnm_expr_new_constant (value_new_string (str)),
					     expr_offset)));
		}

		dao_set_cell_array_expr (dao, 1, i,
					 gnm_expr_new_funcall3
					 (fd_index,
					  gnm_expr_copy (expr_linest),
					  gnm_expr_new_constant (value_new_int (1)),
					  gnm_expr_new_constant (value_new_int (xdim - i))));
		dao_set_cell_array_expr (dao, 2, i,
					 gnm_expr_new_funcall3
					 (fd_index,
					  gnm_expr_copy (expr_linest),
					  gnm_expr_new_constant (value_new_int (2)),
					  gnm_expr_new_constant (value_new_int (xdim - i))));
		dao_set_cell_expr (dao, 3, i, gnm_expr_copy (expr_tstat));
		dao_set_cell_expr (dao, 4, i, gnm_expr_copy (expr_pvalue));
		dao_set_cell_expr (dao, 5, i, gnm_expr_copy (expr_lower));
		dao_set_cell_expr (dao, 6, i, gnm_expr_copy (expr_upper));
	}


	gnm_expr_free (expr_linest);
	gnm_expr_free (expr_tstat);
	gnm_expr_free (expr_pvalue);
	gnm_expr_free (expr_lower);
	gnm_expr_free (expr_upper);

	value_release (val_1_cp);
	value_release (val_2_cp);

	if (rtool->residual) {
		gint n_obs = analysis_tools_calculate_n_obs (val_1, rtool->group_by);
		GnmExpr const *expr_diff;
		GnmExpr const *expr_prediction;

		dao->offset_row += xdim + 1;
		dao_set_italic (dao, 0, 0, xdim + 7, 0);
		dao_set_cell (dao, 0, 0, _("Constant"));
		dao_set_array_expr (dao, 1, 0, xdim, 1,
				    gnm_expr_new_funcall1
				    (fd_transpose,
				     make_rangeref (-1, - xdim - 1, -1, -2)));
		set_cell_text_row (dao, xdim + 1, 0, _("/Prediction"
						       "/"
						       "/Residual"
						       "/Leverages"
						       "/Internally studentized"
						       "/Externally studentized"
						       "/p-Value"));
		dao_set_cell_expr (dao, xdim + 2, 0, make_cellref (1 - xdim, - 18 - xdim));
		if (rtool->group_by == GNM_TOOL_GROUPED_BY_ROW) {
			dao_set_array_expr (dao, 1, 1, xdim, n_obs,
					    gnm_expr_new_funcall1
					    (fd_transpose,
					     gnm_expr_new_constant (val_1)));
			dao_set_array_expr (dao, xdim + 2, 1, 1, n_obs,
					    gnm_expr_new_funcall1
					    (fd_transpose,
					     gnm_expr_new_constant (val_2)));
		} else {
			dao_set_array_expr (dao, 1, 1, xdim, n_obs,
					    gnm_expr_new_constant (val_1));
			dao_set_array_expr (dao, xdim + 2, 1, 1, n_obs,
					    gnm_expr_new_constant (val_2));
		}

		expr_prediction =  gnm_expr_new_funcall2 (fd_sumproduct,
							  dao_get_rangeref (dao, 1, - 2 - xdim, 1, - 2),
							  gnm_expr_new_funcall1
							  (fd_transpose, make_rangeref
							   (-1 - xdim, 0, -1, 0)));
		expr_diff = gnm_expr_new_binary (make_cellref (-1, 0), GNM_EXPR_OP_SUB, make_cellref (-2, 0));

		for (i = 0; i < n_obs; i++) {
			dao_set_cell_expr (dao, xdim + 1, i + 1, gnm_expr_copy (expr_prediction));
			dao_set_cell_expr (dao, xdim + 3, i + 1, gnm_expr_copy (expr_diff));
			dao_set_cell_expr (dao, 0, i + 1, gnm_expr_new_constant (value_new_int (1)));
		}
		gnm_expr_free (expr_diff);
		gnm_expr_free (expr_prediction);

		if (dao_cell_is_visible (dao, xdim + 4, n_obs)) {
			GnmExpr const *expr_X = dao_get_rangeref (dao, rtool->intercept ? 0 : 1, 1, xdim, n_obs);
			GnmExpr const *expr_diagonal =
				gnm_expr_new_funcall1
				(fd_leverage, expr_X);
			GnmExpr const *expr_var =
				dao_get_cellref (dao, 3, - 6 - xdim);
			GnmExpr const *expr_int_stud =
				gnm_expr_new_binary
				(make_cellref (-2, 0),
				 GNM_EXPR_OP_DIV,
				 gnm_expr_new_funcall1
				 (fd_sqrt,
				  gnm_expr_new_binary
				  (expr_var,
				   GNM_EXPR_OP_MULT,
				   gnm_expr_new_binary
				   (gnm_expr_new_constant (value_new_int (1)),
				    GNM_EXPR_OP_SUB,
				    make_cellref (-1, 0)))));
			GnmExpr const *expr_ext_stud;
			GnmExpr const *expr_p_val_res;

			expr_var = gnm_expr_new_binary
				(gnm_expr_new_binary
				 (dao_get_cellref (dao, 2, - 6 - xdim),
				  GNM_EXPR_OP_SUB,
				  gnm_expr_new_binary
				  (make_cellref (-3, 0),
				   GNM_EXPR_OP_EXP,
				   gnm_expr_new_constant (value_new_int (2)))),
				 GNM_EXPR_OP_DIV,
				 gnm_expr_new_binary
				 (dao_get_cellref (dao, 1, - 6 - xdim),
				  GNM_EXPR_OP_SUB,
				  gnm_expr_new_constant (value_new_int (1))));
			expr_ext_stud = gnm_expr_new_binary
				(make_cellref (-3, 0),
				 GNM_EXPR_OP_DIV,
				 gnm_expr_new_funcall1
				 (fd_sqrt,
				  gnm_expr_new_binary
				  (expr_var,
				   GNM_EXPR_OP_MULT,
				   gnm_expr_new_binary
				   (gnm_expr_new_constant (value_new_int (1)),
				    GNM_EXPR_OP_SUB,
				    make_cellref (-2, 0)))));
			expr_p_val_res = gnm_expr_new_funcall3
				(fd_tdist,
				 gnm_expr_new_funcall1
				 (fd_abs,
				  make_cellref (-1, 0)),
				 gnm_expr_new_binary
				 (dao_get_cellref (dao, 1, - 6 - xdim),
				  GNM_EXPR_OP_SUB,
				  gnm_expr_new_constant (value_new_int (1))),
				 gnm_expr_new_constant (value_new_int (2)));

			dao_set_array_expr (dao, xdim + 4, 1, 1, n_obs, expr_diagonal);
			dao_set_format (dao, xdim + 5, 1, xdim + 6, n_obs, "0.0000");
			dao_set_percent (dao, xdim + 7, 1, xdim + 7, n_obs);
			for (i = 0; i < n_obs; i++){
				dao_set_cell_expr (dao, xdim + 5, i + 1, gnm_expr_copy (expr_int_stud));
				dao_set_cell_expr (dao, xdim + 6, i + 1, gnm_expr_copy (expr_ext_stud));
				dao_set_cell_expr (dao, xdim + 7, i + 1, gnm_expr_copy (expr_p_val_res));
			}
			gnm_expr_free (expr_int_stud);
			gnm_expr_free (expr_ext_stud);
			gnm_expr_free (expr_p_val_res);
		}
	} else {
		value_release (val_1);
		value_release (val_2);
	}

	gnm_func_dec_usage (fd_linest);
	gnm_func_dec_usage (fd_index);
	gnm_func_dec_usage (fd_fdist);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_sqrt);
	gnm_func_dec_usage (fd_tdist);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_tinv);
	gnm_func_dec_usage (fd_transpose);
	if (fd_concatenate != NULL)
		gnm_func_dec_usage (fd_concatenate);
	if (fd_cell != NULL)
		gnm_func_dec_usage (fd_cell);
	if (fd_offset != NULL)
		gnm_func_dec_usage (fd_offset);
	if (fd_sumproduct != NULL)
		gnm_func_dec_usage (fd_sumproduct);
	if (fd_leverage != NULL)
		gnm_func_dec_usage (fd_leverage);

	dao_redraw_respan (dao);

	return FALSE;
}

static gboolean
analysis_tool_regression_simple_engine_run (GnmRegressionTool *rtool, data_analysis_output_t *dao)
{
	GnmGenericBAnalysisTool *gtool = &rtool->parent;
	GnmFunc *fd_linest  = gnm_func_get_and_use ("LINEST");
	GnmFunc *fd_index   = gnm_func_get_and_use ("INDEX");
	GnmFunc *fd_fdist   = gnm_func_get_and_use ("FDIST");
	GnmFunc *fd_rows    = gnm_func_get_and_use ("ROWS");
	GnmFunc *fd_columns = gnm_func_get_and_use ("COLUMNS");

	GSList *inputdata;
	guint row;

	GnmValue *val_dep = value_dup (gtool->base.range_2);
	GnmExpr const *expr_intercept
		= gnm_expr_new_constant (value_new_bool (rtool->intercept));
	GnmExpr const *expr_observ;
	GnmExpr const *expr_val_dep;

	dao_set_italic (dao, 0, 0, 4, 0);
	dao_set_italic (dao, 0, 2, 5, 2);
        set_cell_text_row (dao, 0, 0, rtool->multiple_y ?
			   _("/SUMMARY OUTPUT"
			     "/"
			     "/Independent Variable"
			     "/"
			     "/Observations") :
			   _("/SUMMARY OUTPUT"
			     "/"
			     "/Response Variable"
			     "/"
			     "/Observations"));
        set_cell_text_row (dao, 0, 2, rtool->multiple_y ?
			   _("/Response Variable"
			     "/R^2"
			     "/Slope"
			     "/Intercept"
			     "/F"
			     "/Significance of F") :
			   _("/Independent Variable"
			     "/R^2"
			     "/Slope"
			     "/Intercept"
			     "/F"
			     "/Significance of F"));
	analysis_tools_write_a_label (val_dep, dao,
				      gtool->base.labels, rtool->group_by,
				      3, 0);

	expr_val_dep = gnm_expr_new_constant (val_dep);
	dao_set_cell_expr (dao, 5, 0, gnm_expr_new_binary (gnm_expr_new_funcall1 (fd_rows, gnm_expr_copy (expr_val_dep)),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_funcall1 (fd_columns, gnm_expr_copy (expr_val_dep))));
	expr_observ = dao_get_cellref (dao, 5, 0);

	for (row = 3, inputdata = rtool->indep_vars; inputdata != NULL;
	     inputdata = inputdata->next, row++) {
		GnmValue *val_indep = value_dup (inputdata->data);
		GnmExpr const *expr_linest;

		dao_set_italic (dao, 0, row, 0, row);
		analysis_tools_write_a_label (val_indep, dao,
					      gtool->base.labels, rtool->group_by,
					      0, row);
		expr_linest = rtool->multiple_y ?
			gnm_expr_new_funcall4 (fd_linest,
					       gnm_expr_new_constant (val_indep),
					       gnm_expr_copy (expr_val_dep),
					       gnm_expr_copy (expr_intercept),
					       gnm_expr_new_constant (value_new_bool (TRUE))) :
			gnm_expr_new_funcall4 (fd_linest,
					       gnm_expr_copy (expr_val_dep),
					       gnm_expr_new_constant (val_indep),
					       gnm_expr_copy (expr_intercept),
					       gnm_expr_new_constant (value_new_bool (TRUE)));
		dao_set_cell_array_expr (dao, 1, row,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (3)),
							gnm_expr_new_constant (value_new_int (1))));
		dao_set_cell_array_expr (dao, 4, row,
				 gnm_expr_new_funcall3 (fd_index,
							gnm_expr_copy (expr_linest),
							gnm_expr_new_constant (value_new_int (4)),
							gnm_expr_new_constant (value_new_int (1))));
		dao_set_array_expr (dao, 2, row, 2, 1, expr_linest);

		dao_set_cell_expr (dao, 5, row, gnm_expr_new_funcall3
				   (fd_fdist,
				    make_cellref (-1, 0),
				    gnm_expr_new_constant (value_new_int (1)),
				    gnm_expr_new_binary (gnm_expr_copy (expr_observ),
							 GNM_EXPR_OP_SUB,
							 gnm_expr_new_constant (value_new_int (2)))));

	}

	gnm_expr_free (expr_intercept);
	gnm_expr_free (expr_observ);
	gnm_expr_free (expr_val_dep);

	gnm_func_dec_usage (fd_fdist);
	gnm_func_dec_usage (fd_linest);
	gnm_func_dec_usage (fd_index);
	gnm_func_dec_usage (fd_rows);
	gnm_func_dec_usage (fd_columns);

	dao_redraw_respan (dao);

	return FALSE;
}
