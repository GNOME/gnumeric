/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * analysis-histogram.c:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "analysis-exp-smoothing.h"
#include "analysis-tools.h"
#include "value.h"
#include "ranges.h"
#include "expr.h"
#include "func.h"
#include "numbers.h"
#include "sheet-object-graph.h"
#include <goffice/graph/gog-graph.h>
#include <goffice/graph/gog-object.h>
#include <goffice/graph/gog-chart.h>
#include <goffice/graph/gog-plot.h>
#include <goffice/graph/gog-series.h>
#include <goffice/utils/go-glib-extras.h>


static GnmExpr const *
analysis_tool_exp_smoothing_funcall5 (GnmFunc *fd, GnmExpr const *ex, int y, int x, int dy, int dx)
{
	GnmExprList *list;
	list = gnm_expr_list_prepend (NULL, gnm_expr_new_constant (value_new_int (dx))); 
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (dy))); 
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (x))); 
	list = gnm_expr_list_prepend (list, gnm_expr_new_constant (value_new_int (y))); 
	list = gnm_expr_list_prepend (list, gnm_expr_copy (ex));

	return gnm_expr_new_funcall (fd, list);
}

static void
create_line_plot (GogPlot **plot, SheetObject **so)
{
		GogGraph     *graph;
		GogChart     *chart;
		
		graph = g_object_new (GOG_GRAPH_TYPE, NULL);
		chart = GOG_CHART (gog_object_add_by_name (GOG_OBJECT (graph), "Chart", NULL));
		*plot = gog_plot_new_by_name ("GogLinePlot");
		gog_object_add_by_name (GOG_OBJECT (chart), "Plot", GOG_OBJECT (*plot));
		*so = sheet_object_graph_new (graph);
		g_object_unref (graph);
}

static void
attach_series (GogPlot *plot, GnmExpr const *expr)
{
	GogSeries    *series;
	
	if (plot == NULL)
		return;

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
	GnmExpr const *expr_gamma = NULL;

	if (info->std_error_flag) {
		fd_sqrt = gnm_func_lookup ("SQRT", NULL);
		gnm_func_ref (fd_sqrt);		
		fd_sumxmy2 = gnm_func_lookup ("SUMXMY2", NULL);
		gnm_func_ref (fd_sumxmy2);		
	}

	fd_index = gnm_func_lookup ("INDEX", NULL);
	gnm_func_ref (fd_index);		
	fd_offset = gnm_func_lookup ("OFFSET", NULL);
	gnm_func_ref (fd_offset);

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

			dao_set_italic (dao, col, 0, col, 0);
			dao_set_cell_expr (dao, col, 0, expr_title);
		} else
			dao_set_cell_printf 
				(dao, col, 0, 
				 (info->base.group_by ? _("Row %d") : _("Column %d")), 
				 source);

		switch (info->base.group_by) {
		case GROUPED_BY_ROW:
			height = value_area_get_width (val, NULL);
			mover = &x;
			break;
		default:
			height = value_area_get_height (val, NULL);
			mover = &y;
			break;
		}	

		sheet = val->v_range.cell.a.sheet;
		expr_input = gnm_expr_new_constant (val);

		attach_series (plot, gnm_go_data_vector_new_expr (sheet, gnm_expr_top_new (gnm_expr_copy (expr_input))));
		attach_series (plot, dao_go_data_vector (dao, col, 1, col, height));

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
						(fd_offset, expr_input, y, x, delta_y, delta_x);
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
	if (expr_gamma)
		gnm_expr_free (expr_gamma);
	if (fd_sqrt != NULL)
		gnm_func_unref (fd_sqrt);
	if (fd_sumxmy2 != NULL)
		gnm_func_unref (fd_sumxmy2);
	gnm_func_unref (fd_offset);
	gnm_func_unref (fd_index);

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
	GnmExpr const *expr_gamma = NULL;

	if (info->std_error_flag) {
		fd_sqrt = gnm_func_lookup ("SQRT", NULL);
		gnm_func_ref (fd_sqrt);		
		fd_sumxmy2 = gnm_func_lookup ("SUMXMY2", NULL);
		gnm_func_ref (fd_sumxmy2);		
	}
	fd_average = gnm_func_lookup ("AVERAGE", NULL);
	gnm_func_ref (fd_average);				
	fd_index = gnm_func_lookup ("INDEX", NULL);
	gnm_func_ref (fd_index);		
	fd_offset = gnm_func_lookup ("OFFSET", NULL);
	gnm_func_ref (fd_offset);

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

			dao_set_italic (dao, col, 0, col, 0);
			dao_set_cell_expr (dao, col, 0, expr_title);
		} else
			dao_set_cell_printf 
				(dao, col, 0, 
				 (info->base.group_by ? _("Row %d") : _("Column %d")), 
				 source);

		switch (info->base.group_by) {
		case GROUPED_BY_ROW:
			height = value_area_get_width (val, NULL);
			mover = &x;
			break;
		default:
			height = value_area_get_height (val, NULL);
			mover = &y;
			break;
		}	

		sheet = val->v_range.cell.a.sheet;
		expr_input = gnm_expr_new_constant (val);

		attach_series (plot, gnm_go_data_vector_new_expr (sheet, gnm_expr_top_new (gnm_expr_copy (expr_input))));
		attach_series (plot, dao_go_data_vector (dao, col, 2, col, height + 1));

		/*  F(t+1) = F(t) + damp_fact * ( A(t+1) - F(t) ) */
		
		x = 1;
		y = 1;
		*mover = 5;
		dao_set_cell_expr (dao, col, 1, gnm_expr_new_funcall1
				   (fd_average, 
				    analysis_tool_exp_smoothing_funcall5 (fd_offset, expr_input , 0, 0, y, x)));
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
						(fd_offset, expr_input, y, x, delta_y, delta_x);
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
	if (expr_gamma)
		gnm_expr_free (expr_gamma);
	if (fd_sqrt != NULL)
		gnm_func_unref (fd_sqrt);
	if (fd_sumxmy2 != NULL)
		gnm_func_unref (fd_sumxmy2);
	gnm_func_unref (fd_average);
	gnm_func_unref (fd_offset);
	gnm_func_unref (fd_index);

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
		fd_sqrt = gnm_func_lookup ("SQRT", NULL);
		gnm_func_ref (fd_sqrt);		
		fd_sumxmy2 = gnm_func_lookup ("SUMXMY2", NULL);
		gnm_func_ref (fd_sumxmy2);		
	}
		
	fd_linest = gnm_func_lookup ("LINEST", NULL);
	gnm_func_ref (fd_linest);				
	fd_index = gnm_func_lookup ("INDEX", NULL);
	gnm_func_ref (fd_index);		
	fd_offset = gnm_func_lookup ("OFFSET", NULL);
	gnm_func_ref (fd_offset);

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

			dao_set_italic (dao, col, 0, col, 0);
			dao_set_cell_expr (dao, col, 0, expr_title);
		} else
			dao_set_cell_printf 
				(dao, col, 0, 
				 (info->base.group_by ? _("Row %d") : _("Column %d")), 
				 source);

		switch (info->base.group_by) {
		case GROUPED_BY_ROW:
			height = value_area_get_width (val, NULL);
			mover = &x;
			break;
		default:
			height = value_area_get_height (val, NULL);
			mover = &y;
			break;
		}	

		sheet = val->v_range.cell.a.sheet;
		expr_input = gnm_expr_new_constant (val);

		attach_series (plot, gnm_go_data_vector_new_expr (sheet, gnm_expr_top_new (gnm_expr_copy (expr_input))));
		attach_series (plot, dao_go_data_vector (dao, col, 2, col, height + 1));

		if (dao_cell_is_visible (dao, col+1, 1))
		{
			GnmExpr const *expr_linest;
			
			x = 1;
			y = 1;
			*mover = 5;
			expr_linest = gnm_expr_new_funcall1
				(fd_linest, 
				 analysis_tool_exp_smoothing_funcall5 (fd_offset, expr_input , 0, 0, y, x));
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
		} else {
			dao_set_cell (dao, col, 1, _("Holt's trend corrected exponential\n"
						     "smoothing requires at least 2\n"
						     "output columns for each data set."));
			dao_set_cell_comment (dao, col, 0, _("Holt's trend corrected exponential\n"
							     "smoothing requires at least 2\n"
							     "output columns for each data set."));
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
						(fd_offset, expr_input, y, x, delta_y, delta_x);

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
	}

	if (so != NULL)
		dao_set_sheet_object (dao, 0, 1, so);

	gnm_expr_free (expr_alpha);
	if (expr_gamma)
		gnm_expr_free (expr_gamma);
	if (fd_sqrt != NULL)
		gnm_func_unref (fd_sqrt);
	if (fd_sumxmy2 != NULL)
		gnm_func_unref (fd_sumxmy2);
	gnm_func_unref (fd_linest);
	gnm_func_unref (fd_offset);
	gnm_func_unref (fd_index);

	dao_redraw_respan (dao);

	return FALSE;
}


gboolean
analysis_tool_exponential_smoothing_engine (data_analysis_output_t *dao,
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
