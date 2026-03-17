/*
 * analysis-sign-test.c:
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
#include <tools/analysis-sign-test.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <sheet.h>


G_DEFINE_TYPE (GnmSignTestTool, gnm_sign_test_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

enum {
	SIGN_TEST_PROP_0,
	SIGN_TEST_PROP_MEDIAN,
	SIGN_TEST_PROP_ALPHA
};

static void
gnm_sign_test_tool_set_property (GObject *object, guint property_id,
				 GValue const *value, GParamSpec *pspec)
{
	GnmSignTestTool *tool = GNM_SIGN_TEST_TOOL (object);

	switch (property_id) {
	case SIGN_TEST_PROP_MEDIAN:
		tool->median = g_value_get_double (value);
		break;
	case SIGN_TEST_PROP_ALPHA:
		tool->alpha = g_value_get_double (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_sign_test_tool_get_property (GObject *object, guint property_id,
				 GValue *value, GParamSpec *pspec)
{
	GnmSignTestTool *tool = GNM_SIGN_TEST_TOOL (object);

	switch (property_id) {
	case SIGN_TEST_PROP_MEDIAN:
		g_value_set_double (value, tool->median);
		break;
	case SIGN_TEST_PROP_ALPHA:
		g_value_set_double (value, tool->alpha);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_sign_test_tool_init (GnmSignTestTool *tool)
{
	tool->median = 0.0;
	tool->alpha = 0.05;
}

static gboolean
gnm_sign_test_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	analysis_tool_prepare_input_range (gtool);
	dao_adjust (dao, 1 + g_slist_length (gtool->base.input), 8);
	return FALSE;
}

static char *
gnm_sign_test_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Sign Test (%s)"));
}

static gboolean
gnm_sign_test_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Sign Test"));
	return FALSE;
}

static gboolean
gnm_sign_test_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Sign Test"));
}

static gboolean
gnm_sign_test_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmSignTestTool *stool = GNM_SIGN_TEST_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &stool->parent;
	guint     col;
	GSList *data = gtool->base.input;
	gboolean first = TRUE;
	GnmExpr const *expr;
	GnmExpr const *expr_neg;
	GnmExpr const *expr_pos;
	GnmExpr const *expr_isnumber;
	GnmFunc *fd_median = gnm_func_get_and_use ("MEDIAN");
	GnmFunc *fd_if = gnm_func_get_and_use ("IF");
	GnmFunc *fd_sum = gnm_func_get_and_use ("SUM");
	GnmFunc *fd_min = gnm_func_get_and_use ("MIN");
	GnmFunc *fd_binomdist = gnm_func_get_and_use ("BINOMDIST");
	GnmFunc *fd_isnumber = gnm_func_get_and_use ("ISNUMBER");
	GnmFunc *fd_iferror = gnm_func_get_and_use ("IFERROR");

	dao_set_italic (dao, 0, 0, 0, 9);
	set_cell_text_col (dao, 0, 0, _("/Sign Test"
					"/Median"
					"/Predicted Median"
					"/Test Statistic"
					"/N"
					"/\xce\xb1"
					"/P(T\xe2\x89\xa4t) one-tailed"
					"/P(T\xe2\x89\xa4t) two-tailed"));

	for (col = 0; data != NULL; data = data->next, col++) {
		GnmValue *val_org = value_dup (data->data);
		GnmExpr const *expr_org;

		/* Note that analysis_tools_write_label may modify val_org */
		dao_set_italic (dao, col + 1, 0, col+1, 0);
		analysis_tools_write_label (gtool, val_org, dao, col + 1, 0, col + 1);
		expr_org = gnm_expr_new_constant (val_org);

		if (first) {
			dao_set_cell_float (dao, col + 1, 2, stool->median);
			dao_set_cell_float (dao, col + 1, 5, stool->alpha);
			first = FALSE;
		} else {
			dao_set_cell_expr (dao, col + 1, 2, make_cellref (-1,0));
			dao_set_cell_expr (dao, col + 1, 5, make_cellref (-1,0));
		}

		expr_isnumber = gnm_expr_new_funcall3
			(fd_if, gnm_expr_new_funcall1
			 (fd_isnumber, gnm_expr_copy (expr_org)),
			 gnm_expr_new_constant (value_new_int (1)),
			 gnm_expr_new_constant (value_new_int (0)));

		expr = gnm_expr_new_funcall1
			(fd_median,
			 gnm_expr_copy (expr_org));
		dao_set_cell_expr (dao, col + 1, 1, expr);

		expr_neg = gnm_expr_new_funcall1
			(fd_sum,
			 gnm_expr_new_binary
			 (gnm_expr_copy (expr_isnumber), GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall2
			  (fd_iferror,
			   gnm_expr_new_funcall3
			   (fd_if, gnm_expr_new_binary (gnm_expr_copy (expr_org),
							GNM_EXPR_OP_LT, make_cellref (0,-1)),
			    gnm_expr_new_constant (value_new_int (1)),
			    gnm_expr_new_constant (value_new_int (0))),
			   gnm_expr_new_constant (value_new_int (0)))));
		expr_pos = gnm_expr_new_funcall1
			(fd_sum,
			 gnm_expr_new_binary
			 (gnm_expr_copy (expr_isnumber), GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall2
			  (fd_iferror,
			   gnm_expr_new_funcall3
			   (fd_if, gnm_expr_new_binary (gnm_expr_copy (expr_org),
							GNM_EXPR_OP_GT, make_cellref (0,-1)),
			    gnm_expr_new_constant (value_new_int (1)),
			    gnm_expr_new_constant (value_new_int (0))),
			   gnm_expr_new_constant (value_new_int (0)))));
		expr = gnm_expr_new_funcall2
			(fd_min, expr_neg, expr_pos);
		dao_set_cell_array_expr (dao, col + 1, 3, expr);

		expr = gnm_expr_new_funcall1
			(fd_sum, gnm_expr_new_binary
			 (expr_isnumber, GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall2
			  (fd_iferror, gnm_expr_new_funcall3
			   (fd_if, gnm_expr_new_binary (expr_org,
							GNM_EXPR_OP_NOT_EQUAL, make_cellref (0,-2)),
			    gnm_expr_new_constant (value_new_int (1)),
			    gnm_expr_new_constant (value_new_int (0))),
			   gnm_expr_new_constant (value_new_int (0)))));
		dao_set_cell_array_expr (dao, col + 1, 4, expr);

		expr = gnm_expr_new_funcall4 (fd_binomdist, make_cellref (0,-3), make_cellref (0,-2),
					      gnm_expr_new_constant (value_new_float (0.5)),
					      gnm_expr_new_constant (value_new_bool (TRUE)));
		dao_set_cell_array_expr (dao, col + 1, 6, expr);

		expr = gnm_expr_new_binary (gnm_expr_new_constant (value_new_int (2)),
					    GNM_EXPR_OP_MULT, make_cellref (0,-1));
		dao_set_cell_array_expr (dao, col + 1, 7, expr);
	}

	gnm_func_dec_usage (fd_median);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_min);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_binomdist);
	gnm_func_dec_usage (fd_isnumber);
	gnm_func_dec_usage (fd_iferror);

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_sign_test_tool_class_init (GnmSignTestToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_sign_test_tool_set_property;
	gobject_class->get_property = gnm_sign_test_tool_get_property;

	at_class->update_dao = gnm_sign_test_tool_update_dao;
	at_class->update_descriptor = gnm_sign_test_tool_update_descriptor;
	at_class->prepare_output_range = gnm_sign_test_tool_prepare_output_range;
	at_class->format_output_range = gnm_sign_test_tool_format_output_range;
	at_class->perform_calc = gnm_sign_test_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		SIGN_TEST_PROP_MEDIAN,
		g_param_spec_double ("median", NULL, NULL,
			-GNM_MAX, GNM_MAX, 0.0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		SIGN_TEST_PROP_ALPHA,
		g_param_spec_double ("alpha", NULL, NULL,
			0.0, 1.0, 0.05, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_sign_test_tool_new (void)
{
	return g_object_new (GNM_TYPE_SIGN_TEST_TOOL, NULL);
}

/********************************************************************/

G_DEFINE_TYPE (GnmSignTestTwoTool, gnm_sign_test_two_tool, GNM_TYPE_GENERIC_B_ANALYSIS_TOOL)

static void
gnm_sign_test_two_tool_init (G_GNUC_UNUSED GnmSignTestTwoTool *tool)
{
}

static gboolean
gnm_sign_test_two_tool_update_dao (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_adjust (dao, 3, 8);
	return FALSE;
}

static char *
gnm_sign_test_two_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Sign Test (%s)"));
}

static gboolean
gnm_sign_test_two_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Sign Test"));
	return FALSE;
}

static gboolean
gnm_sign_test_two_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Sign Test"));
}

static gboolean
gnm_sign_test_two_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmSignTestTwoTool *stool = GNM_SIGN_TEST_TWO_TOOL (tool);
	GnmGenericBAnalysisTool *gtool = &stool->parent;
	GnmValue *val_1;
	GnmValue *val_2;
	GnmExpr const *expr_1;
	GnmExpr const *expr_2;
	GnmExpr const *expr;
	GnmExpr const *expr_diff;
	GnmExpr const *expr_neg;
	GnmExpr const *expr_pos;
	GnmExpr const *expr_isnumber_1;
	GnmExpr const *expr_isnumber_2;
	GnmFunc *fd_median = gnm_func_get_and_use ("MEDIAN");
	GnmFunc *fd_if = gnm_func_get_and_use ("IF");
	GnmFunc *fd_sum = gnm_func_get_and_use ("SUM");
	GnmFunc *fd_min = gnm_func_get_and_use ("MIN");
	GnmFunc *fd_binomdist = gnm_func_get_and_use ("BINOMDIST");
	GnmFunc *fd_isnumber = gnm_func_get_and_use ("ISNUMBER");
	GnmFunc *fd_iferror = gnm_func_get_and_use ("IFERROR");

	dao_set_italic (dao, 0, 0, 0, 9);
	set_cell_text_col (dao, 0, 0, _("/Sign Test"
					"/Median"
					"/Predicted Difference"
					"/Test Statistic"
					"/N"
					"/\xce\xb1"
					"/P(T\xe2\x89\xa4t) one-tailed"
					"/P(T\xe2\x89\xa4t) two-tailed"));

	val_1 = value_dup (gtool->base.range_1);
	val_2 = value_dup (gtool->base.range_2);

	/* Labels */
	dao_set_italic (dao, 1, 0, 2, 0);
	analysis_tools_write_variable_label (val_1, dao, 1, 0,
					  gtool->base.labels, 1);
	analysis_tools_write_variable_label (val_2, dao, 2, 0,
					  gtool->base.labels, 2);

	expr_1 = gnm_expr_new_constant (value_dup (val_1));
	expr_2 = gnm_expr_new_constant (value_dup (val_2));

	dao_set_cell_float (dao, 1, 2, stool->median);
	dao_set_cell_float (dao, 1, 5, gtool->base.alpha);

	expr = gnm_expr_new_funcall1
		(fd_median,
		 gnm_expr_copy (expr_1));
	dao_set_cell_expr (dao, 1, 1, expr);

	expr = gnm_expr_new_funcall1
		(fd_median,
		 gnm_expr_copy (expr_2));
	dao_set_cell_expr (dao, 2, 1, expr);

	expr_diff = gnm_expr_new_binary (gnm_expr_copy (expr_1),
					 GNM_EXPR_OP_SUB,
					 gnm_expr_copy (expr_2));

	expr_isnumber_1 = gnm_expr_new_funcall3
		(fd_if, gnm_expr_new_funcall1
		 (fd_isnumber, expr_1),
		 gnm_expr_new_constant (value_new_int (1)),
		 gnm_expr_new_constant (value_new_int (0)));
	expr_isnumber_2 = gnm_expr_new_funcall3
		(fd_if, gnm_expr_new_funcall1
		 (fd_isnumber, expr_2),
		 gnm_expr_new_constant (value_new_int (1)),
		 gnm_expr_new_constant (value_new_int (0)));

	expr_neg = gnm_expr_new_funcall1
		(fd_sum,
		 gnm_expr_new_binary
		 (gnm_expr_copy (expr_isnumber_1), GNM_EXPR_OP_MULT,
		  gnm_expr_new_binary
		  (gnm_expr_copy (expr_isnumber_1), GNM_EXPR_OP_MULT,
		   gnm_expr_new_funcall2
		   (fd_iferror,
		    gnm_expr_new_funcall3
		    (fd_if, gnm_expr_new_binary (gnm_expr_copy (expr_diff),
						 GNM_EXPR_OP_LT, make_cellref (0,-1)),
		     gnm_expr_new_constant (value_new_int (1)),
		     gnm_expr_new_constant (value_new_int (0))),
		    gnm_expr_new_constant (value_new_int (0))))));
	expr_pos = gnm_expr_new_funcall1
		(fd_sum,
		 gnm_expr_new_binary
		 (gnm_expr_copy (expr_isnumber_1), GNM_EXPR_OP_MULT,
		  gnm_expr_new_binary
		  (gnm_expr_copy (expr_isnumber_1), GNM_EXPR_OP_MULT,
		   gnm_expr_new_funcall2
		   (fd_iferror,
		    gnm_expr_new_funcall3
		    (fd_if, gnm_expr_new_binary (gnm_expr_copy (expr_diff),
						 GNM_EXPR_OP_GT, make_cellref (0,-1)),
		     gnm_expr_new_constant (value_new_int (1)),
		     gnm_expr_new_constant (value_new_int (0))),
		    gnm_expr_new_constant (value_new_int (0))))));
	expr = gnm_expr_new_funcall2
		(fd_min, expr_neg, expr_pos);
	dao_set_cell_array_expr (dao, 1, 3, expr);

	expr = gnm_expr_new_funcall1
		(fd_sum, gnm_expr_new_binary
		 (expr_isnumber_1, GNM_EXPR_OP_MULT,
		  gnm_expr_new_binary
		  (expr_isnumber_2, GNM_EXPR_OP_MULT,
		   gnm_expr_new_funcall2
		   (fd_iferror, gnm_expr_new_funcall3
		    (fd_if, gnm_expr_new_binary (expr_diff,
						 GNM_EXPR_OP_NOT_EQUAL, make_cellref (0,-2)),
		     gnm_expr_new_constant (value_new_int (1)),
		     gnm_expr_new_constant (value_new_int (0))),
		    gnm_expr_new_constant (value_new_int (0))))));
	dao_set_cell_array_expr (dao, 1, 4, expr);

	expr = gnm_expr_new_funcall4 (fd_binomdist, make_cellref (0,-3), make_cellref (0,-2),
				      gnm_expr_new_constant (value_new_float (0.5)),
				      gnm_expr_new_constant (value_new_bool (TRUE)));
	dao_set_cell_array_expr (dao, 1, 6,
				 gnm_expr_new_funcall2
				 (fd_min,
				  gnm_expr_copy (expr),
				  gnm_expr_new_binary
				  (gnm_expr_new_constant (value_new_int (1)),
				   GNM_EXPR_OP_SUB,
				   expr)));

	expr = gnm_expr_new_binary (gnm_expr_new_constant (value_new_int (2)),
				    GNM_EXPR_OP_MULT, make_cellref (0,-1));
	dao_set_cell_array_expr (dao, 1, 7, expr);

	gnm_func_dec_usage (fd_median);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_min);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_binomdist);
	gnm_func_dec_usage (fd_isnumber);
	gnm_func_dec_usage (fd_iferror);

	value_release (val_1);
	value_release (val_2);

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_sign_test_two_tool_class_init (GnmSignTestTwoToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_sign_test_two_tool_update_dao;
	at_class->update_descriptor = gnm_sign_test_two_tool_update_descriptor;
	at_class->prepare_output_range = gnm_sign_test_two_tool_prepare_output_range;
	at_class->format_output_range = gnm_sign_test_two_tool_format_output_range;
	at_class->perform_calc = gnm_sign_test_two_tool_perform_calc;
}

GnmAnalysisTool *
gnm_sign_test_two_tool_new (void)
{
	return g_object_new (GNM_TYPE_SIGN_TEST_TWO_TOOL, NULL);
}
