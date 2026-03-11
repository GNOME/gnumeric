/*
 * analysis-frequency.c:
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
#include <tools/analysis-frequency.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>
#include <sheet-object-graph.h>
#include <goffice/goffice.h>
#include <sheet.h>


G_DEFINE_TYPE (GnmFrequencyTool, gnm_frequency_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

static void
gnm_frequency_tool_init (GnmFrequencyTool *tool)
{
	tool->predetermined = FALSE;
	tool->bin = NULL;
}

static void
gnm_frequency_tool_finalize (GObject *obj)
{
	GnmFrequencyTool *tool = GNM_FREQUENCY_TOOL (obj);
	if (tool->bin)
		value_release (tool->bin);
	G_OBJECT_CLASS (gnm_frequency_tool_parent_class)->finalize (obj);
}

static gboolean
gnm_frequency_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmFrequencyTool *ftool = GNM_FREQUENCY_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &ftool->parent;
	int i, j;

	analysis_tool_prepare_input_range (gtool);
	if (!analysis_tool_check_input_homogeneity (gtool)) {
		gtool->base.err = gtool->base.group_by + 1;
		return TRUE;
	}

	i = g_slist_length (gtool->base.input);
	if (ftool->predetermined) {
		GnmRange range;
		range_init_value (&range, ftool->bin);
		j = range_height (&range) * range_width (&range);
	} else
		j = ftool->n;

	dao_adjust (dao, 1 + i, 1 + j);
	return FALSE;
}

static char *
gnm_frequency_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Frequency Table (%s)"));
}

static gboolean
gnm_frequency_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Frequency Table"));
	return FALSE;
}

static gboolean
gnm_frequency_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Frequency Table"));
}

static gboolean
gnm_frequency_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmFrequencyTool *ftool = GNM_FREQUENCY_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &ftool->parent;
	gint i_limit, col;
	GSList *l;

	GnmFunc *fd_sum;
	GnmFunc *fd_if;
	GnmFunc *fd_index;
	GnmFunc *fd_isblank;
	GnmFunc *fd_rows = NULL;
	GnmFunc *fd_columns = NULL;
	GnmFunc *fd_exact = NULL;

	fd_sum = gnm_func_get_and_use ("SUM");
	fd_if = gnm_func_get_and_use ("IF");
	fd_index = gnm_func_get_and_use ("INDEX");
	fd_isblank = gnm_func_get_and_use ("ISBLANK");

	if (ftool->exact) {
		fd_exact = gnm_func_get_and_use ("EXACT");
	}
	if (ftool->percentage) {
		fd_rows = gnm_func_get_and_use ("ROWS");
		fd_columns = gnm_func_get_and_use ("COLUMNS");
	}
	/* General Info */

	dao_set_italic (dao, 0, 0, 0, 1);
	set_cell_text_col (dao, 0, 0, _("/Frequency Table"
					"/Category"));

	/* Setting up the categories */

	if (ftool->predetermined) {
		int row = 2, i, j, i_h_limit, i_w_limit;
		GnmExpr const *expr_bin;
		GnmRange range;

		range_init_value (&range, ftool->bin);
		i_h_limit = range_height (&range);
		i_w_limit = range_width (&range);
		i_limit = i_h_limit * i_w_limit;

		expr_bin = gnm_expr_new_constant (ftool->bin);

		for (i = 1; i <= i_h_limit; i++)
			for (j = 1; j <= i_w_limit; j++) {
				GnmExpr const *expr_index;

				expr_index =  gnm_expr_new_funcall3
					(fd_index,
					 gnm_expr_copy (expr_bin),
					 gnm_expr_new_constant (value_new_int (i)),
					 gnm_expr_new_constant (value_new_int (j)));

				dao_set_cell_expr (dao, 0, row++,
						   gnm_expr_new_funcall3
						   (fd_if,
						    gnm_expr_new_funcall1
						    (fd_isblank,
						     gnm_expr_copy (expr_index)),
						    gnm_expr_new_constant (value_new_string ("")),
						    expr_index));
			}
		gnm_expr_free (expr_bin);
	} else {
		i_limit = ftool->n;
	}

	for (l = gtool->base.input, col = 1; l; col++, l = l->next) {
		GnmValue *val = value_dup ((GnmValue *)l->data);
		GnmValue *val_c = NULL;
		GnmExpr const *expr_count;
		GnmExpr const *expr_data;
		GnmExpr const *expr_if;
		int i, row = 2;


		dao_set_italic (dao, col, 1, col, 1);
		if (gtool->base.labels) {
			val_c = value_dup (val);
			switch (gtool->base.group_by) {
			case GROUPED_BY_ROW:
				val->v_range.cell.a.col++;
				break;
			default:
				val->v_range.cell.a.row++;
				break;
			}
			dao_set_cell_expr (dao, col, 1,
					   gnm_expr_new_funcall1 (fd_index,
								  gnm_expr_new_constant (val_c)));
		} else {
			char *txt;

			switch (gtool->base.group_by) {
			case GROUPED_BY_ROW:
				txt = g_strdup_printf (_("Row %d"), col);
				break;
			case GROUPED_BY_COL:
				txt = g_strdup_printf (_("Column %d"), col);
				break;
			default:
				txt = g_strdup_printf (_("Area %d"), col);
				break;
			}
			dao_set_cell (dao, col, 1, txt);
			g_free (txt);
		}

		expr_data = gnm_expr_new_constant (val);

		if (ftool->exact)
			expr_if = gnm_expr_new_funcall2
				(fd_exact, gnm_expr_copy (expr_data),
				 make_cellref (- col, 0));
		else
			expr_if = gnm_expr_new_binary
				(gnm_expr_copy (expr_data),
				 GNM_EXPR_OP_EQUAL, make_cellref (- col, 0));

		expr_count = gnm_expr_new_funcall1 (fd_sum,
						    gnm_expr_new_funcall3
						    (fd_if, expr_if,
						     gnm_expr_new_constant (value_new_int (1)),
						     gnm_expr_new_constant (value_new_int (0))));

		if (ftool->percentage) {
			dao_set_format  (dao, col, 2, col, i_limit + 2, "0.0%");
			expr_count = gnm_expr_new_binary (expr_count,
							  GNM_EXPR_OP_DIV,
							  gnm_expr_new_binary
							  (gnm_expr_new_funcall1
							   (fd_rows, gnm_expr_copy (expr_data)),
							   GNM_EXPR_OP_MULT,
							  gnm_expr_new_funcall1
							   (fd_columns, expr_data)));
		} else
			gnm_expr_free (expr_data);

		for (i = 0; i < i_limit; i++, row++)
			dao_set_cell_array_expr (dao, col, row, gnm_expr_copy (expr_count));

		gnm_expr_free (expr_count);
	}

	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_sum);
	gnm_func_dec_usage (fd_index);
	gnm_func_dec_usage (fd_isblank);
	if (fd_rows != NULL)
		gnm_func_dec_usage (fd_rows);
	if (fd_columns != NULL)
		gnm_func_dec_usage (fd_columns);
	if (fd_exact != NULL)
		gnm_func_dec_usage (fd_exact);

	/* Create Chart if requested */
	if (ftool->chart != NO_CHART) {
		SheetObject *so;
		GogGraph     *graph;
		GogChart     *chart;
		GogPlot	     *plot;
		GogSeries    *series;
		GOData *cats;
		GOData *values;
		int ct;

		graph = g_object_new (GOG_TYPE_GRAPH, NULL);
		chart = GOG_CHART (gog_object_add_by_name (
						   GOG_OBJECT (graph), "Chart", NULL));
		plot = gog_plot_new_by_name ("GogBarColPlot");
		if (ftool->chart == BAR_CHART)
			go_object_toggle (plot, "horizontal");
		gog_object_add_by_name (GOG_OBJECT (chart),
					"Plot", GOG_OBJECT (plot));

		cats = dao_go_data_vector (dao, 0, 2,
					     0, 2 + i_limit);

		for (ct = 1; ct < col; ct ++) {
			g_object_ref (cats);
			values = dao_go_data_vector (dao, ct, 2,
						     ct, 2 + i_limit);

			series = gog_plot_new_series (plot);
			gog_series_set_dim (series, 0, cats, NULL);
			gog_series_set_dim (series, 1, values, NULL);
		}
		g_object_unref (cats);

		so = sheet_object_graph_new (graph);
		g_object_unref (graph);

		dao_set_sheet_object (dao, 0, 1, so);
	}

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_frequency_tool_class_init (GnmFrequencyToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->finalize = gnm_frequency_tool_finalize;
	at_class->update_dao = gnm_frequency_tool_update_dao;
	at_class->update_descriptor = gnm_frequency_tool_update_descriptor;
	at_class->prepare_output_range = gnm_frequency_tool_prepare_output_range;
	at_class->format_output_range = gnm_frequency_tool_format_output_range;
	at_class->perform_calc = gnm_frequency_tool_perform_calc;
}

GnmAnalysisTool *
gnm_frequency_tool_new (void)
{
	return g_object_new (GNM_TYPE_FREQUENCY_TOOL, NULL);
}

