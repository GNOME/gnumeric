/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "analysis-kaplan-meier.h"
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
#include <goffice/graph/gog-style.h>
#include <goffice/graph/gog-styled-object.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-marker.h>

static gboolean
analysis_tool_kaplan_meier_engine_run (data_analysis_output_t *dao,
				    analysis_tools_data_kaplan_meier_t *info)
{
	int rows, row;

	GnmExpr const *expr_data;
	GnmExpr const *expr_censor;
	GnmExpr const *expr_small;
	GnmExpr const *expr_time;
	GnmExpr const *expr_at_risk;
	GnmExpr const *expr_deaths;
	GnmExpr const *expr_prob_zero;
	GnmExpr const *expr_prob;
	GnmExpr const *expr_std_err = NULL;

	GnmFunc *fd_if;
	GnmFunc *fd_iserror;
	GnmFunc *fd_small;
	GnmFunc *fd_sum;
	GnmFunc *fd_sqrt = NULL;

	fd_small = gnm_func_lookup ("SMALL", NULL);
	gnm_func_ref (fd_small);
	fd_if = gnm_func_lookup ("IF", NULL);
	gnm_func_ref (fd_if);
	fd_iserror = gnm_func_lookup ("ISERROR", NULL);
	gnm_func_ref (fd_iserror);
	fd_sum = gnm_func_lookup ("SUM", NULL);
	gnm_func_ref (fd_sum);
	
	if (info->std_err) {
		fd_sqrt = gnm_func_lookup ("SQRT", NULL);
		gnm_func_ref (fd_sqrt);
	}

	rows =  info->base.range_1->v_range.cell.b.row 
		- info->base.range_1->v_range.cell.a.row + 1;

	dao_set_italic (dao, 0, 0, 0, 0);
	dao_set_cell (dao, 0, 0, _("Kaplan-Meier"));

	dao_set_italic (dao, 0, 1, 3, 1);
	set_cell_text_row (dao, 0, 1, 
			   "/Time"
			   "/At Risk"
			   "/Deaths"
			   "/Probability");
	if (info->std_err) {
		dao_set_italic (dao, 4, 1, 4, 1);
		dao_set_cell (dao, 4, 1, "Standard Error"); 
	}

	expr_data = gnm_expr_new_constant (value_dup (info->base.range_1));
	
	expr_at_risk = gnm_expr_new_funcall3 
		(fd_if,
		 gnm_expr_new_binary (make_cellref (-1, 0),
				      GNM_EXPR_OP_EQUAL,
				      gnm_expr_new_constant (value_new_string (""))),
		 gnm_expr_new_constant (value_new_string ("")),
		 gnm_expr_new_funcall1 (fd_sum,
					gnm_expr_new_funcall3 
					(fd_if,
					 gnm_expr_new_binary
					 (gnm_expr_copy (expr_data),
					  GNM_EXPR_OP_LT,
					  make_cellref (-1, 0)),
					 gnm_expr_new_constant (value_new_int (0)),
					 gnm_expr_new_constant (value_new_int (1)))));

	if (info->censored) {
		expr_censor = gnm_expr_new_constant (value_dup (info->base.range_2));
		expr_deaths = gnm_expr_new_funcall3 
			(fd_if,
			 gnm_expr_new_binary (make_cellref (-1, 0),
					      GNM_EXPR_OP_EQUAL,
					      gnm_expr_new_constant (value_new_string (""))),
			 gnm_expr_new_constant (value_new_string ("")),
			 gnm_expr_new_funcall1 (fd_sum,
						gnm_expr_new_binary
						(gnm_expr_new_funcall3 
						 (fd_if,
						  gnm_expr_new_binary
						  (gnm_expr_copy (expr_data),
						   GNM_EXPR_OP_EQUAL,
						   make_cellref (-2, 0)),
						  gnm_expr_new_constant (value_new_int (1)),
						  gnm_expr_new_constant (value_new_int (0))),
						 GNM_EXPR_OP_MULT,
						 gnm_expr_new_funcall3 
						 (fd_if,
						  gnm_expr_new_binary
						  (expr_censor,
						   GNM_EXPR_OP_EQUAL,
						   gnm_expr_new_constant (value_new_int (info->censor_mark))),
						  gnm_expr_new_constant (value_new_int (0)),
						  gnm_expr_new_constant (value_new_int (1))))));
	} else
		expr_deaths = gnm_expr_new_funcall3 
			(fd_if,
			 gnm_expr_new_binary (make_cellref (-1, 0),
					      GNM_EXPR_OP_EQUAL,
					      gnm_expr_new_constant (value_new_string (""))),
			 gnm_expr_new_constant (value_new_string ("")),
			 gnm_expr_new_funcall1 (fd_sum,
						gnm_expr_new_funcall3 
						(fd_if,
						 gnm_expr_new_binary
						 (gnm_expr_copy (expr_data),
						  GNM_EXPR_OP_EQUAL,
						  make_cellref (-2, 0)),
						 gnm_expr_new_constant (value_new_int (1)),
						 gnm_expr_new_constant (value_new_int (0)))));
		
	expr_small = gnm_expr_new_funcall2 (fd_small, 
					    gnm_expr_new_funcall3
					    (fd_if,
					     gnm_expr_new_binary
					     (gnm_expr_copy (expr_data),
					      GNM_EXPR_OP_GT,
					      make_cellref (0, -1)),
					     expr_data,
					     gnm_expr_new_constant (value_new_string ("N/A"))),
					    gnm_expr_new_constant (value_new_int (1)));
	expr_time = gnm_expr_new_funcall3 (fd_if,
					   gnm_expr_new_funcall1 (fd_iserror, 
								  gnm_expr_copy (expr_small)),
					   gnm_expr_new_constant (value_new_string ("")),
					   expr_small);
	
	expr_prob_zero = gnm_expr_new_binary (gnm_expr_new_binary (make_cellref (-2, 0),
								   GNM_EXPR_OP_SUB,
								   make_cellref (-1, 0)),
					      GNM_EXPR_OP_DIV,
					      make_cellref (-2, 0));
	expr_prob = gnm_expr_new_funcall3 (fd_if,
					   gnm_expr_new_binary
					   (make_cellref (-1, 0),
					    GNM_EXPR_OP_EQUAL,
					    gnm_expr_new_constant (value_new_string (""))),
					   gnm_expr_new_constant (value_new_string ("")),
					   gnm_expr_new_binary (gnm_expr_copy (expr_prob_zero),
								GNM_EXPR_OP_MULT,
								make_cellref (0, -1)));

	if (info->std_err) {
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
									     (gnm_expr_new_constant (value_new_int (1)),
									      GNM_EXPR_OP_SUB,
									      make_cellref (-1, 0)),
									     GNM_EXPR_OP_DIV,
									     make_cellref (-3, 0)))));

		dao_set_format  (dao, 4, 2, 4, rows + 1, "0.0000");
		dao_set_cell_expr (dao, 4, 2, gnm_expr_copy (expr_std_err));
	}

	dao_set_format  (dao, 3, 2, 3, rows + 1, "0.00%");

	dao_set_cell_int (dao, 0, 2, 0);
	dao_set_cell_array_expr (dao, 1, 2, gnm_expr_copy (expr_at_risk)); 	
	dao_set_cell_array_expr (dao, 2, 2, gnm_expr_copy (expr_deaths)); 	
	dao_set_cell_expr (dao, 3, 2, expr_prob_zero);

	for (row = 1; row < rows; row++) {
		dao_set_cell_array_expr (dao, 0, 2+row, gnm_expr_copy (expr_time)); 	
		dao_set_cell_array_expr (dao, 1, 2+row, gnm_expr_copy (expr_at_risk)); 	
		dao_set_cell_array_expr (dao, 2, 2+row, gnm_expr_copy (expr_deaths)); 	
		dao_set_cell_array_expr (dao, 3, 2+row, gnm_expr_copy (expr_prob)); 
		if (info->std_err)
			dao_set_cell_expr (dao, 4, 2+row, gnm_expr_copy (expr_std_err));
	}

	gnm_expr_free (expr_time);
	gnm_expr_free (expr_at_risk);
	gnm_expr_free (expr_deaths);
	gnm_expr_free (expr_prob);
	if (expr_std_err != NULL)
		gnm_expr_free (expr_std_err);
	
	gnm_func_unref (fd_small);
	gnm_func_unref (fd_if);
	gnm_func_unref (fd_iserror);
	gnm_func_unref (fd_sum);
	if (fd_sqrt != NULL)
		gnm_func_unref (fd_sqrt);

	/* Create Chart if requested */
	if (info->chart) {
		SheetObject *so;
		GogGraph     *graph;
		GogChart     *chart;
		GogPlot	     *plot;
		GogSeries    *series;
		GOData *times;
		GOData *probabilities;
		GogStyle  *style;
		
		graph = g_object_new (GOG_GRAPH_TYPE, NULL);
		chart = GOG_CHART (gog_object_add_by_name (
						   GOG_OBJECT (graph), "Chart", NULL));

		plot = gog_plot_new_by_name ("GogXYPlot");
		go_object_set_property (G_OBJECT (plot), "interpolation",
						"Default interpolation", "step-start",
						NULL, NULL);

		gog_object_add_by_name (GOG_OBJECT (chart),
					"Plot", GOG_OBJECT (plot));
		
		times = dao_go_data_vector (dao, 0, 2, 0, 1+rows);
		probabilities = dao_go_data_vector (dao, 3, 2, 3, 1+rows);
			
		series = gog_plot_new_series (plot);
		gog_series_set_dim (series, 0, times, NULL);
		gog_series_set_dim (series, 1, probabilities, NULL);

		style = gog_styled_object_get_style (GOG_STYLED_OBJECT (series));
		style->marker.auto_shape = FALSE;
		go_marker_set_shape (style->marker.mark, GO_MARKER_NONE);
		gog_styled_object_set_style (GOG_STYLED_OBJECT (series), style);

		so = sheet_object_graph_new (graph);
		g_object_unref (graph);
		
		dao_set_sheet_object (dao, 0, 1, so);
	}

	dao_redraw_respan (dao);

	return FALSE;
}


gboolean
analysis_tool_kaplan_meier_engine (data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_kaplan_meier_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, 
						_("Kaplan-Meier (%s)"),
						result) 
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		dao_adjust (dao, info->std_err ? 5 : 4, 
			    info->base.range_1->v_range.cell.b.row 
			    - info->base.range_1->v_range.cell.a.row + 3);
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		return analysis_tool_generic_b_clean (specs);
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return FALSE;
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Kaplan-Meier Estimates"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Kaplan-Meier Estimates"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_kaplan_meier_engine_run (dao, specs);
	}
	return TRUE;
}




