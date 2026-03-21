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
#include <sheet.h>


G_DEFINE_TYPE (GnmOneMeanTestTool, gnm_one_mean_test_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

enum {
	ONE_MEAN_TEST_PROP_0,
	ONE_MEAN_TEST_PROP_MEAN,
	ONE_MEAN_TEST_PROP_ALPHA
};

static void
gnm_one_mean_test_tool_set_property (GObject      *obj,
				     guint         property_id,
				     GValue const *value,
				     GParamSpec   *pspec)
{
	GnmOneMeanTestTool *tool = GNM_ONE_MEAN_TEST_TOOL (obj);

	switch (property_id) {
	case ONE_MEAN_TEST_PROP_MEAN:
		tool->mean = g_value_get_double (value);
		break;
	case ONE_MEAN_TEST_PROP_ALPHA:
		tool->alpha = g_value_get_double (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
gnm_one_mean_test_tool_get_property (GObject    *obj,
				     guint       property_id,
				     GValue     *value,
				     GParamSpec *pspec)
{
	GnmOneMeanTestTool *tool = GNM_ONE_MEAN_TEST_TOOL (obj);

	switch (property_id) {
	case ONE_MEAN_TEST_PROP_MEAN:
		g_value_set_double (value, tool->mean);
		break;
	case ONE_MEAN_TEST_PROP_ALPHA:
		g_value_set_double (value, tool->alpha);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
		break;
	}
}

static void
gnm_one_mean_test_tool_init (GnmOneMeanTestTool *tool)
{
	tool->mean = 0.0;
	tool->alpha = 0.05;
}

static void
gnm_one_mean_test_tool_finalize (GObject *obj)
{
	G_OBJECT_CLASS (gnm_one_mean_test_tool_parent_class)->finalize (obj);
}

static gboolean
gnm_one_mean_test_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	analysis_tool_prepare_input_range (gtool);
	dao_adjust (dao, 1 + g_slist_length (gtool->base.input), 10);
	return FALSE;
}

static char *
gnm_one_mean_test_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Student-t Test (%s)"));
}

static gboolean
gnm_one_mean_test_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	dao_prepare_output (wbc, dao, _("Student-t Test"));
	return FALSE;
}

static gboolean
gnm_one_mean_test_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	return dao_format_output (wbc, dao, _("Student-t Test"));
}

static gboolean
gnm_one_mean_test_tool_perform_calc (GnmAnalysisTool *tool, WorkbookControl *wbc, data_analysis_output_t *dao)
{
	GnmOneMeanTestTool *otool = GNM_ONE_MEAN_TEST_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &otool->parent;
	guint    col;
	GSList  *data = gtool->base.input;
	gboolean first = TRUE;
	GnmFunc *fd_count = gnm_func_get_and_use ("COUNT");
	GnmFunc *fd_mean = gnm_func_get_and_use ("AVERAGE");
	GnmFunc *fd_var = gnm_func_get_and_use ("VAR");
	GnmFunc *fd_sqrt = gnm_func_get_and_use ("SQRT");
	GnmFunc *fd_abs = gnm_func_get_and_use ("ABS");
	GnmFunc *fd_tdist = gnm_func_get_and_use ("TDIST");
	GnmFunc *fd_iferror = gnm_func_get_and_use ("IFERROR");

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
		analysis_tools_write_label (gtool, val_org, dao, col, 0, col);
		expr_org = gnm_expr_new_constant (val_org);
		expr_range_clean = gnm_expr_new_funcall2
			(fd_iferror, gnm_expr_copy (expr_org), gnm_expr_new_constant (value_new_string ("")));

		if (first) {
			dao_set_cell_float (dao, col, 3, otool->mean);
			dao_set_cell_float (dao, col, 7, otool->alpha);
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

static void
gnm_one_mean_test_tool_class_init (GnmOneMeanTestToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_one_mean_test_tool_set_property;
	gobject_class->get_property = gnm_one_mean_test_tool_get_property;
	gobject_class->finalize = gnm_one_mean_test_tool_finalize;
	at_class->update_dao = gnm_one_mean_test_tool_update_dao;
	at_class->update_descriptor = gnm_one_mean_test_tool_update_descriptor;
	at_class->prepare_output_range = gnm_one_mean_test_tool_prepare_output_range;
	at_class->format_output_range = gnm_one_mean_test_tool_format_output_range;
	at_class->perform_calc = gnm_one_mean_test_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		ONE_MEAN_TEST_PROP_MEAN,
		g_param_spec_double ("mean", NULL, NULL,
				     -G_MAXDOUBLE, G_MAXDOUBLE, 0.0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		ONE_MEAN_TEST_PROP_ALPHA,
		g_param_spec_double ("alpha", NULL, NULL,
				     0.0, 1.0, 0.05, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_one_mean_test_tool_new (void)
{
	return g_object_new (GNM_TYPE_ONE_MEAN_TEST_TOOL, NULL);
}
