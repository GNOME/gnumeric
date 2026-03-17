/*
 * analysis-signed-rank-test.c:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2010, 2016 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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
#include <tools/analysis-signed-rank-test.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>

#include <sheet.h>


static inline GnmExpr const *
make_int (int n)
{
	return gnm_expr_new_constant (value_new_int (n));
}
static inline GnmExpr const *
make_float (gnm_float x)
{
	return gnm_expr_new_constant (value_new_float (x));
}


G_DEFINE_TYPE (GnmSignedRankTestTool, gnm_signed_rank_test_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

enum {
	SIGNED_RANK_TEST_PROP_0,
	SIGNED_RANK_TEST_PROP_MEDIAN,
	SIGNED_RANK_TEST_PROP_ALPHA
};

static void
gnm_signed_rank_test_tool_set_property (GObject *object, guint property_id,
					GValue const *value, GParamSpec *pspec)
{
	GnmSignedRankTestTool *tool = GNM_SIGNED_RANK_TEST_TOOL (object);

	switch (property_id) {
	case SIGNED_RANK_TEST_PROP_MEDIAN:
		tool->median = g_value_get_double (value);
		break;
	case SIGNED_RANK_TEST_PROP_ALPHA:
		tool->alpha = g_value_get_double (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_signed_rank_test_tool_get_property (GObject *object, guint property_id,
					GValue *value, GParamSpec *pspec)
{
	GnmSignedRankTestTool *tool = GNM_SIGNED_RANK_TEST_TOOL (object);

	switch (property_id) {
	case SIGNED_RANK_TEST_PROP_MEDIAN:
		g_value_set_double (value, tool->median);
		break;
	case SIGNED_RANK_TEST_PROP_ALPHA:
		g_value_set_double (value, tool->alpha);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_signed_rank_test_tool_init (GnmSignedRankTestTool *tool)
{
	tool->median = 0.0;
	tool->alpha = 0.05;
}

static gboolean
gnm_signed_rank_test_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmGenericAnalysisTool *gtool = GNM_GENERIC_ANALYSIS_TOOL (tool);
	analysis_tool_prepare_input_range (gtool);
	dao_adjust (dao, 1 + g_slist_length (gtool->base.input), 10);
	return FALSE;
}

static char *
gnm_signed_rank_test_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Wilcoxon Signed Rank Test (%s)"));
}

static gboolean
gnm_signed_rank_test_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Wilcoxon Signed Rank Test"));
	return FALSE;
}

static gboolean
gnm_signed_rank_test_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Wilcoxon Signed Rank Test"));
}

static gboolean
gnm_signed_rank_test_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmSignedRankTestTool *stool = GNM_SIGNED_RANK_TEST_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &stool->parent;
	guint     col;
	GSList *data = gtool->base.input;
	gboolean first = TRUE;

	GnmExpr const *expr;
	GnmExpr const *expr_isnumber;
	GnmExpr const *expr_diff;
	GnmExpr const *expr_expect;
	GnmExpr const *expr_var;
	GnmExpr const *expr_abs;
	GnmExpr const *expr_big;

	GnmFunc *fd_median    = gnm_func_get_and_use ("MEDIAN");
	GnmFunc *fd_if        = gnm_func_get_and_use ("IF");
	GnmFunc *fd_sum       = gnm_func_get_and_use ("SUM");
	GnmFunc *fd_min       = gnm_func_get_and_use ("MIN");
	GnmFunc *fd_normdist  = gnm_func_get_and_use ("NORMDIST");
	GnmFunc *fd_isnumber  = gnm_func_get_and_use ("ISNUMBER");
	GnmFunc *fd_iferror   = gnm_func_get_and_use ("IFERROR");
	GnmFunc *fd_rank      = gnm_func_get_and_use ("RANK.AVG");
	GnmFunc *fd_abs       = gnm_func_get_and_use ("ABS");
	GnmFunc *fd_sqrt      = gnm_func_get_and_use ("SQRT");
	GnmFunc *fd_max       = gnm_func_get_and_use ("MAX");

	dao_set_italic (dao, 0, 0, 0, 9);
	set_cell_text_col (dao, 0, 0, _("/Wilcoxon Signed Rank Test"
					"/Median"
					"/Predicted Median"
					"/N"
					"/S\xe2\x88\x92"
					"/S+"
					"/Test Statistic"
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
			dao_set_cell_float (dao, col + 1, 7, stool->alpha);
			first = FALSE;
		} else {
			dao_set_cell_expr (dao, col + 1, 2, make_cellref (-1,0));
			dao_set_cell_expr (dao, col + 1, 7, make_cellref (-1,0));
		}

		expr_isnumber = gnm_expr_new_funcall3
			(fd_if, gnm_expr_new_funcall1
			 (fd_isnumber, gnm_expr_copy (expr_org)),
			 make_int (1),
			 make_int (0));

		expr = gnm_expr_new_funcall1
			(fd_median,
			 gnm_expr_copy (expr_org));
		dao_set_cell_expr (dao, col + 1, 1, expr);

		expr_diff = gnm_expr_new_binary
			(gnm_expr_copy (expr_org), GNM_EXPR_OP_SUB, make_cellref (0,-2));
		expr_abs = gnm_expr_new_funcall1
			(fd_abs, gnm_expr_copy (expr_diff));
		expr_big = gnm_expr_new_binary
			(gnm_expr_new_funcall1
			 (fd_max, gnm_expr_copy (expr_abs)),
			 GNM_EXPR_OP_ADD,
			 make_int (1));
		expr = gnm_expr_new_funcall3
			(fd_if,
			 gnm_expr_new_funcall1
			 (fd_isnumber, gnm_expr_copy (expr_org)),
			 gnm_expr_new_funcall3
			 (fd_if,
			  gnm_expr_new_binary
			  (gnm_expr_copy (expr_org),
			   GNM_EXPR_OP_EQUAL,
			   make_cellref (0,-2)),
			  gnm_expr_copy (expr_big),
			  expr_abs),
			 expr_big);
		expr = gnm_expr_new_funcall3
			(fd_rank,
			 gnm_expr_new_unary (GNM_EXPR_OP_UNARY_NEG,
					     expr_diff),
			 expr,
			 make_int (1));

		dao_set_cell_array_expr
			(dao, col + 1, 4,
			 gnm_expr_new_funcall1
			 (fd_sum,
			  gnm_expr_new_binary
			  (gnm_expr_copy (expr_isnumber),
			   GNM_EXPR_OP_MULT,
			   gnm_expr_new_funcall3
			   (fd_if,
			    gnm_expr_new_binary
			    (gnm_expr_copy (expr_org),
			     GNM_EXPR_OP_LT,
			     make_cellref (0,-2)),
			    expr,
			    make_int (0)))));

		expr = gnm_expr_new_funcall1
			(fd_sum, gnm_expr_new_binary
			 (expr_isnumber, GNM_EXPR_OP_MULT,
			  gnm_expr_new_funcall2
			  (fd_iferror, gnm_expr_new_funcall3
			   (fd_if, gnm_expr_new_binary (expr_org,
							GNM_EXPR_OP_NOT_EQUAL, make_cellref (0,-1)),
			    make_int (1),
			    make_int (0)),
			   make_int (0))));
		dao_set_cell_array_expr (dao, col + 1, 3, expr);

		dao_set_cell_expr (dao, col + 1, 5,
				   gnm_expr_new_binary
				   (gnm_expr_new_binary
				    (gnm_expr_new_binary
				     (make_cellref (0,-2),
				      GNM_EXPR_OP_MULT,
				      gnm_expr_new_binary
				      (make_cellref (0,-2),
				       GNM_EXPR_OP_ADD,
				       make_int (1))),
				     GNM_EXPR_OP_DIV,
				     make_int (2)),
				    GNM_EXPR_OP_SUB,
				    make_cellref (0,-1)));
		dao_set_cell_expr (dao, col + 1, 6,
				   gnm_expr_new_funcall2
				   (fd_min, make_cellref (0,-1), make_cellref (0,-2)));

		expr_expect = gnm_expr_new_binary
			  (gnm_expr_new_binary
			   (make_cellref (0,-5),
			   GNM_EXPR_OP_MULT,
			    gnm_expr_new_binary
			    (make_cellref (0,-5),
			     GNM_EXPR_OP_ADD,
			     make_int (1))),
			   GNM_EXPR_OP_DIV,
			   make_int (4));
		expr_var = gnm_expr_new_binary
			(gnm_expr_new_binary
			 (gnm_expr_copy (expr_expect),
			  GNM_EXPR_OP_MULT,
			  gnm_expr_new_binary
			  (gnm_expr_new_binary
			   (make_int (2),
			    GNM_EXPR_OP_MULT,
			    make_cellref (0,-5)),
			   GNM_EXPR_OP_ADD,
			   make_int (1))),
			 GNM_EXPR_OP_DIV,
			 make_int (6));
		expr = gnm_expr_new_funcall4
			(fd_normdist, gnm_expr_new_binary
			 (make_cellref (0,-2),
			  GNM_EXPR_OP_ADD,
			  make_float (0.5)),
			 expr_expect,
			 gnm_expr_new_funcall1 (fd_sqrt, expr_var),
			 gnm_expr_new_constant (value_new_bool (TRUE)));
		dao_set_cell_expr (dao, col + 1, 8,
				   gnm_expr_new_funcall3
				   (fd_if,
				    gnm_expr_new_binary
				    (make_cellref (0,-5),
				     GNM_EXPR_OP_LT,
				     make_int (12)),
				    gnm_expr_new_constant (value_new_error_NA (NULL)),
				    expr));
		dao_set_cell_comment (dao,  col + 1, 8,
				      _("This p-value is calculated by a normal approximation.\n"
					"It is only valid if the sample size is at least 12."));

		expr = gnm_expr_new_binary (make_int (2),
					    GNM_EXPR_OP_MULT, make_cellref (0,-1));
		dao_set_cell_expr (dao, col + 1, 9, expr);
	}

	gnm_func_dec_usage (fd_median);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_min);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_normdist);
	gnm_func_dec_usage (fd_isnumber);
	gnm_func_dec_usage (fd_iferror);
	gnm_func_dec_usage (fd_rank);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_sqrt);
	gnm_func_dec_usage (fd_max);

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_signed_rank_test_tool_class_init (GnmSignedRankTestToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->set_property = gnm_signed_rank_test_tool_set_property;
	gobject_class->get_property = gnm_signed_rank_test_tool_get_property;

	at_class->update_dao = gnm_signed_rank_test_tool_update_dao;
	at_class->update_descriptor = gnm_signed_rank_test_tool_update_descriptor;
	at_class->prepare_output_range = gnm_signed_rank_test_tool_prepare_output_range;
	at_class->format_output_range = gnm_signed_rank_test_tool_format_output_range;
	at_class->perform_calc = gnm_signed_rank_test_tool_perform_calc;

	g_object_class_install_property (gobject_class,
		SIGNED_RANK_TEST_PROP_MEDIAN,
		g_param_spec_double ("median", NULL, NULL,
			-GNM_MAX, GNM_MAX, 0.0, G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
		SIGNED_RANK_TEST_PROP_ALPHA,
		g_param_spec_double ("alpha", NULL, NULL,
			0.0, 1.0, 0.05, G_PARAM_READWRITE));
}

GnmAnalysisTool *
gnm_signed_rank_test_tool_new (void)
{
	return g_object_new (GNM_TYPE_SIGNED_RANK_TEST_TOOL, NULL);
}

/********************************************************************/

G_DEFINE_TYPE (GnmSignedRankTestTwoTool, gnm_signed_rank_test_two_tool, GNM_TYPE_GENERIC_B_ANALYSIS_TOOL)

static void
gnm_signed_rank_test_two_tool_init (G_GNUC_UNUSED GnmSignedRankTestTwoTool *tool)
{
}

static gboolean
gnm_signed_rank_test_two_tool_update_dao (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_adjust (dao, 3, 10);
	return FALSE;
}

static char *
gnm_signed_rank_test_two_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Wilcoxon Signed Rank Test (%s)"));
}

static gboolean
gnm_signed_rank_test_two_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Wilcoxon Signed Rank Test"));
	return FALSE;
}

static gboolean
gnm_signed_rank_test_two_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Wilcoxon Signed Rank Test"));
}

static gboolean
gnm_signed_rank_test_two_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmSignedRankTestTwoTool *stool = GNM_SIGNED_RANK_TEST_TWO_TOOL (tool);
	GnmGenericBAnalysisTool *gtool = &stool->parent;
	GnmValue *val_1;	GnmValue *val_2;

	GnmExpr const *expr_1;
	GnmExpr const *expr_2;

	GnmExpr const *expr;
	GnmExpr const *expr_diff;
	GnmExpr const *expr_diff_pred;
	GnmExpr const *expr_isnumber_1;
	GnmExpr const *expr_isnumber_2;
	GnmExpr const *expr_isnumber;
	GnmExpr const *expr_expect;
	GnmExpr const *expr_var;
	GnmExpr const *expr_abs;
	GnmExpr const *expr_big;

	GnmFunc *fd_median    = gnm_func_get_and_use ("MEDIAN");
	GnmFunc *fd_if        = gnm_func_get_and_use ("IF");
	GnmFunc *fd_sum       = gnm_func_get_and_use ("SUM");
	GnmFunc *fd_min       = gnm_func_get_and_use ("MIN");
	GnmFunc *fd_normdist  = gnm_func_get_and_use ("NORMDIST");
	GnmFunc *fd_isnumber  = gnm_func_get_and_use ("ISNUMBER");
	GnmFunc *fd_iferror   = gnm_func_get_and_use ("IFERROR");
	GnmFunc *fd_rank      = gnm_func_get_and_use ("RANK.AVG");
	GnmFunc *fd_abs       = gnm_func_get_and_use ("ABS");
	GnmFunc *fd_sqrt      = gnm_func_get_and_use ("SQRT");
	GnmFunc *fd_max       = gnm_func_get_and_use ("MAX");

	dao_set_italic (dao, 0, 0, 0, 10);
	set_cell_text_col (dao, 0, 0, _("/Wilcoxon Signed Rank Test"
					"/Median"
					"/Observed Median Difference"
					"/Predicted Median Difference"
					"/N"
					"/S\xe2\x88\x92"
					"/S+"
					"/Test Statistic"
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

	dao_set_cell_float (dao, 1, 3, stool->median);
	dao_set_cell_float (dao, 1, 8, gtool->base.alpha);

	expr_isnumber_1 = gnm_expr_new_funcall3
		(fd_if, gnm_expr_new_funcall1
		 (fd_isnumber, gnm_expr_copy (expr_1)),
		 make_int (1),
		 make_int (0));
	expr_isnumber_2 = gnm_expr_new_funcall3
		(fd_if, gnm_expr_new_funcall1
		 (fd_isnumber, gnm_expr_copy (expr_2)),
		 make_int (1),
		 make_int (0));
	expr_isnumber = gnm_expr_new_binary
		(expr_isnumber_1,
		 GNM_EXPR_OP_MULT,
		 expr_isnumber_2);

	expr = gnm_expr_new_funcall1
		(fd_median,
		 gnm_expr_new_funcall3
		 (fd_if,
		  gnm_expr_new_binary
		  (gnm_expr_copy (expr_isnumber),
		   GNM_EXPR_OP_EQUAL,
		   make_int (1)),
		  gnm_expr_copy (expr_1),
		  gnm_expr_new_constant (value_new_string (""))));
	dao_set_cell_array_expr (dao, 1, 1, expr);

	expr = gnm_expr_new_funcall1
		(fd_median,
		 gnm_expr_new_funcall3
		 (fd_if,
		  gnm_expr_new_binary
		  (gnm_expr_copy (expr_isnumber),
		   GNM_EXPR_OP_EQUAL,
		   make_int (1)),
		  gnm_expr_copy (expr_2),
		  gnm_expr_new_constant (value_new_string (""))));
	dao_set_cell_array_expr (dao, 2, 1, expr);

	expr_diff = gnm_expr_new_binary (gnm_expr_copy (expr_1),
					 GNM_EXPR_OP_SUB,
					 gnm_expr_copy (expr_2));
	dao_set_cell_array_expr
		(dao, 1, 2,
		 gnm_expr_new_funcall1
		 (fd_median,
		  gnm_expr_new_funcall3
		  (fd_if,
		   gnm_expr_new_binary
		   (gnm_expr_copy (expr_isnumber),
		    GNM_EXPR_OP_EQUAL,
		    make_int (1)),
		   gnm_expr_copy (expr_diff),
		   gnm_expr_new_constant (value_new_string ("")))));

	expr = gnm_expr_new_funcall1
		(fd_sum, gnm_expr_new_binary
		 (gnm_expr_copy (expr_isnumber),
		  GNM_EXPR_OP_MULT,
		   gnm_expr_new_funcall2
		   (fd_iferror, gnm_expr_new_funcall3
		    (fd_if, gnm_expr_new_binary
		     (gnm_expr_copy (expr_diff),
		      GNM_EXPR_OP_NOT_EQUAL, make_cellref (0,-1)),
		     make_int (1),
		     make_int (0)),
		    make_int (0))));
	dao_set_cell_array_expr (dao, 1, 4, expr);

	expr_diff_pred = gnm_expr_new_binary
		(gnm_expr_copy (expr_diff),
		 GNM_EXPR_OP_SUB,
		 make_cellref (0,-2));
	expr_abs = gnm_expr_new_funcall1
		(fd_abs, gnm_expr_copy (expr_diff_pred));
	expr_big = gnm_expr_new_binary
		(gnm_expr_new_funcall1
		 (fd_max, gnm_expr_copy (expr_abs)),
		 GNM_EXPR_OP_ADD,
		 make_int (1));
	expr = gnm_expr_new_funcall3
		(fd_if,
		 gnm_expr_new_funcall1
		 (fd_isnumber, expr_1),
		 gnm_expr_new_funcall3
		 (fd_if,
		  gnm_expr_new_funcall1
		  (fd_isnumber, expr_2),
		  gnm_expr_new_funcall3
		  (fd_if,
		   gnm_expr_new_binary
		   (gnm_expr_copy (expr_diff),
		    GNM_EXPR_OP_EQUAL,
		    make_cellref (0,-2)),
		   gnm_expr_copy (expr_big),
		   expr_abs),
		  gnm_expr_copy (expr_big)),
		 expr_big);
	expr = gnm_expr_new_funcall3
		(fd_rank,
		 gnm_expr_new_unary (GNM_EXPR_OP_UNARY_NEG,
				     expr_diff_pred),
		 expr,
		 make_int (1));
	expr = gnm_expr_new_funcall1
		(fd_sum,
		 gnm_expr_new_binary
		 (expr_isnumber,
		  GNM_EXPR_OP_MULT,
		  gnm_expr_new_funcall3
		  (fd_if,
		   gnm_expr_new_binary
		   (expr_diff,
		    GNM_EXPR_OP_LT,
		    make_cellref (0,-2)),
		   expr,
		   make_int (0))));

	dao_set_cell_array_expr (dao, 1, 5, expr);

	dao_set_cell_expr (dao, 1, 6,
			   gnm_expr_new_binary
			   (gnm_expr_new_binary
			    (gnm_expr_new_binary
			     (make_cellref (0,-2),
			      GNM_EXPR_OP_MULT,
			      gnm_expr_new_binary
			      (make_cellref (0,-2),
			       GNM_EXPR_OP_ADD,
			       make_int (1))),
			     GNM_EXPR_OP_DIV,
			     make_int (2)),
			    GNM_EXPR_OP_SUB,
			    make_cellref (0,-1)));

	dao_set_cell_expr
		(dao, 1, 7,
		 gnm_expr_new_funcall2
		 (fd_min, make_cellref (0,-1), make_cellref (0,-2)));

	expr_expect = gnm_expr_new_binary
		(gnm_expr_new_binary
		 (make_cellref (0,-5),
		  GNM_EXPR_OP_MULT,
		  gnm_expr_new_binary
		  (make_cellref (0,-5),
		   GNM_EXPR_OP_ADD,
		   make_int (1))),
		 GNM_EXPR_OP_DIV,
		 make_int (4));
	expr_var = gnm_expr_new_binary
		(gnm_expr_new_binary
		 (gnm_expr_copy (expr_expect),
		  GNM_EXPR_OP_MULT,
		  gnm_expr_new_binary
		  (gnm_expr_new_binary
		   (make_int (2),
		    GNM_EXPR_OP_MULT,
		    make_cellref (0,-5)),
		   GNM_EXPR_OP_ADD,
		   make_int (1))),
		 GNM_EXPR_OP_DIV,
		 make_int (6));
	expr = gnm_expr_new_funcall4
		(fd_normdist, gnm_expr_new_binary
		 (make_cellref (0,-2),
		  GNM_EXPR_OP_ADD,
		  make_float (0.5)),
		 expr_expect,
		 gnm_expr_new_funcall1 (fd_sqrt, expr_var),
		 gnm_expr_new_constant (value_new_bool (TRUE)));
	dao_set_cell_expr (dao, 1, 9,
			   gnm_expr_new_funcall3
			   (fd_if,
			    gnm_expr_new_binary
			    (make_cellref (0,-5),
			     GNM_EXPR_OP_LT,
			     make_int (12)),
			    gnm_expr_new_constant (value_new_error_NA (NULL)),
			    expr));
	dao_set_cell_comment
		(dao,  1, 9,
		 _("This p-value is calculated by a normal approximation.\n"
		   "It is only valid if the sample size is at least 12."));

	expr = gnm_expr_new_binary (make_int (2),
				    GNM_EXPR_OP_MULT, make_cellref (0,-1));
	dao_set_cell_array_expr (dao, 1, 10, expr);

	gnm_func_dec_usage (fd_median);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_min);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_normdist);
	gnm_func_dec_usage (fd_isnumber);
	gnm_func_dec_usage (fd_iferror);
	gnm_func_dec_usage (fd_rank);
	gnm_func_dec_usage (fd_abs);
	gnm_func_dec_usage (fd_sqrt);
	gnm_func_dec_usage (fd_max);

	value_release (val_1);
	value_release (val_2);

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_signed_rank_test_two_tool_class_init (GnmSignedRankTestTwoToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_signed_rank_test_two_tool_update_dao;
	at_class->update_descriptor = gnm_signed_rank_test_two_tool_update_descriptor;
	at_class->prepare_output_range = gnm_signed_rank_test_two_tool_prepare_output_range;
	at_class->format_output_range = gnm_signed_rank_test_two_tool_format_output_range;
	at_class->perform_calc = gnm_signed_rank_test_two_tool_perform_calc;
}

GnmAnalysisTool *
gnm_signed_rank_test_two_tool_new (void)
{
	return g_object_new (GNM_TYPE_SIGNED_RANK_TEST_TWO_TOOL, NULL);
}
