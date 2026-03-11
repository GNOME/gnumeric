/*
 * analysis-normality.c:
 *
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
#include <tools/analysis-normality.h>
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

G_DEFINE_TYPE (GnmNormalityTool, gnm_normality_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

static void
gnm_normality_tool_init (GnmNormalityTool *tool)
{
	tool->alpha = 0.05;
	tool->type = normality_test_type_andersondarling;
	tool->graph = FALSE;
}

static gboolean
gnm_normality_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmNormalityTool *ntool = GNM_NORMALITY_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &ntool->parent;
	analysis_tool_prepare_input_range (gtool);
	dao_adjust (dao, 1 + g_slist_length (gtool->base.input), 6);
	return FALSE;
}

static char *
gnm_normality_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Normality Test (%s)"));
}

static gboolean
gnm_normality_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Normality Test"));
	return FALSE;
}

static gboolean
gnm_normality_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Normality Test"));
}

static gboolean
gnm_normality_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmNormalityTool *ntool = GNM_NORMALITY_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &ntool->parent;
	guint   col;
	GSList *data = gtool->base.input;
	GnmFunc *fd;
	GnmFunc *fd_if;

	char const *fdname;
	char const *testname;
	char const *n_comment;

	GogGraph     *graph = NULL;
	GogPlot	     *plot = NULL;
	SheetObject *so;

	switch (ntool->type) {
	case normality_test_type_andersondarling:
		fdname = "ADTEST";
		testname = N_("Anderson-Darling Test");
		n_comment = N_("For the Anderson-Darling Test\n"
			       "the sample size must be at\n"
			       "least 8.");
		break;
	case normality_test_type_cramervonmises:
		fdname = "CVMTEST";
		testname = N_("Cram\xc3\xa9r-von Mises Test");
		n_comment = N_("For the Cram\xc3\xa9r-von Mises Test\n"
			       "the sample size must be at\n"
			       "least 8.");
		break;
	case normality_test_type_lilliefors:
		fdname = "LKSTEST";
		testname = N_("Lilliefors (Kolmogorov-Smirnov) Test");
		n_comment = N_("For the Lilliefors (Kolmogorov-Smirnov) Test\n"
			       "the sample size must be at least 5.");
		break;
	case normality_test_type_shapirofrancia:
		fdname = "SFTEST";
		testname = N_("Shapiro-Francia Test");
		n_comment = N_("For the Shapiro-Francia Test\n"
			       "the sample size must be at\n"
			       "least 5 and at most 5000.");
		break;
	default:
		g_assert_not_reached ();
	}

	fd = gnm_func_get_and_use (fdname);
	fd_if = gnm_func_get_and_use ("IF");

	dao_set_italic (dao, 0, 0, 0, 5);
        dao_set_cell (dao, 0, 0, _(testname));


	if (ntool->graph) {
		GogChart     *chart;

		graph = g_object_new (GOG_TYPE_GRAPH, NULL);
		chart = GOG_CHART (gog_object_add_by_name (
						   GOG_OBJECT (graph), "Chart", NULL));

		plot = gog_plot_new_by_name ("GogProbabilityPlot");
		go_object_set_property (G_OBJECT (plot), "distribution",
						"Distribution", "GODistNormal",
						NULL, NULL);

		gog_object_add_by_name (GOG_OBJECT (chart),
					"Plot", GOG_OBJECT (plot));
	}


	/* xgettext:
	 * Note to translators: in the following string and others like it,
	 * the "/" is a separator character that can be changed to anything
	 * if the translation needs the slash; just use, say, "|" instead.
	 *
	 * The items are bundled like this to increase translation context.
	 */
        set_cell_text_col (dao, 0, 1, _("/Alpha"
					"/p-Value"
					"/Statistic"
					"/N"
					"/Conclusion"));

	dao_set_cell_comment (dao, 0, 4, _(n_comment));

	for (col = 1; data != NULL; data = data->next, col++) {
		GnmValue *val_org = value_dup (data->data);

		/* Note that analysis_tools_write_label may modify val_org */
		dao_set_italic (dao, col, 0, col, 0);
		analysis_tools_write_label (gtool, val_org, dao,
					    col, 0, col);
		if (ntool->graph) {
			GogSeries    *series;

			series = gog_plot_new_series (plot);
			gog_series_set_dim (series, 0,
					    gnm_go_data_vector_new_expr
					    (val_org->v_range.cell.a.sheet,
					     gnm_expr_top_new (gnm_expr_new_constant (value_dup (val_org)))),
					    NULL);
		}

		if (col == 1)
			dao_set_cell_float (dao, col, 1, ntool->alpha);
		else
			dao_set_cell_expr (dao, col, 1,
					   make_cellref (1 - col, 0));

		dao_set_array_expr (dao, col, 2, 1, 3,
				    gnm_expr_new_funcall1 (fd, gnm_expr_new_constant (val_org)));
		dao_set_cell_expr (dao, col, 5,
				   gnm_expr_new_funcall3
				   (fd_if, gnm_expr_new_binary
				    (make_cellref (0, -4),
				     GNM_EXPR_OP_GTE,
				     make_cellref (0, -3)),
				    gnm_expr_new_constant (value_new_string (_("Not normal"))),
				    gnm_expr_new_constant (value_new_string (_("Possibly normal")))));
	}

	if (ntool->graph) {
		so = sheet_object_graph_new (graph);
		g_object_unref (graph);

		dao_set_sheet_object (dao, 0, 1, so);
	}


	gnm_func_dec_usage (fd);
	gnm_func_dec_usage (fd_if);

	dao_redraw_respan (dao);
	return 0;
}

static void
gnm_normality_tool_class_init (GnmNormalityToolClass *klass)
{
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	at_class->update_dao = gnm_normality_tool_update_dao;
	at_class->update_descriptor = gnm_normality_tool_update_descriptor;
	at_class->prepare_output_range = gnm_normality_tool_prepare_output_range;
	at_class->format_output_range = gnm_normality_tool_format_output_range;
	at_class->perform_calc = gnm_normality_tool_perform_calc;
}

GnmAnalysisTool *
gnm_normality_tool_new (void)
{
	return g_object_new (GNM_TYPE_NORMALITY_TOOL, NULL);
}

