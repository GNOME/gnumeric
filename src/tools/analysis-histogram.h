/*
 * analysis-histogram.h:
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


#ifndef ANALYSIS_HISTOGRAM_H
#define ANALYSIS_HISTOGRAM_H

#include <gnumeric.h>
#include <numbers.h>
#include <tools/dao.h>
#include <tools/tools.h>
#include <tools/analysis-tools.h>

typedef enum {
	bintype_no_inf_lower = 0,
	bintype_no_inf_upper = 1 << 0,
	bintype_p_inf_lower  = 1 << 1,
	bintype_p_inf_upper  = bintype_no_inf_upper | bintype_p_inf_lower,
	bintype_m_inf_lower  = 1 << 2,
	bintype_m_inf_upper  = bintype_m_inf_lower | bintype_no_inf_upper,
	bintype_pm_inf_lower = bintype_m_inf_lower | bintype_p_inf_lower,
	bintype_pm_inf_upper = bintype_m_inf_lower | bintype_p_inf_lower | bintype_no_inf_upper
} analysis_histogram_bin_type_t;

typedef enum {
	NO_CHART = 0,
	HISTOGRAM_CHART,
	BAR_CHART,
	COLUMN_CHART
} chart_t;


typedef struct {
	analysis_tools_data_generic_t base;
	gboolean   predetermined;
	GnmValue   *bin;
	analysis_histogram_bin_type_t   bin_type;
	gboolean   max_given;
	gboolean   min_given;
	gnm_float max;
	gnm_float min;
	gint       n;
	gboolean   percentage;
	gboolean   cumulative;
	gboolean   only_numbers;
	chart_t   chart;
} analysis_tools_data_histogram_t;

gboolean analysis_tool_histogram_engine (GOCmdContext *gcc, data_analysis_output_t *dao, gpointer specs,
					   analysis_tool_engine_t selector, gpointer result);

#endif
