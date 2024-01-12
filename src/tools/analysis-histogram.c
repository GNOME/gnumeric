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
make_hist_expr (analysis_tools_data_histogram_t *info,
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
	GnmFunc *fd_count = info->percentage ?
		gnm_func_lookup_or_add_placeholder (info->only_numbers ? "COUNT" : "COUNTA") : NULL;
	GnmFunc *fd_isnumber = gnm_func_lookup_or_add_placeholder (info->only_numbers ? "ISNUMBER" : "ISBLANK");
	gint to_col = (info->cumulative) ? 0 : 1;

	if (info->bin_type & bintype_no_inf_upper) {
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

	if (info->cumulative)
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

	if (info->only_numbers)
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

	if (info->percentage)
		expr = gnm_expr_new_binary (expr,
					    GNM_EXPR_OP_DIV,
					    gnm_expr_new_funcall1
					    (fd_count,
					     expr_data));
	else
		gnm_expr_free (expr_data);

	return expr;
}

static gboolean
analysis_tool_histogram_engine_run (data_analysis_output_t *dao,
				    analysis_tools_data_histogram_t *info)
{
	GnmRange range;
	gint i, i_limit, i_start, i_end, col;
	GSList *l;
	gint to_col = (info->cumulative) ? 0 : 1;

	GnmExpr const *expr_bin = NULL;

	GnmFunc *fd_small;
	GnmFunc *fd_index = NULL;

	char const *format;

	fd_small = gnm_func_lookup_or_add_placeholder ("SMALL");
	gnm_func_inc_usage (fd_small);

	if (info->base.labels) {
		fd_index = gnm_func_lookup_or_add_placeholder ("INDEX");
		gnm_func_inc_usage (fd_index);
	}


	/* General Info */

	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Histogram"));

	/* Setting up the bins */

	if (info->predetermined) {
		range_init_value (&range, info->bin);
		i_limit = range_height (&range) * range_width (&range);
	} else {
		i_limit = info->n;
	}

	i_end = i_limit;
	if (info->bin_type & bintype_p_inf_lower)
		i_end++;
	if (info->bin_type & bintype_m_inf_lower)
		i_end++;
	dao_set_format  (dao, to_col, 1, to_col, 1, "\"\";\"\"");
	format = (info->bin_type & bintype_no_inf_upper) ?
		/* translator note: only translate the */
		/* "to below" and "up to" exclusive of */
		/* the quotation marks: */
		_("\"to below\" * General") : _("\"up to\" * General");
	dao_set_format  (dao, to_col, 2, to_col, i_end, format);

	if (info->bin_type & bintype_m_inf_lower) {
		dao_set_cell_value (dao, to_col, 1, value_new_float (-GNM_MAX));
		i_start = 2;
	} else
		i_start = 1;

	if (info->predetermined) {
		expr_bin = gnm_expr_new_constant (info->bin);
		for (i = 0; i < i_limit; i++)
			dao_set_cell_expr (dao, to_col, i_start + i,
					   gnm_expr_new_funcall2 (fd_small,
								  gnm_expr_copy (expr_bin),
								  gnm_expr_new_constant
								  (value_new_int (i + 1))));
	} else {
		GnmValue *val = value_dup (info->base.input->data);
		GnmExpr const *expr_min;
		GnmExpr const *expr_max;

		if (info->base.labels)
			switch (info->base.group_by) {
			case GROUPED_BY_ROW:
				val->v_range.cell.a.col++;
				break;
			default:
				val->v_range.cell.a.row++;
				break;
			}

		if (info->min_given)
			dao_set_cell_float (dao, to_col, i_start, info->min);
		else {
			GnmFunc *fd_min;

			fd_min = gnm_func_lookup_or_add_placeholder ("MIN");
			gnm_func_inc_usage (fd_min);
			dao_set_cell_expr (dao, to_col, i_start,
					   gnm_expr_new_funcall1
					   (fd_min,
					    gnm_expr_new_constant (value_dup (val))));
			gnm_func_dec_usage (fd_min);
		}

		if (info->max_given)
			dao_set_cell_float (dao, to_col, i_start + i_limit - 1, info->max);
		else {
			GnmFunc *fd_max;

			fd_max = gnm_func_lookup_or_add_placeholder ("MAX");
			gnm_func_inc_usage (fd_max);
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
								  gnm_expr_new_constant (value_new_int (info->n - 1))))));

		gnm_expr_free (expr_min);
		gnm_expr_free (expr_max);
	}

	if (info->bin_type & bintype_p_inf_lower) {
		dao_set_format  (dao, to_col, i_end, to_col, i_end,
		/* translator note: only translate the */
		/* "to" and "\xe2\x88\x9e" exclusive of */
		/* the quotation marks: */
				 _("\"to\" * \"\xe2\x88\x9e\""));
		dao_set_cell_value (dao, to_col, i_end, value_new_float (GNM_MAX));
	}

	/* format the lower end of the bins */

	if (!info->cumulative) {
		GnmExpr const *expr_cr = make_cellref (1,-1);

		format = (info->bin_type & bintype_no_inf_upper) ?
		/* translator note: only translate the */
		/* "from" and "above" exclusive of */
		/* the quotation marks: */
			_("\"from\" * General") : _("\"above\" * General");
		dao_set_format  (dao, 0, 2, 0, i_end, format);
		if (info->bin_type & bintype_m_inf_lower)
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

	for (l = info->base.input, col = to_col + 1; l; col++, l = l->next) {
		GnmValue *val = l->data;
		GnmValue *val_c = NULL;

		dao_set_italic (dao, col, 1, col, 1);
		if (info->base.labels) {
			val_c = value_dup (val);
			switch (info->base.group_by) {
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

			switch (info->base.group_by) {
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

		if (info->percentage)
			dao_set_format (dao, col, 2, col, i_end, "0.0%");

		for (i = 2; i <= i_end; i++) {
			gboolean fromminf = (i == 2) &&
				(info->bin_type & bintype_m_inf_lower);
			gboolean topinf = (i == i_end) &&
				(info->bin_type & bintype_p_inf_lower);
			dao_set_cell_array_expr
				(dao, col, i,
				 make_hist_expr (info, col, val,
						 fromminf, topinf, dao));
		}
	}


	if (expr_bin != NULL)
		gnm_expr_free (expr_bin);

	gnm_func_dec_usage (fd_small);
	if (fd_index != NULL)
		gnm_func_dec_usage (fd_index);

	/* Create Chart if requested */
	if (info->chart != NO_CHART) {
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

		if (info->chart == HISTOGRAM_CHART) {
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
			if (info->chart == BAR_CHART)
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

		if (info->chart == HISTOGRAM_CHART) {
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
		} else if (info->chart == COLUMN_CHART) {
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
		} else if (info->chart == BAR_CHART) {
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


static gint
calc_length (GnmValue   *bin)
{
	g_return_val_if_fail (bin != NULL, 0);
	g_return_val_if_fail (VALUE_IS_CELLRANGE (bin), 0);

	return ((bin->v_range.cell.b.col - bin->v_range.cell.a.col + 1) *
		(bin->v_range.cell.b.row - bin->v_range.cell.a.row + 1));
}

gboolean
analysis_tool_histogram_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_histogram_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Histogram (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
	{
		int i, j;

		prepare_input_range (&info->base.input, info->base.group_by);

		i = 1 + ((info->predetermined) ? calc_length (info->bin) : info->n);
		if (info->bin_type & bintype_p_inf_lower)
			i++;
		if (info->bin_type & bintype_m_inf_lower)
			i++;

		j = g_slist_length (info->base.input) + ((info->cumulative) ? 1 : 2);

		dao_adjust (dao, j, i);

		return FALSE;
	}
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Histogram"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Histogram"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_histogram_engine_run (dao, specs);
	}
	return TRUE;
}




