/*
 * analysis-anova.h:
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


#ifndef GNM_TOOLS_ANALYSIS_ANOVA_H_
#define GNM_TOOLS_ANALYSIS_ANOVA_H_

#include <gnumeric.h>
#include <numbers.h>
#include <tools/analysis-tools.h>

/****************  2-Factor ANOVA  ***************/
/*** with contingency table like data provision **/

#define GNM_TYPE_ANOVA_TWO_FACTOR_TOOL (gnm_anova_two_factor_tool_get_type ())
GType gnm_anova_two_factor_tool_get_type (void);
typedef struct _GnmAnovaTwoFactorTool GnmAnovaTwoFactorTool;
typedef struct _GnmAnovaTwoFactorToolClass GnmAnovaTwoFactorToolClass;

struct _GnmAnovaTwoFactorTool {
	GnmAnalysisTool parent;

	analysis_tools_error_code_t err;
	WorkbookControl *wbc;
	GnmValue     *input;
	group_by_t group_by;
	gboolean   labels;
	gnm_float alpha;
	gint       replication;
	gint       rows;
	gint       n_c;
	gint       n_r;
};

struct _GnmAnovaTwoFactorToolClass {
	GnmAnalysisToolClass parent_class;
};

#define GNM_ANOVA_TWO_FACTOR_TOOL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_TYPE_ANOVA_TWO_FACTOR_TOOL, GnmAnovaTwoFactorTool))
#define GNM_IS_ANOVA_TWO_FACTOR_TOOL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_TYPE_ANOVA_TWO_FACTOR_TOOL))

GnmAnalysisTool *gnm_anova_two_factor_tool_new (void);


#endif
