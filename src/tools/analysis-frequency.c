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

static gboolean
analysis_tool_frequency_engine_run (data_analysis_output_t *dao,
				    analysis_tools_data_frequency_t *info)
{
	gint i_limit, col;
	GSList *l;

	GnmFunc *fd_sum;
	GnmFunc *fd_if;
	GnmFunc *fd_index;
	GnmFunc *fd_isblank;
	GnmFunc *fd_rows = NULL;
	GnmFunc *fd_columns = NULL;
	GnmFunc *fd_exact = NULL;

	fd_sum = gnm_func_lookup_or_add_placeholder ("SUM");
	gnm_func_inc_usage (fd_sum);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF");
	gnm_func_inc_usage (fd_if);
	fd_index = gnm_func_lookup_or_add_placeholder ("INDEX");
	gnm_func_inc_usage (fd_index);
	fd_isblank = gnm_func_lookup_or_add_placeholder ("ISBLANK");
	gnm_func_inc_usage (fd_isblank);

	if (info->exact) {
		fd_exact = gnm_func_lookup_or_add_placeholder ("EXACT");
		gnm_func_inc_usage (fd_exact);
	}
	if (info->percentage) {
		fd_rows = gnm_func_lookup_or_add_placeholder ("ROWS");
		gnm_func_inc_usage (fd_rows);
		fd_columns = gnm_func_lookup_or_add_placeholder ("COLUMNS");
		gnm_func_inc_usage (fd_columns);
	}
	/* General Info */

	dao_set_italic (dao, 0, 0, 0, 1);
	set_cell_text_col (dao, 0, 0, _("/Frequency Table"
					"/Category"));

	/* Setting up the categories */

	if (info->predetermined) {
		int row = 2, i, j, i_h_limit, i_w_limit;
		GnmExpr const *expr_bin;
		GnmRange range;

		range_init_value (&range, info->bin);
		i_h_limit = range_height (&range);
		i_w_limit = range_width (&range);
		i_limit = i_h_limit * i_w_limit;

		expr_bin = gnm_expr_new_constant (info->bin);

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
		i_limit = info->n;
	}

	for (l = info->base.input, col = 1; l; col++, l = l->next) {
		GnmValue *val = value_dup ((GnmValue *)l->data);
		GnmValue *val_c = NULL;
		GnmExpr const *expr_count;
		GnmExpr const *expr_data;
		GnmExpr const *expr_if;
		int i, row = 2;


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
			char *txt;

			switch (info->base.group_by) {
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

		if (info->exact)
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

		if (info->percentage) {
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
	if (info->chart != NO_CHART) {
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
		if (info->chart == BAR_CHART)
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


static gint
calc_length (GnmValue   *bin)
{
	g_return_val_if_fail (bin != NULL, 0);
	g_return_val_if_fail (VALUE_IS_CELLRANGE (bin), 0);

	return ((bin->v_range.cell.b.col - bin->v_range.cell.a.col + 1) *
		(bin->v_range.cell.b.row - bin->v_range.cell.a.row + 1));
}


gboolean
analysis_tool_frequency_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_frequency_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Frequency Table (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
	{
		int i;

		prepare_input_range (&info->base.input, info->base.group_by);

		i = 2 + ((info->predetermined) ? calc_length (info->bin) : info->n);

		dao_adjust (dao, g_slist_length (info->base.input) + 1, i);

		return FALSE;
	}
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Frequency Table"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Frequency Table"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_frequency_engine_run (dao, specs);
	}
	return TRUE;
}




