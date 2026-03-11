/*
 * analysis-chi-squared.h:
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


#ifndef GNM_TOOLS_ANALYSIS_CHI_SQUARED_H_
#define GNM_TOOLS_ANALYSIS_CHI_SQUARED_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

typedef struct {
	WorkbookControl *wbc;
	GnmValue        *input;
	gboolean         labels;
	gboolean         independence;
	gnm_float        alpha;
	gint             n_c;
	gint             n_r;
} analysis_tools_data_chi_squared_t;

#define GNM_TYPE_CHI_SQUARED_TOOL (gnm_chi_squared_tool_get_type ())
GType gnm_chi_squared_tool_get_type (void);
typedef struct _GnmChiSquaredTool GnmChiSquaredTool;
typedef struct _GnmChiSquaredToolClass GnmChiSquaredToolClass;

struct _GnmChiSquaredTool {
	GnmAnalysisTool parent;
	analysis_tools_data_chi_squared_t data;
};

struct _GnmChiSquaredToolClass {
	GnmAnalysisToolClass parent_class;
};

#define GNM_CHI_SQUARED_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_CHI_SQUARED_TOOL, GnmChiSquaredTool))
#define GNM_IS_CHI_SQUARED_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_CHI_SQUARED_TOOL))

GnmAnalysisTool *gnm_chi_squared_tool_new (void);


#endif
