/*
 * analysis-regression.h:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2008 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 * (C) Copyright 2026 by Morten Welinder <terra@gnome.org>
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


#ifndef GNM_TOOLS_ANALYSIS_REGRESSION_H_
#define GNM_TOOLS_ANALYSIS_REGRESSION_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

#define GNM_TYPE_REGRESSION_TOOL (gnm_regression_tool_get_type ())
GType gnm_regression_tool_get_type (void);
typedef struct _GnmRegressionTool GnmRegressionTool;
typedef struct _GnmRegressionToolClass GnmRegressionToolClass;

struct _GnmRegressionTool {
	GnmGenericBAnalysisTool parent;
	gnm_tool_group_by_t group_by;
	gboolean   intercept;
	gboolean   multiple_regression;
        gboolean   multiple_y;
        gboolean   residual;
	GSList    *indep_vars;
};

struct _GnmRegressionToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_REGRESSION_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_REGRESSION_TOOL, GnmRegressionTool))
#define GNM_IS_REGRESSION_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_REGRESSION_TOOL))

GnmAnalysisTool *gnm_regression_tool_new (void);

#endif
