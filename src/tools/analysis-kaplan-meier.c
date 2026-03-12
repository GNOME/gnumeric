/*
 * analysis-kaplan-meier.c:
 *
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2008 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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
#include <tools/analysis-kaplan-meier.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>
#include <sheet-object-graph.h>
#include <graph.h>
#include <goffice/goffice.h>
#include <sheet.h>

G_DEFINE_TYPE (GnmKaplanMeierTool, gnm_kaplan_meier_tool, GNM_TYPE_GENERIC_B_ANALYSIS_TOOL)

static void
gnm_kaplan_meier_tool_init (GnmKaplanMeierTool *tool)
{
	tool->range_3 = NULL;
	tool->group_list = NULL;
}

static void
cb_free_group (analysis_tools_kaplan_meier_group_t *group)
{
	g_free (group->name);
	g_free (group);
}

static void
gnm_kaplan_meier_tool_finalize (GObject *obj)
{
	GnmKaplanMeierTool *ktool = GNM_KAPLAN_MEIER_TOOL (obj);
	value_release (ktool->range_3);
	g_slist_free_full (ktool->group_list, (GDestroyNotify)cb_free_group);
	G_OBJECT_CLASS (gnm_kaplan_meier_tool_parent_class)->finalize (obj);
}

static gboolean
gnm_kaplan_meier_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmKaplanMeierTool *ktool = GNM_KAPLAN_MEIER_TOOL (tool);
	GnmGenericBAnalysisTool *gtool = &ktool->parent;
	int repetitions = ((ktool->group_list == NULL) ? 1
			   : g_slist_length (ktool->group_list));
	int colspan = ((ktool->std_err ? 4 : 3) + (ktool->censored ? 1 : 0));
	int rows =  gtool->base.range_1->v_range.cell.b.row
		- gtool->base.range_1->v_range.cell.a.row + 1;

	dao_adjust (dao, colspan + 1, repetitions * (rows + 5) + 10);
	return FALSE;
}

static char *
gnm_kaplan_meier_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Kaplan-Meier Estimates (%s)"));
}

static gboolean
gnm_kaplan_meier_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Kaplan-Meier Estimates"));
	return FALSE;
}

static gboolean
gnm_kaplan_meier_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Kaplan-Meier Estimates"));
}

static gboolean
gnm_kaplan_meier_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmKaplanMeierTool *ktool = GNM_KAPLAN_MEIER_TOOL (tool);
	GnmGenericBAnalysisTool *gtool = &ktool->parent;
	int rows, row;
	int std_err_col = ktool->censored ? 4 : 3;
	int prob_col = ktool->censored ? 3 : 2;
	int repetitions = ((ktool->group_list == NULL) ? 1
			   : g_slist_length (ktool->group_list));
	int colspan = ((ktool->std_err ? 4 : 3) + (ktool->censored ? 1 : 0));
	int i;
	int logrank_test_y_offset = 0;

	GnmExpr const *expr_data;
	GnmExpr const *expr_group_data = NULL;
	GnmExpr const *expr_small;
	GnmExpr const *expr_time;
	GnmExpr const *expr_at_risk;
	GnmExpr const *expr_deaths;
	GnmExpr const *expr_censures = NULL;
	GnmExpr const *expr_prob_zero;
	GnmExpr const *expr_prob;
	GnmExpr const *expr_std_err = NULL;
	GnmFunc *fd_sqrt = NULL;
	GnmFunc *fd_min = NULL;
	GogGraph     *graph = NULL;
	GogPlot	     *plot  = NULL;
	SheetObject  *so;
	GOData       *times = NULL;
	GSList *gl = ktool->group_list;
	GnmFunc *fd_small = gnm_func_get_and_use ("SMALL");
	GnmFunc *fd_if = gnm_func_get_and_use ("IF");
	GnmFunc *fd_iserror = gnm_func_get_and_use ("ISERROR");
	GnmFunc *fd_sum = gnm_func_get_and_use ("SUM");

	if (ktool->std_err) {
		fd_sqrt = gnm_func_get_and_use ("SQRT");
	}
	if (ktool->median) {
		fd_min = gnm_func_get_and_use ("MIN");
	}

	rows =  gtool->base.range_1->v_range.cell.b.row
		- gtool->base.range_1->v_range.cell.a.row + 1;

	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Kaplan-Meier"));


	if (ktool->chart) {
		GogChart     *chart;

		graph = g_object_new (GOG_TYPE_GRAPH, NULL);
		chart = GOG_CHART (gog_object_add_by_name (
						   GOG_OBJECT (graph), "Chart", NULL));

		plot = gog_plot_new_by_name ("GogXYPlot");
		go_object_set_property (G_OBJECT (plot), "interpolation",
						"Default interpolation", "step-start",
						NULL, NULL);

		gog_object_add_by_name (GOG_OBJECT (chart),
					"Plot", GOG_OBJECT (plot));
		times = dao_go_data_vector (dao, 0, 2, 0, 1+rows);
	}

	dao_set_italic (dao, 0, 1, 0, 1);
	dao_set_cell (dao, 0, 1, _("Time"));

	expr_data = gnm_expr_new_constant (value_dup (gtool->base.range_1));

	if (ktool->group_list != NULL && ktool->range_3 != NULL)
		expr_group_data = gnm_expr_new_constant (value_dup (ktool->range_3));

	expr_small = gnm_expr_new_funcall2 (fd_small,
					    gnm_expr_new_funcall3
					    (fd_if,
					     gnm_expr_new_binary
					     (gnm_expr_copy (expr_data),
					      GNM_EXPR_OP_GT,
					      make_cellref (0, -1)),
					     gnm_expr_copy (expr_data),
					     gnm_expr_new_constant (value_new_string ("N/A"))),
					    gnm_expr_new_constant (value_new_int (1)));
	expr_time = gnm_expr_new_funcall3 (fd_if,
					   gnm_expr_new_funcall1 (fd_iserror,
								  gnm_expr_copy (expr_small)),
					   gnm_expr_new_constant (value_new_string ("")),
					   expr_small);

	dao_set_cell_int (dao, 0, 2, 0);
	for (row = 1; row < rows; row++)
		dao_set_cell_array_expr (dao, 0, 2+row, gnm_expr_copy (expr_time));

	gnm_expr_free (expr_time);



	dao->offset_col++;

	/* Repeated Info start */
	for (i = 0; i < repetitions; i++) {
		GnmExpr const *expr_group = NULL;

		if (gl != NULL && gl->data != NULL) {
			analysis_tools_kaplan_meier_group_t *gd = gl->data;
			if (gd->name != NULL) {
				dao_set_italic (dao, 0, 0, 0, 0);
				dao_set_cell (dao, 0, 0, gd->name);

				if (gd->group_from == gd->group_to)
					expr_group = gnm_expr_new_funcall3
					(fd_if,
					 gnm_expr_new_binary
					 (gnm_expr_copy (expr_group_data),
					  GNM_EXPR_OP_EQUAL,
					  gnm_expr_new_constant (value_new_int (gd->group_from))),
					 gnm_expr_new_constant (value_new_int (1)),
					 gnm_expr_new_constant (value_new_int (0)));
				else
					expr_group =  gnm_expr_new_binary
					(gnm_expr_new_funcall3
					 (fd_if,
					  gnm_expr_new_binary
					  (gnm_expr_copy (expr_group_data),
					   GNM_EXPR_OP_GTE,
					   gnm_expr_new_constant (value_new_int (gd->group_from))),
					  gnm_expr_new_constant (value_new_int (1)),
					  gnm_expr_new_constant (value_new_int (0))),
					 GNM_EXPR_OP_MULT,
					 gnm_expr_new_funcall3
					 (fd_if,
					  gnm_expr_new_binary
					  (gnm_expr_copy (expr_group_data),
					   GNM_EXPR_OP_LTE,
					   gnm_expr_new_constant (value_new_int (gd->group_to))),
					  gnm_expr_new_constant (value_new_int (1)),
					  gnm_expr_new_constant (value_new_int (0))));
			}
		}

		if (expr_group == NULL)
			expr_group = gnm_expr_new_constant (value_new_int (1));

		dao_set_italic (dao, 0, 1, prob_col, 1);
		if (ktool->censored)
			set_cell_text_row (dao, 0, 1,
					   _("/At Risk"
					     "/Deaths"
					     "/Censures"
					     "/Probability"));
		else
			set_cell_text_row (dao, 0, 1,
					   _("/At Risk"
					     "/Deaths"
					     "/Probability"));
		if (ktool->std_err) {
			dao_set_italic (dao, std_err_col, 1, std_err_col, 1);
			dao_set_cell (dao, std_err_col, 1, _("Standard Error"));
		}

		expr_at_risk = gnm_expr_new_funcall3
			(fd_if,
			 gnm_expr_new_binary (make_cellref (-1-i*colspan, 0),
					      GNM_EXPR_OP_EQUAL,
					      gnm_expr_new_constant (value_new_string (""))),
			 gnm_expr_new_constant (value_new_string ("")),
			 gnm_expr_new_funcall1 (fd_sum,
						gnm_expr_new_binary (
						gnm_expr_new_funcall3
						(fd_if,
						 gnm_expr_new_binary
						 (gnm_expr_copy (expr_data),
						  GNM_EXPR_OP_LT,
						  make_cellref (-1-i*colspan, 0)),
						 gnm_expr_new_constant (value_new_int (0)),
						 gnm_expr_new_constant (value_new_int (1))),
						GNM_EXPR_OP_MULT,
						gnm_expr_copy (expr_group))));

		if (ktool->censored) {
			GnmExpr const *expr_censor;

			if (ktool->censor_mark == ktool->censor_mark_to)
				expr_censor = gnm_expr_new_funcall3
					(fd_if,
					 gnm_expr_new_binary
					 (gnm_expr_new_constant (value_dup (gtool->base.range_2)),
					  GNM_EXPR_OP_EQUAL,
					  gnm_expr_new_constant (value_new_int (ktool->censor_mark))),
					 gnm_expr_new_constant (value_new_int (1)),
					 gnm_expr_new_constant (value_new_int (0)));
			else
				expr_censor = gnm_expr_new_binary
					(gnm_expr_new_funcall3
					 (fd_if,
					  gnm_expr_new_binary
					  (gnm_expr_new_constant (value_dup (gtool->base.range_2)),
					   GNM_EXPR_OP_GTE,
					   gnm_expr_new_constant (value_new_int (ktool->censor_mark))),
					  gnm_expr_new_constant (value_new_int (1)),
					  gnm_expr_new_constant (value_new_int (0))),
					 GNM_EXPR_OP_MULT,
					 gnm_expr_new_funcall3
					 (fd_if,
					  gnm_expr_new_binary
					  (gnm_expr_new_constant (value_dup (gtool->base.range_2)),
					   GNM_EXPR_OP_LTE,
					   gnm_expr_new_constant (value_new_int (ktool->censor_mark_to))),
					  gnm_expr_new_constant (value_new_int (1)),
					  gnm_expr_new_constant (value_new_int (0))));


			expr_deaths = gnm_expr_new_funcall3
				(fd_if,
				 gnm_expr_new_binary (make_cellref (-1, 0),
						      GNM_EXPR_OP_EQUAL,
						      gnm_expr_new_constant (value_new_string (""))),
				 gnm_expr_new_constant (value_new_string ("")),
				 gnm_expr_new_funcall1 (fd_sum,
							gnm_expr_new_binary
							(gnm_expr_copy (expr_group),
							 GNM_EXPR_OP_MULT,
							 gnm_expr_new_binary
							 (gnm_expr_new_funcall3
							  (fd_if,
							   gnm_expr_new_binary
							   (gnm_expr_copy (expr_data),
							    GNM_EXPR_OP_EQUAL,
							    make_cellref (-2-i*colspan, 0)),
							   gnm_expr_new_constant (value_new_int (1)),
							   gnm_expr_new_constant (value_new_int (0))),
							  GNM_EXPR_OP_MULT,
							  gnm_expr_new_binary
							  (gnm_expr_new_constant (value_new_int (1)),
							   GNM_EXPR_OP_SUB,
							   gnm_expr_copy (expr_censor))))));
			expr_censures =  gnm_expr_new_funcall3
				(fd_if,
				 gnm_expr_new_binary (make_cellref (-1, 0),
						      GNM_EXPR_OP_EQUAL,
						      gnm_expr_new_constant (value_new_string (""))),
				 gnm_expr_new_constant (value_new_string ("")),
				 gnm_expr_new_funcall1 (fd_sum,
							gnm_expr_new_binary
							(gnm_expr_copy (expr_group),
							 GNM_EXPR_OP_MULT,
							 gnm_expr_new_binary
							 (gnm_expr_new_funcall3
							  (fd_if,
							   gnm_expr_new_binary
							   (gnm_expr_copy (expr_data),
							    GNM_EXPR_OP_EQUAL,
							    make_cellref (-3-i*colspan, 0)),
							   gnm_expr_new_constant (value_new_int (1)),
							   gnm_expr_new_constant (value_new_int (0))),
							  GNM_EXPR_OP_MULT,
							  expr_censor))));
		} else
			expr_deaths = gnm_expr_new_funcall3
				(fd_if,
				 gnm_expr_new_binary (make_cellref (-1, 0),
						      GNM_EXPR_OP_EQUAL,
						      gnm_expr_new_constant (value_new_string (""))),
				 gnm_expr_new_constant (value_new_string ("")),
				 gnm_expr_new_funcall1 (fd_sum,
							gnm_expr_new_binary
							(gnm_expr_copy (expr_group),
							 GNM_EXPR_OP_MULT,
							 gnm_expr_new_funcall3
							 (fd_if,
							  gnm_expr_new_binary
							  (gnm_expr_copy (expr_data),
							   GNM_EXPR_OP_EQUAL,
							   make_cellref (-2-i*colspan, 0)),
							  gnm_expr_new_constant (value_new_int (1)),
							  gnm_expr_new_constant (value_new_int (0))))));

		expr_prob_zero = gnm_expr_new_binary (gnm_expr_new_binary (make_cellref ( - prob_col, 0),
									   GNM_EXPR_OP_SUB,
									   make_cellref (1 - prob_col, 0)),
						      GNM_EXPR_OP_DIV,
						      make_cellref ( - prob_col, 0));
		expr_prob = gnm_expr_new_funcall3 (fd_if,
						   gnm_expr_new_binary
						   (make_cellref (1 - prob_col, 0),
						    GNM_EXPR_OP_EQUAL,
						    gnm_expr_new_constant (value_new_string (""))),
						   gnm_expr_new_constant (value_new_string ("")),
						   gnm_expr_new_binary (gnm_expr_copy (expr_prob_zero),
									GNM_EXPR_OP_MULT,
									make_cellref (0, -1)));

		if (ktool->std_err) {
			expr_std_err = gnm_expr_new_funcall3 (fd_if,
							      gnm_expr_new_binary
							      (make_cellref (-1, 0),
							       GNM_EXPR_OP_EQUAL,
							       gnm_expr_new_constant (value_new_string (""))),
							      gnm_expr_new_constant (value_new_string ("")),
							      gnm_expr_new_binary (make_cellref (-1, 0),
										   GNM_EXPR_OP_MULT,
										   gnm_expr_new_funcall1
										   (fd_sqrt,
										    gnm_expr_new_binary
										    (gnm_expr_new_binary
										     (gnm_expr_new_constant
										      (value_new_int (1)),
										      GNM_EXPR_OP_SUB,
										      make_cellref (-1, 0)),
										     GNM_EXPR_OP_DIV,
										     make_cellref
										     ( - std_err_col, 0)))));

			dao_set_format  (dao, std_err_col, 2, std_err_col, rows + 1, "0.0000");
			dao_set_cell_expr (dao, std_err_col, 2, gnm_expr_copy (expr_std_err));
		}

		dao_set_format  (dao, prob_col, 2, prob_col, rows + 1, "0.00%");

		dao_set_cell_array_expr (dao, 0, 2, gnm_expr_copy (expr_at_risk));
		dao_set_cell_array_expr (dao, 1, 2, gnm_expr_copy (expr_deaths));
		dao_set_cell_expr (dao, prob_col, 2, expr_prob_zero);

		if (expr_censures != NULL)
			dao_set_cell_array_expr (dao, 2, 2, gnm_expr_copy (expr_censures));

		for (row = 1; row < rows; row++) {
			dao_set_cell_array_expr (dao, 0, 2+row, gnm_expr_copy (expr_at_risk));
			dao_set_cell_array_expr (dao, 1, 2+row, gnm_expr_copy (expr_deaths));
			if (expr_censures != NULL)
				dao_set_cell_array_expr (dao, 2, 2+row, gnm_expr_copy (expr_censures));
			dao_set_cell_array_expr (dao, prob_col, 2+row, gnm_expr_copy (expr_prob));
			if (ktool->std_err)
				dao_set_cell_expr (dao, std_err_col, 2+row, gnm_expr_copy (expr_std_err));
		}

		gnm_expr_free (expr_at_risk);
		gnm_expr_free (expr_deaths);
		gnm_expr_free (expr_prob);
		if (expr_censures != NULL) {
			gnm_expr_free (expr_censures);
			expr_censures = NULL;
		}
		if (expr_std_err != NULL) {
			gnm_expr_free (expr_std_err);
			expr_std_err = NULL;
		}

		/* Create Chart if requested */
		if (ktool->chart) {
			GogSeries    *series;
			GOData *probabilities;
			GOStyle  *style;

			probabilities = dao_go_data_vector (dao, prob_col, 2, prob_col, 1+rows);

			g_object_ref (times);
			series = gog_plot_new_series (plot);
			gog_series_set_dim (series, 0, times, NULL);
			gog_series_set_dim (series, 1, probabilities, NULL);

			style = go_styled_object_get_style (GO_STYLED_OBJECT (series));
			style->marker.auto_shape = FALSE;
			go_marker_set_shape (style->marker.mark, GO_MARKER_NONE);
			go_styled_object_set_style (GO_STYLED_OBJECT (series), style);

			if (ktool->censored && ktool->ticks) {
				GOData *censures;
				GnmExpr const *expr;

				expr = gnm_expr_new_binary
					(gnm_expr_new_binary (dao_get_rangeref (dao, prob_col, 2, prob_col, 1+rows),
							      GNM_EXPR_OP_DIV,
							      dao_get_rangeref (dao, 2, 2, 2, 1+rows)),
					 GNM_EXPR_OP_MULT,
					 dao_get_rangeref (dao, 2, 2, 2, 1+rows));

				censures = gnm_go_data_vector_new_expr (dao->sheet, gnm_expr_top_new (expr));

				series = gog_plot_new_series (plot);
				g_object_ref (times);
				gog_series_set_dim (series, 0, times, NULL);
				gog_series_set_dim (series, 1, censures, NULL);

				style = go_styled_object_get_style (GO_STYLED_OBJECT (series));
				style->marker.auto_shape = FALSE;
				go_marker_set_shape (style->marker.mark, GO_MARKER_TRIANGLE_DOWN);
				style->line.dash_type = GO_LINE_NONE;
				style->line.auto_dash = FALSE;
				style->line.width = 0;
				go_styled_object_set_style (GO_STYLED_OBJECT (series), style);
			}
		}

		gnm_expr_free (expr_group);

		dao->offset_col += colspan;
		if (gl != NULL)
			gl = gl->next;
	}
	/* End of Loop */

	if (ktool->chart) {
		so = sheet_object_graph_new (graph);
		g_object_unref (graph);
		g_object_unref (times);

		dao_set_sheet_object (dao, 0, 1, so);
	}

	if (ktool->median) {
		dao_set_italic (dao, 1, 1, 1, 1);
		dao_set_cell (dao, 1, 1, _("Median"));

		dao->offset_col += 2;
		gl = ktool->group_list;

		for (i = 0; i < repetitions; i++) {
			/* the next involves (colspan-1) since the median field moves to the right. */
			gint prob_dx = - (repetitions - i)* (colspan - 1) - 1;
			gint times_dx = - colspan * repetitions - i - 3;
			GnmExpr const *expr_median;

			dao_set_italic (dao, 0, 0, 0, 0);

			if (gl != NULL && gl->data != NULL) {
				analysis_tools_kaplan_meier_group_t *gd = gl->data;
				if (gd->name != NULL) {
					dao_set_cell (dao, 0, 0, gd->name);
				}
				gl = gl->next;
			}

			expr_prob = gnm_expr_new_binary
				(gnm_expr_new_funcall3
				 (fd_if,
				  gnm_expr_new_binary
				  (make_rangeref(prob_dx, 1, prob_dx, rows),
				   GNM_EXPR_OP_GT,
				   gnm_expr_new_constant (value_new_float (0.5))),
				  gnm_expr_new_constant (value_new_string ("NA")),
				  gnm_expr_new_constant (value_new_int (1))),
				 GNM_EXPR_OP_MULT,
				 make_rangeref (times_dx, 1, times_dx, rows));

			expr_median =  gnm_expr_new_funcall1
				(fd_min,
				 gnm_expr_new_funcall3
				 (fd_if,
				  gnm_expr_new_funcall1
				  (fd_iserror,
				   gnm_expr_copy (expr_prob)),
				  gnm_expr_new_constant (value_new_string ("NA")),
				  expr_prob));

			dao_set_cell_array_expr (dao, 0, 1,expr_median);

			dao->offset_col += 1;
		}
		logrank_test_y_offset = 5;
		dao->offset_col -= (2 + repetitions);
	}

	if (ktool->logrank_test) {
		GnmExpr const *expr_statistic = gnm_expr_new_constant (value_new_int (0));
		GnmExpr const *expr_p;
		GnmExpr const *expr_n_total = gnm_expr_new_constant (value_new_int (0));
		GnmExpr const *expr_death_total = gnm_expr_new_constant (value_new_int (0));
		GnmFunc *fd_chidist = gnm_func_get_and_use ("CHIDIST");

		dao_set_italic (dao, 1, logrank_test_y_offset, 1, logrank_test_y_offset+3);
		set_cell_text_col (dao, 1, logrank_test_y_offset,
				   _("/Log-Rank Test"
				     "/Statistic"
				     "/Degrees of Freedom"
				     "/p-Value"));

		/* Test Statistic */
		for (i = 0; i < repetitions; i++) {
			gint atrisk_dx = - (repetitions - i)* colspan - 2;
			expr_n_total = gnm_expr_new_binary
				(expr_n_total, GNM_EXPR_OP_ADD,
				 make_rangeref ( atrisk_dx,
						 - logrank_test_y_offset + 1,
						 atrisk_dx,
						 - logrank_test_y_offset + rows));
			expr_death_total = gnm_expr_new_binary
				(expr_death_total, GNM_EXPR_OP_ADD,
				 make_rangeref ( atrisk_dx + 1,
						 - logrank_test_y_offset + 1,
						 atrisk_dx + 1,
						 - logrank_test_y_offset + rows));
		}

		for (i = 0; i < repetitions; i++) {
			GnmExpr const *expr_expect;
			gint atrisk_dx = - (repetitions - i)* colspan - 2;

			expr_expect = gnm_expr_new_binary
				(gnm_expr_new_binary
				 (gnm_expr_copy (expr_death_total),
				  GNM_EXPR_OP_MULT,
				  make_rangeref (atrisk_dx,
						 - logrank_test_y_offset + 1,
						 atrisk_dx,
						 - logrank_test_y_offset + rows)),
				 GNM_EXPR_OP_DIV,
				 gnm_expr_copy (expr_n_total));

			expr_expect = gnm_expr_new_funcall3 (
				fd_if,
				gnm_expr_new_funcall1 (fd_iserror,
						       gnm_expr_copy (expr_expect)),
				gnm_expr_new_constant (value_new_int (0)),
				expr_expect);
			expr_expect = gnm_expr_new_funcall1 (fd_sum,
							     expr_expect);
			expr_expect = gnm_expr_new_binary (
				gnm_expr_new_binary (
					gnm_expr_new_binary (
						gnm_expr_new_funcall1 (
							fd_sum,
							make_rangeref
							(atrisk_dx + 1,
							 - logrank_test_y_offset + 1,
							 atrisk_dx + 1,
							 - logrank_test_y_offset + rows)),
						GNM_EXPR_OP_SUB,
						gnm_expr_copy (expr_expect)),
					GNM_EXPR_OP_EXP,
					gnm_expr_new_constant (value_new_int (2))),
				GNM_EXPR_OP_DIV,
				expr_expect);
			expr_statistic =  gnm_expr_new_binary (
				expr_statistic, GNM_EXPR_OP_ADD, expr_expect);
		}
		gnm_expr_free (expr_n_total);
		gnm_expr_free (expr_death_total);

		dao_set_cell_array_expr (dao, 2, logrank_test_y_offset + 1, expr_statistic);

		/* Degree of Freedoms */
		dao_set_cell_int (dao, 2, logrank_test_y_offset + 2, repetitions - 1);

		/* p Value */
		expr_p = gnm_expr_new_funcall2 (fd_chidist,
						make_cellref (0,-2),
						make_cellref (0,-1));
		dao_set_cell_expr (dao, 2, logrank_test_y_offset + 3, expr_p);

		gnm_func_dec_usage (fd_chidist);
	}





	gnm_expr_free (expr_data);
	if (expr_group_data != NULL)
		gnm_expr_free (expr_group_data);

	gnm_func_dec_usage (fd_small);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_iserror);
	gnm_func_dec_usage (fd_sum);
	if (fd_sqrt != NULL)
		gnm_func_dec_usage (fd_sqrt);
	if (fd_min != NULL)
		gnm_func_dec_usage (fd_min);

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_kaplan_meier_tool_class_init (GnmKaplanMeierToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->finalize = gnm_kaplan_meier_tool_finalize;
	at_class->update_dao = gnm_kaplan_meier_tool_update_dao;
	at_class->update_descriptor = gnm_kaplan_meier_tool_update_descriptor;
	at_class->prepare_output_range = gnm_kaplan_meier_tool_prepare_output_range;
	at_class->format_output_range = gnm_kaplan_meier_tool_format_output_range;
	at_class->perform_calc = gnm_kaplan_meier_tool_perform_calc;
}

GnmAnalysisTool *
gnm_kaplan_meier_tool_new (void)
{
	return g_object_new (GNM_TYPE_KAPLAN_MEIER_TOOL, NULL);
}
