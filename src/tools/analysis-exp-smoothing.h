/*
 * analysis-histogram.h:
 *
 * This is a complete reimplementation of the exponential smoothing tool in 2008
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


#ifndef GNM_TOOLS_ANALYSIS_EXP_SMOOTHING_H_
#define GNM_TOOLS_ANALYSIS_EXP_SMOOTHING_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

typedef enum {
	exp_smoothing_type_ses_h = 0,
	exp_smoothing_type_ses_r,
	exp_smoothing_type_des,
	exp_smoothing_type_ates,
	exp_smoothing_type_mtes
} exponential_smoothing_type_t;

#define GNM_TYPE_EXP_SMOOTHING_TOOL (gnm_exp_smoothing_tool_get_type ())
GType gnm_exp_smoothing_tool_get_type (void);
typedef struct _GnmExpSmoothingTool GnmExpSmoothingTool;
typedef struct _GnmExpSmoothingToolClass GnmExpSmoothingToolClass;

struct _GnmExpSmoothingTool {
	GnmGenericAnalysisTool parent;
	gnm_float damp_fact;
	gnm_float g_damp_fact;
	gnm_float s_damp_fact;
	int s_period;
	int std_error_flag;
	int df;
	gboolean show_graph;
	exponential_smoothing_type_t es_type;
};

struct _GnmExpSmoothingToolClass {
	GnmGenericAnalysisToolClass parent_class;
};

#define GNM_EXP_SMOOTHING_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_EXP_SMOOTHING_TOOL, GnmExpSmoothingTool))
#define GNM_IS_EXP_SMOOTHING_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_EXP_SMOOTHING_TOOL))

GnmAnalysisTool *gnm_exp_smoothing_tool_new (void);


#endif
