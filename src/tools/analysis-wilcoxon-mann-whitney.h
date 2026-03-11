/*
 * analysis-wilcoxon-mann-whitney.h:
 *
 * Author:
 *   Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2010 by Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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


#ifndef GNM_TOOLS_ANALYSIS_WILCOXON_MANN_WHITNEY_H_
#define GNM_TOOLS_ANALYSIS_WILCOXON_MANN_WHITNEY_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

/**************** Wilcoxon-Mann-Whitney Test ***************/

#define GNM_TYPE_WILCOXON_MANN_WHITNEY_TOOL (gnm_wilcoxon_mann_whitney_tool_get_type ())
GType gnm_wilcoxon_mann_whitney_tool_get_type (void);
typedef struct _GnmWilcoxonMannWhitneyTool GnmWilcoxonMannWhitneyTool;
typedef struct _GnmWilcoxonMannWhitneyToolClass GnmWilcoxonMannWhitneyToolClass;

struct _GnmWilcoxonMannWhitneyTool {
	GnmGenericBAnalysisTool parent;
};

struct _GnmWilcoxonMannWhitneyToolClass {
	GnmGenericBAnalysisToolClass parent_class;
};

#define GNM_WILCOXON_MANN_WHITNEY_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_WILCOXON_MANN_WHITNEY_TOOL, GnmWilcoxonMannWhitneyTool))
#define GNM_IS_WILCOXON_MANN_WHITNEY_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_WILCOXON_MANN_WHITNEY_TOOL))

GnmAnalysisTool *gnm_wilcoxon_mann_whitney_tool_new (void);


#endif
