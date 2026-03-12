/*
 * analysis-histogram.c:
 *
  * This is a complete reimplementation of the histogram tool in 2008
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
#include <tools/analysis-histogram.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <func.h>
#include <numbers.h>
#include <sheet-object-graph.h>
#include <goffice/goffice.h>
#include <sheet.h>

static GnmExpr const *
make_hist_expr (GnmHistogramTool *htool,
		int col, GnmValue *val,
		gboolean fromminf, gboolean topinf,
		data_analysis_output_t *dao)
{
	GnmExpr const *expr;
	GnmExpr const *expr_data;
	GnmExpr const *expr_if_to, *expr_if_from;
	GnmExprOp from, to;
	GnmFunc *fd_if = gnm_func_lookup_or_add_placeholder ("IF");
	GnmFunc *fd_sum = gnm_func_lookup_or_add_placeholder ("SUM");
	GnmFunc *fd_count = htool->percentage ?
		gnm_func_lookup_or_add_placeholder (htool->only_numbers ? "COUNT" : "COUNTA") : NULL;
	GnmFunc *fd_isnumber = gnm_func_lookup_or_add_placeholder (htool->only_numbers ? "ISNUMBER" : "ISBLANK");
	gint to_col = (htool->cumulative) ? 0 : 1;

	if (htool->bin_type & bintype_no_inf_upper) {
		from = GNM_EXPR_OP_LT;
		to = GNM_EXPR_OP_GTE;
	} else {
		from = GNM_EXPR_OP_LTE;
		to = GNM_EXPR_OP_GT;
	}

	expr_data = gnm_expr_new_constant (value_dup (val));
	if (topinf)
		expr_if_to = gnm_expr_new_constant (value_new_int (1));
	else
		expr_if_to = gnm_expr_new_funcall3
			(fd_if,
			 gnm_expr_new_binary
			 (gnm_expr_copy (expr_data),
			  to, make_cellref (- (col-to_col), 0)),
			 gnm_expr_new_constant (value_new_int (0)),
			 gnm_expr_new_constant (value_new_int (1)));

	if (htool->cumulative)
		expr = expr_if_to;
	else {
		GnmExpr const *one = gnm_expr_new_constant (value_new_int (1));
		if (fromminf)
			expr_if_from = one;
		else
			expr_if_from = gnm_expr_new_funcall3
				(fd_if,
				 gnm_expr_new_binary
				 (gnm_expr_copy (expr_data),
				  from, make_cellref (- col, 0)),
				 gnm_expr_new_constant (value_new_int (0)),
				 one);
		expr = gnm_expr_new_binary (expr_if_from,
					      GNM_EXPR_OP_MULT,
					      expr_if_to);
	}

	if (htool->only_numbers)
		expr = gnm_expr_new_binary (expr,
					    GNM_EXPR_OP_MULT,
					    gnm_expr_new_funcall3
					    (fd_if,gnm_expr_new_funcall1
					     (fd_isnumber, gnm_expr_copy (expr_data)),
					     gnm_expr_new_constant (value_new_int (1)),
					     gnm_expr_new_constant (value_new_int (0))));
	else
		expr = gnm_expr_new_binary (expr,
					    GNM_EXPR_OP_MULT,
					    gnm_expr_new_funcall3
					    (fd_if,gnm_expr_new_funcall1
					     (fd_isnumber, gnm_expr_copy (expr_data)),
					     gnm_expr_new_constant (value_new_int (0)),
					     gnm_expr_new_constant (value_new_int (1))));


	expr = gnm_expr_new_funcall1 (fd_sum, expr);

	if (htool->percentage)
		expr = gnm_expr_new_binary (expr,
					    GNM_EXPR_OP_DIV,
					    gnm_expr_new_funcall1
					    (fd_count,
					     expr_data));
	else
		gnm_expr_free (expr_data);

	return expr;
}


G_DEFINE_TYPE (GnmHistogramTool, gnm_histogram_tool, GNM_TYPE_GENERIC_ANALYSIS_TOOL)

static void
gnm_histogram_tool_init (GnmHistogramTool *tool)
{
	tool->predetermined = FALSE;
	tool->bin = NULL;
}

static void
gnm_histogram_tool_finalize (GObject *obj)
{
	GnmHistogramTool *tool = GNM_HISTOGRAM_TOOL (obj);
	value_release (tool->bin);
	G_OBJECT_CLASS (gnm_histogram_tool_parent_class)->finalize (obj);
}

static gboolean
gnm_histogram_tool_update_dao (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmHistogramTool *htool = GNM_HISTOGRAM_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &htool->parent;
	int i, j;

	analysis_tool_prepare_input_range (gtool);
	if (!analysis_tool_check_input_homogeneity (gtool)) {
		gtool->base.err = gtool->base.group_by + 1;
		return TRUE;
	}

	i = g_slist_length (gtool->base.input);
	if (htool->predetermined) {
		j = (htool->bin->v_range.cell.b.col - htool->bin->v_range.cell.a.col + 1) *
			(htool->bin->v_range.cell.b.row - htool->bin->v_range.cell.a.row + 1);
	} else
		j = htool->n;

	if (htool->bin_type & bintype_p_inf_lower) j++;
	if (htool->bin_type & bintype_m_inf_lower) j++;

	dao_adjust (dao, 1 + i, 1 + j);
	return FALSE;
}

static char *
gnm_histogram_tool_update_descriptor (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_command_descriptor (dao, _("Histogram (%s)"));
}

static gboolean
gnm_histogram_tool_prepare_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	dao_prepare_output (NULL, dao, _("Histogram"));
	return FALSE;
}

static gboolean
gnm_histogram_tool_format_output_range (G_GNUC_UNUSED GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	return dao_format_output (dao, _("Histogram"));
}

static gboolean
gnm_histogram_tool_perform_calc (GnmAnalysisTool *tool, data_analysis_output_t *dao)
{
	GnmHistogramTool *htool = GNM_HISTOGRAM_TOOL (tool);
	GnmGenericAnalysisTool *gtool = &htool->parent;
	GnmRange range;
	gint i, i_limit, i_start, i_end, col;
	GSList *l;
	gint to_col = (htool->cumulative) ? 0 : 1;
	GnmExpr const *expr_bin = NULL;
	GnmFunc *fd_index = NULL;
	char const *format;
	GnmFunc *fd_small = gnm_func_get_and_use ("SMALL");

	if (gtool->base.labels) {
		fd_index = gnm_func_get_and_use ("INDEX");
	}


	/* General Info */

	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Histogram"));

	/* Setting up the bins */

	if (htool->predetermined) {
		range_init_value (&range, htool->bin);
		i_limit = range_height (&range) * range_width (&range);
	} else {
		i_limit = htool->n;
	}

	i_end = i_limit;
	if (htool->bin_type & bintype_p_inf_lower)
		i_end++;
	if (htool->bin_type & bintype_m_inf_lower)
		i_end++;
	dao_set_format  (dao, to_col, 1, to_col, 1, "\"\";\"\"");
	format = (htool->bin_type & bintype_no_inf_upper) ?
		/* translator note: only translate the */
		/* "to below" and "up to" exclusive of */
		/* the quotation marks: */
		_("\"to below\" * General") : _("\"up to\" * General");
	dao_set_format  (dao, to_col, 2, to_col, i_end, format);

	if (htool->bin_type & bintype_m_inf_lower) {
		dao_set_cell_value (dao, to_col, 1, value_new_float (-GNM_MAX));
		i_start = 2;
	} else
		i_start = 1;

	if (htool->predetermined) {
		expr_bin = gnm_expr_new_constant (htool->bin);
		for (i = 0; i < i_limit; i++)
			dao_set_cell_expr (dao, to_col, i_start + i,
					   gnm_expr_new_funcall2 (fd_small,
								  gnm_expr_copy (expr_bin),
								  gnm_expr_new_constant
								  (value_new_int (i + 1))));
	} else {
		GnmValue *val = value_dup (gtool->base.input->data);
		GnmExpr const *expr_min;
		GnmExpr const *expr_max;

		if (gtool->base.labels)
			switch (gtool->base.group_by) {
			case GROUPED_BY_ROW:
				val->v_range.cell.a.col++;
				break;
			default:
				val->v_range.cell.a.row++;
				break;
			}

		if (htool->min_given)
			dao_set_cell_float (dao, to_col, i_start, htool->min);
		else {
			GnmFunc *fd_min = gnm_func_get_and_use ("MIN");
			dao_set_cell_expr (dao, to_col, i_start,
					   gnm_expr_new_funcall1
					   (fd_min,
					    gnm_expr_new_constant (value_dup (val))));
			gnm_func_dec_usage (fd_min);
		}

		if (htool->max_given)
			dao_set_cell_float (dao, to_col, i_start + i_limit - 1, htool->max);
		else {
			GnmFunc *fd_max = gnm_func_get_and_use ("MAX");
			dao_set_cell_expr (dao, to_col, i_start + i_limit - 1,
					   gnm_expr_new_funcall1
					   (fd_max,
					    gnm_expr_new_constant (value_dup (val))));
			gnm_func_dec_usage (fd_max);
		}

		value_release (val);

		expr_min = dao_get_cellref (dao, to_col, i_start);
		expr_max = dao_get_cellref (dao, to_col, i_start + i_limit - 1);

		for (i = 1; i < i_limit - 1; i++)
			dao_set_cell_expr (dao, to_col, i_start + i,
					   gnm_expr_new_binary (gnm_expr_copy (expr_min),
								GNM_EXPR_OP_ADD,
								gnm_expr_new_binary
								(gnm_expr_new_constant (value_new_int (i)),
								 GNM_EXPR_OP_MULT,
								 gnm_expr_new_binary
								 (gnm_expr_new_binary
								  (gnm_expr_copy (expr_max),
								   GNM_EXPR_OP_SUB,
								   gnm_expr_copy (expr_min)),
								  GNM_EXPR_OP_DIV,
								  gnm_expr_new_constant (value_new_int (htool->n - 1))))));

		gnm_expr_free (expr_min);
		gnm_expr_free (expr_max);
	}

	if (htool->bin_type & bintype_p_inf_lower) {
		dao_set_format  (dao, to_col, i_end, to_col, i_end,
		/* translator note: only translate the */
		/* "to" and "\xe2\x88\x9e" exclusive of */
		/* the quotation marks: */
				 _("\"to\" * \"\xe2\x88\x9e\""));
		dao_set_cell_value (dao, to_col, i_end, value_new_float (GNM_MAX));
	}

	/* format the lower end of the bins */

	if (!htool->cumulative) {
		GnmExpr const *expr_cr = make_cellref (1,-1);

		format = (htool->bin_type & bintype_no_inf_upper) ?
		/* translator note: only translate the */
		/* "from" and "above" exclusive of */
		/* the quotation marks: */
			_("\"from\" * General") : _("\"above\" * General");
		dao_set_format  (dao, 0, 2, 0, i_end, format);
		if (htool->bin_type & bintype_m_inf_lower)
			dao_set_format  (dao, 0, 2, 0, 2,
		/* translator note: only translate the */
		/* "from" and "\xe2\x88\x92\xe2\x88\x9e" exclusive of */
		/* the quotation marks: */
					 _("\"from\" * \"\xe2\x88\x92\xe2\x88\x9e\";"
					   "\"from\" * \"\xe2\x88\x92\xe2\x88\x9e\""));
		for (i = 2; i <= i_end; i++)
			dao_set_cell_expr (dao, 0, i, gnm_expr_copy (expr_cr));

		gnm_expr_free (expr_cr);
	}

	/* insert formulas for histogram values */

	for (l = gtool->base.input, col = to_col + 1; l; col++, l = l->next) {
		GnmValue *val = l->data;
		GnmValue *val_c = NULL;

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
			char const *format;

			switch (gtool->base.group_by) {
			case GROUPED_BY_ROW:
				format = _("Row %d");
				break;
			case GROUPED_BY_COL:
				format = _("Column %d");
				break;
			default:
				format = _("Area %d");
				break;
			}
			dao_set_cell_printf (dao, col, 1, format, col - to_col);
		}

		if (htool->percentage)
			dao_set_format (dao, col, 2, col, i_end, "0.0%");

		for (i = 2; i <= i_end; i++) {
			gboolean fromminf = (i == 2) &&
				(htool->bin_type & bintype_m_inf_lower);
			gboolean topinf = (i == i_end) &&
				(htool->bin_type & bintype_p_inf_lower);
			dao_set_cell_array_expr
				(dao, col, i,
				 make_hist_expr (htool, col, val,
						 fromminf, topinf, dao));
		}
	}


	if (expr_bin != NULL)
		gnm_expr_free (expr_bin);

	gnm_func_dec_usage (fd_small);
	if (fd_index != NULL)
		gnm_func_dec_usage (fd_index);

	/* Create Chart if requested */
	if (htool->chart != NO_CHART) {
		SheetObject *so;
		GogGraph     *graph;
		GogChart     *chart;
		GogPlot	     *plot;
		GogSeries    *series;
		gint limits_start, limits_end, values_start, values_end;
		GOData *limits;
		GOData *values;
		int ct;

		graph = g_object_new (GOG_TYPE_GRAPH, NULL);
		chart = GOG_CHART (gog_object_add_by_name (
						   GOG_OBJECT (graph), "Chart", NULL));

		if (htool->chart == HISTOGRAM_CHART) {
			plot = gog_plot_new_by_name ("GogHistogramPlot");
			limits_start =  i_start;
			limits_end =  i_start + i_limit - 1;
			values_start = i_start + 1;
			values_end = i_start + i_limit - 1;
		} else {
			plot = gog_plot_new_by_name ("GogBarColPlot");
			limits_start =  2;
			limits_end =  i_end;
			values_start = 2;
			values_end = i_end;
			if (htool->chart == BAR_CHART)
				go_object_toggle (plot, "horizontal");
		}

		gog_object_add_by_name (GOG_OBJECT (chart),
					"Plot", GOG_OBJECT (plot));

		limits = dao_go_data_vector (dao, to_col, limits_start,
					     to_col, limits_end);

		for (ct = 1; ct < (col - to_col); ct ++) {
			g_object_ref (limits);
			values = dao_go_data_vector (dao, to_col + ct, values_start,
						     to_col + ct, values_end);

			series = gog_plot_new_series (plot);
			gog_series_set_dim (series, 0, limits, NULL);
			gog_series_set_dim (series, 1, values, NULL);
		}
		g_object_unref (limits);

		if (htool->chart == HISTOGRAM_CHART) {
			GogObject *axis;
			GogObject *label;
			GnmExprTop const *label_string;
			GOData *data;
		        axis = gog_object_get_child_by_name (GOG_OBJECT (chart), "X-Axis");
			go_object_set_property (G_OBJECT (axis), "assigned-format-string-XL",
						"X-Axis Format", "0.0EE0",
						NULL, NULL);
			axis = gog_object_get_child_by_name (GOG_OBJECT (chart), "Y-Axis");
			label_string = gnm_expr_top_new_constant (value_new_string (_("Frequency Density")));
			data = gnm_go_data_scalar_new_expr (dao->sheet, label_string);
			label = gog_object_add_by_name (axis, "Label", NULL);
			gog_dataset_set_dim (GOG_DATASET (label), 0, data, NULL);
		} else if (htool->chart == COLUMN_CHART) {
			GogObject *axis;
			GogObject *label;
			GnmExprTop const *label_string;
			GOData *data;
		        axis = gog_object_get_child_by_name (GOG_OBJECT (chart), "X-Axis");
			go_object_set_property (G_OBJECT (axis), "assigned-format-string-XL",
						"X-Axis Format", "0.0EE0",
						NULL, NULL);
			axis = gog_object_get_child_by_name (GOG_OBJECT (chart), "Y-Axis");
			label_string = gnm_expr_top_new_constant (value_new_string (_("Frequency")));
			data = gnm_go_data_scalar_new_expr (dao->sheet, label_string);
			label = gog_object_add_by_name (axis, "Label", NULL);
			gog_dataset_set_dim (GOG_DATASET (label), 0, data, NULL);
		} else if (htool->chart == BAR_CHART) {
			GogObject *axis;
			GogObject *label;
			GnmExprTop const *label_string;
			GOData *data;
		        axis = gog_object_get_child_by_name (GOG_OBJECT (chart), "Y-Axis");
			go_object_set_property (G_OBJECT (axis), "assigned-format-string-XL",
						"X-Axis Format", "0.0EE0",
						NULL, NULL);
			axis = gog_object_get_child_by_name (GOG_OBJECT (chart), "X-Axis");
			label_string = gnm_expr_top_new_constant (value_new_string (_("Frequency")));
			data = gnm_go_data_scalar_new_expr (dao->sheet, label_string);
			label = gog_object_add_by_name (axis, "Label", NULL);
			gog_dataset_set_dim (GOG_DATASET (label), 0, data, NULL);
		}

		so = sheet_object_graph_new (graph);
		g_object_unref (graph);

		dao_set_sheet_object (dao, 0, 1, so);
	}

	dao_redraw_respan (dao);

	return FALSE;
}

static void
gnm_histogram_tool_class_init (GnmHistogramToolClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GnmAnalysisToolClass *at_class = GNM_ANALYSIS_TOOL_CLASS (klass);

	gobject_class->finalize = gnm_histogram_tool_finalize;
	at_class->update_dao = gnm_histogram_tool_update_dao;
	at_class->update_descriptor = gnm_histogram_tool_update_descriptor;
	at_class->prepare_output_range = gnm_histogram_tool_prepare_output_range;
	at_class->format_output_range = gnm_histogram_tool_format_output_range;
	at_class->perform_calc = gnm_histogram_tool_perform_calc;
}

GnmAnalysisTool *
gnm_histogram_tool_new (void)
{
	return g_object_new (GNM_TYPE_HISTOGRAM_TOOL, NULL);
}
