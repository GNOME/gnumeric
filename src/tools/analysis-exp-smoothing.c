/*
 * analysis-exp-smoothing.c:
 *
  * This is a complete reimplementation of the exponential smoothing tool in tool in 2008
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
#include <tools/analysis-exp-smoothing.h>
#include <tools/analysis-tools.h>
#include <value.h>
#include <ranges.h>
#include <expr.h>
#include <graph.h>
#include <func.h>
#include <numbers.h>
#include <sheet-object-graph.h>
#include <goffice/goffice.h>
#include <sheet.h>


static GnmExpr const *
analysis_tool_exp_smoothing_funcall5 (GnmFunc *fd, GnmExpr const *ex, int y, int x, int dy, int dx)
{
	GnmExprList *list;
	list = gnm_expr_list_prepend (NULL, gnm_expr_new_constant (value_new_int (dx)));
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (dy)));
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (x)));
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (y)));
	list = gnm_expr_list_prepend (list, ex);

	return gnm_expr_new_funcall (fd, list);
}

static void
create_line_plot (GogPlot **plot, SheetObject **so)
{
		GogGraph     *graph;
		GogChart     *chart;

		graph = g_object_new (GOG_TYPE_GRAPH, NULL);
		chart = GOG_CHART (gog_object_add_by_name (GOG_OBJECT (graph), "Chart", NULL));
		*plot = gog_plot_new_by_name ("GogLinePlot");
		gog_object_add_by_name (GOG_OBJECT (chart), "Plot", GOG_OBJECT (*plot));
		*so = sheet_object_graph_new (graph);
		g_object_unref (graph);
}

static void
attach_series (GogPlot *plot, GOData *expr)
{
	GogSeries    *series;

	g_return_if_fail (plot != NULL);

	series = gog_plot_new_series (plot);
	gog_series_set_dim (series, 1, expr, NULL);
}

static gboolean
analysis_tool_exponential_smoothing_engine_ses_h_run (data_analysis_output_t *dao,
						analysis_tools_data_exponential_smoothing_t *info)
{
	GSList *l;
	gint col = 0;
	gint source;
	SheetObject *so = NULL;
	GogPlot	     *plot = NULL;
	GnmFunc *fd_index;
	GnmFunc *fd_offset;
	GnmFunc *fd_sqrt = NULL;
	GnmFunc *fd_sumxmy2 = NULL;
	GnmExpr const *expr_alpha = NULL;

	if (info->std_error_flag) {
		fd_sqrt = gnm_func_lookup_or_add_placeholder ("SQRT");
		gnm_func_inc_usage (fd_sqrt);
		fd_sumxmy2 = gnm_func_lookup_or_add_placeholder ("SUMXMY2");
		gnm_func_inc_usage (fd_sumxmy2);
	}

	fd_index = gnm_func_lookup_or_add_placeholder ("INDEX");
	gnm_func_inc_usage (fd_index);
	fd_offset = gnm_func_lookup_or_add_placeholder ("OFFSET");
	gnm_func_inc_usage (fd_offset);

	if (info->show_graph)
		create_line_plot (&plot, &so);

	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Exponential Smoothing"));
	dao_set_format  (dao, 0, 1, 0, 1, _("\"\xce\xb1 =\" * 0.000"));
	dao_set_cell_expr (dao, 0, 1, gnm_expr_new_constant (value_new_float (info->damp_fact)));
	expr_alpha = dao_get_cellref (dao, 0, 1);

	dao->offset_row = 2;

	for (l = info->base.input, source = 1; l; l = l->next, col++, source++) {
		GnmValue *val = value_dup ((GnmValue *)l->data);
		GnmValue *val_c = NULL;
		GnmExpr const *expr_title = NULL;
		GnmExpr const *expr_input = NULL;
		gint height;
		gint  x = 1;
		gint  y = 1;
		gint  *mover;
		guint delta_x = 1;
		guint delta_y = 1;
		gint row;
		Sheet *sheet;
		GnmEvalPos ep;

		sheet = val->v_range.cell.a.sheet;
		eval_pos_init_sheet (&ep, sheet);

		dao_set_italic (dao, col, 0, col, 0);
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
			expr_title = gnm_expr_new_funcall1 (fd_index,
							    gnm_expr_new_constant (val_c));

			dao_set_cell_expr (dao, col, 0, expr_title);
		} else
			dao_set_cell_printf
				(dao, col, 0,
				 (info->base.group_by == GROUPED_BY_ROW ?
				  _("Row %d") : _("Column %d")),
				 source);

		switch (info->base.group_by) {
		case GROUPED_BY_ROW:
			height = value_area_get_width (val, &ep);
			mover = &x;
			break;
		default:
			height = value_area_get_height (val, &ep);
			mover = &y;
			break;
		}

		expr_input = gnm_expr_new_constant (val);

		if (plot != NULL) {
			attach_series (plot, gnm_go_data_vector_new_expr (sheet, gnm_expr_top_new
									  (gnm_expr_copy (expr_input))));
			attach_series (plot, dao_go_data_vector (dao, col, 1, col, height));
		}

		/*  F(t+1) = F(t) + damp_fact * ( A(t) - F(t) ) */
		(*mover) = 1;
		dao_set_cell_expr (dao, col, 1,
				   gnm_expr_new_funcall1 (fd_index,
							  gnm_expr_copy (expr_input)));

		for (row = 2; row <= height; row++, (*mover)++) {
			GnmExpr const *A;
			GnmExpr const *F;

			A = gnm_expr_new_binary (gnm_expr_copy (expr_alpha),
						 GNM_EXPR_OP_MULT,
						 gnm_expr_new_funcall3
						 (fd_index,
						  gnm_expr_copy (expr_input),
						  gnm_expr_new_constant(value_new_int(y)),
						  gnm_expr_new_constant(value_new_int(x))));
			F = gnm_expr_new_binary (gnm_expr_new_binary (gnm_expr_new_constant
								      (value_new_int (1)),
								      GNM_EXPR_OP_SUB,
								      gnm_expr_copy (expr_alpha)),
						 GNM_EXPR_OP_MULT,
						 make_cellref (0, -1));
			dao_set_cell_expr (dao, col, row, gnm_expr_new_binary (A, GNM_EXPR_OP_ADD, F));
		}

		if (info->std_error_flag) {
			col++;
			dao_set_italic (dao, col, 0, col, 0);
			dao_set_cell (dao, col, 0, _("Standard Error"));

			y = 0;
			x = 0;
			(*mover) = 1;
			for (row = 1; row <= height; row++) {
				if (row > 1 && row <= height && (row - 1 - info->df) > 0) {
					GnmExpr const *expr_offset;

					if (info->base.group_by == GROUPED_BY_ROW)
						delta_x = row - 1;
					else
						delta_y = row - 1;

					expr_offset = analysis_tool_exp_smoothing_funcall5
						(fd_offset, gnm_expr_copy (expr_input), y, x, delta_y, delta_x);
					dao_set_cell_expr (dao, col, row,
							   gnm_expr_new_funcall1
							   (fd_sqrt,
							    gnm_expr_new_binary
							    (gnm_expr_new_funcall2
							     (fd_sumxmy2,
							      expr_offset,
							      make_rangeref (-1, 2 - row, -1, 0)),
							     GNM_EXPR_OP_DIV,
							     gnm_expr_new_constant (value_new_int
										    (row - 1 - info->df)))));
				} else
					dao_set_cell_na (dao, col, row);
			}
		}

		gnm_expr_free (expr_input);
	}

	if (so != NULL)
		dao_set_sheet_object (dao, 0, 1, so);

	gnm_expr_free (expr_alpha);
	if (fd_sqrt != NULL)
		gnm_func_dec_usage (fd_sqrt);
	if (fd_sumxmy2 != NULL)
		gnm_func_dec_usage (fd_sumxmy2);
	gnm_func_dec_usage (fd_offset);
	gnm_func_dec_usage (fd_index);

	dao_redraw_respan (dao);

	return FALSE;
}

static gboolean
analysis_tool_exponential_smoothing_engine_ses_r_run (data_analysis_output_t *dao,
						analysis_tools_data_exponential_smoothing_t *info)
{
	GSList *l;
	gint col = 0;
	gint source;
	SheetObject *so = NULL;
	GogPlot	     *plot = NULL;
	GnmFunc *fd_index;
	GnmFunc *fd_offset;
	GnmFunc *fd_average;
	GnmFunc *fd_sqrt = NULL;
	GnmFunc *fd_sumxmy2 = NULL;
	GnmExpr const *expr_alpha = NULL;

	if (info->std_error_flag) {
		fd_sqrt = gnm_func_lookup_or_add_placeholder ("SQRT");
		gnm_func_inc_usage (fd_sqrt);
		fd_sumxmy2 = gnm_func_lookup_or_add_placeholder ("SUMXMY2");
		gnm_func_inc_usage (fd_sumxmy2);
	}
	fd_average = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_average);
	fd_index = gnm_func_lookup_or_add_placeholder ("INDEX");
	gnm_func_inc_usage (fd_index);
	fd_offset = gnm_func_lookup_or_add_placeholder ("OFFSET");
	gnm_func_inc_usage (fd_offset);

	if (info->show_graph)
		create_line_plot (&plot, &so);

	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Exponential Smoothing"));
	dao_set_format  (dao, 0, 1, 0, 1, _("\"\xce\xb1 =\" * 0.000"));
	dao_set_cell_expr (dao, 0, 1, gnm_expr_new_constant (value_new_float (info->damp_fact)));
	expr_alpha = dao_get_cellref (dao, 0, 1);

	dao->offset_row = 2;

	for (l = info->base.input, source = 1; l; l = l->next, col++, source++) {
		GnmValue *val = value_dup ((GnmValue *)l->data);
		GnmValue *val_c = NULL;
		GnmExpr const *expr_title = NULL;
		GnmExpr const *expr_input = NULL;
		gint height;
		gint  x = 1;
		gint  y = 1;
		gint  *mover;
		guint delta_x = 1;
		guint delta_y = 1;
		gint row;
		Sheet *sheet;
		GnmEvalPos ep;

		sheet = val->v_range.cell.a.sheet;
		eval_pos_init_sheet (&ep, sheet);

		dao_set_italic (dao, col, 0, col, 0);
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
			expr_title = gnm_expr_new_funcall1 (fd_index,
							    gnm_expr_new_constant (val_c));

			dao_set_cell_expr (dao, col, 0, expr_title);
		} else
			dao_set_cell_printf
				(dao, col, 0,
				 (info->base.group_by == GROUPED_BY_ROW ?
				  _("Row %d") : _("Column %d")),
				 source);

		switch (info->base.group_by) {
		case GROUPED_BY_ROW:
			height = value_area_get_width (val, &ep);
			mover = &x;
			break;
		default:
			height = value_area_get_height (val, &ep);
			mover = &y;
			break;
		}

		expr_input = gnm_expr_new_constant (val);

		if (plot != NULL) {
			attach_series (plot, gnm_go_data_vector_new_expr (sheet, gnm_expr_top_new
									  (gnm_expr_copy (expr_input))));
			attach_series (plot, dao_go_data_vector (dao, col, 2, col, height + 1));
		}

		/*  F(t+1) = F(t) + damp_fact * ( A(t+1) - F(t) ) */

		x = 1;
		y = 1;
		*mover = 5;
		dao_set_cell_expr (dao, col, 1, gnm_expr_new_funcall1
				   (fd_average,
				    analysis_tool_exp_smoothing_funcall5 (fd_offset, gnm_expr_copy (expr_input), 0, 0, y, x)));
		x = 1;
		y = 1;
		(*mover) = 1;
		for (row = 1; row <= height; row++, (*mover)++) {
			GnmExpr const *A;
			GnmExpr const *F;

			A = gnm_expr_new_binary (gnm_expr_copy (expr_alpha),
						 GNM_EXPR_OP_MULT,
						 gnm_expr_new_funcall3
						 (fd_index,
						  gnm_expr_copy (expr_input),
						  gnm_expr_new_constant(value_new_int(y)),
						  gnm_expr_new_constant(value_new_int(x))));
			F = gnm_expr_new_binary (gnm_expr_new_binary (gnm_expr_new_constant
								      (value_new_int (1)),
								      GNM_EXPR_OP_SUB,
								      gnm_expr_copy (expr_alpha)),
						 GNM_EXPR_OP_MULT,
						 make_cellref (0, -1));
			dao_set_cell_expr (dao, col, row + 1, gnm_expr_new_binary (A, GNM_EXPR_OP_ADD, F));
		}

		if (info->std_error_flag) {
			col++;
			dao_set_italic (dao, col, 0, col, 0);
			dao_set_cell (dao, col, 0, _("Standard Error"));

			y = 0;
			x = 0;
			(*mover) = 0;
			for (row = 1; row <= height+1; row++) {
				if (row > 1 && (row - 1 - info->df) > 0) {
					GnmExpr const *expr_offset;

					if (info->base.group_by == GROUPED_BY_ROW)
						delta_x = row - 1;
					else
						delta_y = row - 1;

					expr_offset = analysis_tool_exp_smoothing_funcall5
						(fd_offset, gnm_expr_copy (expr_input), y, x, delta_y, delta_x);
					dao_set_cell_expr (dao, col, row,
							   gnm_expr_new_funcall1
							   (fd_sqrt,
							    gnm_expr_new_binary
							    (gnm_expr_new_funcall2
							     (fd_sumxmy2,
							      expr_offset,
							      make_rangeref (-1, 1 - row, -1, -1)),
							     GNM_EXPR_OP_DIV,
							     gnm_expr_new_constant (value_new_int
										    (row - 1 - info->df)))));
				} else
					dao_set_cell_na (dao, col, row);
			}
		}
		gnm_expr_free (expr_input);
	}

	if (so != NULL)
		dao_set_sheet_object (dao, 0, 1, so);

	gnm_expr_free (expr_alpha);
	if (fd_sqrt != NULL)
		gnm_func_dec_usage (fd_sqrt);
	if (fd_sumxmy2 != NULL)
		gnm_func_dec_usage (fd_sumxmy2);
	gnm_func_dec_usage (fd_average);
	gnm_func_dec_usage (fd_offset);
	gnm_func_dec_usage (fd_index);

	dao_redraw_respan (dao);

	return FALSE;
}

static gboolean
analysis_tool_exponential_smoothing_engine_des_run (data_analysis_output_t *dao,
						analysis_tools_data_exponential_smoothing_t *info)
{
	GSList *l;
	gint col = 0;
	gint source;
	SheetObject *so = NULL;
	GogPlot	     *plot = NULL;
	GnmFunc *fd_index;
	GnmFunc *fd_offset;
	GnmFunc *fd_linest;
	GnmFunc *fd_sqrt = NULL;
	GnmFunc *fd_sumxmy2 = NULL;
	GnmExpr const *expr_alpha = NULL;
	GnmExpr const *expr_gamma = NULL;

	if (info->std_error_flag) {
		fd_sqrt = gnm_func_lookup_or_add_placeholder ("SQRT");
		gnm_func_inc_usage (fd_sqrt);
		fd_sumxmy2 = gnm_func_lookup_or_add_placeholder ("SUMXMY2");
		gnm_func_inc_usage (fd_sumxmy2);
	}

	fd_linest = gnm_func_lookup_or_add_placeholder ("LINEST");
	gnm_func_inc_usage (fd_linest);
	fd_index = gnm_func_lookup_or_add_placeholder ("INDEX");
	gnm_func_inc_usage (fd_index);
	fd_offset = gnm_func_lookup_or_add_placeholder ("OFFSET");
	gnm_func_inc_usage (fd_offset);

	if (info->show_graph)
		create_line_plot (&plot, &so);

	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Exponential Smoothing"));

	dao_set_format  (dao, 0, 1, 0, 1, _("\"\xce\xb1 =\" * 0.000"));
	dao_set_cell_expr (dao, 0, 1, gnm_expr_new_constant (value_new_float (info->damp_fact)));
	expr_alpha = dao_get_cellref (dao, 0, 1);

	dao_set_format  (dao, 1, 1, 1, 1, _("\"\xce\xb3 =\" * 0.000"));
	dao_set_cell_expr (dao, 1, 1, gnm_expr_new_constant (value_new_float (info->g_damp_fact)));
	expr_gamma = dao_get_cellref (dao, 1, 1);

	dao->offset_row = 2;

	for (l = info->base.input, source = 1; l; l = l->next, col++, source++) {
		GnmValue *val = value_dup ((GnmValue *)l->data);
		GnmValue *val_c = NULL;
		GnmExpr const *expr_title = NULL;
		GnmExpr const *expr_input = NULL;
		gint height;
		gint  x = 1;
		gint  y = 1;
		gint  *mover;
		guint delta_x = 1;
		guint delta_y = 1;
		gint row;
		Sheet *sheet;
		GnmEvalPos ep;

		sheet = val->v_range.cell.a.sheet;
		eval_pos_init_sheet (&ep, sheet);

		dao_set_italic (dao, col, 0, col, 0);
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
			expr_title = gnm_expr_new_funcall1 (fd_index,
							    gnm_expr_new_constant (val_c));

			dao_set_cell_expr (dao, col, 0, expr_title);
		} else
			dao_set_cell_printf
				(dao, col, 0,
				 (info->base.group_by == GROUPED_BY_ROW ?
				  _("Row %d") : _("Column %d")),
				 source);

		switch (info->base.group_by) {
		case GROUPED_BY_ROW:
			height = value_area_get_width (val, &ep);
			mover = &x;
			break;
		default:
			height = value_area_get_height (val, &ep);
			mover = &y;
			break;
		}

		expr_input = gnm_expr_new_constant (val);

		if (plot != NULL) {
			attach_series (plot, gnm_go_data_vector_new_expr (sheet, gnm_expr_top_new
									  (gnm_expr_copy (expr_input))));
			attach_series (plot, dao_go_data_vector (dao, col, 2, col, height + 1));
		}

		if (dao_cell_is_visible (dao, col+1, 1))
		{
			GnmExpr const *expr_linest;

			x = 1;
			y = 1;
			*mover = 5;
			expr_linest = gnm_expr_new_funcall1
				(fd_linest,
				 analysis_tool_exp_smoothing_funcall5 (fd_offset, gnm_expr_copy (expr_input), 0, 0, y, x));
			dao_set_cell_expr (dao, col, 1,
					   gnm_expr_new_funcall3 (fd_index,
								  gnm_expr_copy (expr_linest),
								  gnm_expr_new_constant (value_new_int (1)),
								  gnm_expr_new_constant (value_new_int (2))));
			dao_set_cell_expr (dao, col + 1, 1,
					   gnm_expr_new_funcall3 (fd_index,
								  expr_linest,
								  gnm_expr_new_constant (value_new_int (1)),
								  gnm_expr_new_constant (value_new_int (1))));

			*mover = 1;
			for (row = 1; row <= height; row++, (*mover)++) {
				GnmExpr const *LB;
				GnmExpr const *A;
				GnmExpr const *LL;
				GnmExpr const *B;
				A = gnm_expr_new_binary (gnm_expr_copy (expr_alpha),
							 GNM_EXPR_OP_MULT,
							 gnm_expr_new_funcall3
							 (fd_index,
							  gnm_expr_copy (expr_input),
							  gnm_expr_new_constant(value_new_int(y)),
							  gnm_expr_new_constant(value_new_int(x))));
				LB = gnm_expr_new_binary (gnm_expr_new_binary (gnm_expr_new_constant
									       (value_new_int (1)),
									       GNM_EXPR_OP_SUB,
									       gnm_expr_copy (expr_alpha)),
							  GNM_EXPR_OP_MULT,
							  gnm_expr_new_binary (make_cellref (0, -1),
									       GNM_EXPR_OP_ADD,
									       make_cellref (1, -1)));
				dao_set_cell_expr (dao, col, row + 1, gnm_expr_new_binary (A, GNM_EXPR_OP_ADD, LB));

				LL = gnm_expr_new_binary (gnm_expr_copy (expr_gamma),
							  GNM_EXPR_OP_MULT,
							  gnm_expr_new_binary (make_cellref (-1, 0),
									       GNM_EXPR_OP_SUB,
									       make_cellref (-1, -1)));
				B = gnm_expr_new_binary (gnm_expr_new_binary (gnm_expr_new_constant
									      (value_new_int (1)),
									      GNM_EXPR_OP_SUB,
									      gnm_expr_copy (expr_gamma)),
							 GNM_EXPR_OP_MULT,
							 make_cellref (0, -1));
				dao_set_cell_expr (dao, col + 1, row + 1, gnm_expr_new_binary (LL, GNM_EXPR_OP_ADD, B));
			}

			col++;

			if (info->std_error_flag) {
				col++;
				dao_set_italic (dao, col, 0, col, 0);
				dao_set_cell (dao, col, 0, _("Standard Error"));

				y = 0;
				x = 0;
				(*mover) = 0;
				for (row = 1; row <= height+1; row++) {
					if (row > 1 && (row - 1 - info->df) > 0) {
						GnmExpr const *expr_offset;

						if (info->base.group_by == GROUPED_BY_ROW)
							delta_x = row - 1;
						else
							delta_y = row - 1;

						expr_offset = analysis_tool_exp_smoothing_funcall5
							(fd_offset, gnm_expr_copy (expr_input), y, x, delta_y, delta_x);

						dao_set_cell_expr (dao, col, row,
								   gnm_expr_new_funcall1
								   (fd_sqrt,
								    gnm_expr_new_binary
								    (gnm_expr_new_funcall2
								     (fd_sumxmy2,
								      expr_offset,
								      gnm_expr_new_binary(make_rangeref (-2, 1 - row, -2, -1),
											  GNM_EXPR_OP_ADD,
											  make_rangeref (-1, 1 - row, -1, -1))),
								     GNM_EXPR_OP_DIV,
								     gnm_expr_new_constant (value_new_int
											    (row - 1 - info->df)))));
					} else
						dao_set_cell_na (dao, col, row);
				}
			}
			gnm_expr_free (expr_input);
		} else {
			dao_set_cell (dao, col, 1, _("Holt's trend corrected exponential\n"
						     "smoothing requires at least 2\n"
						     "output columns for each data set."));
			dao_set_cell_comment (dao, col, 0, _("Holt's trend corrected exponential\n"
							     "smoothing requires at least 2\n"
							     "output columns for each data set."));
			value_release (val);
		}
	}

	if (so != NULL)
		dao_set_sheet_object (dao, 0, 1, so);

	gnm_expr_free (expr_alpha);
	gnm_expr_free (expr_gamma);
	if (fd_sqrt != NULL)
		gnm_func_dec_usage (fd_sqrt);
	if (fd_sumxmy2 != NULL)
		gnm_func_dec_usage (fd_sumxmy2);
	gnm_func_dec_usage (fd_linest);
	gnm_func_dec_usage (fd_offset);
	gnm_func_dec_usage (fd_index);

	dao_redraw_respan (dao);

	return FALSE;
}


static gboolean
analysis_tool_exponential_smoothing_engine_ates_run (data_analysis_output_t *dao,
						     analysis_tools_data_exponential_smoothing_t *info)
{
	GSList *l;
	gint col = 0, time, maxheight;
	gint source;
	SheetObject *so = NULL;
	GogPlot	     *plot = NULL;
	GnmFunc *fd_index;
	GnmFunc *fd_linest;
	GnmFunc *fd_average;
	GnmFunc *fd_if;
	GnmFunc *fd_mod;
	GnmFunc *fd_row;
	GnmFunc *fd_sqrt = NULL;
	GnmFunc *fd_sumxmy2 = NULL;
	GnmExpr const *expr_alpha = NULL;
	GnmExpr const *expr_gamma = NULL;
	GnmExpr const *expr_delta = NULL;

	if (info->std_error_flag) {
		fd_sqrt = gnm_func_lookup_or_add_placeholder ("SQRT");
		gnm_func_inc_usage (fd_sqrt);
		fd_sumxmy2 = gnm_func_lookup_or_add_placeholder ("SUMXMY2");
		gnm_func_inc_usage (fd_sumxmy2);
	}

	fd_linest = gnm_func_lookup_or_add_placeholder ("LINEST");
	gnm_func_inc_usage (fd_linest);
	fd_index = gnm_func_lookup_or_add_placeholder ("INDEX");
	gnm_func_inc_usage (fd_index);
	fd_average = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_average);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF");
	gnm_func_inc_usage (fd_if);
	fd_mod = gnm_func_lookup_or_add_placeholder ("mod");
	gnm_func_inc_usage (fd_mod);
	fd_row = gnm_func_lookup_or_add_placeholder ("row");
	gnm_func_inc_usage (fd_row);

	if (info->show_graph)
		create_line_plot (&plot, &so);

	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Exponential Smoothing"));

	dao_set_format  (dao, 2, 0, 2, 0, _("\"\xce\xb1 =\" * 0.000"));
	dao_set_cell_expr (dao, 2, 0, gnm_expr_new_constant (value_new_float (info->damp_fact)));
	expr_alpha = dao_get_cellref (dao, 2, 0);

	dao_set_format  (dao, 3, 0, 3, 0, _("\"\xce\xb3 =\" * 0.000"));
	dao_set_cell_expr (dao, 3, 0, gnm_expr_new_constant (value_new_float (info->g_damp_fact)));
	expr_gamma = dao_get_cellref (dao, 3, 0);

	dao_set_format  (dao, 4, 0, 4, 0, _("\"\xce\xb4 =\" * 0.000"));
	dao_set_cell_expr (dao, 4, 0, gnm_expr_new_constant (value_new_float (info->s_damp_fact)));
	expr_delta = dao_get_cellref (dao, 4, 0);

	dao_set_italic (dao, 0, 2, 0, 2);
	dao_set_cell (dao, 0, 2, _("Time"));

	maxheight = analysis_tool_calc_length (&info->base);

	dao->offset_row = 2 + info->s_period;

	for (time = 1 - info->s_period; time <= maxheight; time++)
		dao_set_cell_int (dao, 0, time, time);

	dao->offset_col = 1;

	for (l = info->base.input, source = 1; l; l = l->next, source++) {
		GnmValue *val = value_dup ((GnmValue *)l->data);
		GnmValue *val_c = NULL;
		GnmExpr const *expr_title = NULL;
		GnmExpr const *expr_input = NULL;
		GnmExpr const *expr_index = NULL;
		GnmExpr const *expr_linest;
		GnmExpr const *expr_level;
		GnmExpr const *expr_trend;
		GnmExpr const *expr_season;
		GnmExpr const *expr_season_est;
		GnmExpr const *expr_data;
		GnmExpr const *expr_linest_intercept;
		GnmExpr const *expr_linest_slope;
		gint height;
		GnmEvalPos ep;

		eval_pos_init_sheet (&ep, val->v_range.cell.a.sheet);

		if (dao_cell_is_visible (dao, col+3, 1))
		{
			dao_set_italic (dao, col + 1, -info->s_period, col + 3, -info->s_period);
			set_cell_text_row (dao, col + 1, -info->s_period, _("/Level"
									    "/Trend"
									    "/Seasonal Adjustment"));

			dao_set_italic (dao, col,  -info->s_period, col,  -info->s_period);
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
				expr_title = gnm_expr_new_funcall1 (fd_index,
								    gnm_expr_new_constant (val_c));

				dao_set_cell_expr (dao, col,  -info->s_period, expr_title);
			} else
				dao_set_cell_printf
					(dao, col,  -info->s_period,
					 (info->base.group_by  == GROUPED_BY_ROW ?
					  _("Row %d") : _("Column %d")),
					 source);


			switch (info->base.group_by) {
			case GROUPED_BY_ROW:
				height = value_area_get_width (val, &ep);
				expr_input = gnm_expr_new_constant (val);
				expr_index = gnm_expr_new_funcall3 (fd_index, gnm_expr_copy (expr_input),
								    gnm_expr_new_constant (value_new_int (1)),
								    make_cellref (-1 - col, 0));
				break;
			default:
				height = value_area_get_height (val, &ep);
				expr_input = gnm_expr_new_constant (val);
				expr_index = gnm_expr_new_funcall3 (fd_index, gnm_expr_copy (expr_input),
								    make_cellref (-1 - col, 0),
								    gnm_expr_new_constant (value_new_int (1)));
				break;
			}

			expr_data = dao_get_rangeref (dao, col, 1, col, height);
			expr_linest = gnm_expr_new_funcall1
				(fd_linest, gnm_expr_copy (expr_data));
			dao_set_cell_expr (dao, col+1, 0,
					   gnm_expr_new_funcall3 (fd_index,
								  gnm_expr_copy (expr_linest),
								  gnm_expr_new_constant (value_new_int (1)),
								  gnm_expr_new_constant (value_new_int (2))));
			expr_linest_intercept = dao_get_cellref (dao, col + 1, 0);
			dao_set_cell_expr (dao, col + 2, 0,
					   gnm_expr_new_funcall3 (fd_index,
								  expr_linest,
								  gnm_expr_new_constant (value_new_int (1)),
								  gnm_expr_new_constant (value_new_int (1))));
			expr_linest_slope = dao_get_cellref (dao, col + 2, 0);
			expr_level = gnm_expr_new_binary (gnm_expr_new_binary
							  (gnm_expr_copy (expr_alpha),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_binary
							   (make_cellref (-1,0),
							    GNM_EXPR_OP_SUB,
							    make_cellref (2,-info->s_period))),
							  GNM_EXPR_OP_ADD,
							  gnm_expr_new_binary
							  (gnm_expr_new_binary
							   (gnm_expr_new_constant (value_new_int (1)),
							    GNM_EXPR_OP_SUB,
							    gnm_expr_copy (expr_alpha)),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_binary
							   (make_cellref (0,-1),
							    GNM_EXPR_OP_ADD,
							    make_cellref (1,-1))));
			expr_trend = gnm_expr_new_binary (gnm_expr_new_binary
							  (gnm_expr_copy (expr_gamma),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_binary
							   (make_cellref (-1,0),
							    GNM_EXPR_OP_SUB,
							    make_cellref (-1,-1))),
							  GNM_EXPR_OP_ADD,
							  gnm_expr_new_binary
							  (gnm_expr_new_binary
							   (gnm_expr_new_constant (value_new_int (1)),
							    GNM_EXPR_OP_SUB,
							    gnm_expr_copy (expr_gamma)),
							   GNM_EXPR_OP_MULT,
							   make_cellref (0,-1)));
			expr_season = gnm_expr_new_binary (gnm_expr_new_binary
							  (gnm_expr_copy (expr_delta),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_binary
							   (make_cellref (-3,0),
							    GNM_EXPR_OP_SUB,
							    make_cellref (-2,0))),
							  GNM_EXPR_OP_ADD,
							  gnm_expr_new_binary
							  (gnm_expr_new_binary
							   (gnm_expr_new_constant (value_new_int (1)),
							    GNM_EXPR_OP_SUB,
							    gnm_expr_copy (expr_delta)),
							   GNM_EXPR_OP_MULT,
							   make_cellref (0,-info->s_period)));

			for (time = 1; time <= maxheight; time++) {
				dao_set_cell_expr (dao, col, time, gnm_expr_copy (expr_index));
				dao_set_cell_expr (dao, col+1, time, gnm_expr_copy (expr_level));
				dao_set_cell_expr (dao, col+2, time, gnm_expr_copy (expr_trend));
				dao_set_cell_expr (dao, col+3, time, gnm_expr_copy (expr_season));
			}
			gnm_expr_free (expr_index);
			gnm_expr_free (expr_level);
			gnm_expr_free (expr_trend);
			gnm_expr_free (expr_season);

			if (plot != NULL) {
				attach_series (plot, dao_go_data_vector (dao, col, 1, col, height));
				attach_series (plot, dao_go_data_vector (dao, col+1, 1, col+1, height));
			}

			/* We still need to calculate the estimates for the seasonal adjustment. */

			expr_season_est = gnm_expr_new_funcall1
				(fd_average,
				 gnm_expr_new_funcall3
				 (fd_if,
				  gnm_expr_new_binary
				  (gnm_expr_new_funcall2
				   (fd_mod,
				    gnm_expr_new_binary
				    (gnm_expr_new_funcall1
				     (fd_row,
				      gnm_expr_copy (expr_data)),
				     GNM_EXPR_OP_SUB,
				     gnm_expr_new_funcall (fd_row, NULL)),
				    gnm_expr_new_constant (value_new_int (info->s_period))),
				   GNM_EXPR_OP_EQUAL,
				   gnm_expr_new_constant (value_new_int (0))),
				  gnm_expr_new_binary
				  (expr_data,
				   GNM_EXPR_OP_SUB,
					  gnm_expr_new_binary
				  (expr_linest_intercept,
				   GNM_EXPR_OP_ADD,
				   gnm_expr_new_binary
				   (dao_get_rangeref (dao, -1, 1, -1, height),
				    GNM_EXPR_OP_MULT,
				    expr_linest_slope))),
				  gnm_expr_new_constant (value_new_string ("NA"))));

			for (time = 0; time > -info->s_period; time--)
				dao_set_cell_array_expr (dao, col+3, time, gnm_expr_copy (expr_season_est));

			gnm_expr_free (expr_season_est);

			col += 4;
			if (info->std_error_flag) {
				int row;

				dao_set_italic (dao, col, - info->s_period, col, - info->s_period);
				dao_set_cell (dao, col, - info->s_period, _("Standard Error"));

				for (row = 1; row <= height; row++) {
					if (row > 1 && (row - info->df) > 0) {
						GnmExpr const *expr_stderr;

						expr_stderr = gnm_expr_new_funcall1
							(fd_sqrt,
							 gnm_expr_new_binary
							 (gnm_expr_new_funcall2
							  (fd_sumxmy2,
							   make_rangeref (-4, 1 - row, -4, 0),
							   gnm_expr_new_binary
							   (make_rangeref (-1, 1 - row - info->s_period,
									   -1,  - info->s_period),
							    GNM_EXPR_OP_ADD,
							    gnm_expr_new_binary
							    (make_rangeref (-2, - row, -2, -1),
							     GNM_EXPR_OP_ADD,
							     make_rangeref (-3, - row, -3, -1)))),
							  GNM_EXPR_OP_DIV,
							  gnm_expr_new_constant (value_new_int
										 (row - info->df))));
						dao_set_cell_expr (dao, col, row, expr_stderr);
					} else
						dao_set_cell_na (dao, col, row);
				}
				col++;
			}
			gnm_expr_free (expr_input);
		} else {
			dao_set_cell (dao, col, 0, _("The additive Holt-Winters exponential\n"
						     "smoothing method requires at least 4\n"
						     "output columns for each data set."));
			dao_set_cell_comment (dao, col, 0, _("The additive Holt-Winters exponential\n"
							     "smoothing method requires at least 4\n"
							     "output columns for each data set."));
			value_release (val);
		}
	}

	if (so != NULL)
		dao_set_sheet_object (dao, 0, 1, so);

	gnm_expr_free (expr_alpha);
	gnm_expr_free (expr_gamma);
	gnm_expr_free (expr_delta);
	if (fd_sqrt != NULL)
		gnm_func_dec_usage (fd_sqrt);
	if (fd_sumxmy2 != NULL)
		gnm_func_dec_usage (fd_sumxmy2);
	gnm_func_dec_usage (fd_linest);
	gnm_func_dec_usage (fd_index);
	gnm_func_dec_usage (fd_average);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_mod);
	gnm_func_dec_usage (fd_row);

	dao_redraw_respan (dao);

	return FALSE;
}

static gboolean
analysis_tool_exponential_smoothing_engine_mtes_run (data_analysis_output_t *dao,
						     analysis_tools_data_exponential_smoothing_t *info)
{
	GSList *l;
	gint col = 0, time, maxheight;
	gint source;
	SheetObject *so = NULL;
	GogPlot	     *plot = NULL;
	GnmFunc *fd_index;
	GnmFunc *fd_offset;
	GnmFunc *fd_linest;
	GnmFunc *fd_average;
	GnmFunc *fd_if;
	GnmFunc *fd_mod;
	GnmFunc *fd_row;
	GnmFunc *fd_sqrt = NULL;
	GnmFunc *fd_sumsq = NULL;
	GnmExpr const *expr_alpha = NULL;
	GnmExpr const *expr_gamma = NULL;
	GnmExpr const *expr_delta = NULL;

	if (info->std_error_flag) {
		fd_sqrt = gnm_func_lookup_or_add_placeholder ("SQRT");
		gnm_func_inc_usage (fd_sqrt);
		fd_sumsq = gnm_func_lookup_or_add_placeholder ("SUMSQ");
		gnm_func_inc_usage (fd_sumsq);
	}

	fd_linest = gnm_func_lookup_or_add_placeholder ("LINEST");
	gnm_func_inc_usage (fd_linest);
	fd_index = gnm_func_lookup_or_add_placeholder ("INDEX");
	gnm_func_inc_usage (fd_index);
	fd_offset = gnm_func_lookup_or_add_placeholder ("OFFSET");
	gnm_func_inc_usage (fd_offset);
	fd_average = gnm_func_lookup_or_add_placeholder ("AVERAGE");
	gnm_func_inc_usage (fd_average);
	fd_if = gnm_func_lookup_or_add_placeholder ("IF");
	gnm_func_inc_usage (fd_if);
	fd_mod = gnm_func_lookup_or_add_placeholder ("mod");
	gnm_func_inc_usage (fd_mod);
	fd_row = gnm_func_lookup_or_add_placeholder ("row");
	gnm_func_inc_usage (fd_row);

	if (info->show_graph)
		create_line_plot (&plot, &so);

	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Exponential Smoothing"));

	dao_set_format  (dao, 2, 0, 2, 0, _("\"\xce\xb1 =\" * 0.000"));
	dao_set_cell_expr (dao, 2, 0, gnm_expr_new_constant (value_new_float (info->damp_fact)));
	expr_alpha = dao_get_cellref (dao, 2, 0);

	dao_set_format  (dao, 3, 0, 3, 0, _("\"\xce\xb3 =\" * 0.000"));
	dao_set_cell_expr (dao, 3, 0, gnm_expr_new_constant (value_new_float (info->g_damp_fact)));
	expr_gamma = dao_get_cellref (dao, 3, 0);

	dao_set_format  (dao, 4, 0, 4, 0, _("\"\xce\xb4 =\" * 0.000"));
	dao_set_cell_expr (dao, 4, 0, gnm_expr_new_constant (value_new_float (info->s_damp_fact)));
	expr_delta = dao_get_cellref (dao, 4, 0);

	dao_set_italic (dao, 0, 2, 0, 2);
	dao_set_cell (dao, 0, 2, _("Time"));

	maxheight = analysis_tool_calc_length (&info->base);

	dao->offset_row = 2 + info->s_period;

	for (time = 1 - info->s_period; time <= maxheight; time++)
		dao_set_cell_int (dao, 0, time, time);

	dao->offset_col = 1;

	for (l = info->base.input, source = 1; l; l = l->next, source++) {
		GnmValue *val = value_dup ((GnmValue *)l->data);
		GnmValue *val_c = NULL;
		GnmExpr const *expr_title = NULL;
		GnmExpr const *expr_input = NULL;
		GnmExpr const *expr_index = NULL;
		GnmExpr const *expr_linest;
		GnmExpr const *expr_level;
		GnmExpr const *expr_trend;
		GnmExpr const *expr_season;
		GnmExpr const *expr_season_est;
		GnmExpr const *expr_season_denom;
		GnmExpr const *expr_data;
		GnmExpr const *expr_linest_intercept;
		GnmExpr const *expr_linest_slope;
		gint height, starting_length, i;
		GnmExprList *args = NULL;
		GnmEvalPos ep;

		eval_pos_init_sheet (&ep, val->v_range.cell.a.sheet);

		if (dao_cell_is_visible (dao, col+3, 1))
		{
			dao_set_italic (dao, col + 1, -info->s_period, col + 3, -info->s_period);
			set_cell_text_row (dao, col + 1, -info->s_period, _("/Level"
									    "/Trend"
									    "/Seasonal Adjustment"));

			dao_set_italic (dao, col,  -info->s_period, col,  -info->s_period);
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
				expr_title = gnm_expr_new_funcall1 (fd_index,
								    gnm_expr_new_constant (val_c));

				dao_set_cell_expr (dao, col,  -info->s_period, expr_title);
			} else
				dao_set_cell_printf
					(dao, col,  -info->s_period,
					 (info->base.group_by  == GROUPED_BY_ROW ?
					  _("Row %d") : _("Column %d")),
					 source);


			switch (info->base.group_by) {
			case GROUPED_BY_ROW:
				height = value_area_get_width (val, &ep);
				expr_input = gnm_expr_new_constant (val);
				expr_index = gnm_expr_new_funcall3 (fd_index, gnm_expr_copy (expr_input),
								    gnm_expr_new_constant (value_new_int (1)),
								    make_cellref (-1 - col, 0));
				break;
			default:
				height = value_area_get_height (val, &ep);
				expr_input = gnm_expr_new_constant (val);
				expr_index = gnm_expr_new_funcall3 (fd_index, gnm_expr_copy (expr_input),
								    make_cellref (-1 - col, 0),
								    gnm_expr_new_constant (value_new_int (1)));
				break;
			}
			starting_length = 4 * info->s_period;
			if (starting_length > height)
				starting_length = height;
			expr_data = analysis_tool_exp_smoothing_funcall5 (fd_offset,
									  dao_get_rangeref (dao, col, 1, col, height),
									  0, 0, starting_length, 1);
			expr_linest = gnm_expr_new_funcall1 (fd_linest, gnm_expr_copy (expr_data));
			dao_set_cell_expr (dao, col+1, 0,
					   gnm_expr_new_funcall3 (fd_index,
								  gnm_expr_copy (expr_linest),
								  gnm_expr_new_constant (value_new_int (1)),
								  gnm_expr_new_constant (value_new_int (2))));
			expr_linest_intercept = dao_get_cellref (dao, col + 1, 0);
			dao_set_cell_expr (dao, col + 2, 0,
					   gnm_expr_new_funcall3 (fd_index,
								  expr_linest,
								  gnm_expr_new_constant (value_new_int (1)),
								  gnm_expr_new_constant (value_new_int (1))));
			expr_linest_slope = dao_get_cellref (dao, col + 2, 0);
			expr_level = gnm_expr_new_binary (gnm_expr_new_binary
							  (gnm_expr_copy (expr_alpha),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_binary
							   (make_cellref (-1,0),
							    GNM_EXPR_OP_DIV,
							    make_cellref (2,-info->s_period))),
							  GNM_EXPR_OP_ADD,
							  gnm_expr_new_binary
							  (gnm_expr_new_binary
							   (gnm_expr_new_constant (value_new_int (1)),
							    GNM_EXPR_OP_SUB,
							    gnm_expr_copy (expr_alpha)),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_binary
							   (make_cellref (0,-1),
							    GNM_EXPR_OP_ADD,
							    make_cellref (1,-1))));
			expr_trend = gnm_expr_new_binary (gnm_expr_new_binary
							  (gnm_expr_copy (expr_gamma),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_binary
							   (make_cellref (-1,0),
							    GNM_EXPR_OP_SUB,
							    make_cellref (-1,-1))),
							  GNM_EXPR_OP_ADD,
							  gnm_expr_new_binary
							  (gnm_expr_new_binary
							   (gnm_expr_new_constant (value_new_int (1)),
							    GNM_EXPR_OP_SUB,
							    gnm_expr_copy (expr_gamma)),
							   GNM_EXPR_OP_MULT,
							   make_cellref (0,-1)));
			expr_season = gnm_expr_new_binary (gnm_expr_new_binary
							  (gnm_expr_copy (expr_delta),
							   GNM_EXPR_OP_MULT,
							   gnm_expr_new_binary
							   (make_cellref (-3,0),
							    GNM_EXPR_OP_DIV,
							    make_cellref (-2,0))),
							  GNM_EXPR_OP_ADD,
							  gnm_expr_new_binary
							  (gnm_expr_new_binary
							   (gnm_expr_new_constant (value_new_int (1)),
							    GNM_EXPR_OP_SUB,
							    gnm_expr_copy (expr_delta)),
							   GNM_EXPR_OP_MULT,
							   make_cellref (0,-info->s_period)));

			for (time = 1; time <= maxheight; time++) {
				dao_set_cell_expr (dao, col, time, gnm_expr_copy (expr_index));
				dao_set_cell_expr (dao, col+1, time, gnm_expr_copy (expr_level));
				dao_set_cell_expr (dao, col+2, time, gnm_expr_copy (expr_trend));
				dao_set_cell_expr (dao, col+3, time, gnm_expr_copy (expr_season));
			}
			gnm_expr_free (expr_index);
			gnm_expr_free (expr_level);
			gnm_expr_free (expr_trend);
			gnm_expr_free (expr_season);

			if (plot != NULL) {
				attach_series (plot, dao_go_data_vector (dao, col, 1, col, height));
				attach_series (plot, dao_go_data_vector (dao, col+1, 1, col+1, height));
			}

			/* We still need to calculate the estimates for the seasonal adjustment. */
			/* = average(if(mod(row(expr_data)-row(),4)=0,expr_data/($E$7+$F$7*$C$8:$C$23),"NA")) */
			for (i = 0; i<info->s_period; i++) {
				expr_season_est = gnm_expr_new_funcall1
					(fd_average,
					 gnm_expr_new_funcall3
					 (fd_if,
					  gnm_expr_new_binary
					  (gnm_expr_new_funcall2
					   (fd_mod,
					    gnm_expr_new_binary
					    (gnm_expr_new_funcall1
					     (fd_row,
					      gnm_expr_copy (expr_data)),
					     GNM_EXPR_OP_SUB,
					     gnm_expr_new_constant (value_new_int (i))),
					    gnm_expr_new_constant (value_new_int (info->s_period))),
					   GNM_EXPR_OP_EQUAL,
					   gnm_expr_new_constant (value_new_int (0))),
					  gnm_expr_new_binary
					  (gnm_expr_copy (expr_data),
					   GNM_EXPR_OP_DIV,
					   gnm_expr_new_binary
					   (gnm_expr_copy (expr_linest_intercept),
					    GNM_EXPR_OP_ADD,
					    gnm_expr_new_binary
					    (analysis_tool_exp_smoothing_funcall5
					     (fd_offset, dao_get_rangeref
					      (dao, -1, 1, -1, height), 0, 0, starting_length, 1),
					     GNM_EXPR_OP_MULT,
					     gnm_expr_copy (expr_linest_slope)))),
					  gnm_expr_new_constant (value_new_string ("NA"))));
				args = gnm_expr_list_prepend (args, expr_season_est);
			}
			expr_season_denom = gnm_expr_new_funcall (fd_average, args);

			expr_season_est = gnm_expr_new_funcall1
				(fd_average,
				 gnm_expr_new_funcall3
				 (fd_if,
				  gnm_expr_new_binary
				  (gnm_expr_new_funcall2
				   (fd_mod,
				    gnm_expr_new_binary
				    (gnm_expr_new_funcall1
				     (fd_row,
				      gnm_expr_copy (expr_data)),
				     GNM_EXPR_OP_SUB,
				     gnm_expr_new_funcall (fd_row, NULL)),
				    gnm_expr_new_constant (value_new_int (info->s_period))),
				   GNM_EXPR_OP_EQUAL,
				   gnm_expr_new_constant (value_new_int (0))),
				  gnm_expr_new_binary
				  (expr_data,
				   GNM_EXPR_OP_DIV,
				   gnm_expr_new_binary
				   (expr_linest_intercept,
				    GNM_EXPR_OP_ADD,
				    gnm_expr_new_binary
				    (analysis_tool_exp_smoothing_funcall5
				     (fd_offset, dao_get_rangeref
				      (dao, -1, 1, -1, height), 0, 0, starting_length, 1),
				     GNM_EXPR_OP_MULT,
				     expr_linest_slope))),
				  gnm_expr_new_constant (value_new_string ("NA"))));

			expr_season_est = gnm_expr_new_binary (expr_season_est,
							       GNM_EXPR_OP_DIV,
							       expr_season_denom);

			for (time = 0; time > -info->s_period; time--)
				dao_set_cell_array_expr (dao, col+3, time, gnm_expr_copy (expr_season_est));

			gnm_expr_free (expr_season_est);
			col += 4;
			if (info->std_error_flag) {
				int row;

				dao_set_italic (dao, col, - info->s_period, col, - info->s_period);
				dao_set_cell (dao, col, - info->s_period, _("Standard Error"));

				for (row = 1; row <= height; row++) {
					if (row > 1 && (row - info->df) > 0) {
						GnmExpr const *expr_stderr;
						GnmExpr const *expr_denom;

						expr_denom =  gnm_expr_new_binary
							(gnm_expr_new_binary
							 (make_rangeref (-2, - row, -2, -1),
							  GNM_EXPR_OP_ADD,
							  make_rangeref (-3, - row, -3, -1)),
							 GNM_EXPR_OP_MULT,
							 make_rangeref (-1, 1 - row - info->s_period,
									-1,  - info->s_period));
						expr_stderr = gnm_expr_new_funcall1
							(fd_sqrt,
							 gnm_expr_new_binary
							 (gnm_expr_new_funcall1
							  (fd_sumsq,
							   gnm_expr_new_binary
							   (gnm_expr_new_binary
							    (make_rangeref (-4, 1 - row, -4, 0),
							     GNM_EXPR_OP_SUB,
							     gnm_expr_copy (expr_denom)),
							    GNM_EXPR_OP_DIV,
							    expr_denom)),
							  GNM_EXPR_OP_DIV,
							  gnm_expr_new_constant (value_new_int
										 (row - info->df))));
						dao_set_cell_array_expr (dao, col, row, expr_stderr);
					} else
						dao_set_cell_na (dao, col, row);
				}
				col++;
			}
			gnm_expr_free (expr_input);
		} else {
			dao_set_cell (dao, col, 0, _("The multiplicative Holt-Winters exponential\n"
						     "smoothing method requires at least 4\n"
						     "output columns for each data set."));
			dao_set_cell_comment (dao, col, 0, _("The multiplicative Holt-Winters exponential\n"
							     "smoothing method requires at least 4\n"
							     "output columns for each data set."));
			value_release (val);
		}

	}

	if (so != NULL)
		dao_set_sheet_object (dao, 0, 1, so);

	gnm_expr_free (expr_alpha);
	gnm_expr_free (expr_gamma);
	gnm_expr_free (expr_delta);
	if (fd_sqrt != NULL)
		gnm_func_dec_usage (fd_sqrt);
	if (fd_sumsq != NULL)
		gnm_func_dec_usage (fd_sumsq);
	gnm_func_dec_usage (fd_linest);
	gnm_func_dec_usage (fd_offset);
	gnm_func_dec_usage (fd_index);
	gnm_func_dec_usage (fd_average);
	gnm_func_dec_usage (fd_if);
	gnm_func_dec_usage (fd_mod);
	gnm_func_dec_usage (fd_row);

	dao_redraw_respan (dao);

	return FALSE;
}

gboolean
analysis_tool_exponential_smoothing_engine (G_GNUC_UNUSED GOCmdContext *gcc, data_analysis_output_t *dao,
					    gpointer specs,
					    analysis_tool_engine_t selector,
					    gpointer result)
{
	analysis_tools_data_exponential_smoothing_t *info = specs;
	int n = 0, m;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Exponential Smoothing (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->base.input, info->base.group_by);
		n = 1;
		m = 3 + analysis_tool_calc_length (specs);
		if (info->std_error_flag)
			n++;
		if (info->es_type == exp_smoothing_type_ses_r) {
			m++;
		}
		if (info->es_type == exp_smoothing_type_des) {
			n++;
			m++;
		}
		if (info->es_type == exp_smoothing_type_ates ||
		    info->es_type == exp_smoothing_type_mtes) {
			n += 4;
			m += info->s_period;
		}
		dao_adjust (dao,
			    n * g_slist_length (info->base.input), m);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Exponential Smoothing"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Exponential Smoothing"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		switch (info->es_type) {
		case exp_smoothing_type_mtes:
			return analysis_tool_exponential_smoothing_engine_mtes_run (dao, specs);
		case exp_smoothing_type_ates:
			return analysis_tool_exponential_smoothing_engine_ates_run (dao, specs);
		case exp_smoothing_type_des:
			return analysis_tool_exponential_smoothing_engine_des_run (dao, specs);
		case exp_smoothing_type_ses_r:
			return analysis_tool_exponential_smoothing_engine_ses_r_run (dao, specs);
		case exp_smoothing_type_ses_h:
		default:
			return analysis_tool_exponential_smoothing_engine_ses_h_run (dao, specs);
		}
	}
	return TRUE;  /* We shouldn't get here */
}
